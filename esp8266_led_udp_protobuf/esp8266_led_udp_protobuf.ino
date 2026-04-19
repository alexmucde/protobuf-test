#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

extern "C" {
  #include "pb.h"
  #include "pb_encode.h"
  #include "pb_decode.h"
  #include "led.pb.h"
}

const char* ssid = "ESP8266-Protobuf-AP";
const char* password = "12345678";

const uint16_t UDP_PORT = 5000;

// Bei vielen ESP8266-Boards ist GPIO2 die Onboard-LED.
// Oft active low: LOW = an, HIGH = aus
const int LED_PIN = 2;
const bool LED_ACTIVE_LOW = true;

WiFiUDP udp;

bool ledState = false;
uint8_t rxBuffer[128];
uint8_t txBuffer[128];

void setLed(bool on) {
  ledState = on;

  if (LED_ACTIVE_LOW) {
    digitalWrite(LED_PIN, on ? LOW : HIGH);
  } else {
    digitalWrite(LED_PIN, on ? HIGH : LOW);
  }
}

bool decodeLedCommand(const uint8_t* data, size_t len, LedCommand* cmd) {
  pb_istream_t stream = pb_istream_from_buffer(data, len);
  if (!pb_decode(&stream, LedCommand_fields, cmd)) {
    Serial.print("pb_decode Fehler: ");
    Serial.println(PB_GET_ERROR(&stream));
    return false;
  }
  return true;
}

bool encodeLedStatus(const LedStatus* status, uint8_t* outBuf, size_t outSize, size_t* outLen) {
  pb_ostream_t stream = pb_ostream_from_buffer(outBuf, outSize);
  if (!pb_encode(&stream, LedStatus_fields, status)) {
    Serial.print("pb_encode Fehler: ");
    Serial.println(PB_GET_ERROR(&stream));
    return false;
  }
  *outLen = stream.bytes_written;
  return true;
}

void sendStatus(IPAddress remoteIp, uint16_t remotePort, bool state, const char* text) {
  LedStatus status = LedStatus_init_zero;
  status.led_on = state;

  strncpy(status.message, text, sizeof(status.message) - 1);
  status.message[sizeof(status.message) - 1] = '\0';

  size_t encodedLen = 0;
  if (!encodeLedStatus(&status, txBuffer, sizeof(txBuffer), &encodedLen)) {
    return;
  }

  udp.beginPacket(remoteIp, remotePort);
  udp.write(txBuffer, encodedLen);
  udp.endPacket();
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(LED_PIN, OUTPUT);
  setLed(false);

  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);

  bool ok = WiFi.softAP(ssid, password, 1, false, 4);

  Serial.println();
  if (!ok) {
    Serial.println("SoftAP Start fehlgeschlagen");
    return;
  }

  Serial.println("Access Point gestartet");
  Serial.print("SSID: ");
  Serial.println(ssid);
  Serial.print("Passwort: ");
  Serial.println(password);
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());

  udp.begin(UDP_PORT);
  Serial.print("UDP Server gestartet auf Port ");
  Serial.println(UDP_PORT);
}

void loop() {
  int packetSize = udp.parsePacket();
  if (packetSize <= 0) {
    delay(1);
    return;
  }

  IPAddress remoteIp = udp.remoteIP();
  uint16_t remotePort = udp.remotePort();

  if (packetSize > (int)sizeof(rxBuffer)) {
    while (udp.available()) {
      udp.read();
    }
    sendStatus(remoteIp, remotePort, ledState, "packet too large");
    return;
  }

  int len = udp.read(rxBuffer, sizeof(rxBuffer));
  if (len <= 0) {
    return;
  }

  LedCommand cmd = LedCommand_init_zero;

  if (!decodeLedCommand(rxBuffer, len, &cmd)) {
    sendStatus(remoteIp, remotePort, ledState, "decode error");
    return;
  }

  if (cmd.has_led_on) {
    setLed(cmd.led_on);
    sendStatus(remoteIp, remotePort, ledState, ledState ? "led set on" : "led set off");
  } else {
    sendStatus(remoteIp, remotePort, ledState, "status");
  }
}
