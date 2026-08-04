#include <algorithm>
#include <cstring>

#include "../Savers.h"
#include "Font.h"

/**
    Scrolling Marquee.

    A line of text crossing the frame, bouncing gently up and down, in a colour
    that cycles. The saver everybody used to leave a message on their monitor.

    ## The scene controls, in this saver

    - **Size** -- cap height, as a fraction of the frame height.
    - **Line Width** -- stroke weight.
    - **Density** -- how many copies of the text are in flight. More than one
      turns it into a repeating band, which is what makes it usable as a
      background rather than a message.
    - **Length** -- how far it bounces vertically. Zero holds it on the centre
      line.
    - **Complexity** -- how fast it bounces.
    - **Variation** -- how much the copies differ in hue.

    The string comes from the **Text** parameter. Empty falls back to the
    plugin's name rather than drawing nothing, because an empty marquee looks
    exactly like a broken one.

    ## Wrapping

    The scroll position is `time * speed` reduced modulo one repeat length, so
    it is a pure function of time and never accumulates. A copy is placed every
    repeat length across a span wider than the frame, so one is always entering
    as another leaves.

    The repeat length is the text's own width plus a gap, which means **a longer
    message scrolls with the same gap rather than the same period** -- the
    behaviour you want, and the one you do not get by dividing the frame into a
    fixed number of slots.
*/
namespace idler
{
namespace
{
class Marquee : public Saver
{
public:
	void Build( const Settings& s, Scene& scene ) override
	{
		SetFlatCamera( s, scene );

		const char* message = ( s.text != nullptr && s.text[ 0 ] != '\0' ) ? s.text : "Idler";

		const float capHeight = 0.1f + s.size * 0.8f;
		const float width     = ( 0.02f + s.lineWidth * 0.12f ) * capHeight;
		const int copies      = CountFromDensity( s.density, 1, 6 );

		const float textWidth = font::MeasureText( message ) * capHeight;
		// The gap scales with the text so a short message does not end up with
		// a gap many times its own length.
		const float repeat = textWidth + capHeight * 2.0f + s.aspect * 0.5f;

		const float scrollSpeed = 0.35f * ( 1.0f + s.audioLevel * s.audioSize * 2.0f );
		float scroll            = s.time * scrollSpeed;
		scroll -= std::floor( scroll / repeat ) * repeat;

		const float bounceHeight = s.length * ( 1.0f - capHeight * 0.5f );
		const float bounceRate   = 0.05f + s.complexity * 0.35f;

		// Enough copies to cover the frame plus one entering and one leaving.
		const int span = static_cast< int >( ( s.aspect * 2.0f + repeat ) / repeat ) + 2;

		for( int copy = 0; copy < copies; ++copy )
		{
			// Copies are stacked at different heights and phases rather than
			// simply repeated, so a Density above one reads as a band of text
			// rather than as one line drawn several times.
			const float copyFraction = ( copies > 1 )
			                             ? static_cast< float >( copy ) / static_cast< float >( copies )
			                             : 0.0f;

			const float bounce = ( TriangleWave( s.time * bounceRate + copyFraction ) * 2.0f - 1.0f ) *
			                     bounceHeight;
			const float baseline = bounce - capHeight * 0.5f;

			const Vec3 classic = HsvToRgb( s.time * 0.07f + copyFraction * s.variation, 0.9f, 1.0f );
			const Vec4 colour  = s.Colour( classic, copy, copies );

			for( int repeatIndex = -1; repeatIndex < span; ++repeatIndex )
			{
				const float x = s.aspect + capHeight - scroll +
				                static_cast< float >( repeatIndex ) * repeat -
				                copyFraction * repeat;

				// Cheap reject before laying out a whole string of glyphs.
				if( x > s.aspect + capHeight || x + textWidth < -s.aspect - capHeight )
					continue;

				DrawText( scene, message, x, baseline, capHeight, width, colour );
			}
		}
	}

private:
	static void DrawText( Scene& scene, const char* message, float x, float baseline,
	                      float capHeight, float width, const Vec4& colour )
	{
		std::vector< Vec2 > points;
		std::vector< Vec4 > colours;

		for( const char* p = message; *p != '\0'; ++p )
		{
			const bool small   = ( *p >= 'a' && *p <= 'z' );
			const float scale  = capHeight * ( small ? font::kSmallCapScale : 1.0f );
			const font::Glyph& glyph = font::GetGlyph( *p );

			for( const font::Stroke& stroke : glyph.strokes )
			{
				points.clear();
				colours.clear();
				points.reserve( stroke.size() );
				colours.reserve( stroke.size() );

				for( const Vec2& point : stroke )
				{
					points.push_back( { x + point.x * scale, baseline + point.y * scale } );
					colours.push_back( colour );
				}

				scene.mesh.AddPolyline( points.data(), static_cast< int >( points.size() ), false,
				                        width, colours.data() );
			}

			x += glyph.advance * scale;
		}
	}
};
} // namespace

std::unique_ptr< Saver > MakeMarquee()
{
	return std::make_unique< Marquee >();
}

} // namespace idler
