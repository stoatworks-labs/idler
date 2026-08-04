#pragma once

#include <cstdint>
#include <memory>

#include "Controls.h"
#include "Hash.h"
#include "Scene.h"

/**
    The savers, and the one idea that makes eleven of them one plugin.

    ## Every saver is a pure function of (time, seed)

    A saver is handed a `Settings` -- which carries a time in seconds and a seed
    and nothing else that varies -- and fills a `Scene`. It is not told what the
    last frame looked like, and it does not keep one.

    That is the same rule the rest of the fleet's generators follow, and it buys
    the same four things:

    - **Nothing drifts with the frame rate.** A Mystify polygon bounces because
      its position is a triangle wave of time, not because something integrates
      a velocity and tests for a wall. Resolume's frame rate drops when the show
      gets heavy, and a saver that slowed down under load would come apart from
      the music.
    - **Any frame renders on its own.** `idtest --time 84.5` renders the frame
      at 84.5 seconds cold, which is what nearly every test in this repo
      depends on.
    - **Beat sync is free rather than bolted on.** Time is just a number. Give
      it the host clock and the saver free-runs; give it the host's bar position
      and it locks, with no second code path.
    - **Scrubbing works.** Drag the Phase slider and the picture goes where you
      dragged it, because there is nothing that had to have happened first.

    Six of the eleven are pure functions outright. Mystify, Beziers, Curves,
    Flying Windows, Flying Through Space and Scrolling Marquee are all
    positions computed from time by arithmetic, exactly like the rest of the
    fleet.

    ## The two that could not be, and what was done about them

    **3D Pipes grows.** Where the pipe goes next depends on where it has
    already been -- it must not run back through itself -- so segment 400 is not
    computable from time without knowing segments 1 to 399. **3D Maze** is
    milder but the same: which corridor the camera turns down depends on the
    maze it generated and the junctions it has already taken.

    Rather than give those two a private exception, they are made pure by
    **deterministic replay**. `GrowingSaver` below defines a fixed-size tick,
    and the state at time t is *by definition* the state reached by replaying
    from the seed to tick `floor( t * tickRate )`. So the property that matters
    is preserved exactly: the frame at t depends on nothing but t and the seed.

    The cache is an optimisation and **must never change the answer**. It holds
    the state and the tick it belongs to, and it is used only to skip forward.
    Anything else -- a different seed, a parameter that changes how growth
    works, or a time earlier than the cache -- rebuilds from tick zero.
    `idtest --replay` is the test for exactly this: it renders a frame cold and
    renders it again after running the clock forwards through it, and the two
    must be byte-identical.

    Two consequences worth knowing:

    - **A backwards scrub is expensive**, because it replays. It is bounded by
      `kMaxReplaySteps` and by the fact that both growing savers reset
      themselves when they fill up, so the work is capped, not unbounded.
    - **Ticks are not frames.** Dropping frames costs nothing; the next frame
      replays to wherever it should be.

    ## Counter-based randomness

    Replay only works if the random decisions replay too, which is why `Random`
    in Hash.h is a counter rather than a state machine -- see the note there.
*/
namespace idler
{

/// The base every saver implements.
class Saver
{
public:
	virtual ~Saver() = default;

	/// Fill `scene` with the picture at `settings.time`.
	///
	/// `scene` arrives cleared. A saver sets the camera, the shading and the
	/// mesh; it never touches OpenGL.
	virtual void Build( const Settings& settings, Scene& scene ) = 0;

	/// How many triangles this saver would like the caller to expect, for the
	/// harness's benefit. Zero means "no useful estimate".
	virtual size_t ExpectedTriangles( const Settings& ) const { return 0; }
};

/**
    The base for the two savers that grow.

    A subclass supplies the tick rate, a reset, a step, and a draw. This class
    supplies the replay, the cache and the invalidation, so that neither
    subclass can get the "cache must not change the answer" rule subtly wrong
    in its own way.
*/
class GrowingSaver : public Saver
{
public:
	void Build( const Settings& settings, Scene& scene ) final;

	/// Throw the cache away. Only the harness needs this -- it is how
	/// `idtest --replay` renders a frame cold rather than from wherever the
	/// previous render left the cache.
	void Invalidate() { cachedTick = -1; }

protected:
	/// Ticks per second of saver time. Constant per saver: a tick that varied
	/// with a parameter would mean the same time landed on a different tick
	/// depending on a slider, and the picture would jump when that slider moved.
	virtual float TickRate( const Settings& settings ) const = 0;

	/// Start again from nothing. `rng` has already been reset to the seed.
	virtual void ResetState( const Settings& settings, Random& rng ) = 0;

	/// Advance exactly one tick.
	virtual void Step( const Settings& settings, Random& rng ) = 0;

	/// Draw the current state. `alpha` is 0..1 through the current tick, for
	/// interpolating the part of the picture that moves smoothly -- the tip of
	/// a growing pipe, the camera between two maze cells.
	virtual void Draw( const Settings& settings, float alpha, Scene& scene ) = 0;

	/// Everything that changes what growth *does*, mixed into one number.
	///
	/// Not the whole `Settings`: colour, opacity and the camera all change the
	/// picture without changing the pipe network, and rebuilding the network
	/// when somebody nudges a hue slider would make those sliders stutter. A
	/// subclass includes the seed plus whichever of the generic seven it reads
	/// during growth, and nothing else.
	virtual uint64_t GrowthKey( const Settings& settings ) const = 0;

private:
	/// The ceiling on one frame's replay. Reached only by a wild host clock or
	/// a scrub to the far end of a long composition; logged when it is, because
	/// the symptom otherwise looks like the host hanging.
	static constexpr int kMaxReplaySteps = 400000;

	Random rng;
	uint64_t cachedKey = 0;
	int cachedTick     = -1;
};

/// Build the saver for `kind`.
std::unique_ptr< Saver > MakeSaver( SaverKind kind );

//---------------------------------------------------------------------------
// The individual savers. Declared here rather than in eleven headers because
// the only thing that ever names one is MakeSaver.
//---------------------------------------------------------------------------
std::unique_ptr< Saver > MakeMystify();
std::unique_ptr< Saver > MakeBeziers();
std::unique_ptr< Saver > MakeCurves();
std::unique_ptr< Saver > MakeFlyingWindows();
std::unique_ptr< Saver > MakeStarfield();
std::unique_ptr< Saver > MakeMarquee();
std::unique_ptr< Saver > MakeMaze();
std::unique_ptr< Saver > MakePipes();
std::unique_ptr< Saver > MakeFlyingObjects();
std::unique_ptr< Saver > MakeFlowerBox();
std::unique_ptr< Saver > MakeText3D();

//---------------------------------------------------------------------------
// Shared helpers the savers use.
//---------------------------------------------------------------------------

/// The orthographic camera the 2D savers draw through.
///
/// X runs -aspect..+aspect and Y runs -1..+1, so **one unit is half the frame
/// height at any aspect ratio**. A saver sizing something in these units keeps
/// its proportions when the composition gets wider, and a circle is round.
/// Getting this wrong is invisible on a square render and draws ellipses on
/// every real output.
void SetFlatCamera( const Settings& settings, Scene& scene );

/**
    A point bouncing inside the flat camera's box, as a pure function of time.

    Shared by Mystify and Beziers, which are the same motion with a different
    thing drawn through the points.

    Each point gets its own rate and starting phase from the hash, and each axis
    is an independent triangle wave -- so it travels in a straight line, turns
    the instant it touches an edge, and does it without anything integrating a
    velocity or testing for a collision. `margin` keeps the point that far
    inside the edge, so a thick line does not get its outer half clipped at the
    turn.
*/
Vec2 BouncePoint( uint32_t seed, int index, float time, float rate, float aspect, float margin );

/**
    One object flying out of the middle of the screen toward the viewer.

    Shared by Flying Through Space and Flying Windows, which are the same
    motion with a different thing drawn at each point -- a dot in one, the
    Windows logo in the other. Naming it once is also what keeps them
    consistent: they were consistent on the machine, because they were the same
    trick.

    The object travels along a fixed direction from the origin at a constant
    rate in Z, and the perspective divide does the rest -- so it accelerates
    across the screen as it approaches, which is what makes it read as depth
    rather than as something being scaled up.

    `age` is 0 at the far plane and 1 at the near one, and it wraps: object `i`
    is *always* somewhere on its run, so the field is full from the first frame
    rather than filling up over the first ten seconds. That matters because a
    VJ triggering the clip wants it already going.
*/
struct Flight
{
	Vec2 screen;///< Position in the flat camera's units.
	float scale;///< Size multiplier from the perspective divide.
	float age;  ///< 0 at the far plane, 1 at the near one.
};

Flight FlyingPoint( uint32_t seed, int index, float time, float rate );

/// Map the generic 0..1 `density` onto a whole number in [lo, hi].
///
/// Quadratic, because the difference between 3 and 4 of something is a
/// different-looking picture and the difference between 51 and 52 is nothing --
/// so a linear slider would spend most of its travel on choices nobody makes.
int CountFromDensity( float density, int lo, int hi );

} // namespace idler
