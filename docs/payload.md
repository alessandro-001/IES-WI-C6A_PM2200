# PM2200 MQTT payload contract

Same transport and envelope as the existing sensors, so the Pi bridge only needs the new topic added.
Measurement names and semantics follow the PM2200 user manual (NHA2778902-11, `docs/PM2200-datasheet.PDF`). The firmware only reads the meter; meter setup (wiring mode, CT/VT ratios, Modbus settings) is the installer's.

## Topics

| Topic | Retained | Interval |
|---|---|---|
| `sensors/PM_<last4mac>/power` | no | 5 s |
| `sensors/PM_<last4mac>/attributes` | yes | 60 s and on reconnect |

`device_id` = `PM_` + last 4 hex chars of the MAC (e.g. `PM_ABCD`). The last topic segment (`power`) is the Influx measurement name.

## `power` payload

```json
{
  "device_id": "PM_ABCD",
  "timestamp": "2026-01-01T12:00:00Z",
  "reading": {
    "sensor_type": 4,
    "sensor_type_label": "power",
    "firmware": "1.0.0",
    "rssi": -60,
    "sensor_ok": true,
    "meter_ok": true,
    "v1": 230.1, "v2": 229.8, "v3": 230.4, "v_avg": 230.1,
    "v12": 398.5, "v23": 398.1, "v31": 399.0, "vll_avg": 398.5,
    "i1": 12.30, "i2": 11.90, "i3": 12.60, "i_avg": 12.27, "i_n": 0.80,
    "p1": 2650, "p2": 2540, "p3": 2700, "p_total_w": 7890,
    "q1": 870, "q2": 830, "q3": 890, "q_total_var": 2590,
    "s1": 2790, "s2": 2670, "s3": 2840, "s_total_va": 8300,
    "pf1": 0.95, "pf2": 0.95, "pf3": 0.95, "pf": 0.95,
    "freq_hz": 50.00,
    "i_unbal_pct": 2.4, "v_unbal_ll_pct": 0.2, "v_unbal_ln_pct": 0.2,
    "energy_wh": 1234567, "energy_wh_recv": 0,
    "energy_vah": 1300000, "energy_vah_recv": 0,
    "energy_varh": 410000, "energy_varh_recv": 0
  }
}
```

`timestamp` is ISO-8601 UTC and is omitted while NTP time is not yet valid (the Pi fills server time), as for the other sensors.

## Keys

All instantaneous values are the meter's own readings; the firmware does no averaging, scaling beyond the unit conversion below, or integration.

| Key | Unit | Meter quantity |
|---|---|---|
| `v1..v3` | V | Voltage L-N, phase 1/2/3 |
| `v_avg` | V | Voltage L-N, 3-phase average |
| `v12`, `v23`, `v31` | V | Voltage L-L |
| `vll_avg` | V | Voltage L-L, 3-phase average |
| `i1..i3` | A | Current, phase 1/2/3 |
| `i_avg` | A | Current, 3-phase average |
| `i_n` | A | Neutral current (calculated by the meter) |
| `p1..p3`, `p_total_w` | W | Active power, per phase / total (meter kW x 1000) |
| `q1..q3`, `q_total_var` | var | Reactive power (meter kVAR x 1000) |
| `s1..s3`, `s_total_va` | VA | Apparent power (meter kVA x 1000) |
| `pf1..pf3`, `pf` | - | Power factor per phase / 3-phase total, **true PF** (the meter's default) |
| `freq_hz` | Hz | System frequency |
| `i_unbal_pct` | % | Current unbalance, worst phase |
| `v_unbal_ll_pct` | % | Voltage unbalance L-L, worst phase |
| `v_unbal_ln_pct` | % | Voltage unbalance L-N, worst phase |
| `energy_wh`, `energy_wh_recv` | Wh | Active energy delivered (import) / received (export), cumulative (meter kWh x 1000) |
| `energy_vah`, `energy_vah_recv` | VAh | Apparent energy delivered / received |
| `energy_varh`, `energy_varh_recv` | varh | Reactive energy delivered / received |
| `sensor_ok` | bool | RS485 layer healthy (false after `RS485_MAX_FAILS` consecutive failures) |
| `meter_ok` | bool | Last Modbus read returned valid values |

Sign convention (as the meter reports it): active power is positive for delivered (import) and negative for received (export); PF carries the meter's sign (IEC or IEEE, whichever the installer configured). Energy counters are cumulative, non-volatile in the meter, and never integrated on the ESP.

Not published (derive downstream if wanted): energy total (D+R) and net (D-R), per-phase energy (only valid for 3PH4W wiring), demand, THD, harmonics, min/max, per-phase unbalance. All can be added later as new keys.

On Modbus failure the last good values are kept and the flags go false, matching the existing sensors. Consumers must ignore unknown keys; existing keys never change meaning.

Note: this payload is about 1.1 KB, larger than the 900-byte build buffer and the 1024-byte MQTT buffer in the prototype, so both must be raised when the payload builder is written (Phase 3).

## `attributes` payload (retained)

Existing keys (`mac`, `ip`, `rssi`, `firmware`, `sensor_type`) plus:

| Key | Example |
|---|---|
| `meter_model` | `"PM2200"` |
| `modbus_addr` | `1` |
| `baud` | `9600` |
| `parity` | `"E"` (`E`/`O`/`N`; `N` implies 2 stop bits) |
