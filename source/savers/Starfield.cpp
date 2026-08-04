#include <algorithm>

#include "../Savers.h"

/**
    Flying Through Space.

    White dots streaming out of the middle of the screen. The one everybody
    remembers, and the one with the least in it.

    ## The scene controls, in this saver

    - **Density** -- how many stars. 20..1200.
    - **Size** -- how big a near star gets.
    - **Length** -- streak. At zero a star is a dot, as it shipped; turned up it
      draws the distance it covered over the last instant, which is the
      "warp speed" look the original did not have.
    - **Line Width** -- the streak's thickness.
    - **Variation** -- how much brightness varies between stars.
    - **Complexity** -- how quickly a star fades in as it arrives.

    ## The streak is the last instant of the same function

    Same trick as Mystify's trail, and worth stating because it is the thing
    that makes both of them behave: the tail of the streak is the star's
    position at `time - dt`, not a remembered previous position. So the streak
    length is exact at any frame rate, and it is correct on the very first frame
    rather than starting as a dot and growing.

    The one thing to be careful of is the wrap. A star that passed the near
    plane between `time - dt` and `time` has a tail on the *other side* of the
    screen, and joining those two points draws a line straight across the frame.
    That is checked for explicitly below -- it is the single ugliest artefact
    this saver can produce, and it only appears at high streak lengths, which is
    exactly where nobody thinks to look.
*/
namespace idler
{
namespace
{
class Starfield : public Saver
{
public:
	void Build( const Settings& s, Scene& scene ) override
	{
		SetFlatCamera( s, scene );

		const int stars = CountFromDensity( s.density, 20, 1200 );
		const float rate = 0.12f * ( 1.0f + s.audioLevel * s.audioSize * 3.0f );

		const float dotSize   = 0.004f + s.size * 0.022f;
		const float streakDt  = s.length * 0.55f;
		const float streakWide = ( 0.5f + s.lineWidth ) * dotSize;

		// How fast a star reaches full brightness after it appears at the far
		// plane. Without this stars pop into existence at the horizon, which
		// the original avoided by simply being dim out there.
		const float fadeIn = 0.02f + s.complexity * 0.35f;

		for( int i = 0; i < stars; ++i )
		{
			const Flight now = FlyingPoint( s.seed, i, s.time, rate );

			// Brightness: near stars are brighter, plus a per-star constant so
			// the field does not look like one object photocopied.
			const float vary       = 1.0f - s.variation * Unit( Hash3( static_cast< uint32_t >( i ), s.seed, 0xABCDU ) ) * 0.7f;
			const float arrival    = std::min( 1.0f, now.age / std::max( 1e-4f, fadeIn ) );
			const float brightness = std::min( 1.0f, 0.25f + now.scale * 0.9f ) * vary * arrival;

			const Vec3 classic{ brightness, brightness, brightness };
			const Vec4 colour = s.Colour( classic, i, stars );

			if( streakDt <= 0.001f )
			{
				const float r = dotSize * std::min( 3.0f, now.scale );
				scene.mesh.AddRect( { now.screen.x - r, now.screen.y - r },
				                    { now.screen.x + r, now.screen.y + r }, colour );
				continue;
			}

			const Flight before = FlyingPoint( s.seed, i, s.time - streakDt, rate );

			// The wrap check. If the star passed the near plane during the
			// streak interval its `age` went backwards, and the two ends are on
			// opposite sides of the frame -- joining them draws a line across
			// the whole picture.
			if( before.age > now.age )
			{
				const float r = dotSize * std::min( 3.0f, now.scale );
				scene.mesh.AddRect( { now.screen.x - r, now.screen.y - r },
				                    { now.screen.x + r, now.screen.y + r }, colour );
				continue;
			}

			// The tail is dimmer, which is what makes a streak read as motion
			// rather than as a rod.
			const Vec4 tail{ colour.x, colour.y, colour.z, colour.w * 0.15f };
			scene.mesh.AddLine( before.screen, now.screen,
			                    streakWide * std::min( 3.0f, now.scale ), tail, colour );
		}
	}

	size_t ExpectedTriangles( const Settings& s ) const override
	{
		return static_cast< size_t >( CountFromDensity( s.density, 20, 1200 ) ) * 2;
	}
};
} // namespace

std::unique_ptr< Saver > MakeStarfield()
{
	return std::make_unique< Starfield >();
}

} // namespace idler
