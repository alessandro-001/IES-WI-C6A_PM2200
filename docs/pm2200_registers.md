# PM2200 register map

One row per key in `docs/payload.md`. **Every address below is UNVERIFIED.** The PM2200 manual (NHA2778902-11, `docs/PM2200-datasheet.PDF`) names the measurements but contains no Modbus register map; it points to Schneider's "PM2000 series Modbus register list" on se.com. Verify against that list before connecting a real meter.

Working assumptions (config constants in `include/pm2200_map.h`, since a wrong value here is the usual first-connect failure):

- Function code 0x03 (holding registers), data type float32 = 2 registers.
- Word order: big-endian ABCD (high word first).
- Addresses are **0-based offsets** as listed by the third-party source; the 1-based register number is offset + 1.

Status legend:
- `aggsoft`: listed by https://aggsoft.com/serial-data-logger/tutorials/modbus-data-logging/schneider-electric-em6400ng-pm2100-pm2200.htm (EM6400NG/PM2100/PM2200 family).
- `stride`: not listed; extrapolated from the +2 register stride between neighbouring rows.
- `?`: no address known, do not read until the official list is checked.

| Payload key | Quantity | Offset | Unit (meter) | Payload scale | Status |
|---|---|---|---|---|---|
| `i1` | Current A | 2999 | A | x1 | aggsoft |
| `i2` | Current B | 3001 | A | x1 | aggsoft |
| `i3` | Current C | 3003 | A | x1 | aggsoft |
| `i_n` | Neutral current | ? | A | x1 | ? |
| `i_avg` | Current 3-phase avg | 3009 | A | x1 | aggsoft |
| `v12` | Voltage A-B | 3019 | V | x1 | aggsoft |
| `v23` | Voltage B-C | 3021 | V | x1 | stride |
| `v31` | Voltage C-A | 3023 | V | x1 | stride |
| `vll_avg` | Voltage L-L avg | 3025 | V | x1 | stride |
| `v1` | Voltage A-N | 3027 | V | x1 | aggsoft |
| `v2` | Voltage B-N | 3029 | V | x1 | stride |
| `v3` | Voltage C-N | 3031 | V | x1 | stride |
| `v_avg` | Voltage L-N avg | 3035 | V | x1 | aggsoft |
| `p1` | Active power A | 3053 | kW | x1000 (W) | aggsoft |
| `p2` | Active power B | 3055 | kW | x1000 (W) | stride |
| `p3` | Active power C | 3057 | kW | x1000 (W) | stride |
| `p_total_w` | Active power total | 3059 | kW | x1000 (W) | aggsoft |
| `q1` | Reactive power A | 3061 | kVAR | x1000 (var) | aggsoft |
| `q2` | Reactive power B | 3063 | kVAR | x1000 (var) | stride |
| `q3` | Reactive power C | 3065 | kVAR | x1000 (var) | stride |
| `q_total_var` | Reactive power total | 3067 | kVAR | x1000 (var) | stride |
| `s1` | Apparent power A | 3069 | kVA | x1000 (VA) | aggsoft |
| `s2` | Apparent power B | 3071 | kVA | x1000 (VA) | stride |
| `s3` | Apparent power C | 3073 | kVA | x1000 (VA) | stride |
| `s_total_va` | Apparent power total | 3075 | kVA | x1000 (VA) | stride |
| `pf1`..`pf3` | PF per phase | ? | - | decode (see below) | ? |
| `pf` | PF total | ? | - | decode (see below) | ? |
| `freq_hz` | Frequency | ? | Hz | x1 | ? |
| `i_unbal_pct` | Current unbalance, worst | ? | % | x1 | ? |
| `v_unbal_ll_pct` | Voltage unbalance L-L, worst | ? | % | x1 | ? |
| `v_unbal_ln_pct` | Voltage unbalance L-N, worst | ? | % | x1 | ? |
| `energy_wh` | Active energy delivered | 2699 | kWh | x1000 (Wh) | aggsoft |
| `energy_wh_recv` | Active energy received | 2701 | kWh | x1000 (Wh) | aggsoft |
| `energy_varh` | Reactive energy delivered | 2707 | kVARh | x1000 (varh) | aggsoft |
| `energy_varh_recv` | Reactive energy received | 2709 | kVARh | x1000 (varh) | aggsoft |
| `energy_vah` | Apparent energy delivered | ? | kVAh | x1000 (VAh) | ? |
| `energy_vah_recv` | Apparent energy received | ? | kVAh | x1000 (VAh) | ? |

## Notes

- **PF register format.** The manual (p. 86) says each PF occupies one floating-point register on a -2 to +2 scale, with the quadrant encoded in the value, not a plain -1 to +1 PF. The firmware must decode it into a signed PF (IEC or IEEE sign, as configured on the meter) before publishing. The exact formula is only given as a diagram in the manual; confirm it against the official register list and the real meter, do not guess.
- **Energy.** float32 holds about 7 significant digits, so very large cumulative totals lose resolution; an int64 Wh register reported for the PM2000 family (around 3204) would avoid that but is also unverified.
- **Retrofit mode.** PM2220/PM2230 can be switched on the front panel to a legacy register map (Maint > Setup > Comm). The addresses above may belong to that map rather than the native one, so the map must match the model and mode of the unit that gets deployed. Meter setup is the installer's; we only read.
- **Wiring.** Phase-wise energy exists only for 3PH4W wiring and is not published. V L-N values are only meaningful on 4-wire systems.
- **Serial settings.** Baud 4800/9600/19200/38400; parity Even/Odd (1 stop bit) or None (2 stop bits). The manual gives no defaults; the installer sets them on the meter and enters the same values in the board's setup page.
