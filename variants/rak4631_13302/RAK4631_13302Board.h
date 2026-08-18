#pragma once

#include <MeshCore.h>
#include <Arduino.h>
#include <helpers/NRF52Board.h>

// built-ins
#define  PIN_VBAT_READ    5
#define  ADC_MULTIPLIER   (3 * 1.73 * 1.187 * 1000)

/*
 * RAK19007 base + RAK4631 core + RAK13302 (SX1262 + SKY66122 1W FEM) in the IO slot.
 *
 * Same radio wiring as the RAK3401 host board, with one extra job: the SX1262
 * that lives inside the RAK4630 stamp is unused here, so begin() parks it in
 * SLEEP instead of leaving it idling in STDBY_RC.
 */
class RAK4631_13302Board : public NRF52BoardDCDC {
protected:
#ifdef NRF52_POWER_MANAGEMENT
  void initiateShutdown(uint8_t reason) override;
#endif

public:
  RAK4631_13302Board() : NRF52Board("RAK4631_OTA") {}
  void begin();

#if MESH_DEBUG
  // Reports how the onboard radio was parked, late enough in boot for the
  // USB serial link to be up and the message to actually be seen.
  void onBootComplete() override;
#endif

  #define BATTERY_SAMPLES 8

  uint16_t getBattMilliVolts() override {
    analogReadResolution(12);

    uint32_t raw = 0;
    for (int i = 0; i < BATTERY_SAMPLES; i++) {
      raw += analogRead(PIN_VBAT_READ);
    }
    raw = raw / BATTERY_SAMPLES;

    return (ADC_MULTIPLIER * raw) / 4096;
  }

  const char* getManufacturerName() const override {
    return "RAK 4631 + 13302";
  }

  // TX/RX switching is handled by SX1262 DIO2 -> SKY66122 CTX (hardware-timed).
  // No onBeforeTransmit/onAfterTransmit overrides needed.
};
