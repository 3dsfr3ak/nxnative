// NVN counter timestamps: 614.4 MHz ticks (384 ticks per 625 ns) on the Switch, not the CPU's 19.2 MHz system counter; on the Switch 2 the system counter's own 31.25 MHz ticks
// (32 ns a tick, as guest_ticks in hle_os.c). The Legend of Zelda: Ocarina of Time (Switch 2) turns the difference of two raw reports into its wind and particle time step at
// 31.25 MHz: at 614.4 MHz its foliage, falling leaves and hanging plants moved 19.66 times too fast (an agl pass's time input advanced 0.3456 in a 17.88 ms frame, = 17.58 ms of
// GPU time read at 31.25 MHz). A game that converts its reports through nvnDeviceGetTimestampInNanoseconds is right at either rate, as both directions take the same one.
// DM_NVN_TS_HZ=<Hz> sets another rate for a test. Keep the guest clock's epoch in both directions. Wide products avoid overflow on long host uptimes.
#pragma once
#include <stdint.h>
#include <stdlib.h>

#define NVN_TS_HZ_NX 614400000ull
#define NVN_TS_HZ_OUNCE 31250000ull
int dm_sdk_ounce(void);   // 1 when the sdk module was built for the Switch 2 (loader.c; it caches its answer once the modules are loaded)
static inline uint64_t nvn_ts_hz(void) {
    static uint64_t env = ~0ull;
    if (env == ~0ull) { const char *e = getenv("DM_NVN_TS_HZ"); env = e && *e ? strtoull(e, NULL, 10) : 0; }
    return env ? env : dm_sdk_ounce() ? NVN_TS_HZ_OUNCE : NVN_TS_HZ_NX;
}
static inline uint64_t nvn_ts_from_ns_at(uint64_t ns, uint64_t hz) {
    if (hz == NVN_TS_HZ_NX) return (uint64_t)((__uint128_t)ns * 384 / 625);
    if (hz == NVN_TS_HZ_OUNCE) return ns / 32;
    return (uint64_t)((__uint128_t)ns * hz / 1000000000u);
}
static inline uint64_t nvn_ts_to_ns_at(uint64_t ticks, uint64_t hz) {
    if (hz == NVN_TS_HZ_NX) return (uint64_t)((__uint128_t)ticks * 625 / 384);
    if (hz == NVN_TS_HZ_OUNCE) return ticks * 32;
    return (uint64_t)((__uint128_t)ticks * 1000000000u / hz);
}
static inline uint64_t nvn_timestamp_from_ns(uint64_t ns) { return nvn_ts_from_ns_at(ns, nvn_ts_hz()); }
static inline uint64_t nvn_timestamp_to_ns(uint64_t ticks) { return nvn_ts_to_ns_at(ticks, nvn_ts_hz()); }
