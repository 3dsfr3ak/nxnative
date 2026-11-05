// A right Joy-Con's raw reports (joycon.h): buttons, the stick and its calibration, the IMU's frame and scales, the setup sequence.
#include <stdio.h>
#include "../joycon.h"

static int checks, fails;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)
static int near(float a, float b) { return fabsf(a - b) < 1e-3f; }

static void put12(uint8_t *b, int x, int y) { b[0] = x & 0xff; b[1] = (uint8_t)(x >> 8 | (y & 0xf) << 4); b[2] = (uint8_t)(y >> 4); }
static void put16(uint8_t *b, int v) { b[0] = v & 0xff; b[1] = v >> 8 & 0xff; }
static void full(uint8_t r[49], int stickx, int sticky) { memset(r, 0, 49); r[0] = 0x30; put12(r + 9, stickx, sticky); }
// A reply to the SPI read of the current step
static size_t spi_reply(uint8_t *r, const jc_init *s, const uint8_t *data) {
    uint8_t cmd[8]; jc_init_command(s, 0, cmd);
    memset(r, 0, 64); r[0] = 0x21; r[13] = 0x90; r[14] = 0x10; r[15] = cmd[1]; r[16] = cmd[2]; r[19] = cmd[5];
    memcpy(r + 20, data, cmd[5]);
    return 20 + cmd[5];
}

int main(void) {
    jc_cal c; jc_cal_defaults(&c); uint8_t r[64]; jc_input in;

    // Buttons, by the protocol's bits
    full(r, 2048, 2048); r[3] = 0x01; CHECK(jc_decode(&c, r, 49, &in) && in.buttons == DM_BTN_Y);
    r[3] = 0x02; jc_decode(&c, r, 49, &in); CHECK(in.buttons == DM_BTN_X);
    r[3] = 0x04; jc_decode(&c, r, 49, &in); CHECK(in.buttons == DM_BTN_B);
    r[3] = 0x08; jc_decode(&c, r, 49, &in); CHECK(in.buttons == DM_BTN_A);
    r[3] = 0xc0; r[4] = 0x06; jc_decode(&c, r, 49, &in); CHECK(in.buttons == (DM_BTN_R | DM_BTN_ZR | DM_BTN_PLUS | DM_BTN_RS));
    r[3] = 0; r[4] = 0x30; jc_decode(&c, r, 49, &in); CHECK(in.buttons == 0);   // Home and Capture: nothing of the game's
    r[5] = 0xff; r[4] = 0; jc_decode(&c, r, 49, &in); CHECK(in.buttons == 0);   // the left Joy-Con's byte: not this controller's

    // The stick, against its calibration; a reply carries it too, other reports nothing
    full(r, 2048, 2048); jc_decode(&c, r, 49, &in); CHECK(near(in.stick[0], 0) && near(in.stick[1], 0) && in.samples == 0);
    full(r, 2048 + 700, 2048 - 1400); jc_decode(&c, r, 49, &in); CHECK(near(in.stick[0], .5f) && near(in.stick[1], -1));
    full(r, 4000, 0); jc_decode(&c, r, 49, &in); CHECK(near(in.stick[0], 1) && near(in.stick[1], -1));   // clamped
    r[0] = 0x21; CHECK(jc_decode(&c, r, 15, &in) && near(in.stick[0], 1) && in.samples == 0);
    r[0] = 0x3f; CHECK(!jc_decode(&c, r, 49, &in));
    uint8_t st[9]; put12(st, 2000, 1900); put12(st + 3, 1000, 800); put12(st + 6, 500, 400);
    jc_cal k; jc_cal_defaults(&k); jc_cal_stick(&k, st);
    CHECK(k.center[0] == 2000 && k.center[1] == 1900 && k.lo[0] == 1000 && k.lo[1] == 800 && k.hi[0] == 500 && k.hi[1] == 400);
    full(r, 2000 + 250, 1900 - 400); jc_decode(&k, r, 49, &in); CHECK(near(in.stick[0], .5f) && near(in.stick[1], -.5f));
    memset(st, 0xff, 9); jc_cal_stick(&k, st); CHECK(k.center[0] == JC_STICK_CENTER && k.lo[1] == JC_STICK_RANGE && k.hi[0] == JC_STICK_RANGE);   // erased flash

    // The frame: lying face up it reads 1 G down in the Switch frame, whatever the IC's own axes
    full(r, 2048, 2048);
    for (int s = 0; s < 3; s++) put16(r + 13 + 12 * s + 4, -4096);   // raw z of a right Joy-Con face up (hid-nintendo: negated against the Pro Controller)
    jc_decode(&c, r, 49, &in);
    CHECK(in.samples == 3 && near(in.acc[0][0], 0) && near(in.acc[0][1], 0) && near(in.acc[0][2], -1) && near(in.acc[2][2], -1));
    // Each raw axis goes where the map says, with its sign: acc = (-raw1, -raw0, raw2), rot = (raw4, raw3, -raw5)
    full(r, 2048, 2048); put16(r + 13, 4096); put16(r + 15, 2048); put16(r + 17, 1024);
    jc_decode(&c, r, 49, &in); CHECK(near(in.acc[0][0], -.5f) && near(in.acc[0][1], -1) && near(in.acc[0][2], .25f));
    full(r, 2048, 2048); put16(r + 19, 1000); put16(r + 21, 2000); put16(r + 23, 3000);
    float dps = 936.f / JC_GYRO_SCALE * (float)M_PI / 180.f;
    jc_decode(&c, r, 49, &in); CHECK(near(in.rot[0][0], 2000 * dps) && near(in.rot[0][1], 1000 * dps) && near(in.rot[0][2], -3000 * dps));
    // Three samples, in the report's order
    full(r, 2048, 2048); put16(r + 13 + 4, 1000); put16(r + 25 + 4, 2000); put16(r + 37 + 4, 3000);
    jc_decode(&c, r, 49, &in); CHECK(near(in.acc[0][2], 1000 / 4096.f) && near(in.acc[1][2], 2000 / 4096.f) && near(in.acc[2][2], 3000 / 4096.f));
    // A short full report has no samples
    CHECK(jc_decode(&c, r, 40, &in) && in.samples == 0);

    // IMU calibration: scales by raw axis, the gyro's offset taken off
    uint8_t im[24]; memset(im, 0, sizeof im);
    for (int i = 0; i < 3; i++) { put16(im + 6 + 2 * i, 16000); put16(im + 12 + 2 * i, 10); put16(im + 18 + 2 * i, 13010); }
    jc_cal_defaults(&k); jc_cal_imu(&k, im);
    full(r, 2048, 2048); put16(r + 17, -4000); put16(r + 19, 10); put16(r + 21, 1310);
    jc_decode(&k, r, 49, &in);
    CHECK(near(in.acc[0][2], -1)); CHECK(near(in.rot[0][1], 0)); CHECK(near(in.rot[0][0], 1300 * 936.f / 13000 * (float)M_PI / 180.f));
    memset(im, 0xff, sizeof im); jc_cal_imu(&k, im);
    CHECK(k.acc_off[0] == 0 && k.acc_scale[1] == JC_ACC_SCALE && k.gyro_off[2] == 0 && k.gyro_scale[0] == JC_GYRO_SCALE);   // erased flash

    // Setup with factory calibration: the magic is not there
    jc_init s; jc_init_start(&s); uint8_t cmd[8], blank[24] = {0}, magic[2] = {0xB2, 0xA1}, stick[9]; put12(stick, 2100, 1950); put12(stick + 3, 1100, 1200); put12(stick + 6, 1300, 1400);
    CHECK(jc_init_command(&s, 0, cmd) == 6 && cmd[0] == 0x10 && cmd[1] == 0x1B && cmd[2] == 0x80 && cmd[5] == 2);
    size_t n = spi_reply(r, &s, blank); CHECK(jc_init_reply(&s, r, n) && s.step == JC_STICK_FACTORY);
    jc_init_command(&s, 0, cmd); CHECK(cmd[1] == 0x46 && cmd[2] == 0x60 && cmd[5] == 9);
    CHECK(!jc_init_reply(&s, r, n));   // the old reply: another address
    n = spi_reply(r, &s, stick); CHECK(jc_init_reply(&s, r, n) && s.step == JC_IMU_MAGIC && s.cal.center[0] == 2100);
    n = spi_reply(r, &s, blank); CHECK(jc_init_reply(&s, r, n) && s.step == JC_IMU_FACTORY);
    jc_init_command(&s, 0, cmd); CHECK(cmd[1] == 0x20 && cmd[2] == 0x60 && cmd[5] == 24);
    memset(im, 0, sizeof im); for (int i = 0; i < 3; i++) { put16(im + 6 + 2 * i, 16000); put16(im + 18 + 2 * i, 13000); }
    n = spi_reply(r, &s, im); CHECK(jc_init_reply(&s, r, n) && s.step == JC_IMU_CONFIG && s.cal.acc_scale[0] == 16000);
    CHECK(jc_init_command(&s, 0, cmd) == 5 && cmd[0] == 0x41 && cmd[1] == 3 && cmd[2] == 0);
    memset(r, 0, 64); r[0] = 0x21; r[14] = 0x41; CHECK(jc_init_reply(&s, r, 20) && s.step == JC_IMU_ON);
    CHECK(jc_init_command(&s, 0, cmd) == 2 && cmd[0] == 0x40 && cmd[1] == 1);
    r[14] = 0x41; CHECK(!jc_init_reply(&s, r, 20));   // not this step's answer
    r[0] = 0x30; r[14] = 0x40; CHECK(!jc_init_reply(&s, r, 49));   // a state report that happens to hold the byte
    r[0] = 0x21; CHECK(jc_init_reply(&s, r, 20) && s.step == JC_MODE);
    CHECK(jc_init_command(&s, 0, cmd) == 2 && cmd[0] == 0x03 && cmd[1] == 0x30);
    r[14] = 0x03; CHECK(jc_init_reply(&s, r, 20) && s.step == JC_LIGHT);
    CHECK(jc_init_command(&s, 2, cmd) == 2 && cmd[0] == 0x30 && cmd[1] == 4);   // player 3's light
    r[14] = 0x30; CHECK(jc_init_reply(&s, r, 20) && s.step == JC_DONE);
    CHECK(jc_init_command(&s, 0, cmd) == 0 && !jc_init_reply(&s, r, 20));

    // With user calibration
    jc_init_start(&s); n = spi_reply(r, &s, magic); CHECK(jc_init_reply(&s, r, n) && s.step == JC_STICK_USER);
    jc_init_command(&s, 0, cmd); CHECK(cmd[1] == 0x1D && cmd[2] == 0x80 && cmd[5] == 9);
    n = spi_reply(r, &s, stick); CHECK(jc_init_reply(&s, r, n) && s.step == JC_IMU_MAGIC);
    n = spi_reply(r, &s, magic); CHECK(jc_init_reply(&s, r, n) && s.step == JC_IMU_USER);
    jc_init_command(&s, 0, cmd); CHECK(cmd[1] == 0x28 && cmd[2] == 0x80 && cmd[5] == 24);

    // No answer at all: every step skipped reaches the end with the nominal scales
    jc_init_start(&s); int steps = 0; while (s.step != JC_DONE && steps++ < 20) jc_init_skip(&s);
    CHECK(s.step == JC_DONE && steps < 20 && s.cal.acc_scale[0] == JC_ACC_SCALE && s.cal.gyro_scale[2] == JC_GYRO_SCALE);

    // The output report
    uint8_t out[JC_OUTPUT_LEN]; uint8_t sub[2] = {0x40, 1}; jc_packet(out, 0x13, sub, 2);
    CHECK(out[0] == 1 && out[1] == 3 && out[3] == 1 && out[4] == 0x40 && out[10] == 0x40 && out[11] == 1 && out[12] == 0);

    printf("t_joycon: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
