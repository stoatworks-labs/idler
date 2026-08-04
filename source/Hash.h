#pragma once

#include <cstdint>

/**
    An exact integer hash, and the deterministic stream built on it.

    Every "random" quantity in Idler comes from here: which way a pipe turns at
    a junction, where the maze puts its walls, a star's spawn point, a Mystify
    polygon's bounce frequencies.

    It is an integer hash rather than the usual `fract( sin( x ) * 43758.5453 )`
    because the usual one is transcendental, and its result differs between
    GPUs, between drivers, and between a GPU and a CPU. That matters more here
    than anywhere else in the fleet: 3D Pipes and 3D Maze are **replayed** from
    the seed to reach the frame you asked for (see Savers.h), so a single hash
    disagreeing in its last bit does not shift one pixel -- it sends the pipe
    down a different corridor, and the two machines draw entirely different
    pictures from then on. A composition built on the show laptop and opened on
    the rack machine has to grow the same pipe network.

    `lowbias32` is Chris Wellons' 32-bit integer bijection, chosen for having
    about the lowest avalanche bias of any two-round xorshift-multiply.
*/
namespace idler
{
inline uint32_t Hash32( uint32_t x )
{
	x ^= x >> 16;
	x *= 0x7feb352dU;
	x ^= x >> 15;
	x *= 0x846ca68bU;
	x ^= x >> 16;
	return x;
}

/// Mix two values into one hash. Used as Hash2( index, seed ) so that nudging
/// the seed reshuffles everything rather than rotating the set.
inline uint32_t Hash2( uint32_t a, uint32_t b )
{
	return Hash32( a ^ Hash32( b + 0x9e3779b9U ) );
}

inline uint32_t Hash3( uint32_t a, uint32_t b, uint32_t c )
{
	return Hash32( Hash2( a, b ) ^ Hash32( c + 0x85ebca6bU ) );
}

/// A hash to 0..1.
///
/// Takes the **top 24 bits**. That is the widest slice that converts to a
/// float32 without rounding -- a float32 has a 24-bit significand -- so the
/// conversion is exact and two machines cannot disagree in the last bit.
inline float Unit( uint32_t h )
{
	return static_cast< float >( h >> 8 ) * ( 1.0f / 16777216.0f );
}

/// A hash to -1..1.
inline float Signed( uint32_t h )
{
	return Unit( h ) * 2.0f - 1.0f;
}

/**
    A counter-based random stream.

    The growing savers need a *sequence* of decisions -- turn left, go straight,
    start a new pipe -- and they need the same sequence every time they are
    replayed from the same seed. This is a counter, not a state machine: draw
    number `n` is `Hash2( n, seed )` and depends on nothing that came before it.

    That is the property that makes replay cheap and correct. A conventional
    PRNG carries state, so resuming a replay from a cached checkpoint means
    serialising that state and trusting it; a counter means the checkpoint is
    one integer, and a replay resumed at tick 400 draws exactly the numbers a
    replay from tick 0 would have drawn by the time it got there.
*/
class Random
{
public:
	Random() = default;
	explicit Random( uint32_t seed ) : seed( seed ) {}

	void Reset( uint32_t newSeed )
	{
		seed    = newSeed;
		counter = 0;
	}

	uint32_t Next() { return Hash2( counter++, seed ); }

	/// 0..1.
	float Unit01() { return Unit( Next() ); }

	/// -1..1.
	float Signed11() { return Signed( Next() ); }

	/// A whole number in [0, n). Modulo of a well-mixed 32-bit value: the bias
	/// is under one part in 2^27 for the small n every caller here uses, which
	/// is a smaller effect than the bias in the hash itself.
	int Below( int n )
	{
		return n <= 1 ? 0 : static_cast< int >( Next() % static_cast< uint32_t >( n ) );
	}

	float Range( float lo, float hi ) { return lo + ( hi - lo ) * Unit01(); }

	/// How many numbers have been drawn. That, with the seed, is the whole of
	/// this object's state -- which is what makes a replay checkpoint one
	/// integer rather than a serialised generator.
	uint32_t Counter() const { return counter; }
	void SetCounter( uint32_t c ) { counter = c; }

private:
	uint32_t seed    = 1;
	uint32_t counter = 0;
};

} // namespace idler
