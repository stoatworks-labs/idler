#include <algorithm>
#include <vector>

#include "../Savers.h"
#include "Teapot.h"

/**
    3D Pipes.

    Plumbing growing into a box, one elbow at a time, until it fills up and
    starts again.

    ## Why this one needs replay

    Where a pipe goes next depends on where it has already been -- it must not
    run back through itself -- so the state at segment 400 is not computable
    from the clock without segments 1 to 399. This is one of the two savers
    that cannot be a pure function of time on its own, and it is made one by
    `GrowingSaver`: the state at time t is *defined* as the state reached by
    replaying from the seed to tick `floor( t * tickRate )`. See Savers.h.

    A tick is **one cell of travel**. That is the natural unit: it is the
    smallest thing that can happen, and it is what the alpha inside a tick
    interpolates -- the growing tip of the pipe, which is the only part of the
    picture that moves smoothly.

    ## The scene controls, in this saver

    - **Density** -- how many pipes grow at once. 1..5.
    - **Complexity** -- the grid, 6..18 cells on a side. A finer grid is more
      plumbing and thinner pipes.
    - **Size** -- pipe radius, as a fraction of the cell.
    - **Length** -- how full the box gets before it clears and starts again.
    - **Variation** -- how often a pipe turns rather than carrying straight on.
    - **Line Width** -- unused except as the wireframe weight.

    ## The teapot

    Every so often a joint comes out as a teapot instead of a ball. That was in
    the original and it is the detail people remember, so it is here -- chosen
    by hash off the pipe and the joint index rather than by a counter, which is
    what keeps it in the same place on a replay.

    ## Clearing is a step, not a special case

    When the box is full the state resets *inside* `Step`, so growth carries on
    for as long as the clock does and the replay stays a plain loop. Nothing
    outside this file knows a reset happened.
*/
namespace idler
{
namespace
{
/// Cells of travel per second.
constexpr float kTickRate = 7.0f;

/// The six directions a pipe can travel, ordered in opposing pairs so that the
/// reverse of direction `d` is `d ^ 1`.
const int kDirections[ 6 ][ 3 ] = {
	{ 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }
};

struct Cell
{
	int x = 0, y = 0, z = 0;
};

struct Pipe
{
	std::vector< Cell > path;
	int direction = 0;
	Vec3 colour{ 1.0f, 1.0f, 1.0f };
	bool alive = true;
};

class Pipes : public GrowingSaver
{
protected:
	float TickRate( const Settings& ) const override { return kTickRate; }

	uint64_t GrowthKey( const Settings& s ) const override
	{
		// Only what growth reads. Size, colour and the camera all change the
		// picture without changing where the plumbing goes, and rebuilding the
		// network when somebody nudges a hue slider would make that slider
		// stutter.
		return static_cast< uint64_t >( s.seed ) |
		       ( static_cast< uint64_t >( CountFromDensity( s.density, 1, 5 ) ) << 32 ) |
		       ( static_cast< uint64_t >( GridSize( s ) ) << 40 ) |
		       ( static_cast< uint64_t >( s.length * 255.0f ) << 48 ) |
		       ( static_cast< uint64_t >( s.variation * 255.0f ) << 56 );
	}

	void ResetState( const Settings& s, Random& rng ) override
	{
		grid = GridSize( s );
		occupied.assign( static_cast< size_t >( grid ) * static_cast< size_t >( grid ) *
		                     static_cast< size_t >( grid ),
		                 0 );
		pipes.clear();
		placed = 0;

		const int count = CountFromDensity( s.density, 1, 5 );
		for( int i = 0; i < count; ++i )
			StartPipe( rng );
	}

	void Step( const Settings& s, Random& rng ) override
	{
		const float turnChance = 0.08f + s.variation * 0.5f;

		// Fullness at which the box clears. The original did not fill the box
		// solid before restarting -- it stopped while there was still space --
		// and stopping early is what keeps the shape readable.
		const int capacity = static_cast< int >(
			static_cast< float >( occupied.size() ) * ( 0.12f + s.length * 0.5f ) );

		bool anyAlive = false;

		for( Pipe& pipe : pipes )
		{
			if( !pipe.alive )
				continue;

			const Cell head = pipe.path.back();

			// Keep going, or turn. The straight-ahead cell is tested as well as
			// the dice, so a pipe that cannot continue straight still gets to
			// turn rather than dying with space all round it.
			int direction = pipe.direction;
			if( rng.Unit01() < turnChance || !Free( Advance( head, direction ) ) )
				direction = ChooseDirection( head, pipe.direction, rng );

			if( direction < 0 )
			{
				pipe.alive = false;
				continue;
			}

			const Cell next = Advance( head, direction );
			Occupy( next );
			pipe.path.push_back( next );
			pipe.direction = direction;
			++placed;
			anyAlive = true;
		}

		// Everything boxed in, or the box is as full as it is allowed to get.
		// Both mean start again -- and doing it here rather than in Build is
		// what keeps the replay a plain loop with no special cases in it.
		if( !anyAlive || placed >= capacity )
			ResetState( s, rng );
	}

	void Draw( const Settings& s, float alpha, Scene& scene ) override
	{
		SetCamera( s, scene );

		const float cell   = kBoxSize / static_cast< float >( grid );
		const float radius = cell * ( 0.12f + s.size * 0.3f );

		// Sides around a pipe. Eight is where a cylinder stops reading as a
		// prism at the sizes these are drawn; more is bandwidth for nothing,
		// and there can be a thousand of them.
		const int sides = 8;

		int pipeIndex = 0;
		for( const Pipe& pipe : pipes )
		{
			const Vec4 colour = s.Colour( pipe.colour, pipeIndex, static_cast< int >( pipes.size() ) );

			for( size_t i = 0; i + 1 < pipe.path.size(); ++i )
			{
				const Vec3 from = World( pipe.path[ i ], cell );
				Vec3 to         = World( pipe.path[ i + 1 ], cell );

				// The last segment of a live pipe is the growing tip: it
				// extends across the tick rather than appearing whole. This is
				// the only thing `alpha` is for, and it is what stops the
				// picture stepping at the tick rate.
				const bool isTip = pipe.alive && ( i + 2 == pipe.path.size() );
				if( isTip )
					to = Lerp( from, to, std::max( 0.02f, alpha ) );

				const Vec3 along = to - from;
				scene.mesh.AddCylinder( Mat4::Translate( from ) * Mat4::AlignZTo( along ),
				                        radius, Length( along ), sides, colour, false, false );
			}

			// A ball capping the very start, so a pipe does not begin with an
			// open tube end.
			if( !pipe.path.empty() )
				scene.mesh.AddSphere( Mat4::Translate( World( pipe.path.front(), cell ) ),
				                      radius * 1.28f, 6, sides, colour );

			// A ball at each CORNER, covering the mitre the two cylinders do not
			// make.
			//
			// Only at corners. Putting one at every cell -- which is cheaper,
			// because it needs no comparison -- turns every straight run into a
			// string of beads: the ball has to be wider than the pipe to cover
			// the mitre, and 28% wider is not subtle. That is what this looked
			// like before somebody rendered a frame and looked at it.
			for( size_t i = 1; i + 1 < pipe.path.size(); ++i )
			{
				const Cell& before = pipe.path[ i - 1 ];
				const Cell& here   = pipe.path[ i ];
				const Cell& after  = pipe.path[ i + 1 ];

				const bool straight = ( here.x - before.x == after.x - here.x ) &&
				                      ( here.y - before.y == after.y - here.y ) &&
				                      ( here.z - before.z == after.z - here.z );
				if( straight )
					continue;

				const Vec3 at = World( pipe.path[ i ], cell );

				if( Hash3( static_cast< uint32_t >( pipeIndex ), static_cast< uint32_t >( i ), s.seed ) % 37U == 0U )
					AddTeapot( scene.mesh, Mat4::Translate( at ), radius * 2.6f, colour );
				else
					scene.mesh.AddSphere( Mat4::Translate( at ), radius * 1.28f, 6, sides, colour );
			}

			++pipeIndex;
		}
	}

private:
	static constexpr float kBoxSize = 2.0f;

	static int GridSize( const Settings& s ) { return 6 + static_cast< int >( s.complexity * 12.0f + 0.5f ); }

	static Cell Advance( const Cell& c, int direction )
	{
		return { c.x + kDirections[ direction ][ 0 ],
		         c.y + kDirections[ direction ][ 1 ],
		         c.z + kDirections[ direction ][ 2 ] };
	}

	bool InBounds( const Cell& c ) const
	{
		return c.x >= 0 && c.y >= 0 && c.z >= 0 && c.x < grid && c.y < grid && c.z < grid;
	}

	size_t Index( const Cell& c ) const
	{
		return ( static_cast< size_t >( c.z ) * static_cast< size_t >( grid ) +
		         static_cast< size_t >( c.y ) ) *
		           static_cast< size_t >( grid ) +
		       static_cast< size_t >( c.x );
	}

	bool Free( const Cell& c ) const { return InBounds( c ) && occupied[ Index( c ) ] == 0; }

	void Occupy( const Cell& c ) { occupied[ Index( c ) ] = 1; }

	Vec3 World( const Cell& c, float cell ) const
	{
		const float half = kBoxSize * 0.5f;
		return { ( static_cast< float >( c.x ) + 0.5f ) * cell - half,
		         ( static_cast< float >( c.y ) + 0.5f ) * cell - half,
		         ( static_cast< float >( c.z ) + 0.5f ) * cell - half };
	}

	/// A free direction out of `from`, never a reversal. -1 when boxed in.
	int ChooseDirection( const Cell& from, int current, Random& rng )
	{
		int candidates[ 6 ];
		int count = 0;

		const int reverse = current ^ 1;

		for( int d = 0; d < 6; ++d )
		{
			if( d == reverse )
				continue;
			if( Free( Advance( from, d ) ) )
				candidates[ count++ ] = d;
		}

		if( count == 0 )
			return -1;

		return candidates[ rng.Below( count ) ];
	}

	void StartPipe( Random& rng )
	{
		// A bounded number of tries for a free cell rather than a search of the
		// whole grid. Late in a fill most of the box is taken and an exhaustive
		// search would run every tick; giving up and leaving the pipe unstarted
		// is fine, because the box is about to clear anyway.
		//
		// How many numbers this draws depends on how many attempts it took,
		// which is fine and worth being clear about: the attempts are a
		// function of the occupancy, and the occupancy is a function of the
		// replay so far. A replay and a live run take the same number of
		// attempts and so draw the same numbers. What would break that is an
		// early exit on anything outside the replay -- a wall-clock timeout,
		// say -- and there is none.
		for( int attempt = 0; attempt < 24; ++attempt )
		{
			const Cell start{ rng.Below( grid ), rng.Below( grid ), rng.Below( grid ) };
			if( !Free( start ) )
				continue;

			Pipe pipe;
			Occupy( start );
			pipe.path.push_back( start );
			pipe.direction = rng.Below( 6 );

			// The classic look is saturated plastic rather than pastel, which
			// is why the saturation and value are pinned high and only the hue
			// is drawn.
			pipe.colour = HsvToRgb( rng.Unit01(), 0.85f, 1.0f );

			// One cell of pipe laid down immediately, so a fresh network has
			// something in it. Without this the frame at t = 0 is empty -- a
			// single cell is a point, and it takes two to make a segment -- and
			// a generator that renders nothing on the frame you trigger it is
			// broken for the only way anybody uses one.
			const Cell second = Advance( start, pipe.direction );
			if( Free( second ) )
			{
				Occupy( second );
				pipe.path.push_back( second );
				++placed;
			}

			pipes.push_back( std::move( pipe ) );
			++placed;
			return;
		}
	}

	void SetCamera( const Settings& s, Scene& scene ) const
	{
		scene.depthTest = true;
		scene.shading   = s.shading;

		// A slow orbit. The original's camera was fixed and the pipes grew
		// toward it; the orbit is the one change that earns its place, because
		// a fixed camera on a plugin someone leaves running for an hour is a
		// still picture with something crawling in it.
		const float angle    = s.time * 0.06f;
		const float distance = 2.6f + s.camDistance * 4.0f;

		const Vec3 eye{ std::sin( angle ) * distance,
		                std::sin( s.camTilt ) * distance,
		                std::cos( angle ) * distance };

		scene.view = Mat4::LookAt( eye, { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } );
		scene.proj = Mat4::Perspective( s.fov, std::max( 0.01f, s.aspect ), 0.05f, 40.0f );

		if( s.fog > 0.001f )
		{
			// Measured from the camera, so the fog keeps its depth as the
			// camera pulls back rather than swallowing the whole box.
			scene.fogStart = distance * ( 0.4f + ( 1.0f - s.fog ) * 0.8f );
			scene.fogEnd   = distance + kBoxSize * 1.4f;
		}
	}

	std::vector< uint8_t > occupied;
	std::vector< Pipe > pipes;
	int grid   = 10;
	int placed = 0;
};
} // namespace

std::unique_ptr< Saver > MakePipes()
{
	return std::make_unique< Pipes >();
}

} // namespace idler
