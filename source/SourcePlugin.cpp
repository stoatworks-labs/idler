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
	"The Windows 95/98 screensavers, rebuilt",         // Plugin description
	"Idler FFGL source"                                // About
);

extern "C" const char* IdlerSourceBuildStamp()
{
	return "idler " IDLER_VERSION " source, built " __DATE__ " " __TIME__;
}
