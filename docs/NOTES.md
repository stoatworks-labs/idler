# Notes

Working notes for this repo: status, decisions, and the traps that have actually bitten.
Migrated out of Claude Code's memory on 2026-08-24, so they are written in the first
person and dated by when each thing was learned — that date is usually the useful part.

Cross-cutting notes that are not specific to this repo live in
[fleet-notes](https://github.com/stoatworks-labs/fleet-notes).

## idler

*idler — the eleven Windows 95/98 screensavers as FFGL source + mask effect for Resolume; PUBLIC MIT v0.1.0 RELEASED 2026-08-04, all homes live, and the first fleet plugin verified in Resolume BEFORE its release*

**idler** (built and released 2026-08-04) — the Windows 95/98 screensaver suite
as **two** FFGL 2.1 plugins for Resolume: `Idler` (FF_SOURCE, `ID01`) and
`Idler Mask` (FF_EFFECT, `ID02`). C++17 + GLSL 4.1, CMake, `~/Projects/idler`,
**PUBLIC MIT** at `stoatworks-labs/idler`, **v0.1.0 released**.

Eleven savers, one plugin: Mystify, Beziers, Curves and Colors, Flying Windows,
Flying Through Space, Scrolling Marquee, 3D Maze, 3D Pipes, 3D Flying Objects,
3D FlowerBox, 3D Text.

**The one idea:** every saver is a pure function of (time, seed) — same rule as
[orrery](https://github.com/stoatworks-labs/orrery/blob/main/docs/NOTES.md) (`orrery`). The two that genuinely grow (Pipes, Maze) are made pure by
**deterministic replay** from the seed with a cache that may only skip forward;
`idtest --replay` demands **byte-identical** cold-vs-warm frames and gets them
for all eleven. Counter-based `Random` (draw *n* = `Hash2(n, seed)`) is what
makes a replay checkpoint one integer.

**A saver never touches OpenGL** — it fills a `Scene` (camera, shading mode, one
triangle mesh) and the renderer draws it. Eleven savers, one draw call.

**OFX port done 2026-08-04**, after the v0.1.0 release: `source/ofx/` is a
second *renderer*, not a second plugin. `Raster.cpp` rasterises `Scene` in
software (an OFX host hands over a buffer; Resolve calls the CPU path when it
likes and Natron has no GL path at all), and the savers, Controls, Mesh and
Presets link from the same files — the CMake saver list is declared **once** and
shared. `idtest --raster` renders every saver through both paths and compares
the *picture*, not the pixels: all eleven agree at 100% coverage, mean channel
error under 0.0006. `--raster-sheet` writes GL beside software, because a number
agreeing with a number is not evidence that either is a picture. Sync offers
Free and Manual only (OFX carries no tempo); no audio; `eRenderInstanceSafe`
because of the replay cache. **Never run in a real OFX host** — only `ofxprobe`.

**The port was in `main` for a day without shipping**: the release workflow had
no mention of it, so v0.1.0 went out FFGL-only and the Resolve plugins existed
nowhere a user could get them. **v0.1.1** (2026-08-06) adds the CI packaging —
its own zip per platform, separate from the FFGL artefact because it installs to
`/Library/OFX/Plugins` — plus the two checks that otherwise only fail after a
tag (both entry points exported, `CFBundleExecutable` against the binary). Worth
remembering as a shape: *adding a build target does not add it to the release.*

**v0.1.1 was cut ENTIRELY LOCALLY on 2026-08-06**, because GitHub Actions was in
a major outage with webhook triggers throttled — which is also why the tag push
fired no run at all, and why a manual dispatch queued and was then cancelled.
All six assets built on this Mac and in the Parallels guest, published with
`gh release create`, and the signing gate passes (3/3 macOS kinds notarised).
The fleet convention is that public repos build in CI; this is the exception and
the reason is worth knowing, because it proved the local path works for a
C++/FFGL repo end to end.

Two things had to be built that had never been built before: the **Windows OFX
binary** (MSVC x64 cross-compiled from the ARM64 guest via the `Hostarm64 -> x64`
toolset, PE machine 0x8664) and **vcpkg**, which was not installed in the guest
at all and which GLEW arrives through — without it the FFGL configure fails
outright. `~/Projects/.release-vm/*.ps1` are the scripts that did it.

**Porting found a bug in the shipped v0.1.0:** the *effect* variant defaulted
Background Alpha to 1 like the source, so an opaque black background covered
the clip it exists to draw over — Over looked like a replace, and Reveal, Hide
and Colourise were all no-ops (scene alpha 1 everywhere, nothing to cut
against). Effect now defaults to 0, and **presets skip the background on the
effect** for the same reason they skip Mask Mode. Fixed in both builds.

**First fleet plugin to allocate its own framebuffer** — 3D needs a depth buffer
and the host's is colour-only. `Target` is hand-written, not subclassed off
`FFGLFBO`, because both of that class's bugs live in the inherited parts
([ffgl sdk bugs](https://github.com/stoatworks-labs/fleet-notes/blob/main/notes/reference_ffgl_sdk_bugs.md)). **All lines are geometry**: a core profile only
guarantees `glLineWidth` 1.0, which is what macOS gives.

**Allan loaded both plugins into Resolume and confirmed all eleven render, BEFORE
the release** — the first time in this fleet that a plugin reached a real host
ahead of its tag, so the docs say so instead of the usual "never loaded into
Resolume". Still unverified there: **Bar sync against a real transport**, and how
the parameter groups land in the inspector. Never used on a live show. Windows is
built in CI and, since the release check was added, **loaded into a real Arena
7.27.1 at every release** — both plugins register correctly and all 39 reported
controls match the declared ones. That runs on software rendering and on a
machine with no sound device.

Verified 13/13 by `tools/verify.sh` (universal build, `plugMain`,
`CFBundleExecutable`, ad-hoc codesign, geometry, coverage, replay, 64
control/context sweep pairs). Signed + notarised automatically by the autosign
agent minutes after the tag; `spctl` on the shipped dmg reads
`Notarized Developer ID`.

**Every real bug was found by looking at a contact sheet, not by an assertion** —
pipes beaded at every cell (a joint ball on every path point rather than every
corner), maze walls flat enough that a frame facing one read as a rendering
failure, and a Mystify trail spanning a thirtieth of the motion so twelve steps
landed on top of each other. All three passed every test. `idtest --sheet`
asserts nothing and is the most valuable tool in the repo.

**Browser demo LIVE 2026-08-05** at `idler-demo.stoatworks-labs.com` — all
eleven savers, both variants. The two shaders are copied character for
character; the engine, savers, Controls, Presets, Font and Teapot are a hand
port, checked by `demo/tools/check_geometry.mjs` against `idtest --geometry`
(11/11 match on triangles, vertices and bounding box). See
[hand ported demo verification](https://github.com/stoatworks-labs/fleet-notes/blob/main/notes/reference_hand_ported_demo_verification.md) for the pattern and its JS traps.
The kit gained an optional depth attachment on `PassBuffer` for this.

Release homes all live 2026-08-04: repo, website `/software/idler/`, **YouTube
`r_ZPnHM8-NY`** (58.3s, rendered not filmed), Instagram reel published, both
embeds, download block, hero (`docs/hero.png`, registered in the website's
`shots.json`). Traps are in the repo's `AGENTS.md`;
**idler generic controls** (below) has the design cost worth knowing.
**disclaimer scope** (working-practice note, kept in Claude memory) applies.

## idler generic controls

*Eleven savers sharing seven generic scene controls: what it buys, what it costs, and the two release-pipeline tables a new saver silently breaks*

**idler** (below) puts eleven screensavers behind **one** parameter list: seven
deliberately generic scene controls (Density, Complexity, Size, Length, Line
Width, Variation, Shading), each mapped by each saver to whatever it has that is
analogous. Density is Mystify's polygon count, the starfield's star count and the
number of pipes growing at once.

**The alternative was ~90 parameters of which ~82 are dead at any moment**, plus
a saved composition that renumbers itself whenever a saver is added. The generic
seven are the right trade, and the costs are real and worth writing down.

## The same slider genuinely does different things

A preset built for one saver rarely reads well on another. That is why
**`Presets.h` is closer to load-bearing here than anywhere else in the fleet**:
the first eleven presets are each saver *as it actually ran*, and picking a saver
from the dropdown alone gives you that saver driven by whatever the sliders
happen to say. The distinction is in the README, because it is not guessable.

## Most controls are dead most of the time — by design

`Fog` does nothing in Mystify (no depth), `Line Width` nothing in FlowerBox (no
lines), `Text` nothing in nine of eleven. A naive sweep reports about half the
parameter list as broken, so `tools/sweep.py` carries a **context table** naming
the savers each control is live in, and sweeps it there on that saver's preset.

Two sweep findings that were the *harness*, not the plugin, and are worth
recognising again:

- **A control that only drives the CLOCK looks dead when the clock is pinned.**
  Speed and Audio Speed swept as dead because `SetTimeOverride` replaces the
  clock. `idtest --hosttime` drives the real one over two frames instead.
- **Audio was never exercised offline**, because `UpdateAudio` ran from
  `ProcessOpenGL` and the harness calls `Render` directly. Moved into `Render`.

## Two release-pipeline tables a new saver or project silently breaks

- **`make_social.py`'s `CUTS`** is hand-maintained. A project absent from it
  produces **no Instagram cut and no caption entry, and prints nothing** — the
  run looks successful. idler needed a row added.
- **the website's `scripts/shots.json`** — `make_screens.py` *prunes* anything in
  `public/screens/` it did not produce, so a hero committed straight into the
  website repo is a file waiting to be deleted. Point it at something the project
  repo can regenerate, and put the regenerating command in the repo
  (`idler/docs/README.md` does).

## The cue sheet must never set `Preset`

`idtest --sequence` applies every cue **on every frame**, not once. A `Preset`
cue therefore re-applies forever — and because editing any preset-covered
parameter flips the dropdown back to Custom, a `Preset` cue and a ramp of
anything it covers fight once per frame: the ramp sets the value and clears the
preset, the preset copies its whole row back. The picture flickers between two
parameter sets at 30Hz while the cue sheet reads as correct. Set each saver's
scene out in full instead.

Related: **release workflow** (working-practice note, kept in Claude memory), [orrery](https://github.com/stoatworks-labs/orrery/blob/main/docs/NOTES.md) (`orrery`).

## 3D Maze is an endless maze, and the first two fixes were not enough

*Learned 2026-09-01, after the third report of "it isn't going anywhere"*

The maze was one grid of 6..16 cells a side, generated once at reset and walked
for the rest of the clip. Two separate things made that read as **stuck**, and
they had to be fixed in that order because the first one hides the second.

1. **The turning rule** (fixed earlier). At a dead end the walk turned round and
   then preferred to carry straight on at each junction on the way back — which,
   travelling backwards, is the corridor it arrived down. Preferring the
   **least-walked exit** fixed it, and `idtest --walk` was written for it.
2. **The maze itself.** A hundred cells is about a minute of walking, and a
   *perfect* maze is a tree: exactly one route between any two cells, so every
   dead end costs a full retrace. Measured on the shipped preset, one tick in
   fifteen was a 180-degree turn and net displacement after a minute was under
   three cells. Through fog that shows three cells, that is indistinguishable
   from being stuck — and no turning rule can fix a maze with an end.

Now the maze is **chunked and infinite**: `chunk × chunk` cells generated as a
pure function of `(chunkX, chunkZ, seed)`, joined by two doorways per shared
edge drawn from a stream keyed on the **edge** so both sides agree, and
**braided** — all but one dead end in six gets one more wall opened, which is
what turns the tree into a graph with loops. Chunks more than 20 cells from the
camera are dropped and rebuilt from the seed if the walk returns.

Three things that were not obvious:

- **Chunk generation must draw from its own stream, never the walk's.** How many
  chunks get built depends on how far the drawing reaches, which depends on
  **Fog** — and Fog is deliberately not in the growth key. One shared stream and
  `--replay` fails the moment somebody moves the Fog slider.
- **Dropping chunks drops their pass counts, and that is fine.** What gets
  dropped depends only on where the walk has been, so a replay drops exactly
  what a live run dropped. Deterministic, which is all `--replay` asks.
- **A window floor cannot catch a finite maze.** `--walk` measured distinct cells
  in a 100-tick window, and a walk pacing a hundred cells passed it comfortably
  for months. It now also requires **400+ distinct cells across the whole
  1200-tick run** — above the 256 the largest old grid could ever hold, so no
  maze generated once can pass it. The endless one reaches 620–726.

Drawing follows from the same change: the whole maze can no longer be drawn, so
it is the cells within a disc that follows the fog, minus everything behind the
camera's own plane. The **disc is measured in whole cells** on purpose — the demo
is a hand port checked against this one's triangle count, and a float comparison
deciding whether a cell is in or out is a float32-vs-double divergence waiting to
happen. The preset went from 16,920 triangles to 28,512.

Related: **idler** (above), [hand ported demo verification](https://github.com/stoatworks-labs/fleet-notes/blob/main/notes/reference_hand_ported_demo_verification.md).
