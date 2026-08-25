# idler

The Windows 95/98 screensaver suite — Mystify, Beziers, Curves and Colors,
Flying Windows, Flying Through Space, Scrolling Marquee, 3D Maze, 3D Pipes,
3D Flying Objects, 3D FlowerBox, 3D Text — as **two** FFGL plugins for Resolume
Arena/Avenue: a source (`Idler`) and an effect that composites over the clip
(`Idler Mask`). C++/GLSL, CMake MODULE → universal `.bundle` (macOS) + Windows
`.dll`. Public MIT repo.

Read `AGENTS.md` before changing the replay, the coordinate conventions or the
framebuffer.

## Commands (CMake)
- Configure: `cmake -B build -DCMAKE_BUILD_TYPE=Release`
- Fast dev build: add `-DCMAKE_OSX_ARCHITECTURES=arm64`
- Build: `cmake --build build`
- Install both bundles to Resolume: `cmake --install build`
- Render a frame: `./build/idtest --out /tmp/frame.png --time 18`
- A specific saver on its own preset: `--saver 7 --preset 8`
- The effect over a test clip: `./build/idtest --effect --out /tmp/mask.png`
- Drive the real clock instead of pinning: `--hosttime 18`
- List parameters: `./build/idtest --list`
- Contact sheet of all eleven: `./build/idtest --sheet /tmp/sheet.png`
- Set anything by name: `--set "Density=0.8" --set "Shading=2"`

## Savers, by index
`0` Mystify · `1` Beziers · `2` Curves and Colors · `3` Flying Windows ·
`4` Flying Through Space · `5` Scrolling Marquee · `6` 3D Maze · `7` 3D Pipes ·
`8` 3D Flying Objects · `9` 3D FlowerBox · `10` 3D Text

Preset `N+1` is saver `N` set up the way it actually ran. The Saver dropdown
alone gives you that saver driven by whatever the sliders happen to say — see
`Presets.h` for why that distinction matters here more than elsewhere.

## OpenFX build
- `source/ofx/IdlerOFX.cpp` → `build/Idler.ofx.bundle` (target `IdlerOFX`,
  `-DBUILD_OFX=OFF` to skip): **both** plugins in one bundle —
  `com.stoatworks.idler` (generator) and `com.stoatworks.idlermask` (filter).
- The savers, Controls, Presets and Mesh are linked straight from source. Only
  the renderer differs: `source/ofx/Raster.cpp` rasterises `Scene` in software,
  because an OFX host hands over a buffer and most never offer a GL context.
- **`idtest --raster` is what keeps the two renderers honest.** Add a shading
  rule to the fragment shader and it goes here too.
- Sync offers Free and Manual only — OFX carries no tempo. There is no audio.
- Smoke test (ofxprobe drives the Filter context; the generator's render runs
  only in a real host):
  `../resolume-ofx-bridge/build/ofxprobe --dir build --render com.stoatworks.idlermask --size 640x360 --out /tmp/i.bmp`
- OFX SDK subset (BSD-3) vendored under `external/openfx`.
- Install for Resolve: copy the bundle into `/Library/OFX/Plugins`.

## Verify
- Everything, including the release-time bundle checks: `tools/verify.sh`
- While working: `tools/verify.sh --fast`
- The replay cache does not change the answer: `./build/idtest --replay`
- The mesh each saver builds: `./build/idtest --geometry`
- Every saver draws something: `./build/idtest --coverage`
- No dead controls: `python3 tools/sweep.py`
- **Before a release**, the shipped Windows artefact in a real Arena:
  `../plugin-bench/arena/gate.sh idler` — loads the actual `.dll` on the win-lab
  VM and checks registration, the control surface the host sees, FFGL's
  16-char name truncation, and that every control still moves the picture.
  Takes minutes, needs the VM, and is deliberately NOT in `tools/verify.sh`.
  Its expectation lives at `plugin-bench/arena/expect/idler.json`, so a
  deliberate change to the controls changes that file too.
- The software rasteriser agrees with GL: `./build/idtest --raster`
- ...and look at the difference: `--raster-sheet /tmp/raster.png`

## Notes
- **Every saver is a pure function of (time, seed).** The two that grow —
  Pipes and Maze — are made pure by deterministic replay from the seed, with a
  cache that must never change the answer. `--replay` demands byte-identical
  frames.
- **A saver never touches OpenGL.** It fills a `Scene`: a camera, a shading
  mode, and one triangle mesh. Eleven savers, one draw call — and the OFX build
  rasterises the same `Scene` in software rather than reimplementing anything.
- **The effect variant defaults Background Alpha to 0, the source to 1.** An
  opaque background covers the clip the effect exists to draw over, and makes
  every mask mode a no-op. Presets skip the background on the effect for the
  same reason they skip Mask Mode.
- **Eleven savers share seven generic scene controls**, so most controls are
  dead in most savers by design. `tools/sweep.py` carries the table of which
  saver each is live in; adding a saver means revisiting it.
- **All lines are geometry.** A core profile only guarantees a line width of
  1.0, which is what macOS gives, so `glLineWidth` is never used.
- This is the only fleet plugin that allocates its own framebuffer — 3D needs a
  depth buffer and the host's is colour-only. `Target` works around two SDK bugs;
  see `AGENTS.md`.
- All host parameters are 0..1 and mapped in `Controls.cpp`. `SetParamInfo`
  clamps a standard default into 0..1 before `SetParamRange` can widen it.
  **Option parameters are the exception** — they hold the element value.
- `PT_ABOUT` is a display-only TEXT parameter and **must** have
  `SetTextParameter` return success, or no host can instantiate the plugin.
- `idler_core` is an **OBJECT** library, and each plugin's registration is
  listed directly in its own target — see `AGENTS.md`.
- macOS build must be universal (arm64 + x86_64). Verify with `lipo`, never the
  build log.
- `flat`, `active`, `filter`, `input`, `output`, `sample`, `common` are GLSL
  reserved words. Shader errors surface only at runtime, in the diagnostics log.
- Public repo. "Commit" = commit **and** push.

## Diagnostics

`source/Diag.{h,cpp}` — log file only, no crash handler (this runs inside
Resolume), no bundle command. It covers the failures that all look identical
from outside ("it does nothing"): a shader that will not compile, a framebuffer
that will not complete, and a replay that ran away.

    ~/Library/Logs/idler/idler.YYYY-MM-DD.log
