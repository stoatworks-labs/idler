#include <algorithm>

#include "../Savers.h"

/**
    Flying Windows.

    The four-pane logo, streaming out of the middle of the screen. Same flight
    as Flying Through Space -- see `FlyingPoint` in Savers.h -- with a logo
    drawn at each point instead of a dot. They were the same trick on the
    machine and they are the same trick here.

    ## The scene controls, in this saver

    - **Density** -- how many logos. 4..120.
    - **Size** -- how big a near logo gets.
    - **Complexity** -- the gap between the four panes, from touching to widely
      separated.
    - **Variation** -- how much the logos differ in brightness.
    - **Line Width** -- the pane corner inset, which is as close as a flat quad
      gets to the original's rounded look.
    - **Length** -- unused.

    ## The logo

    Four quads in the four colours, sheared into a parallelogram and stacked so
    the top two are narrower than the bottom two -- the perspective of the
    original mark, faked with a shear because it is drawn flat. It is a
    recognisable gesture rather than a reproduction, which is the right side of
    the line for something that ships as a plugin: it reads as the logo at
    speed, and nothing here is a copy of Microsoft's artwork.
*/
namespace idler
{
namespace
{
/// The four pane colours, in reading order.
const Vec3 kPaneColours[ 4 ] = {
	{ 0.95f, 0.28f, 0.20f },// red
	{ 0.40f, 0.75f, 0.25f },// green
	{ 0.20f, 0.50f, 0.90f },// blue
	{ 0.98f, 0.78f, 0.18f } // yellow
};

class FlyingWindows : public Saver
{
public:
	void Build( const Settings& s, Scene& scene ) override
	{
		SetFlatCamera( s, scene );

		const int logos  = CountFromDensity( s.density, 4, 120 );
		const float rate = 0.1f * ( 1.0f + s.audioLevel * s.audioSize * 2.5f );

		const float unit  = ( 0.03f + s.size * 0.09f );
		const float gap   = 0.06f + s.complexity * 0.35f;
		const float inset = s.lineWidth * 0.18f;

		// The shear that fakes the mark's perspective. Constant, because it is
		// part of the shape rather than something to animate.
		constexpr float kShear = 0.22f;
		// How much narrower the top row is than the bottom.
		constexpr float kTaper = 0.78f;

		for( int i = 0; i < logos; ++i )
		{
			const Flight flight = FlyingPoint( s.seed, i, s.time, rate );

			const float scale = unit * flight.scale;
			if( scale < 0.0005f )
				continue;

			const float vary = 1.0f - s.variation * Unit( Hash3( static_cast< uint32_t >( i ), s.seed, 0x5A5AU ) ) * 0.6f;

			// Fade in from the far plane, out at the near one. The near fade
			// matters: without it a logo covering half the frame vanishes
			// between one frame and the next.
			const float arrival  = std::min( 1.0f, flight.age * 12.0f );
			const float departure = std::min( 1.0f, ( 1.0f - flight.age ) * 6.0f );
			const float alpha    = arrival * departure;

			for( int pane = 0; pane < 4; ++pane )
			{
				const int column = pane % 2;
				const int row    = pane / 2;

				const float rowTaper = ( row == 0 ) ? kTaper : 1.0f;

				// Pane centre in logo units, before the shear.
				const float cx = ( static_cast< float >( column ) - 0.5f ) * ( 1.0f + gap ) * rowTaper;
				const float cy = ( 0.5f - static_cast< float >( row ) ) * ( 1.0f + gap );

				const float halfW = 0.5f * rowTaper * ( 1.0f - inset );
				const float halfH = 0.5f * ( 1.0f - inset );

				// Sheared quad, so it is built corner by corner rather than as
				// an axis-aligned rect.
				const Vec2 corners[ 4 ] = {
					{ cx - halfW, cy - halfH },
					{ cx + halfW, cy - halfH },
					{ cx + halfW, cy + halfH },
					{ cx - halfW, cy + halfH }
				};

				const Vec3 classic = kPaneColours[ pane ] * vary;
				const Vec4 colour  = s.Colour( classic, i, logos );
				const Vec4 faded{ colour.x, colour.y, colour.z, colour.w * alpha };

				const uint32_t base = scene.mesh.Mark();
				for( const Vec2& corner : corners )
				{
					const float x = ( corner.x + corner.y * kShear ) * scale + flight.screen.x;
					const float y = corner.y * scale + flight.screen.y;
					scene.mesh.AddVertex( { x, y, 0.0f }, { 0.0f, 0.0f, 1.0f }, faded );
				}
				scene.mesh.AddQuad( base, base + 1, base + 2, base + 3 );
			}
		}
	}

	size_t ExpectedTriangles( const Settings& s ) const override
	{
		return static_cast< size_t >( CountFromDensity( s.density, 4, 120 ) ) * 8;
	}
};
} // namespace

std::unique_ptr< Saver > MakeFlyingWindows()
{
	return std::make_unique< FlyingWindows >();
}

} // namespace idler
