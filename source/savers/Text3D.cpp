#include <algorithm>
#include <vector>

#include "../Savers.h"
#include "Font.h"

/**
    3D Text.

    Lettering with real depth, turning on the spot.

    ## The scene controls, in this saver

    - **Size** -- cap height.
    - **Line Width** -- how heavy the strokes are. This is the letterform's
      weight, and it is the control that most changes the character of the
      thing: thin reads as neon, heavy reads as a chrome logo.
    - **Length** -- extrusion depth.
    - **Complexity** -- how the text tumbles, from a plain spin about the
      vertical to a full three-axis roll.
    - **Density** -- how many copies are stacked in depth, which turns a word
      into a tunnel of itself.
    - **Variation** -- how much the copies differ in hue.

    The string comes from the **Text** parameter. Empty falls back to the
    plugin's name.

    ## How a stroke becomes a solid

    The font is strokes down the middle of each letter rather than outlines
    (see Font.h for why). A stroke is turned into a slab: the polyline is
    offset either side by half the weight -- with the same mitred joins the 2D
    line builder uses -- and then that ribbon is given a front face, a back
    face and walls all the way round.

    The result is genuinely solid: it has silhouette, it catches the light on
    its sides as it turns, and there is nothing to see through. What it is not
    is a *typeface* extruded -- the letters have a constant stroke weight,
    like signage rather than like a serif face. That is a consequence of a
    stroke font and it is the right trade for something that has to look
    identical in four hosts on two operating systems.

    ## The mitre limit matters more here than in 2D

    An unlimited mitre on a sharp corner -- the apex of an A, the point of a V
    -- sends the offset point off to infinity. In 2D that draws a spike. Here
    it drags a *solid* spike across the frame, and because it is real geometry
    with real depth it also fights the depth buffer of everything behind it.
    The limit below is the same four-times-the-width the 2D builder uses, and
    it is load bearing rather than cosmetic.
*/
namespace idler
{
namespace
{
/// Past this the join falls back to a bevel. See the note above.
constexpr float kMitreLimit = 4.0f;

Vec2 NormaliseSafe( const Vec2& v )
{
	const float length = std::sqrt( v.x * v.x + v.y * v.y );
	if( length < 1e-9f )
		return { 1.0f, 0.0f };
	return { v.x / length, v.y / length };
}

Vec2 Perp( const Vec2& d )
{
	return { -d.y, d.x };
}

/// Offset a polyline either side by `half`, with mitred joins.
void OffsetStroke( const font::Stroke& stroke, float half, std::vector< Vec2 >& left,
                   std::vector< Vec2 >& right )
{
	const int count = static_cast< int >( stroke.size() );
	left.clear();
	right.clear();

	for( int i = 0; i < count; ++i )
	{
		const Vec2& here = stroke[ static_cast< size_t >( i ) ];

		const Vec2 previousDirection =
			( i > 0 ) ? NormaliseSafe( here - stroke[ static_cast< size_t >( i - 1 ) ] )
			          : NormaliseSafe( stroke[ 1 ] - here );
		const Vec2 nextDirection =
			( i + 1 < count ) ? NormaliseSafe( stroke[ static_cast< size_t >( i + 1 ) ] - here )
			                  : NormaliseSafe( here - stroke[ static_cast< size_t >( i - 1 ) ] );

		const Vec2 previousNormal = Perp( previousDirection );
		const Vec2 nextNormal     = Perp( nextDirection );

		Vec2 mitre  = NormaliseSafe( previousNormal + nextNormal );
		const float cosHalf = mitre.x * previousNormal.x + mitre.y * previousNormal.y;

		float scale = ( std::fabs( cosHalf ) < 1e-4f ) ? kMitreLimit : 1.0f / cosHalf;
		if( scale > kMitreLimit || scale < -kMitreLimit )
		{
			mitre = nextNormal;
			scale = 1.0f;
		}

		const Vec2 offset = mitre * ( half * scale );
		left.push_back( { here.x + offset.x, here.y + offset.y } );
		right.push_back( { here.x - offset.x, here.y - offset.y } );
	}
}

/// A stroke as a closed solid slab, in the XY plane, extruded to +-`depth`.
void AddSlab( Mesh& mesh, const Mat4& transform, const font::Stroke& stroke, float half,
              float depth, const Vec4& colour )
{
	static thread_local std::vector< Vec2 > left, right;
	OffsetStroke( stroke, half, left, right );

	const int count = static_cast< int >( left.size() );
	if( count < 2 )
		return;

	auto place = [ & ]( const Vec2& p, float z ) {
		return ( transform * Vec4( { p.x, p.y, z }, 1.0f ) ).xyz();
	};

	const Vec3 front = Normalise( transform.TransformDirection( { 0.0f, 0.0f, 1.0f } ) );
	const Vec3 back  = -front;

	// Front and back faces.
	for( int i = 0; i + 1 < count; ++i )
	{
		const uint32_t base = mesh.Mark();
		mesh.AddVertex( place( left[ static_cast< size_t >( i ) ], depth ), front, colour );
		mesh.AddVertex( place( right[ static_cast< size_t >( i ) ], depth ), front, colour );
		mesh.AddVertex( place( right[ static_cast< size_t >( i + 1 ) ], depth ), front, colour );
		mesh.AddVertex( place( left[ static_cast< size_t >( i + 1 ) ], depth ), front, colour );
		mesh.AddQuad( base, base + 1, base + 2, base + 3 );

		const uint32_t backBase = mesh.Mark();
		mesh.AddVertex( place( left[ static_cast< size_t >( i ) ], -depth ), back, colour );
		mesh.AddVertex( place( left[ static_cast< size_t >( i + 1 ) ], -depth ), back, colour );
		mesh.AddVertex( place( right[ static_cast< size_t >( i + 1 ) ], -depth ), back, colour );
		mesh.AddVertex( place( right[ static_cast< size_t >( i ) ], -depth ), back, colour );
		mesh.AddQuad( backBase, backBase + 1, backBase + 2, backBase + 3 );
	}

	// The two side walls.
	auto wall = [ & ]( const std::vector< Vec2 >& edge, bool flip ) {
		for( int i = 0; i + 1 < count; ++i )
		{
			const Vec2& a = edge[ static_cast< size_t >( i ) ];
			const Vec2& b = edge[ static_cast< size_t >( i + 1 ) ];

			Vec2 outward = Perp( NormaliseSafe( b - a ) );
			if( flip )
				outward = { -outward.x, -outward.y };
			const Vec3 normal = Normalise( transform.TransformDirection( { outward.x, outward.y, 0.0f } ) );

			const uint32_t base = mesh.Mark();
			mesh.AddVertex( place( a, -depth ), normal, colour );
			mesh.AddVertex( place( b, -depth ), normal, colour );
			mesh.AddVertex( place( b, depth ), normal, colour );
			mesh.AddVertex( place( a, depth ), normal, colour );
			mesh.AddQuad( base, base + 1, base + 2, base + 3 );
		}
	};
	wall( left, false );
	wall( right, true );

	// End caps, so a stroke is closed rather than a tube open at both ends.
	// Without these a letter reads as hollow the moment it turns edge-on.
	auto cap = [ & ]( int index, bool atStart ) {
		const Vec2& l = left[ static_cast< size_t >( index ) ];
		const Vec2& r = right[ static_cast< size_t >( index ) ];

		const Vec2 along = NormaliseSafe( atStart
		                                    ? stroke[ 0 ] - stroke[ 1 ]
		                                    : stroke[ static_cast< size_t >( count - 1 ) ] -
		                                          stroke[ static_cast< size_t >( count - 2 ) ] );
		const Vec3 normal = Normalise( transform.TransformDirection( { along.x, along.y, 0.0f } ) );

		const uint32_t base = mesh.Mark();
		mesh.AddVertex( place( l, -depth ), normal, colour );
		mesh.AddVertex( place( r, -depth ), normal, colour );
		mesh.AddVertex( place( r, depth ), normal, colour );
		mesh.AddVertex( place( l, depth ), normal, colour );
		mesh.AddQuad( base, base + 1, base + 2, base + 3 );
	};
	cap( 0, true );
	cap( count - 1, false );
}

class Text3D : public Saver
{
public:
	void Build( const Settings& s, Scene& scene ) override
	{
		scene.depthTest = true;
		scene.shading   = ( s.shading == Shading::Flat ) ? Shading::Lit : s.shading;

		const char* message = ( s.text != nullptr && s.text[ 0 ] != '\0' ) ? s.text : "Idler";

		const float capHeight = 0.35f + s.size * 1.1f;
		const float weight    = ( 0.05f + s.lineWidth * 0.16f ) * capHeight;
		const float depth     = ( 0.03f + s.length * 0.35f ) * capHeight;

		const float textWidth = font::MeasureText( message ) * capHeight;

		// Framing off the text's own width, so a long message is pulled back
		// far enough to fit rather than running off both sides. This is the
		// thing that makes the saver usable with a real message in it.
		const float distance = ( 1.2f + s.camDistance * 2.5f ) *
		                       std::max( 1.0f, textWidth / ( 2.2f * std::max( 0.5f, s.aspect ) ) ) +
		                       capHeight;

		scene.view = Mat4::LookAt( { 0.0f, std::sin( s.camTilt ) * distance * 0.5f, distance },
		                           { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } );
		scene.proj = Mat4::Perspective( s.fov, std::max( 0.01f, s.aspect ), 0.05f, distance * 6.0f );

		if( s.fog > 0.001f )
		{
			scene.fogStart = distance * 0.6f;
			scene.fogEnd   = distance * ( 1.5f + ( 1.0f - s.fog ) * 3.0f );
		}

		const int copies = CountFromDensity( s.density, 1, 8 );

		// Complexity opens the tumble out from one axis to three. At zero it is
		// the plain spin about the vertical the original did.
		const float roll = s.complexity;
		const Mat4 tumble = Mat4::RotateY( s.time * 0.5f ) *
		                    Mat4::RotateX( std::sin( s.time * 0.31f ) * roll * 0.6f ) *
		                    Mat4::RotateZ( std::sin( s.time * 0.23f ) * roll * 0.4f );

		for( int copy = 0; copy < copies; ++copy )
		{
			const float behind = static_cast< float >( copy ) * capHeight * 0.9f;
			const Mat4 place   = tumble * Mat4::Translate( { -textWidth * 0.5f, -capHeight * 0.5f, -behind } );

			const float fraction = ( copies > 1 )
			                         ? static_cast< float >( copy ) / static_cast< float >( copies - 1 )
			                         : 0.0f;

			const Vec3 classic = HsvToRgb( s.time * 0.05f + fraction * s.variation, 0.55f,
			                               1.0f - fraction * 0.5f );
			const Vec4 colour = s.Colour( classic, copy, copies );

			float pen = 0.0f;
			for( const char* p = message; *p != '\0'; ++p )
			{
				const bool small        = ( *p >= 'a' && *p <= 'z' );
				const float scale       = capHeight * ( small ? font::kSmallCapScale : 1.0f );
				const font::Glyph& glyph = font::GetGlyph( *p );

				for( const font::Stroke& stroke : glyph.strokes )
				{
					// The glyph's own strokes are in cap-height units, so the
					// scale goes into the transform rather than into every
					// point -- which also keeps the extrusion depth in the
					// letter's units instead of the world's.
					const Mat4 glyphPlace =
						place * Mat4::Translate( { pen, 0.0f, 0.0f } ) * Mat4::Scale( Vec3( scale ) );

					AddSlab( scene.mesh, glyphPlace, stroke, weight / scale * 0.5f, depth / scale, colour );
				}

				pen += glyph.advance * scale;
			}
		}
	}
};
} // namespace

std::unique_ptr< Saver > MakeText3D()
{
	return std::make_unique< Text3D >();
}

} // namespace idler
