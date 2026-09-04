#include "Idler.h"

/**
    The effect: the saver drawn over -- or cut into -- the incoming clip.

    See SourcePlugin.cpp for why this registration lives in its own translation
    unit and why the shared library is an OBJECT library.

    The only differences from the source are the `overInput` flag, which adds
    the input and switches the composite shader to its `HAS_INPUT` variant, and
    the plugin type. Mask Mode and Mix are declared by both so that a
    composition can be moved between them without the parameter ids shifting.
*/
namespace
{
class IdlerEffect : public idler::IdlerPlugin
{
public:
	IdlerEffect() :
		IdlerPlugin( true )
	{
	}
};
} // namespace

static CFFGLPluginInfo PluginInfo(
	PluginFactory< IdlerEffect >,                        // Create method
	"ID02",                                              // Plugin unique ID of maximum length 4
	"Idler Mask",                                        // Plugin name
	2,                                                   // API major version number
	1,                                                   // API minor version number
	0,                                                   // Plugin major version number
	1,                                                   // Plugin minor version number
	FF_EFFECT,                                           // Plugin type
	"The Windows 95/98 screensavers, over your clip.\n\nEleven of them: Mystify, Beziers, Curves and Colors, Flying Windows, Flying Through Space, Scrolling Marquee, 3D Maze, 3D Pipes, 3D Flying Objects, 3D FlowerBox and 3D Text.\n\nEvery saver is a pure function of time and a seed. Nothing integrates a velocity and nothing remembers the last frame, so a Mystify polygon bounces because its position is a triangle wave rather than because something tested for a wall - and when the show gets heavy and the frame rate drops, the motion does not slow down and come apart from the music.\n\nStart from a Preset, at the bottom.",// Plugin description
	"Idler Mask FFGL effect"                             // About
);

extern "C" const char* IdlerEffectBuildStamp()
{
	return "idler " IDLER_VERSION " effect, built " __DATE__ " " __TIME__;
}
