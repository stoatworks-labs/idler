#include "Idler.h"

/**
    The generator: the saver over its own background, no input.

    **This file is listed directly in the IdlerSource target, not in
    idler_core.** Both plugins share the class; what they do not share is the
    `CFFGLPluginInfo` below, and putting either registration in the shared
    library would register both plugins into both bundles.

    It is also why the shared library is an OBJECT library rather than a STATIC
    one. `CFFGLPluginInfo` registers itself from a file-scope constructor and
    nothing ever references it by name, so in an archive the linker is entitled
    to drop the whole translation unit -- giving a bundle that loads, exports
    `plugMain`, and reports that it contains no plugins.

        nm -gU Idler.bundle/Contents/MacOS/Idler | grep plugMain
*/
namespace
{
class IdlerSource : public idler::IdlerPlugin
{
public:
	IdlerSource() :
		IdlerPlugin( false )
	{
	}
};
} // namespace

static CFFGLPluginInfo PluginInfo(
	PluginFactory< IdlerSource >,                      // Create method
	"ID01",                                            // Plugin unique ID of maximum length 4
	"Idler",                                           // Plugin name
	2,                                                 // API major version number
	1,                                                 // API minor version number
	0,                                                 // Plugin major version number
	1,                                                 // Plugin minor version number
	FF_SOURCE,                                         // Plugin type
	"The Windows 95/98 screensavers, rebuilt.\n\nEleven of them: Mystify, Beziers, Curves and Colors, Flying Windows, Flying Through Space, Scrolling Marquee, 3D Maze, 3D Pipes, 3D Flying Objects, 3D FlowerBox and 3D Text.\n\nEvery saver is a pure function of time and a seed. Nothing integrates a velocity and nothing remembers the last frame, so a Mystify polygon bounces because its position is a triangle wave rather than because something tested for a wall - and when the show gets heavy and the frame rate drops, the motion does not slow down and come apart from the music.\n\nStart from a Preset, at the bottom.",// Plugin description
	"Idler FFGL source"                                // About
);

extern "C" const char* IdlerSourceBuildStamp()
{
	return "idler " IDLER_VERSION " source, built " __DATE__ " " __TIME__;
}
