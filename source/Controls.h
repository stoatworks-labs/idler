#pragma once

#include <cstdint>

#include "Scene.h"// for Shading, which a Settings carries
#include "Vec.h"

/**
    Host parameters, and what they mean.

    **Every numeric parameter Idler declares is a plain 0..1 float**, even where
    it stands for a maze size, a field of view in degrees or a seed. That is not
    a style preference. `CFFGLPluginManager::SetParamInfo` clamps an
    `FF_TYPE_STANDARD` default into 0..1 *before* returning, and `SetParamRange`
    can only be called afterwards because it finds the parameter by id -- so a
    parameter declared in degrees cannot declare a default in degrees. There is
    no `SetParamDefault`. A default of 60 becomes 1, silently, and the plugin
    starts up wrong in a way no build step notices.

    So the range lives here, in the conversion, and the host only ever sees
    0..1.

    ## The shape of the problem this file solves

    Eleven screensavers, one parameter list. Giving each saver its own controls
    would mean around ninety parameters, of which eighty-two are dead at any
    moment, and a saved composition that renumbers itself whenever a saver is
    added.

    So there are **seven scene controls with deliberately generic names**, and
    each saver maps them to whatever it has that is analogous. `Density` is the
    number of Mystify polygons, the number of stars, the number of pipes, the
    number of flying windows. `Complexity` is the vertex count of a Mystify
    polygon, the order of a Bezier, the size of the maze grid, how often a pipe
    turns.

    The cost of that is honest and worth stating: **the same slider does
    genuinely different things in different savers**, and a preset built for one
    saver rarely reads well on another. The alternative -- per-saver parameter
    banks -- costs more, because it puts the dead sliders in the inspector
    instead of in the documentation. Every mapping is written down in the
    saver's own file, and `tools/sweep.py` carries a table of which savers each
    control is live in, so a control that does nothing in the default saver is
    not reported as broken.
*/
namespace idler
{

/// The savers, in the order they appear in the dropdown.
///
/// Grouped as they were on the machine: the ones that shipped with Windows
/// first, then the OpenGL ones that came with Plus! and became stock in 98.
/// Adding one goes on the END -- a saved composition stores the element index,
/// so inserting in the middle silently changes what an existing project plays.
enum class SaverKind
{
	Mystify = 0,     ///< Bouncing polygons trailing their own history.
	Beziers,         ///< The same idea on curves rather than straight edges.
	Curves,          ///< "Curves and Colors": a spirograph on a cycling palette.
	FlyingWindows,   ///< Logos receding into the distance.
	Starfield,       ///< "Flying Through Space".
	Marquee,         ///< "Scrolling Marquee": a text banner crossing the frame.
	Maze,            ///< "3D Maze": a first-person walk down brick corridors.
	Pipes,           ///< "3D Pipes": a plumbing network growing in a box.
	FlyingObjects,   ///< "3D Flying Objects": lit solids tumbling past.
	FlowerBox,       ///< "3D FlowerBox": a polyhedron morphing on the spot.
	Text3D,          ///< "3D Text": extruded lettering, rotating.

	Count
};

const char* SaverName( SaverKind saver );

/// Where the clock comes from.
enum class Sync
{
	Free = 0,  ///< The host clock. Speed is a multiplier on real time.
	Beat,      ///< One host beat is one second of saver time.
	Bar,       ///< One host bar is one second of saver time.
	Manual,    ///< Speed is ignored; the Phase slider is the only driver, so the
	           ///< operator can key it against the edit.

	Count
};

const char* SyncName( Sync sync );

/// Where the colours come from.
enum class ColourMode
{
	Classic = 0,  ///< Each saver's own palette, as it was. Mystify's cycling
	              ///< primaries, the maze's red brick, the pipes' shiny plastic.
	Tint,         ///< Everything in the one chosen colour, shaded.
	Spread,       ///< The chosen colour as a starting hue, fanned out across the
	              ///< objects by Hue Spread.
	Cycle,        ///< Hue rotating with time, which is what most of these did
	              ///< anyway and what makes any of them usable behind a band.

	Count
};

const char* ColourModeName( ColourMode mode );

/// What the effect variant does with the clip it is given. Ignored by the
/// source, which has no clip.
enum class MaskMode
{
	Over = 0,   ///< The saver drawn on top of the clip.
	Reveal,     ///< The clip shows only where the saver drew.
	Hide,       ///< The clip shows everywhere except where the saver drew.
	Colourise,  ///< The clip, tinted by the saver, only where it drew.

	Count
};

const char* MaskModeName( MaskMode mode );

/**
    Parameter ids.

    The declaration order in Idler.cpp is the order they appear in the host, and
    the groups depend on consecutive ids staying consecutive -- `SetParamGroup`
    collapses *runs* of same-group parameters, so reordering these silently
    splits a group into two.

    Ids are also what a saved composition refers to. Anything added after
    release goes on the end, even where it belongs somewhere else in the list.
*/
enum ParamId : unsigned int
{
	// Saver
	PT_SAVER = 0,
	PT_PRESET,

	// Scene. Seven generic controls; what each means per saver is in that
	// saver's file and in sweep.py's context table.
	PT_DENSITY,
	PT_COMPLEXITY,
	PT_SIZE,
	PT_LENGTH,
	PT_LINE_WIDTH,
	PT_VARIATION,
	PT_SHADING,

	// Motion
	PT_SYNC,
	PT_SPEED,
	PT_PHASE,
	PT_SEED,

	// Camera. Live in the six 3D savers; dead in the 2D ones, which are drawn
	// through an orthographic camera that nothing here touches.
	PT_FOV,
	PT_CAM_DISTANCE,
	PT_CAM_TILT,
	PT_FOG,

	// Colour
	PT_COLOUR_MODE,
	PT_COLOUR_R,
	PT_COLOUR_G,
	PT_COLOUR_B,
	PT_HUE_SPREAD,
	PT_HUE_CYCLE,
	PT_OPACITY,
	PT_BACK_R,
	PT_BACK_G,
	PT_BACK_B,
	PT_BACK_OPACITY,

	// Text. Live in Scrolling Marquee and 3D Text.
	PT_TEXT,

	// Output. Both plugins declare both of these so that a composition can be
	// moved between the source and the effect without the parameter list
	// shifting underneath it; the source simply has nothing to mask against and
	// ignores them.
	PT_MASK_MODE,
	PT_MIX,

	// Audio. PT_AUDIO is an FFT buffer (FF_TYPE_BUFFER, FF_USAGE_FFT): Resolume
	// shows it as an audio-source picker and writes one spectrum bin per
	// element. FFGL only; OFX hosts have no audio analysis and never see these.
	PT_AUDIO,
	PT_AUDIO_SIZE,
	PT_AUDIO_SPEED,

	// Display-only. A TEXT parameter, which means the plugin MUST override
	// SetTextParameter as well as GetTextParameter -- the SDK's instantiateGL
	// sets every parameter's default on a fresh instance and deletes the
	// instance if any set returns FF_FAIL, and the base SetTextParameter is a
	// stub that does exactly that. Get it wrong and no real host can create the
	// plugin at all, while every offline harness in this repo still passes.
	PT_ABOUT,

	// One button per link the About block carries -- the guide, the project
	// page, the source, the funding page -- each of which opens a browser and
	// stores nothing. How many there are is decided by which URLs
	// StoatworksAbout.h actually holds, so Idler.cpp static_asserts this run
	// against `about::kParamCount`: the user guide added the fourth button, and
	// without the assert that would silently shift PT_COUNT and
	// leave the last one undeclared.
	//
	// These are FFGL-only, like PT_AUDIO above. IdlerOFX.cpp sizes its array
	// from PT_COUNT and fills the ids it knows by hand, so the extra slots
	// simply stay zero there.
	PT_ABOUT_BUTTON_1,
	PT_ABOUT_BUTTON_2,
	PT_ABOUT_BUTTON_3,
	PT_ABOUT_BUTTON_4,

	PT_COUNT
};

/// The number of FFT bins asked of the host.
constexpr unsigned int kAudioBins = 32;

/**
    The parameters, resolved.

    A saver is handed one of these rather than the raw slider values, so that
    the conversion curves live in one file and a saver reads a field with a
    physical meaning. The generic seven stay 0..1 here on purpose: their range
    is per-saver, and burning a single range into this struct would mean the
    maze and the starfield had to agree on what "density" means numerically.
*/
struct Settings
{
	SaverKind saver = SaverKind::Mystify;

	// The generic seven, 0..1. Mapped by each saver, documented there.
	float density    = 0.5f;
	float complexity = 0.5f;
	float size       = 0.5f;
	float length     = 0.5f;
	float lineWidth  = 0.5f;
	float variation  = 0.5f;
	Shading shading  = Shading::Lit;

	/// Saver time, in seconds. Already has speed, sync and phase folded in --
	/// a saver never sees the host clock, which is what lets the harness pin it.
	float time = 0.0f;

	uint32_t seed = 1;

	float fov         = 1.0472f;///< radians
	float camDistance = 0.5f;   ///< 0..1, meaning is per-saver
	float camTilt     = 0.0f;   ///< radians
	float fog         = 0.5f;   ///< 0..1; 0 disables

	ColourMode colourMode = ColourMode::Classic;
	Vec3 tint             = { 1.0f, 1.0f, 1.0f };
	float hueSpread       = 0.25f;
	float hueCycle        = 0.0f;///< turns per second
	float opacity         = 1.0f;
	Vec4 background       = { 0.0f, 0.0f, 0.0f, 1.0f };

	/// Aspect ratio of the render, width / height.
	float aspect = 16.0f / 9.0f;

	/// The marquee / 3D Text string.
	const char* text = "";

	/// Audio level, smoothed, 0..1. Zero when nothing is connected, which is
	/// the case every audio-reactive control has to be usable in.
	float audioLevel = 0.0f;
	float audioSize  = 0.0f;///< How much audioLevel scales things. 0 = off.
	float audioSpeed = 0.0f;///< How much audioLevel drives the clock. 0 = off.

	/// A colour for object `index` of `count`, honouring the colour mode.
	///
	/// `classic` is what that saver would have used, so a saver states its own
	/// palette and gets the four modes for free.
	Vec4 Colour( const Vec3& classic, int index, int count ) const;
};

//---------------------------------------------------------------------------
// The conversions. Curves rather than straight lines wherever the useful part
// of a range is bunched at one end.
//---------------------------------------------------------------------------

/// Speed multiplier. 0..4, exponential, exactly 1 at the centre of the slider
/// so "as it ran on the machine" is a place you can find by feel, with a dead
/// zone at the bottom so a frozen frame is reachable by dragging to zero
/// rather than by luck.
float SpeedFromParam( float value );

/// Phase offset in seconds. 0..60 -- long enough that Manual sync can key a
/// pipe network from empty to full across a minute-long cue.
float PhaseFromParam( float value );

/// The seed. 1..9999 -- an integer, so that nudging the slider grows a
/// different maze rather than an imperceptibly different one.
uint32_t SeedFromParam( float value );

/// Vertical field of view in radians. 20..120 degrees.
float FovFromParam( float value );

/// Camera tilt in radians. -60..60 degrees, exactly 0 at the centre.
float CamTiltFromParam( float value );

/// Hue range spanned across the object set, in turns. 0..1.
float HueSpreadFromParam( float value );

/// Hue rotation in turns per second. -0.5..0.5, exactly 0 at the centre.
float HueCycleFromParam( float value );

/// Every host parameter, mapped into a Settings.
///
/// The FFGL plugin and the OpenFX one both call this with their own 0..1 array,
/// so the curves, ranges and the background premultiply have exactly one home.
/// `time` is saver time with speed, sync and phase already folded in.
Settings SettingsFromParams( const float* params, int width, int height, float time,
                             const char* text, float audioLevel );

/// Read an option parameter.
///
/// Option parameters do NOT hold 0..1 -- they hold the element value the
/// operator chose, 0, 1, 2 and so on. The clamp is for a stale composition
/// naming an element that no longer exists.
int Option( float value, int count );

} // namespace idler
