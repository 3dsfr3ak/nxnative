// Mac: a Joy-Con over Bluetooth, right or left, as a raw HID pad (joycon.h, pad_hid_mac.h). GameController lists a lone one with no extended gamepad profile, and merges a left and a
// right one into one controller with no motion, so front_mac.m gives neither a player (gc_joycon_raw); here each Joy-Con is a
// hid_pad with a slot like any other, its buttons and stick written into pad_hid so front_mac.m's focus, slot and merge handling apply to it unchanged. Runs on the main run loop.
// Unlike the Pro Controller's passive reader (motion_pro_mac.h) this one writes: the Joy-Con is asked for its calibration, its IMU and the full report mode (output report 0x01), one sub-command at a
// time, each answered in report 0x21; a step with no answer is sent again and then skipped, keeping the nominal scales. The device is opened nonexclusive, so GameController keeps it too.
#pragma once
#include "joycon.h"

typedef struct joycon_dev { hid_pad *pad; jc_init init; uint8_t report[512]; uint8_t counter; int left, tries, idle, live, said_fail; CFRunLoopTimerRef timer; uint64_t last_t; } joycon_dev;   // left: a left Joy-Con
enum { JC_TICK_MS = 100, JC_TRIES = 5, JC_IDLE_TICKS = 30, JC_SAMPLE_NS = 5000000 };

static void jc_send(joycon_dev *j) {
    uint8_t cmd[8], out[JC_OUTPUT_LEN]; int n = jc_init_command(&j->init, hidpad_slot(j->pad), cmd); if (!n) return;
    jc_packet(out, j->counter++, cmd, n);
    IOReturn r = IOHIDDeviceSetReport(j->pad->hid, kIOHIDReportTypeOutput, out[0], out, sizeof out);
    if (r != kIOReturnSuccess && !j->said_fail++) LOG("[RED] Joy-Con: cannot write to the controller (0x%x, sub-command 0x%02x); it may stay without its calibration or motion", r, cmd[0]);
}
static void jc_stop_timer(joycon_dev *j) { if (j->timer) { CFRunLoopTimerInvalidate(j->timer); CFRelease(j->timer); j->timer = NULL; } }
static void jc_tick(CFRunLoopTimerRef t, void *ctx) {
    (void)t;
    joycon_dev *j = ctx;
    if (j->init.step == JC_DONE) {   // set up: wait for the stream, and say if it never comes
        if (j->live) { jc_stop_timer(j); return; }
        if (++j->idle == JC_IDLE_TICKS) { LOG("[RED] Joy-Con: no motion in %d s after setup; its buttons still work if reports arrive", JC_IDLE_TICKS * JC_TICK_MS / 1000); jc_stop_timer(j); }
        return;
    }
    if (++j->tries >= JC_TRIES) {
        LOG("[RED] Joy-Con: no answer to setup step %d, going on without it", j->init.step);
        jc_init_skip(&j->init); j->tries = 0;
    }
    jc_send(j);
}
static void jc_report(void *ctx, IOReturn result, void *sender, IOHIDReportType type, uint32_t report_id, uint8_t *r, CFIndex len) {
    (void)sender; (void)report_id;
    joycon_dev *j = ctx;
    if (result != kIOReturnSuccess || type != kIOHIDReportTypeInput || len <= 0) return;
    if (j->init.step != JC_DONE && jc_init_reply(&j->init, r, (size_t)len)) {
        j->tries = 0;
        if (j->init.step == JC_DONE) LOG("[hid] %s: set up, stick rest %d %d, gyro scale %d", j->pad->name, j->init.cal.center[0], j->init.cal.center[1], j->init.cal.gyro_scale[0]);
        else jc_send(j);
    }
    jc_input in;
    if (!jc_decode_side(&j->init.cal, r, (size_t)len, &in, j->left)) return;
    pad_hid *st = &j->pad->st;
    st->buttons = in.buttons; st->axis[j->left ? PH_X : PH_Z] = in.stick[0]; st->axis[j->left ? PH_Y : PH_RZ] = -in.stick[1];   // pad_hid_sticks: HID's y grows downward
    hidpad_read(j->pad);
    if (!in.samples) return;
    if (!j->live) { j->live = 1; LOG("[hid] %s: motion is live", j->pad->name); }
    int p = hidpad_motion_player(j->pad); if (p < 0) return;
    uint64_t now = now_ns();
    for (int k = 0; k < in.samples; k++) {
        // Three samples a report, stamped back from its arrival. Reports arrive with jitter, so a stamp can land at or before the last one: every sample still counts, a hair later
        uint64_t t = now - (uint64_t)(in.samples - 1 - k) * JC_SAMPLE_NS;
        if (t <= j->last_t) t = j->last_t + 1000;
        if (t > now) t = now;
        dm_motion_set_hid(p, in.acc[k], in.rot[k], t);
        j->last_t = t;
    }
}
// A Joy-Con found: a hid_pad with this state, attached like any other. 0 if it is not one
static int joycon_found(IOHIDDeviceRef hid) {
    NSNumber *vendor = (__bridge NSNumber *)IOHIDDeviceGetProperty(hid, CFSTR(kIOHIDVendorIDKey)), *product = (__bridge NSNumber *)IOHIDDeviceGetProperty(hid, CFSTR(kIOHIDProductIDKey));
    if (vendor.intValue != JC_VENDOR || (product.intValue != JC_PRODUCT_RIGHT && product.intValue != JC_PRODUCT_LEFT)) return 0;
    int left = product.intValue == JC_PRODUCT_LEFT;
    // macOS lists one Joy-Con as two HID services, the device and AppleGCHIDUserEventDriver over it, neither marked synthetic and each with the reports: as two pads it was players 1 and 2.
    // Both have the device's location ID, which holds its Bluetooth address, so another Joy-Con has its own. The first one found plays; they go away together
    NSNumber *where = (__bridge NSNumber *)IOHIDDeviceGetProperty(hid, CFSTR(kIOHIDLocationIDKey));
    for (hid_pad *o = g_hidpads; o && where; o = o->next) if (o->jc && [where isEqual:(__bridge NSNumber *)IOHIDDeviceGetProperty(o->hid, CFSTR(kIOHIDLocationIDKey))]) return 1;
    hid_pad *d = calloc(1, sizeof *d); joycon_dev *j = calloc(1, sizeof *j);
    if (!d || !j) { free(d); free(j); LOG("[RED] Joy-Con: cannot allocate device state"); return 1; }
    pad_hid_init(&d->st, NULL);
    for (int b = 0; b < 16; b++) d->st.map[b] = (uint8_t)(b + 1);   // buttons holds DM_BTN_* bits already
    if (left) d->st.has[PH_X] = d->st.has[PH_Y] = 1; else d->st.has[PH_Z] = d->st.has[PH_RZ] = 1;   // a left Joy-Con's stick is the left stick
    snprintf(d->name, sizeof d->name, left ? "Joy-Con (L)" : "Joy-Con (R)");
    d->hid = (IOHIDDeviceRef)CFRetain(hid); d->jc = j; d->next = g_hidpads; g_hidpads = d;
    j->pad = d; j->left = left; jc_init_start_side(&j->init, left);
    IOHIDDeviceRegisterInputReportCallback(hid, j->report, sizeof j->report, jc_report, j);
    hidpad_attach(d);
    CFRunLoopTimerContext cx = {.info = j};
    j->timer = CFRunLoopTimerCreate(kCFAllocatorDefault, CFAbsoluteTimeGetCurrent() + JC_TICK_MS / 1000.0, JC_TICK_MS / 1000.0, 0, 0, jc_tick, &cx);
    CFRunLoopAddTimer(CFRunLoopGetMain(), j->timer, kCFRunLoopCommonModes);
    jc_send(j);
    return 1;
}
static void joycon_removed(hid_pad *d) {
    joycon_dev *j = d->jc;
    IOHIDDeviceRegisterInputReportCallback(d->hid, j->report, sizeof j->report, NULL, NULL);   // while its buffer and context still exist
    jc_stop_timer(j); free(j); d->jc = NULL;
}
