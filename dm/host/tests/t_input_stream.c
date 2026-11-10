// hle_audio.c unit test: SDK 21's InputStream on the Mac's microphone (DM_MIC=1). No Audio Queue starts (the test marks its stream started itself, so macOS asks for no
// permission): frames go in through the capture callback, as 10 ms buffers would. With DM_MIC=1 one virtual input device is listed and the raw one named; a stream opens with its
// format and capacity, takes the captured frames oldest first, drops the oldest when it is full, converts to PCM16 or float on its channels, applies its gain, purges and closes.
#include "../hle_audio.c"
#include <time.h>
__thread _Atomic int dm_host_work;   // defined by the graphics host in a full build
uint64_t now_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000000ull + t.tv_nsec; }
static struct { const void *k; void *v; } om[8];
void *objmap_get(const void *k) { for (int i = 0; i < 8; i++) if (om[i].k == k) return om[i].v; return NULL; }
void objmap_put(const void *k, void *v) { for (int i = 0; i < 8; i++) if (!om[i].k) { om[i].k = k; om[i].v = v; return; } }
void *objmap_del(const void *k) { for (int i = 0; i < 8; i++) if (om[i].k == k) { om[i].k = 0; return om[i].v; } return NULL; }
void hle_signal_system_event(void *g) { (void)g; }
void hle_bind_system_event(void *g, int autoclear) { (void)g; (void)autoclear; }
module_t *module_of(const void *p) { (void)p; return NULL; }
int dm_guest_as_contains(uintptr_t a) { (void)a; return 0; }
void dm_host_helper_qos(qos_class_t q) { (void)q; }

#define FAIL(...) do { printf("FAIL " __VA_ARGS__); puts(""); return 1; } while (0)
static void capture(const int16_t *pcm, int n) {   // one capture buffer, as the Audio Queue hands it over
    struct AudioQueueBuffer b = {.mAudioDataBytesCapacity = (UInt32)(n * 2), .mAudioData = (void *)pcm, .mAudioDataByteSize = (UInt32)(n * 2)};
    mic_cb(NULL, NULL, &b, NULL, (UInt32)n, NULL);
}
int main(void) {
    setenv("DM_MIC", "1", 1); setenv("DM_MIC_GAIN", "1", 1);   // a fixed gain of 1: the frames arrive as captured
    enum { CAP = 960 };
    char names[2][0x100] = {{0}}, raw[0x100], a[0x350] = {0}, b[0x350] = {0};
    if (h_is_List((char *)names, 2) != 1 || !names[0][0]) FAIL("DM_MIC=1 lists one virtual input device");
    h_is_RawDev(raw); if (!raw[0]) FAIL("the raw input device has a name");
    if (h_is_State(a) != OS_NONE || h_is_Filled(a) || h_is_Capacity(a) || h_is_GetGain(a) != 1.0f) FAIL("a stream never opened: closed, nothing captured, gain 1");
    int32_t bad[3] = {OS_PCM16, 1, 44100};
    if (!h_is_Open(a, NULL, 0, names[0], bad, CAP)) FAIL("a rate the host does not capture at fails the open");
    int32_t mono[3] = {OS_PCM16, 1, 48000}, stereo[3] = {OS_FLOAT, 2, 0};
    if (h_is_Open(a, NULL, 0, names[0], mono, CAP) || h_is_Open(b, NULL, 0, raw, stereo, CAP)) FAIL("open");
    if (h_is_State(a) != OS_STOPPED || h_is_Capacity(a) != CAP || h_is_Free(a) != CAP || h_is_Filled(a)) FAIL("after open: state %d, capacity %d, free %d", h_is_State(a), h_is_Capacity(a), h_is_Free(a));
    os_format f = h_is_Format(b); if (f.format != OS_FLOAT || f.channels != 2 || f.rate != 48000) FAIL("the stereo float stream's format: %d %d %d", f.format, f.channels, f.rate);
    int16_t pcm[480]; for (int k = 0; k < 480; k++) pcm[k] = (int16_t)(k * 10 - 2400);
    capture(pcm, 480);
    if (h_is_Filled(a)) FAIL("a stopped stream takes no frames");
    ((istream *)objmap_get(a))->state = OS_STARTED; ((istream *)objmap_get(b))->state = OS_STARTED;   // started without the Audio Queue
    capture(pcm, 480);
    if (h_is_Filled(a) != 480 || h_is_Free(a) != CAP - 480 || h_is_Filled(b) != 480) FAIL("10 ms captured: filled %d, free %d", h_is_Filled(a), h_is_Free(a));
    int16_t out[480]; if (h_is_Read(a, out, 100) != 100 || memcmp(out, pcm, 200)) FAIL("the first 100 frames, as captured");
    float fo[2 * 480]; if (h_is_Read(b, fo, 480) != 480 || fo[0] != pcm[0] / 32768.0f || fo[1] != fo[0] || fo[2 * 479] != pcm[479] / 32768.0f) FAIL("float frames on both channels");
    capture(pcm, 480); capture(pcm, 480);   // 380 + 960 frames into room for 960: the oldest 380 go
    if (h_is_Filled(a) != CAP || ((istream *)objmap_get(a))->dropped != 380) FAIL("full: filled %d, dropped %llu", h_is_Filled(a), (unsigned long long)((istream *)objmap_get(a))->dropped);
    if (h_is_Read(a, out, 480) != 480 || memcmp(out, pcm, sizeof out)) FAIL("after the overflow the oldest frame left is the start of the second buffer");
    h_is_SetGain(a, 2.0f, 0); if (h_is_GetGain(a) != 2.0f) FAIL("gain kept");
    if (h_is_Read(a, out, 10) != 10 || out[0] != 2 * pcm[0] || out[9] != 2 * pcm[9]) FAIL("the gain applied as frames are read: %d", out[0]);
    h_is_Purge(a); if (h_is_Filled(a) || h_is_Read(a, out, 10)) FAIL("a purge drops what was not read");
    h_is_Close(a); h_is_Close(b);
    if (h_is_State(a) != OS_NONE || h_is_Filled(b)) FAIL("closed");
    puts("input stream: listing, open, capture, overflow, formats, gain, purge and close passed");
    return 0;
}
