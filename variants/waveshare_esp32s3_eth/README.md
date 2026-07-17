# Waveshare ESP32-S3-ETH + Core1262 (SX1262)

Companion radio / repeater / room server on a [Waveshare ESP32-S3-ETH](https://www.waveshare.com/wiki/ESP32-S3-ETH)
(ESP32-S3R8, 16MB flash, onboard W5500 SPI Ethernet, optional PoE module for power),
with an external [Waveshare Core1262](https://www.waveshare.com/wiki/Core1262-868M) LoRa module.

The `companion_radio_eth` build serves the standard companion frame protocol on TCP
port 5000 over wired Ethernet (DHCP by default; static IP via the `ETH_STATIC_*`
build flags in `platformio.ini`).

## Wiring: Core1262 to ESP32-S3-ETH header

| Core1262 pin (silkscreen) | ESP32-S3 GPIO | Note |
|---------------------------|---------------|------|
| CLK (SCK)                 | 38            | SPI clock |
| MISO                      | 39            |      |
| MOSI                      | 40            |      |
| CS (NSS)                  | 41            | SPI chip select |
| RST (RESET)               | 42            |      |
| BUSY                      | 47            |      |
| DIO1                      | 48            |      |
| RXEN                      | 15            | RF switch RX enable |
| TXEN         | — (jumper to module DIO2) | DIO2 drives the TX side of the RF switch (`SX126X_DIO2_AS_RF_SWITCH`) |
| VCC          | 3V3           |      |
| GND          | GND           |      |

Alternatively, wire TXEN to a spare GPIO and replace `SX126X_DIO2_AS_RF_SWITCH=true`
with `-D SX126X_TXEN=<gpio>`.

These GPIOs are only otherwise used by the (unpopulated) camera connector, so the
TF card slot and all onboard peripherals keep working. Do **not** move any signal to
GPIO 9–14 (W5500), 4–7 (TF card), 33–37 (octal PSRAM), 19/20 (USB), 43/44 (UART0),
21 (WS2812 RGB LED, used as the TX indicator), or strapping pins 0/45/46.

I2C for an optional RTC/sensors is on SDA=16 / SCL=17 (the ESP32-S3 default of
SDA=8/SCL=9 would clash with the W5500 reset line).
