# Nintendo Switch Sports Resort (04008E001F098000): the rewriter's patches of this game's own code. dm/tools/patch_nso.py loads every profile's patches.py and applies those of a
# module's build; FRAME_STEP as described there ("Registered patches"). Patches of Nintendo SDK module builds are in dm/tools/sdk_patches.py
from nso_asm import ADRP, B

# The frame rate the game steps by. Its sead::GameFrameworkOunce runs a frame procedure once a frame (vtable +0xe0, main+0x33b640; x19 the framework) that calls the frame-rate
# controller (vtable +0x128, main+0x33b6e0 → main+0x33b720, its state at fw+0x268). Unless its fixed-rate flag is set (fw+0x2c8 bit 0: the rate at fw+0x2c0 stays as the game
# set it, as in golf), the controller turns the frame's measured CPU times ([fw+0x140] and [fw+0x148], in ticks) into whole vsyncs, picks an interval with hysteresis (fw+0x2b0) and
# the rate 60 / interval (fw+0x2c0). Either way it passes the rate to main+0x33bb30, which sets the present interval and fw+0xf0, the rate its frame limiter (main+0x391ba80) spaces
# frames by below 60. The game's systems step by the rate: about ten readers reach the framework through [[main+0x6784a60]+0xa8]+0x40 and scale by 60 / [fw+0x2c0]
# (main+0x22e204 adds it to a frame count, main+0x2c3efc takes 1 / rate as the seconds a frame), and the frame procedure stores 60 / rate at main+0x605a320, which input reads for
# its hold timers. Play runs at 60 and loading at 30; frames that wait on the GPU keep 60, so on a Mac whose frames take longer than a vsync the game ran slow (with DM_FPS=30
# pacing alone it looked slow to the player, 2026-11-05).
# Two stubs in the frame procedure. After the controller (main+0x33b68c, its load of the rate for the input timers) the rate the systems read becomes 60 / the host's step, at most 3
# (the host's largest hold, 20 fps; at most 2, two-player golf ran at 0.85 of real time, 526 of 1800 frames held 3 vsyncs, 2026-11-11; the game's own intervals seen are 1 and 2),
# and the vsyncs stepped are counted; the game's own rate is kept on the state page (+0x38) with the rate written (+0x3c). Before the controller
# (main+0x33b67c, its vtable load) the game's rate goes back, unless the game set a rate of its own since: in fixed mode the controller keeps the field as it is, so the rate written
# stayed and the next frame took it for the game's own (the game fixed a step of 2 at 30 fps for good, 2026-11-11). So the controller, the present interval and the limiter's rate
# (fw+0xf0) stay the game's: the host presents by its own hold (hle_nvn.c present_itv), and a limiter set from a payback step of 2 would wait 33 ms a frame at hold 1. A fixed rate
# below 60 stays the game's and is published as a step the game fixes (state +0x24), so the host holds those frames. Until the host has decided a step, and at fps_target 0, the
# game's own rate runs. x8, x16 and x17 are free at both sites (x8 is loaded right after each; the function uses neither x16 nor x17). Bodies assembled with clang, checked by
# disassembly
def frame_step(ctx):
    here, emit, fs_site, fs_put, FS_STATE_OFF = ctx.here, ctx.emit, ctx.site, ctx.put, ctx.state
    before, after = 0x33b67c, 0x33b68c
    fs_site(before, '680240f9')   # ldr x8,[x19]: the framework's vtable, then its controller (+0x128)
    fs_site(after, '60c242bd')    # ldr s0,[x19,#0x2c0]: the rate, then 60 / rate for the input timers
    start = here(); emit(ADRP(16, here(), FS_STATE_OFF))
    for w in (0xb9400211,   # ldr w17,[x16]           target fps (0 = off)
              0x34000111,   # cbz w17,orig (+0x24)
              0xb9403e11,   # ldr w17,[x16,#0x3c]     the rate written last frame (0 = none)
              0x340000d1,   # cbz w17,orig
              0xb942c268,   # ldr w8,[x19,#0x2c0]
              0x6b11011f,   # cmp w8,w17
              0x54000061,   # b.ne orig               the game set a rate of its own since: it stays
              0xb9403a08,   # ldr w8,[x16,#0x38]      the game's own rate
              0xb902c268,   # str w8,[x19,#0x2c0]     back for the controller and its setter
              0xf9400268):  # orig: ldr x8,[x19]
        emit(w)
    emit(B(here(), before + 4))
    fs_put(before, [B(before, start)], 'the game\'s own frame rate back before its controller runs, unless the game set one since (state page +0x38, +0x3c)')
    start = here(); emit(ADRP(16, here(), FS_STATE_OFF))
    for w in (0xb9400211,   # ldr w17,[x16]           target fps (0 = off)
              0x34000351,   # cbz w17,orig (+0x6c)
              0xb942c268,   # ldr w8,[x19,#0x2c0]     the game's own rate, after its controller
              0xb9003a08,   # str w8,[x16,#0x38]      kept for the restore
              0xb9003e1f,   # str wzr,[x16,#0x3c]     nothing written yet
              0x394b2271,   # ldrb w17,[x19,#0x2c8]   the controller's fixed-rate flag
              0x36000111,   # tbz w17,#0,host (+0x38)
              0x7100f11f,   # cmp w8,#60
              0x540000ca,   # b.ge host               fixed at 60: the host's step still applies
              0x34000248,   # cbz w8,orig
              0x52800791,   # mov w17,#60
              0x1ac80a28,   # udiv w8,w17,w8
              0xb9002608,   # str w8,[x16,#0x24]      a step the game fixes: the host holds it
              0x1400000e,   # b orig
              0xb900261f,   # host: str wzr,[x16,#0x24]
              0xb9402208,   # ldr w8,[x16,#0x20]      the host's step (0 = not decided)
              0x34000168,   # cbz w8,orig
              0x52800071,   # mov w17,#3
              0x71000d1f,   # cmp w8,#3
              0x1a919108,   # csel w8,w8,w17,ls       at most 3
              0x52800791,   # mov w17,#60
              0x1ac80a31,   # udiv w17,w17,w8
              0xb902c271,   # str w17,[x19,#0x2c0]    the rate the systems step by: 60 / step
              0xb9003e11,   # str w17,[x16,#0x3c]     and what was written
              0xf9401a11,   # ldr x17,[x16,#0x30]
              0x8b284231,   # add x17,x17,w8,uxtw
              0xf9001a11,   # str x17,[x16,#0x30]     vsyncs stepped
              0xbd42c260):  # orig: ldr s0,[x19,#0x2c0]
        emit(w)
    emit(B(here(), after + 4))
    fs_put(after, [B(after, start)], 'frame rate the game steps by: 60 / the host\'s step, at most 3, unless the game fixes its own rate below 60 (state page target fps; 0 = the game\'s rate)')

FRAME_STEP = {'3941550a42845731651bcd05edbb4703': frame_step}

# How general each patch is and could be (dm/tools/docs_patches.py "Binding", listed in docs/agents/game-patches.md): what, its binding now, the loosest it could have (goal),
# purpose, why; blocked or kept: why the gap stays open (cannot close yet, or left open on purpose); checked: what was checked on game files (else a judgement)
BINDING = [
    {'what': 'frame rate the game steps by, from the host\'s step', 'now': 'build', 'goal': 'game', 'purpose': 'frame rate',
     'why': "the game's own frame-rate controller in its sead framework",
     'blocked': 'frame-step stubs have no function-matched form on this branch',
     'checked': 'v65536 main: the sites, their free registers, the controller and the rate\'s readers read by disassembly (2026-11-11)'},
]
