"""
sim_meter.py
============
Simulated Schneider PM2200 behind an IES-WI-C6A board. Prints the exact MQTT
`power` payload (see docs/payload.md) to the terminal every few seconds.

Usage:
    python sim_meter.py                   # run until Ctrl+C
    python sim_meter.py --wh-start 5000   # start the Wh counter at 5000
    python sim_meter.py --check           # run the self-check and exit
"""

import argparse, cmath, json, math, random, sys, time
from datetime import datetime, timezone

# ── Config ────────────────────────────────────────────────────────────────────
DEVICE_ID = "PM_SIM1"
FIRMWARE  = "1.0.0"
INTERVAL  = 5                      # seconds, same as the firmware publish timer
V_NOM     = 230.0                  # V L-N
F_NOM     = 50.0                   # Hz
I_BASE    = (12.0, 11.5, 12.5)     # nominal phase currents, A

# Payload keys in output order with printf formats (mirrors the firmware snprintf)
FIELDS = [
    ("v1", "%.1f"), ("v2", "%.1f"), ("v3", "%.1f"), ("v_avg", "%.1f"),
    ("v12", "%.1f"), ("v23", "%.1f"), ("v31", "%.1f"), ("vll_avg", "%.1f"),
    ("i1", "%.2f"), ("i2", "%.2f"), ("i3", "%.2f"), ("i_avg", "%.2f"), ("i_n", "%.2f"),
    ("p1", "%.0f"), ("p2", "%.0f"), ("p3", "%.0f"), ("p_total_w", "%.0f"),
    ("q1", "%.0f"), ("q2", "%.0f"), ("q3", "%.0f"), ("q_total_var", "%.0f"),
    ("s1", "%.0f"), ("s2", "%.0f"), ("s3", "%.0f"), ("s_total_va", "%.0f"),
    ("pf1", "%.2f"), ("pf2", "%.2f"), ("pf3", "%.2f"), ("pf", "%.2f"),
    ("freq_hz", "%.2f"),
    ("i_unbal_pct", "%.1f"), ("v_unbal_ll_pct", "%.1f"), ("v_unbal_ln_pct", "%.1f"),
    ("energy_wh", "%.0f"), ("energy_wh_recv", "%.0f"),
    ("energy_vah", "%.0f"), ("energy_vah_recv", "%.0f"),
    ("energy_varh", "%.0f"), ("energy_varh_recv", "%.0f"),
]
# ─────────────────────────────────────────────────────────────────────────────

def worst_unbal(vals):
    avg = sum(vals) / len(vals)
    return max(abs(x - avg) for x in vals) / avg * 100

class Meter:
    def __init__(self, wh=0.0, seed=None):
        self.rng = random.Random(seed)
        self.t = 0.0
        # consumer only: received energy stays 0; VAh/VARh follow the Wh start
        self.wh, self.vah, self.varh = wh, wh / 0.95, wh * 0.33

    def step(self, dt):
        """Advance dt seconds and return every payload value as a float."""
        self.t += dt
        t, r = self.t, self.rng
        v  = [V_NOM * (1 + 0.008 * math.sin(t / 37 + 2.1 * k) + r.uniform(-0.0015, 0.0015)) for k in range(3)]
        i  = [I_BASE[k] * (1 + 0.25 * math.sin(t / 90 + k) + r.uniform(-0.03, 0.03)) for k in range(3)]
        pf = [0.94 + 0.035 * math.sin(t / 50 + k) for k in range(3)]
        s  = [v[k] * i[k] for k in range(3)]
        p  = [s[k] * pf[k] for k in range(3)]
        q  = [math.sqrt(s[k] ** 2 - p[k] ** 2) for k in range(3)]

        # phasors: voltages 120 deg apart, currents lag by acos(pf)
        va = [cmath.rect(v[k], -2 * math.pi * k / 3) for k in range(3)]
        ia = [cmath.rect(i[k], -2 * math.pi * k / 3 - math.acos(pf[k])) for k in range(3)]
        vll = [abs(va[k] - va[(k + 1) % 3]) for k in range(3)]

        self.wh  += sum(p) * dt / 3600
        self.vah += sum(s) * dt / 3600
        self.varh += sum(q) * dt / 3600

        m = {"v_avg": sum(v) / 3, "vll_avg": sum(vll) / 3, "i_avg": sum(i) / 3, "i_n": abs(sum(ia)),
             "p_total_w": sum(p), "q_total_var": sum(q), "s_total_va": sum(s), "pf": sum(p) / sum(s),
             "freq_hz": F_NOM + 0.05 * math.sin(t / 20),
             "i_unbal_pct": worst_unbal(i), "v_unbal_ll_pct": worst_unbal(vll), "v_unbal_ln_pct": worst_unbal(v),
             "energy_wh": self.wh, "energy_wh_recv": 0.0,
             "energy_vah": self.vah, "energy_vah_recv": 0.0,
             "energy_varh": self.varh, "energy_varh_recv": 0.0}
        for k in range(3):
            m.update({f"v{k+1}": v[k], f"i{k+1}": i[k], f"p{k+1}": p[k], f"q{k+1}": q[k],
                      f"s{k+1}": s[k], f"pf{k+1}": pf[k]})
        m.update({"v12": vll[0], "v23": vll[1], "v31": vll[2]})
        return m

def payload(m, device_id=DEVICE_ID, rssi=-60):
    ts = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    reading = ",".join('"%s":%s' % (k, f % m[k]) for k, f in FIELDS)
    return ('{"device_id":"%s","timestamp":"%s","reading":{"sensor_type":4,"sensor_type_label":"power",'
            '"firmware":"%s","rssi":%d,"sensor_ok":true,"meter_ok":true,"simulated":true,%s}}'
            % (device_id, ts, FIRMWARE, rssi, reading))

def check():
    m, prev = Meter(wh=1000, seed=1), 1000
    for _ in range(720):                                  # one hour of 5 s steps
        v = m.step(INTERVAL)
        assert v["energy_wh"] >= prev, "Wh counter must never decrease"
        prev = v["energy_wh"]
        assert all(0.99 * V_NOM <= v[f"v{k}"] <= 1.01 * V_NOM for k in (1, 2, 3)), "V L-N out of +/-1%"
        assert all(0.90 <= v[f"pf{k}"] <= 0.98 for k in (1, 2, 3)), "PF out of 0.90-0.98"
        assert abs(v["p_total_w"] - sum(v[f"p{k}"] for k in (1, 2, 3))) < 1e-6
        assert all(abs(v[f"s{k}"] ** 2 - v[f"p{k}"] ** 2 - v[f"q{k}"] ** 2) < 1e-3 for k in (1, 2, 3))
    text = payload(v)
    doc = json.loads(text)                                # valid JSON, expected shape
    assert list(doc["reading"])[7:] == [k for k, _ in FIELDS], "key order must match FIELDS"
    assert doc["reading"]["simulated"] is True
    assert doc["device_id"] == DEVICE_ID and doc["reading"]["sensor_type"] == 4
    print("check OK: payload %d bytes, %d keys, Wh %d -> %d" % (len(text), len(doc["reading"]), 1000, prev))

def main():
    ap = argparse.ArgumentParser(description="Simulated PM2200 payload generator")
    ap.add_argument("--device-id", default=DEVICE_ID)
    ap.add_argument("--wh-start", type=float, default=0.0, help="initial active energy delivered, Wh")
    ap.add_argument("--interval", type=float, default=INTERVAL, help="seconds between payloads")
    ap.add_argument("--check", action="store_true", help="run the self-check and exit")
    a = ap.parse_args()
    if a.check:
        return check()
    m = Meter(wh=a.wh_start)
    try:
        while True:
            print(payload(m.step(a.interval), a.device_id), flush=True)
            time.sleep(a.interval)
    except KeyboardInterrupt:
        sys.exit(0)

if __name__ == "__main__":
    main()
