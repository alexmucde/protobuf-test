#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

extern "C" {
  #include "pb_encode.h"
}
#include "led.pb.h"

const char* ssid = "LED_Server";
const char* password = "12345678";

IPAddress serverIP(192, 168, 4, 1);
const uint16_t serverPort = 4210;
const uint16_t localPort = 4211;

WiFiUDP udp;

// Taster an D5 gegen GND
const int buttonPin = D3;

bool ledState = false;
bool lastReading = HIGH;
bool stableState = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;

void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(buttonPin, INPUT_PULLUP);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  Serial.print("Verbinde mit ");
  Serial.println(ssid);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WLAN verbunden");
  Serial.print("Client IP: ");
  Serial.println(WiFi.localIP());

  udp.begin(localPort);
  Serial.print("Lokaler UDP Port: ");
  Serial.println(localPort);
}

void sendCommand(bool state) {
  uint8_t buffer[64];

  LedCommand cmd = LedCommand_init_zero;
  cmd.state = state;

  pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));

  if (!pb_encode(&stream, LedCommand_fields, &cmd)) {
    Serial.println("Fehler beim Kodieren von Protobuf");
    return;
  }

  udp.beginPacket(serverIP, serverPort);
  udp.write(buffer, stream.bytes_written);
  udp.endPacket();

  Serial.print("Gesendet: LED ");
  Serial.println(state ? "AN" : "AUS");
}

void loop() {
  bool reading = digitalRead(buttonPin);

  if (reading != lastReading) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading != stableState) {
      stableState = reading;

      if (stableState == LOW) {
        ledState = !ledState;
        sendCommand(ledState);
      }
    }
  }

  lastReading = reading;

  int packetSize = udp.parsePacket();
  if (packetSize > 0) {
    char reply[16] = {0};
    int len = udp.read(reply, sizeof(reply) - 1);
    if (len > 0) {
      Serial.print("Antwort vom Server: ");
      Serial.println(reply);
    }
  }
}
