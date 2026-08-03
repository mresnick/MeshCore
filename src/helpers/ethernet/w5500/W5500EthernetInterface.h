#pragma once

#include "../SerialEthernetInterface.h"
#include <SPI.h>
#include <ETH.h>
#include <WiFi.h>
#include <WiFiServer.h>
#include <WiFiClient.h>

// W5500-over-SPI, using arduino-esp32's native ETH.h (arduino-esp32 3.x required).
// WiFiServer/WiFiClient are aliases of NetworkServer/NetworkClient there, so they
// work unchanged over the wired interface.
#ifndef ETH_SPI_HOST
  #define ETH_SPI_HOST SPI3_HOST
#endif
#ifndef ETH_PHY_ADDR
  #define ETH_PHY_ADDR 1
#endif

class W5500EthernetInterface : public SerialEthernetInterface {

  bool _isConnected;
  WiFiServer server;
  WiFiClient client;

  public:
    W5500EthernetInterface() {
      _isConnected = false;
    }

    bool begin();
    void loop() override;

    // BaseSerialInterface methods
    bool isConnected() const override;

    int available() override;
    int read() override;
    size_t write(const uint8_t *buf, size_t size) override;
};
