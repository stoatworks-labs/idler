#include "Idler.h"

#include <string>
#include <chrono>
#include <algorithm>
#include <cmath>

#include "Diag.h"
#include "Shaders.h"
#include "StoatworksAboutParams.h"

namespace idler
{
// The buttons are declared one per link, so the count in Controls.h and the
// count the block actually has must agree. They diverge the day somebody
// writes a user guide, and this is what says so.
static_assert( PT_COUNT - PT_ABOUT == stoatworks::about::kParamCount,
               "Controls.h's About run no longer matches StoatworksAbout.h -- "
               "add or remove a PT_ABOUT_BUTTON_n to match" );

namespace
{
float Clamp01( float v )
{
	return std::min( 1.0f, std::max( 0.0f, v ) );
}

/// What the About parameter shows. Display only.
///
/// The shared line -- name, version, licence, maker -- plus the one fact that
/// is Idler's alone and belongs nowhere else. It had been hand-rolled here,
/// which is how it came to name `stoatworks.com`, a domain that is not ours.
std::string BuildAboutText()
{
	return stoatworks::about::textParam( 0 ) +
	       "  |  The Windows 95/98 screensavers, rebuilt. Not affiliated with Microsoft.";
}

/// Frames that must agree before the host's clock unit is settled.
constexpr int kClockVotes = 4;

/// Wall clock, to calibrate the host's against. Steady rather than system, so
/// nothing here moves if the machine's clock is corrected.
double wallSeconds()
{
	using namespace std::chrono;
	static const steady_clock::time_point start = steady_clock::now();
	return duration_cast< duration< double > >( steady_clock::now() - start ).count();
}
} // namespace

//---------------------------------------------------------------------------
// Construction and parameter declaration
//---------------------------------------------------------------------------
IdlerPlugin::IdlerPlugin( bool overInput ) :
	overInput( overInput )
{
	// The source has no input; the effect takes one.
	SetMinInputs( overInput ? 1 : 0 );
	SetMaxInputs( overInput ? 1 : 0 );

	aboutText = BuildAboutText();

	//-----------------------------------------------------------------------
	// Defaults.
	//
	// Set BEFORE the parameters are declared, because SetOptionParamInfo takes
	// the default as an argument and reads it from here.
	//
	// Every numeric default is a 0..1 host value that Controls.cpp maps to a
	// physical one -- see the note there on why a ranged parameter cannot carry
	// a ranged default.
	//
	// The defaults are Mystify's, because it has to be *something* and Mystify
	// is the one people picture. Every other saver has a preset that sets the
	// scene controls to what it actually wants; see Presets.h on why that is
	// closer to load bearing here than in the rest of the fleet.
	//-----------------------------------------------------------------------
	params[ PT_SAVER ]  = static_cast< float >( SaverKind::Mystify );
	params[ PT_PRESET ] = 0.0f;// Custom

	params[ PT_DENSITY ]    = 0.35f;
	params[ PT_COMPLEXITY ] = 0.2f;
	params[ PT_SIZE ]       = 0.5f;
	params[ PT_LENGTH ]     = 0.45f;
	params[ PT_LINE_WIDTH ] = 0.25f;
	params[ PT_VARIATION ]  = 0.5f;
	params[ PT_SHADING ]    = static_cast< float >( Shading::Flat );

	params[ PT_SYNC ]  = static_cast< float >( Sync::Free );
	params[ PT_SPEED ] = 0.5f;// exactly 1.0x
	params[ PT_PHASE ] = 0.0f;
	params[ PT_SEED ]  = 0.0f;// seed 1

	params[ PT_FOV ]          = 0.4f;// 60 degrees
	params[ PT_CAM_DISTANCE ] = 0.5f;
	params[ PT_CAM_TILT ]     = 0.5f;// exactly level
	params[ PT_FOG ]          = 0.0f;

	params[ PT_COLOUR_MODE ]  = static_cast< float >( ColourMode::Classic );
	params[ PT_COLOUR_R ]     = 1.0f;
	params[ PT_COLOUR_G ]     = 1.0f;
	params[ PT_COLOUR_B ]     = 1.0f;
	params[ PT_HUE_SPREAD ]   = 0.3f;
	params[ PT_HUE_CYCLE ]    = 0.5f;// exactly 0
	params[ PT_OPACITY ]      = 1.0f;
	params[ PT_BACK_R ]       = 0.0f;
	params[ PT_BACK_G ]       = 0.0f;
	params[ PT_BACK_B ]       = 0.0f;
	// The SOURCE wants opaque black -- that is what makes it usable as a luma
	// mask on a layer above. The EFFECT wants the opposite: it exists to draw
	// the saver over the clip, and an opaque background covers the clip
	// completely, which makes Over look like the effect replaced the clip and
	// makes Reveal, Hide and Colourise no-ops (scene alpha is 1 everywhere, so
	// there is nothing for them to cut against). Found porting to OFX.
	params[ PT_BACK_OPACITY ] = overInput ? 0.0f : 1.0f;

	params[ PT_MASK_MODE ] = static_cast< float >( MaskMode::Over );
	params[ PT_MIX ]       = 1.0f;

	params[ PT_AUDIO_SIZE ]  = 0.0f;
	params[ PT_AUDIO_SPEED ] = 0.0f;

	//-----------------------------------------------------------------------
	// Declaration. This order is the order the host shows them in.
	//-----------------------------------------------------------------------
	SetOptionParamInfo( PT_SAVER, "Saver", static_cast< int >( SaverKind::Count ), params[ PT_SAVER ] );
	for( unsigned int i = 0; i < static_cast< unsigned int >( SaverKind::Count ); ++i )
		SetParamElementInfo( PT_SAVER, i, SaverName( static_cast< SaverKind >( i ) ), static_cast< float >( i ) );

	// Factory presets. Element 0 is Custom; picking anything else copies that
	// preset's values into the covered parameters and raises value events so
	// the host re-reads the sliders. Editing a covered slider flips back to
	// Custom. Declared next to the Saver dropdown, and not tucked at the end
	// like the rest of the fleet does, because here it is how you get a saver
	// set up the way it actually ran.
	SetOptionParamInfo( PT_PRESET, "Preset", 1 + presets::kCount, params[ PT_PRESET ] );
	SetParamElementInfo( PT_PRESET, 0, "Custom", 0.0f );
	for( int i = 0; i < presets::kCount; ++i )
		SetParamElementInfo( PT_PRESET, 1 + i, presets::kPresets[ i ].name, static_cast< float >( 1 + i ) );

	SetParamInfof( PT_DENSITY, "Density", FF_TYPE_STANDARD );
	SetParamInfof( PT_COMPLEXITY, "Complexity", FF_TYPE_STANDARD );
	SetParamInfof( PT_SIZE, "Size", FF_TYPE_STANDARD );
	SetParamInfof( PT_LENGTH, "Length", FF_TYPE_STANDARD );
	SetParamInfof( PT_LINE_WIDTH, "Line Width", FF_TYPE_STANDARD );
	SetParamInfof( PT_VARIATION, "Variation", FF_TYPE_STANDARD );

	SetOptionParamInfo( PT_SHADING, "Shading", static_cast< int >( Shading::Count ), params[ PT_SHADING ] );
	SetParamElementInfo( PT_SHADING, 0, "Flat", 0.0f );
	SetParamElementInfo( PT_SHADING, 1, "Lit", 1.0f );
	SetParamElementInfo( PT_SHADING, 2, "Wireframe", 2.0f );

	SetOptionParamInfo( PT_SYNC, "Sync", static_cast< int >( Sync::Count ), params[ PT_SYNC ] );
	for( unsigned int i = 0; i < static_cast< unsigned int >( Sync::Count ); ++i )
		SetParamElementInfo( PT_SYNC, i, SyncName( static_cast< Sync >( i ) ), static_cast< float >( i ) );

	SetParamInfof( PT_SPEED, "Speed", FF_TYPE_STANDARD );
	SetParamInfof( PT_PHASE, "Phase", FF_TYPE_STANDARD );
	SetParamInfof( PT_SEED, "Seed", FF_TYPE_STANDARD );

	SetParamInfof( PT_FOV, "Field of View", FF_TYPE_STANDARD );
	SetParamInfof( PT_CAM_DISTANCE, "Camera Distance", FF_TYPE_STANDARD );
	SetParamInfof( PT_CAM_TILT, "Camera Tilt", FF_TYPE_STANDARD );
	SetParamInfof( PT_FOG, "Fog", FF_TYPE_STANDARD );

	SetOptionParamInfo( PT_COLOUR_MODE, "Colour Mode", static_cast< int >( ColourMode::Count ), params[ PT_COLOUR_MODE ] );
	for( unsigned int i = 0; i < static_cast< unsigned int >( ColourMode::Count ); ++i )
		SetParamElementInfo( PT_COLOUR_MODE, i, ColourModeName( static_cast< ColourMode >( i ) ), static_cast< float >( i ) );

	// FF_TYPE_RED carries the swatch; the green and blue components are separate
	// parameters that the host groups behind it by type, which is why only the
	// red one gets a human name.
	SetParamInfof( PT_COLOUR_R, "Colour", FF_TYPE_RED );
	SetParamInfof( PT_COLOUR_G, "Colour_Green", FF_TYPE_GREEN );
	SetParamInfof( PT_COLOUR_B, "Colour_Blue", FF_TYPE_BLUE );
	SetParamInfof( PT_HUE_SPREAD, "Hue Spread", FF_TYPE_STANDARD );
	SetParamInfof( PT_HUE_CYCLE, "Hue Cycle", FF_TYPE_STANDARD );
	SetParamInfof( PT_OPACITY, "Opacity", FF_TYPE_STANDARD );

	SetParamInfof( PT_BACK_R, "Background", FF_TYPE_RED );
	SetParamInfof( PT_BACK_G, "Background_Green", FF_TYPE_GREEN );
	SetParamInfof( PT_BACK_B, "Background_Blue", FF_TYPE_BLUE );
	SetParamInfof( PT_BACK_OPACITY, "Background Opacity", FF_TYPE_STANDARD );

	SetParamInfo( PT_TEXT, "Text", FF_TYPE_TEXT, text.c_str() );

	SetOptionParamInfo( PT_MASK_MODE, "Mask Mode", static_cast< int >( MaskMode::Count ), params[ PT_MASK_MODE ] );
	for( unsigned int i = 0; i < static_cast< unsigned int >( MaskMode::Count ); ++i )
		SetParamElementInfo( PT_MASK_MODE, i, MaskModeName( static_cast< MaskMode >( i ) ), static_cast< float >( i ) );

	SetParamInfof( PT_MIX, "Mix", FF_TYPE_STANDARD );

	//-----------------------------------------------------------------------
	// Audio. PT_AUDIO is an FFT buffer: Resolume shows it as an audio-source
	// picker and writes one spectrum bin per element, low frequencies first.
	// The element defaults are zero on purpose -- with no audio routed the two
	// knobs do nothing, rather than the picture twitching to a phantom signal.
	//-----------------------------------------------------------------------
	SetBufferParamInfo( PT_AUDIO, "Audio", kAudioBins, FF_USAGE_FFT );
	for( unsigned int i = 0; i < kAudioBins; ++i )
		SetParamElementInfo( PT_AUDIO, i, "", 0.0f );

	SetParamInfof( PT_AUDIO_SIZE, "Audio Size", FF_TYPE_STANDARD );
	SetParamInfof( PT_AUDIO_SPEED, "Audio Speed", FF_TYPE_STANDARD );

	SetParamInfo( PT_ABOUT, "About", FF_TYPE_TEXT, aboutText.c_str() );
	{
		// Inline rather than through a helper: SetParamInfo is protected on
		// CFFGLPlugin, so nothing outside the class can call it.
		FFUInt32 aboutId = PT_ABOUT + 1;
		for( const auto& b : stoatworks::about::buttons() )
			SetParamInfo( aboutId++, b.label, FF_TYPE_EVENT, false );
	}

	//-----------------------------------------------------------------------
	// Groups. Thirty-odd parameters in one flat list is how somebody else's
	// inspector stops being readable. SetParamGroup collapses *runs* of
	// same-group parameters, which is why the ids in Controls.h have to stay in
	// this order.
	//-----------------------------------------------------------------------
	for( unsigned int id = PT_SAVER; id <= PT_PRESET; ++id )
		SetParamGroup( id, "Saver" );
	for( unsigned int id = PT_DENSITY; id <= PT_SHADING; ++id )
		SetParamGroup( id, "Scene" );
	for( unsigned int id = PT_SYNC; id <= PT_SEED; ++id )
		SetParamGroup( id, "Motion" );
	for( unsigned int id = PT_FOV; id <= PT_FOG; ++id )
		SetParamGroup( id, "Camera" );
	for( unsigned int id = PT_COLOUR_MODE; id <= PT_BACK_OPACITY; ++id )
		SetParamGroup( id, "Colour" );
	SetParamGroup( PT_TEXT, "Text" );
	for( unsigned int id = PT_MASK_MODE; id <= PT_MIX; ++id )
		SetParamGroup( id, "Output" );
	for( unsigned int id = PT_AUDIO; id <= PT_AUDIO_SPEED; ++id )
		SetParamGroup( id, "Audio" );
	for( unsigned int id = PT_ABOUT; id < PT_COUNT; ++id )
		SetParamGroup( id, "About" );
}

//---------------------------------------------------------------------------
// GL lifetime
//---------------------------------------------------------------------------
bool IdlerPlugin::BuildShaders()
{
	if( !sceneShader.Compile( SceneVertexShader(), SceneFragmentShader() ) )
	{
		diag::error( "scene shader would not compile" );
		return false;
	}

	if( !compositeShader.Compile( CompositeVertexShader(), CompositeFragmentShader( overInput ) ) )
	{
		diag::error( "composite shader would not compile" );
		return false;
	}

	// A uniform that does not resolve is a silent no-op -- glUniform on
	// location -1 is documented to do nothing -- and for these two the symptom
	// is a plugin that draws a blank frame, which is indistinguishable from a
	// saver that produced no geometry. Say so in the log rather than leaving it
	// to be guessed at.
	uniformView = sceneShader.FindUniform( "View" );
	uniformProj = sceneShader.FindUniform( "Proj" );
	if( uniformView < 0 || uniformProj < 0 )
		diag::error( "camera uniforms did not resolve -- nothing will be drawn" );

	return true;
}

FFResult IdlerPlugin::InitGL( const FFGLViewportStruct* vp )
{
	diag::init();

	const GLubyte* version  = glGetString( GL_VERSION );
	const GLubyte* renderer = glGetString( GL_RENDERER );
	diag::info( std::string( "InitGL, GL " ) +
	            ( version != nullptr ? reinterpret_cast< const char* >( version ) : "unknown" ) + " on " +
	            ( renderer != nullptr ? reinterpret_cast< const char* >( renderer ) : "unknown" ) );

	if( !BuildShaders() )
	{
		DeInitGL();
		return FF_FAIL;
	}

	glGenVertexArrays( 1, &emptyVAO );

	glGenVertexArrays( 1, &vertexArray );
	glGenBuffers( 1, &vertexBuffer );
	glGenBuffers( 1, &indexBuffer );

	glBindVertexArray( vertexArray );
	glBindBuffer( GL_ARRAY_BUFFER, vertexBuffer );
	glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, indexBuffer );

	const GLsizei stride = kFloatsPerVertex * static_cast< GLsizei >( sizeof( float ) );
	auto attribute       = []( GLuint location, GLint size, int offsetFloats, GLsizei stride ) {
        glEnableVertexAttribArray( location );
        glVertexAttribPointer( location, size, GL_FLOAT, GL_FALSE, stride,
                               reinterpret_cast< const void* >( static_cast< uintptr_t >( offsetFloats ) *
                                                                sizeof( float ) ) );
	};
	attribute( kAttrPosition, 3, 0, stride );
	attribute( kAttrNormal, 3, 3, stride );
	attribute( kAttrColour, 4, 6, stride );
	attribute( kAttrBarycentric, 3, 10, stride );

	// The element array binding is part of the VAO, so it must be left bound.
	// The array buffer binding is not, and unbinding it here would be harmless
	// -- left as-is so the state at the end of InitGL matches the state the
	// draw path expects.
	glBindVertexArray( 0 );
	glBindBuffer( GL_ARRAY_BUFFER, 0 );

	if( vp != nullptr )
		target.Ensure( static_cast< int >( vp->width ), static_cast< int >( vp->height ) );

	return FF_SUCCESS;
}

FFResult IdlerPlugin::DeInitGL()
{
	sceneShader.FreeGLResources();
	compositeShader.FreeGLResources();

	// While the host's context is still current. A destructor cannot do this --
	// see the note in Target.cpp.
	target.Release();

	if( vertexBuffer != 0 )
		glDeleteBuffers( 1, &vertexBuffer );
	if( indexBuffer != 0 )
		glDeleteBuffers( 1, &indexBuffer );
	if( vertexArray != 0 )
		glDeleteVertexArrays( 1, &vertexArray );
	if( emptyVAO != 0 )
		glDeleteVertexArrays( 1, &emptyVAO );

	vertexBuffer = indexBuffer = vertexArray = emptyVAO = 0;

	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
// Parameters
//---------------------------------------------------------------------------
FFResult IdlerPlugin::SetFloatParameter( unsigned int index, float value )
{
	if( index >= PT_COUNT )
		return FF_FAIL;

	// The About buttons open a browser and store nothing, so they are handled
	// before the params[] write below and before the preset bookkeeping --
	// pressing one is not the operator taking over from a preset.
	if( index > PT_ABOUT )
		return stoatworks::about::handleParam( index - PT_ABOUT, value ) ? FF_SUCCESS : FF_FAIL;

	if( index == PT_PRESET )
	{
		const int chosen = static_cast< int >( std::lround( value ) );
		if( chosen != static_cast< int >( std::lround( params[ PT_PRESET ] ) ) )
			applyPreset( chosen );
		return FF_SUCCESS;
	}

	// A slider moved while a preset is active means the operator has taken
	// over: the dropdown falls back to Custom. The equality guard matters --
	// hosts that honour the value events echo the preset's own values straight
	// back through here, and that echo must not un-set the preset.
	const float previous = params[ index ];
	params[ index ]      = value;

	const int active = static_cast< int >( std::lround( params[ PT_PRESET ] ) );
	if( active > 0 && std::fabs( value - previous ) > 1e-4f )
	{
		for( unsigned int id : kPresetParamIDs )
		{
			if( id == index )
			{
				params[ PT_PRESET ] = 0.0f;
				RaiseParamEvent( PT_PRESET, FF_EVENT_FLAG_VALUE );
				break;
			}
		}
	}

	return FF_SUCCESS;
}

void IdlerPlugin::applyPreset( int presetIndex )
{
	params[ PT_PRESET ] = static_cast< float >( presetIndex );

	if( presetIndex <= 0 || presetIndex > presets::kCount )
		return;// Custom: the sliders keep whatever they said

	const presets::Preset& preset = presets::kPresets[ presetIndex - 1 ];
	for( int j = 0; j < presets::kParamCount; ++j )
	{
		const unsigned int id = kPresetParamIDs[ j ];

		// On the effect, the background is the operator's compositing decision
		// and not the preset's -- exactly like Mask Mode and Mix, which presets
		// already leave alone for the same reason. Without this, picking any
		// preset re-covers the clip with opaque black.
		if( overInput
		    && ( id == PT_BACK_R || id == PT_BACK_G || id == PT_BACK_B || id == PT_BACK_OPACITY ) )
			continue;

		if( std::fabs( params[ id ] - preset.v[ j ] ) <= 1e-6f )
			continue;

		// The copy is what changes the picture; the event only tells the host
		// to re-read the slider. A host that ignores it renders the preset
		// correctly and merely shows stale knobs.
		params[ id ] = preset.v[ j ];
		RaiseParamEvent( id, FF_EVENT_FLAG_VALUE );
	}
}

float IdlerPlugin::GetFloatParameter( unsigned int index )
{
	return index < PT_COUNT ? params[ index ] : 0.0f;
}

char* IdlerPlugin::GetTextParameter( unsigned int index )
{
	// const_cast because the SDK's signature is non-const. The host reads the
	// pointer and does not write through it; both strings are members so they
	// outlive the call.
	if( index == PT_TEXT )
		return const_cast< char* >( text.c_str() );
	if( index == PT_ABOUT )
		return const_cast< char* >( aboutText.c_str() );
	return nullptr;
}

FFResult IdlerPlugin::SetTextParameter( unsigned int index, const char* value )
{
	if( index == PT_TEXT )
	{
		text = ( value != nullptr ) ? value : "";
		return FF_SUCCESS;
	}

	// PT_ABOUT is display only, and it MUST still return success. The SDK's
	// instantiateGL sets every parameter's default on a fresh instance and
	// deletes the instance if any set fails, so returning FF_FAIL here means no
	// real host can create the plugin -- while every harness in this repo,
	// which drives the class directly, carries on passing. This exact bug
	// shipped in three of the fleet's plugins before it was found.
	if( index == PT_ABOUT )
		return FF_SUCCESS;

	return FF_FAIL;
}

void IdlerPlugin::SetTimeOverride( float seconds )
{
	timePinned = true;
	pinnedTime = seconds;
}

void IdlerPlugin::InvalidateReplay()
{
	if( auto* growing = dynamic_cast< GrowingSaver* >( saver.get() ) )
		growing->Invalidate();
}

//---------------------------------------------------------------------------
// The clock
//---------------------------------------------------------------------------
void IdlerPlugin::UpdateClock()
{
	// FFGL never says what unit SetTime arrives in, and hosts disagree:
	// Resolume sends MILLISECONDS (measured live at 20.0 per frame at its
	// 50 fps, and the SDK's own Particles sample divides by 1000), while the
	// offline harness sends seconds. Reading it raw is a thousand times fast
	// on the one host that matters and exactly right on the one that gets
	// tested, which is how it stays hidden.
	//
	// This used to guess the unit from the magnitude of a single frame delta
	// and then lock. That had three holes: a delta between 0.5 and 2.0 decided
	// nothing, a burst of sub-0.5 ms frames at load -- a thumbnail render on a
	// quick GPU -- locked it to "seconds" for the rest of the session, and
	// while undecided it assumed seconds, which is precisely the millisecond
	// host's wrong answer.
	//
	// So measure instead of guessing. steady_clock says how much real time
	// passed, the host says how much host time passed, and the ratio names the
	// unit outright. Nothing plausible sits between 1 and 1000, so both bands
	// are wide and a frame fitting neither simply does not vote.
	const double wallNow = wallSeconds();
	if( wallStart < 0.0 )
		wallStart = wallNow;

	// Never read `hostTime` before the host has set it: CFFGLPlugin's
	// constructor initialises bpm and barPhase and leaves hostTime
	// uninitialised, so until SetTime lands it is whatever was in that memory.
	const double raw = hostTimeSeen ? hostTime : -1.0;

	if( clockScale == 0.0 && raw >= 0.0 && lastRawTime >= 0.0 && lastWallTime >= 0.0 )
	{
		const double hostDelta = raw - lastRawTime;
		const double wallDelta = wallNow - lastWallTime;

		// A paused host, a looping clip or a stalled frame tells us nothing.
		if( hostDelta > 0.0 && wallDelta >= 0.0005 )
		{
			const double ratio = hostDelta / wallDelta;
			if( ratio > 0.1 && ratio < 10.0 )
				++secondsVotes;
			else if( ratio > 100.0 && ratio < 10000.0 )
				++millisVotes;

			// Several frames rather than one, so a single odd frame -- the
			// first after a seek, say -- cannot decide it on its own.
			if( secondsVotes >= kClockVotes || millisVotes >= kClockVotes )
			{
				clockScale = millisVotes > secondsVotes ? 0.001 : 1.0;
				diag::info( std::string( "host clock is " )
				            + ( clockScale == 0.001 ? "milliseconds" : "seconds" )
				            + ", scale=" + std::to_string( clockScale ) );
			}
		}
	}

	if( raw >= 0.0 )
		lastRawTime = raw;
	lastWallTime = wallNow;

	// Until the unit is settled -- and for a host that never calls SetTime at
	// all -- run on the real clock. Wrong in origin but right in rate, where
	// assuming seconds would be a thousand times fast on Resolume.
	hostSeconds = ( raw >= 0.0 && clockScale != 0.0 ) ? raw * clockScale : wallNow - wallStart;
}

FFResult IdlerPlugin::SetTime( double time )
{
	hostTimeSeen = true;
	return CFFGLPlugin::SetTime( time );
}

void IdlerPlugin::SetClockScaleForTest( double scale )
{
	clockScale = scale;
}

void IdlerPlugin::TickClockForTest()
{
	UpdateClock();
	UpdatePhaseAnchor();
}

double IdlerPlugin::ClockScaleForTest() const
{
	return clockScale;
}

double IdlerPlugin::HostSecondsForTest() const
{
	return hostSeconds;
}

float IdlerPlugin::CurrentTimeForTest() const
{
	return CurrentTime();
}

//---------------------------------------------------------------------------
void IdlerPlugin::UpdatePhaseAnchor()
{
	const Sync sync   = static_cast< Sync >( Option( params[ PT_SYNC ], static_cast< int >( Sync::Count ) ) );
	const float speed = SpeedFromParam( params[ PT_SPEED ] );

	// Beat and Bar are meant to jump -- they re-lock to the transport, which is
	// the point of them. Manual ignores Speed entirely, and a pinned clock has
	// replaced the host's. Keep the anchor following the clock through all
	// three so that returning to Free resumes rather than leaps.
	if( sync != Sync::Free || timePinned )
	{
		anchorClock = hostSeconds;
		anchorSpeed = speed;
		return;
	}

	// First frame: leave the anchor at clock zero, time zero. That makes the
	// expression in CurrentTime identical to the old `hostSeconds * speed` for
	// as long as nobody touches Speed, which is what keeps every rendered-frame
	// test and tools/sweep.py measuring the same thing they measured before.
	if( anchorSpeed < 0.0f )
	{
		anchorSpeed = speed;
		return;
	}

	if( speed != anchorSpeed )
	{
		// Once per speed change, not once per frame: this carries the exact time
		// forward rather than integrating it, so a long session cannot
		// accumulate rounding into a drift. Frame rate still cannot affect what
		// is on screen.
		phaseAnchor += ( hostSeconds - anchorClock ) * anchorSpeed;
		anchorClock = hostSeconds;
		anchorSpeed = speed;
	}
}

void IdlerPlugin::UpdateAudio()
{
	// Frame delta for the release filter, off the normalised clock UpdateClock
	// just advanced, so the ms-vs-seconds question is already settled. First
	// frame -- or a clock that has not moved -- snaps instead.
	const double now = hostSeconds;
	const double dt  = ( audioClock >= 0.0 && now > audioClock ) ? now - audioClock : 0.0;
	audioClock       = now;

	// The host writes the spectrum into the parameter's own elements; there is
	// no setter to override for a buffer parameter, so this is where it is read
	// from.
	const ParamInfo* info = FindParamInfo( PT_AUDIO );
	if( info == nullptr )
		return;

	float sum = 0.0f;
	for( const auto& element : info->elements )
	{
		// sqrt because bin magnitudes bunch near zero: a spectrum used raw
		// leaves everything but the kick invisible.
		sum += std::sqrt( std::max( 0.0f, element.value ) );
	}
	const float raw = info->elements.empty()
	                    ? 0.0f
	                    : std::min( 1.0f, sum / static_cast< float >( info->elements.size() ) * 2.0f );

	// Fast up, slow down -- the same asymmetry as the rest of the fleet and for
	// the same reason: a flash that arrives a frame late reads as broken, while
	// one that takes ~150 ms to die away reads as intended.
	const float release = dt > 0.0 ? 1.0f - std::exp( static_cast< float >( -dt / 0.15 ) ) : 1.0f;

	if( raw >= audioLevel )
		audioLevel = raw;
	else
		audioLevel += ( raw - audioLevel ) * release;

	// The one integration in the plugin, and the reason Audio Speed is its own
	// control rather than folded into Speed: there is no "what was the spectrum
	// forty seconds ago", so an audio-driven clock cannot be scrubbed and
	// cannot be a pure function of time. Everything else here can.
	audioTime += static_cast< double >( audioLevel ) * dt;
}

float IdlerPlugin::CurrentTime() const
{
	const Sync sync   = static_cast< Sync >( Option( params[ PT_SYNC ], static_cast< int >( Sync::Count ) ) );
	const float speed = SpeedFromParam( params[ PT_SPEED ] );
	const float manual = PhaseFromParam( params[ PT_PHASE ] );
	const float audio  = Clamp01( params[ PT_AUDIO_SPEED ] ) * static_cast< float >( audioTime ) * 4.0f;

	// Pinning replaces the CLOCK, not the whole time. The Phase slider stays
	// live underneath it, which is what lets tools/sweep.py prove that slider
	// is connected -- a pin that swallowed it too would make it look dead.
	if( timePinned )
		return pinnedTime + manual;

	float driven = 0.0f;

	switch( sync )
	{
	case Sync::Free:
		// Not `hostSeconds * speed`: see UpdatePhaseAnchor. Until the operator
		// has moved Speed this is exactly that product, because the anchor
		// starts at clock zero with time zero.
		driven = static_cast< float >( phaseAnchor + ( hostSeconds - anchorClock ) * speed );
		break;

	case Sync::Beat:
	case Sync::Bar:
	{
		//-------------------------------------------------------------------
		// The host hands us a tempo and a position *within* the current bar,
		// and never says which bar it is. A bar counter would be state, and
		// state is the thing this plugin does not have.
		//
		// So recover a continuous bar number without keeping one: the clock
		// gives an estimate of how many bars have passed, `barPhase` gives the
		// exact position inside the bar, and the whole number that reconciles
		// them is `round( estimate - barPhase )`. The result is continuous
		// across the bar line -- as `barPhase` wraps from 1 to 0 the rounded
		// integer steps up by one at the same moment -- and it stays exact even
		// if the clock estimate is off by up to half a bar.
		//
		// It can name the wrong absolute bar if the host's transport did not
		// start at time zero. That is invisible for the pure savers, whose
		// animation repeats. It is NOT invisible for the two growing ones: an
		// integer bar offset means the pipe network starts part-grown. That is
		// a defensible reading of "locked to the transport" and it is written
		// down in AGENTS.md rather than worked around, because the alternative
		// is keeping a bar counter, which is state.
		//-------------------------------------------------------------------
		const float tempo      = bpm > 1.0f ? bpm : 120.0f;
		const float barSeconds = 240.0f / tempo;// four beats to the bar
		const float estimate   = static_cast< float >( hostSeconds ) / barSeconds;
		const float within     = Clamp01( barPhase );

		const float bars = within + std::round( estimate - within );

		driven = ( sync == Sync::Beat ? bars * 4.0f : bars ) * speed;
		break;
	}

	case Sync::Manual:
	default:
		// Speed is deliberately ignored. This is the mode for driving Phase
		// from Resolume's own BPM-synced animation, or from a keyframe, or from
		// a MIDI fader -- and a second clock underneath it would fight whatever
		// is doing the driving.
		driven = 0.0f;
		break;
	}

	return driven + manual + audio;
}

//---------------------------------------------------------------------------
// Parameters to settings
//---------------------------------------------------------------------------
Settings IdlerPlugin::CurrentSettings( int width, int height ) const
{
	return SettingsFromParams( params, width, height, CurrentTime(), text.c_str(), audioLevel );
}

void IdlerPlugin::EnsureSaver()
{
	const SaverKind wanted =
		static_cast< SaverKind >( Option( params[ PT_SAVER ], static_cast< int >( SaverKind::Count ) ) );

	if( saver && saverKind == wanted )
		return;

	saver     = MakeSaver( wanted );
	saverKind = wanted;
}

//---------------------------------------------------------------------------
// Rendering
//---------------------------------------------------------------------------
void IdlerPlugin::UploadMesh()
{
	const Mesh& mesh      = scene.mesh;
	const bool deIndex    = ( scene.shading == Shading::Wireframe );
	const size_t triangles = mesh.TriangleCount();

	uploadBuffer.clear();
	indexScratch.clear();

	auto push = [ & ]( const Vertex& v, float b0, float b1, float b2 ) {
		uploadBuffer.push_back( v.position.x );
		uploadBuffer.push_back( v.position.y );
		uploadBuffer.push_back( v.position.z );
		uploadBuffer.push_back( v.normal.x );
		uploadBuffer.push_back( v.normal.y );
		uploadBuffer.push_back( v.normal.z );
		uploadBuffer.push_back( v.colour.x );
		uploadBuffer.push_back( v.colour.y );
		uploadBuffer.push_back( v.colour.z );
		uploadBuffer.push_back( v.colour.w );
		uploadBuffer.push_back( b0 );
		uploadBuffer.push_back( b1 );
		uploadBuffer.push_back( b2 );
	};

	if( deIndex )
	{
		// A shared vertex cannot carry three different barycentric corners at
		// once, so wireframe pays for its own de-indexing. Only wireframe does.
		uploadBuffer.reserve( triangles * 3 * kFloatsPerVertex );
		indexScratch.reserve( triangles * 3 );

		for( size_t t = 0; t + 2 < mesh.indices.size(); t += 3 )
		{
			push( mesh.vertices[ mesh.indices[ t ] ], 1.0f, 0.0f, 0.0f );
			push( mesh.vertices[ mesh.indices[ t + 1 ] ], 0.0f, 1.0f, 0.0f );
			push( mesh.vertices[ mesh.indices[ t + 2 ] ], 0.0f, 0.0f, 1.0f );

			const uint32_t base = static_cast< uint32_t >( t );
			indexScratch.push_back( base );
			indexScratch.push_back( base + 1 );
			indexScratch.push_back( base + 2 );
		}
	}
	else
	{
		uploadBuffer.reserve( mesh.vertices.size() * kFloatsPerVertex );
		for( const Vertex& v : mesh.vertices )
			push( v, 1.0f, 1.0f, 1.0f );
		indexScratch = mesh.indices;
	}

	glBindBuffer( GL_ARRAY_BUFFER, vertexBuffer );
	glBufferData( GL_ARRAY_BUFFER,
	              static_cast< GLsizeiptr >( uploadBuffer.size() * sizeof( float ) ),
	              uploadBuffer.data(), GL_STREAM_DRAW );

	glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, indexBuffer );
	glBufferData( GL_ELEMENT_ARRAY_BUFFER,
	              static_cast< GLsizeiptr >( indexScratch.size() * sizeof( uint32_t ) ),
	              indexScratch.data(), GL_STREAM_DRAW );
}

void IdlerPlugin::Render( int width, int height, GLuint inputTexture, float maxU, float maxV, GLuint hostFBO )
{
	if( width <= 0 || height <= 0 )
		return;

	EnsureSaver();

	// The clock and the audio filter are advanced HERE rather than in
	// ProcessOpenGL, so that the offline harness -- which calls Render directly
	// and never goes through plugMain -- exercises them too. It did not, at
	// first, and the consequence was that both Audio controls were dead in
	// every offline test while working perfectly in a host: the harness was
	// setting the spectrum and then rendering with a filter that had never
	// been run. tools/sweep.py is what found it.
	UpdateClock();
	UpdatePhaseAnchor();
	UpdateAudio();

	const Settings settings = CurrentSettings( width, height );

	scene.Clear();
	scene.background = settings.background;
	if( saver )
		saver->Build( settings, scene );

	if( !target.Ensure( width, height ) )
		return;

	//-----------------------------------------------------------------------
	// Pass one: the scene, into our own target.
	//-----------------------------------------------------------------------
	target.Bind();

	glClearColor( scene.background.x, scene.background.y, scene.background.z, scene.background.w );
	glClearDepth( 1.0 );
	glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

	if( scene.depthTest )
	{
		glEnable( GL_DEPTH_TEST );
		glDepthFunc( GL_LESS );
	}
	else
	{
		glDisable( GL_DEPTH_TEST );
	}

	// Culling stays OFF. Several savers are deliberately single-sided surfaces
	// seen from both sides -- the Flying Windows logo, a FlowerBox mid-morph
	// where the surface passes through itself -- and the fragment shader already
	// flips the normal for a back face. Culling would make those disappear from
	// one side, which reads as geometry randomly vanishing.
	glDisable( GL_CULL_FACE );

	glEnable( GL_BLEND );
	glBlendEquation( GL_FUNC_ADD );
	glBlendFunc( GL_ONE, GL_ONE_MINUS_SRC_ALPHA );

	if( !scene.mesh.Empty() )
	{
		UploadMesh();

		// Plain glUseProgram rather than a ffglex scoped binding: every
		// Scoped* here CLEARS its binding to zero on scope exit instead of
		// restoring it, so the state is put back by hand at the end instead.
		glUseProgram( sceneShader.GetGLID() );
		glUniformMatrix4fv( uniformView, 1, GL_FALSE, scene.view.m );
		glUniformMatrix4fv( uniformProj, 1, GL_FALSE, scene.proj.m );

		sceneShader.Set( "ShadingMode", static_cast< int >( scene.shading ) );
		sceneShader.Set( "LightDirection", scene.lightDirection.x, scene.lightDirection.y, scene.lightDirection.z );
		sceneShader.Set( "Ambient", scene.ambient );
		sceneShader.Set( "FogRange", scene.fogStart, scene.fogEnd );
		lastEdgeWidth = EdgeWidthPixels( settings.lineWidth, width, height );
		sceneShader.Set( "EdgeWidth", lastEdgeWidth );

		glBindVertexArray( vertexArray );
		glDrawElements( GL_TRIANGLES, static_cast< GLsizei >( indexScratch.size() ), GL_UNSIGNED_INT, nullptr );
		glBindVertexArray( 0 );
	}

	//-----------------------------------------------------------------------
	// Pass two: the target onto the output.
	//
	// The host's framebuffer and viewport both have to be put back by hand.
	// ScopedFBOBinding restores the binding and NOT the viewport, so a pass
	// that sized the viewport to an off-screen buffer leaks that size into the
	// final pass -- and the symptom does not look like a viewport bug. The
	// effect renders correctly into a CORNER of the frame and leaves the rest
	// untouched, which in any viewer that shows transparency as white reads as
	// the plugin having blown out to solid white.
	//-----------------------------------------------------------------------
	glBindFramebuffer( GL_FRAMEBUFFER, hostFBO );
	glViewport( 0, 0, width, height );

	glDisable( GL_DEPTH_TEST );
	glEnable( GL_BLEND );
	glBlendEquation( GL_FUNC_ADD );
	glBlendFunc( GL_ONE, GL_ONE_MINUS_SRC_ALPHA );

	glUseProgram( compositeShader.GetGLID() );

	glActiveTexture( GL_TEXTURE0 );
	glBindTexture( GL_TEXTURE_2D, target.ColourTexture() );
	compositeShader.Set( "SceneTexture", 0 );

	if( overInput )
	{
		glActiveTexture( GL_TEXTURE1 );
		glBindTexture( GL_TEXTURE_2D, inputTexture );
		compositeShader.Set( "InputTexture", 1 );
		compositeShader.Set( "MaxUV", maxU, maxV );
	}

	compositeShader.Set( "MaskMode", Option( params[ PT_MASK_MODE ], static_cast< int >( MaskMode::Count ) ) );
	compositeShader.Set( "MixAmount", Clamp01( params[ PT_MIX ] ) );

	glBindVertexArray( emptyVAO );
	glDrawArrays( GL_TRIANGLES, 0, 3 );
	glBindVertexArray( 0 );

	// Put the state back by hand. Every ffglex::Scoped* binding CLEARS to zero
	// on scope exit rather than restoring, so using them here would hand the
	// host a cleared texture unit rather than the one it had.
	glActiveTexture( GL_TEXTURE1 );
	glBindTexture( GL_TEXTURE_2D, 0 );
	glActiveTexture( GL_TEXTURE0 );
	glBindTexture( GL_TEXTURE_2D, 0 );
	glUseProgram( 0 );
}

FFResult IdlerPlugin::ProcessOpenGL( ProcessOpenGLStruct* pGL )
{
	if( overInput && ( pGL == nullptr || pGL->numInputTextures < 1 || pGL->inputTextures[ 0 ] == nullptr ) )
		return FF_FAIL;

	// The host viewport is the only statement anywhere of how big the output
	// is: FFGL gives a source no other size information.
	GLint viewport[ 4 ] = { 0, 0, 0, 0 };
	glGetIntegerv( GL_VIEWPORT, viewport );

	int width  = viewport[ 2 ];
	int height = viewport[ 3 ];

	GLuint inputTexture = 0;
	float maxU = 1.0f, maxV = 1.0f;

	if( overInput )
	{
		const FFGLTextureStruct& texture = *pGL->inputTextures[ 0 ];
		inputTexture                     = texture.Handle;

		// The input texture can be BIGGER than the picture -- the host hands
		// over a pooled or power-of-two texture and says how much of it was
		// really drawn.
		const FFGLTexCoords coords = GetMaxGLTexCoords( texture );
		maxU                               = coords.s;
		maxV                               = coords.t;

		if( width <= 0 || height <= 0 )
		{
			width  = static_cast< int >( texture.Width );
			height = static_cast< int >( texture.Height );
		}
	}

	if( width <= 0 || height <= 0 )
		return FF_FAIL;

	Render( width, height, inputTexture, maxU, maxV, pGL->HostFBO );

	return FF_SUCCESS;
}

} // namespace idler
