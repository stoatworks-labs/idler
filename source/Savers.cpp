#include "Savers.h"

#include <algorithm>
#include <cmath>

#include "Diag.h"

namespace idler
{

void GrowingSaver::Build( const Settings& settings, Scene& scene )
{
	const float rate = std::max( 0.01f, TickRate( settings ) );

	// Negative time is reachable: Phase is an offset and Speed can be zero, so
	// a host clock that has not started yet plus a phase of nothing lands
	// slightly below zero. Clamp rather than let floor() run the tick index
	// negative, which would rebuild every frame.
	const float t          = std::max( 0.0f, settings.time );
	const double exactTick = static_cast< double >( t ) * static_cast< double >( rate );

	const int wantedTick = static_cast< int >(
		std::min( exactTick, static_cast< double >( kMaxReplaySteps ) ) );
	const float alpha = static_cast< float >( exactTick - std::floor( exactTick ) );

	const uint64_t key = GrowthKey( settings );

	// The cache is an optimisation and must not change the answer. It is used
	// ONLY to skip forward within one growth key; anything else starts again.
	const bool mustRebuild = ( cachedTick < 0 ) || ( key != cachedKey ) || ( wantedTick < cachedTick );
	if( mustRebuild )
	{
		rng.Reset( settings.seed );
		ResetState( settings, rng );
		cachedKey  = key;
		cachedTick = 0;
	}

	if( exactTick > static_cast< double >( kMaxReplaySteps ) )
	{
		static bool warned = false;
		if( !warned )
		{
			warned = true;
			diag::warn( "replay capped at " + std::to_string( kMaxReplaySteps ) +
			            " ticks; the picture will stop advancing. Time=" + std::to_string( settings.time ) );
		}
	}

	while( cachedTick < wantedTick )
	{
		Step( settings, rng );
		++cachedTick;
	}

	Draw( settings, alpha, scene );
}

std::unique_ptr< Saver > MakeSaver( SaverKind kind )
{
	switch( kind )
	{
	case SaverKind::Mystify:       return MakeMystify();
	case SaverKind::Beziers:       return MakeBeziers();
	case SaverKind::Curves:        return MakeCurves();
	case SaverKind::FlyingWindows: return MakeFlyingWindows();
	case SaverKind::Starfield:     return MakeStarfield();
	case SaverKind::Marquee:       return MakeMarquee();
	case SaverKind::Maze:          return MakeMaze();
	case SaverKind::Pipes:         return MakePipes();
	case SaverKind::FlyingObjects: return MakeFlyingObjects();
	case SaverKind::FlowerBox:     return MakeFlowerBox();
	case SaverKind::Text3D:        return MakeText3D();
	default:                       return MakeMystify();
	}
}

void SetFlatCamera( const Settings& settings, Scene& scene )
{
	const float halfWidth = std::max( 0.01f, settings.aspect );
	scene.proj      = Mat4::Ortho( -halfWidth, halfWidth, -1.0f, 1.0f, -1.0f, 1.0f );
	scene.view      = Mat4::Identity();
	scene.depthTest = false;
	scene.shading   = Shading::Flat;
}

Vec2 BouncePoint( uint32_t seed, int index, float time, float rate, float aspect, float margin )
{
	const uint32_t hx = Hash3( static_cast< uint32_t >( index ), seed, 0x1111U );
	const uint32_t hy = Hash3( static_cast< uint32_t >( index ), seed, 0x2222U );

	// Rates spread over roughly 3:2 rather than 1:2. Anything wider and the
	// slow axis is visibly slower than the fast one, which reads as the point
	// sliding along a wall rather than travelling across the box; anything
	// narrower and every point in the set moves at the same speed and the
	// polygon keeps its shape instead of writhing.
	const float rateX = rate * ( 0.8f + Unit( hx ) * 0.5f );
	const float rateY = rate * ( 0.8f + Unit( hy ) * 0.5f );

	const float phaseX = Unit( Hash3( static_cast< uint32_t >( index ), seed, 0x3333U ) );
	const float phaseY = Unit( Hash3( static_cast< uint32_t >( index ), seed, 0x4444U ) );

	const float halfWidth  = std::max( 0.05f, aspect - margin );
	const float halfHeight = std::max( 0.05f, 1.0f - margin );

	return { ( TriangleWave( time * rateX + phaseX ) * 2.0f - 1.0f ) * halfWidth,
	         ( TriangleWave( time * rateY + phaseY ) * 2.0f - 1.0f ) * halfHeight };
}

Flight FlyingPoint( uint32_t seed, int index, float time, float rate )
{
	const uint32_t h = Hash2( static_cast< uint32_t >( index ), seed );

	// A fixed direction from the origin, and a starting offset along the run so
	// the field is already full at time zero.
	const float angle  = Unit( h ) * kTwoPi;
	const float radius = 0.15f + Unit( Hash2( h, 0x5555U ) ) * 0.85f;
	const float offset = Unit( Hash2( h, 0x6666U ) );

	// Each object gets its own speed, but over a narrow range. A wide spread
	// reads as objects at different depths moving at different speeds, which is
	// physically backwards -- in a real flight everything shares one velocity
	// and the perspective divide supplies the difference.
	const float speed = rate * ( 0.85f + Unit( Hash2( h, 0x7777U ) ) * 0.3f );

	float age = time * speed + offset;
	age -= std::floor( age );

	// Z from far to near. Never reaches zero: at the near plane the scale
	// diverges, and an object of infinite size for one frame is a full-screen
	// flash.
	constexpr float kFar  = 1.0f;
	constexpr float kNear = 0.06f;
	const float z         = kFar + ( kNear - kFar ) * age;

	const float scale = 0.35f / z;

	Flight flight;
	flight.screen = { std::cos( angle ) * radius * scale, std::sin( angle ) * radius * scale };
	flight.scale  = scale;
	flight.age    = age;
	return flight;
}

int CountFromDensity( float density, int lo, int hi )
{
	const float clamped = std::max( 0.0f, std::min( 1.0f, density ) );
	const float curved  = clamped * clamped;
	const int span      = hi - lo;
	return lo + static_cast< int >( curved * static_cast< float >( span ) + 0.5f );
}

} // namespace idler
