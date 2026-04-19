Hier ist das **komplette Paket** für **ESP8266 + UDP + Protobuf**, inklusive:

* `.proto`
* `led.options` für nanopb
* **Arduino-Sketch** für den ESP8266 als Access Point
* **Python-Client** zum Setzen der LED oder Abfragen des Status

## 1) `led.proto`

```proto
syntax = "proto3";

message LedCommand {
  bool has_led_on = 1;
  bool led_on = 2;
}

message LedStatus {
  bool led_on = 1;
  string message = 2;
}
```

## 2) `led.options`

Damit nanopb für `message` ein festes Char-Array erzeugt:

```txt
LedStatus.message max_size:32
```

## 3) Dateien generieren

Auf deinem PC:

```bash
protoc --nanopb_out=. led.proto
protoc --python_out=. led.proto
```

Danach hast du typischerweise:

* `led.pb.h`
* `led.pb.c`
* `led_pb2.py`

---

# 4) Arduino-Programm für ESP8266

Datei: `esp8266_led_udp_protobuf.ino`

```cpp
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
```

---

# 5) Python-Programm

Datei: `led_control.py`

```python
import socket
import argparse
import sys
import led_pb2

DEFAULT_IP = "192.168.4.1"
DEFAULT_PORT = 5000
DEFAULT_TIMEOUT = 2.0


def send_command(ip: str, port: int, timeout: float, mode: str, state: str | None) -> int:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(timeout)

    cmd = led_pb2.LedCommand()

    if mode == "status":
        cmd.has_led_on = False
    elif mode == "set":
        cmd.has_led_on = True
        cmd.led_on = (state == "on")
    else:
        print(f"Unbekannter Modus: {mode}", file=sys.stderr)
        return 2

    payload = cmd.SerializeToString()

    try:
        sock.sendto(payload, (ip, port))
        data, addr = sock.recvfrom(1024)
    except socket.timeout:
        print("Keine Antwort vom ESP", file=sys.stderr)
        sock.close()
        return 1
    except OSError as exc:
        print(f"Netzwerkfehler: {exc}", file=sys.stderr)
        sock.close()
        return 1

    sock.close()

    status = led_pb2.LedStatus()
    try:
        status.ParseFromString(data)
    except Exception as exc:
        print(f"Antwort konnte nicht dekodiert werden: {exc}", file=sys.stderr)
        return 1

    print(f"Antwort von {addr[0]}:{addr[1]}")
    print(f"LED: {'AN' if status.led_on else 'AUS'}")
    print(f"Message: {status.message}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="ESP8266 LED Steuerung per UDP + Protobuf"
    )

    parser.add_argument(
        "mode",
        choices=["set", "status"],
        help="set = LED setzen, status = Status abfragen"
    )

    parser.add_argument(
        "state",
        nargs="?",
        choices=["on", "off"],
        help="nur bei mode=set erforderlich"
    )

    parser.add_argument("--ip", default=DEFAULT_IP, help="ESP IP-Adresse")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help="UDP Port")
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT, help="Timeout in Sekunden")

    args = parser.parse_args()

    if args.mode == "set" and args.state is None:
        parser.error("bei 'set' muss state 'on' oder 'off' angegeben werden")

    if args.mode == "status" and args.state is not None:
        parser.error("bei 'status' darf kein state angegeben werden")

    return send_command(args.ip, args.port, args.timeout, args.mode, args.state)


if __name__ == "__main__":
    raise SystemExit(main())
```

---

# 6) Verwendung

## LED einschalten

```bash
python led_control.py set on
```

## LED ausschalten

```bash
python led_control.py set off
```

## Nur Status abfragen

```bash
python led_control.py status
```

## Mit anderer IP

```bash
python led_control.py status --ip 192.168.4.1
```

---

# 7) Was du im Arduino-Projekt brauchst

In deinem Sketch-Ordner oder als Bibliothek:

* `esp8266_led_udp_protobuf.ino`
* `led.pb.h`
* `led.pb.c`
* nanopb:

  * `pb.h`
  * `pb_common.h`
  * `pb_common.c`
  * `pb_encode.h`
  * `pb_encode.c`
  * `pb_decode.h`
  * `pb_decode.c`

---

# 8) Ablauf

1. ESP8266-Sketch kompilieren und hochladen
2. Mit dem WLAN `ESP8266-Protobuf-AP` verbinden
3. Python-Script auf dem Client-Rechner ausführen
4. Befehle per UDP senden

---

# 9) Erwartete Antworten

Bei `python led_control.py set on` zum Beispiel:

```text
Antwort von 192.168.4.1:5000
LED: AN
Message: led set on
```

Bei `python led_control.py status`:

```text
Antwort von 192.168.4.1:5000
LED: AUS
Message: status
```

---

# 10) Hinweis zur ESP8266-LED

Bei vielen ESP8266-Boards ist die eingebaute LED:

* an `GPIO2`
* **active low**

Falls deine LED invertiert reagiert, ändere:

```cpp
const bool LED_ACTIVE_LOW = true;
```

auf:

```cpp
const bool LED_ACTIVE_LOW = false;
```

Ich kann dir daraus auch noch eine Version machen, die **ohne Protobuf** nur mit einfachem UDP-Text wie `SET ON`, `SET OFF`, `STATUS` arbeitet.
