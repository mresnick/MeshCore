#include <Arduino.h>
#include <Wire.h>
#include "nrf_gpio.h"

#include "RAK4631_13302Board.h"

/* ------------------------------------------------------------------------
 * Parking the RAK4630's built-in SX1262
 *
 * On this board the LoRa traffic goes through the RAK13302 in the IO slot, but
 * the RAK4630 stamp still carries its own SX1262 wired to P1.10..P1.15. That
 * radio has no power switch of its own - it hangs off the module's always-on
 * 3V3 rail - so after a reset it sits in STDBY_RC burning ~600uA forever.
 *
 * SetSleep with sleepConfig = 0 (cold start, RTC wake-up disabled) drops it to
 * the datasheet's ~160nA sleep current. It only leaves sleep on an NSS falling
 * edge or a reset, and nothing else in this firmware touches those pins, so a
 * single shot at boot is enough. nRF52 GPIO state survives SYSTEMOFF, so the
 * radio also stays asleep through deep sleep.
 *
 * This is bit-banged rather than done through the SPI peripheral on purpose:
 * the shared SPI object is later re-pointed at the RAK13302's pins by
 * CustomSX1262::std_init(), and a one-shot two-byte command at boot has no
 * reason to fight over it.
 * ------------------------------------------------------------------------ */

#define SX126X_CMD_SET_SLEEP        0x84
#define SX126X_SLEEP_COLD_START     0x00   // cold start, RTC wake-up off
#define SX126X_CMD_READ_REGISTER    0x1D
#define SX126X_REG_LORA_SYNC_WORD   0x0740 // reads back 0x1424 on a freshly reset part

static uint8_t onboardRadioTransfer(uint8_t out) {
  uint8_t in = 0;
  for (int i = 7; i >= 0; i--) {   // SPI mode 0, MSB first
    digitalWrite(ONBOARD_LORA_MOSI, (out >> i) & 0x01);
    digitalWrite(ONBOARD_LORA_SCLK, HIGH);
    in = (in << 1) | (digitalRead(ONBOARD_LORA_MISO) ? 1 : 0);
    digitalWrite(ONBOARD_LORA_SCLK, LOW);
  }
  return in;
}

#if MESH_DEBUG
// Readings kept for the boot-complete report, once the USB serial link is up:
// the BUSY line changing across the SetSleep command is the observable evidence
// that the radio acted on it, rather than something we have to take on faith.
static uint8_t onboard_sleep_status = 0;
static uint8_t onboard_busy_before = 0;
static uint8_t onboard_busy_after = 0;
static uint16_t onboard_sync_word = 0;
#endif

static void parkOnboardRadio() {
  // Cut power to the RAK4630's antenna switch first. This is only the switch,
  // not the SX1262 - the radio still has to be put to sleep over SPI below.
  pinMode(ONBOARD_RADIO_ANT_PWR, OUTPUT);
  digitalWrite(ONBOARD_RADIO_ANT_PWR, LOW);

  digitalWrite(ONBOARD_LORA_NSS, HIGH);  // preload OUT latch so pinMode can't glitch NSS low
  pinMode(ONBOARD_LORA_NSS, OUTPUT);
  digitalWrite(ONBOARD_LORA_NSS, HIGH);
  pinMode(ONBOARD_LORA_SCLK, OUTPUT);
  digitalWrite(ONBOARD_LORA_SCLK, LOW);
  pinMode(ONBOARD_LORA_MOSI, OUTPUT);
  digitalWrite(ONBOARD_LORA_MOSI, LOW);
  pinMode(ONBOARD_LORA_MISO, INPUT);
  pinMode(ONBOARD_LORA_BUSY, INPUT);

  // Hardware reset, so we know the radio is in STDBY_RC whatever it was doing
  // before (a warm restart can find it still in RX).
  digitalWrite(ONBOARD_LORA_RESET, HIGH); // preload OUT latch so pinMode can't glitch NRESET low
  pinMode(ONBOARD_LORA_RESET, OUTPUT);
  digitalWrite(ONBOARD_LORA_RESET, LOW);  // datasheet: >= 100us
  delay(1);
  digitalWrite(ONBOARD_LORA_RESET, HIGH);

  uint32_t started_at = millis();
  while (digitalRead(ONBOARD_LORA_BUSY) && millis() - started_at < 20) { } // wait for radio to be ready

#if MESH_DEBUG
  onboard_busy_before = digitalRead(ONBOARD_LORA_BUSY);   // STDBY_RC and ready -> expect LOW

  // Read a register with a known reset value, to tell a working SPI link apart
  // from a floating MISO that happens to return a plausible-looking pattern.
  digitalWrite(ONBOARD_LORA_NSS, LOW);
  delayMicroseconds(2);
  onboardRadioTransfer(SX126X_CMD_READ_REGISTER);
  onboardRadioTransfer((SX126X_REG_LORA_SYNC_WORD >> 8) & 0xFF);
  onboardRadioTransfer(SX126X_REG_LORA_SYNC_WORD & 0xFF);
  onboardRadioTransfer(0x00);                             // status byte
  onboard_sync_word  = (uint16_t)onboardRadioTransfer(0x00) << 8;
  onboard_sync_word |= onboardRadioTransfer(0x00);
  delayMicroseconds(2);
  digitalWrite(ONBOARD_LORA_NSS, HIGH);

  started_at = millis();
  while (digitalRead(ONBOARD_LORA_BUSY) && millis() - started_at < 20) { }
#endif

  digitalWrite(ONBOARD_LORA_NSS, LOW);
  delayMicroseconds(2);
  uint8_t status = onboardRadioTransfer(SX126X_CMD_SET_SLEEP);
  onboardRadioTransfer(SX126X_SLEEP_COLD_START);
  delayMicroseconds(2);
  digitalWrite(ONBOARD_LORA_NSS, HIGH);   // sleep is entered on the rising edge of NSS
  delay(1);                               // RadioLib allows the same settling time (~500us per datasheet)

#if MESH_DEBUG
  onboard_sleep_status = status;
  onboard_busy_after = digitalRead(ONBOARD_LORA_BUSY);
#endif
  (void) status;

  // MISO and BUSY are not driven while the radio sleeps - disconnect their input
  // buffers rather than leave them floating. NSS/RESET stay driven HIGH and
  // SCLK/MOSI stay driven LOW, which is what keeps the radio asleep.
  nrf_gpio_cfg_default(g_ADigitalPinMap[ONBOARD_LORA_MISO]);
  nrf_gpio_cfg_default(g_ADigitalPinMap[ONBOARD_LORA_BUSY]);
}

#ifdef NRF52_POWER_MANAGEMENT
// Static configuration for power management
// Values set in variant.h defines
const PowerMgtConfig power_config = {
  .lpcomp_ain_channel = PWRMGT_LPCOMP_AIN,
  .lpcomp_refsel = PWRMGT_LPCOMP_REFSEL,
  .voltage_bootlock = PWRMGT_VOLTAGE_BOOTLOCK
};

void RAK4631_13302Board::initiateShutdown(uint8_t reason) {
  // Disable SKY66122 FEM (CSD+CPS LOW = shutdown, <1 uA)
  digitalWrite(SX126X_POWER_EN, LOW);

  // Disable 3V3 switched peripherals and the RAK13302 5V boost
  digitalWrite(PIN_3V3_EN, LOW);

  if (reason == SHUTDOWN_REASON_LOW_VOLTAGE ||
      reason == SHUTDOWN_REASON_BOOT_PROTECT) {
    configureVoltageWake(power_config.lpcomp_ain_channel, power_config.lpcomp_refsel);
  }

  enterSystemOff(reason);
}
#endif // NRF52_POWER_MANAGEMENT

#if MESH_DEBUG
void RAK4631_13302Board::onBootComplete() {
  // Re-check BUSY now that the RAK13302 has been initialised and is receiving:
  // still high means nothing since boot has disturbed the sleeping radio.
  // Reading BUSY is safe - only an NSS falling edge or a reset can wake it.
  pinMode(ONBOARD_LORA_BUSY, INPUT);
  uint8_t busy_now = digitalRead(ONBOARD_LORA_BUSY);
  nrf_gpio_cfg_default(g_ADigitalPinMap[ONBOARD_LORA_BUSY]);

  MESH_DEBUG_PRINTLN("onboard SX1262 parked: sync_word=0x%04X (expect 0x1424), status=0x%02X, BUSY %u -> %u, still %u",
                     onboard_sync_word, onboard_sleep_status, onboard_busy_before, onboard_busy_after, busy_now);
}
#endif

void RAK4631_13302Board::begin() {
  NRF52BoardDCDC::begin();

  // Get the unused stamp radio out of the way first, so it is never left
  // idling while the rest of the board comes up.
  parkOnboardRadio();

  pinMode(PIN_VBAT_READ, INPUT);
#ifdef PIN_USER_BTN
  pinMode(PIN_USER_BTN, INPUT_PULLUP);
#endif

#ifdef PIN_USER_BTN_ANA
  pinMode(PIN_USER_BTN_ANA, INPUT_PULLUP);
#endif

#if defined(PIN_BOARD_SDA) && defined(PIN_BOARD_SCL)
  Wire.setPins(PIN_BOARD_SDA, PIN_BOARD_SCL);
#endif

  Wire.begin();

  // PIN_3V3_EN (WB_IO2, P1.02) controls the 3V3_S switched peripheral rail
  // AND the 5V boost regulator (U5) on the RAK13302 that powers the SKY66122 PA.
  // Must stay HIGH during radio operation - do not toggle for power saving.
  pinMode(PIN_3V3_EN, OUTPUT);
  digitalWrite(PIN_3V3_EN, HIGH);

  // Enable SKY66122-11 FEM on the RAK13302 module.
  // CSD and CPS are tied together on the RAK13302 PCB, routed to IO3 (P0.21).
  // HIGH = FEM active (LNA for RX, PA path available for TX).
  // TX/RX switching (CTX) is handled by SX1262 DIO2 via SetDIO2AsRfSwitchCtrl.
  pinMode(SX126X_POWER_EN, OUTPUT);
#ifdef NRF52_POWER_MANAGEMENT
  // Boot voltage protection check (may not return if voltage too low)
  // We need to call this after we configure SX126X_POWER_EN as output but before we pull high
  checkBootVoltage(&power_config);
#endif
  digitalWrite(SX126X_POWER_EN, HIGH);
  delay(1);  // SKY66122 turn-on settling time (tON = 3us typ)
}
