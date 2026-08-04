#include <algorithm>
#include <vector>

#include "../Savers.h"

/**
    3D Maze.

    A first-person walk down brick corridors, taking whatever turning comes up,
    forever.

    ## Why this one needs replay

    Which corridor the camera is in depends on every junction it has already
    taken. Like 3D Pipes, it is made a pure function of time by
    `GrowingSaver`: the state at time t is the state reached by replaying from
    the seed. See Savers.h.

    A tick is **one cell of travel**, and the alpha inside it slides the camera
    from one cell to the next -- and swings the heading round a corner. That
    turn is the only thing in this saver that is not a straight line, and it is
    eased rather than linear, because a linear heading interpolation whips
    round the corner at a constant rate and reads as a camera on rails rather
    than as walking.

    ## The scene controls, in this saver

    - **Complexity** -- the maze, 6..16 cells on a side.
    - **Density** -- how often the walk prefers a turn over carrying straight
      on. Low is long corridors; high is a fidget.
    - **Size** -- corridor width against wall height, so the corridor can be a
      tunnel or a hall.
    - **Fog** -- how far down the corridor you can see. This is the control
      that matters most: a corridor that ends in a hard-edged wall at the far
      clip plane reads as a bug, and the original faded to black too.
    - **Variation** -- how much the wall colour varies cell to cell, which is
      what stands in for the brick texture.
    - **Length**, **Line Width** -- unused, except the wireframe weight.

    ## The bricks are geometry, not a texture

    The original's walls were a brick bitmap. There is no bitmap here -- the
    plugin would have to carry one, and a low-resolution brick blown up across
    a 4K output looks like exactly what it is -- so each wall face is
    **tessellated into individual bricks**, in courses, each taking its own
    shade from a hash of its position.

    That is more than decoration, and the reason is worth stating because the
    first version did not do it. A maze walk spends a lot of its time facing a
    wall: you travel up to a junction, and at the end of that move the thing in
    front of you is a wall half a cell away. That is not a bug -- it is what
    walking down a corridor is -- but with a flat-shaded wall the frame becomes
    a **single rectangle of one colour**, and it reads as the renderer having
    failed rather than as a wall. With courses of brick on it, the same frame
    reads as a wall you have walked up to. The geometry was right the whole
    time; the picture was not.

    It costs a few thousand extra triangles on a maze that was already cheap.

    ## The whole maze is drawn every frame

    No culling. At sixteen cells on a side that is at most a few thousand
    quads, which is nothing, and the alternative -- a visibility test from a
    camera that is inside the geometry -- is a great deal of code to save
    bandwidth nobody is short of. The fog hides the far end anyway.
*/
namespace idler
{
namespace
{
/// Cells of travel per second. Slower than Pipes: this is a walking pace, and
/// the original's was famously unhurried.
constexpr float kTickRate = 1.6f;

/// Wall bits per cell.
enum Wall
{
	kNorth = 1,// -Z
	kSouth = 2,// +Z
	kWest  = 4,// -X
	kEast  = 8 // +X
};

/// Heading indices, and the cell offsets they mean.
const int kHeadingDX[ 4 ] = { 0, 1, 0, -1 };
const int kHeadingDZ[ 4 ] = { -1, 0, 1, 0 };
const int kHeadingWall[ 4 ] = { kNorth, kEast, kSouth, kWest };

class Maze : public GrowingSaver
{
protected:
	float TickRate( const Settings& ) const override { return kTickRate; }

	uint64_t GrowthKey( const Settings& s ) const override
	{
		return static_cast< uint64_t >( s.seed ) |
		       ( static_cast< uint64_t >( GridSize( s ) ) << 32 ) |
		       ( static_cast< uint64_t >( s.density * 255.0f ) << 40 );
	}

	void ResetState( const Settings& s, Random& rng ) override
	{
		grid = GridSize( s );
		Generate( rng );

		x = rng.Below( grid );
		z = rng.Below( grid );

		// The first heading has to be one there is actually a corridor along,
		// or the walk opens by driving into a wall.
		headingIn  = FirstOpenHeading( x, z, rng );
		headingOut = ChooseHeading( x, z, headingIn, 0.3f, rng );

		previousX = x;
		previousZ = z;
	}

	/**
	    One cell of travel.

	    The state carries **two** headings: the one being travelled along right
	    now, and the one that will be taken out of the cell being arrived at.

	    That second one is the whole reason this is not simply "move, then
	    decide". With one heading the camera arrives at each cell still facing
	    the corridor it came down -- so at the end of every move that ends in a
	    turn, it is looking directly at a wall a third of a metre away, and the
	    frame is a flat rectangle of brick. It looked like a rendering bug and
	    it was a sequencing one.

	    Deciding the exit in advance lets `Draw` swing the camera round during
	    the second half of the move, so it arrives already looking down the
	    corridor it is about to take.
	*/
	void Step( const Settings& s, Random& rng ) override
	{
		previousX = x;
		previousZ = z;
		headingIn = headingOut;

		x += kHeadingDX[ headingIn ];
		z += kHeadingDZ[ headingIn ];

		const float turnBias = 0.1f + s.density * 0.6f;
		headingOut           = ChooseHeading( x, z, headingIn, turnBias, rng );
	}

	void Draw( const Settings& s, float alpha, Scene& scene ) override
	{
		scene.depthTest = true;
		scene.shading   = ( s.shading == Shading::Flat ) ? Shading::Lit : s.shading;

		const float cell   = 1.0f;
		const float height = cell * ( 0.55f + ( 1.0f - s.size ) * 1.6f );

		//-------------------------------------------------------------------
		// The camera. Position slides linearly between cells; the facing holds
		// the corridor being travelled for the first part of the move and then
		// swings round to the next one, so the camera arrives already looking
		// where it is about to go. See the note on Step.
		//
		// The swing is eased rather than linear, because a linear turn goes
		// round the corner at a constant rate and reads as rails rather than
		// as walking.
		//-------------------------------------------------------------------
		constexpr float kTurnStart = 0.45f;
		const float turnProgress   = std::max( 0.0f, ( alpha - kTurnStart ) / ( 1.0f - kTurnStart ) );
		const float ease           = turnProgress * turnProgress * ( 3.0f - 2.0f * turnProgress );

		const Vec3 from = CellCentre( previousX, previousZ, cell );
		const Vec3 to   = CellCentre( x, z, cell );
		Vec3 eye        = Lerp( from, to, alpha );
		eye.y           = height * 0.5f;

		// Shortest way round, so a turn from heading 3 to heading 0 goes
		// forwards through the corner rather than three-quarters of the way
		// back round. Without this, one turn in four spins the camera.
		int delta = headingOut - headingIn;
		if( delta > 2 )
			delta -= 4;
		if( delta < -2 )
			delta += 4;

		const float angle = ( static_cast< float >( headingIn ) + static_cast< float >( delta ) * ease ) *
		                    ( kPi * 0.5f );

		const Vec3 forward{ std::sin( angle ), 0.0f, -std::cos( angle ) };
		const Vec3 target = eye + forward + Vec3( 0.0f, std::sin( s.camTilt ), 0.0f );

		scene.view = Mat4::LookAt( eye, target, { 0.0f, 1.0f, 0.0f } );
		scene.proj = Mat4::Perspective( s.fov, std::max( 0.01f, s.aspect ), 0.02f, 60.0f );

		if( s.fog > 0.001f )
		{
			// Short. Seeing more than a few cells makes the maze read as a
			// model rather than as a place you are inside.
			scene.fogStart = cell * ( 0.5f + ( 1.0f - s.fog ) * 5.0f );
			scene.fogEnd   = scene.fogStart + cell * ( 1.5f + ( 1.0f - s.fog ) * 10.0f );
		}

		//-------------------------------------------------------------------
		// The maze itself.
		//-------------------------------------------------------------------
		const float thickness = cell * 0.06f;
		const float half      = cell * 0.5f;

		for( int cz = 0; cz < grid; ++cz )
			for( int cx = 0; cx < grid; ++cx )
			{
				const uint8_t walls = cells[ Index( cx, cz ) ];
				const Vec3 centre   = CellCentre( cx, cz, cell );

				// Each face takes its shade from the cell hash. This is what
				// stands in for the brick texture -- see the note at the top.
				const float shade = 0.72f + Unit( Hash3( static_cast< uint32_t >( cx ),
				                                         static_cast< uint32_t >( cz ), seedForWalls ) ) *
				                              0.28f * ( 0.2f + s.variation );

				// The classic maze was red brick with a grey floor.
				const Vec3 brick{ 0.78f * shade, 0.30f * shade, 0.22f * shade };
				const Vec4 wallColour = s.Colour( brick, cx + cz * grid, grid * grid );

				const Vec3 mid{ centre.x, height * 0.5f, centre.z };
				const uint32_t cellKey = Hash2( static_cast< uint32_t >( cx ),
				                                static_cast< uint32_t >( cz ) ) ^ seedForWalls;

				if( walls & kNorth )
					AddBrickWall( scene.mesh, { mid.x, mid.y, mid.z - half }, { half, height * 0.5f, thickness },
					              true, wallColour, cellKey ^ 0x11U, s.variation );
				if( walls & kWest )
					AddBrickWall( scene.mesh, { mid.x - half, mid.y, mid.z }, { thickness, height * 0.5f, half },
					              false, wallColour, cellKey ^ 0x22U, s.variation );

				// South and east only on the far edge of the grid: every other
				// one is some other cell's north or west, and drawing both
				// leaves two coplanar faces fighting for the same depth.
				if( ( walls & kSouth ) && cz == grid - 1 )
					AddBrickWall( scene.mesh, { mid.x, mid.y, mid.z + half }, { half, height * 0.5f, thickness },
					              true, wallColour, cellKey ^ 0x33U, s.variation );
				if( ( walls & kEast ) && cx == grid - 1 )
					AddBrickWall( scene.mesh, { mid.x + half, mid.y, mid.z }, { thickness, height * 0.5f, half },
					              false, wallColour, cellKey ^ 0x44U, s.variation );

				const Vec3 floorGrey{ 0.28f * shade, 0.28f * shade, 0.30f * shade };
				const Vec4 floorColour = s.Colour( floorGrey, cx + cz * grid, grid * grid );
				scene.mesh.AddBox( { centre.x, -thickness, centre.z }, { half, thickness, half }, floorColour );

				const Vec3 ceilingGrey{ 0.16f * shade, 0.16f * shade, 0.19f * shade };
				const Vec4 ceilingColour = s.Colour( ceilingGrey, cx + cz * grid, grid * grid );
				scene.mesh.AddBox( { centre.x, height + thickness, centre.z }, { half, thickness, half }, ceilingColour );
			}
	}

private:
	static int GridSize( const Settings& s ) { return 6 + static_cast< int >( s.complexity * 10.0f + 0.5f ); }

	size_t Index( int cx, int cz ) const
	{
		return static_cast< size_t >( cz ) * static_cast< size_t >( grid ) + static_cast< size_t >( cx );
	}

	Vec3 CellCentre( int cx, int cz, float cell ) const
	{
		const float offset = static_cast< float >( grid ) * cell * 0.5f;
		return { ( static_cast< float >( cx ) + 0.5f ) * cell - offset, 0.0f,
		         ( static_cast< float >( cz ) + 0.5f ) * cell - offset };
	}

	/**
	    A perfect maze by depth-first backtracking, with an explicit stack.

	    Recursion would be the obvious way to write it and would blow the stack
	    on a large grid -- the corridor this carves is a single path that can
	    reach every cell, so the recursion depth is the cell count.
	*/
	void Generate( Random& rng )
	{
		const size_t total = static_cast< size_t >( grid ) * static_cast< size_t >( grid );
		cells.assign( total, static_cast< uint8_t >( kNorth | kSouth | kWest | kEast ) );

		std::vector< uint8_t > visited( total, 0 );
		std::vector< int > stack;
		stack.reserve( total );

		seedForWalls = rng.Next();

		int cx = rng.Below( grid );
		int cz = rng.Below( grid );
		visited[ Index( cx, cz ) ] = 1;
		stack.push_back( static_cast< int >( Index( cx, cz ) ) );

		while( !stack.empty() )
		{
			const int here = stack.back();
			cx             = here % grid;
			cz             = here / grid;

			int options[ 4 ];
			int count = 0;
			for( int h = 0; h < 4; ++h )
			{
				const int nx = cx + kHeadingDX[ h ];
				const int nz = cz + kHeadingDZ[ h ];
				if( nx < 0 || nz < 0 || nx >= grid || nz >= grid )
					continue;
				if( visited[ Index( nx, nz ) ] )
					continue;
				options[ count++ ] = h;
			}

			if( count == 0 )
			{
				stack.pop_back();
				continue;
			}

			const int h  = options[ rng.Below( count ) ];
			const int nx = cx + kHeadingDX[ h ];
			const int nz = cz + kHeadingDZ[ h ];

			// Knock out both sides of the wall. Removing only one leaves a
			// corridor you can walk into and not out of, and the walk above
			// tests the cell it is standing in -- so a one-sided wall shows up
			// as the camera walking through a wall it can still see.
			cells[ Index( cx, cz ) ] &= static_cast< uint8_t >( ~kHeadingWall[ h ] );
			cells[ Index( nx, nz ) ] &= static_cast< uint8_t >( ~kHeadingWall[ ( h + 2 ) % 4 ] );

			visited[ Index( nx, nz ) ] = 1;
			stack.push_back( static_cast< int >( Index( nx, nz ) ) );
		}
	}

	/**
	    A wall slab whose two broad faces are laid up in courses of brick.

	    `alongX` says which way the wall runs, which decides which axis the
	    courses run along and which pair of faces gets them. The narrow faces --
	    the top of the wall and its two ends -- are left plain: they are a few
	    centimetres wide and nobody has ever looked at one.

	    Each brick's shade comes from a hash of its own row and column, so the
	    pattern is stable frame to frame and different wall to wall. Bricks are
	    inset by a mortar gap and alternate courses are offset by half a brick,
	    because a running bond is what makes it read as masonry rather than as
	    tiling.
	*/
	static void AddBrickWall( Mesh& mesh, const Vec3& centre, const Vec3& halfExtent, bool alongX,
	                          const Vec4& colour, uint32_t key, float variation )
	{
		constexpr int kCourses     = 6;
		constexpr int kPerCourse   = 4;
		constexpr float kMortar    = 0.06f;// fraction of a brick
		const float faceHalf       = alongX ? halfExtent.x : halfExtent.z;
		const float depth          = alongX ? halfExtent.z : halfExtent.x;

		// The solid slab, so the wall is still opaque seen end-on and from
		// above. The bricks sit proud of its two broad faces, so what shows
		// between them is this -- which makes the slab the MORTAR, and it has
		// to be darker than the brick or the courses read as bright lines with
		// dark gaps between them, which is masonry inside out.
		const Vec4 mortar{ colour.x * 0.45f, colour.y * 0.45f, colour.z * 0.5f, colour.w };
		mesh.AddBox( centre, halfExtent, mortar );

		const float brickHeight = ( halfExtent.y * 2.0f ) / static_cast< float >( kCourses );
		const float brickWidth  = ( faceHalf * 2.0f ) / static_cast< float >( kPerCourse );

		// Proud of the slab by a hair. Enough to win the depth test at the
		// distances this is seen from, small enough not to show at a grazing
		// angle.
		const float proud = depth + 0.0015f;

		for( int side = 0; side < 2; ++side )
		{
			const float offset = ( side == 0 ) ? proud : -proud;
			const Vec3 normal  = alongX ? Vec3( 0.0f, 0.0f, side == 0 ? 1.0f : -1.0f )
			                            : Vec3( side == 0 ? 1.0f : -1.0f, 0.0f, 0.0f );

			for( int course = 0; course < kCourses; ++course )
			{
				// Running bond: every other course starts half a brick along.
				const float shift = ( course % 2 == 0 ) ? 0.0f : brickWidth * 0.5f;

				// One extra brick per offset course, so the shift does not leave
				// a gap at the end of the wall. The pair that overhang are
				// clipped by the courses above and below and by the next cell's
				// wall, which is why they can simply be drawn.
				for( int brick = -1; brick <= kPerCourse; ++brick )
				{
					const float u0 = -faceHalf + shift + static_cast< float >( brick ) * brickWidth;
					const float u1 = u0 + brickWidth;

					const float clampedU0 = std::max( u0, -faceHalf );
					const float clampedU1 = std::min( u1, faceHalf );
					if( clampedU1 - clampedU0 < brickWidth * 0.15f )
						continue;

					const float v0 = -halfExtent.y + static_cast< float >( course ) * brickHeight;
					const float v1 = v0 + brickHeight;

					const float insetU = brickWidth * kMortar;
					const float insetV = brickHeight * kMortar;

					const float a0 = clampedU0 + insetU, a1 = clampedU1 - insetU;
					const float b0 = v0 + insetV, b1 = v1 - insetV;
					if( a1 <= a0 || b1 <= b0 )
						continue;

					// Above 1, so a brick is always brighter than the mortar
					// behind it however the variation is set.
					const float shade =
						1.0f + Unit( Hash3( key, static_cast< uint32_t >( course ),
						                    static_cast< uint32_t >( brick + 1 ) * 2U + static_cast< uint32_t >( side ) ) ) *
						           ( 0.1f + variation * 0.5f );

					const Vec4 brickColour{ colour.x * shade, colour.y * shade, colour.z * shade, colour.w };

					auto corner = [ & ]( float u, float v ) {
						return alongX ? Vec3( centre.x + u, centre.y + v, centre.z + offset )
						              : Vec3( centre.x + offset, centre.y + v, centre.z + u );
					};

					const uint32_t base = mesh.Mark();
					mesh.AddVertex( corner( a0, b0 ), normal, brickColour );
					mesh.AddVertex( corner( a1, b0 ), normal, brickColour );
					mesh.AddVertex( corner( a1, b1 ), normal, brickColour );
					mesh.AddVertex( corner( a0, b1 ), normal, brickColour );
					mesh.AddQuad( base, base + 1, base + 2, base + 3 );
				}
			}
		}
	}

	/// The first heading out of a cell that has a corridor along it.
	int FirstOpenHeading( int cx, int cz, Random& rng ) const
	{
		int options[ 4 ];
		int count = 0;
		for( int h = 0; h < 4; ++h )
			if( ( cells[ Index( cx, cz ) ] & kHeadingWall[ h ] ) == 0 )
				options[ count++ ] = h;

		// A perfect maze has no fully walled cell, so `count` is never zero --
		// but the fallback costs a line and the alternative is reading past the
		// end of the array if that ever stops being true.
		return count == 0 ? 0 : options[ rng.Below( count ) ];
	}

	/// Where to go on leaving `(cx, cz)`, having arrived along `arrived`.
	///
	/// Reversing is a last resort, which is what makes the walk read as
	/// exploring rather than as pacing up and down one corridor.
	int ChooseHeading( int cx, int cz, int arrived, float turnBias, Random& rng ) const
	{
		int options[ 4 ];
		int count         = 0;
		const int reverse = ( arrived + 2 ) % 4;

		for( int h = 0; h < 4; ++h )
		{
			if( h == reverse )
				continue;
			if( ( cells[ Index( cx, cz ) ] & kHeadingWall[ h ] ) == 0 )
				options[ count++ ] = h;
		}

		if( count == 0 )
		{
			// A dead end. Turn round -- and draw a number anyway, so the number
			// of draws does not depend on the branch taken. A replay that
			// consumed a different count here would diverge from a live run at
			// the first dead end, which is the sort of bug that shows up as
			// "the maze is different after you scrub".
			(void)rng.Next();
			return reverse;
		}

		const bool straightOpen = ( cells[ Index( cx, cz ) ] & kHeadingWall[ arrived ] ) == 0;
		if( straightOpen && rng.Unit01() >= turnBias )
			return arrived;

		return options[ rng.Below( count ) ];
	}

	std::vector< uint8_t > cells;
	int grid = 10;

	int x = 0, z = 0;
	int previousX = 0, previousZ = 0;

	/// The corridor being travelled along, and the one that will be taken out
	/// of the cell being arrived at. See the note on Step.
	int headingIn  = 0;
	int headingOut = 0;

	/// Drives the per-cell wall shade. Drawn from the stream at generation
	/// time so it belongs to the maze rather than to the frame.
	uint32_t seedForWalls = 0;
};
} // namespace

std::unique_ptr< Saver > MakeMaze()
{
	return std::make_unique< Maze >();
}

} // namespace idler
