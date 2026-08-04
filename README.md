# idler

> **AI-assisted project.** This codebase was created with [Claude](https://claude.com/claude-code)
> (Anthropic), directed and reviewed by a human author. Every saver is verified
> by an offline harness that drives the real plugin class in a headless GL
> context: it renders frames and checks the mesh each saver builds, that every
> saver draws at five different times including zero, that no control is dead,
> and — for the two savers that grow — that a frame reached by replaying the
> clock is **byte-identical** to the same frame rendered cold (see
> [Status](#status)). Both plugins have been loaded into Resolume and run by the
> author; what has *not* been checked there is listed under
> [Status](#status). Check it in your own rig before trusting it in a show.

The Windows 95/98 screensaver suite, rebuilt as a generator for Resolume
Arena/Avenue.

Eleven savers in one plugin — the flat ones that shipped with Windows and the
OpenGL ones that came with Plus! and became stock in 98:

| | |
|---|---|
| **Mystify** | Bouncing polygons trailing their own history |
| **Beziers** | The same motion, drawn as curves |
| **Curves and Colors** | A spirograph drawing itself on a rotating palette |
| **Flying Windows** | The four-pane logo streaming out of the screen |
| **Flying Through Space** | The starfield |
| **Scrolling Marquee** | A text banner crossing the frame |
| **3D Maze** | A first-person walk down brick corridors |
| **3D Pipes** | Plumbing growing into a box, teapots and all |
| **3D Flying Objects** | Lit solids tumbling past |
| **3D FlowerBox** | A polyhedron morphing on the spot |
| **3D Text** | Extruded lettering, turning |

Ships as two plugins: **Idler**, a source, and **Idler Mask**, an effect that
draws the saver over the incoming clip — or reveals, hides or tints the clip
through it.

<!-- downloads:start -->
<!-- downloads:end -->

## Why it is not just nostalgia

Every saver is a **pure function of (time, seed)**. Nothing integrates a
velocity, nothing remembers the previous frame.

That is not a purist's flourish — it is what makes the plugin usable in a show:

- **It cannot drift.** Resolume's frame rate drops when the show gets heavy. A
  saver that slowed down under load would come apart from the music.
- **Beat sync is free.** Time is just a number, so handing it the host's bar
  position locks the animation with no second code path.
- **Phase is scrubbable and keyframable.** Drag it and the picture goes where
  you dragged it, including for 3D Pipes — the network grows to wherever you
  put the slider.

The two savers that genuinely grow — Pipes and Maze — get there by
**deterministic replay** from the seed, so the same composition builds the same
pipe network on the show laptop and the rack machine.

## Controls

Eleven savers share one parameter list: seven generic scene controls (Density,
Complexity, Size, Length, Line Width, Variation, Shading) that each saver maps
to whatever it has that is analogous, plus motion, camera, colour and the
Mask/Mix output pair.

**Start from a preset.** The first eleven presets are each saver set up the way
it actually ran — picking a saver from the dropdown alone gives you that saver
driven by whatever the sliders happen to say, which is often not what it looked
like on the machine. The remaining presets are looks that were never on
anybody's monitor: a wireframe maze over transparency, a hyperspace starfield,
pipes as a tinted overlay.

Colour Mode is worth knowing: **Classic** is each saver's own palette as it was;
**Tint**, **Spread** and **Cycle** put the whole suite under one colour scheme,
which is what makes any of it usable behind a band.

## Building

```bash
git clone --recursive https://github.com/stoatworks-labs/idler
cd idler
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build        # into ~/Documents/Resolume Arena/Extra Effects
```

macOS builds universal (arm64 + x86_64) by default. Windows needs vcpkg for
GLEW; the manifest is in `vcpkg.json`.

## Status

Verified offline by `tools/verify.sh`, which runs:

- **universal build**, `plugMain` exported, `CFBundleExecutable` correct, and an
  ad-hoc codesign — the same checks the release workflow does, run locally where
  they are cheap
- **geometry** — the mesh each saver builds: triangle counts, bounding boxes,
  indices in range, every normal unit length
- **coverage** — all eleven savers emit geometry that lands on the frame, at
  five times including zero
- **replay** — a frame rendered cold and the same frame reached by running the
  clock up to it are byte-identical, for all eleven
- **sweep** — 64 control/context pairs, none dead

Both plugins have been **loaded into Resolume and run**, and all eleven savers
render there.

What that does **not** cover, and you should assume is untested:

- **Bar sync has not been checked against a real transport**, and how the
  parameter groups land in the inspector has not been reviewed.
- **It has never been used on a live show.**
- **Windows is built but never run.**
- There is no OpenFX build yet, so no Resolve/Nuke/Natron.

## Not affiliated with Microsoft

This is an original implementation of screensavers everybody remembers. No
Microsoft code, artwork or assets are used or included. The Flying Windows logo
is a four-quad gesture toward the mark, not a reproduction of it, and 3D Pipes'
teapot is generated from a profile curve rather than being Newell's dataset.

## Licence

MIT. See [LICENSE](LICENSE).
