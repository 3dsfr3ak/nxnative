// A large memory pool in chunks (nvn_mtl.m BIG_MIN): a lookup gets a zero-copy buffer that holds what it asked and starts at its stride, so only what lookups reach is wired. The buffers must
// say the same memory as the guest address, a read of unknown extent must have BIG_UNKNOWN past it, a chunk asked for more is lengthened, a texture's upload wires no chunk (up_big_src),
// raw global memory's pool table covers the whole pool (big_full), the pool's removal takes every entry, and a smaller pool stays one buffer.
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t *g_mod; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int checks, failed;
#define CHECK(label, c) do { checks++; if (!(c)) { failed++; fprintf(stderr, "FAIL: %s (line %d)\n", label, __LINE__); } } while (0)
static void finish(void) { CHECK("submission completes", mtl_wait(mtl_sub_commit(), 5000000000ull)); CHECK("GPU has no submission error", !mtl_last_commit_error()); }
// The lookup holds [a, a + n) in one buffer whose contents are the guest's memory there, and the GPU address agrees
static id<MTLBuffer> look(uint64_t a, uint64_t n) {
    void *mb = NULL; uint64_t mo = 0, gpu = 0;
    if (!mtl_gpu_addr(a, n, &mb, &mo, &gpu)) { CHECK("lookup found", 0); return nil; }
    id<MTLBuffer> b = (__bridge id<MTLBuffer>)mb;
    CHECK("buffer holds the range", mo + (n ? n : 1) <= b.length);
    CHECK("contents are the guest's memory", (uint64_t)(uintptr_t)b.contents + mo == a);
    CHECK("GPU address is the buffer's", gpu == b.gpuAddress + mo);
    void *cb = NULL; uint64_t co = 0; CHECK("the CPU lookup agrees", mtl_addr(a, n, &cb, &co) && (uint64_t)(uintptr_t)((__bridge id<MTLBuffer>)cb).contents + co == a);
    return b;
}
int main(void) { @autoreleasepool {
    mtl_init();
    const uint64_t GB = 1ull << 30, MB = 1ull << 20;
    uint8_t *mem = mmap(NULL, GB, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0); if (mem == MAP_FAILED) abort();
    uint64_t base = (uint64_t)(uintptr_t)mem;
    int pools0 = g_npool;
    mtl_pool_add(mem, GB);
    CHECK("no buffer until a lookup", g_npool == pools0 && g_nbig == 1);
    CHECK("overlapping buffers: barriers carry alias visibility", resource_vis() & MTL4VisibilityOptionResourceAlias);
    id<MTLBuffer> a = look(base + 100, 4096);
    CHECK("one chunk", g_npool == pools0 + 1);
    CHECK("a chunk is far smaller than the pool", a.length <= 3 * BIG_S);
    CHECK("the same stride: the same buffer", look(base + 200, 4096) == a);
    CHECK("no more chunks", g_npool == pools0 + 1);
    id<MTLBuffer> far = look(base + 512 * MB + 5, 64);
    CHECK("another stride: another buffer", far != a && g_npool == pools0 + 2);
    id<MTLBuffer> cross = look(base + 48 * MB - 8, 64);   // across a stride's end: one buffer holds it
    CHECK("across strides: its stride's chunk", cross && cross != a);
    // Length 1: the read's extent is not known
    void *mb = NULL; uint64_t mo = 0, gpu = 0;   // CHECK goes on after a failed lookup, which writes none of them
    uint64_t un = base + 100 * MB + 3;
    CHECK("unknown extent: found", mtl_gpu_addr(un, 1, &mb, &mo, &gpu));
    CHECK("unknown extent: BIG_UNKNOWN past the address", ((__bridge id<MTLBuffer>)mb).length - mo >= BIG_UNKNOWN);
    (void)look(base + 200 * MB + 3, 100);   // a short chunk first
    CHECK("then unknown in its stride: found", mtl_gpu_addr(base + 200 * MB + 9, 1, &mb, &mo, &gpu));
    CHECK("then unknown in its stride: lengthened", ((__bridge id<MTLBuffer>)mb).length - mo >= BIG_UNKNOWN);
    int n1 = g_npool;
    id<MTLBuffer> big = look(base + 300 * MB, 100 * MB);
    CHECK("a long range in one buffer", big.length >= 100 * MB && g_npool == n1 + 1);
    id<MTLBuffer> again = look(base + 300 * MB + 4096, 4096);
    CHECK("its stride keeps the long chunk", again == big);
    // A check that only asks whether storage exists (tex_storage_live, for each ID of a bindless texture table): no chunk, nothing wired
    int n4 = g_npool, r4 = g_pool_res_n; uint64_t held = base + 700 * MB;
    CHECK("in the pool: held", pool_holds(held, 64 * MB));
    CHECK("past its end: not held", !pool_holds(base + GB - 8, 64));
    CHECK("in no pool: not held", !pool_holds(4096, 16));
    tex_t stored; memset(&stored, 0, sizeof stored); stored.mem = (void *)(uintptr_t)held; stored.L.size = 64 * MB;
    CHECK("a texture there is live", tex_storage_live(&stored));
    CHECK("no chunk for asking", g_npool == n4 && !g_big[0].chunk[(held - g_big[0].a) >> 24]);
    CHECK("nothing wired for asking", g_pool_res_n == r4);
    // The pool's end
    CHECK("past the end: not found", !mtl_gpu_addr(base + GB - 8, 64, &mb, &mo, &gpu));
    CHECK("at the end: found", mtl_gpu_addr(base + GB - 8, 8, &mb, &mo, &gpu));
    CHECK("at the end, unknown: found", mtl_gpu_addr(base + GB - 8, 1, &mb, &mo, &gpu));
    CHECK("the end's buffer stops at the pool", (uint64_t)(uintptr_t)((__bridge id<MTLBuffer>)mb).contents + ((__bridge id<MTLBuffer>)mb).length == base + GB);
    // A texture's upload: right bytes, through a buffer of its own, no chunk made and no chunk's memory left resident
    tex_t t = {.magic = MAGIC('T', 'E', 'X', 0), .has_layout = 1, .serial = 99700};
    t.d = (nvn_texdesc){.target = 1, .format = NVN_FORMAT_RGBA8, .width = 256, .height = 256, .depth = 1, .levels = 1, .samples = 1}; nvn_tex_layout(&t.d, &t.L);
    pool_t pool = {.magic = MAGIC('P', 'O', 'O', 'L'), .mem = mem, .size = GB, .flags = NVN_MEMORY_POOL_FLAGS_CPU_UNCACHED | NVN_MEMORY_POOL_FLAGS_GPU_CACHED};
    t.pool = &pool; t.mem = mem + 700 * MB;
    uint32_t *lin = malloc(256 * 256 * 4); for (uint32_t i = 0; i < 256 * 256; i++) lin[i] = i * 2654435761u;
    CHECK("swizzle", !nvn_swizzle(&t.d, 0, 0, lin, 256 * 4, t.mem));
    int n2 = g_npool; mtl_sub_begin(); id<MTLTexture> image = tex_mtl(&t); CHECK("texture resolves", image != nil); finish();
    CHECK("the upload made no chunk", g_npool == n2);
    id<MTLBuffer> rb = [g_dev newBufferWithLength:256 * 256 * 4 options:MTLResourceStorageModeShared];
    id<MTLCommandBuffer> cb = q_cb(); id<MTLBlitCommandEncoder> ce = [cb blitCommandEncoder];
    [ce copyFromTexture:image sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(256, 256, 1) toBuffer:rb destinationOffset:0 destinationBytesPerRow:256 * 4 destinationBytesPerImage:256 * 256 * 4];
    [ce endEncoding]; [cb commit]; [cb waitUntilCompleted];
    CHECK("the texture holds what the pool holds", !memcmp(rb.contents, lin, 256 * 256 * 4));
    free(lin);
    mtl_tex_release(&t);
    // Raw global memory reaches what the game asked an address for. Nothing asked: the pool stays in parts, and the table adds no chunk
    int n5 = g_npool;
    mtl_sub_begin(); CHECK("pool table made", gmem_map_gpu() != 0); finish();
    CHECK("nothing asked: no whole-pool entry", !g_big[0].full);
    CHECK("nothing asked: no new chunk", g_npool == n5);
    // A buffer's address: its stride joins the table
    uint64_t asked = base + 800 * MB + 4096; uintptr_t stride = (asked - g_big[0].a) >> 24;
    CHECK("not reached yet", !g_big[0].chunk[stride]);
    uint64_t gen0 = atomic_load(&g_pool_gen); mtl_pool_asked(mem, asked, 4096); CHECK("a new part asked changes the pools", atomic_load(&g_pool_gen) != gen0);
    mtl_sub_begin(); gmem_map_gpu(); finish();
    CHECK("asked: its stride has a chunk, still in parts", g_big[0].chunk[stride] && !g_big[0].full);
    int in_table = 0; const uint64_t *te = (const uint64_t *)g_gmem_buf.contents;
    for (int k = 1; k < DMSH_GMEM_MAP_ENTRIES; k++) { const uint64_t *e = te + k * DMSH_GMEM_MAP_ENTRY / 8; if (e[0] <= asked && asked < e[1]) in_table = 1; }
    CHECK("asked: the table holds it", in_table);
    gen0 = atomic_load(&g_pool_gen); mtl_pool_asked(mem, asked, 4096); CHECK("asked again: nothing changes", atomic_load(&g_pool_gen) == gen0);
    mtl_pool_asked(mem, base + GB + 4096, 16); CHECK("outside the pool: nothing changes", atomic_load(&g_pool_gen) == gen0);
    // Half its strides asked: two-stride chunks would count for more than the pool, so one buffer
    mtl_pool_asked(mem, base, GB / 2);
    mtl_sub_begin(); gmem_map_gpu(); finish();
    CHECK("half asked: the whole pool has an entry", g_big[0].full > 0 && g_pools[g_big[0].full - 1].end == base + GB);
    CHECK("lookups take it", look(base + 900 * MB, 4096).length == GB);
    // The pool's own address: all of it, at once
    uint8_t *mem2 = mmap(NULL, GB, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0); if (mem2 == MAP_FAILED) abort();
    mtl_pool_add(mem2, GB); mtl_pool_asked(mem2, 0, 0);
    mtl_sub_begin(); gmem_map_gpu(); finish();
    CHECK("the pool's address asked: the whole pool has an entry", g_nbig == 2 && g_big[1].full > 0);
    mtl_pool_del(mem2); munmap(mem2, GB); CHECK("that pool is gone", g_nbig == 1);
    // The texture table: within the budget the walk makes them; past it only those whose handles a bound buffer holds
    tex_t named = t, unnamed = t, nosamp = t; named.serial = 99701; named.mem = mem + 720 * MB; unnamed.serial = 99702; unnamed.mem = mem + 724 * MB; nosamp.serial = 99703; nosamp.mem = mem + 728 * MB;
    mtl_reg_tex(7, &named, NULL); mtl_reg_tex(8, &unnamed, NULL); mtl_reg_tex(9, &nosamp, NULL);
    samp_par sp; memset(&sp, 0, sizeof sp); mtl_reg_samp(3, &sp);
    pool_walk_decide(0, g_ntreg, 1001); CHECK("within the budget: it walks", !g_pool_sparse && g_pool_walked == 3 * t.L.size);
    pool_walk_decide(0, g_ntreg, 1001); CHECK("the same registrations are not counted twice", g_pool_walked == 3 * t.L.size);
    g_pool_walked = g_dev.recommendedMaxWorkingSetSize / 8; pool_walk_decide(0, g_ntreg, 1002); CHECK("past the budget: it stops", g_pool_sparse);
    __unsafe_unretained id<MTLBuffer> pb = nil; mtl_sub_begin();
    (void)pool_gpu(0, MTLTextureType2D, NVN_DPF_FLOAT, 0, &pb);
    CHECK("past the budget: the walk makes none", !tex_get(named.serial) && !tex_get(unnamed.serial));
    const uint64_t *tab = (const uint64_t *)((const uint8_t *)pb.contents + DMSH_POOL_HEAD); uint64_t none = tab[7];
    uint32_t *words = (uint32_t *)(mem + 730 * MB); words[0] = 1234; words[1] = 7u | 3u << 20; words[2] = 8u; words[3] = 9u | 5u << 20;   // a number, a handle, an ID with no sampler, a sampler nothing registered
    S.ssbo[0][1] = (p_bind_buf){0, 1, (uint64_t)(uintptr_t)words, 16};
    CHECK("the bound buffer names one", pool_scan_bound(0));
    CHECK("its texture is made", tex_get(named.serial) != NULL);
    CHECK("a word with no sampler or a sampler not registered is no handle", !tex_get(unnamed.serial) && !tex_get(nosamp.serial));
    (void)pool_gpu(0, MTLTextureType2D, NVN_DPF_FLOAT, 0, &pb); tab = (const uint64_t *)((const uint8_t *)pb.contents + DMSH_POOL_HEAD);
    CHECK("the table holds it now", tab[7] != none && tab[8] == none);
    words[2] = 8u | 3u << 20; CHECK("once a frame", !pool_scan_bound(0));
    atomic_fetch_add(&g_cur_frame, 1); CHECK("the next frame reads it again", pool_scan_bound(0) && tex_get(unnamed.serial) != NULL);
    // Two buffers of one size 4 KB apart, bound together: each is read once a frame (a table indexed by bits that left out 12 to 15 put them in one slot, and each draw read both again)
    atomic_fetch_add(&g_cur_frame, 1);
    uint32_t *w2 = words + 1024; memset(w2, 0, 16);
    S.ssbo[0][2] = (p_bind_buf){0, 1, (uint64_t)(uintptr_t)w2, 16};
    uint64_t sc0 = g_pscan_n; (void)pool_scan_bound(0); uint64_t sc1 = g_pscan_n; (void)pool_scan_bound(0); (void)pool_scan_bound(0);
    CHECK("two buffers 4 KB apart: each read once a frame", sc1 - sc0 == 2 && g_pscan_n == sc1);
    atomic_fetch_add(&g_cur_frame, 1); (void)pool_scan_bound(0);
    CHECK("and both again the next frame", g_pscan_n == sc1 + 2);
    S.ssbo[0][2] = (p_bind_buf){0};
    finish();
    S.ssbo[0][1] = (p_bind_buf){0};
    mtl_reg_tex(7, NULL, NULL); mtl_reg_tex(8, NULL, NULL); mtl_reg_tex(9, NULL, NULL); mtl_tex_release(&named); mtl_tex_release(&unnamed); g_pool_sparse = 0; g_pool_walked = 0;
    // A smaller pool is one buffer, and the removal of the large one takes all its entries
    uint8_t *small = mmap(NULL, 64 * MB, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0); if (small == MAP_FAILED) abort();
    int n3 = g_npool; mtl_pool_add(small, 64 * MB); CHECK("one entry", g_npool == n3 + 1 && g_nbig == 1);
    CHECK("asking does not wire a small pool either", pool_holds((uint64_t)(uintptr_t)small + 10, 16) && !g_pools[g_npool - 1].res);
    CHECK("its buffer is the whole pool", look((uint64_t)(uintptr_t)small + 10, 16).length == 64 * MB);
    mtl_pool_del(mem); mtl_drain();
    CHECK("no large pool left", g_nbig == 0);
    CHECK("only the small pool's entry is left of those added", g_npool == pools0 + 1);
    CHECK("the small pool still answers, its entry moved", look((uint64_t)(uintptr_t)small + 20, 16).length == 64 * MB);
    CHECK("the large pool's addresses do not", !mtl_gpu_addr(base + 100, 4, &mb, &mo, &gpu));
    mtl_pool_del(small); mtl_drain();
    printf("t_pool_chunks: %d checks, %d failed\n", checks, failed); return failed != 0;
}}
