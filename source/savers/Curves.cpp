#include <algorithm>

#include "../Savers.h"

/**
    Curves and Colors.

    A spirograph figure drawing itself, on a palette that rotates as it goes.
    The head of the curve advances, the tail rubs out behind it, and the ratio
    driving the figure drifts so it never quite repeats.

    ## The scene controls, in this saver

    - **Density** -- how many curves are on screen at once. 1..4, each offset
      along the figure.
    - **Complexity** -- the frequency ratio of the roulette, 1..9. Whole
      numbers close the figure; the fractional part is what makes it precess.
    - **Length** -- how much of the figure is drawn at any moment, from a short
      arc to the whole closed curve.
    - **Size** -- overall radius.
    - **Line Width** -- stroke weight.
    - **Variation** -- how far the inner radius sits from the outer, which is
      what turns a circle into a rosette and then into a star.

    ## Why the ratio is not snapped to a whole number

    An integer ratio closes the figure exactly and it repeats forever. A ratio a
    hair off an integer makes the whole figure precess slowly, which is the
    difference between a logo and something that stays interesting for an hour.
    Complexity is therefore continuous, and lands on a whole number only if you
    put it there.
*/
namespace idler
{
namespace
{
/// Points along the drawn arc. High, because a spirograph's curvature spikes at
/// the cusps and an under-sampled cusp shows as a visible corner.
constexpr int kSamples = 512;

class Curves : public Saver
{
public:
	void Build( const Settings& s, Scene& scene ) override
	{
		SetFlatCamera( s, scene );

		const int curves = CountFromDensity( s.density, 1, 4 );

		// The roulette: a point on a circle of radius `inner` rolling inside one
		// of radius `outer`, offset from the rolling circle's centre by `arm`.
		const float ratio = 1.0f + s.complexity * 8.0f;
		const float outer = ( 0.25f + s.size * 0.7f ) * ( 1.0f + s.audioLevel * s.audioSize * 0.4f );
		const float inner = outer / ratio;
		const float arm   = inner * ( 0.4f + s.variation * 1.6f );

		const float width = 0.003f + s.lineWidth * 0.02f;

		// How much of the figure is visible. The figure closes after `ratio`
		// turns of the outer circle when the ratio is whole, so that is the
		// natural full extent.
		const float span = ( 0.08f + s.length * 0.92f ) * kTwoPi * ratio;

		// Where the head is. Deliberately slow: the original drew at a
		// leisurely rate and speeding it up turns the figure into a flicker.
		const float head = s.time * 0.55f;

		std::vector< Vec2 > points( kSamples );
		std::vector< Vec4 > colours( kSamples );

		for( int c = 0; c < curves; ++c )
		{
			// Curves are spaced around the figure rather than given their own
			// seeds, so they read as one drawing being traced several times over
			// rather than as several unrelated drawings.
			const float offset = static_cast< float >( c ) / static_cast< float >( curves ) * kTwoPi * ratio;

			for( int i = 0; i < kSamples; ++i )
			{
				const float alongCurve = static_cast< float >( i ) / static_cast< float >( kSamples - 1 );
				const float theta      = head + offset - span * ( 1.0f - alongCurve );

				// Hypotrochoid.
				const float k = ( outer - inner ) / inner;
				points[ static_cast< size_t >( i ) ] = {
					( outer - inner ) * std::cos( theta ) + arm * std::cos( k * theta ),
					( outer - inner ) * std::sin( theta ) - arm * std::sin( k * theta )
				};

				// The palette rotates along the curve AND with time, which is
				// what "and Colors" meant: the figure is a moving slice through
				// a rotating rainbow rather than a coloured line.
				const Vec3 classic = HsvToRgb( theta * 0.02f + s.time * 0.05f, 0.85f, 1.0f );
				const Vec4 colour  = s.Colour( classic, c, curves );

				// The tail fades out rather than stopping dead, which is what
				// makes the figure look like it is being drawn instead of
				// scrolling.
				const float fade = std::min( 1.0f, alongCurve * 6.0f );
				colours[ static_cast< size_t >( i ) ] = { colour.x, colour.y, colour.z, colour.w * fade };
			}

			scene.mesh.AddPolyline( points.data(), kSamples, false, width, colours.data() );
		}
	}

	size_t ExpectedTriangles( const Settings& s ) const override
	{
		return static_cast< size_t >( CountFromDensity( s.density, 1, 4 ) ) * ( kSamples - 1 ) * 2;
	}
};
} // namespace

std::unique_ptr< Saver > MakeCurves()
{
	return std::make_unique< Curves >();
}

} // namespace idler
