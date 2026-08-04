#include <algorithm>

#include "../Savers.h"

/**
    Mystify Your Mind.

    Polygons whose corners bounce around the screen, each polygon leaving a
    trail of its own recent shapes behind it, the whole thing cycling through
    colours.

    ## The scene controls, in this saver

    - **Density** -- how many polygons. 1..6.
    - **Complexity** -- corners per polygon. 2..8. Two corners is a bouncing
      line, which is what the original's second shape often collapsed to and is
      worth having.
    - **Length** -- how many trail steps. 1..24.
    - **Line Width** -- stroke weight.
    - **Variation** -- how fast the corners travel relative to each other.
    - **Size** -- how far from the edge the corners are allowed to go. Below
      the middle the polygon works a smaller box in the centre of the frame.

    ## The trail is not a buffer

    The obvious way to leave a trail is to keep the last N shapes in a list and
    push a new one each frame. That would make the trail's length depend on the
    frame rate, and it would mean the picture could not be rendered at an
    arbitrary time.

    Instead trail step `i` is simply **the polygon evaluated at `time - i * dt`**.
    The trail is a window backwards through the same pure function that draws
    the head. Nothing is stored, the spacing is exact at any frame rate, and
    scrubbing works.

    `dt` is fixed at 1/30 s of saver time rather than tied to the real frame
    interval, so the trail is the same length in seconds whatever the host is
    managing -- which is the whole point.
*/
namespace idler
{
namespace
{
/**
    Trail spacing, in seconds of saver time.

    This number and the corner speed below have to be chosen together, and
    getting them wrong is not a subtle failure. The corners travel about
    0.18 frame-heights per second; a trail of a dozen steps at 1/30 s spans a
    third of a second, which is **six per cent of the frame** -- so every trail
    step lands almost exactly on top of the one in front and the whole thing
    renders as one thick sliver rather than as a trail.

    At 1/15 s a full trail spans something you can see, which is the point of
    the saver.
*/
constexpr float kTrailStep = 1.0f / 15.0f;

class Mystify : public Saver
{
public:
	void Build( const Settings& s, Scene& scene ) override
	{
		SetFlatCamera( s, scene );

		const int polygons = CountFromDensity( s.density, 1, 6 );
		const int corners  = 2 + static_cast< int >( s.complexity * 6.0f + 0.5f );
		const int trail    = 1 + static_cast< int >( s.length * 23.0f + 0.5f );

		// A line's own width has to be kept inside the box or the outer half of
		// the stroke is clipped at every bounce, which reads as the polygon
		// snagging on the edge.
		const float width  = 0.004f + s.lineWidth * 0.03f;
		const float margin = width * 0.5f + ( 1.0f - s.size ) * 0.6f;

		// Variation spreads the corner rates. At zero every corner of every
		// polygon travels at the same speed, which keeps the shape rigid and
		// is a legitimate -- if static -- look.
		const float rate = 0.09f * ( 0.4f + s.variation * 1.2f ) *
		                   ( 1.0f + s.audioLevel * s.audioSize * 2.0f );

		std::vector< Vec2 > points( static_cast< size_t >( corners ) );
		std::vector< Vec4 > colours( static_cast< size_t >( corners ) );

		for( int p = 0; p < polygons; ++p )
		{
			// Each polygon gets its own seed space so that adding a polygon
			// does not reshuffle the ones already on screen.
			const uint32_t polygonSeed = Hash2( static_cast< uint32_t >( p ), s.seed );

			// Oldest first, so the head of the trail is drawn last and lands on
			// top. There is no depth test here -- draw order is the only
			// ordering a flat scene has.
			for( int step = trail - 1; step >= 0; --step )
			{
				const float when = s.time - static_cast< float >( step ) * kTrailStep;

				// Fade along the trail. The head keeps full opacity; the tail
				// reaches a quarter rather than zero, because a trail that
				// fades all the way out just looks shorter than it is.
				const float alongTrail = ( trail > 1 )
				                           ? static_cast< float >( step ) / static_cast< float >( trail - 1 )
				                           : 0.0f;
				const float fade = 1.0f - alongTrail * 0.75f;

				// The classic colour: a full-saturation hue that cycles, with
				// each trail step a little behind the one in front, so the
				// trail is a smear through the palette rather than one flat
				// colour. That banding IS the look.
				const Vec3 classic = HsvToRgb(
					static_cast< float >( p ) * 0.37f + when * 0.08f, 1.0f, 1.0f );

				const Vec4 colour = s.Colour( classic, p, polygons );
				const Vec4 faded{ colour.x, colour.y, colour.z, colour.w * fade };

				for( int c = 0; c < corners; ++c )
				{
					points[ static_cast< size_t >( c ) ] = BouncePoint(
						polygonSeed, c, when, rate, s.aspect, margin );
					colours[ static_cast< size_t >( c ) ] = faded;
				}

				// Closed for three corners or more; a two-corner "polygon" is a
				// line, and closing it would draw it twice at double width.
				const bool closed = corners > 2;
				scene.mesh.AddPolyline( points.data(), corners, closed, width, colours.data() );
			}
		}
	}

	size_t ExpectedTriangles( const Settings& s ) const override
	{
		const int polygons = CountFromDensity( s.density, 1, 6 );
		const int corners  = 2 + static_cast< int >( s.complexity * 6.0f + 0.5f );
		const int trail    = 1 + static_cast< int >( s.length * 23.0f + 0.5f );
		return static_cast< size_t >( polygons ) * static_cast< size_t >( trail ) *
		       static_cast< size_t >( corners ) * 2;
	}
};
} // namespace

std::unique_ptr< Saver > MakeMystify()
{
	return std::make_unique< Mystify >();
}

} // namespace idler
