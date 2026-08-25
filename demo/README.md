# demo/ — the browser demo

The page at **idler-demo.stoatworks-labs.com**: Idler's two shader programs and a
JavaScript port of its 3D engine and its eleven savers, running in WebGL2 on
clips generated in the page.

## What is the plugin's, and what is not

Idler is the odd one out in this fleet, and the honesty question has a different
answer here than on the other demos.

On tinsel, orrery, downpour and the rest, the plugin *is* a fragment shader:
copying the shader across means the demo runs the plugin's actual work. Idler is
not shaped like that. `source/Shaders.cpp` is 242 lines and two programs — the
scene shader lights and fogs a triangle soup, the composite shader blends the
result over the clip. Everything that makes Idler *Idler* is CPU-side: a small 3D
engine and eleven savers that build meshes into a `Scene`.

So:

- **The two shader programs are copied character for character.**
  `tools/check_shaders.py` proves it.
- **Everything else is a hand port** — `Vec.h`, `Scene.h`, `Mesh.cpp`, `Hash.h`,
  `Controls.cpp`, `Presets.h`, `Font.cpp`, `Teapot.cpp` and the eleven savers,
  translated by hand into `plugin.js`. A hand port is a second implementation.

## How the port is checked

`tools/check_geometry.mjs` is the important one, and it exists because a second
implementation with nothing checking it is how a demo comes to draw a *plausible*
wrong picture — the exact failure this repo's harness exists to prevent.

`idtest --geometry` builds every saver on its own preset at a pinned time and
prints the triangle count, the vertex count and the bounding box.
`check_geometry.mjs` runs the ported savers under the same conditions and
compares the same three numbers. All eleven match.

```bash
node demo/tools/check_geometry.mjs
```

That catches a mis-ported loop bound, an off-by-one in a builder, a conversion
curve that has drifted, a hash that has lost its low bits to a double — the
errors that actually happen in a port like this and that look fine on screen. It
caught nothing on the way in, which is itself the point: it is how you know.

**What it does not cover.** It does not compare individual vertices, colours or
normals — a bounding box is a coarse instrument. And there is **no equivalent of
`idtest --replay`** here, which is what proves the two growing savers reach a
byte-identical state whether rendered cold or scrubbed to. 3D Pipes and 3D Maze
are the two most likely things on this page to be subtly wrong, and the geometry
check only exercises them at one instant.

Both checkers, and a kit-drift check, run from `tools/verify.sh`.

## Running it locally

```bash
python3 -m http.server 8805 --directory demo
```

There is no build step: hand-written ES modules, with the shared kit vendored
into `demo/vendor/` by `stoatworks-backend/resolume-demo/sync.sh`. Never edit
anything in `vendor/` — fix the master and re-sync, or the other ten demo repos
silently lack the fix.

## Deploying

```bash
cf-run npx wrangler deploy
```

Check `git status` first: a parallel session sharing this checkout can have
staged its own work into `demo/`.

Verify **by content, never by status code** — a wrong page returns a cheerful
200:

```bash
curl -s 'https://idler-demo.stoatworks-labs.com/?cb=1' | grep -o '<title>[^<]*'
```

A freshly attached custom domain 500s for a minute or two; retry before believing
it.

## What the page cannot have

Listed in full in the `differences` array in `plugin.js`, which is what the "What
this page does not reproduce" disclosure is built from. The ones worth knowing:

- **No host tempo.** Sync declares Free, Beat, Bar and Manual because the plugin
  does, but a browser has no transport. Beat and Bar fall through to the plugin's
  own 120 BPM fallback with the bar position taken from the same clock, which
  makes them deterministic functions of time rather than locked to anything.
- **No audio.** `PT_AUDIO` is an FFT buffer Resolume fills from an audio-source
  picker. The audio level here is always zero — which is the plugin's own
  behaviour with nothing routed, so Audio Size and Audio Speed do nothing rather
  than the picture twitching to a phantom signal.
- **No About parameter.** Display-only; the header links cover it.
- **Presets reset more than the plugin's do.** The kit's preset menu puts every
  parameter a preset does not name back to its default; the plugin leaves Sync,
  Phase, Seed, the text, Mask Mode, Mix and the audio controls alone.

## The variant switch

The transport's **Plugin** dropdown chooses between `Idler` (the source) and
`Idler Mask` (the effect), which in the repo are two bundles from one class and
one constructor flag.

Switching it re-applies **Background Alpha**'s constructor default — 1 on the
source, 0 on the effect. That is not a page invention: switching variant is
instantiating the other plugin, and a fresh `Idler Mask` really does start with a
transparent background. Without it the effect comes up with an opaque background
covering the clip it exists to draw over, which makes Over look like the effect
replaced the clip and makes Reveal, Hide and Colourise no-ops.

The preset table drops the background entries on the effect for the same reason,
exactly as `applyPreset` does in `Idler.cpp`.
