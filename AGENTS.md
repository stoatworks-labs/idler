# idler — orientation for another LLM (or a newcomer)

**What it is:** the Windows 95/98 screensaver suite, as **two** FFGL 2.1 plugins
for Resolume Arena/Avenue. `Idler` is a source that draws a saver over its own
background; `Idler Mask` is an effect that draws it over — or cuts it into — the
incoming clip. Eleven savers, one plugin. C++17 + GLSL 4.1, CMake, universal
macOS `.bundle` and a Windows `.dll`. Public, MIT,
`github.com/stoatworks-labs/idler`.

`CLAUDE.md` is the command reference — build, install, verify. This file is the
*why*: read it before touching the replay, the coordinate conventions, or the
framebuffer.

---

## The one idea

**Every saver is a pure function of (time, seed).**

A saver is handed a `Settings` — which carries a time in seconds and a seed and
nothing else that varies — and fills a `Scene`. It is not told what the last
frame looked like, and it does not keep one.

Four things follow, and all four are why it is written this way:

- **Nothing drifts with the frame rate.** A Mystify polygon bounces because its
  position is a triangle wave of time, not because something integrates a
  velocity and tests for a wall. Resolume's frame rate drops when the show gets
  heavy, and a saver that slowed down under load would come apart from the music.
- **Any frame renders on its own.** `idtest --time 84.5` renders the frame at
  84.5 seconds cold. Nearly every test here depends on it.
- **Beat sync is free rather than bolted on.** Time is just a number. Give it
  the host clock and the saver free-runs; give it the host's bar position and it
  locks, with no second code path.
- **Scrubbing works.** Drag Phase and the picture goes where you dragged it,
  because nothing had to have happened first.

### The two that could not be, and what was done about them

**3D Pipes grows.** Where a pipe goes next depends on where it has already been
— it must not run back through itself — so segment 400 is not computable from
the clock without segments 1 to 399. **3D Maze** is milder but the same.

Rather than give those two a private exception, they are made pure by
**deterministic replay** (`GrowingSaver` in `Savers.h`). A tick is a fixed unit
— one cell of travel — and the state at time *t* is *by definition* the state
reached by replaying from the seed to tick `floor( t × tickRate )`.

**The cache is an optimisation and must never change the answer.** It holds a
state and the tick it belongs to, and it is used only to skip forward. A
different seed, a parameter that changes how growth works, or a time earlier
than the cache all rebuild from tick zero. `idtest --replay` is the test for
exactly this, and it demands **byte-identical** frames — a cache that is merely
nearly right passes every visual check there is.

Replay only works if the random decisions replay too, which is why `Random` in
`Hash.h` is a **counter** rather than a state machine: draw *n* is
`Hash2( n, seed )` and depends on nothing before it, so a checkpoint is one
integer.

---

## The traps

Ordered by how much time they will cost you.

**This is the one plugin in the fleet that allocates its own framebuffer, and it
has to.** Six savers are perspective 3D with self-overlapping geometry, and a
depth test needs a depth buffer; the host's framebuffer is colour-only and there
is nothing to borrow. `Target` is written by hand rather than subclassed off
`ffglex::FFGLFBO` because **both** of that class's bugs live in the parts that
would have been inherited: `Release()` leaks the colour texture (it tests
`depthBufferID` twice), and `Initialise` allocates under a `ScopedTextureBinding`
whose destructor **clears** the binding to 0 rather than restoring it — so
allocating silently unbinds the caller's input texture, on the allocating frame
only. `Target::Ensure` saves and restores `GL_TEXTURE_BINDING_2D` and
`GL_FRAMEBUFFER_BINDING` for that second reason.

**`ScopedFBOBinding` restores the framebuffer and NOT the viewport**, so the
off-screen pass's size leaks into the composite pass. The symptom does not look
like a viewport bug: the plugin renders correctly into a *corner* of the frame,
which in any viewer that shows transparency as white reads as the effect having
blown out to solid white. `Render` restores both by hand.

**A core profile is only required to support a line width of 1.0, and macOS
supports exactly that.** `glLineWidth( 4 )` raises no error, sets no state, and
draws hairlines. So every line in this plugin is **real geometry**:
`Mesh::AddPolyline` builds mitred quads, and Wireframe shading fades in
fragments near a triangle edge rather than using `glPolygonMode`.

**Wireframe de-indexes the mesh, and only wireframe does.** A shared vertex
cannot carry three different barycentric corners at once. That happens in
`UploadMesh`, not in any saver.

**A ranged parameter cannot have a ranged default.** `SetParamInfo` clamps an
`FF_TYPE_STANDARD` default into 0..1 *before* returning, and `SetParamRange` can
only be called afterwards. There is no `SetParamDefault`. So every host
parameter here is 0..1 and the conversions live in `Controls.cpp`. A default
field of view of 60 degrees would silently become 1.

**Option parameters do NOT hold 0..1.** They hold the element value the operator
chose — 0, 1, 2… — read through `Option()`, which rounds and clamps. The clamp
is for a stale composition naming an element that no longer exists.

**A TEXT parameter without a `SetTextParameter` override makes the whole plugin
uninstantiable.** The SDK's `instantiateGL` sets every parameter's default on a
fresh instance and **deletes the instance if any set returns FF_FAIL**, and the
base `SetTextParameter` is a stub that returns exactly that. `PT_ABOUT` is
display-only and still returns `FF_SUCCESS`. This shipped in three fleet plugins
before it was found, and it is invisible to every harness here — they drive the
class directly and never go through `plugMain`.

**`FFGLShader::Set` has no matrix overload, and no overload a `mat4` would fail
to convert to** — so a wrong call would compile. Both matrices go through a raw
`glUniformMatrix4fv` on a `FindUniform` location.

**Most parameters are supposed to do nothing most of the time.** Eleven savers
share seven generic scene controls: `Fog` is dead in Mystify, `Line Width` is
dead in FlowerBox, `Text` is dead in nine of eleven. That is the cost of the
design, and `tools/sweep.py` carries the context table that stops it reporting
half the parameter list as broken. **Add a saver and that table wants
revisiting.**

**Beat sync recovers a bar count without keeping one** — `round( estimate −
barPhase )` reconciles the clock's estimate with the exact position inside the
bar. It can name the wrong *absolute* bar if the transport did not start at
zero. For the nine pure savers that is invisible, because their animation
repeats. For the two growing ones it is **not**: an integer bar offset means the
pipe network starts part-grown. That is a defensible reading of "locked to the
transport" and it is written down rather than worked around, because the
alternative is a bar counter, which is state.

**Audio Speed is the one integration in the plugin, and it is deliberately its
own control.** There is no "what was the spectrum forty seconds ago", so an
audio-driven clock cannot be scrubbed and cannot be a pure function of time. It
is separate from Speed, and it defaults to zero, so nothing else loses the
property by accident.

**The effect variant must default Background Opacity to 0, and the source to 1.**
They are the same class with one flag, so it is natural to give them the same
defaults, and v0.1.0 shipped doing exactly that. An opaque black background
covers the clip the effect exists to draw over: Over looks like the effect
replaced the clip, and Reveal, Hide and Colourise become no-ops, because the
scene's alpha is 1 everywhere and there is nothing for them to cut against. The
source wants the opposite — opaque black is what makes its output usable as a
luma mask on the layer above. **Presets skip the background on the effect** for
the same reason they already skip Mask Mode and Mix: on an effect the background
is a compositing decision about somebody else's clip, not a property of how the
saver looked. Found by porting to OFX, where the mask plugin rendered the saver
over a test clip that had vanished.

**`set -o pipefail` plus `grep -q` is a trap, and `tools/verify.sh` had it.**
A `grep -q` that finds its match exits immediately; the writer upstream takes
SIGPIPE; under pipefail the whole pipeline reports failure even though the match
succeeded. So `nm -gU "$binary" | grep -q plugMain` reports a correctly-exported
symbol as missing. It is **output-size dependent**, so it fails intermittently
and on the larger binary first — here it gave a false FAIL on the OFX bundle
while the identical FFGL line got away with it. Capture into a variable and match
with `case`, which has no pipeline at all. The direction matters: written as
`if ! ... | grep -q FORBIDDEN`, the same bug is a false **PASS**.

**`flat`, `active`, `filter`, `input`, `output`, `sample` and `common` are GLSL
reserved words**, and a shader that will not compile surfaces only at runtime,
as "the plugin does nothing". That is what `Diag` is for.

---

## The OpenFX build

`source/ofx/` is a second **renderer**, not a second plugin. `Savers.cpp`,
`Controls.cpp`, `Mesh.cpp` and `Presets.h` are linked straight into the OFX
target from the same files the FFGL bundles use — the CMake list of savers is
declared once and shared, because a twelfth saver added to a duplicated list
links fine on the side it was added to.

An OFX host hands over a buffer and expects pixels; Resolve will call the CPU
path whenever it likes and Natron has no GL path at all. So `Raster.cpp`
rasterises `Scene` in software, mirroring `Shaders.cpp` and the GL state
`Idler.cpp` sets. Two renderers for one plugin is a divergence waiting to
happen, and the divergence anyone would actually hit is a preset that looks
right in Resolume and wrong in Resolve — which is what `idtest --raster` exists
to catch. It compares the picture rather than the pixels, because a GPU's fill
rule, interpolation precision and `fwidth` are its own.

Two things the software path cannot cut a corner on:

- **Near-plane clipping.** A vertex at or behind the eye has w <= 0 and the
  divide puts it somewhere meaningless rather than merely wrong. 3D Maze walks
  the camera down a corridor with walls passing either side, so it fires on most
  of that saver's frames.
- **Draw order.** With blending and the depth test both on, the picture depends
  on the order the mesh was built in, so triangles are never sorted.

What differs from FFGL, deliberately: Sync offers Free and Manual only (OFX
carries no tempo, and deriving one from the frame number would be a different
feature wearing the same name), and there is no audio. The render is
`eRenderInstanceSafe` because the growing savers keep a replay cache, and two
concurrent renders of one instance sharing it would flicker between two network
lengths; the compositing pass is still threaded.

**It has never run in a real OFX host** — only under `ofxprobe`.

## Checking your work

`tools/verify.sh` runs the lot, including the bundle checks the release workflow
does — architecture, `plugMain`, `CFBundleExecutable`, and an ad-hoc codesign of
a *copy*. Those are duplicated here on purpose: a check that only ever runs in
CI, after a tag, is a check that will catch you after the tag.

The three that matter check different things:

- **`--replay`** guards the central claim, above. Byte-identical or it fails.
  It is blind to a saver being *consistently* wrong.
- **`--geometry`** measures the mesh rather than the picture: triangle counts,
  bounding boxes, indices in range, and that every normal is unit length. That
  last one earns its place because a zero-length normal is a NaN once
  normalised, and one NaN normal takes the lighting of the **whole draw call**
  with it — so the symptom is a saver that goes black with nothing to say why.
  It is blind to everything after the mesh: camera, shading, compositing.
- **`--coverage`** requires every saver, at five times including zero, to emit
  geometry that lands somewhere. Crude, and it earns its place because the most
  likely way to break one of eleven savers is to make it draw *nothing*, which
  is invisible in a repo where the default saver still works.
- **`sweep.py`** is the only thing that catches a dead control.

**`idtest --sheet` asserts nothing and is the most valuable of the lot.** Every
real bug found in this repo so far was found by looking at one, not by an
assertion: the pipes beaded at every cell because a joint ball went on every
path point rather than every corner; the maze walls flat enough that a frame
facing one read as a rendering failure; the Mystify trail spanning a thirtieth
of the motion, so twelve trail steps landed on top of each other. **All three
passed every assertion above.** Regenerate it after any change to a saver, and
look at it.

**Host verification is Allan's, not an agent's.** Driving the Resolume GUI from
a session is unreliable, so nothing here should attempt it.

**Both plugins have been loaded into Resolume and run, and all eleven savers
render there** (Allan, 2026-08-04, ahead of v0.1.0). That is the first time a
plugin in this fleet reached a real host before its release rather than after.

Still unchecked in the host, and worth saying so rather than letting the
distinction rot: whether **Bar sync** locks against a real transport, and how
the **parameter groups** land in the inspector. Neither is provable offline —
the harness supplies its own clock and has no inspector.

---

## Things deliberately not done

- **No brick texture in 3D Maze.** The walls are tessellated into courses of
  brick as real geometry instead. A bitmap would have to be carried in the
  plugin and would look like a low-resolution bitmap on a 4K output. This is
  not decoration: a maze walk spends much of its time facing a wall half a cell
  away, and a *flat-shaded* wall makes that frame one rectangle of one colour,
  which reads as the renderer having failed.
- **The teapot is not Newell's.** It is built from a profile curve, a swept
  spout and a torus arc. At the size of a pipe elbow the difference is below a
  pixel, and a table of 306 control points typed from memory is a table that can
  be quietly wrong — where a generated teapot either has a spout or does not,
  and `--geometry` can check it.
- **No lowercase in the font.** Lowercase is drawn as small capitals. A stroke
  font's lowercase is a second full alphabet of different construction, for two
  savers that are nearly always set in capitals.
- **The Flying Windows logo is a gesture, not a reproduction.** Four sheared
  quads in the four colours. It reads as the mark at speed and copies nobody's
  artwork.
- **3D Flying Objects are allowed to pass through each other.** Keeping them
  apart needs a collision response, which is state, and would cost the
  seekability.
- **No multisampling on the target.** The 2D savers antialias in the geometry;
  the 3D ones are hard-edged solids that read correctly aliased, as they did on
  the machine.
- **No culling.** Several savers are single-sided surfaces seen from both sides,
  and the fragment shader already flips the normal on a back face.

## The OpenFX port, when it happens

There is **no OFX build**, and the tree deliberately says so: `external/openfx`
and `cmake/InfoOFX.plist.in` are not vendored, because a vendored SDK that
nothing compiles reads as a half-finished port. Both come back when the port
does — copy the plist template from **flipbook**, which is the one parameterised
on `@PROJECT_NAME@` rather than hardcoding the previous plugin's
`CFBundleExecutable`. That mistake costs a tag: the bundle assembles, `lipo` and
`nm` pass, the plugin loads and renders, and `codesign` fails.

Porting is not a thin shim here. The FFGL build is a GPU renderer with a depth
buffer; an OFX host wants a CPU render, so it needs a **software rasteriser over
`Scene`** — triangles with a z-buffer and the same shading rules. That is the
work, and `Scene` was shaped to make it possible: a saver never touches OpenGL,
so all eleven would come across unchanged.

Related: [orrery](https://github.com/stoatworks-labs/orrery) (the CMake, harness
and Diag patterns came from there), downpour, old-cathode, nesolume.
