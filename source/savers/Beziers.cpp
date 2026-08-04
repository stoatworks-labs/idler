#include <algorithm>

#include "../Savers.h"

/**
    Beziers.

    Mystify's motion with a curve through it instead of straight edges: the
    bouncing points are control points, and what gets drawn is the Bezier they
    define. Everything about the trail and the colour cycle is the same, which
    is why this file is short.

    ## The scene controls, in this saver

    - **Density** -- how many curves. 1..5.
    - **Complexity** -- control points per curve. 3..9. Three is a quadratic.
    - **Length** -- trail steps. 1..24.
    - **Line Width**, **Variation**, **Size** -- as Mystify.

    ## De Casteljau, not the polynomial

    The curve is evaluated by repeated linear interpolation rather than by
    summing Bernstein basis terms. The polynomial form needs binomial
    coefficients, and at nine control points those are big enough that the
    terms alternate in sign and lose precision against each other -- the curve
    develops a wobble near the ends that looks like a bug in the bouncing.
    De Casteljau is a few more multiplies and is stable at any order this saver
    can reach.
*/
namespace idler
{
namespace
{
// Matched to the corner speed below; see the note in Mystify.cpp on why a
// trail that spans a third of a second is not a trail.
constexpr float kTrailStep = 1.0f / 15.0f;

/// Points along the curve. Fixed rather than adaptive: a subdivision count
/// that responded to curvature would change the vertex count frame to frame,
/// and this mesh is uploaded whole every frame anyway.
constexpr int kSamples = 48;

Vec2 DeCasteljau( const Vec2* control, int count, float t )
{
	// At most nine control points, so this fits on the stack and the whole
	// evaluation stays in cache.
	Vec2 work[ 9 ];
	const int n = std::min( count, 9 );
	for( int i = 0; i < n; ++i )
		work[ i ] = control[ i ];

	for( int level = n - 1; level > 0; --level )
		for( int i = 0; i < level; ++i )
			work[ i ] = work[ i ] + ( work[ i + 1 ] - work[ i ] ) * t;

	return work[ 0 ];
}

class Beziers : public Saver
{
public:
	void Build( const Settings& s, Scene& scene ) override
	{
		SetFlatCamera( s, scene );

		const int curves   = CountFromDensity( s.density, 1, 5 );
		const int controls = 3 + static_cast< int >( s.complexity * 6.0f + 0.5f );
		const int trail    = 1 + static_cast< int >( s.length * 23.0f + 0.5f );

		const float width  = 0.004f + s.lineWidth * 0.03f;
		const float margin = width * 0.5f + ( 1.0f - s.size ) * 0.6f;
		const float rate   = 0.09f * ( 0.4f + s.variation * 1.2f ) *
		                   ( 1.0f + s.audioLevel * s.audioSize * 2.0f );

		std::vector< Vec2 > control( static_cast< size_t >( controls ) );
		std::vector< Vec2 > points( kSamples );
		std::vector< Vec4 > colours( kSamples );

		for( int c = 0; c < curves; ++c )
		{
			const uint32_t curveSeed = Hash2( static_cast< uint32_t >( c ), s.seed );

			for( int step = trail - 1; step >= 0; --step )
			{
				const float when = s.time - static_cast< float >( step ) * kTrailStep;

				const float alongTrail = ( trail > 1 )
				                           ? static_cast< float >( step ) / static_cast< float >( trail - 1 )
				                           : 0.0f;
				const float fade = 1.0f - alongTrail * 0.75f;

				const Vec3 classic = HsvToRgb(
					static_cast< float >( c ) * 0.29f + when * 0.06f, 1.0f, 1.0f );
				const Vec4 colour = s.Colour( classic, c, curves );
				const Vec4 faded{ colour.x, colour.y, colour.z, colour.w * fade };

				for( int i = 0; i < controls; ++i )
					control[ static_cast< size_t >( i ) ] =
						BouncePoint( curveSeed, i, when, rate, s.aspect, margin );

				for( int i = 0; i < kSamples; ++i )
				{
					const float t = static_cast< float >( i ) / static_cast< float >( kSamples - 1 );
					points[ static_cast< size_t >( i ) ]  = DeCasteljau( control.data(), controls, t );
					colours[ static_cast< size_t >( i ) ] = faded;
				}

				scene.mesh.AddPolyline( points.data(), kSamples, false, width, colours.data() );
			}
		}
	}

	size_t ExpectedTriangles( const Settings& s ) const override
	{
		const int curves = CountFromDensity( s.density, 1, 5 );
		const int trail  = 1 + static_cast< int >( s.length * 23.0f + 0.5f );
		return static_cast< size_t >( curves ) * static_cast< size_t >( trail ) * ( kSamples - 1 ) * 2;
	}
};
} // namespace

std::unique_ptr< Saver > MakeBeziers()
{
	return std::make_unique< Beziers >();
}

} // namespace idler
