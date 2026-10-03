"""
webap_preview.py
================
Preview of the board's setup page (webAP) on the PC, fed by the simulated PM2200.
Serves the real HTML out of src/web_server.cpp (edit the page, refresh the browser)
and emulates the board's endpoints, so what you see is what the board will serve.

Usage:
    python webap_preview.py                  # http://localhost:8080   (discovery page: /discover)
    python webap_preview.py --real-meter     # leave out the fields whose register address is still unknown
    python webap_preview.py --port 9000 --interval 2 --wh-start 0
    python webap_preview.py --check          # run the self-check and exit
"""

import argparse, json, os, re, sys, threading, time
from collections import deque
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

import sim_meter

# ── Config ────────────────────────────────────────────────────────────────────
ROOT       = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
WEB_SERVER = os.path.join(ROOT, "src", "web_server.cpp")
CONFIG_H   = os.path.join(ROOT, "include", "config.h")
MAP_H      = os.path.join(ROOT, "include", "pm2200_map.h")

MAC        = "A4:CF:12:AB:EE:FF"
IP         = "192.168.0.50"
# ─────────────────────────────────────────────────────────────────────────────

# Preview only: the real discovery page scans the LAN, so show the simulated board as a found device
DISCOVER_DEMO = """<script>
window.addEventListener('load', async () => {
  const grid = document.getElementById('grid');
  const info = await (await fetch('/device_info')).json();
  const power = await (await fetch('/power')).json();
  grid.innerHTML = '';
  grid.appendChild(renderCard(location.host, info, power));
  grid.appendChild(renderCard('192.168.0.51', { device_name: 'ESP32-A1B2C3D4E5F6', device_id: 'A1:B2:C3:D4:E5:F6', firmware: '2.1.0' }, null));
  setStatus('Preview: 2 demo devices', 'done');
});
</script>
</body>"""


def read(path):
    with open(path, "rb") as f:
        return f.read().decode("utf-8")


def page(name):
    """HTML of a raw-string page in web_server.cpp, read fresh so edits show on refresh."""
    m = re.search(r'const char %s\[\] PROGMEM = R"rawliteral\((.*?)\)rawliteral";' % name,
                  read(WEB_SERVER).replace("\r\n", "\n"), re.S)
    if not m:
        sys.exit("cannot find %s in %s" % (name, WEB_SERVER))
    return m.group(1)


def firmware_version():
    m = re.search(r'#define\s+FIRMWARE_VERSION\s+"([^"]+)"', read(CONFIG_H))
    return m.group(1) if m else "?"


def unknown_fields():
    """Payload fields whose register address is still PM2200_REG_UNKNOWN in pm2200_map.h."""
    return set(re.findall(r"PM2200_REG_UNKNOWN,\s*&Pm2200Reading::(\w+)", read(MAP_H)))


class Board:
    """Emulated board: the simulated meter polled every `interval` s, plus the settings and log."""

    def __init__(self, interval=sim_meter.INTERVAL, real_meter=False, wh=1234567):
        self.interval = interval
        self.omit = unknown_fields() if real_meter else set()
        self.meter = sim_meter.Meter(wh=wh)
        self.settings = {"addr": 1, "baud": 9600, "parity": "E", "sim": True}
        self.reading, self.poll = None, 0
        self.logs, self.seq = deque(maxlen=40), 0
        self.lock = threading.Lock()
        for line in ("[Boot] WiFi OK — IP: " + IP, "[LocalMQTT] connected weedsync.local:1883",
                     "[LocalMQTT] topic sensors/PM_EEFF/power", "[PM2200] simulated addr 1 9600 E"):
            self.log(line)
        self.tick()

    def log(self, line):
        self.logs.append(line)
        self.seq += 1

    def tick(self):
        with self.lock:
            self.poll += 1
            if self.settings["sim"]:
                self.reading = self.meter.step(self.interval)
                self.log("[LocalMQTT] Published to sensors/PM_EEFF/power (%d bytes)"
                         % len(sim_meter.payload(self.reading)))
            else:
                self.log("[RS485] timeout — no response (meter missing / wiring / baud?)")

    def run(self):
        while True:
            time.sleep(self.interval)
            self.tick()

    def power_json(self):
        """Same shape as the board's GET /power (and the MQTT reading object)."""
        with self.lock:
            r, sim = self.reading, self.settings["sim"]
            ok = sim and r is not None
            head = '{"sensor_ok":%s,"meter_ok":%s,"simulated":%s,"status":"%s","poll":%d' % (
                str(ok).lower(), str(ok).lower(), str(sim).lower(),
                "simulated" if sim else "no response", self.poll)
            body = "".join(',"%s":%s' % (k, f % r[k]) for k, f in sim_meter.FIELDS
                           if ok and k not in self.omit)
            return head + body + "}"

    def meter_json(self):
        s = self.settings
        return json.dumps({"addr": s["addr"], "baud": s["baud"], "parity": s["parity"], "sim": s["sim"]})

    def set_meter(self, form):
        """Same validation as the board's POST /set_meter; returns (status code, body)."""
        try:
            addr, baud = int(form.get("addr", ["0"])[0]), int(form.get("baud", ["0"])[0])
        except ValueError:
            addr, baud = 0, 0
        parity = form.get("parity", [""])[0]
        if not 1 <= addr <= 247 or baud not in (4800, 9600, 19200, 38400) or parity not in ("E", "O", "N"):
            return 400, '{"status":"error","message":"Invalid address, baud or parity"}'
        with self.lock:
            self.settings = {"addr": addr, "baud": baud, "parity": parity,
                             "sim": form.get("sim", ["0"])[0] == "1"}
            self.reading = None                      # never show values from the previous data source
            self.log("[PM2200] %s addr %d %d %s" % ("simulated" if self.settings["sim"] else "meter", addr, baud, parity))
        self.tick()
        return 200, '{"status":"ok"}'

    def device_info(self):
        mdns = "bossfarm-" + MAC.replace(":", "")[8:].lower() + ".local"
        return json.dumps({"device_id": MAC, "device_name": "ESP32-" + MAC.replace(":", ""), "mdns": mdns,
                           "ip": IP, "rssi": -58, "firmware": firmware_version(), "commissioned": True})

    def logs_json(self):
        return json.dumps({"seq": self.seq, "lines": list(self.logs)})


def make_handler(board):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *args):                 # the page polls every 2 s — keep the terminal quiet
            pass

        def send(self, code, body, ctype="application/json"):
            data = body.encode("utf-8")
            self.send_response(code)
            self.send_header("Content-Type", ctype + "; charset=utf-8")
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            self.wfile.write(data)

        def do_GET(self):
            path = urlparse(self.path).path
            routes = {
                "/power":         lambda: self.send(200, board.power_json()),
                "/meter":         lambda: self.send(200, board.meter_json()),
                "/device_info":   lambda: self.send(200, board.device_info()),
                "/wifi":          lambda: self.send(200, '{"ssid":"FarmNet","connected":true}'),
                "/logs":          lambda: self.send(200, board.logs_json()),
                "/broker_status": lambda: self.send(200, '{"connected":true,"ip":"weedsync.local","port":1883}'),
                "/scan":          lambda: self.send(200, '[{"ssid":"FarmNet","rssi":-48,"secure":true},'
                                                         '{"ssid":"Guest","rssi":-71,"secure":false}]'),
            }
            if path == "/":
                self.send(200, page("HTML_PAGE"), "text/html")
            elif path == "/discover":
                self.send(200, page("DISCOVER_PAGE").replace("</body>", DISCOVER_DEMO), "text/html")
            elif path in routes:
                routes[path]()
            else:
                self.send(404, '{"status":"error","message":"not found"}')

        def do_POST(self):
            path = urlparse(self.path).path
            form = parse_qs(self.rfile.read(int(self.headers.get("Content-Length", 0))).decode("utf-8"))
            if path == "/set_meter":
                self.send(*board.set_meter(form))
            elif path == "/set_wifi":
                self.send(200, '{"ok":true,"ip":"%s","mdns":"bossfarm-eeff.local"}' % IP)
            elif path == "/factory_reset":
                self.send(200, "OK", "text/plain")
            else:
                self.send(404, '{"status":"error","message":"not found"}')

    return Handler


def check():
    b = Board(real_meter=False)
    full = json.loads(b.power_json())
    assert full["meter_ok"] and full["simulated"] and full["p_total_w"] > 0 and full["poll"] == 1
    assert [k for k in full if k in dict(sim_meter.FIELDS)] == [k for k, _ in sim_meter.FIELDS]

    b2 = Board(real_meter=True)
    real = json.loads(b2.power_json())
    unknown = unknown_fields()
    assert unknown and not (unknown & set(real)), "unknown-address fields must be left out"
    assert "p_total_w" in real and "energy_wh" in real

    status, _ = b.set_meter({"addr": ["1"], "baud": ["9600"], "parity": ["E"], "sim": ["0"]})
    off = json.loads(b.power_json())
    assert status == 200 and not off["meter_ok"] and "p_total_w" not in off, "no meter -> no values"
    assert b.set_meter({"addr": ["0"], "baud": ["9600"], "parity": ["E"]})[0] == 400

    html = page("HTML_PAGE")
    assert 'id="meter-rows"' in html and 'id="chart-power"' in html and "function renderMeter" in html
    assert "renderCard(ip, info, power)" in page("DISCOVER_PAGE")
    print("check OK: %d keys full, %d left out for unknown registers (%s)"
          % (len(full) - 5, len(unknown), ", ".join(sorted(unknown))))


def main():
    ap = argparse.ArgumentParser(description="Preview of the PM2200 setup page with simulated data")
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--interval", type=float, default=sim_meter.INTERVAL, help="seconds between meter polls")
    ap.add_argument("--wh-start", type=float, default=1234567, help="initial active energy delivered, Wh")
    ap.add_argument("--real-meter", action="store_true", help="leave out fields whose register address is unknown")
    ap.add_argument("--check", action="store_true", help="run the self-check and exit")
    a = ap.parse_args()
    if a.check:
        return check()

    board = Board(a.interval, a.real_meter, a.wh_start)
    threading.Thread(target=board.run, daemon=True).start()
    server = ThreadingHTTPServer(("127.0.0.1", a.port), make_handler(board))
    print("Setup page preview : http://localhost:%d/" % a.port)
    print("Discovery preview  : http://localhost:%d/discover" % a.port)
    print("Meter polled every %.0f s%s — Ctrl+C to stop" % (a.interval, " (unknown-address fields left out)" if a.real_meter else ""))
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
