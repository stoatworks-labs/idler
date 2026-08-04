#pragma once

/**
    Factory presets.

    In the rest of the fleet a preset is a shortcut. Here it is closer to load
    bearing, and it is worth being explicit about why.

    Eleven savers share seven generic scene controls (see Controls.h), so there
    is no single set of slider positions that is right for all of them. A
    Density that gives Mystify its four polygons gives the starfield four stars.
    Picking a saver from the dropdown therefore gives you *that saver driven by
    whatever the sliders happen to say*, which is frequently not what it looked
    like on the machine.

    **The first eleven presets are each saver as it actually ran.** They are the
    reference: pick "3D Pipes" from the preset list and you get the pipes at the
    density, camera and palette they had, not merely the Pipes code. The rest
    are looks that are useful behind a band and were never on anybody's
    monitor.

    The values live in the same 0..1 parameter space both builds expose (the
    FFGL and OFX builds deliberately share it), so ONE table drives both and a
    preset looks identical in Resolume and Resolve. Plain data only; the
    application machinery lives with each host's glue. Both FFGL plugins (the
    source and the mask) share the same class, so both get the dropdown from
    this one table too.

    Element 0 of the host-facing dropdown is "Custom" and is not in this table:
    it means "the sliders are the truth".

    A preset covers the saver, the scene, the camera and the colour. It leaves
    alone: Sync (the FFGL build offers beat modes the OFX build cannot, so an
    index here would mean different things in different hosts), Phase (the
    operator's driver, often keyed), Seed (which variation, not what kind), the
    text, Mask Mode (what the effect does to the clip is the operator's call),
    Mix, and the audio controls.
*/

namespace idler
{
namespace presets
{
/// The parameters a preset sets, in one fixed order. The FFGL build binds this
/// order to its ParamIds and the OFX build to its param handles; both
/// static_assert against kParamCount so the three lists cannot drift apart
/// silently.
enum Param
{
	kSaver,
	kDensity,
	kComplexity,
	kSize,
	kLength,
	kLineWidth,
	kVariation,
	kShading,
	kSpeed,
	kFov,
	kCamDistance,
	kCamTilt,
	kFog,
	kColourMode,
	kColourR,
	kColourG,
	kColourB,
	kHueSpread,
	kHueCycle,
	kOpacity,
	kBackR,
	kBackG,
	kBackB,
	kBackOpacity,
	kParamCount
};

struct Preset
{
	const char* name;
	float v[ kParamCount ];
};

// Option values are element indices, not 0..1:
//   Saver   0 Mystify / 1 Beziers / 2 Curves / 3 Flying Windows /
//           4 Flying Through Space / 5 Scrolling Marquee / 6 3D Maze /
//           7 3D Pipes / 8 3D Flying Objects / 9 3D FlowerBox / 10 3D Text
//   Shading 0 Flat / 1 Lit / 2 Wireframe
//   Colour  0 Classic / 1 Tint / 2 Spread / 3 Cycle
// Speed is unity at 0.5, Cam Tilt is level at 0.5, Fov is 60 degrees at 0.4.
inline constexpr Preset kPresets[] = {
	//-----------------------------------------------------------------------
	// The eleven, as they ran.
	//-----------------------------------------------------------------------

	// Two polygons of four vertices trailing eight steps, on cycling primaries.
	{ "Mystify",
	  { /*Saver*/ 0, /*Density*/ 0.55f, /*Complex*/ 0.35f, /*Size*/ 0.5f, /*Length*/ 0.45f,
	    /*Line*/ 0.25f, /*Vary*/ 0.5f, /*Shading*/ 0, /*Speed*/ 0.5f, /*Fov*/ 0.4f,
	    /*CamDist*/ 0.5f, /*Tilt*/ 0.5f, /*Fog*/ 0.0f, /*ColMode*/ 0,
	    /*RGB*/ 1.0f, 1.0f, 1.0f, /*HueSpr*/ 0.3f, /*HueCyc*/ 0.5f, /*Opacity*/ 1.0f,
	    /*Back*/ 0.0f, 0.0f, 0.0f, /*BackOp*/ 1.0f } },

	{ "Beziers",
	  { /*Saver*/ 1, /*Density*/ 0.3f, /*Complex*/ 0.35f, /*Size*/ 0.5f, /*Length*/ 0.5f,
	    /*Line*/ 0.25f, /*Vary*/ 0.5f, /*Shading*/ 0, /*Speed*/ 0.5f, /*Fov*/ 0.4f,
	    /*CamDist*/ 0.5f, /*Tilt*/ 0.5f, /*Fog*/ 0.0f, /*ColMode*/ 0,
	    /*RGB*/ 1.0f, 1.0f, 1.0f, /*HueSpr*/ 0.3f, /*HueCyc*/ 0.5f, /*Opacity*/ 1.0f,
	    /*Back*/ 0.0f, 0.0f, 0.0f, /*BackOp*/ 1.0f } },

	// The spirograph, on the slow palette rotation it always had.
	{ "Curves and Colors",
	  { /*Saver*/ 2, /*Density*/ 0.25f, /*Complex*/ 0.45f, /*Size*/ 0.6f, /*Length*/ 0.7f,
	    /*Line*/ 0.2f, /*Vary*/ 0.5f, /*Shading*/ 0, /*Speed*/ 0.5f, /*Fov*/ 0.4f,
	    /*CamDist*/ 0.5f, /*Tilt*/ 0.5f, /*Fog*/ 0.0f, /*ColMode*/ 0,
	    /*RGB*/ 1.0f, 1.0f, 1.0f, /*HueSpr*/ 0.5f, /*HueCyc*/ 0.55f, /*Opacity*/ 1.0f,
	    /*Back*/ 0.0f, 0.0f, 0.0f, /*BackOp*/ 1.0f } },

	{ "Flying Windows",
	  { /*Saver*/ 3, /*Density*/ 0.45f, /*Complex*/ 0.5f, /*Size*/ 0.5f, /*Length*/ 0.5f,
	    /*Line*/ 0.3f, /*Vary*/ 0.4f, /*Shading*/ 0, /*Speed*/ 0.5f, /*Fov*/ 0.45f,
	    /*CamDist*/ 0.5f, /*Tilt*/ 0.5f, /*Fog*/ 0.0f, /*ColMode*/ 0,
	    /*RGB*/ 1.0f, 1.0f, 1.0f, /*HueSpr*/ 0.3f, /*HueCyc*/ 0.5f, /*Opacity*/ 1.0f,
	    /*Back*/ 0.0f, 0.0f, 0.0f, /*BackOp*/ 1.0f } },

	// White dots on black, no streaks: the stock setting, not the "warp" one.
	{ "Flying Through Space",
	  { /*Saver*/ 4, /*Density*/ 0.55f, /*Complex*/ 0.5f, /*Size*/ 0.35f, /*Length*/ 0.1f,
	    /*Line*/ 0.3f, /*Vary*/ 0.5f, /*Shading*/ 0, /*Speed*/ 0.5f, /*Fov*/ 0.45f,
	    /*CamDist*/ 0.5f, /*Tilt*/ 0.5f, /*Fog*/ 0.0f, /*ColMode*/ 0,
	    /*RGB*/ 1.0f, 1.0f, 1.0f, /*HueSpr*/ 0.3f, /*HueCyc*/ 0.5f, /*Opacity*/ 1.0f,
	    /*Back*/ 0.0f, 0.0f, 0.0f, /*BackOp*/ 1.0f } },

	{ "Scrolling Marquee",
	  { /*Saver*/ 5, /*Density*/ 0.5f, /*Complex*/ 0.5f, /*Size*/ 0.55f, /*Length*/ 0.5f,
	    /*Line*/ 0.45f, /*Vary*/ 0.0f, /*Shading*/ 0, /*Speed*/ 0.5f, /*Fov*/ 0.4f,
	    /*CamDist*/ 0.5f, /*Tilt*/ 0.5f, /*Fog*/ 0.0f, /*ColMode*/ 0,
	    /*RGB*/ 1.0f, 1.0f, 1.0f, /*HueSpr*/ 0.3f, /*HueCyc*/ 0.5f, /*Opacity*/ 1.0f,
	    /*Back*/ 0.0f, 0.0f, 0.0f, /*BackOp*/ 1.0f } },

	// Red brick, fogged to black at the end of the corridor, 60 degrees.
	{ "3D Maze",
	  { /*Saver*/ 6, /*Density*/ 0.4f, /*Complex*/ 0.4f, /*Size*/ 0.5f, /*Length*/ 0.5f,
	    /*Line*/ 0.3f, /*Vary*/ 0.5f, /*Shading*/ 1, /*Speed*/ 0.5f, /*Fov*/ 0.4f,
	    /*CamDist*/ 0.5f, /*Tilt*/ 0.5f, /*Fog*/ 0.55f, /*ColMode*/ 0,
	    /*RGB*/ 1.0f, 1.0f, 1.0f, /*HueSpr*/ 0.3f, /*HueCyc*/ 0.5f, /*Opacity*/ 1.0f,
	    /*Back*/ 0.0f, 0.0f, 0.0f, /*BackOp*/ 1.0f } },

	// Shiny plastic elbows filling the box, one pipe at a time.
	{ "3D Pipes",
	  { /*Saver*/ 7, /*Density*/ 0.62f, /*Complex*/ 0.45f, /*Size*/ 0.5f, /*Length*/ 0.6f,
	    /*Line*/ 0.3f, /*Vary*/ 0.5f, /*Shading*/ 1, /*Speed*/ 0.5f, /*Fov*/ 0.4f,
	    /*CamDist*/ 0.5f, /*Tilt*/ 0.5f, /*Fog*/ 0.3f, /*ColMode*/ 0,
	    /*RGB*/ 1.0f, 1.0f, 1.0f, /*HueSpr*/ 0.6f, /*HueCyc*/ 0.5f, /*Opacity*/ 1.0f,
	    /*Back*/ 0.0f, 0.0f, 0.0f, /*BackOp*/ 1.0f } },

	{ "3D Flying Objects",
	  { /*Saver*/ 8, /*Density*/ 0.5f, /*Complex*/ 0.5f, /*Size*/ 0.5f, /*Length*/ 0.5f,
	    /*Line*/ 0.3f, /*Vary*/ 0.6f, /*Shading*/ 1, /*Speed*/ 0.5f, /*Fov*/ 0.4f,
	    /*CamDist*/ 0.5f, /*Tilt*/ 0.5f, /*Fog*/ 0.2f, /*ColMode*/ 0,
	    /*RGB*/ 1.0f, 1.0f, 1.0f, /*HueSpr*/ 0.4f, /*HueCyc*/ 0.5f, /*Opacity*/ 1.0f,
	    /*Back*/ 0.0f, 0.0f, 0.0f, /*BackOp*/ 1.0f } },

	{ "3D FlowerBox",
	  { /*Saver*/ 9, /*Density*/ 0.5f, /*Complex*/ 0.55f, /*Size*/ 0.55f, /*Length*/ 0.5f,
	    /*Line*/ 0.3f, /*Vary*/ 0.5f, /*Shading*/ 1, /*Speed*/ 0.5f, /*Fov*/ 0.4f,
	    /*CamDist*/ 0.5f, /*Tilt*/ 0.5f, /*Fog*/ 0.0f, /*ColMode*/ 0,
	    /*RGB*/ 1.0f, 1.0f, 1.0f, /*HueSpr*/ 0.7f, /*HueCyc*/ 0.5f, /*Opacity*/ 1.0f,
	    /*Back*/ 0.0f, 0.0f, 0.0f, /*BackOp*/ 1.0f } },

	{ "3D Text",
	  { /*Saver*/ 10, /*Density*/ 0.5f, /*Complex*/ 0.5f, /*Size*/ 0.55f, /*Length*/ 0.35f,
	    /*Line*/ 0.4f, /*Vary*/ 0.5f, /*Shading*/ 1, /*Speed*/ 0.5f, /*Fov*/ 0.4f,
	    /*CamDist*/ 0.5f, /*Tilt*/ 0.5f, /*Fog*/ 0.0f, /*ColMode*/ 0,
	    /*RGB*/ 1.0f, 1.0f, 1.0f, /*HueSpr*/ 0.3f, /*HueCyc*/ 0.5f, /*Opacity*/ 1.0f,
	    /*Back*/ 0.0f, 0.0f, 0.0f, /*BackOp*/ 1.0f } },

	//-----------------------------------------------------------------------
	// Looks that were never on anybody's monitor.
	//-----------------------------------------------------------------------

	// The maze as a wireframe over transparency, for laying over footage.
	{ "Maze Wireframe",
	  { /*Saver*/ 6, /*Density*/ 0.4f, /*Complex*/ 0.4f, /*Size*/ 0.5f, /*Length*/ 0.5f,
	    /*Line*/ 0.4f, /*Vary*/ 0.5f, /*Shading*/ 2, /*Speed*/ 0.55f, /*Fov*/ 0.5f,
	    /*CamDist*/ 0.5f, /*Tilt*/ 0.5f, /*Fog*/ 0.6f, /*ColMode*/ 1,
	    /*RGB*/ 0.2f, 1.0f, 0.9f, /*HueSpr*/ 0.3f, /*HueCyc*/ 0.5f, /*Opacity*/ 1.0f,
	    /*Back*/ 0.0f, 0.0f, 0.0f, /*BackOp*/ 0.0f } },

	// Dense fast starfield with long streaks: a hyperspace jump.
	{ "Warp Speed",
	  { /*Saver*/ 4, /*Density*/ 0.85f, /*Complex*/ 0.5f, /*Size*/ 0.3f, /*Length*/ 0.8f,
	    /*Line*/ 0.3f, /*Vary*/ 0.5f, /*Shading*/ 0, /*Speed*/ 0.75f, /*Fov*/ 0.6f,
	    /*CamDist*/ 0.5f, /*Tilt*/ 0.5f, /*Fog*/ 0.0f, /*ColMode*/ 3,
	    /*RGB*/ 0.5f, 0.7f, 1.0f, /*HueSpr*/ 0.3f, /*HueCyc*/ 0.56f, /*Opacity*/ 1.0f,
	    /*Back*/ 0.0f, 0.0f, 0.0f, /*BackOp*/ 1.0f } },

	// Mystify with everything turned up: a lot of long rainbow trails.
	{ "Mystify Overdrive",
	  { /*Saver*/ 0, /*Density*/ 0.8f, /*Complex*/ 0.6f, /*Size*/ 0.5f, /*Length*/ 0.9f,
	    /*Line*/ 0.4f, /*Vary*/ 0.8f, /*Shading*/ 0, /*Speed*/ 0.62f, /*Fov*/ 0.4f,
	    /*CamDist*/ 0.5f, /*Tilt*/ 0.5f, /*Fog*/ 0.0f, /*ColMode*/ 2,
	    /*RGB*/ 1.0f, 0.2f, 0.6f, /*HueSpr*/ 1.0f, /*HueCyc*/ 0.53f, /*Opacity*/ 1.0f,
	    /*Back*/ 0.0f, 0.0f, 0.0f, /*BackOp*/ 1.0f } },

	// Pipes on transparency, tinted, no fog: an overlay for a video wall.
	{ "Pipes Overlay",
	  { /*Saver*/ 7, /*Density*/ 0.45f, /*Complex*/ 0.6f, /*Size*/ 0.4f, /*Length*/ 0.7f,
	    /*Line*/ 0.3f, /*Vary*/ 0.5f, /*Shading*/ 1, /*Speed*/ 0.6f, /*Fov*/ 0.35f,
	    /*CamDist*/ 0.6f, /*Tilt*/ 0.5f, /*Fog*/ 0.0f, /*ColMode*/ 2,
	    /*RGB*/ 0.1f, 0.9f, 1.0f, /*HueSpr*/ 0.8f, /*HueCyc*/ 0.5f, /*Opacity*/ 1.0f,
	    /*Back*/ 0.0f, 0.0f, 0.0f, /*BackOp*/ 0.0f } },
};

inline constexpr int kCount = int( sizeof( kPresets ) / sizeof( kPresets[ 0 ] ) );

} // namespace presets
} // namespace idler
