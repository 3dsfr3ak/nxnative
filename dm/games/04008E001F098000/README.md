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

## Other open items

- Shaders `a77a3a7b19f29a89` and `c7772ae369ee1c0e` are missing from the shader map, and one mesh draw is skipped.
- The VP9 decoder refuses packets with 12 pictures waiting for a released buffer.
- A right Joy-Con's SL and SR buttons, Home and Capture reach nothing, and it has no rumble. A left Joy-Con is not read.
- The sensor's specifications other than its ranges are answered as 0, values unknown.
- Joy-Con icon colours use an inferred layout.
- 10 shaders do not translate, most of them mesh shaders (`ISBERD mode b8`), the others Ampere instructions 481 and 243, and 3 mesh stages are left out. Whether a scene needs them is not known.
- No pipeline prewarm list yet.

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
- [Output streams](../../../docs/reference/audio.md#output-streams-sdk-22): the menu movie's sound.
- [SensorFusion and NpadImu](../../../docs/reference/services.md#sensorfusion-and-npadimu-switch-2-sdks) and [a lone right Joy-Con on the Mac](../../../docs/reference/services.md#a-lone-right-joy-con-on-the-mac).
- Host calls first needed here: `nn::fs::MountRamDisk`, the game's own save mounted read-only, Home button protection, `GetJcmsState`, Joy-Con icon colours, and virtual output devices.
- The game supports only handheld and dual Joy-Con styles (`0x6`). Offering full key looped its controller applet, so `style_for_player` follows the supported set.
