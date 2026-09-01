#include <algorithm>
#include <cstdint>
#include <vector>

#include "../Savers.h"

/**
    3D Maze.

    A first-person walk down brick corridors, taking whatever turning comes up,
    forever -- and the maze is genuinely endless: it is built in chunks around
    the camera as it goes, and the ones it has left behind are dropped. There
    is no edge to reach and no far corner to have seen.

    ## Why it had to be endless

    It used to be one square grid, six to sixteen cells a side, generated once
    and walked for the rest of the clip. Two things followed, and together they
    were the bug people actually reported -- "it isn't going anywhere":

    - **A hundred cells is about a minute.** After that every corridor is one
      the camera has already been down, and since the fog shows two or three
      cells there is nothing to tell you which minute you are watching.
    - **A perfect maze is a tree**, so between any two cells there is exactly
      one route. Every dead end therefore costs a full retrace back down the
      corridor you came up, and one tick in fifteen was a 180-degree turn on
      the spot. Measured on the shipped preset, the camera's net displacement
      after a minute of walking was under three cells. It was pacing.

    Preferring the least-walked exit (see ChooseHeading) was an earlier attempt
    at this and it is still here, because it is what stops the walk circling a
    short loop. But no choice of turning fixes a maze with a hundred cells in
    it, and none of them fixes a tree.

    ## How the maze is endless

    Space is divided into chunks of `chunk` x `chunk` cells. A chunk's walls are
    a **pure function of its chunk coordinates and the seed** -- a depth-first
    backtracker run on that chunk's own random stream -- so a chunk built now
    and the same chunk built again in an hour are identical, and it does not
    matter in what order or how often they get built.

    Two things join them up:

    - **Doorways.** Each shared edge gets two, at positions drawn from a stream
      keyed on the edge itself, so the chunks on either side agree about them
      without either one having to know the other exists.
    - **Braiding.** After the doorways are cut, a chunk goes back over its dead
      ends and opens one more wall on all but one in six of them. That is what
      turns the tree into a graph with loops in it: with somewhere else to go,
      the walk is not forced back down its own approach, and the 180-degree
      turn drops from one tick in fifteen to about one in a hundred and fifty.
      The ones left in are worth keeping -- walking up to a wall is part of what
      this saver is, and with loops around it a dead end now costs one cell
      rather than a retrace of twenty.

    Only the chunks near the camera are kept (`kKeepRadius`); the rest are
    dropped and rebuilt from the seed if the walk ever comes back. Two
    consequences worth stating:

    - The **pass counts go with them**, so a chunk revisited after a long
      absence is unexplored again. That is deliberate -- they are what stops a
      short loop, and nothing needs them to be remembered for an hour -- and it
      is deterministic, because what gets dropped depends only on where the
      walk has been.
    - The camera walks away from the origin for as long as the clip runs. Over
      the whole of `kMaxReplaySteps` -- 69 hours of playback -- it gets about
      450 cells out, where a float32 still resolves 3e-5 of a cell. There is
      nothing to rebase.

    ## Why this one needs replay

    Which corridor the camera is in depends on every junction it has already
    taken. Like 3D Pipes, it is made a pure function of time by `GrowingSaver`:
    the state at time t is the state reached by replaying from the seed. See
    Savers.h.

    A tick is **one cell of travel**, and the alpha inside it slides the camera
    from one cell to the next -- and swings the heading round a corner. That
    turn is the only thing in this saver that is not a straight line, and it is
    eased rather than linear, because a linear heading interpolation whips
    round the corner at a constant rate and reads as a camera on rails rather
    than as walking.

    Generating a chunk draws from **its own** stream, never from the walk's, so
    the walk replays the same whatever the drawing happened to build. That is
    the property `idtest --replay` checks and the reason the fog can decide how
    far the maze is built without changing where the camera goes.

    ## The scene controls, in this saver

    - **Complexity** -- the chunk, 6..16 cells on a side. It is now the scale of
      the maze's structure rather than its size, because it has no size.
    - **Density** -- how often the walk prefers a turn over carrying straight
      on, where both are equally unexplored. Low is long corridors; high is a
      fidget. It cannot buy a retrace: see ChooseHeading.
    - **Size** -- corridor width against wall height, so the corridor can be a
      tunnel or a hall.
    - **Fog** -- how far down the corridor you can see. This is the control
      that matters most: a corridor that ends in a hard-edged wall at the far
      clip plane reads as a bug, and the original faded to black too. It also
      sets how far the maze is built, since there is no point building what the
      fog hides.
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

    ## What gets drawn

    The whole maze cannot be drawn any more, so what is drawn is the cells
    within sight: a disc of radius `DrawRadius` around the camera, which follows
    the fog, minus everything behind the camera's own plane. That last test is
    exact rather than a guess -- a cell's geometry reaches 0.75 of a cell from
    its centre and no perspective projection sees behind its own eye -- so it
    cannot pop anything into view. It is not a visibility test and does not try
    to be one; the fog and the walls do the rest.
*/
namespace idler
{
namespace
{
/// Cells of travel per second. Slower than Pipes: this is a walking pace, and
/// the original's was famously unhurried.
constexpr float kTickRate = 1.6f;

/// How far out, in cells, built maze is kept around the camera. Comfortably
/// beyond any draw radius below, so a chunk being looked at is never dropped
/// out from under the drawing.
constexpr int kKeepRadius = 20;

/// The furthest the maze is ever drawn, in cells. Reached only with the fog
/// off, where the far clip plane used to be what stopped you seeing forever.
constexpr int kMaxDrawRadius = 14;

/// Doorways per shared chunk edge. One would connect them; two keeps the seam
/// from being a bottleneck the walk has to funnel back through.
constexpr int kDoorsPerEdge = 2;

/// One dead end in this many survives the braid. See the note at the top on
/// why they are not all removed.
constexpr int kDeadEndsKept = 6;

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

/// Division and remainder that keep going the same way below zero. The maze
/// runs in every direction from the origin, and C++'s `/` and `%` truncate
/// toward zero -- which would make the chunk two cells wide at the origin and
/// mirror the maze about it.
int FloorDiv( int a, int b )
{
	const int q = a / b;
	return ( a % b != 0 && ( a < 0 ) != ( b < 0 ) ) ? q - 1 : q;
}

int FloorMod( int a, int b )
{
	const int r = a % b;
	return ( r != 0 && ( r < 0 ) != ( b < 0 ) ) ? r + b : r;
}

/// One built square of maze, and how often the walk has left each of its cells
/// along each heading.
struct Chunk
{
	int cx = 0;
	int cz = 0;
	std::vector< uint8_t > cells;
	std::vector< uint32_t > passes;
};

class Maze : public GrowingSaver
{
protected:
	float TickRate( const Settings& ) const override { return kTickRate; }

	uint64_t GrowthKey( const Settings& s ) const override
	{
		return static_cast< uint64_t >( s.seed ) |
		       ( static_cast< uint64_t >( ChunkSize( s ) ) << 32 ) |
		       ( static_cast< uint64_t >( s.density * 255.0f ) << 40 );
	}

	void ResetState( const Settings& s, Random& rng ) override
	{
		chunkSize = ChunkSize( s );
		seed      = s.seed;
		chunks.clear();

		// Drawn from the walk's stream so it belongs to the maze rather than to
		// the frame, exactly as it did when there was one grid to seed.
		seedForWalls = rng.Next();

		// The origin, because an endless maze has no middle to start in and no
		// corner to start from. The seed decides what is there.
		x = 0;
		z = 0;

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

		// Mark the way out before taking it. This is the whole of what stops
		// the walk circling one loop for ever -- see ChooseHeading.
		MarkPass( x, z, headingIn );

		x += kHeadingDX[ headingIn ];
		z += kHeadingDZ[ headingIn ];

		const float turnBias = 0.1f + s.density * 0.6f;
		headingOut           = ChooseHeading( x, z, headingIn, turnBias, rng );

		Forget();
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
		// The maze itself: the cells within sight of the camera, built on
		// demand. See the note at the top on what gets drawn.
		//-------------------------------------------------------------------
		const float thickness = cell * 0.06f;
		const float half      = cell * 0.5f;

		const int radius = DrawRadius( s, scene, cell );

		// The disc is measured in whole cells rather than in world units, so
		// the count of what gets drawn is integer arithmetic. The demo is a
		// hand port checked against this one's triangle count (see
		// demo/tools/check_geometry.mjs), and a float comparison deciding
		// whether a cell is in or out is a difference between float32 here and
		// double there waiting to happen.
		const int reach = ( radius + 1 ) * ( radius + 1 );

		// A cell's geometry reaches 0.75 of a cell from its centre -- half a
		// cell each way, plus the wall thickness, corner to corner -- and no
		// perspective projection sees behind its own eye. So a centre further
		// back than that cannot contribute a visible triangle, whatever the
		// field of view is set to.
		const float behind = -0.8f * cell;

		// One row further out than the disc, because only the north and west
		// walls of a cell are drawn: the far side of the last row of floor is
		// the next row's north wall. Every other south or east wall is some
		// other cell's north or west, and drawing both leaves two coplanar
		// faces fighting for the same depth.
		for( int dz = -radius; dz <= radius + 1; ++dz )
			for( int dx = -radius; dx <= radius + 1; ++dx )
			{
				if( dx * dx + dz * dz > reach )
					continue;

				const int cx = x + dx;
				const int cz = z + dz;

				const Vec3 centre = CellCentre( cx, cz, cell );
				const Vec3 offset{ centre.x - eye.x, 0.0f, centre.z - eye.z };

				if( Dot( offset, forward ) < behind )
					continue;

				const uint8_t walls = Walls( cx, cz );

				// Each face takes its shade from the cell hash. This is what
				// stands in for the brick texture -- see the note at the top.
				const float shade = 0.72f + Unit( Hash3( static_cast< uint32_t >( cx ),
				                                         static_cast< uint32_t >( cz ), seedForWalls ) ) *
				                              0.28f * ( 0.2f + s.variation );

				// The colour fan wraps every chunk. There is no total to spread
				// a hue across when the maze has no end, and a fan that wrapped
				// on nothing in particular would drift as the camera walked.
				const int fanX     = FloorMod( cx, chunkSize );
				const int fanZ     = FloorMod( cz, chunkSize );
				const int fanIndex = fanX + fanZ * chunkSize;
				const int fanCount = chunkSize * chunkSize;

				// The classic maze was red brick with a grey floor.
				const Vec3 brick{ 0.78f * shade, 0.30f * shade, 0.22f * shade };
				const Vec4 wallColour = s.Colour( brick, fanIndex, fanCount );

				const Vec3 mid{ centre.x, height * 0.5f, centre.z };
				const uint32_t cellKey = Hash2( static_cast< uint32_t >( cx ),
				                                static_cast< uint32_t >( cz ) ) ^ seedForWalls;

				if( walls & kNorth )
					AddBrickWall( scene.mesh, { mid.x, mid.y, mid.z - half }, { half, height * 0.5f, thickness },
					              true, wallColour, cellKey ^ 0x11U, s.variation );
				if( walls & kWest )
					AddBrickWall( scene.mesh, { mid.x - half, mid.y, mid.z }, { thickness, height * 0.5f, half },
					              false, wallColour, cellKey ^ 0x22U, s.variation );

				// The extra row carries walls only; it has no floor of its own
				// and drawing one would put a lip beyond the fog.
				if( dx > radius || dz > radius )
					continue;

				const Vec3 floorGrey{ 0.28f * shade, 0.28f * shade, 0.30f * shade };
				const Vec4 floorColour = s.Colour( floorGrey, fanIndex, fanCount );
				scene.mesh.AddBox( { centre.x, -thickness, centre.z }, { half, thickness, half }, floorColour );

				const Vec3 ceilingGrey{ 0.16f * shade, 0.16f * shade, 0.19f * shade };
				const Vec4 ceilingColour = s.Colour( ceilingGrey, fanIndex, fanCount );
				scene.mesh.AddBox( { centre.x, height + thickness, centre.z }, { half, thickness, half }, ceilingColour );
			}
	}

private:
	static int ChunkSize( const Settings& s ) { return 6 + static_cast< int >( s.complexity * 10.0f + 0.5f ); }

	/// How far out to build and draw, in cells. There is nothing to be gained
	/// by drawing what the fog has already taken to black, so this follows it;
	/// with the fog off it is the flat ceiling, which is what stops an endless
	/// maze being an endless amount of geometry.
	static int DrawRadius( const Settings& s, const Scene& scene, float cell )
	{
		const float sight = ( s.fog > 0.001f ) ? scene.fogEnd : static_cast< float >( kMaxDrawRadius ) * cell;
		const int wanted  = static_cast< int >( sight / cell ) + 2;
		return std::min( kMaxDrawRadius, std::max( 4, wanted ) );
	}

	Vec3 CellCentre( int cx, int cz, float cell ) const
	{
		return { ( static_cast< float >( cx ) + 0.5f ) * cell, 0.0f,
		         ( static_cast< float >( cz ) + 0.5f ) * cell };
	}

	//-----------------------------------------------------------------------
	// The chunks.
	//-----------------------------------------------------------------------

	/// The chunk holding `(cx, cz)`, built if it is not there.
	///
	/// The returned reference is good until the next call -- building appends
	/// to the vector -- which is why every caller below takes what it wants
	/// from it and does not hold on.
	Chunk& ChunkFor( int cx, int cz )
	{
		const int wantX = FloorDiv( cx, chunkSize );
		const int wantZ = FloorDiv( cz, chunkSize );

		for( Chunk& c : chunks )
			if( c.cx == wantX && c.cz == wantZ )
				return c;

		chunks.push_back( Generate( wantX, wantZ ) );
		return chunks.back();
	}

	size_t IndexIn( int cx, int cz ) const
	{
		return static_cast< size_t >( FloorMod( cz, chunkSize ) ) * static_cast< size_t >( chunkSize ) +
		       static_cast< size_t >( FloorMod( cx, chunkSize ) );
	}

	uint8_t Walls( int cx, int cz ) { return ChunkFor( cx, cz ).cells[ IndexIn( cx, cz ) ]; }

	/// How many times the walk has left `(cx, cz)` along `heading`.
	uint32_t Passes( int cx, int cz, int heading )
	{
		return ChunkFor( cx, cz ).passes[ IndexIn( cx, cz ) * 4U + static_cast< size_t >( heading ) ];
	}

	void MarkPass( int cx, int cz, int heading )
	{
		++ChunkFor( cx, cz ).passes[ IndexIn( cx, cz ) * 4U + static_cast< size_t >( heading ) ];
	}

	/// Drop the chunks the walk has left behind.
	///
	/// What goes depends only on where the camera is, so a replay drops exactly
	/// what a live run dropped. It has to: the pass counts go with the chunk,
	/// and they are what the walk steers by.
	void Forget()
	{
		for( size_t i = chunks.size(); i-- > 0; )
		{
			const int lowX  = chunks[ i ].cx * chunkSize;
			const int lowZ  = chunks[ i ].cz * chunkSize;
			const int highX = lowX + chunkSize - 1;
			const int highZ = lowZ + chunkSize - 1;

			if( highX < x - kKeepRadius || lowX > x + kKeepRadius ||
			    highZ < z - kKeepRadius || lowZ > z + kKeepRadius )
			{
				chunks[ i ] = chunks.back();
				chunks.pop_back();
			}
		}
	}

	/**
	    The doorways through one shared edge.

	    Keyed on the **edge** rather than on either chunk, so the two sides
	    agree without consulting each other: the edge below `(cx, cz)`'s west
	    side is the edge above `(cx - 1, cz)`'s east side, and both name it
	    `(cx, cz)`. Get this wrong in either direction and the maze grows a wall
	    with a door on one face and none on the other, which shows up as the
	    camera walking through a wall it can still see.
	*/
	void EdgeDoors( int cx, int cz, bool vertical, int doors[ kDoorsPerEdge ] ) const
	{
		Random rng( Hash3( static_cast< uint32_t >( cx ), static_cast< uint32_t >( cz ),
		                   seed ^ ( vertical ? 0x5EED1A17U : 0x5EED2B26U ) ) );
		for( int i = 0; i < kDoorsPerEdge; ++i )
			doors[ i ] = rng.Below( chunkSize );
	}

	/**
	    One chunk of maze: a perfect maze by depth-first backtracking, the
	    doorways to its four neighbours, then the braid.

	    **Its own random stream.** Nothing here draws from the walk's, because
	    the walk has to replay identically however many chunks the drawing
	    happened to build first -- and how many that is depends on the fog,
	    which is not part of the growth key and must never be.

	    Recursion would be the obvious way to write the carve and would blow the
	    stack on a large chunk -- the corridor it cuts is a single path that can
	    reach every cell, so the recursion depth is the cell count.
	*/
	Chunk Generate( int chunkX, int chunkZ ) const
	{
		Chunk chunk;
		chunk.cx = chunkX;
		chunk.cz = chunkZ;

		const int side     = chunkSize;
		const size_t total = static_cast< size_t >( side ) * static_cast< size_t >( side );
		chunk.cells.assign( total, static_cast< uint8_t >( kNorth | kSouth | kWest | kEast ) );
		chunk.passes.assign( total * 4U, 0U );

		Random rng( Hash3( static_cast< uint32_t >( chunkX ), static_cast< uint32_t >( chunkZ ), seed ) );

		auto at = [ side ]( int cx, int cz ) {
			return static_cast< size_t >( cz ) * static_cast< size_t >( side ) + static_cast< size_t >( cx );
		};

		std::vector< uint8_t > visited( total, 0 );
		std::vector< int > stack;
		stack.reserve( total );

		int cx = rng.Below( side );
		int cz = rng.Below( side );
		visited[ at( cx, cz ) ] = 1;
		stack.push_back( static_cast< int >( at( cx, cz ) ) );

		while( !stack.empty() )
		{
			const int here = stack.back();
			cx             = here % side;
			cz             = here / side;

			int options[ 4 ];
			int count = 0;
			for( int h = 0; h < 4; ++h )
			{
				const int nx = cx + kHeadingDX[ h ];
				const int nz = cz + kHeadingDZ[ h ];
				if( nx < 0 || nz < 0 || nx >= side || nz >= side )
					continue;
				if( visited[ at( nx, nz ) ] )
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
			// corridor you can walk into and not out of, and the walk tests the
			// cell it is standing in -- so a one-sided wall shows up as the
			// camera walking through a wall it can still see.
			chunk.cells[ at( cx, cz ) ] &= static_cast< uint8_t >( ~kHeadingWall[ h ] );
			chunk.cells[ at( nx, nz ) ] &= static_cast< uint8_t >( ~kHeadingWall[ ( h + 2 ) % 4 ] );

			visited[ at( nx, nz ) ] = 1;
			stack.push_back( static_cast< int >( at( nx, nz ) ) );
		}

		//-------------------------------------------------------------------
		// The doorways, which is what makes the chunks one maze rather than a
		// tiling of separate ones. Each edge is opened from this side only --
		// the chunk on the other side opens its own half from the same numbers.
		//-------------------------------------------------------------------
		int doors[ kDoorsPerEdge ];

		EdgeDoors( chunkX, chunkZ, true, doors );
		for( int i = 0; i < kDoorsPerEdge; ++i )
			chunk.cells[ at( 0, doors[ i ] ) ] &= static_cast< uint8_t >( ~kWest );

		EdgeDoors( chunkX + 1, chunkZ, true, doors );
		for( int i = 0; i < kDoorsPerEdge; ++i )
			chunk.cells[ at( side - 1, doors[ i ] ) ] &= static_cast< uint8_t >( ~kEast );

		EdgeDoors( chunkX, chunkZ, false, doors );
		for( int i = 0; i < kDoorsPerEdge; ++i )
			chunk.cells[ at( doors[ i ], 0 ) ] &= static_cast< uint8_t >( ~kNorth );

		EdgeDoors( chunkX, chunkZ + 1, false, doors );
		for( int i = 0; i < kDoorsPerEdge; ++i )
			chunk.cells[ at( doors[ i ], side - 1 ) ] &= static_cast< uint8_t >( ~kSouth );

		//-------------------------------------------------------------------
		// The braid: open one more wall on most dead ends, which is what turns
		// the tree into something with loops in it. See the note at the top on
		// why this matters more than any turning rule can.
		//
		// Only interior walls, because opening one on the boundary would need
		// the agreement of a chunk that may not exist yet. A dead end always
		// has an interior wall to give: a corner cell has two interior
		// directions and can only be a dead end if at least one of them is
		// still shut.
		//-------------------------------------------------------------------
		for( int bz = 0; bz < side; ++bz )
			for( int bx = 0; bx < side; ++bx )
			{
				int open = 0;
				for( int h = 0; h < 4; ++h )
					if( ( chunk.cells[ at( bx, bz ) ] & kHeadingWall[ h ] ) == 0 )
						++open;
				if( open != 1 )
					continue;

				if( rng.Below( kDeadEndsKept ) == 0 )
					continue;

				int options[ 4 ];
				int count = 0;
				for( int h = 0; h < 4; ++h )
				{
					const int nx = bx + kHeadingDX[ h ];
					const int nz = bz + kHeadingDZ[ h ];
					if( nx < 0 || nz < 0 || nx >= side || nz >= side )
						continue;
					if( ( chunk.cells[ at( bx, bz ) ] & kHeadingWall[ h ] ) == 0 )
						continue;
					options[ count++ ] = h;
				}

				if( count == 0 )
					continue;

				const int h  = options[ rng.Below( count ) ];
				const int nx = bx + kHeadingDX[ h ];
				const int nz = bz + kHeadingDZ[ h ];

				chunk.cells[ at( bx, bz ) ] &= static_cast< uint8_t >( ~kHeadingWall[ h ] );
				chunk.cells[ at( nx, nz ) ] &= static_cast< uint8_t >( ~kHeadingWall[ ( h + 2 ) % 4 ] );
			}

		return chunk;
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
	int FirstOpenHeading( int cx, int cz, Random& rng )
	{
		const uint8_t walls = Walls( cx, cz );

		int options[ 4 ];
		int count = 0;
		for( int h = 0; h < 4; ++h )
			if( ( walls & kHeadingWall[ h ] ) == 0 )
				options[ count++ ] = h;

		// A perfect maze has no fully walled cell, so `count` is never zero --
		// but the fallback costs a line and the alternative is reading past the
		// end of the array if that ever stops being true.
		return count == 0 ? 0 : options[ rng.Below( count ) ];
	}

	/**
	    Where to go on leaving `(cx, cz)`, having arrived along `arrived`.

	    Reversing is a last resort, which is what makes the walk read as
	    exploring rather than as pacing up and down one corridor.

	    **The choice is made among the exits this walk has used LEAST.** With
	    the maze braided (see Generate) most junctions offer a way round rather
	    than a way back, and this is what stops the walk taking the same way
	    round for ever: a loop it has been round once is no longer the least
	    walked thing on offer, so the next junction sends it somewhere new.

	    Counting **exits** rather than **cells** matters: with cell counts the
	    walk can sit between two dead ends bouncing off each in turn -- each
	    bounce makes the other the less-visited of the two -- and never spend
	    the visit that would make the third way out the least visited. Per-exit
	    counts cannot do that, because the bounce spends the count on the way it
	    just went.

	    `turnBias` still decides between exits that are equally unexplored,
	    which is what it does on fresh ground -- so Density still lengthens the
	    corridors, it just cannot buy a retrace with them.
	*/
	int ChooseHeading( int cx, int cz, int arrived, float turnBias, Random& rng )
	{
		const uint8_t walls = Walls( cx, cz );

		int options[ 4 ];
		int count         = 0;
		const int reverse = ( arrived + 2 ) % 4;
		uint32_t fewest   = UINT32_MAX;

		for( int h = 0; h < 4; ++h )
		{
			if( h == reverse )
				continue;
			if( ( walls & kHeadingWall[ h ] ) != 0 )
				continue;

			const uint32_t used = Passes( cx, cz, h );
			if( used < fewest )
			{
				fewest = used;
				count  = 0;
			}
			if( used == fewest )
				options[ count++ ] = h;
		}

		if( count == 0 )
		{
			// One of the dead ends the braid left in. Turn round -- and draw a
			// number anyway, so the number of draws does not depend on the
			// branch taken. A replay that consumed a different count here would
			// diverge from a live run at the first dead end, which is the sort
			// of bug that shows up as "the maze is different after you scrub".
			(void)rng.Next();
			return reverse;
		}

		// Straight on, if straight on is one of the least-walked ways out.
		// Testing membership rather than merely "is the wall open" is the
		// whole point: an open corridor the walk has already been down is not
		// a candidate while an unwalked one is on offer.
		bool straightOpen = false;
		for( int i = 0; i < count; ++i )
			straightOpen = straightOpen || ( options[ i ] == arrived );

		if( straightOpen && rng.Unit01() >= turnBias )
			return arrived;

		return options[ rng.Below( count ) ];
	}

	/// The built maze near the camera, and nothing else. Bounded by `Forget`:
	/// at the smallest chunk and the widest keep radius this is a few dozen
	/// chunks of a few hundred bytes, whatever the clock says.
	std::vector< Chunk > chunks;

	int chunkSize = 10;
	uint32_t seed = 1;

	int x = 0, z = 0;
	int previousX = 0, previousZ = 0;

	/// The corridor being travelled along, and the one that will be taken out
	/// of the cell being arrived at. See the note on Step.
	int headingIn  = 0;
	int headingOut = 0;

	/// Drives the per-cell wall shade. Drawn from the walk's stream at reset so
	/// it belongs to the maze rather than to the frame.
	uint32_t seedForWalls = 0;
};
} // namespace

std::unique_ptr< Saver > MakeMaze()
{
	return std::make_unique< Maze >();
}

} // namespace idler
