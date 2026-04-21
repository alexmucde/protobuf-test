
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#define DLT_TCP_PORT 3490

// ...existing code...

extern "C" {
  #include "pb_decode.h"
}
#include "led.pb.h"

const char* apSsid = "LED_Server";
const char* apPassword = "12345678";

WiFiUDP udp;
const uint16_t udpPort = 4210;

const int ledPin = LED_BUILTIN;




#define MAX_CLIENTS 8
struct ClientInfo {
  IPAddress ip;
  uint16_t port;
};
ClientInfo clients[MAX_CLIENTS];
uint8_t clientCount = 0;

WiFiServer dltServer(DLT_TCP_PORT);
WiFiClient dltClients[MAX_CLIENTS];

// Hilfsfunktion: Prüfe, ob Client schon bekannt ist
int findClient(IPAddress ip, uint16_t port) {
  for (uint8_t i = 0; i < clientCount; ++i) {
    if (clients[i].ip == ip && clients[i].port == port) return i;
  }
  return -1;
}

// Hilfsfunktion: Füge neuen Client hinzu (falls nicht vorhanden)
void addClient(IPAddress ip, uint16_t port) {
  if (findClient(ip, port) == -1) {
    if (clientCount < MAX_CLIENTS) {
      clients[clientCount].ip = ip;
      clients[clientCount].port = port;
      clientCount++;
    } else {
      // FIFO: Ältesten überschreiben
      for (uint8_t i = 1; i < MAX_CLIENTS; ++i) clients[i-1] = clients[i];
      clients[MAX_CLIENTS-1].ip = ip;
      clients[MAX_CLIENTS-1].port = port;
    }
  }
}

// Hilfsfunktion: DLT non-verbose UDP Nachricht senden
void sendDLTMessage(bool ledState) {
  // DLT Standard Header (16 Byte, non-verbose, TCP, minimal)
  static uint8_t dlt_counter = 0;
  uint8_t payload = ledState ? 0x01 : 0x00;
  const char* stateStr = ledState ? "LED state: AN" : "LED state: AUS";
  uint16_t strLen = strlen(stateStr) + 1; // include null terminator
  // DLT TCP marker: 'D','L','T',0x01
  // Standard header: 4 marker + 2 len + 1 counter + 1 type + 4 ECU + 4 session + 4 timestamp + 4 msgid
  // Extended header: 1 info + 1 nargs + 4 APID + 4 CTID
  // Arguments: 1 string (type 0x31, 2 bytes len, value), 1 UINT8 (0x23, 1 byte value)
  uint16_t stdHeaderLen = 4 + 2 + 1 + 1 + 4 + 4 + 4 + 4;
  uint16_t extHeaderLen = 1 + 1 + 4 + 4;
  uint16_t arg1Len = 1 + 2 + strLen; // type + strlen + value (with null)
  uint16_t arg2Len = 1 + 1; // type + value
  uint16_t totalLen = stdHeaderLen + extHeaderLen + arg1Len + arg2Len;
  uint8_t dltMsg[128];
  int idx = 0;
  // Marker
  dltMsg[idx++] = 'D'; dltMsg[idx++] = 'L'; dltMsg[idx++] = 'T'; dltMsg[idx++] = 0x01;
  // Length (little endian)
  dltMsg[idx++] = totalLen & 0xFF;
  dltMsg[idx++] = (totalLen >> 8) & 0xFF;
  // Counter
  dltMsg[idx++] = dlt_counter++;
  // Header type: 0x01 = verbose, 0x10 = ECU present, 0x40 = session, 0x80 = timestamp
  dltMsg[idx++] = 0x01 | 0x10 | 0x40 | 0x80;
  // ECU ID
  dltMsg[idx++] = 'E'; dltMsg[idx++] = 'C'; dltMsg[idx++] = 'U'; dltMsg[idx++] = '1';
  // Session ID (4 bytes, can be 0)
  dltMsg[idx++] = 0x00; dltMsg[idx++] = 0x00; dltMsg[idx++] = 0x00; dltMsg[idx++] = 0x00;
  // Timestamp (4 bytes, can be 0)
  dltMsg[idx++] = 0x00; dltMsg[idx++] = 0x00; dltMsg[idx++] = 0x00; dltMsg[idx++] = 0x00;
  // Message ID (4 bytes, e.g., 1000, little endian)
  dltMsg[idx++] = 0xE8; dltMsg[idx++] = 0x03; dltMsg[idx++] = 0x00; dltMsg[idx++] = 0x00;
  // Extended header
  dltMsg[idx++] = 0x14; // Info: log info, type info: INFO, log
  dltMsg[idx++] = 0x02; // Number of arguments (string + uint8)
  dltMsg[idx++] = 'L'; dltMsg[idx++] = 'E'; dltMsg[idx++] = 'D'; dltMsg[idx++] = ' ';
  dltMsg[idx++] = 'S'; dltMsg[idx++] = 'T'; dltMsg[idx++] = 'A'; dltMsg[idx++] = 'T';
  // Argument 1: String
  dltMsg[idx++] = 0x31; // typeinfo: string
  dltMsg[idx++] = strLen & 0xFF; // string length (little endian)
  dltMsg[idx++] = (strLen >> 8) & 0xFF;
  memcpy(&dltMsg[idx], stateStr, strLen);
  idx += strLen;
  // Argument 2: UINT8
  dltMsg[idx++] = 0x23; // typeinfo: UINT8
  dltMsg[idx++] = payload;
  // Send to all connected TCP clients
  for (uint8_t i = 0; i < MAX_CLIENTS; ++i) {
    if (dltClients[i] && dltClients[i].connected()) {
      dltClients[i].write(dltMsg, idx);
    }
  }
}

void setLed(bool on) {
  // ESP8266 onboard LED meist active-low
  digitalWrite(ledPin, on ? LOW : HIGH);
  sendDLTMessage(on);
}

void setup() {
  dltServer.begin();
  Serial.print("DLT TCP Server lauscht auf Port ");
  Serial.println(DLT_TCP_PORT);
  Serial.begin(115200);
  delay(200);

  pinMode(ledPin, OUTPUT);
  setLed(false);

  WiFi.mode(WIFI_AP);

  if (WiFi.softAP(apSsid, apPassword)) {
    Serial.println();
    Serial.println("Access Point gestartet");
    Serial.print("SSID: ");
    Serial.println(apSsid);
    Serial.print("IP: ");
    Serial.println(WiFi.softAPIP());
  } else {
    Serial.println("Fehler beim Starten des Access Points");
  }

  udp.begin(udpPort);
  Serial.print("UDP lauscht auf Port ");
  Serial.println(udpPort);
}

void loop() {
  // Accept new DLT TCP clients
  if (dltServer.hasClient()) {
    for (uint8_t i = 0; i < MAX_CLIENTS; ++i) {
      if (!dltClients[i] || !dltClients[i].connected()) {
        if (dltClients[i]) dltClients[i].stop();
        dltClients[i] = dltServer.available();
        Serial.print("Neuer DLT TCP-Client verbunden: ");
        Serial.println(dltClients[i].remoteIP());
        break;
      }
    }
    WiFiClient rejectClient = dltServer.available();
    if (rejectClient) rejectClient.stop();
  }
  int packetSize = udp.parsePacket();
  if (packetSize <= 0) {
    return;
  }

  // Merke Client
  IPAddress clientIP = udp.remoteIP();
  uint16_t clientPort = udp.remotePort();
  addClient(clientIP, clientPort);

  uint8_t buffer[64];
  int len = udp.read(buffer, sizeof(buffer));
  if (len <= 0) {
    return;
  }

  LedCommand cmd = LedCommand_init_zero;
  pb_istream_t stream = pb_istream_from_buffer(buffer, len);

  if (pb_decode(&stream, LedCommand_fields, &cmd)) {
    setLed(cmd.state);

    Serial.print("LED gesetzt auf: ");
    Serial.println(cmd.state ? "AN" : "AUS");

    udp.beginPacket(clientIP, clientPort);
    udp.write("OK", 2);
    udp.endPacket();
  } else {
    Serial.println("Fehler beim Dekodieren von Protobuf");
  }
}
