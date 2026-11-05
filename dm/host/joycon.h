// A right Joy-Con over Bluetooth, read as raw HID (joycon_mac.h). GameController lists one with no extended gamepad profile, so front_mac.m has no buttons or stick from it, and macOS's
// GCMotion is zero-only on the Pro Controller (motion_pro.h). Pure, so tests/t_joycon.c checks it without a controller.
// Protocol: dekuNukem/Nintendo_Switch_Reverse_Engineering. Input report 0x30 is 3 buttons bytes at +3, the right stick at +9 (two 12-bit values in 3 bytes), then three IMU samples of
// 12 bytes from +13, 5 ms apart, oldest first (as motion_pro.h). Report 0x21 has the same head and answers a sub-command: its id at +14, an SPI read's address, length and data from +15.
// Axes: the IC is mounted turned on the right Joy-Con, so against the Pro Controller's mapping (motion_pro.h) its raw y and z are negated (as Linux hid-nintendo has it). In the Switch
// frame, x right, y forward, z up out of the face with gravity down, held as attached to a console: acc = (-raw1, -raw0, raw2), rot = (raw4, raw3, -raw5).
// Scales: factory or user calibration from SPI flash, the nominal ones when a value is blank; a held offset is subtracted from the gyro, as the acceleration's is not.
#pragma once
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "dm_input.h"

enum { JC_VENDOR = 0x057E, JC_PRODUCT_RIGHT = 0x2007, JC_OUTPUT_LEN = 49, JC_STICK_CENTER = 2048, JC_STICK_RANGE = 1400, JC_ACC_SCALE = 16384, JC_GYRO_SCALE = 13371 };
typedef struct {
    int center[2], lo[2], hi[2];   // the stick's rest and its reach below and above it, x and y
    int acc_off[3], acc_scale[3], gyro_off[3], gyro_scale[3];   // by raw axis
} jc_cal;
typedef struct {
    uint32_t buttons;   // DM_BTN_*
    float stick[2];     // -1..1, up is positive
    int samples;        // 3 in a full report, 0 otherwise
    float acc[3][3], rot[3][3];   // oldest first: G with gravity down, rad/s, in the Switch frame
} jc_input;

static inline void jc_cal_defaults(jc_cal *c) {
    memset(c, 0, sizeof *c);
    for (int i = 0; i < 2; i++) { c->center[i] = JC_STICK_CENTER; c->lo[i] = c->hi[i] = JC_STICK_RANGE; }
    for (int i = 0; i < 3; i++) { c->acc_scale[i] = JC_ACC_SCALE; c->gyro_scale[i] = JC_GYRO_SCALE; }
}
static inline void jc_pair(const uint8_t b[3], int out[2]) { out[0] = b[0] | (b[1] & 0xf) << 8; out[1] = b[1] >> 4 | b[2] << 4; }
static inline int jc_valid12(int v) { return v != 0 && v != 0xfff; }
// The right stick's 9 bytes in flash: rest, reach below, reach above
static inline void jc_cal_stick(jc_cal *c, const uint8_t b[9]) {
    int v[3][2]; for (int k = 0; k < 3; k++) jc_pair(b + 3 * k, v[k]);
    for (int i = 0; i < 2; i++) {
        c->center[i] = jc_valid12(v[0][i]) ? v[0][i] : JC_STICK_CENTER;
        c->lo[i] = jc_valid12(v[1][i]) ? v[1][i] : JC_STICK_RANGE;
        c->hi[i] = jc_valid12(v[2][i]) ? v[2][i] : JC_STICK_RANGE;
    }
}
static inline int jc_s16(const uint8_t *b) { return (int16_t)((uint16_t)b[0] | (uint16_t)b[1] << 8); }
// The IMU's 24 bytes in flash: acceleration offsets, acceleration scales, gyro offsets, gyro scales. An erased scale (-1) or one with no span keeps the nominal one
static inline void jc_cal_imu(jc_cal *c, const uint8_t b[24]) {
    for (int i = 0; i < 3; i++) {
        int ao = jc_s16(b + 2 * i), as = jc_s16(b + 6 + 2 * i), go = jc_s16(b + 12 + 2 * i), gs = jc_s16(b + 18 + 2 * i);
        if (ao == -1) ao = 0;
        if (go == -1) go = 0;
        if (as == -1 || as == ao) { ao = 0; as = JC_ACC_SCALE; }
        if (gs == -1 || gs == go) { go = 0; gs = JC_GYRO_SCALE; }
        c->acc_off[i] = ao; c->acc_scale[i] = as; c->gyro_off[i] = go; c->gyro_scale[i] = gs;
    }
}
static inline float jc_axis(int raw, int center, int lo, int hi) {
    int d = raw - center, r = d > 0 ? hi : lo;
    float v = r > 0 ? (float)d / (float)r : 0;
    return v < -1 ? -1 : v > 1 ? 1 : v;
}
static inline uint32_t jc_buttons(uint8_t right, uint8_t shared) {
    uint32_t m = 0;
    if (right & 0x01) m |= DM_BTN_Y;
    if (right & 0x02) m |= DM_BTN_X;
    if (right & 0x04) m |= DM_BTN_B;
    if (right & 0x08) m |= DM_BTN_A;
    if (right & 0x40) m |= DM_BTN_R;
    if (right & 0x80) m |= DM_BTN_ZR;
    if (shared & 0x02) m |= DM_BTN_PLUS;
    if (shared & 0x04) m |= DM_BTN_RS;
    return m;
}
// 1 if r is a report with the controller's state. A sub-command's reply carries the buttons and the stick too
static inline int jc_decode(const jc_cal *c, const uint8_t *r, size_t len, jc_input *o) {
    if (len < 12 || (r[0] != 0x30 && r[0] != 0x21)) return 0;
    memset(o, 0, sizeof *o);
    o->buttons = jc_buttons(r[3], r[4]);
    int s[2]; jc_pair(r + 9, s);
    for (int i = 0; i < 2; i++) o->stick[i] = jc_axis(s[i], c->center[i], c->lo[i], c->hi[i]);
    if (r[0] != 0x30 || len < 49) return 1;
    int data = 0; float acc[3], gyro[3];
    for (int k = 0; k < 3; k++) {
        const uint8_t *b = r + 13 + 12 * k;
        for (int i = 0; i < 3; i++) {
            int ra = jc_s16(b + 2 * i), rg = jc_s16(b + 6 + 2 * i);
            data |= ra != 0 || rg != 0;
            acc[i] = (float)ra * 4.f / (float)(c->acc_scale[i] - c->acc_off[i]);
            gyro[i] = (float)(rg - c->gyro_off[i]) * 936.f / (float)(c->gyro_scale[i] - c->gyro_off[i]) * (float)M_PI / 180.f;
        }
        o->acc[k][0] = -acc[1]; o->acc[k][1] = -acc[0]; o->acc[k][2] = acc[2];
        o->rot[k][0] = gyro[1]; o->rot[k][1] = gyro[0]; o->rot[k][2] = -gyro[2];
    }
    o->samples = data ? 3 : 0;   // all zero: the IMU is off, no motion source
    return 1;
}

// Setup, one sub-command at a time: calibration from flash (the user's if its magic is there, else the factory's), then the IMU and the full report mode, then the player light
enum { JC_STICK_MAGIC, JC_STICK_USER, JC_STICK_FACTORY, JC_IMU_MAGIC, JC_IMU_USER, JC_IMU_FACTORY, JC_IMU_CONFIG, JC_IMU_ON, JC_MODE, JC_LIGHT, JC_DONE };
typedef struct { int step; jc_cal cal; } jc_init;
static inline void jc_init_start(jc_init *s) { s->step = JC_STICK_MAGIC; jc_cal_defaults(&s->cal); }
static inline int jc_spi_of(int step, unsigned *addr, int *len) {
    switch (step) {
    case JC_STICK_MAGIC: *addr = 0x801B; *len = 2; return 1;
    case JC_STICK_USER: *addr = 0x801D; *len = 9; return 1;
    case JC_STICK_FACTORY: *addr = 0x6046; *len = 9; return 1;
    case JC_IMU_MAGIC: *addr = 0x8026; *len = 2; return 1;
    case JC_IMU_USER: *addr = 0x8028; *len = 24; return 1;
    case JC_IMU_FACTORY: *addr = 0x6020; *len = 24; return 1;
    }
    return 0;
}
// The sub-command of the current step: its id and arguments; returns their count, 0 when setup is over
static inline int jc_init_command(const jc_init *s, int player, uint8_t out[8]) {
    unsigned a; int n;
    if (jc_spi_of(s->step, &a, &n)) { out[0] = 0x10; out[1] = a & 0xff; out[2] = a >> 8 & 0xff; out[3] = out[4] = 0; out[5] = (uint8_t)n; return 6; }
    switch (s->step) {
    case JC_IMU_CONFIG: out[0] = 0x41; out[1] = 3; out[2] = 0; out[3] = 1; out[4] = 1; return 5;   // +-2000 dps, +-8 G, 208 Hz, 100 Hz: the scales above
    case JC_IMU_ON: out[0] = 0x40; out[1] = 1; return 2;
    case JC_MODE: out[0] = 0x03; out[1] = 0x30; return 2;
    case JC_LIGHT: out[0] = 0x30; out[1] = (uint8_t)(1u << (player < 0 ? 0 : player & 3)); return 2;
    }
    return 0;
}
// The next step when the current one has no answer: blank calibration keeps the nominal scales
static inline void jc_init_skip(jc_init *s) {
    switch (s->step) {
    case JC_STICK_MAGIC: s->step = JC_STICK_FACTORY; break;
    case JC_STICK_USER: case JC_STICK_FACTORY: s->step = JC_IMU_MAGIC; break;
    case JC_IMU_MAGIC: s->step = JC_IMU_FACTORY; break;
    case JC_IMU_USER: case JC_IMU_FACTORY: s->step = JC_IMU_CONFIG; break;
    default: if (s->step < JC_DONE) s->step++; break;
    }
}
// Takes a report; 1 if it answered the current step, which has then moved on
static inline int jc_init_reply(jc_init *s, const uint8_t *r, size_t len) {
    uint8_t cmd[8]; int n = jc_init_command(s, 0, cmd);
    if (!n || len < 15 || r[0] != 0x21 || r[14] != cmd[0]) return 0;
    unsigned a; int want;
    if (!jc_spi_of(s->step, &a, &want)) { jc_init_skip(s); return 1; }
    if (len < 20 + (size_t)want || r[15] != cmd[1] || r[16] != cmd[2] || r[19] != cmd[5]) return 0;
    const uint8_t *d = r + 20; int magic = want == 2 && d[0] == 0xB2 && d[1] == 0xA1;
    switch (s->step) {
    case JC_STICK_MAGIC: s->step = magic ? JC_STICK_USER : JC_STICK_FACTORY; return 1;
    case JC_STICK_USER: case JC_STICK_FACTORY: jc_cal_stick(&s->cal, d); break;
    case JC_IMU_MAGIC: s->step = magic ? JC_IMU_USER : JC_IMU_FACTORY; return 1;
    case JC_IMU_USER: case JC_IMU_FACTORY: jc_cal_imu(&s->cal, d); break;
    }
    jc_init_skip(s);
    return 1;
}
// Output report 0x01: a packet number, a neutral rumble for both motors, the sub-command
static inline void jc_packet(uint8_t out[JC_OUTPUT_LEN], uint8_t counter, const uint8_t *cmd, int n) {
    static const uint8_t neutral[8] = {0x00, 0x01, 0x40, 0x40, 0x00, 0x01, 0x40, 0x40};
    memset(out, 0, JC_OUTPUT_LEN);
    out[0] = 0x01; out[1] = counter & 0xf; memcpy(out + 2, neutral, sizeof neutral); memcpy(out + 10, cmd, (size_t)n);
}
