// The tables for handles traced to no slot (nvn_mtl.m pool_gpu) follow the IDs that changed: what no entry depends on leaves the table alone, an ID that changed makes a version that differs in that entry,
// a new epoch, a table that fell behind or (with every registered texture made) a memory pool walks again, a walk that finds the same entries keeps the buffer, a version the GPU may hold is not written,
// and a guest write reaches a texture only a table reads.
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t *g_mod; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int checks, failed;
#define CHECK(label, c) do { checks++; if (!(c)) { failed++; fprintf(stderr, "FAIL: %s (line %d)\n", label, __LINE__); } } while (0)
static void finish(void) { CHECK("submission completes", mtl_wait(mtl_sub_commit(), 5000000000ull)); CHECK("GPU has no submission error", !mtl_last_commit_error()); }
static const uint64_t *ents(id<MTLBuffer> b) { return (const uint64_t *)((const uint8_t *)b.contents + DMSH_POOL_HEAD); }
static id<MTLBuffer> tab(void) { __unsafe_unretained id<MTLBuffer> pb = nil; (void)pool_gpu(0, MTLTextureType2D, NVN_DPF_FLOAT, 0, &pb); return pb; }
static id<MTLBuffer> stab(void) { __unsafe_unretained id<MTLBuffer> pb = nil; (void)pool_gpu(1, 0, 0, 0, &pb); return pb; }
static uint64_t bw, bg;
static void mark(void) { bw = g_stat_pool_full; bg = g_psnap_gen; }
#define MOVED(label, w, v) do { CHECK(label ": walks", g_stat_pool_full - bw == (w)); CHECK(label ": versions", g_psnap_gen - bg == (v)); mark(); } while (0)
static pool_t pool; static uint8_t *mem;
static tex_t mk(int k) {
    tex_t t = {.magic = MAGIC('T', 'E', 'X', 0), .has_layout = 1, .serial = 99800 + (uint64_t)k, .pool = &pool};
    t.d = (nvn_texdesc){.target = 1, .format = NVN_FORMAT_RGBA8, .width = 64, .height = 64, .depth = 1, .levels = 1, .samples = 1}; nvn_tex_layout(&t.d, &t.L);
    t.mem = mem + (uint64_t)k * (1u << 20);
    mtl_tex_init(&t);
    return t;
}
static void fill(const tex_t *t, uint32_t seed, uint32_t *lin) {
    for (uint32_t i = 0; i < 64 * 64; i++) lin[i] = (i + seed) * 2654435761u;
    dm_guest_write_prepare(t->mem, t->L.size);
    CHECK("swizzle", !nvn_swizzle(&t->d, 0, 0, lin, 64 * 4, t->mem));
}
static int holds(id<MTLTexture> image, const uint32_t *lin) {
    id<MTLBuffer> rb = [g_dev newBufferWithLength:64 * 64 * 4 options:MTLResourceStorageModeShared];
    id<MTLCommandBuffer> cb = q_cb(); id<MTLBlitCommandEncoder> ce = [cb blitCommandEncoder];
    [ce copyFromTexture:image sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(64, 64, 1) toBuffer:rb destinationOffset:0 destinationBytesPerRow:64 * 4 destinationBytesPerImage:64 * 64 * 4];
    [ce endEncoding]; [cb commit]; [cb waitUntilCompleted];
    return !memcmp(rb.contents, lin, 64 * 64 * 4);
}
int main(void) { @autoreleasepool {
    mtl_init();
    const size_t size = 64u << 20;
    if (posix_memalign((void **)&mem, (size_t)getpagesize(), size)) abort();
    memset(mem, 0, size);
    pool = (pool_t){.magic = MAGIC('P', 'O', 'O', 'L'), .mem = mem, .size = size, .flags = NVN_MEMORY_POOL_FLAGS_CPU_UNCACHED | NVN_MEMORY_POOL_FLAGS_GPU_CACHED};
    mtl_pool_add(mem, size); nvn_dirty_add_range(mem, size);
    uint32_t *lin = malloc(64 * 64 * 4);
    tex_t a = mk(1), b = mk(2), c = mk(3), d = mk(4), e = mk(5);
    fill(&a, 1, lin);
    samp_par sp; memset(&sp, 0, sizeof sp); mtl_reg_samp(1, &sp);
    mtl_reg_tex(5, &a, NULL); mtl_reg_tex(6, &b, NULL);
    mark(); mtl_sub_begin();

    // The first table walks; making its textures noted their serials, so the next look checks those IDs and finds them the same
    id<MTLBuffer> v1 = tab(); MOVED("the first table", 1, 1);
    const uint64_t none = ents(v1)[*(const uint32_t *)v1.contents];
    CHECK("registered IDs hold their textures", ents(v1)[5] != none && ents(v1)[6] != none && ents(v1)[5] != ents(v1)[6]);
    CHECK("an empty ID holds the dummy", ents(v1)[7] == none);
    CHECK("the entry is the texture's", ents(v1)[5] == g_treg[5].view_rid);
    CHECK("nothing changed: the same buffer", tab() == v1); MOVED("nothing changed", 0, 0);

    // What no entry depends on
    ser_note(424242); CHECK("a serial no ID holds: the same buffer", tab() == v1); MOVED("a serial no ID holds", 0, 0);
    uint64_t rg = atomic_load(&g_reg_gen); mtl_reg_samp(2, &sp); CHECK("that moves the registration generation", atomic_load(&g_reg_gen) != rg);
    CHECK("a sampler registered: the same texture table", tab() == v1); MOVED("a sampler registered", 0, 0);
    ser_note(a.serial); CHECK("a serial an ID holds, its texture the same: the same buffer", tab() == v1); MOVED("the same texture", 0, 0);

    // A registration: a version that differs in that entry, and the version in use stays as it was
    mtl_reg_tex(7, &c, NULL);
    id<MTLBuffer> v2 = tab(); MOVED("a registration", 0, 1);
    CHECK("a new version", v2 != v1);
    CHECK("the new ID holds its texture", ents(v2)[7] != none && ents(v2)[7] == g_treg[7].view_rid);
    CHECK("the others are kept", ents(v2)[5] == ents(v1)[5] && ents(v2)[6] == ents(v1)[6] && ents(v2)[8] == none);
    CHECK("the version in use is not written", ents(v1)[7] == none);
    mtl_reg_tex(7, &c, NULL); CHECK("the same registration again: the same buffer", tab() == v2); MOVED("the same registration", 0, 0);
    mtl_reg_tex(7, &d, NULL);
    id<MTLBuffer> v3 = tab(); MOVED("another texture for the ID", 0, 1);
    CHECK("the ID holds the other texture", ents(v3)[7] == g_treg[7].view_rid && ents(v3)[7] != ents(v2)[7]);
    mtl_reg_tex(7, NULL, NULL);
    id<MTLBuffer> v4 = tab(); MOVED("an ID emptied", 0, 1);
    CHECK("the emptied ID holds the dummy", ents(v4)[7] == none);

    // Its memory pool gone, and registered again
    mtl_reg_tex_gone(6);
    id<MTLBuffer> v5 = tab(); MOVED("gone", 0, 1);
    CHECK("a gone ID holds the dummy", ents(v5)[6] == none && ents(v5)[5] == ents(v1)[5]);
    mtl_reg_tex(6, &b, NULL);
    id<MTLBuffer> v6 = tab(); MOVED("registered again", 0, 1);
    CHECK("registered again it holds a texture", ents(v6)[6] != none && ents(v6)[6] == g_treg[6].view_rid);
    finish(); mtl_sub_begin(); (void)tab(); mark();

    // Walks: a new epoch, a table behind the log, a pool, and a walk that finds the same entries keeps the buffer
    id<MTLBuffer> cur = tab();
    g_ser_gen++; CHECK("a new epoch: the same buffer", tab() == cur); MOVED("a new epoch", 1, 0);
    g_sparse_synced = nvn_sparse_gen() + 1; ser_note(424243); CHECK("a sparse mapping that moved: the same buffer", tab() == cur); MOVED("a sparse mapping that moved", 1, 0);
    ser_note(424244); CHECK("one that did not move: the same buffer", tab() == cur); MOVED("a sparse mapping that did not move", 0, 0);
    for (int i = 0; i < POOL_LOG + 1; i++) plog(1000);
    CHECK("behind the log: the same buffer", tab() == cur); MOVED("behind the log", 1, 0);
    uint8_t *other = mmap(NULL, 1u << 20, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0); if (other == MAP_FAILED) abort();
    mtl_pool_add(other, 1u << 20); CHECK("a pool, every texture made: the same buffer", tab() == cur); MOVED("a pool", 1, 0);
    mtl_pool_del(other); CHECK("a pool gone: the same buffer", tab() == cur); MOVED("a pool gone", 1, 0);

    // A version the GPU may hold is not used again until its submission is done; then no new buffer is made
    finish(); mtl_sub_begin(); (void)tab(); mark();
    id<MTLBuffer> u1 = tab();
    mtl_reg_tex(8, &c, NULL); id<MTLBuffer> u2 = tab();
    mtl_reg_tex(8, &d, NULL); id<MTLBuffer> u3 = tab();
    CHECK("in one submission each version is its own", u1 != u2 && u2 != u3 && u1 != u3);
    MOVED("two versions", 0, 2);
    finish(); mtl_sub_begin();
    uint64_t nb = g_stat_pool_new;
    mtl_reg_tex(9, &c, NULL); id<MTLBuffer> u4 = tab();
    CHECK("after it is done: no new buffer", g_stat_pool_new == nb);
    CHECK("a version used again", u4 == u1 || u4 == u2 || u4 == u3 || u4 == v1 || u4 == v2 || u4 == v3 || u4 == v4 || u4 == v5 || u4 == v6);
    CHECK("it holds the latest entries", ents(u4)[8] == g_treg[8].view_rid && ents(u4)[9] == g_treg[9].view_rid && ents(u4)[5] == g_treg[5].view_rid && ents(u4)[7] == none);
    MOVED("a version used again", 0, 1);

    // A guest write to a texture only a table reads: nothing else looks at it, and the check runs once a submission
    finish(); mtl_sub_begin(); (void)tab(); finish();
    uint32_t *lin2 = malloc(64 * 64 * 4);
    CHECK("the texture holds what it was made from", holds(tex_made(&a), lin));
    fill(&a, 2, lin2);
    mtl_sub_begin(); g_tpkg_sub = g_sub_serial; (void)tab(); finish();   // as if this submission's check had run
    CHECK("no other path reaches it", holds(tex_made(&a), lin));
    mtl_sub_begin(); (void)tab(); finish();
    CHECK("the next submission's check does", holds(tex_made(&a), lin2));
    free(lin); free(lin2);

    // Samplers answer to sampler registrations only, and the same entries keep the buffer
    mtl_sub_begin(); mark();
    id<MTLBuffer> s1 = stab();
    CHECK("the sampler table holds the registered IDs", ents(s1)[1] != ents(s1)[3] && ents(s1)[2] != ents(s1)[3]);
    mtl_reg_tex(10, &c, NULL); (void)tab(); CHECK("a texture registered: the same sampler table", stab() == s1);
    samp_par sp2 = sp; sp2.ws = 1; sp2.lod_max = 3; mtl_reg_samp(3, &sp2);
    id<MTLBuffer> s2 = stab();
    CHECK("a sampler registered: its entry changes", ents(s2)[3] != ents(s1)[3] && ents(s2)[1] == ents(s1)[1]);
    finish();

    // Past the budget: what has no texture stays the dummy with nothing made, a pool walks nothing, and a texture made from a handle joins by its ID
    g_pool_sparse = 1;
    mtl_sub_begin(); (void)tab(); mark();
    mtl_reg_tex(11, &e, NULL);
    id<MTLBuffer> p1 = tab(); MOVED("an ID with no texture yet", 0, 0);
    CHECK("it holds the dummy, nothing made", ents(p1)[11] == none && !tex_get(e.serial));
    uint8_t *other2 = mmap(NULL, 1u << 20, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0); if (other2 == MAP_FAILED) abort();
    mtl_pool_add(other2, 1u << 20); CHECK("a pool: the same buffer", tab() == p1); MOVED("a pool, past the budget", 0, 0);
    mtl_pool_del(other2);
    uint32_t *words = (uint32_t *)(mem + (40u << 20)); words[0] = 0x3f800000; words[1] = 11u | 1u << 20;
    S.ssbo[0][1] = (p_bind_buf){0, 1, (uint64_t)(uintptr_t)words, 8};
    CHECK("the bound buffer names it", pool_scan_bound(0));
    id<MTLBuffer> p2 = tab(); MOVED("a texture made from a handle", 0, 1);
    CHECK("the ID holds it, no walk", ents(p2)[11] != none && ents(p2)[11] == g_treg[11].view_rid && tex_get(e.serial));
    mtl_tex_release(&e);
    id<MTLBuffer> p3 = tab(); MOVED("its texture released", 0, 1);
    CHECK("released it holds the dummy", ents(p3)[11] == none);
    S.ssbo[0][1] = (p_bind_buf){0};
    finish();

    for (int i = 5; i <= 11; i++) mtl_reg_tex(i, NULL, NULL);
    mtl_tex_release(&a); mtl_tex_release(&b); mtl_tex_release(&c); mtl_tex_release(&d);
    nvn_dirty_remove_range(mem); mtl_pool_del(mem); mtl_drain();
    printf("t_pool_table: %d checks, %d failed\n", checks, failed); return failed != 0;
}}
