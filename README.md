# IES-WI-C6A x BossFarm — PM2200 Power Meter Firmware

![ESP32-C6](https://img.shields.io/badge/ESP32--C6-DevKitC--1-blue?style=flat-square)
![PlatformIO](https://img.shields.io/badge/PlatformIO-Arduino-orange?style=flat-square)
![PM2200](https://img.shields.io/badge/Schneider-EasyLogic_PM2200-3dcd58?style=flat-square)
![Modbus](https://img.shields.io/badge/Modbus-RTU_RS485-00a6d6?style=flat-square)
![NeoPixel](https://img.shields.io/badge/NeoPixel-WS2812B-purple?style=flat-square)
![Local MQTT](https://img.shields.io/badge/Local_MQTT-Mosquitto-green?style=flat-square)
![Firmware](https://img.shields.io/badge/Firmware-v1.0.0-lightgrey?style=flat-square)

---
ESP32-C6 firmware for the **IES-WI-C6A** board that reads a **Schneider EasyLogic PM2200** three-phase power meter over RS485 (Modbus RTU) and publishes voltage, current, power and energy over WiFi/MQTT to the BossFarm Raspberry Pi, for the online dashboard. This firmware has one job: there are no other sensors in it.

```
PM2200 --RS485 / Modbus RTU--> IES-WI-C6A (this firmware) --WiFi / MQTT--> Raspberry Pi
                                                           (Mosquitto -> bridge -> InfluxDB -> dashboard)
```

> **Status:** built and tested against a *simulated* meter only. No real PM2200 has been connected yet and the Modbus register addresses are unverified. See [Status and known limitations](#status-and-known-limitations).

---

## What it does

- Polls the meter every 5 s and publishes the readings to the Pi's MQTT broker: voltage L-N and L-L, current (per phase, average, neutral), active / reactive / apparent power (per phase and total), power factor, frequency, unbalance, and cumulative energy delivered and received.
- Serves a setup page on the board: WiFi commissioning, live readings (tables and charts), Modbus settings, device log, factory reset. A discovery page lists the boards on the network with a power / energy summary for each.
- Has a **simulation mode** that generates realistic meter values, so the whole board → WiFi → MQTT path can run with no meter attached.
- Keeps the commissioning model, NeoPixel status ring and factory-reset behaviour of the earlier BossFarm sensor firmware.

---

## Hardware and wiring

Board: **IES-WI-C6A** (ESP32-C6, MAX3485 RS485 transceiver already on the board). No extra hardware.

| Function | Pin |
|---|---|
| RS485 TX (UART1, to MAX3485 DI) | GPIO16 |
| RS485 RX (UART1, from MAX3485 RO) | GPIO17 |
| RS485 DE + RE (HIGH = transmit, LOW = receive) | GPIO14 |
| NeoPixel status ring (12 x WS2812B) | GPIO20 |
| Factory-reset button (hold 5 s) | GPIO5 |

The console is USB-CDC at 115200 baud, so the UART0 pads are free for RS485.

**RS485 cabling** (from the PM2200 manual, `docs/PM2200-datasheet.PDF`):

- One RS485 port per meter, Modbus RTU, up to 32 devices on a bus, 1000 m maximum.
- Shielded cable with 2 twisted pairs (or 1.5): one pair for the (+) and (-) data lines, the other wire for the **C** (common) terminals. Connect (+) to (+) and (-) to (-); if the meter does not answer, try swapping the pair.
- Connect the shield wire to the shield terminal and ground it at one end only.
- Terminate both ends of the bus with 120 ohm.

**Meter settings** (set on the meter's front panel, *Maint > Setup > Comm*; the manual does not state the factory defaults, so read them from the meter):

| Setting | Allowed values |
|---|---|
| Slave address | 1 to 247 |
| Baud rate | 4800, 9600, 19200, 38400 |
| Parity | Even or Odd (1 stop bit), None (2 stop bits) |

Enter the same values in the board's setup page (*Meter Settings*). We only read from the meter; its configuration is the installer's.

---

## Registering a board (step by step)

1. **Flash the firmware** (see [Build and flash](#build-and-flash)).
2. **Power on.** The NeoPixel ring goes white (booting), cyan (hotspot up), then blue (not commissioned).
3. **Join the board's hotspot**: SSID `PM2200_Hotspot_XXXX` (last 4 characters of the MAC), password `AP_PASSWORD` from `include/secrets.h`.
4. **Open `http://192.168.4.1`** (most phones open it through the captive portal).
5. In the **WiFi Connection** card, press **Scan Networks**, pick the site network, enter the password and press **Connect**.
6. On success a confirmation screen shows the board's IP and `.local` name. The hotspot turns off and the board joins the network and connects to the MQTT broker. The LED goes red (no WiFi), amber (WiFi, no broker), then green (broker connected).
7. Open `http://<ip>` or `http://bossfarm-<last4>.local` and set **Meter Settings**. Choose **Data source: PM2200 meter (RS485)** for a real meter.

The board is now commissioned. The hotspot only comes back if WiFi is lost for more than 30 s, or after a factory reset.

**MQTT broker.** The default is `weedsync.local:1883`. To use another address (stored in NVS, kept across factory resets):

```bash
curl -X POST http://<board-ip>/set_broker -d "ip=192.168.0.16" -d "port=1883"
```

**LED states**

| Colour | Meaning |
|---|---|
| White | Booting / factory reset done (2 s before reboot) |
| Cyan | Hotspot up, starting |
| Blue | Not commissioned |
| Red | Commissioned, no WiFi |
| Amber | WiFi up, MQTT broker not connected |
| Green | WiFi and broker connected, data flowing |
| Yellow | Factory-reset button held |

---

## Setup page

Served by the board at `/` (hotspot: `192.168.4.1`, network: its IP or `bossfarm-<last4>.local`):

| Card | Content |
|---|---|
| Device Information | Device ID, firmware, IP, hostname, signal, network status |
| WiFi Connection | Scan, connect |
| Power Meter | Active power, energy (Wh), power factor and frequency tiles; per-phase table (L1, L2, L3, total) for V L-N, V L-L, current, neutral current, kW, kvar, kVA, PF; energy table (active, reactive, apparent; delivered and received); power and current charts (last 60 polls); a **SIMULATED** badge when the values are generated |
| Meter Settings | Slave address, baud rate, parity, data source (meter or simulated) |
| Device Log | Live log of the board |
| Factory Reset | Clears WiFi credentials and commissioning |

`/discover` scans a subnet (default `192.168.0.1-254`) for ESP32 units and shows each one's power, energy and mode; a card opens that unit's page.

| Route | Purpose |
|---|---|
| `GET /power` | Live readings as JSON (same keys as the MQTT `reading`, plus status) |
| `GET /meter`, `POST /set_meter` | Modbus settings (`addr`, `baud`, `parity`, `sim`) |
| `GET /device_info`, `GET /wifi`, `GET /scan`, `POST /set_wifi` | Device info and WiFi commissioning |
| `POST /register` | Connect with the stored credentials and start MQTT |
| `POST /set_broker`, `GET /broker_status` | MQTT broker address and connection state |
| `GET /logs`, `POST /factory_reset`, `GET /discover` | Log, reset, discovery page |

---

## MQTT

| Topic | Retained | Interval |
|---|---|---|
| `sensors/PM_<last4mac>/power` | no | 5 s |
| `sensors/PM_<last4mac>/attributes` | yes | 60 s and after each reconnect |

`PM_<last4mac>` is the device ID: `PM_` plus the last 4 hex characters of the MAC. The broker has no authentication and the client ID is `ESP32C6-<MAC>`.

```json
{
  "device_id": "PM_ABCD",
  "reading": {
    "sensor_type": 4,
    "sensor_type_label": "power",
    "firmware": "1.0.0",
    "rssi": -60,
    "sensor_ok": true,
    "meter_ok": true,
    "simulated": false,
    "v1": 230.1, "v2": 229.8, "v3": 230.4,
    "i1": 12.30, "i2": 11.90, "i3": 12.60,
    "p1": 2650, "p2": 2540, "p3": 2700, "p_total_w": 7890,
    "energy_wh": 1234567
  }
}
```

The example is abridged: the full payload has 39 measurement keys (about 730 bytes). The complete key list, units and rules are in **[docs/payload.md](docs/payload.md)**. Points worth knowing:

- Keys without a value are **left out** (never `null`): a value not read yet, or whose register address is still unknown.
- `simulated: true` marks generated values, so a dashboard can tell them from real consumption.
- The board never sets its clock, so `timestamp` is omitted and the Pi bridge stamps server time on receipt.
- On a Modbus failure the last good values are kept; `sensor_ok` goes false after 3 consecutive failures.
- The Raspberry Pi bridge lives outside this repository. It has to subscribe to `sensors/+/power` (new topic, same envelope as the other sensors) for the data to reach InfluxDB.

---

## Simulation mode and PC tools

The board starts in **simulation mode** (`PM2200_SIM_DEFAULT` in `include/config.h`) because no meter is available yet: it publishes generated values flagged `simulated: true`. Switch **Data source** in the setup page to use a real meter; the choice is stored in NVS.

`tools/pm2200_sim/` runs on a PC with plain Python 3 (standard library only; on Windows use `py`):

```bash
# Print the exact MQTT payload every 5 s
python tools/pm2200_sim/sim_meter.py
python tools/pm2200_sim/sim_meter.py --wh-start 5000 --interval 2

# Preview the board's setup page with simulated data: http://localhost:8080
python tools/pm2200_sim/webap_preview.py            # discovery page: http://localhost:8080/discover
python tools/pm2200_sim/webap_preview.py --real-meter   # leave out values whose register is unknown

# Self-checks
python tools/pm2200_sim/sim_meter.py --check
python tools/pm2200_sim/webap_preview.py --check
```

The preview serves the real HTML out of `src/web_server.cpp` and emulates the board's routes, so editing the page and refreshing the browser shows what the board will serve.

---

## Configuration (`include/config.h`)

| Define | Purpose |
|---|---|
| `AP_SSID` | Hotspot name prefix (`PM2200_Hotspot`, plus the last 4 MAC characters) |
| `FIRMWARE_VERSION` | Firmware version, shown in the page and the MQTT payload |
| `RS485_TX_PIN`, `RS485_RX_PIN`, `RS485_DE_PIN` | RS485 pins |
| `RS485_TIMEOUT_MS`, `RS485_POLL_INTERVAL`, `RS485_MAX_FAILS` | Transaction timeout (400 ms), poll interval (5 s), failures before `sensor_ok` goes false (3) |
| `PM2200_ADDR_DEFAULT`, `PM2200_BAUD_DEFAULT`, `PM2200_PARITY_DEFAULT` | Meter settings used until they are saved from the page |
| `PM2200_SIM_DEFAULT` | `true` = start in simulation mode |
| `LOCAL_MQTT_SERVER`, `LOCAL_MQTT_PORT` | Default broker (`weedsync.local:1883`) |
| `MDNS_PREFIX` | mDNS name prefix (`bossfarm`) |

`include/pm2200_map.h` holds the PM2200 register table, the read blocks, the float word order and the address base.

---

## Build and flash

```bash
cp include/secrets.h.example include/secrets.h     # Windows: copy ...; then set AP_PASSWORD

pio run -e esp32c6                                 # build
pio run -e esp32c6 --target upload                 # flash
pio device monitor --baud 115200                   # serial console (USB-CDC)
```

`include/secrets.h` is gitignored: keep real values out of git and never ship binaries built with real credentials. `HOME_SSID` and `HOME_PASSWORD` are only used if `DEV_MODE` is defined (it is not). The build uses the pioarduino platform (arduino-esp32 3.x) with the `huge_app.csv` partition table; see `platformio.ini`. Flash offsets: bootloader `0x0`, partitions `0x8000`, boot_app0 `0xe000`, application `0x10000`.

The `flash_tool/` folder still holds the earlier project's flasher; see [Status](#status-and-known-limitations).

---

## Repository layout

```
src/
  main.cpp              setup/loop, NeoPixel status, WiFi fallback, 5 s publish timer
  local_mqtt.cpp        MQTT to the Pi broker: power payload, retained attributes
  web_server.cpp        HTTP server, captive portal, setup and discovery pages, /power ...
  wifi_config.cpp       WiFi credentials (NVS), connect, scan
  factory_reset.cpp     GPIO5 hold detection, NVS reset
  provisioning.cpp      legacy ThingsBoard provisioning (unused)
  sensors/
    rs485_sensor.cpp    Modbus RTU transport: UART1, DE/RE, CRC-16, FC 0x03
    pm2200.cpp / .h     PM2200 driver: register decode, polling, simulation, settings
include/                headers, config.h, pm2200_map.h (register table), secrets.h.example
tools/pm2200_sim/       payload simulator and setup-page preview (Python)
docs/                   payload contract, register map, PM2200 manual, proposal
test/native/            native unit tests
flash_tool/             flasher (still the earlier project's, see Status)
```

**NVS namespaces:** `netcfg` (WiFi `ssid`, `pass`), `device` (`commissioned`), `broker` (`host`, `port`), `pm2200` (`addr`, `baud`, `parity`, `sim`). A factory reset (hold GPIO5 for 5 s, or the page's button) clears the WiFi credentials and the commissioning flag; it keeps the broker address and the meter settings.

---

## Testing

```bash
pio test -e test        # native unit tests (WiFi credential validation, legacy provisioning parser)
```

The native tests need a host C/C++ compiler (gcc or clang). GitHub Actions (`.github/workflows/test.yml`) runs them and builds the firmware with `secrets.h.example`. The Python self-checks are listed under [Simulation mode and PC tools](#simulation-mode-and-pc-tools).

---

## Documentation

| File | Content |
|---|---|
| [docs/payload.md](docs/payload.md) | MQTT topics and payload contract, key table, units, rules |
| [docs/pm2200_registers.md](docs/pm2200_registers.md) | Register map with verified / unverified status |
| [docs/PM2200-datasheet.PDF](docs/PM2200-datasheet.PDF) | Schneider EasyLogic PM2200 user manual (NHA2778902-11) |
| [docs/PM2200_proposal.md](docs/PM2200_proposal.md) | Implementation proposal |

---

## Status and known limitations

- **Register addresses are unverified.** The PM2200 manual has no Modbus register map. The addresses in `include/pm2200_map.h` come from a third-party list for the same meter family; several are extrapolated. Schneider's "PM2000 series Modbus register list" (se.com) is needed to verify them.
- **Some values cannot be read yet.** Neutral current, power factor, frequency, the three unbalance values and apparent energy (kVAh) have no known register address. On a real meter they are left out of the payload; in simulation all values are generated. The meter stores power factor on a -2 to +2 scale that has to be decoded once its register is known.
- **Float word order and address base** are constants in `include/pm2200_map.h` (default: high word first, 0-based offsets). A wrong value shows up as rejected polls (`out-of-range value ... check word order / address base`) or as `meter_ok: false`.
- **Simulation is the factory default.** Set `PM2200_SIM_DEFAULT` to `false` for builds that go on a real meter.
- **No real-meter validation yet.** A bring-up checklist (meter settings, wiring, comparing the readings with the meter's display) is still to be written.
- **Raspberry Pi side.** The bridge has to subscribe to `sensors/+/power`, and the dashboard design for the meter data is still to be agreed; both live outside this repository.
- **Flasher.** `flash_tool/` still carries the previous project's branding and paths. The PM2200 flasher and its distributable bundle are the next step.
- **Legacy code.** `src/provisioning.cpp` (ThingsBoard provisioning) is not used by this firmware.
