#include "pm2200.h"
#include "pm2200_map.h"
#include "rs485_sensor.h"
#include "config.h"
#include "web_server.h"

#include <Arduino.h>
#include <Preferences.h>
#include <math.h>
#include <string.h>

//* Schneider EasyLogic PM2200 driver — read-only, one block read per register group.
//* Register addresses are in pm2200_map.h (UNVERIFIED, see docs/pm2200_registers.md).
//* Simulation mode mirrors tools/pm2200_sim/sim_meter.py so the whole ESP -> WiFi -> MQTT
//* path can be tested on the real board with no meter attached.

// ── Shared meter state ───────────────────────────────────────────────────────
Pm2200Reading pm2200;
bool pm2200SensorOK = false;
bool pm2200MeterOK  = false;

// ── Module-private state ─────────────────────────────────────────────────────
static const char* PM_NVS_NS = "pm2200";

static uint8_t  _addr        = PM2200_ADDR_DEFAULT;
static uint32_t _baud        = PM2200_BAUD_DEFAULT;
static char     _parity      = PM2200_PARITY_DEFAULT;
static bool     _sim         = PM2200_SIM_DEFAULT;
static uint32_t _lastPollMs  = 0;
static uint8_t  _consecFails = 0;

// simulator state
static float  _simT   = 0.0f;
static double _simWh  = 0.0, _simVah = 0.0, _simVarh = 0.0;

// ── Decode ───────────────────────────────────────────────────────────────────
// Two 16-bit registers -> float32. Word order is a config constant: a wrong value
// here is the usual cause of garbage values on first connect.
static float regsToFloat32(uint16_t first, uint16_t second) {
    uint32_t u = PM2200_WORD_ORDER_ABCD ? (((uint32_t)first << 16) | second)
                                        : (((uint32_t)second << 16) | first);
    float f;
    memcpy(&f, &u, sizeof(f));
    return f;
}

static void clearReadings() {
    float* f = (float*)&pm2200;                       // every field is a float: unread = NAN
    for (size_t k = 0; k < sizeof(pm2200) / sizeof(float); k++) f[k] = NAN;
}

// ── Serial settings ──────────────────────────────────────────────────────────
static uint32_t serialConfig() {
    if (_parity == 'O') return SERIAL_8O1;
    if (_parity == 'N') return SERIAL_8N2;   // PM2200: no parity -> 2 stop bits
    return SERIAL_8E1;
}

static bool validBaud(uint32_t b) { return b == 4800 || b == 9600 || b == 19200 || b == 38400; }

static void openUart() {
    if (_sim) return;   // no meter attached — leave the bus idle
    rs485BeginUart(_baud, serialConfig());
    Serial.printf("[PM2200] addr %u, %lu baud, parity %c\n", _addr, (unsigned long)_baud, _parity);
}

// ── Real meter read ──────────────────────────────────────────────────────────
static bool readMeter() {
    Pm2200Reading tmp = pm2200;          // commit only if the whole poll succeeds
    uint16_t regs[32];

    for (const Pm2200Block& b : PM2200_BLOCKS) {
        if (!modbusReadHolding(_addr, PM2200_ADDR_BASE + b.start, b.count, regs)) return false;

        for (const Pm2200Reg& r : PM2200_REGS) {
            if (r.offset == PM2200_REG_UNKNOWN) continue;
            if (r.offset < b.start || r.offset + 2 > b.start + b.count) continue;

            float v = regsToFloat32(regs[r.offset - b.start], regs[r.offset - b.start + 1]) * r.scale;
            // Sanity envelope: a wrong word order / address base usually decodes to NaN or a huge float
            if (!isfinite(v) || fabsf(v) > PM2200_VALUE_MAX) {
                Serial.printf("[PM2200] out-of-range value at register %u — poll rejected (check word order / address base)\n",
                              r.offset);
                return false;
            }
            tmp.*(r.field) = v;
        }
    }
    pm2200 = tmp;
    return true;
}

// ── Simulated meter ──────────────────────────────────────────────────────────
static float worstUnbal(const float* x) {
    float avg = (x[0] + x[1] + x[2]) / 3.0f, w = 0.0f;
    for (int k = 0; k < 3; k++) w = max(w, fabsf(x[k] - avg));
    return w / avg * 100.0f;
}

static void simStep(float dt) {
    static const float I_BASE[3] = { 12.0f, 11.5f, 12.5f };
    _simT += dt;
    float t = _simT, v[3], i[3], pf[3], s[3], p[3], q[3], ire = 0.0f, iim = 0.0f;

    for (int k = 0; k < 3; k++) {
        v[k]  = 230.0f * (1 + 0.008f * sinf(t / 37 + 2.1f * k) + random(-15, 16) / 10000.0f);
        i[k]  = I_BASE[k] * (1 + 0.25f * sinf(t / 90 + k) + random(-30, 31) / 1000.0f);
        pf[k] = 0.94f + 0.035f * sinf(t / 50 + k);
        s[k]  = v[k] * i[k];
        p[k]  = s[k] * pf[k];
        q[k]  = sqrtf(s[k] * s[k] - p[k] * p[k]);
        float ang = -2 * PI * k / 3 - acosf(pf[k]);   // current lags voltage by acos(pf)
        ire += i[k] * cosf(ang);
        iim += i[k] * sinf(ang);
    }
    float vll[3] = {                                  // |Va - Vb| for phasors 120 deg apart
        sqrtf(v[0] * v[0] + v[1] * v[1] + v[0] * v[1]),
        sqrtf(v[1] * v[1] + v[2] * v[2] + v[1] * v[2]),
        sqrtf(v[2] * v[2] + v[0] * v[0] + v[2] * v[0]) };
    float pT = p[0] + p[1] + p[2], qT = q[0] + q[1] + q[2], sT = s[0] + s[1] + s[2];

    _simWh += pT * dt / 3600.0;   _simVah += sT * dt / 3600.0;   _simVarh += qT * dt / 3600.0;

    pm2200.v1 = v[0]; pm2200.v2 = v[1]; pm2200.v3 = v[2];  pm2200.v_avg = (v[0] + v[1] + v[2]) / 3;
    pm2200.v12 = vll[0]; pm2200.v23 = vll[1]; pm2200.v31 = vll[2];  pm2200.vll_avg = (vll[0] + vll[1] + vll[2]) / 3;
    pm2200.i1 = i[0]; pm2200.i2 = i[1]; pm2200.i3 = i[2];  pm2200.i_avg = (i[0] + i[1] + i[2]) / 3;
    pm2200.i_n = sqrtf(ire * ire + iim * iim);
    pm2200.p1 = p[0]; pm2200.p2 = p[1]; pm2200.p3 = p[2];  pm2200.p_total_w = pT;
    pm2200.q1 = q[0]; pm2200.q2 = q[1]; pm2200.q3 = q[2];  pm2200.q_total_var = qT;
    pm2200.s1 = s[0]; pm2200.s2 = s[1]; pm2200.s3 = s[2];  pm2200.s_total_va = sT;
    pm2200.pf1 = pf[0]; pm2200.pf2 = pf[1]; pm2200.pf3 = pf[2];  pm2200.pf = pT / sT;
    pm2200.freq_hz = 50.0f + 0.05f * sinf(t / 20);
    pm2200.i_unbal_pct = worstUnbal(i);  pm2200.v_unbal_ll_pct = worstUnbal(vll);  pm2200.v_unbal_ln_pct = worstUnbal(v);
    pm2200.energy_wh = (float)_simWh;    pm2200.energy_wh_recv = 0.0f;       // consumer only
    pm2200.energy_vah = (float)_simVah;  pm2200.energy_vah_recv = 0.0f;
    pm2200.energy_varh = (float)_simVarh; pm2200.energy_varh_recv = 0.0f;
}

// ── Reading JSON (shared by the MQTT payload and the setup page) ─────────────
// Payload keys in output order with printf formats — same list as FIELDS in
// tools/pm2200_sim/sim_meter.py and the key table in docs/payload.md.
struct ReadingField { const char* key; float Pm2200Reading::* field; const char* fmt; };

static const ReadingField READING_FIELDS[] = {
    { "v1", &Pm2200Reading::v1, "%.1f" }, { "v2", &Pm2200Reading::v2, "%.1f" },
    { "v3", &Pm2200Reading::v3, "%.1f" }, { "v_avg", &Pm2200Reading::v_avg, "%.1f" },
    { "v12", &Pm2200Reading::v12, "%.1f" }, { "v23", &Pm2200Reading::v23, "%.1f" },
    { "v31", &Pm2200Reading::v31, "%.1f" }, { "vll_avg", &Pm2200Reading::vll_avg, "%.1f" },
    { "i1", &Pm2200Reading::i1, "%.2f" }, { "i2", &Pm2200Reading::i2, "%.2f" },
    { "i3", &Pm2200Reading::i3, "%.2f" }, { "i_avg", &Pm2200Reading::i_avg, "%.2f" },
    { "i_n", &Pm2200Reading::i_n, "%.2f" },
    { "p1", &Pm2200Reading::p1, "%.0f" }, { "p2", &Pm2200Reading::p2, "%.0f" },
    { "p3", &Pm2200Reading::p3, "%.0f" }, { "p_total_w", &Pm2200Reading::p_total_w, "%.0f" },
    { "q1", &Pm2200Reading::q1, "%.0f" }, { "q2", &Pm2200Reading::q2, "%.0f" },
    { "q3", &Pm2200Reading::q3, "%.0f" }, { "q_total_var", &Pm2200Reading::q_total_var, "%.0f" },
    { "s1", &Pm2200Reading::s1, "%.0f" }, { "s2", &Pm2200Reading::s2, "%.0f" },
    { "s3", &Pm2200Reading::s3, "%.0f" }, { "s_total_va", &Pm2200Reading::s_total_va, "%.0f" },
    { "pf1", &Pm2200Reading::pf1, "%.2f" }, { "pf2", &Pm2200Reading::pf2, "%.2f" },
    { "pf3", &Pm2200Reading::pf3, "%.2f" }, { "pf", &Pm2200Reading::pf, "%.2f" },
    { "freq_hz", &Pm2200Reading::freq_hz, "%.2f" },
    { "i_unbal_pct", &Pm2200Reading::i_unbal_pct, "%.1f" },
    { "v_unbal_ll_pct", &Pm2200Reading::v_unbal_ll_pct, "%.1f" },
    { "v_unbal_ln_pct", &Pm2200Reading::v_unbal_ln_pct, "%.1f" },
    { "energy_wh", &Pm2200Reading::energy_wh, "%.0f" },
    { "energy_wh_recv", &Pm2200Reading::energy_wh_recv, "%.0f" },
    { "energy_vah", &Pm2200Reading::energy_vah, "%.0f" },
    { "energy_vah_recv", &Pm2200Reading::energy_vah_recv, "%.0f" },
    { "energy_varh", &Pm2200Reading::energy_varh, "%.0f" },
    { "energy_varh_recv", &Pm2200Reading::energy_varh_recv, "%.0f" },
};

// Writes `,"key":value` for every field that has a value. A value that was never read (or has
// no known register yet) is NAN, which snprintf would print as the invalid JSON token "nan" —
// the key is left out instead (a missing key is safe for the Influx bridge, a null may not be).
// Returns the number of characters written, or -1 if they do not fit in `cap`.
int pm2200ReadingJson(char* out, size_t cap) {
    if (cap == 0) return -1;
    out[0] = '\0';
    size_t n = 0;

    for (const ReadingField& f : READING_FIELDS) {
        float v = pm2200.*(f.field);
        if (!isfinite(v)) continue;

        char num[24];
        snprintf(num, sizeof(num), f.fmt, v);
        int w = snprintf(out + n, cap - n, ",\"%s\":%s", f.key, num);
        if (w < 0 || (size_t)w >= cap - n) return -1;
        n += w;
    }
    return (int)n;
}

// ── Settings ─────────────────────────────────────────────────────────────────
uint8_t  pm2200Addr()      { return _addr; }
uint32_t pm2200Baud()      { return _baud; }
char     pm2200Parity()    { return _parity; }
bool     pm2200Simulated() { return _sim; }

void pm2200ApplyConfig(uint8_t addr, uint32_t baud, char parity, bool sim) {
    if (addr < 1 || addr > 247) addr = PM2200_ADDR_DEFAULT;
    if (!validBaud(baud))       baud = PM2200_BAUD_DEFAULT;
    if (parity != 'E' && parity != 'O' && parity != 'N') parity = PM2200_PARITY_DEFAULT;
    _addr = addr; _baud = baud; _parity = parity; _sim = sim;

    Preferences p;
    p.begin(PM_NVS_NS, false);
    p.putUChar("addr", _addr);
    p.putUInt("baud", _baud);
    p.putUChar("parity", (uint8_t)_parity);
    p.putBool("sim", _sim);
    p.end();

    clearReadings();                     // never show or publish values from the previous data source
    _consecFails = 0;
    pm2200SensorOK = pm2200MeterOK = false;
    _lastPollMs = 0;                     // force immediate poll on next loop pass
    openUart();
    logPush(String("[PM2200] ") + (_sim ? "simulated" : "meter") + " addr " + String(_addr) +
            " " + String(_baud) + " " + String(_parity));
}

const char* pm2200StatusLabel() {
    if (_sim) return "simulated";
    return pm2200MeterOK ? "ok" : "no response";
}

// ── Init ─────────────────────────────────────────────────────────────────────
void pm2200Init() {
    clearReadings();

    Preferences p;
    p.begin(PM_NVS_NS, true);
    _addr   = p.getUChar("addr", PM2200_ADDR_DEFAULT);
    _baud   = p.getUInt("baud", PM2200_BAUD_DEFAULT);
    _parity = (char)p.getUChar("parity", (uint8_t)PM2200_PARITY_DEFAULT);
    _sim    = p.getBool("sim", PM2200_SIM_DEFAULT);
    p.end();
    if (_addr < 1 || _addr > 247) _addr = PM2200_ADDR_DEFAULT;
    if (!validBaud(_baud))        _baud = PM2200_BAUD_DEFAULT;
    if (_parity != 'E' && _parity != 'O' && _parity != 'N') _parity = PM2200_PARITY_DEFAULT;

    Serial.printf("[PM2200] %s mode\n", _sim ? "SIMULATED" : "meter");
    openUart();
}

// ── Read (call from loop, self-throttled) ────────────────────────────────────
void pm2200Read() {
    uint32_t now = millis();
    if (_lastPollMs != 0 && (now - _lastPollMs) < RS485_POLL_INTERVAL) return;
    uint32_t dtMs = _lastPollMs ? now - _lastPollMs : RS485_POLL_INTERVAL;
    _lastPollMs = now;

    rs485PollCount++;

    bool ok;
    if (_sim) { simStep(dtMs / 1000.0f); ok = true; }
    else      { ok = readMeter(); }

    pm2200MeterOK = ok;
    if (ok) {
        _consecFails = 0;
        pm2200SensorOK = true;
    } else {
        rs485FailCount++;
        if (_consecFails < 255) _consecFails++;

        // Keep last good values visible for transient glitches; flag the meter
        // unavailable only after RS485_MAX_FAILS consecutive failures.
        if (_consecFails >= RS485_MAX_FAILS && pm2200SensorOK) {
            pm2200SensorOK = false;
            logPush("[RS485] PM2200 unavailable (" + String(_consecFails) + " fails)");
        }
    }
}
