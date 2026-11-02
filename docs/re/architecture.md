# Architecture of Oni.exe

Windows v1.0: `Oni.exe`, 1,527,808 bytes, CRC-32 `a8223704`, MSVC, image base 0x400000. Names are
Bungie's, from the Intel Mac build ([inventory](inventory.md)); addresses are the Windows exe's. Marked
*inferred* where the code wasn't read through.

## Platform

| Area | Windows API | Notes |
|---|---|---|
| Graphics | OpenGL, loaded at run time (`opengl32.dll` by `LoadLibraryA` + `GetProcAddress`, no import) | GL back end `gl_draw_engine_initialize` 0x404e00, `initialize_opengl` 0x40bcc0. 3dfx paths: FXT1 compressed textures, 3dfx gamma, a `-glide` switch, the 3dfx OpenGL driver's registry key. GDI only for the pixel format, gamma ramp and `SwapBuffers` |
| Sound | DirectSound (`DSOUND` ordinal 1 = `DirectSoundCreate`) + `MSACM32` (ACM stream conversion) | Compressed sounds are decoded through Windows' ACM codecs (*inferred*: the format, probably MS/IMA ADPCM, is for step 3). A mixer thread (`CreateThread` at 0x4864b0, in the sound module's range) |
| Input | DirectInput (`DirectInputCreateA`), `GetAsyncKeyState`, a keyboard hook | An input event-queue thread (`LIiEventQueue_Initialize` 0x425e50) |
| Movies | Bink (`binkw32.dll`: open, decode, blit to a buffer, DirectSound audio) | `intro.bik`, `outro.bik` |
| Time | `QueryPerformanceCounter` (`UUrMachineTime_High`), `GetTickCount` | |
| Files | `CreateFileA`, `ReadFile`, `CreateFileMappingA`/`MapViewOfFile` | Memory-mapped files (*inferred*: the instance files) |
| Settings | Registry read only (`RegQueryValueExA`) | The installer's `Software\Bungie\Oni` key; config otherwise from text files |

## Start-up and loops

`ONiMain` 0x4d3280: command line (`OniParseCommandLine`), utilities (`UUrInitialize`), every module
(`ONiInitializeAll`), level 0 (`ONrLevel_LoadZero`, the shared data and the menus), Motoko drawing
(`ONrMotoko_SetupDrawing`), the window, splash screen, console (`COrConfigure`). Then two loops that block
until they are done:

1. `OWrOniWindow_Startup`: the **out-of-game UI** (main menu and its dialogs, on the WM toolkit).
2. `ONiRunGame` 0x4d4640: the **game loop**. Per frame: input, the console (`0x4306f0`), game time
   (`ONrGameState_UpdateServerTime`, then `iComputeDeltaTicks`: the game state advances by the ticks that
   have elapsed), sound (`0x483bd0`), then drawing through Motoko (`ONiGameState_Display_Reflectable`, the
   pause screen and its overlays), and an fps counter.

The WM dialogs probably run nested modal loops of their own (*inferred*).

## Data

The game folder holds `GameDataFolder/level<N>_Final.dat` + `.raw` (no `.sep` on Windows: the Mac
version's separate-data files), the BSL scripts in `GameDataFolder/IGMD/<level>/*.bsl`, and the two Bink
movies. The template and instance manager is `TM` (`TMrInitialize`, `TMrTemplate_Register`,
`TMrLevel_Load` 0x4217d0, `TMiGame_InstanceFile_*`). Step 2 ports it.

## Consequences for the port

- **Blocking loops.** The front end and the game each own a loop, and dialogs likely nest more. Two ways
  onto gasm's one-frame-per-call model. One: unwind them into states, as OpenRF did. Two: keep the
  original's structure on gasm's own-main-loop support (`gasm_main` + `gasm_wait_frame` in the C SDK:
  stack switching where the runner has it, Asyncify otherwise; ScummVM and SDL's `main()` run this way).
  Keeping the structure is closer to the original and cheaper to port.
- **Threads** (mixer, input queue): on gasm the mixer is pulled on the frame (audio pushed per frame) and
  input is latched per frame, so neither thread is needed. Their shared state stays behind the original's
  locks (critical sections), which become no-ops in a single-threaded build.
- **OpenGL**: the GL back end is a narrow layer under Motoko (`gl_*`), the natural place for the GL 1.x
  emulation on `gasm:gl`. The 3dfx paths (FXT1, Glide gamma) are dead code on gasm.
- **Time**: `UUrMachineTime_High` becomes the frame clock, so headless runs are deterministic.
