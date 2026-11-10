// tex_dirty_upload's protection at a texture's first use (hle_nvn.c ft_add / ft_arm / nvn_ft_read, nvn_mtl.m mtl_tex_first_use_reads): a block-compressed texture, whose first use
// takes its content from its storage, is not write-protected at its registration, so a fill before that use costs no page fault; it is protected at the first use, and a CPU rewrite
// after it is uploaded by the poll. An uncompressed one is protected from its registration, as before. ReadTexels before any use reads an unprotected one's storage when it holds
// data. Needs a profile with tex_flush_upload and tex_dirty_upload (Ocarina of Time's); with another it says so and passes
#define nvn_game_fps nvn_game_fps_mtl_default
#include "../nvn/nvn_mtl.m"
#undef nvn_game_fps
#define g_ft g_ft_hle
#include "../nvn/hle_nvn.c"
#undef g_ft
uint64_t g_t0_ns;
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t *g_mod; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
void crash_install(void);
static int checks, failed;
#define CHECK(c, label) do { checks++; if (!(c)) { failed++; fprintf(stderr, "FAIL: %s (line %d)\n", label, __LINE__); } } while (0)

static uint64_t faults(void) { uint64_t f, p; nvn_dirty_stats(&f, &p); return f; }
static void first_use(tex_t *t) { mtl_sub_begin(); (void)tex_mtl(t); CHECK(mtl_wait(mtl_sub_commit(), 5000000000ull), "first use submitted"); }
static void make_tex(NVNtexture *o, pool_t *pool, ptrdiff_t off, int format) {
    NVNtextureBuilder tb; h_nvnTextureBuilderSetDefaults(&tb);
    h_nvnTextureBuilderSetWidth(&tb, 256); h_nvnTextureBuilderSetHeight(&tb, 256); h_nvnTextureBuilderSetFormat(&tb, format);
    h_nvnTextureBuilderSetStorage(&tb, (NVNmemoryPool *)pool, off);
    CHECK(h_nvnTextureInitialize(o, &tb), "texture initialized");
}

int main(void) { @autoreleasepool {
    if (!DM_GAME_TEX_FLUSH_UPLOAD || !DM_GAME_TEX_DIRTY_UPLOAD) { printf("t_ft_first_use: the profile has no tex_flush_upload / tex_dirty_upload: nothing to check (0 checks, 0 failed)\n"); return 0; }
    crash_install();   // the same SIGSEGV / SIGBUS handling as production: the page-fault hook of the write protection is dispatched from within it
    mtl_init();
    const size_t len = 4 << 20;
    uint8_t *mem = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0), *mem2 = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    if (mem == MAP_FAILED || mem2 == MAP_FAILED) abort();
    pool_t pool = {0}, pool2 = {0};
    pool_builder_t pb = {.mem = mem, .size = len, .flags = NVN_MEMORY_POOL_FLAGS_CPU_CACHED | NVN_MEMORY_POOL_FLAGS_GPU_CACHED}, pb2 = pb; pb2.mem = mem2;
    CHECK(h_nvnMemoryPoolInitialize((NVNmemoryPool *)&pool, (const NVNmemoryPoolBuilder *)&pb), "CPU-cached pool initialized");
    CHECK(h_nvnMemoryPoolInitialize((NVNmemoryPool *)&pool2, (const NVNmemoryPoolBuilder *)&pb2), "a second CPU-cached pool initialized");

    // A block-compressed texture: its first use takes its storage, so a fill before that use costs no page fault, and the first use takes the fill
    NVNtexture a = {0}; make_tex(&a, &pool, 65536, NVN_FORMAT_RGBA_DXT1); tex_t *ta = (tex_t *)&a;
    CHECK(nvn_ft_tracked(ta->serial) && mtl_tex_first_use_reads(ta), "the block-compressed texture is registered, and its first use reads its storage");
    uint64_t f0 = faults(); memset(ta->mem, 0x22, ta->L.size);
    CHECK(faults() == f0, "a fill before the first use costs no page fault");
    first_use(ta);
    CHECK(tex_cpu_written(ta->serial), "the first use takes the fill from the storage");

    // One whose storage is empty at its first use: protected from that use, so a CPU rewrite after it faults and the poll uploads it
    NVNtexture b = {0}; make_tex(&b, &pool, 1 << 20, NVN_FORMAT_RGBA_DXT1); tex_t *tbb = (tex_t *)&b;
    f0 = faults(); first_use(tbb);
    CHECK(!tex_cpu_written(tbb->serial) && nvn_ft_tracked(tbb->serial), "an empty storage at the first use: not taken from it, still registered");
    f0 = faults(); memset(tbb->mem, 0x40, tbb->L.size);
    CHECK(faults() > f0, "protected from its first use: a rewrite after it faults");
    nvn_ft_poll();
    CHECK(tex_cpu_written(tbb->serial), "the poll uploads the rewrite");

    // An uncompressed texture's first use may leave the storage to the GPU (gws_owned): protected from its registration, as before
    NVNtexture c = {0}; make_tex(&c, &pool2, 2 << 20, NVN_FORMAT_RGBA8); tex_t *tc = (tex_t *)&c;
    CHECK(!mtl_tex_first_use_reads(tc), "an uncompressed texture: its first use may not read the storage");
    f0 = faults(); memset(tc->mem, 0x33, tc->L.size);
    CHECK(faults() > f0, "so it is protected from its registration: a fill faults");
    first_use(tc);
    CHECK(tex_cpu_written(tc->serial), "and its first use sees the CPU wrote it");

    // ReadTexels before any use (any thread): an unprotected texture's storage with data in it is read; one with none is protected from then on
    NVNtexture d = {0}, e = {0}; make_tex(&d, &pool2, 65536, NVN_FORMAT_RGBA_DXT1); make_tex(&e, &pool2, 1 << 20, NVN_FORMAT_RGBA_DXT1);
    tex_t *td = (tex_t *)&d, *te = (tex_t *)&e;
    f0 = faults(); memset(td->mem, 0x55, td->L.size);
    CHECK(faults() == f0, "a block-compressed texture is filled before any use with no page fault");
    CHECK(nvn_ft_read(td, 1) && tex_cpu_written(td->serial), "ReadTexels before any use reads its storage, which holds data");
    CHECK(!nvn_ft_read(te, 1) && !tex_cpu_written(te->serial) && nvn_ft_tracked(te->serial), "an empty one is not read from its storage and stays registered");
    f0 = faults(); te->mem[100] = 1;
    CHECK(faults() > f0, "and is protected from then on");
    CHECK(nvn_ft_read(te, 1) && tex_cpu_written(te->serial), "so a later ReadTexels sees the write");

    h_nvnTextureFinalize(&a); h_nvnTextureFinalize(&b); h_nvnTextureFinalize(&c); h_nvnTextureFinalize(&d); h_nvnTextureFinalize(&e);
    h_nvnMemoryPoolFinalize((NVNmemoryPool *)&pool); h_nvnMemoryPoolFinalize((NVNmemoryPool *)&pool2);
    defer_drain(); munmap(mem, len); munmap(mem2, len);
    printf("t_ft_first_use: %d checks, %d failed\n", checks, failed);
    return failed != 0;
} }
