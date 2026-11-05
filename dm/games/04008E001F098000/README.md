# Nintendo Switch Sports Resort

Title ID `04008E001F098000`, build v65536 (1.1.0), Switch 2 (NVN2).

**Status: plays on the Mac, from a dump only.** Tennis and golf run with a right Joy-Con, motion and sound, reported as working well in short runs with a save carried over from Eglantine. Only `scripts/pack_dump.py` applies the size table margin below, so the NSP script's build would crash. A first start without that save has not been retried since the memory fix, and neither the other sports nor many scene changes in a row have been tested. Mii models are not implemented. All testing so far is on one 16 GB M2 MacBook Air.

## Warning: this game has panicked a Mac

An unguarded run took a 16 GB Mac down with a kernel panic (`IOGPUGroupMemory.cpp:286`, "Memory object not found in fMemoryCountedSet or fPendingMemorySet", on `dmhost`'s Presentation Thread, 13.6 GB wired, 45 MB free, `dmhost` resident 11.4 GB). Low memory is the likely trigger, not a proven one. Save your work first, and on a machine this size run the game only under a memory watchdog.

## Current workarounds

### `rsizetable_margin` (cause open)

The game sizes each model's heap from `System/Resource/ResourceSizeTable.*.rsizetable.zs`. The `Npc_Integration` heaps come up short, the material block's allocation fails without an error (`main+0x435610`), and the game then creates buffers with a null memory pool, which crashes in `nvnMemoryPoolGetFlags`.

Measured by adding to every table entry, one run each: +0x40 crashes, +0x50 and +0x100 pass. So the heaps are 65 to 80 bytes short. The profile adds 0x100 when `scripts/pack_dump.py` packs the image; the other packers do not apply it.

**Open: why these heaps fit on a console.** Looked at and not the cause:

- Device values: the game reads 18, and the only one the host guesses (`0x18`) sizes one descriptor pool at startup.
- Packing: the table's overhead for the non-Mii `Npc_Integration` models is the same `0x52e18` in the base game and the update.
- The allocator's bookkeeping flag: its constructor (`main+0x62ca40`) writes it as a constant.

Not excluded: a game block whose size depends on a host answer, or console behaviour that can't be seen from here.

## Memory

The main constraint on a 16 GB Mac, and still tight: the game's own table budgets about 9 GB (`TextureHeap` 4.2 GB, `GameGraphics` 4.2 GB, `VirtualAddressMemory` 1.5 GB). Its footprint reaches about 11.3 GB in golf, because the originals of uploaded textures stay in guest memory beside the Metal copies (not wired, and read again when a texture object is made, so releasing them was not tried).

What it was: with 11.5 GB of GPU memory in a tennis match, the GPU ran out of memory on changing sports, and on the first start's setup screens. Two host rules wired or made what no draw needed, now fixed for every game:

- [A storage check that wired the pool it asked about](../../../docs/reference/nvn-objects.md#a-storage-check-that-wired-the-pool-it-asked-about) and [what raw global memory reaches of a large pool](../../../docs/reference/nvn-objects.md#what-raw-global-memory-reaches-of-a-large-pool): the 4,272 MB texture pool now has no chunks at all; the game asks no address in it.
- [A texture table past its memory budget](../../../docs/reference/shaders.md#a-texture-table-past-its-memory-budget): about 17,600 registered textures, 12.1 GB, against 8 made from the handles in bound buffers (382 MB).

Measured with those, 3.5 minutes in through a tennis match: GPU allocated 5.6 GB, peak wired 7.9 GB, no GPU errors. The GPU out-of-memory errors of changing sports are gone, and golf loads.

## Performance

Open. Golf on a 16 GB M2 MacBook Air, Docked: about 27 fps at its usual 28 to 35 ms of GPU span, up to 35 fps in cheaper stretches (one run, `DM_NVN_GPUPROF=every:300`). The game sets present interval 1 for play, so its target is 60 fps; it sets 2 only briefly while loading. In the display link's log line, the number after "held for the present interval" is a count of ticks held, not the interval. It points to the GPU, not the CPU: the whole process uses 0.7 cores (Time Profiler). `sample` pauses the process and showed the Presentation Thread at 93%, where Time Profiler has it at 27%.

- **The main shading pass is the largest cost:** 2048x1152, two colours and depth, 4 to 7 ms at about 80 to 120 draws and 12 to 14 ms at about 220, up to 40% of the GPU's busy time. Overdraw is not the cause: a depth pre-pass draws the same geometry first in 0.7 ms with 48-line shaders, and this pass tests depth without writing it, with no `discard`. It is the shading itself: about 30 `AnnaSurface` shaders of about 3,000 MSL lines and 23 texture samples, 62% of the draws through the texture table. The most used one has 184 reads through the argument buffer and 96 `precise::` calls. Which of these costs most is not measured.
- **Attachment traffic:** about 390 MB loaded and 450 MB stored a frame, and about 110 of the frame's roughly 165 passes load and store every attachment, by the host's count. `DM_NVN_TBDR=1` does not help: 396 and 413 MB, 25 to 27 fps, with the picture still right in the one frame checked (4 profiled frames, another moment of play).
- **Not thermal, so far:** the tiny passes, a GPU clock proxy, stay at 15 to 17 us through a 5-minute run on this fanless Air.
- **Idle between encoders:** 5 to 9 ms of each frame's span, of which about 3.5 ms is about 130 small gaps of 20 to 30 us. The game draws about 60 one-draw passes for mip chains and cube faces a frame.
- **Handheld is faster:** one 60 s run in Console mode Handheld, against one Docked run with similar views: about 41 fps against 35 by elapsed time, the 3D at 1600x900 against 2048x1152. In frames of about 141 encoders, render time falls from about 17 ms to 11, attachments from 336 MB loaded to 222, and GPU span from about 26 ms to 23. Encode thread was on only in the Handheld run. The picture is 1920x1080 inside the 2560x1440 window texture, and a single dispatch of about 4.7 ms appears in each frame, which the Docked run does not have: the game's own upscaler, 1600x900 to 1920x1080.
- **Game's upscaler on MetalFX, in Handheld, is faster again:** about 48 fps against 41 by elapsed time, one 60 s run each with similar views and the same settings. The upscaler falls from about 4.7 ms to about 0.9 ms, and GPU span from about 22 ms to 18 in frames of about 141 encoders. MetalFX's encoder has no timestamps of its own, so its time shows as a constant gap of about 0.89 ms between the 1600x900 and 1920x1080 passes. The profile now defaults to MetalFX (`upscaler`), and the player can still choose the game's own, whose look differs. No GPU errors, and the picture looked right in the two screenshots checked, though one putting view shows the character's head faded, which may be the game's own fade with the camera close. Not checked at full size for sharpness.
- **What the idle time is:** of about 6.5 ms a frame in Handheld with MetalFX (10 profiled frames), 0.9 ms is MetalFX itself. Nearly all the rest is inside one submission, not between submissions (about 0.2 ms): about 146 gaps of mostly 20 to 60 us, one per encoder change, plus three or so of 350 to 400 us in one cube map's mip chain, at its 16x16, 8x8 and 4x4 levels, cause unknown. `DM_NVN_BARRIER=ab` in two runs showed no measurable gain from `game` (median difference between neighbouring phases -0.26 and -0.47 ms of GPU, up to about 6 ms either way), though both runs sat at 30 fps.
- **A run stuck at 30 fps, open:** two runs (`game` and the default alternating) stayed at about 30 fps for their whole 2 minutes of measurement at 21 to 29 ms of GPU, with one present for each display link tick and about 30 ticks a second, even though the link asks for 60 Hz (`CAFrameRateRangeMake(60, 60, 60)`). A run with the same settings and no restart had about 60 ticks a second and reached about 48 fps. Both stuck runs follow a false rescue: the display link stops as the window is covered, 500 ms pass before the host hears of it, so [the present thread takes over](../../../docs/reference/frame-rate.md#present-thread-rescues-a-silent-display-link) and the link object is made again while the window is covered. Not proven: another run with three such restarts later reached 41 fps. Why the new link would fire at 30 Hz is not known.
- **Per-segment timestamps (`gpu_timestamps segment` in the profile) turn on the game's own dynamic resolution (measured in one run through `DM_NVN_GPU_TS=segment`):** with the default one time per submission, the game reads almost no GPU time and stays at its largest size. With per-segment timestamps, in Handheld with MetalFX, it sits at 640x360 in about 86% of the sampled frames (the profiled passes of 2 colours and depth), with some at 960x540 and 1120x630, upscaled to 1920x1080, at about 44 to 60 fps, mostly 52 to 60, and GPU span of 12 to 30 ms. The player reports the game looks faster, not measured. The `DM_FPS=30` pin, by contrast, looked slow: it paces frames without changing the game's step. Five new pipelines stalled the frame for 3.8 to 17.7 s each, three at the very start and two at a menu's loading, 64 s in all, with no compiler crash; the prebuilt archive removed such stalls in the one run on it (the pipeline prebuild item below). Why the size stays at its smallest even in frames of 12 to 16 ms is open: the span the game reads includes the idle gaps above.
- **CPU, not the frame rate's limit:** [the texture table](../../../docs/reference/shaders.md#a-texture-table-walked-whole-for-every-change) was 44% of the Presentation Thread and is now 1.1%, and the thread went from 27% to 14% of a core (Time Profiler, one run each). Over 78 s of golf it walked 29 times and made 1,849 versions, only 19 with a new buffer, for 100 ms in all. The frame rate barely moved, as expected when the GPU is the limit: about 25 to 27 fps in the expensive stretches of both runs, and 31 to 36 before against 33 to 40 after in the cheap ones, with different play in each, so 1 or 2 fps at most can be credited to this. The GPU's idle time between encoders did not change. `pool_scan_bound` is what remains, 12.6% of the thread, about 2% of a core.

## Other open items

- Shaders `a77a3a7b19f29a89` and `c7772ae369ee1c0e` are missing from the shader map, and one mesh draw is skipped.
- The VP9 decoder refuses packets with 12 pictures waiting for a released buffer.
- A right Joy-Con's SL and SR buttons, Home and Capture reach nothing, and it has no rumble. A left Joy-Con is not read.
- The sensor's specifications other than its ranges are answered as 0, values unknown.
- Joy-Con icon colours use an inferred layout.
- 10 shaders do not translate, most of them mesh shaders (`ISBERD mode b8`), the others Ampere instructions 481 and 243, and 3 mesh stages are left out. Whether a scene needs them is not known.
- Some draws use `NVN_DRAW_PRIMITIVE_QUAD_STRIP`, which the host does not implement, and are skipped (vertex `6dc3a8f48b7c78af`, fragment `5f06ec80e95d71e3`; the log stops at 8 lines, in 3 of the runs). What they draw is not known.
- No pipeline prewarm list yet.
- **Pipeline prebuild, on by default, is slow and crashes the compiler here.** On a 16 GB M2 MacBook Air, with 8 workers, it did 1,000 of 10,609 pairs in 205 s. `MTLCompilerService` reached 5 to 8 GB within about 30 s and aborted every 25 to 30 s, and 16 of those 1,000 pairs failed. A full run with 4 workers (`DM_PSO_PREBUILD_THREADS=4`, `DM_PSO_PREBUILD_DEPTH=zero`) finished in 4,835 s: 10,443 bases built, 90 already archived, 1 left to the draws, 75 failed, and the archive grew to 801 MB. The compiler still peaked at 8.51 GB and aborted 20 times, while system memory pressure stayed normal in all 2,337 readings. Of the 20 reports, 17 abort in `operator new`, 2 in a growing `SmallVector`, and 1 on an uncaught C++ exception with no allocation frame shown. Pairs were far from equally costly: the first 5,000 took 4,752 s, the other 5,609 about 83 s. None of the 75 failed pairs is among the 878 distinct pairs the game has drawn so far (menus, tennis, golf). Open: 75 failures over 20 aborts is about the 4 compiles in flight each time, so most may be bystanders, but `prebuild_failed.tsv` keeps all of them from being built again on this system build and stamp, and the error (`XPC_ERROR_CONNECTION_INTERRUPTED`) does not tell them apart. The first launch on this archive, 182 s of frames, removed the long hangs: none of the 501 slow frames logged compiled a pipeline (one pipeline had taken 3.8 to 17.7 s before), the worst frame was 754 ms, and the two costliest frames with uploads spent 462 and 541 ms on 400 to 700 MB of textures. Memory did not grow: 3.73 GB at most and 8.07 GB wired, against 3.67 to 4.09 GB and 7.89 to 8.54 GB in the three runs before. The start did change: the step before the first frame took 34.85 s against 0.43 to 1.35 s on the 106 MB archive, so the first frame came 36.7 s after the host started, against 2.3 to 4 s (the time in the settings window, here about 58 s, is not part of these). It did not repeat: the next launch, on the bundled file below, took 1.21 s for that step, with its first frame about 3 s after the host started. Which of three differences caused it is not known: that launch came 5.5 hours after the last one (a launch at 02:36 on the 102 MB archive took 13.4 s), looked through 25 archive files, 12 of which it compacted into one, and compiled one base where the next compiled none. These bases can go into the app without compiling them again: `metal-pack -nthreads 1` merged the 13 `p*.mtl4a` files with the 46 bases that sat in the device's other file into one `<arch>.mtl4a` of 680 MB in about 11 minutes, `pack_app.py --pso` takes it with the device's `prebuilt.tsv`, and a walk of every pair found all 10,533 bases in it, with none built. It opened in 124 ms in that check and 1,118 ms in the app's next launch, against 1,206 ms for the 25 files. A run of about 23 s on it, the start and first menus, compiled nothing and the player reported it runs well; a match has not been played on it. Bases with a vertex layout stay in the device's archive, because the host looks those up only there.

## Driving it on the Mac

Sound needs `DM_AUDIO=1`: the Mac's default is a silent sink.

Screenshots through `DM_CTL` save the whole 2560x1440 window texture and ignore the crop, so the picture looks 80% size with black at the right and bottom. Set `DM_NVN_SHOT_OUT=1280x720` to capture through the present pass, which applies the game's window crop.

## Saves

The game writes one device save, `device_save_data.sav`, first during the setup screens (about 332 s into a first start). It is a table of hashed value names (`GameData/GameDataList.Product.110.byml.zs` lists them without their names) followed by their data. Value `0x34127d` holds the game version that wrote it: 100 for 1.0.0, 110 for 1.1.0.

Value `0xd142ed7d` is a table of registered account IDs. A save from elsewhere holds that system's ID, so converting one means swapping in the host's (`k_uid` in `dm/host/hle_account.c`). A save written by 1.0.0 in Eglantine, converted this way, loaded on 1.1.0: the setup screens were skipped, and the game rewrote it in the 1.1.0 format (value `0x34127d` 110, key `0x375b373e` added). Open: what the game does with an unknown ID, which was not tried.

## Found on this game

- A dump tool's `.listing` files in the RomFS made the game take an empty equipment name; `romfs_image.py` leaves them out.
- The memory rules above.
- [A special register the translator did not know](../../../docs/reference/shaders.md#a-special-register-the-translator-did-not-know): the water's red.
- [A texture table walked whole for every change](../../../docs/reference/shaders.md#a-texture-table-walked-whole-for-every-change): about 3 walks a frame in golf.
- [Output streams](../../../docs/reference/audio.md#output-streams-sdk-22): the menu movie's sound.
- [SensorFusion and NpadImu](../../../docs/reference/services.md#sensorfusion-and-npadimu-switch-2-sdks) and [a lone right Joy-Con on the Mac](../../../docs/reference/services.md#a-lone-right-joy-con-on-the-mac).
- [A Joy-Con that does nothing in handheld mode](../../../docs/reference/services.md#a-joy-con-that-does-nothing-in-handheld-mode): the game opened its controller applet 295 times in under a minute. With the fix it opened none, and the right Joy-Con worked in Console mode Handheld, started from a Mac app's settings window (one short run, reported by the player).
- Host calls first needed here: `nn::fs::MountRamDisk`, the game's own save mounted read-only, Home button protection, `GetJcmsState`, Joy-Con icon colours, and virtual output devices.
- The game supports only handheld and dual Joy-Con styles (`0x6`). Offering full key looped its controller applet, so `style_for_player` follows the supported set.
