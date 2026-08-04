#pragma once

#include <string>

/**
    Logging for a plugin that lives inside somebody else's process.

    A small member of the fleet's `diag` family. The rest of the repos get a
    rotating log, a crash report and a diagnostics bundle; an FFGL plugin gets
    only the log, for two reasons:

    - **No crash handler.** A plugin loaded into Resolume must not install a
      process-wide signal handler. It would intercept faults that are not ours
      and interfere with the host's own handling. A plugin has no business
      deciding what happens when Resolume dies.
    - **No bundle command.** There is no UI to hang one off -- a plugin is a
      list of sliders in someone else's inspector.

    What it covers is the failures that actually happen, all of which look
    identical from the operator's side ("it does nothing") with no message
    anywhere:

    - **A shader would not compile**, so `InitGL` returns `FF_FAIL`. The GL
      vendor, renderer and version strings are logged next to the compile log,
      because a shader that builds on one machine and not on another is a driver
      answer, not a source answer.
    - **The depth buffer would not attach.** Idler is the first plugin in the
      fleet to allocate its own framebuffer, because a depth test cannot be
      borrowed from the host's. An incomplete framebuffer draws nothing, and
      `glCheckFramebufferStatus` is the only place that says why.
    - **A replay ran away.** If the tick a frame asks for is millions ahead of
      the cache -- a host handing over a wild clock value, or a seek to the far
      end of a long composition -- the replay is capped and the cap is logged,
      because the symptom is otherwise a plugin that appears to hang the host.
*/
namespace idler::diag
{

/// Open the log file and record the plugin build, once per process.
void init();

void info( const std::string& message );
void warn( const std::string& message );
void error( const std::string& message );

/// Full path of the log file, for the README to point at.
std::string logPath();

} // namespace idler::diag
