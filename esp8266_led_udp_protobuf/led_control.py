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