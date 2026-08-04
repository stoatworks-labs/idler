#include "Controls.h"

#include <algorithm>
#include <cmath>

namespace idler
{
namespace
{
float Clamp01( float value )
{
	return value < 0.0f ? 0.0f : ( value > 1.0f ? 1.0f : value );
}

/// A slider that is exactly `centreValue` at 0.5 and exponential either side.
///
/// Used wherever "off", "normal" or "none" sits in the middle of a range and
/// has to be findable by feel. A linear slider through 1 on a 0..4 range puts
/// unity at a quarter of the travel, which nobody hits by accident.
float Exponential( float value, float lo, float centreValue, float hi )
{
	value = Clamp01( value );
	if( value < 0.5f )
		return lo * std::pow( centreValue / lo, value * 2.0f );
	return centreValue * std::pow( hi / centreValue, ( value - 0.5f ) * 2.0f );
}
} // namespace

const char* SaverName( SaverKind saver )
{
	switch( saver )
	{
	case SaverKind::Mystify:       return "Mystify";
	case SaverKind::Beziers:       return "Beziers";
	case SaverKind::Curves:        return "Curves and Colors";
	case SaverKind::FlyingWindows: return "Flying Windows";
	case SaverKind::Starfield:     return "Flying Through Space";
	case SaverKind::Marquee:       return "Scrolling Marquee";
	case SaverKind::Maze:          return "3D Maze";
	case SaverKind::Pipes:         return "3D Pipes";
	case SaverKind::FlyingObjects: return "3D Flying Objects";
	case SaverKind::FlowerBox:     return "3D FlowerBox";
	case SaverKind::Text3D:        return "3D Text";
	default:                       return "?";
	}
}

const char* SyncName( Sync sync )
{
	switch( sync )
	{
	case Sync::Free:   return "Free";
	case Sync::Beat:   return "Beat";
	case Sync::Bar:    return "Bar";
	case Sync::Manual: return "Manual";
	default:           return "?";
	}
}

const char* ColourModeName( ColourMode mode )
{
	switch( mode )
	{
	case ColourMode::Classic: return "Classic";
	case ColourMode::Tint:    return "Tint";
	case ColourMode::Spread:  return "Spread";
	case ColourMode::Cycle:   return "Cycle";
	default:                  return "?";
	}
}

const char* MaskModeName( MaskMode mode )
{
	switch( mode )
	{
	case MaskMode::Over:      return "Over";
	case MaskMode::Reveal:    return "Reveal";
	case MaskMode::Hide:      return "Hide";
	case MaskMode::Colourise: return "Colourise";
	default:                  return "?";
	}
}

Vec4 Settings::Colour( const Vec3& classic, int index, int count ) const
{
	// The fan across a set is by index, so it is stable frame to frame: object
	// 3 of 8 is the same hue next frame whatever the others did. Deriving it
	// from position or age instead would make the palette crawl.
	const float fraction = ( count > 1 )
	                         ? static_cast< float >( index ) / static_cast< float >( count - 1 )
	                         : 0.0f;

	Vec3 rgb;
	switch( colourMode )
	{
	case ColourMode::Classic:
		rgb = classic;
		break;

	case ColourMode::Tint:
		// The classic colour's luminance, in the chosen hue. Keeping the
		// luminance is what stops Tint flattening a lit 3D saver into a
		// silhouette -- the shading is in that number.
		rgb = tint * ( 0.2126f * classic.x + 0.7152f * classic.y + 0.0722f * classic.z );
		break;

	case ColourMode::Spread:
	case ColourMode::Cycle:
	{
		// The chosen colour sets the base hue and the saturation; the classic
		// colour still sets the brightness, for the same reason as above.
		const float maxC = std::max( { tint.x, tint.y, tint.z } );
		const float minC = std::min( { tint.x, tint.y, tint.z } );
		const float sat  = ( maxC > 1e-5f ) ? ( maxC - minC ) / maxC : 0.0f;

		float baseHue = 0.0f;
		if( maxC - minC > 1e-5f )
		{
			const float d = maxC - minC;
			if( maxC == tint.x )
				baseHue = ( tint.y - tint.z ) / d / 6.0f;
			else if( maxC == tint.y )
				baseHue = ( 2.0f + ( tint.z - tint.x ) / d ) / 6.0f;
			else
				baseHue = ( 4.0f + ( tint.x - tint.y ) / d ) / 6.0f;
		}

		const float spread = ( colourMode == ColourMode::Spread ) ? hueSpread * fraction : 0.0f;
		const float cycle  = hueCycle * time;
		const float value  = 0.2126f * classic.x + 0.7152f * classic.y + 0.0722f * classic.z;

		rgb = HsvToRgb( baseHue + spread + cycle, sat > 0.02f ? sat : 1.0f, value );
		break;
	}

	default:
		rgb = classic;
		break;
	}

	return { rgb.x, rgb.y, rgb.z, opacity };
}

float SpeedFromParam( float value )
{
	value = Clamp01( value );
	// A dead zone rather than an asymptote: an exponential curve never reaches
	// zero, so without this the only way to stop the animation is to drag to
	// exactly 0.0 and hope the host sends it.
	if( value < 0.02f )
		return 0.0f;
	return Exponential( ( value - 0.02f ) / 0.98f, 0.05f, 1.0f, 4.0f );
}

float PhaseFromParam( float value )
{
	return Clamp01( value ) * 60.0f;
}

uint32_t SeedFromParam( float value )
{
	const int seed = 1 + static_cast< int >( Clamp01( value ) * 9998.0f + 0.5f );
	return static_cast< uint32_t >( seed );
}

float FovFromParam( float value )
{
	const float degrees = 20.0f + Clamp01( value ) * 100.0f;
	return degrees * kPi / 180.0f;
}

float CamTiltFromParam( float value )
{
	return ( Clamp01( value ) * 2.0f - 1.0f ) * ( 60.0f * kPi / 180.0f );
}

float HueSpreadFromParam( float value )
{
	return Clamp01( value );
}

float HueCycleFromParam( float value )
{
	const float centred = Clamp01( value ) * 2.0f - 1.0f;
	// Squared so the useful slow drift occupies most of the travel; a linear
	// slider spends nine tenths of itself on speeds that strobe.
	return centred * std::fabs( centred ) * 0.5f;
}

//---------------------------------------------------------------------------
// Parameters to settings -- the one home.
//
// Two builds read this: the FFGL plugin, and the OpenFX one, which fills the
// same 0..1 array from its own parameters and calls straight through. That is
// deliberate. The mapping is where a preset actually means something -- a curve
// on Speed, a range on Field of View, the premultiply on the background -- and
// a second copy of it is how the same preset comes to look different in Resolve
// than it does in Resolume.
//---------------------------------------------------------------------------
Settings SettingsFromParams( const float* params, int width, int height, float time,
                             const char* text, float audioLevel )
{
	Settings s;

	s.saver = static_cast< SaverKind >( Option( params[ PT_SAVER ], static_cast< int >( SaverKind::Count ) ) );

	s.density    = Clamp01( params[ PT_DENSITY ] );
	s.complexity = Clamp01( params[ PT_COMPLEXITY ] );
	s.size       = Clamp01( params[ PT_SIZE ] );
	s.length     = Clamp01( params[ PT_LENGTH ] );
	s.lineWidth  = Clamp01( params[ PT_LINE_WIDTH ] );
	s.variation  = Clamp01( params[ PT_VARIATION ] );
	s.shading    = static_cast< Shading >( Option( params[ PT_SHADING ], static_cast< int >( Shading::Count ) ) );

	s.time = time;
	s.seed = SeedFromParam( params[ PT_SEED ] );

	s.fov         = FovFromParam( params[ PT_FOV ] );
	s.camDistance = Clamp01( params[ PT_CAM_DISTANCE ] );
	s.camTilt     = CamTiltFromParam( params[ PT_CAM_TILT ] );
	s.fog         = Clamp01( params[ PT_FOG ] );

	s.colourMode = static_cast< ColourMode >( Option( params[ PT_COLOUR_MODE ], static_cast< int >( ColourMode::Count ) ) );
	s.tint       = { Clamp01( params[ PT_COLOUR_R ] ), Clamp01( params[ PT_COLOUR_G ] ), Clamp01( params[ PT_COLOUR_B ] ) };
	s.hueSpread  = HueSpreadFromParam( params[ PT_HUE_SPREAD ] );
	s.hueCycle   = HueCycleFromParam( params[ PT_HUE_CYCLE ] );
	s.opacity    = Clamp01( params[ PT_OPACITY ] );

	const float backAlpha = Clamp01( params[ PT_BACK_OPACITY ] );
	// Premultiplied, because that is what the target is cleared to and what the
	// scene shader writes.
	s.background = { Clamp01( params[ PT_BACK_R ] ) * backAlpha,
	                 Clamp01( params[ PT_BACK_G ] ) * backAlpha,
	                 Clamp01( params[ PT_BACK_B ] ) * backAlpha,
	                 backAlpha };

	s.aspect = ( height > 0 ) ? static_cast< float >( width ) / static_cast< float >( height ) : 1.0f;
	s.text   = text;

	s.audioLevel = audioLevel;
	s.audioSize  = Clamp01( params[ PT_AUDIO_SIZE ] );
	s.audioSpeed = Clamp01( params[ PT_AUDIO_SPEED ] );

	return s;
}

int Option( float value, int count )
{
	const int index = static_cast< int >( value + 0.5f );
	return std::max( 0, std::min( count - 1, index ) );
}

} // namespace idler
