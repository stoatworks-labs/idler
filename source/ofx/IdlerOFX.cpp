/**
    Idler as OpenFX — a generator and a matching filter, in one bundle.

    Both plugins come out of the same core as the FFGL build: `Savers.cpp`
    builds a `Scene`, `Raster.cpp` turns it into pixels, `Controls.cpp` maps the
    parameters and `Presets.h` is the same table. Nothing about a screensaver is
    implemented twice, which is the whole reason `Scene` exists.

    ## What differs from the FFGL build, and why

    **Sync offers Free and Manual only.** OFX carries no tempo: there is no
    equivalent of Resolume's beat or bar position, and inventing one from the
    frame number would be a different feature wearing the same name. Manual is
    the mode that matters here anyway — Phase is a keyframable parameter, so a
    pipe network can be grown against the edit.

    **There is no audio.** OFX has no route from the host's audio to a video
    plugin, so the audio controls are absent rather than present and dead.

    **The render is single-threaded per instance.** The two growing savers keep
    a replay cache, and a cache shared between concurrent renders of the same
    instance would be a race that shows up as a pipe network flickering between
    two lengths. `eRenderInstanceSafe` is the honest setting; the compositing
    pass is still multi-threaded, because that is where the pixels are.

    ## The trap this file was copied into

    `cmake/InfoOFX.plist.in` is parameterised on `@PROJECT_NAME@`. The version
    that circulated in this fleet had the previous plugin's name hardcoded in
    `CFBundleExecutable`, which builds, loads and renders perfectly and then
    fails `codesign` at release time with a message that never mentions the
    plist. `tools/verify.sh` checks it locally.
*/
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "ofxsImageEffect.h"
#include "ofxsMultiThread.h"
#include "ofxsProcessing.h"

// After the OFX Support headers, which is where the OFX types come from.
#include "StoatworksAboutOFX.h"

#include "../Controls.h"
#include "../Presets.h"
#include "../Savers.h"
#include "../Scene.h"
#include "Raster.h"

namespace
{

const char* kSourceIdentifier = "com.stoatworks.idler";
const char* kMaskIdentifier   = "com.stoatworks.idlermask";

/// The rasterised picture, premultiplied, top row first — and the parameters
/// the compositing pass still needs. Built once per render, then read by every
/// worker thread.
struct Frame
{
	const float* rgba = nullptr;
	int width         = 0;
	int height        = 0;
	idler::MaskMode maskMode = idler::MaskMode::Over;
	float mix                = 1.0f;
	bool hasInput            = false;
};

class CompositeBase : public OFX::ImageProcessor
{
public:
	explicit CompositeBase( OFX::ImageEffect& effect ) : OFX::ImageProcessor( effect ) {}

	void setFrame( OFX::Image* source, const Frame* value )
	{
		srcImg = source;
		frame  = value;
	}

protected:
	OFX::Image* srcImg  = nullptr;
	const Frame* frame  = nullptr;
};

/**
    The compositor, mirroring CompositeFragmentShader.

    Every branch here is the same arithmetic as the GLSL, in the same order,
    including the guard on dividing by the scene's alpha in Colourise. The
    picture is premultiplied on both sides.
*/
template< class PIX, int nComponents, int maxValue >
class Composite : public CompositeBase
{
public:
	explicit Composite( OFX::ImageEffect& effect ) : CompositeBase( effect ) {}

	void multiThreadProcessImages( OfxRectI window ) override
	{
		const OfxRectI bounds = _dstImg->getBounds();
		const Frame& f        = *frame;
		const float mix       = f.mix;

		for( int y = window.y1; y < window.y2; ++y )
		{
			if( _effect.abort() )
				break;

			PIX* dstPix = static_cast< PIX* >( _dstImg->getPixelAddress( window.x1, y ) );
			if( dstPix == nullptr )
				continue;

			// The raster buffer's first row is the top of the picture; OFX rows
			// run bottom-up. Getting this wrong produces a vertically mirrored
			// picture that looks entirely plausible on the savers that happen to
			// be symmetrical, which is most of them.
			const int row = f.height - 1 - ( y - bounds.y1 );
			if( row < 0 || row >= f.height )
				continue;

			for( int x = window.x1; x < window.x2; ++x, dstPix += nComponents )
			{
				const int column = x - bounds.x1;
				if( column < 0 || column >= f.width )
					continue;

				const float* scene = f.rgba + ( static_cast< size_t >( row ) * f.width + column ) * 4;

				float clip[ 4 ] = { 0.0f, 0.0f, 0.0f, 0.0f };
				if( f.hasInput && srcImg != nullptr )
				{
					const PIX* srcPix = static_cast< const PIX* >( srcImg->getPixelAddress( x, y ) );
					if( srcPix != nullptr )
						for( int c = 0; c < nComponents && c < 4; ++c )
							clip[ c ] = static_cast< float >( srcPix[ c ] ) / maxValue;
				}

				float result[ 4 ];

				if( !f.hasInput )
				{
					for( int c = 0; c < 4; ++c )
						result[ c ] = scene[ c ] * mix;
				}
				else
				{
					float composed[ 4 ];
					switch( f.maskMode )
					{
						case idler::MaskMode::Reveal:
							for( int c = 0; c < 4; ++c )
								composed[ c ] = clip[ c ] * scene[ 3 ];
							break;

						case idler::MaskMode::Hide:
							for( int c = 0; c < 4; ++c )
								composed[ c ] = clip[ c ] * ( 1.0f - scene[ 3 ] );
							break;

						case idler::MaskMode::Colourise:
						{
							// Un-premultiply to get the saver's colour, which is
							// what tints the clip. Guarded, because dividing a
							// premultiplied black by its own zero alpha is how a
							// mask mode comes to output NaN.
							const float alpha = scene[ 3 ];
							const float scale = alpha > 0.0001f ? 1.0f / alpha : 0.0f;
							for( int c = 0; c < 3; ++c )
								composed[ c ] = clip[ c ] * scene[ c ] * scale * alpha;
							composed[ 3 ] = clip[ 3 ] * alpha;
							break;
						}

						case idler::MaskMode::Over:
						default:
							for( int c = 0; c < 4; ++c )
								composed[ c ] = scene[ c ] + clip[ c ] * ( 1.0f - scene[ 3 ] );
							break;
					}

					for( int c = 0; c < 4; ++c )
						result[ c ] = clip[ c ] + ( composed[ c ] - clip[ c ] ) * mix;
				}

				for( int c = 0; c < nComponents && c < 4; ++c )
				{
					float v = result[ c ];
					if( maxValue != 1 )
						v = std::min( std::max( v, 0.0f ), 1.0f );
					dstPix[ c ] = static_cast< PIX >( v * maxValue + ( maxValue != 1 ? 0.5f : 0.0f ) );
				}
			}
		}
	}
};

//---------------------------------------------------------------------------
// The plugin
//---------------------------------------------------------------------------

class IdlerOFXPlugin : public OFX::ImageEffect
{
public:
	IdlerOFXPlugin( OfxImageEffectHandle handle, bool maskVariantValue ) :
		OFX::ImageEffect( handle ), maskVariant( maskVariantValue )
	{
		dstClip = fetchClip( kOfxImageEffectOutputClipName );
		if( maskVariant )
			srcClip = fetchClip( kOfxImageEffectSimpleSourceClipName );

		saverParam   = fetchChoiceParam( "saver" );
		presetParam  = fetchChoiceParam( "preset" );
		density      = fetchDoubleParam( "density" );
		complexity   = fetchDoubleParam( "complexity" );
		sizeParam    = fetchDoubleParam( "size" );
		lengthParam  = fetchDoubleParam( "length" );
		lineWidth    = fetchDoubleParam( "lineWidth" );
		variation    = fetchDoubleParam( "variation" );
		shading      = fetchChoiceParam( "shading" );
		syncParam    = fetchChoiceParam( "sync" );
		speed        = fetchDoubleParam( "speed" );
		phase        = fetchDoubleParam( "phase" );
		seed         = fetchDoubleParam( "seed" );
		fov          = fetchDoubleParam( "fov" );
		camDistance  = fetchDoubleParam( "camDistance" );
		camTilt      = fetchDoubleParam( "camTilt" );
		fog          = fetchDoubleParam( "fog" );
		colourMode   = fetchChoiceParam( "colourMode" );
		colour       = fetchRGBParam( "colour" );
		hueSpread    = fetchDoubleParam( "hueSpread" );
		hueCycle     = fetchDoubleParam( "hueCycle" );
		opacity      = fetchDoubleParam( "opacity" );
		background   = fetchRGBParam( "background" );
		backOpacity  = fetchDoubleParam( "backOpacity" );
		textParam    = fetchStringParam( "text" );
		mix          = fetchDoubleParam( "mix" );
		if( maskVariant )
			maskMode = fetchChoiceParam( "maskMode" );
	}

	void render( const OFX::RenderArguments& args ) override;
	void changedParam( const OFX::InstanceChangedArgs& args, const std::string& name ) override;

private:
	/// Every parameter as the 0..1 (or option index) array the shared mapping
	/// expects, so the OFX build cannot develop its own idea of what a control
	/// means.
	void readParams( double time, float* out ) const;
	void applyPreset( int presetIndex, double time );

	bool maskVariant = false;

	OFX::Clip* dstClip = nullptr;
	OFX::Clip* srcClip = nullptr;

	OFX::ChoiceParam* saverParam  = nullptr;
	OFX::ChoiceParam* presetParam = nullptr;
	OFX::DoubleParam* density     = nullptr;
	OFX::DoubleParam* complexity  = nullptr;
	OFX::DoubleParam* sizeParam   = nullptr;
	OFX::DoubleParam* lengthParam = nullptr;
	OFX::DoubleParam* lineWidth   = nullptr;
	OFX::DoubleParam* variation   = nullptr;
	OFX::ChoiceParam* shading     = nullptr;
	OFX::ChoiceParam* syncParam   = nullptr;
	OFX::DoubleParam* speed       = nullptr;
	OFX::DoubleParam* phase       = nullptr;
	OFX::DoubleParam* seed        = nullptr;
	OFX::DoubleParam* fov         = nullptr;
	OFX::DoubleParam* camDistance = nullptr;
	OFX::DoubleParam* camTilt     = nullptr;
	OFX::DoubleParam* fog         = nullptr;
	OFX::ChoiceParam* colourMode  = nullptr;
	OFX::RGBParam* colour         = nullptr;
	OFX::DoubleParam* hueSpread   = nullptr;
	OFX::DoubleParam* hueCycle    = nullptr;
	OFX::DoubleParam* opacity     = nullptr;
	OFX::RGBParam* background     = nullptr;
	OFX::DoubleParam* backOpacity = nullptr;
	OFX::StringParam* textParam   = nullptr;
	OFX::ChoiceParam* maskMode    = nullptr;
	OFX::DoubleParam* mix         = nullptr;

	std::unique_ptr< idler::Saver > saver;
	idler::SaverKind saverKind = idler::SaverKind::Count;
	idler::Scene scene;
	std::vector< float > pixels;
};

void IdlerOFXPlugin::readParams( double time, float* out ) const
{
	using namespace idler;

	for( int i = 0; i < PT_COUNT; ++i )
		out[ i ] = 0.0f;

	// ChoiceParam offers only the out-parameter form -- unlike DoubleParam,
	// there is no overload that returns the value.
	int choice = 0;
	saverParam->getValueAtTime( time, choice );
	out[ PT_SAVER ] = static_cast< float >( choice );
	presetParam->getValueAtTime( time, choice );
	out[ PT_PRESET ] = static_cast< float >( choice );
	out[ PT_DENSITY ] = static_cast< float >( density->getValueAtTime( time ) );
	out[ PT_COMPLEXITY ] = static_cast< float >( complexity->getValueAtTime( time ) );
	out[ PT_SIZE ]       = static_cast< float >( sizeParam->getValueAtTime( time ) );
	out[ PT_LENGTH ]     = static_cast< float >( lengthParam->getValueAtTime( time ) );
	out[ PT_LINE_WIDTH ] = static_cast< float >( lineWidth->getValueAtTime( time ) );
	out[ PT_VARIATION ]  = static_cast< float >( variation->getValueAtTime( time ) );
	shading->getValueAtTime( time, choice );
	out[ PT_SHADING ] = static_cast< float >( choice );
	syncParam->getValueAtTime( time, choice );
	out[ PT_SYNC ] = static_cast< float >( choice );
	out[ PT_SPEED ]      = static_cast< float >( speed->getValueAtTime( time ) );
	out[ PT_PHASE ]      = static_cast< float >( phase->getValueAtTime( time ) );
	out[ PT_SEED ]       = static_cast< float >( seed->getValueAtTime( time ) );
	out[ PT_FOV ]        = static_cast< float >( fov->getValueAtTime( time ) );
	out[ PT_CAM_DISTANCE ] = static_cast< float >( camDistance->getValueAtTime( time ) );
	out[ PT_CAM_TILT ]     = static_cast< float >( camTilt->getValueAtTime( time ) );
	out[ PT_FOG ]          = static_cast< float >( fog->getValueAtTime( time ) );
	colourMode->getValueAtTime( time, choice );
	out[ PT_COLOUR_MODE ] = static_cast< float >( choice );

	double r = 0.0, g = 0.0, b = 0.0;
	colour->getValueAtTime( time, r, g, b );
	out[ PT_COLOUR_R ] = static_cast< float >( r );
	out[ PT_COLOUR_G ] = static_cast< float >( g );
	out[ PT_COLOUR_B ] = static_cast< float >( b );

	out[ PT_HUE_SPREAD ] = static_cast< float >( hueSpread->getValueAtTime( time ) );
	out[ PT_HUE_CYCLE ]  = static_cast< float >( hueCycle->getValueAtTime( time ) );
	out[ PT_OPACITY ]    = static_cast< float >( opacity->getValueAtTime( time ) );

	background->getValueAtTime( time, r, g, b );
	out[ PT_BACK_R ] = static_cast< float >( r );
	out[ PT_BACK_G ] = static_cast< float >( g );
	out[ PT_BACK_B ] = static_cast< float >( b );

	out[ PT_BACK_OPACITY ] = static_cast< float >( backOpacity->getValueAtTime( time ) );
	out[ PT_MIX ]          = static_cast< float >( mix->getValueAtTime( time ) );

	if( maskMode != nullptr )
	{
		maskMode->getValueAtTime( time, choice );
		out[ PT_MASK_MODE ] = static_cast< float >( choice );
	}
}

void IdlerOFXPlugin::render( const OFX::RenderArguments& args )
{
	using namespace idler;

	std::unique_ptr< OFX::Image > dst( dstClip->fetchImage( args.time ) );
	if( !dst.get() )
		return;

	std::unique_ptr< OFX::Image > src;
	if( maskVariant && srcClip != nullptr && srcClip->isConnected() )
		src.reset( srcClip->fetchImage( args.time ) );

	const OfxRectI bounds = dst->getBounds();
	const int width       = bounds.x2 - bounds.x1;
	const int height      = bounds.y2 - bounds.y1;
	if( width <= 0 || height <= 0 )
		return;

	float params[ PT_COUNT ];
	readParams( args.time, params );

	// Saver time. Free runs off the host clock; Manual ignores speed entirely so
	// Phase is the only driver, which is the mode to keyframe against an edit.
	// There is no Beat or Bar here — OFX carries no tempo.
	//
	// Saver time is `seconds * speed` here and is NOT anchored the way the FFGL
	// build's is. The anchor exists so that nudging Speed live does not teleport
	// the picture, and it is a running carry -- which needs frames to arrive in
	// order. This host renders arbitrary times in arbitrary order and can
	// keyframe Speed, so an anchor here would make a frame depend on which
	// frames happened to be rendered before it. A pure product is the right
	// answer for a timeline; see Idler.h.
	const double fps      = dstClip->getFrameRate() > 0.0 ? dstClip->getFrameRate() : 25.0;
	const float seconds   = static_cast< float >( args.time / fps );
	const int syncMode    = Option( params[ PT_SYNC ], 2 );
	const float phaseSecs = PhaseFromParam( params[ PT_PHASE ] );
	const float saverTime = syncMode == 1 ? phaseSecs
	                                      : seconds * SpeedFromParam( params[ PT_SPEED ] ) + phaseSecs;

	std::string text;
	textParam->getValueAtTime( args.time, text );

	const Settings settings =
	    SettingsFromParams( params, width, height, saverTime, text.c_str(), 0.0f );

	if( saver == nullptr || saverKind != settings.saver )
	{
		saver     = MakeSaver( settings.saver );
		saverKind = settings.saver;
	}

	scene.Clear();
	scene.background = settings.background;
	if( saver != nullptr )
		saver->Build( settings, scene );

	pixels.assign( static_cast< size_t >( width ) * height * 4, 0.0f );
	Rasterise( scene, pixels.data(), width, height,
	           EdgeWidthPixels( settings.lineWidth, width, height ) );

	Frame frame;
	frame.rgba     = pixels.data();
	frame.width    = width;
	frame.height   = height;
	frame.mix      = std::min( std::max( params[ PT_MIX ], 0.0f ), 1.0f );
	frame.hasInput = src.get() != nullptr;
	frame.maskMode = static_cast< MaskMode >(
	    Option( params[ PT_MASK_MODE ], static_cast< int >( MaskMode::Count ) ) );

	const OFX::BitDepthEnum depth      = dst->getPixelDepth();
	const OFX::PixelComponentEnum comp = dst->getPixelComponents();

	std::unique_ptr< CompositeBase > processor;
	if( comp == OFX::ePixelComponentRGBA )
	{
		if( depth == OFX::eBitDepthFloat )
			processor.reset( new Composite< float, 4, 1 >( *this ) );
		else if( depth == OFX::eBitDepthUShort )
			processor.reset( new Composite< unsigned short, 4, 65535 >( *this ) );
		else if( depth == OFX::eBitDepthUByte )
			processor.reset( new Composite< unsigned char, 4, 255 >( *this ) );
	}
	else if( comp == OFX::ePixelComponentRGB )
	{
		if( depth == OFX::eBitDepthFloat )
			processor.reset( new Composite< float, 3, 1 >( *this ) );
		else if( depth == OFX::eBitDepthUShort )
			processor.reset( new Composite< unsigned short, 3, 65535 >( *this ) );
		else if( depth == OFX::eBitDepthUByte )
			processor.reset( new Composite< unsigned char, 3, 255 >( *this ) );
	}

	if( processor == nullptr )
		OFX::throwSuiteStatusException( kOfxStatErrUnsupported );

	processor->setDstImg( dst.get() );
	processor->setFrame( src.get(), &frame );
	processor->setRenderWindow( args.renderWindow );
	processor->process();
}

void IdlerOFXPlugin::applyPreset( int presetIndex, double time )
{
	using namespace idler;

	if( presetIndex <= 0 || presetIndex > presets::kCount )
		return;  // Custom: the sliders keep whatever they said

	const presets::Preset& preset = presets::kPresets[ presetIndex - 1 ];

	// One edit block, so a host's undo takes the whole preset back at once
	// rather than making the operator undo twenty-four slider moves.
	beginEditBlock( "preset" );

	saverParam->setValueAtTime( time, static_cast< int >( preset.v[ presets::kSaver ] ) );
	density->setValueAtTime( time, preset.v[ presets::kDensity ] );
	complexity->setValueAtTime( time, preset.v[ presets::kComplexity ] );
	sizeParam->setValueAtTime( time, preset.v[ presets::kSize ] );
	lengthParam->setValueAtTime( time, preset.v[ presets::kLength ] );
	lineWidth->setValueAtTime( time, preset.v[ presets::kLineWidth ] );
	variation->setValueAtTime( time, preset.v[ presets::kVariation ] );
	shading->setValueAtTime( time, static_cast< int >( preset.v[ presets::kShading ] ) );
	speed->setValueAtTime( time, preset.v[ presets::kSpeed ] );
	fov->setValueAtTime( time, preset.v[ presets::kFov ] );
	camDistance->setValueAtTime( time, preset.v[ presets::kCamDistance ] );
	camTilt->setValueAtTime( time, preset.v[ presets::kCamTilt ] );
	fog->setValueAtTime( time, preset.v[ presets::kFog ] );
	colourMode->setValueAtTime( time, static_cast< int >( preset.v[ presets::kColourMode ] ) );
	colour->setValueAtTime( time, preset.v[ presets::kColourR ], preset.v[ presets::kColourG ],
	                        preset.v[ presets::kColourB ] );
	hueSpread->setValueAtTime( time, preset.v[ presets::kHueSpread ] );
	hueCycle->setValueAtTime( time, preset.v[ presets::kHueCycle ] );
	opacity->setValueAtTime( time, preset.v[ presets::kOpacity ] );
	// Skipped on the filter: the background there is the operator's compositing
	// decision, not the preset's. See the comment in Presets.h.
	if( !maskVariant )
	{
		background->setValueAtTime( time, preset.v[ presets::kBackR ], preset.v[ presets::kBackG ],
		                            preset.v[ presets::kBackB ] );
		backOpacity->setValueAtTime( time, preset.v[ presets::kBackOpacity ] );
	}

	endEditBlock();
}

void IdlerOFXPlugin::changedParam( const OFX::InstanceChangedArgs& args, const std::string& name )
{
	// The About links open a browser and change nothing about the render.
	if( stoatworks::about::ofx::changedParam( args, name ) )
		return;

	if( name == "preset" )
	{
		int chosen = 0;
		presetParam->getValueAtTime( args.time, chosen );
		applyPreset( chosen, args.time );
		return;
	}

	// A control the preset covers moved, so the operator has taken over and the
	// dropdown falls back to Custom. Only for a real user edit: a host that
	// echoes our own writes back as plugin-originated changes would otherwise
	// un-set the preset we just applied.
	if( args.reason != OFX::eChangeUserEdit )
		return;

	static const char* covered[] = { "saver",      "density",   "complexity", "size",
	                                 "length",     "lineWidth", "variation",  "shading",
	                                 "speed",      "fov",       "camDistance", "camTilt",
	                                 "fog",        "colourMode", "colour",    "hueSpread",
	                                 "hueCycle",   "opacity",   "background", "backOpacity" };

	for( const char* one : covered )
		if( name == one )
		{
			int active = 0;
			presetParam->getValue( active );
			if( active != 0 )
				presetParam->setValue( 0 );
			return;
		}
}

//---------------------------------------------------------------------------
// Describe
//---------------------------------------------------------------------------

void describeCommon( OFX::ImageEffectDescriptor& desc, const char* label )
{
	desc.setLabels( label, label, label );
	desc.setPluginGrouping( "Stoatworks" );
	desc.setPluginDescription(
	    "The Windows 95/98 screensavers — Mystify, Beziers, Curves and Colors, Flying Windows, "
	    "Flying Through Space, Scrolling Marquee, 3D Maze, 3D Pipes, 3D Flying Objects, "
	    "3D FlowerBox and 3D Text. Every saver is a pure function of time and a seed, so any "
	    "frame renders on its own and Phase can be keyframed. Not affiliated with Microsoft." );

	desc.addSupportedContext( OFX::eContextGeneral );
	desc.addSupportedBitDepth( OFX::eBitDepthUByte );
	desc.addSupportedBitDepth( OFX::eBitDepthUShort );
	desc.addSupportedBitDepth( OFX::eBitDepthFloat );

	desc.setSingleInstance( false );
	desc.setHostFrameThreading( false );
	desc.setSupportsMultiResolution( true );
	desc.setSupportsTiles( true );
	desc.setTemporalClipAccess( false );
	desc.setRenderTwiceAlways( false );
	desc.setSupportsMultipleClipPARs( false );

	// The growing savers keep a replay cache, and two concurrent renders of one
	// instance sharing it is a race whose symptom is a pipe network flickering
	// between two lengths. The compositing pass is still threaded.
	desc.setRenderThreadSafety( OFX::eRenderInstanceSafe );
}

OFX::DoubleParamDescriptor* slider( OFX::ImageEffectDescriptor& desc, OFX::PageParamDescriptor* page,
                                    const char* name, const char* label, double initial,
                                    const char* hint )
{
	OFX::DoubleParamDescriptor* param = desc.defineDoubleParam( name );
	param->setLabels( label, label, label );
	param->setHint( hint );
	param->setDefault( initial );
	param->setRange( 0.0, 1.0 );
	param->setDisplayRange( 0.0, 1.0 );
	page->addChild( *param );
	return param;
}

void describeParams( OFX::ImageEffectDescriptor& desc, bool maskVariant )
{
	using namespace idler;

	OFX::PageParamDescriptor* page = desc.definePageParam( "Controls" );

	{
		OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam( "saver" );
		param->setLabels( "Saver", "Saver", "Saver" );
		param->setHint( "Which screensaver." );
		for( int i = 0; i < static_cast< int >( SaverKind::Count ); ++i )
			param->appendOption( SaverName( static_cast< SaverKind >( i ) ) );
		param->setDefault( 0 );
		page->addChild( *param );
	}

	{
		OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam( "preset" );
		param->setLabels( "Preset", "Preset", "Preset" );
		param->setHint(
		    "The first eleven are each saver set up the way it actually ran — picking a saver "
		    "alone gives you that saver driven by whatever the sliders happen to say. Custom "
		    "means the sliders are the truth." );
		param->appendOption( "Custom" );
		for( int i = 0; i < presets::kCount; ++i )
			param->appendOption( presets::kPresets[ i ].name );
		param->setDefault( 1 );
		page->addChild( *param );
	}

	slider( desc, page, "density", "Density", 0.5,
	        "How many of whatever this saver has: polygons, stars, pipes." );
	slider( desc, page, "complexity", "Complexity", 0.5,
	        "How intricate each one is. Per saver: vertices, ratio, maze size." );
	slider( desc, page, "size", "Size", 0.5, "How big the objects are." );
	slider( desc, page, "length", "Length", 0.5, "Trail length, streak length, pipe run." );
	slider( desc, page, "lineWidth", "Line Width", 0.5,
	        "Line weight, in pixels scaled off the short edge." );
	slider( desc, page, "variation", "Variation", 0.5, "How much the objects differ." );

	{
		OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam( "shading" );
		param->setLabels( "Shading", "Shading", "Shading" );
		param->setHint( "Flat, lit, or wireframe." );
		param->appendOption( "Flat" );
		param->appendOption( "Lit" );
		param->appendOption( "Wireframe" );
		param->setDefault( 1 );
		page->addChild( *param );
	}

	{
		OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam( "sync" );
		param->setLabels( "Sync", "Sync", "Sync" );
		param->setHint(
		    "Free runs off the host clock. Manual ignores Speed and makes Phase the only driver, "
		    "which is the one to keyframe. There is no beat or bar here — OFX carries no tempo." );
		param->appendOption( "Free" );
		param->appendOption( "Manual" );
		param->setDefault( 0 );
		page->addChild( *param );
	}

	slider( desc, page, "speed", "Speed", 0.5,
	        "Exponential, and exactly as it ran at the centre. Ignored in Manual." );
	slider( desc, page, "phase", "Phase", 0.0,
	        "Offset in time, 0 to 60 seconds. In Manual this is the whole clock — and for 3D "
	        "Pipes and 3D Maze it grows the network to wherever you put it." );
	slider( desc, page, "seed", "Seed", 0.0, "Which variation. A different maze, not a different kind." );

	slider( desc, page, "fov", "Field of View", 0.4, "20 to 120 degrees." );
	slider( desc, page, "camDistance", "Camera Distance", 0.5, "How far back the camera sits." );
	slider( desc, page, "camTilt", "Camera Tilt", 0.5, "Level at the centre." );
	slider( desc, page, "fog", "Fog", 0.5, "Fades into transparency, not into black. 0 disables." );

	{
		OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam( "colourMode" );
		param->setLabels( "Colour Mode", "Colour Mode", "Colour Mode" );
		param->setHint(
		    "Classic is each saver's own palette as it was. Tint, Spread and Cycle put the whole "
		    "suite under one scheme." );
		param->appendOption( "Classic" );
		param->appendOption( "Tint" );
		param->appendOption( "Spread" );
		param->appendOption( "Cycle" );
		param->setDefault( 0 );
		page->addChild( *param );
	}

	{
		OFX::RGBParamDescriptor* param = desc.defineRGBParam( "colour" );
		param->setLabels( "Colour", "Colour", "Colour" );
		param->setHint( "The tint, or the starting hue. Ignored in Classic." );
		param->setDefault( 1.0, 1.0, 1.0 );
		page->addChild( *param );
	}

	slider( desc, page, "hueSpread", "Hue Spread", 0.25, "Hue fanned across the object set." );
	slider( desc, page, "hueCycle", "Hue Cycle", 0.5, "Hue rotation. Still at the centre." );
	slider( desc, page, "opacity", "Opacity", 1.0, "Of the saver itself." );

	{
		OFX::RGBParamDescriptor* param = desc.defineRGBParam( "background" );
		param->setLabels( "Background", "Background", "Background" );
		param->setHint( "Behind the saver." );
		param->setDefault( 0.0, 0.0, 0.0 );
		page->addChild( *param );
	}

	// The generator wants opaque black; the filter wants none, or it covers the
	// clip it exists to draw over. Same split as the FFGL build.
	slider( desc, page, "backOpacity", "Background Alpha", maskVariant ? 0.0 : 1.0,
	        "0 puts the saver over transparency, which is what the mask modes and a layer above "
	        "want." );

	{
		OFX::StringParamDescriptor* param = desc.defineStringParam( "text" );
		param->setLabels( "Text", "Text", "Text" );
		param->setHint( "Scrolling Marquee and 3D Text. Ignored by the other nine." );
		param->setDefault( "IDLER" );
		page->addChild( *param );
	}

	if( maskVariant )
	{
		OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam( "maskMode" );
		param->setLabels( "Mask Mode", "Mask Mode", "Mask Mode" );
		param->setHint( "What the saver does to the clip." );
		for( int i = 0; i < static_cast< int >( MaskMode::Count ); ++i )
			param->appendOption( MaskModeName( static_cast< MaskMode >( i ) ) );
		param->setDefault( 0 );
		page->addChild( *param );
	}

	slider( desc, page, "mix", "Mix", 1.0, "Fades the whole effect back to the clip." );

	// The Stoatworks About block: a read-only credit line and one push button
	// per link, in a group that starts folded. Last, so it sits under the
	// effect's own controls.
	stoatworks::about::ofx::describe( desc, page );
}

//---------------------------------------------------------------------------
// Factories
//---------------------------------------------------------------------------

class IdlerSourceFactory : public OFX::PluginFactoryHelper< IdlerSourceFactory >
{
public:
	IdlerSourceFactory() :
		OFX::PluginFactoryHelper< IdlerSourceFactory >( kSourceIdentifier, 1, 0 )
	{
	}

	void describe( OFX::ImageEffectDescriptor& desc ) override
	{
		describeCommon( desc, "Idler" );
		desc.addSupportedContext( OFX::eContextGenerator );
	}

	void describeInContext( OFX::ImageEffectDescriptor& desc, OFX::ContextEnum ) override
	{
		OFX::ClipDescriptor* dst = desc.defineClip( kOfxImageEffectOutputClipName );
		dst->addSupportedComponent( OFX::ePixelComponentRGBA );
		dst->addSupportedComponent( OFX::ePixelComponentRGB );
		dst->setSupportsTiles( true );
		describeParams( desc, false );
	}

	OFX::ImageEffect* createInstance( OfxImageEffectHandle handle, OFX::ContextEnum ) override
	{
		return new IdlerOFXPlugin( handle, false );
	}
};

class IdlerMaskFactory : public OFX::PluginFactoryHelper< IdlerMaskFactory >
{
public:
	IdlerMaskFactory() : OFX::PluginFactoryHelper< IdlerMaskFactory >( kMaskIdentifier, 1, 0 ) {}

	void describe( OFX::ImageEffectDescriptor& desc ) override
	{
		describeCommon( desc, "Idler Mask" );
		desc.addSupportedContext( OFX::eContextFilter );
	}

	void describeInContext( OFX::ImageEffectDescriptor& desc, OFX::ContextEnum ) override
	{
		OFX::ClipDescriptor* src = desc.defineClip( kOfxImageEffectSimpleSourceClipName );
		src->addSupportedComponent( OFX::ePixelComponentRGBA );
		src->addSupportedComponent( OFX::ePixelComponentRGB );
		src->setSupportsTiles( true );

		OFX::ClipDescriptor* dst = desc.defineClip( kOfxImageEffectOutputClipName );
		dst->addSupportedComponent( OFX::ePixelComponentRGBA );
		dst->addSupportedComponent( OFX::ePixelComponentRGB );
		dst->setSupportsTiles( true );

		describeParams( desc, true );
	}

	OFX::ImageEffect* createInstance( OfxImageEffectHandle handle, OFX::ContextEnum ) override
	{
		return new IdlerOFXPlugin( handle, true );
	}
};

}  // namespace

namespace OFX
{
namespace Plugin
{
void getPluginIDs( OFX::PluginFactoryArray& ids )
{
	static IdlerSourceFactory source;
	static IdlerMaskFactory mask;
	ids.push_back( &source );
	ids.push_back( &mask );
}
}  // namespace Plugin
}  // namespace OFX
