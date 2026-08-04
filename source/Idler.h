#pragma once

#include <FFGLSDK.h>

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "Controls.h"
#include "Presets.h"
#include "Savers.h"
#include "Scene.h"
#include "Target.h"

/**
    Idler -- the Windows 95/98 screensavers, as a generator for Resolume.

    What is worth knowing about how it works is in three other files and not
    repeated here:

    - **Savers.h** -- every saver is a pure function of (time, seed), and the
      two that grow are made pure by deterministic replay.
    - **Scene.h** -- a saver fills a camera and one triangle mesh, and never
      touches OpenGL.
    - **Target.h** -- why this is the one plugin in the fleet that allocates its
      own framebuffer, and which two SDK bugs that walks into.

    This class is the part that talks to the host: it declares the parameters,
    normalises the host clock, resolves everything into a `Settings`, asks the
    current saver for a `Scene`, and draws it.

    Both plugins are this class. The source draws the saver over its own
    background; the effect draws it over, or into, the incoming clip. They
    differ by a constructor flag, one `#define` handed to the composite shader,
    and their input count -- little enough that keeping them as one class is
    what stops them drifting apart.

    See AGENTS.md for the traps.
*/
namespace idler
{
class IdlerPlugin : public CFFGLPlugin
{
public:
	explicit IdlerPlugin( bool overInput );

	// CFFGLPlugin
	FFResult InitGL( const FFGLViewportStruct* vp ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* pGL ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float GetFloatParameter( unsigned int index ) override;

	char* GetTextParameter( unsigned int index ) override;

	/// Display-only text still has to accept a set.
	///
	/// `instantiateGL` in the SDK sets EVERY parameter's default on a fresh
	/// instance and **deletes the instance if any set returns FF_FAIL**, and the
	/// base `CFFGLPlugin::SetTextParameter` is a stub that returns exactly that.
	/// So a plugin that declares the About line without overriding this cannot
	/// be instantiated by any real host at all -- while every harness in this
	/// repo, which drives the class directly and never goes through plugMain,
	/// carries on passing.
	FFResult SetTextParameter( unsigned int index, const char* value ) override;

	/// Render one frame into whatever is currently bound, at `width` x `height`.
	///
	/// Exposed for the offline harness, which drives this class rather than a
	/// copy of it -- a test that exercises a reimplementation tests the
	/// reimplementation.
	void Render( int width, int height, GLuint inputTexture, float maxU, float maxV, GLuint hostFBO );

	/// The settings as they would be resolved at this size, for the harness to
	/// predict what the saver should have drawn.
	Settings CurrentSettings( int width, int height ) const;

	/// The scene the last Render built. The harness measures this: how many
	/// triangles, where the bounding box is, whether every normal is unit
	/// length.
	const Scene& LastScene() const { return scene; }

	/// Pin the driven time, ignoring the host clock and the beat.
	///
	/// The harness needs this because a test that renders "the frame at 84.5
	/// seconds" and then predicts what is in it has to be certain that both
	/// halves used the same 84.5 -- and `hostTime` is a double that arrives
	/// from outside.
	///
	/// The Phase parameter is still added on top, so pinning does not make that
	/// slider look dead to a sweep.
	void SetTimeOverride( float seconds );

	/// Throw away any replay cache, so the next frame is rendered cold.
	/// `idtest --replay` is the reason this is public.
	void InvalidateReplay();

private:
	/// The ParamId each presets::Param drives, in presets::Param order. The
	/// preset table stays host-agnostic; this is the FFGL binding of it.
	static constexpr unsigned int kPresetParamIDs[ presets::kParamCount ] = {
		PT_SAVER, PT_DENSITY, PT_COMPLEXITY, PT_SIZE, PT_LENGTH, PT_LINE_WIDTH,
		PT_VARIATION, PT_SHADING, PT_SPEED, PT_FOV, PT_CAM_DISTANCE, PT_CAM_TILT,
		PT_FOG, PT_COLOUR_MODE, PT_COLOUR_R, PT_COLOUR_G, PT_COLOUR_B,
		PT_HUE_SPREAD, PT_HUE_CYCLE, PT_OPACITY,
		PT_BACK_R, PT_BACK_G, PT_BACK_B, PT_BACK_OPACITY
	};

	/// Copy a factory preset's values into params[] and raise value events so
	/// the host re-reads the sliders. `presetIndex` is 1-based; 0 is Custom.
	void applyPreset( int presetIndex );

	bool BuildShaders();

	/// Make sure `saver` matches PT_SAVER. Called at the top of a render rather
	/// than from SetFloatParameter, because a host is entitled to change a
	/// parameter from a thread that has no GL context -- and building a saver
	/// there would be fine today and a crash the first time one of them wants a
	/// texture.
	void EnsureSaver();

	/// Pack `scene.mesh` into `uploadBuffer` and hand it to the GPU.
	///
	/// De-indexes when the shading is Wireframe, because a shared vertex cannot
	/// carry three different barycentric corners at once. See Shaders.h.
	void UploadMesh();

	/// Saver time in seconds, for the current parameters and host state.
	float CurrentTime() const;

	const bool overInput;

	ffglex::FFGLShader sceneShader;
	ffglex::FFGLShader compositeShader;

	Target target;

	GLuint vertexArray  = 0;
	GLuint vertexBuffer = 0;
	GLuint indexBuffer  = 0;

	/// A core profile refuses to draw with no vertex array bound, even when the
	/// vertex shader sources nothing and builds its triangle from `gl_VertexID`.
	/// This is that array: created empty, bound to draw, never filled.
	GLuint emptyVAO = 0;

	/// Uniform locations for the two matrices. They go through raw
	/// `glUniformMatrix4fv` because `ffglex::FFGLShader::Set` has no matrix
	/// overload -- and, worse, no overload that a mat4 would fail to convert
	/// to, so a wrong call would compile.
	GLint uniformView = -1;
	GLint uniformProj = -1;

	float params[ PT_COUNT ] = {};

	/// The marquee / 3D Text string. A std::string rather than a fixed buffer
	/// because GetTextParameter hands the host a pointer it reads later, so it
	/// has to stay alive and stable.
	std::string text = "Idler";
	std::string aboutText;

	std::unique_ptr< Saver > saver;
	SaverKind saverKind = SaverKind::Count;///< Count = "none built yet"

	Scene scene;
	std::vector< float > uploadBuffer;
	std::vector< uint32_t > indexScratch;

	//---------------------------------------------------------------------
	// Host clock units.
	//
	// The FFGL header never says what unit SetTime is in, and hosts disagree:
	// Resolume hands over MILLISECONDS (measured live: 20.0 per frame at its
	// 50 fps, and the SDK's own Particles sample divides by 1000), while the
	// offline harness sends seconds. UpdateClock decides from the first
	// plausible frame delta and sticks: 0.001..0.5 is a seconds-host frame,
	// 2..500 is a milliseconds-host frame, anything else keeps waiting.
	// `hostSeconds` is the normalised clock CurrentTime reads.
	//---------------------------------------------------------------------
	void UpdateClock();

	double clockScale  = 0.0;///< 0 until decided; then 1.0 or 0.001
	double lastRawTime = -1.0;
	double hostSeconds = 0.0;

	//---------------------------------------------------------------------
	// Audio.
	//
	// The host writes one spectrum bin per element of PT_AUDIO; UpdateAudio
	// runs the band average through an attack/release filter into
	// `audioLevel`. The smoothing lives here and not in a saver because a
	// saver is a pure function of its settings -- state would cost it that,
	// and everything the harness proves about it.
	//---------------------------------------------------------------------
	void UpdateAudio();

	float audioLevel  = 0.0f;
	double audioClock = -1.0;

	/// Saver time accumulated from the audio-driven part of the clock.
	///
	/// Kept separately from `hostSeconds` because it is an integration -- the
	/// only one in the plugin. Audio has no seek: there is no "what was the
	/// spectrum 40 seconds ago", so an audio-driven clock cannot be a pure
	/// function of time and cannot be scrubbed. That is why Audio Speed is a
	/// separate control from Speed rather than folded into it, and why it
	/// defaults to zero.
	double audioTime = 0.0;

	bool timePinned    = false;
	float pinnedTime   = 0.0f;
};

} // namespace idler
