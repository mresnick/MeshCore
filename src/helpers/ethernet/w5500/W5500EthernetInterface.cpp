#include "W5500EthernetInterface.h"

static void onEthEvent(arduino_event_id_t event, arduino_event_info_t info) {
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      ETHERNET_DEBUG_PRINTLN("Ethernet Started");
      break;
    case ARDUINO_EVENT_ETH_CONNECTED:
      ETHERNET_DEBUG_PRINTLN("Ethernet Connected");
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      ETHERNET_DEBUG_PRINTLN("Ethernet Disconnected");
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      ETHERNET_DEBUG_PRINTLN("Ethernet Got IP");
      ETHERNET_DEBUG_PRINT_IP("IP Address", ETH.localIP());
      ETHERNET_DEBUG_PRINT_IP("Subnet Mask", ETH.subnetMask());
      ETHERNET_DEBUG_PRINT_IP("Gateway", ETH.gatewayIP());
      ETHERNET_DEBUG_PRINT_IP("DNS", ETH.dnsIP());
      ETHERNET_DEBUG_PRINTLN("MAC Address: %s", ETH.macAddress().c_str());
      break;
    default:
      break;
  }
}

bool W5500EthernetInterface::begin() {

  // listen to ethernet events
  Network.onEvent(onEthEvent);

  if (!ETH.begin(ETH_PHY_W5500, ETH_PHY_ADDR, ETH_CS_PIN, ETH_INT_PIN, ETH_RST_PIN,
                 ETH_SPI_HOST, ETH_SCLK_PIN, ETH_MISO_PIN, ETH_MOSI_PIN)) {
    ETHERNET_DEBUG_PRINTLN("Failed to initialize W5500 hardware.");
    return false;
  }

  // Setup Static IP if build flags are present
  #if defined(ETHERNET_STATIC_IP) && defined(ETHERNET_STATIC_GATEWAY) && defined(ETHERNET_STATIC_SUBNET)
    IPAddress ip(ETHERNET_STATIC_IP);
    IPAddress gw(ETHERNET_STATIC_GATEWAY);
    IPAddress sn(ETHERNET_STATIC_SUBNET);
    #if defined(ETHERNET_STATIC_DNS)
      IPAddress dns(ETHERNET_STATIC_DNS);
    #else
      IPAddress dns = gw;
    #endif
    ETH.config(ip, gw, sn, dns);
  #endif

  // Start Server
  server.begin(ETHERNET_TCP_PORT);
  ETHERNET_DEBUG_PRINTLN("listening on TCP port: %d", ETHERNET_TCP_PORT);

  return true;
}

int W5500EthernetInterface::available() {
  return client.available();
}

int W5500EthernetInterface::read() {
  return client.read();
}

size_t W5500EthernetInterface::write(const uint8_t *buf, size_t size) {
  return client.write(buf, size);
}

bool W5500EthernetInterface::isConnected() const {
  return _isConnected;
}

void W5500EthernetInterface::loop() {

  if (server.hasClient()) {
    auto newClient = server.available();
    if (newClient) {
      IPAddress remoteIp = newClient.remoteIP();
      uint16_t remotePort = newClient.remotePort();
      ETHERNET_DEBUG_PRINTLN("New client accepted %u.%u.%u.%u:%u", remoteIp[0], remoteIp[1], remoteIp[2], remoteIp[3], remotePort);
      if (client) {
        ETHERNET_DEBUG_PRINTLN("Closing previous client");
        client.stop();
      }
      client = newClient;
      onClientConnected();
    }
  }

  _isConnected = client.connected();
}
