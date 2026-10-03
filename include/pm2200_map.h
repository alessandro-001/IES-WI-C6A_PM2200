#pragma once
#include "pm2200.h"

//! ── PM2200 register map ──────────────────────────────────────────────────────
//
// ALL ADDRESSES UNVERIFIED — see docs/pm2200_registers.md. The PM2200 manual has no
// register map; verify against Schneider's "PM2000 series Modbus register list"
// before connecting a real meter. Offsets are 0-based as listed by the third-party
// source (1-based register = offset + 1), float32 = 2 registers, FC 0x03.

#define PM2200_ADDR_BASE      0     // added to every offset on the wire (use 1 if the meter list is 1-based)
#define PM2200_WORD_ORDER_ABCD 1    // 1 = high word first (ABCD), 0 = low word first (CDAB)
#define PM2200_REG_UNKNOWN    0xFFFF   // address not known yet — field is skipped, stays NAN
#define PM2200_VALUE_MAX      1e10f    // |scaled value| above this rejects the poll (keeps the payload within its buffer)

//! ── Read blocks (contiguous, <= 32 registers each, one Modbus transaction per block) ──
struct Pm2200Block { uint16_t start; uint16_t count; };

static const Pm2200Block PM2200_BLOCKS[] = {
    { 2699, 12 },    // energy
    { 2999, 12 },    // current
    { 3019, 18 },    // voltage
    { 3053, 24 },    // power
};

//! ── Register table: payload field <- float32 at `offset`, multiplied by `scale` ──
struct Pm2200Reg { uint16_t offset; float Pm2200Reading::* field; float scale; };

static const Pm2200Reg PM2200_REGS[] = {
    { 2999, &Pm2200Reading::i1,          1.0f },
    { 3001, &Pm2200Reading::i2,          1.0f },
    { 3003, &Pm2200Reading::i3,          1.0f },
    { PM2200_REG_UNKNOWN, &Pm2200Reading::i_n, 1.0f },
    { 3009, &Pm2200Reading::i_avg,       1.0f },
    { 3019, &Pm2200Reading::v12,         1.0f },
    { 3021, &Pm2200Reading::v23,         1.0f },   // extrapolated (+2 stride)
    { 3023, &Pm2200Reading::v31,         1.0f },   // extrapolated
    { 3025, &Pm2200Reading::vll_avg,     1.0f },   // extrapolated
    { 3027, &Pm2200Reading::v1,          1.0f },
    { 3029, &Pm2200Reading::v2,          1.0f },   // extrapolated
    { 3031, &Pm2200Reading::v3,          1.0f },   // extrapolated
    { 3035, &Pm2200Reading::v_avg,       1.0f },
    { 3053, &Pm2200Reading::p1,          1000.0f },   // kW -> W
    { 3055, &Pm2200Reading::p2,          1000.0f },   // extrapolated
    { 3057, &Pm2200Reading::p3,          1000.0f },   // extrapolated
    { 3059, &Pm2200Reading::p_total_w,   1000.0f },
    { 3061, &Pm2200Reading::q1,          1000.0f },   // kVAR -> var
    { 3063, &Pm2200Reading::q2,          1000.0f },   // extrapolated
    { 3065, &Pm2200Reading::q3,          1000.0f },   // extrapolated
    { 3067, &Pm2200Reading::q_total_var, 1000.0f },   // extrapolated
    { 3069, &Pm2200Reading::s1,          1000.0f },   // kVA -> VA
    { 3071, &Pm2200Reading::s2,          1000.0f },   // extrapolated
    { 3073, &Pm2200Reading::s3,          1000.0f },   // extrapolated
    { 3075, &Pm2200Reading::s_total_va,  1000.0f },   // extrapolated
    { PM2200_REG_UNKNOWN, &Pm2200Reading::pf1,   1.0f },   // PF is on a -2..+2 scale, needs decoding
    { PM2200_REG_UNKNOWN, &Pm2200Reading::pf2,   1.0f },
    { PM2200_REG_UNKNOWN, &Pm2200Reading::pf3,   1.0f },
    { PM2200_REG_UNKNOWN, &Pm2200Reading::pf,    1.0f },
    { PM2200_REG_UNKNOWN, &Pm2200Reading::freq_hz,        1.0f },
    { PM2200_REG_UNKNOWN, &Pm2200Reading::i_unbal_pct,    1.0f },
    { PM2200_REG_UNKNOWN, &Pm2200Reading::v_unbal_ll_pct, 1.0f },
    { PM2200_REG_UNKNOWN, &Pm2200Reading::v_unbal_ln_pct, 1.0f },
    { 2699, &Pm2200Reading::energy_wh,        1000.0f },   // kWh -> Wh
    { 2701, &Pm2200Reading::energy_wh_recv,   1000.0f },
    { PM2200_REG_UNKNOWN, &Pm2200Reading::energy_vah,      1000.0f },
    { PM2200_REG_UNKNOWN, &Pm2200Reading::energy_vah_recv, 1000.0f },
    { 2707, &Pm2200Reading::energy_varh,      1000.0f },   // kVARh -> varh
    { 2709, &Pm2200Reading::energy_varh_recv, 1000.0f },
};
