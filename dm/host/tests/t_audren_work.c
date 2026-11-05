// Exercise the public size -> caller allocation -> both SDK open routes -> close boundary.
// The service requirement must not be inflated by the local backend's different command representation.
#include "../hle.h"
#include <assert.h>
#include <errno.h>

static void *backing;
static size_t backing_size;
static int allocations, frees, fail_alloc;
static int work_alloc(void **out, size_t align, size_t size) {
    if (fail_alloc) return ENOMEM;
    int r = posix_memalign(out, align, size);
    if (!r) { assert(!backing); backing = *out; backing_size = size; allocations++; }
    return r;
}
static int stream_frees;   // the output stream test: its blocks are not the renderer's work buffer
static void work_free(void *p) {
    if (p && !stream_frees) { assert(p == backing); backing = NULL; backing_size = 0; frees++; }
    free(p);
}
#define posix_memalign work_alloc
#define free work_free
#include "../hle_audren.c"
#undef posix_memalign
#undef free

enum { SDK_WORK = 0x760000, SERVICE_WORK = 0x61d000 };
static int opens, starts, joins, closes, fail_open, nested;
static int event_destroys, event_binds, attaches, event_in_handle;
static void *sdk_event, *game_event;
static void *sdk_work;
static void *map_key, *map_value;
static size_t sdk_size(const uint8_t *param) {
    assert(*(int32_t *)(param + P_DEVICE) == 1 && *(int32_t *)(param + P_MODE) == 1);
    assert((*(uint32_t *)(param + 0x34) & 0xffffff) == 0x564552 || (*(uint32_t *)(param + 0x38) & 0xffffff) == 0x564552);
    return SDK_WORK;
}
static Result sdk_open(void **handle, const uint8_t *param, void *work, size_t size) {
    opens++;
    size_t need = h_ar_WorkSize(param);   // SDK opens can query the already converted parameter through the PLT
    assert(need == SDK_WORK);
    if (!work || ((uintptr_t)work & (WORK_PAGE - 1)) || size < need) return RES_FAIL;
    if (fail_open) return RES_FAIL;
    assert(!backing || (work == backing && size == backing_size));
    sdk_work = work;
    memset(work, 0x5a, need);   // touch the entire area, not just the handle
    handle[0] = work; handle[1] = (uint8_t *)work + 0x80;
    return RES_OK;
}
static Result sdk_open_ev(void **handle, void *ev, const uint8_t *param, void *work, size_t size) {
    assert(ev && ev != game_event);   // the SDK's CPU / manual open keeps this object and signals it on every render: the renderer's, never the game's
    sdk_event = ev;
    Result r = nested ? h_ar_Open0(handle, param, work, size) : sdk_open(handle, param, work, size);
    if (!r && event_in_handle) handle[1] = ev;   // SDK 3 to 12 keep the event in the handle (SDK 3 sdk+0x17dffc, SDK 12 sdk+0x4d274); SDK 18 keeps it at *handle[1]
    return r;
}
static void sdk_destroy_event(void *ev) { assert(ev == sdk_event); event_destroys++; }
static uint32_t sdk_readable(const void *ev) { assert(ev == sdk_event && event_binds); return 0x1002; }
static void sdk_attach(void *ev, uint32_t h, int managed, int clear) {   // the game's object gets the copied read end, managed by the game, auto-clear: what the DSP open attaches
    assert(ev == game_event && h == 0x1003 && managed == 1 && clear == 1);
    attaches++;
}
static void sdk_close(void *obj, void *state) {
    assert(obj == sdk_work && state == (event_in_handle ? sdk_event : (uint8_t *)sdk_work + 0x80));
    assert(((uint8_t *)obj)[SDK_WORK - 1] == 0x5a);   // storage is alive through SDK teardown
    closes++;
}
void *guest_sym(const char *name) {
    if (!strcmp(name, hle_force_h_ar_WorkSize.name)) return sdk_size;
    if (!strcmp(name, hle_force_h_ar_Open.name)) return sdk_open_ev;
    if (!strcmp(name, hle_force_h_ar_Open0.name)) return sdk_open;
    if (!strcmp(name, hle_force_h_ar_Close.name)) return sdk_close;
    if (!strcmp(name, "_ZN2nn2os18DestroySystemEventEPNS0_15SystemEventTypeE")) return sdk_destroy_event;
    if (!strcmp(name, "_ZN2nn2os30GetReadableHandleOfSystemEventEPKNS0_15SystemEventTypeE")) return sdk_readable;
    if (!strcmp(name, "_ZN2nn2os33AttachReadableHandleToSystemEventEPNS0_15SystemEventTypeEjbNS0_14EventClearModeE")) return sdk_attach;
    return NULL;
}
void *objmap_get(const void *key) { return key == map_key ? map_value : NULL; }
void objmap_put(const void *key, void *value) { assert(!map_key); map_key = (void *)key; map_value = value; }
void *objmap_del(const void *key) {
    if (key != map_key) return NULL;
    void *value = map_value; map_key = map_value = NULL; return value;
}
void *dm_guest_thread_spawn(void (*fn)(void *), void *arg, int prio, size_t stack) {
    (void)fn; (void)arg; (void)prio; (void)stack; starts++; return (void *)1;
}
void dm_guest_thread_join(void *tt) {
    assert(tt == (void *)1 && !atomic_load(&g_ar[0].open));
    assert(((uint8_t *)sdk_work)[SDK_WORK - 1] == 0x5a);   // storage also survives the render thread
    joins++;
}
uint64_t now_ns(void) { return 0; }
uint64_t hle_system_event_binding(void *g) { (void)g; return 0; }
int hle_signal_system_event_binding(uint64_t binding) { (void)binding; return 0; }
module_t *module_of(const void *p) { (void)p; return NULL; }
// A host output for the output stream test: records what is appended, and hands blocks back when the test says they played. Off (NULL), as for the renderer tests
static int out_on, out_opened, out_closed, out_started, out_full; static void *out_q[64]; static unsigned out_w, out_r; static uint8_t out_bytes[1 << 16]; static size_t out_len;
void *dm_pcmout_open(int ch) { (void)ch; if (!out_on) return NULL; out_opened++; return &out_on; }
void dm_pcmout_close(void *k) { (void)k; out_closed++; }
void dm_pcmout_start(void *k) { (void)k; out_started++; }
int dm_pcmout_append(void *k, void *b) {
    (void)k; if (!out_on || out_full) return 0;
    const gbuf *g = b; memcpy(out_bytes + out_len, g->ptr, g->size); out_len += g->size; out_q[out_w++ % 64] = b; return 1;
}
static unsigned out_played;
void *dm_pcmout_released(void *k) { (void)k; return out_r < out_played ? out_q[out_r++ % 64] : NULL; }
void dm_pcmout_wait(void *k, int ms) { (void)k; (void)ms; }
void hle_bind_system_event(void *ev, int ac) {
    assert(ev == sdk_event && ac == 1 && event_destroys == 2 * event_binds + 1);   // after the open destroys the SDK's local event (each earlier Close destroyed one more)
    event_binds++;
}
uint32_t hle_dup_readable_handle(uint32_t h) { assert(h == 0x1002); return 0x1003; }

// An output stream: frames reach the output in blocks and in order, what has played makes the room, Stop lets the last part out, Close closes it; a format the output does not take stays silent
static void t_output_stream(void) {
    stream_frees = 1;
    static int16_t in[2 * 4096]; for (int i = 0; i < 2 * 4096; i++) in[i] = (int16_t)(i * 7 + 1);
    int32_t fmt[3] = {2, 2, 48000}; char type[64], name[8] = {0};
    out_on = 1;
    assert(h_os_Open(type, NULL, NULL, 0, name, fmt, 960, 480) == RES_OK && out_opened == 1);
    assert(h_os_Free(type) == 960 && h_os_State(type) == 1);
    assert(h_os_Write(type, in, 100) == 100 && out_len == 0);                  // part of a block waits
    assert(h_os_Free(type) == 860);
    assert(h_os_Write(type, in + 200, 300) == 300 && out_len == 240 * 4);      // one block out, 160 frames waiting
    assert(!memcmp(out_bytes, in, 240 * 4));
    assert(h_os_Start(type) == RES_OK && out_started == 1 && h_os_State(type) == 2);
    assert(h_os_Start(type) == RES_OK && out_started == 1);                    // started once
    assert(h_os_Free(type) == 560);
    out_played = 1; assert(h_os_Free(type) == 800);                            // what played makes room
    assert(h_os_Write(type, in + 800, 2000) == 800);                           // only what fits
    assert(h_os_Free(type) == 0 && h_os_Write(type, in, 10) == 0);
    assert(out_len == 1200 * 4 && !memcmp(out_bytes, in, 1200 * 4));           // in order, across the writes, whole blocks all out
    out_played = 2; assert(h_os_Free(type) == 240);
    assert(h_os_Write(type, in + 2400, 100) == 100 && out_len == 1200 * 4);    // part of a block waits
    h_os_Stop(type); assert(h_os_State(type) == 1);
    assert(out_len == 1300 * 4 && !memcmp(out_bytes, in, 1300 * 4));           // the last part goes out
    h_os_Stop(type); assert(out_len == 1300 * 4);
    out_full = 1; assert(h_os_Open(type, NULL, NULL, 0, name, fmt, 960, 480) == RES_OK && out_opened == 2 && out_closed == 1);   // opened again: the old output goes
    size_t held = out_len;
    assert(h_os_Write(type, in, 240) == 240 && out_len == held);               // a full output: the finished block waits, counted
    assert(h_os_Write(type, in + 480, 10) == 0 && h_os_Free(type) == 720);     // nothing more until it goes
    out_full = 0;
    assert(h_os_Write(type, in + 480, 10) == 10 && out_len == held + 240 * 4 && !memcmp(out_bytes + held, in, 240 * 4));   // it goes first, nothing lost
    h_os_Close(type); assert(out_closed == 2 && h_os_State(type) == 0 && atomic_load(&g_os_audible) == 0);
    fmt[0] = 1; assert(h_os_Open(type, NULL, NULL, 0, name, fmt, 960, 480) == RES_OK && out_opened == 2);   // another format: silent
    assert(h_os_Write(type, in, 100) == 100 && h_os_Free(type) == 860 && out_len == held + 240 * 4);
    h_os_Close(type); assert(out_closed == 2);
    fmt[0] = 2; char more[OS_AUDIBLE_MAX + 1][8];
    for (int i = 0; i <= OS_AUDIBLE_MAX; i++) assert(h_os_Open(more[i], NULL, NULL, 0, name, fmt, 960, 480) == RES_OK);
    assert(out_opened == 2 + OS_AUDIBLE_MAX);                                  // past the limit: silent, not a stop
    for (int i = 0; i <= OS_AUDIBLE_MAX; i++) h_os_Close(more[i]);
    assert(atomic_load(&g_os_audible) == 0);
    out_on = 0; stream_frees = 0;
}

int main(void) {
    uint8_t param[P_SIZE] = {0}, saved[P_SIZE];
    const uint32_t words[] = {48000, 240, 132, 19, 138, 1, 16, 0, 0, 85, 1800, 0, 0, 0x38564552};
    memcpy(param, words, sizeof words);
    t_output_stream();
    memcpy(saved, param, sizeof param);
    // Expected results were obtained by executing Horizon audio build 3a75b4a6's sizing routine,
    // independently of this implementation. A changed count must change the service requirement.
    const struct { int revision, word, value; size_t expected; } cases[] = {
        {8, 4, 138, 0x61d000}, {8, 4, 137, 0x61b000}, {8, 3, 18, 0x5f7000},
        {8, 10, 1799, 0x61c000}, {8, 7, 2, 0x622000}, {9, 4, 138, 0x61e000},
        {12, 4, 138, 0x5f7000}, {14, 4, 138, 0x621000}, {15, 4, 138, 0x6a4000}
    };
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        memcpy(param, saved, sizeof param);
        *(uint32_t *)(param + 0x34) = 0x30564552 + ((uint32_t)cases[i].revision << 24);
        ((uint32_t *)param)[cases[i].word] = cases[i].value;
        size_t got = h_ar_WorkSize(param);
        if (got != cases[i].expected) { fprintf(stderr, "service size case %zu: expected %#zx got %#zx\n", i, cases[i].expected, got); return 1; }
    }
    memcpy(param, saved, sizeof param);
    *(uint32_t *)(param + 0x34) = 0x34564552;
    *(uint32_t *)(param + 0x24) = *(uint32_t *)(param + 0x28) = 0;
    assert(h_ar_WorkSize(param) == 0x308000);   // fixed command buffer and performance format 1
    // The longer SDK 8 public layout, padding bytes, and unknown future revisions.
    memcpy(param, saved, sizeof param);
    *(uint32_t *)(param + 0x34) = 0;
    *(uint32_t *)(param + 0x38) = 0x38564552;
    param[0x21] = param[0x22] = param[0x23] = 0xff;
    assert(h_ar_WorkSize(param) == SERVICE_WORK);
    memcpy(param, saved, sizeof param);
    *(uint32_t *)(param + 0x34) = 0x40564552;
    assert(h_ar_WorkSize(param) == SDK_WORK);
    memcpy(param, saved, sizeof param);
    assert(h_ar_WorkSize(param) == SERVICE_WORK);
    void *work; assert(!posix_memalign(&work, WORK_PAGE, SDK_WORK));
    memset(work, 0xa5, SDK_WORK);
    void *handle[2], *event[8]; game_event = event;
    for (int route = 0; route < 4; route++) {   // no event, event, nested open, event kept in the handle (SDK 3 to 12)
        nested = route == 2; event_in_handle = route == 3;
        int a = allocations, s = starts, c = closes;
        Result r = route ? h_ar_Open(handle, event, param, work, SERVICE_WORK) : h_ar_Open0(handle, param, work, SERVICE_WORK);
        assert(!r && allocations == a + 1 && starts == s + 1);
        assert(handle[0] != work && map_value == backing);
        assert(((uint8_t *)work)[0] == 0xa5 && ((uint8_t *)work)[SERVICE_WORK - 1] == 0xa5);
        assert(!memcmp(param, saved, sizeof param));
        if (route) {
            assert((uint8_t *)sdk_event >= (uint8_t *)g_rev && (uint8_t *)sdk_event < (uint8_t *)(g_rev + 4));
            assert(g_ar[0].handle[1] == (event_in_handle ? sdk_event : (uint8_t *)sdk_work + 0x80));
            memset(event, 0xee, sizeof event);   // the game may reuse its object at once (LEGO Star Wars: on its stack; Little Nightmares): nothing refers to it
        }
        int d = event_destroys;
        h_ar_Close(handle[0], handle[1]);
        assert(!backing && !map_key && allocations == frees && closes == c + 1 && joins == starts);
        assert(event_destroys == d + !!route && !rev_find(handle[0]));   // Close destroys the renderer's event and frees its slot
    }
    event_in_handle = nested = 0;
    assert(event_destroys == 6 && event_binds == 3 && attaches == 3);   // each event open replaces the SDK's local event once, attaches the game's, and Close destroys it
    fail_open = 1;
    int s = starts;
    assert(h_ar_Open0(handle, param, work, SERVICE_WORK));
    assert(!backing && !map_key && allocations == frees && starts == s);
    fail_open = 0; fail_alloc = 1;
    int o = opens;
    assert(h_ar_Open0(handle, param, work, SERVICE_WORK));
    assert(opens == o && starts == s && !backing);
    fail_alloc = 0;
    // Invalid caller storage is still rejected, without giving it a host allocation.
    int a = allocations;
    assert(h_ar_Open0(handle, param, NULL, WORK_PAGE));
    assert(h_ar_Open0(handle, param, (uint8_t *)work + 1, WORK_PAGE));
    assert(h_ar_Open0(handle, param, work, WORK_PAGE - 1));
    assert(h_ar_Open0(handle, param, work, SERVICE_WORK - WORK_PAGE));
    assert(allocations == a && starts == s);
    // An existing full-size buffer and an explicitly CPU renderer retain caller ownership.
    for (int cpu = 0; cpu < 2; cpu++) {
        *(int32_t *)(param + P_DEVICE) = cpu;
        if (cpu) assert(h_ar_WorkSize(param) == SDK_WORK);
        assert(!h_ar_Open0(handle, param, work, SDK_WORK));
        assert(handle[0] == work && allocations == a && !map_key);
        h_ar_Close(handle[0], handle[1]);
    }
    free(work);
    puts("audren work buffer: Horizon sizing fixtures, both opens, nested open, failures and close lifetime passed");
    return 0;
}
