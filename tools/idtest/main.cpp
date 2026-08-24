/**
    idtest -- the offline harness.

    It drives **the real plugin class** through the real FFGL sequence in a
    headless core-profile context. Not a reimplementation of the savers and not
    a preview: the thing under test is `IdlerPlugin`, compiled from the same
    objects that go into the bundles, and every number below comes out of a
    frame it actually rendered.

        --out PATH        render one frame
        --sheet PATH      a contact sheet of all eleven savers
        --list            parameters, with their types and defaults
        --geometry        the mesh each saver builds, checked
        --replay          the replay cache does not change the answer
        --coverage        every saver draws something, at several times
        --effect          the effect variant over a test clip

    ## The three tests that matter, and what each one is blind to

    **`--replay`** is the one that guards this plugin's central claim. 3D Pipes
    and 3D Maze reach the frame you asked for by replaying from the seed, with
    a cache to skip forward. The cache is an optimisation and must never change
    the answer -- so this renders a frame cold, renders it again after running
    the clock up to it, and requires the two to be **byte-identical**. A cache
    that is merely nearly right passes every visual check and fails here.

    It is blind to a saver being wrong in a way that is consistently wrong.

    **`--geometry`** measures the mesh rather than the picture: triangle
    counts, bounding boxes, and that every normal is unit length. Unit normals
    are worth checking on their own because a zero-length one is a NaN once
    normalised, and a single NaN normal poisons the lighting for the whole draw
    call rather than for one triangle -- so the symptom is a saver that goes
    black, with nothing to say why.

    It is blind to anything that goes wrong after the mesh: the camera, the
    shading, the compositing.

    **`--coverage`** is the crude one and it earns its place. Every saver, at
    several times, must put a non-trivial number of lit pixels on the frame. It
    exists because the most likely way to break one of eleven savers is to make
    it draw *nothing* -- an empty mesh, a camera looking the wrong way, a
    degenerate scale -- and that failure is invisible in a repo where the
    default saver still works.

    None of them catches a dead control. See `tools/sweep.py`.
*/

#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <zlib.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "Controls.h"
#include "Idler.h"
#include "ofx/Raster.h"
#include "Savers.h"

using namespace idler;

namespace
{
//---------------------------------------------------------------------------
// A PNG writer. zlib ships with the OS, so this is a few chunk headers and a
// CRC rather than a dependency.
//---------------------------------------------------------------------------
void putU32( std::vector< unsigned char >& out, uint32_t value )
{
	out.push_back( static_cast< unsigned char >( value >> 24 ) );
	out.push_back( static_cast< unsigned char >( value >> 16 ) );
	out.push_back( static_cast< unsigned char >( value >> 8 ) );
	out.push_back( static_cast< unsigned char >( value ) );
}

void putChunk( std::vector< unsigned char >& out, const char* type, const std::vector< unsigned char >& data )
{
	putU32( out, static_cast< uint32_t >( data.size() ) );
	const size_t start = out.size();
	out.insert( out.end(), type, type + 4 );
	out.insert( out.end(), data.begin(), data.end() );
	uLong crc = crc32( 0L, Z_NULL, 0 );
	crc       = crc32( crc, out.data() + start, static_cast< uInt >( 4 + data.size() ) );
	putU32( out, static_cast< uint32_t >( crc ) );
}

bool writePng( const std::string& path, int width, int height, const std::vector< unsigned char >& rgba )
{
	std::vector< unsigned char > raw;
	raw.reserve( static_cast< size_t >( height ) * ( 1 + static_cast< size_t >( width ) * 4 ) );
	for( int y = 0; y < height; ++y )
	{
		raw.push_back( 0 );// filter: none
		const unsigned char* row = rgba.data() + static_cast< size_t >( y ) * width * 4;
		raw.insert( raw.end(), row, row + static_cast< size_t >( width ) * 4 );
	}

	uLongf compressedSize = compressBound( static_cast< uLong >( raw.size() ) );
	std::vector< unsigned char > compressed( compressedSize );
	if( compress2( compressed.data(), &compressedSize, raw.data(), static_cast< uLong >( raw.size() ), 6 ) != Z_OK )
		return false;
	compressed.resize( compressedSize );

	std::vector< unsigned char > png = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };

	std::vector< unsigned char > ihdr;
	putU32( ihdr, static_cast< uint32_t >( width ) );
	putU32( ihdr, static_cast< uint32_t >( height ) );
	ihdr.push_back( 8 );// bit depth
	ihdr.push_back( 6 );// truecolour with alpha
	ihdr.push_back( 0 );
	ihdr.push_back( 0 );
	ihdr.push_back( 0 );
	putChunk( png, "IHDR", ihdr );
	putChunk( png, "IDAT", compressed );
	putChunk( png, "IEND", {} );

	FILE* file = fopen( path.c_str(), "wb" );
	if( file == nullptr )
		return false;
	const size_t written = fwrite( png.data(), 1, png.size(), file );
	fclose( file );
	return written == png.size();
}

//---------------------------------------------------------------------------
// GL plumbing.
//---------------------------------------------------------------------------
CGLContextObj createContext()
{
	const CGLPixelFormatAttribute accelerated[] = {
		kCGLPFAOpenGLProfile, static_cast< CGLPixelFormatAttribute >( kCGLOGLPVersion_GL4_Core ),
		kCGLPFAAccelerated,
		kCGLPFAColorSize, static_cast< CGLPixelFormatAttribute >( 24 ),
		kCGLPFAAlphaSize, static_cast< CGLPixelFormatAttribute >( 8 ),
		static_cast< CGLPixelFormatAttribute >( 0 )
	};
	const CGLPixelFormatAttribute software[] = {
		kCGLPFAOpenGLProfile, static_cast< CGLPixelFormatAttribute >( kCGLOGLPVersion_GL4_Core ),
		kCGLPFAColorSize, static_cast< CGLPixelFormatAttribute >( 24 ),
		kCGLPFAAlphaSize, static_cast< CGLPixelFormatAttribute >( 8 ),
		static_cast< CGLPixelFormatAttribute >( 0 )
	};

	CGLPixelFormatObj format = nullptr;
	GLint formatCount        = 0;
	if( CGLChoosePixelFormat( accelerated, &format, &formatCount ) != kCGLNoError || format == nullptr )
	{
		if( CGLChoosePixelFormat( software, &format, &formatCount ) != kCGLNoError || format == nullptr )
			return nullptr;
	}

	CGLContextObj context = nullptr;
	const CGLError error  = CGLCreateContext( format, nullptr, &context );
	CGLDestroyPixelFormat( format );
	if( error != kCGLNoError )
		return nullptr;

	CGLSetCurrentContext( context );
	return context;
}

struct Surface
{
	GLuint texture = 0;
	GLuint fbo     = 0;
	int width      = 0;
	int height     = 0;
};

Surface makeSurface( int width, int height )
{
	Surface surface;
	surface.width  = width;
	surface.height = height;

	glGenTextures( 1, &surface.texture );
	glBindTexture( GL_TEXTURE_2D, surface.texture );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	glBindTexture( GL_TEXTURE_2D, 0 );

	glGenFramebuffers( 1, &surface.fbo );
	glBindFramebuffer( GL_FRAMEBUFFER, surface.fbo );
	glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, surface.texture, 0 );
	return surface;
}

void releaseSurface( Surface& surface )
{
	if( surface.fbo != 0 )
		glDeleteFramebuffers( 1, &surface.fbo );
	if( surface.texture != 0 )
		glDeleteTextures( 1, &surface.texture );
	surface = Surface();
}

/// Straight out of GL, bottom row first.
std::vector< unsigned char > readBytes( const Surface& surface )
{
	std::vector< unsigned char > pixels( static_cast< size_t >( surface.width ) * surface.height * 4 );
	glBindFramebuffer( GL_FRAMEBUFFER, surface.fbo );
	glPixelStorei( GL_PACK_ALIGNMENT, 1 );
	glReadPixels( 0, 0, surface.width, surface.height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data() );
	return pixels;
}

std::vector< unsigned char > flipRows( const std::vector< unsigned char >& image, int width, int height )
{
	std::vector< unsigned char > flipped( image.size() );
	const size_t stride = static_cast< size_t >( width ) * 4;
	for( int y = 0; y < height; ++y )
		std::memcpy( flipped.data() + static_cast< size_t >( y ) * stride,
		             image.data() + static_cast< size_t >( height - 1 - y ) * stride, stride );
	return flipped;
}

/// A test clip for the effect: coloured quadrants over a gradient, so that a
/// mask mode getting its geometry or its UV flip wrong is obvious rather than
/// merely plausible.
GLuint makeTestClip( int width, int height )
{
	std::vector< unsigned char > pixels( static_cast< size_t >( width ) * height * 4 );
	for( int y = 0; y < height; ++y )
		for( int x = 0; x < width; ++x )
		{
			const float u = static_cast< float >( x ) / static_cast< float >( width );
			const float v = static_cast< float >( y ) / static_cast< float >( height );

			unsigned char* p = &pixels[ ( static_cast< size_t >( y ) * width + x ) * 4 ];
			p[ 0 ] = static_cast< unsigned char >( ( u < 0.5f ? 220.0f : 40.0f ) * ( 0.4f + 0.6f * v ) );
			p[ 1 ] = static_cast< unsigned char >( ( v < 0.5f ? 200.0f : 60.0f ) * ( 0.4f + 0.6f * u ) );
			p[ 2 ] = static_cast< unsigned char >( 255.0f * ( 0.3f + 0.7f * ( 1.0f - v ) ) );
			p[ 3 ] = 255;
		}

	GLuint texture = 0;
	glGenTextures( 1, &texture );
	glBindTexture( GL_TEXTURE_2D, texture );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data() );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glBindTexture( GL_TEXTURE_2D, 0 );
	return texture;
}

//---------------------------------------------------------------------------
// Parameters by name.
//---------------------------------------------------------------------------
std::map< std::string, unsigned int > parameterIndex( IdlerPlugin& plugin )
{
	std::map< std::string, unsigned int > byName;
	for( unsigned int i = 0; i < plugin.GetNumParams(); ++i )
	{
		const char* name = plugin.GetParamName( i );
		if( name != nullptr )
			byName[ name ] = i;
	}
	return byName;
}

bool applySetting( IdlerPlugin& plugin, const std::string& assignment )
{
	const size_t equals = assignment.find( '=' );
	if( equals == std::string::npos )
	{
		fprintf( stderr, "--set wants Name=value, got '%s'\n", assignment.c_str() );
		return false;
	}

	const std::string name  = assignment.substr( 0, equals );
	const std::string value = assignment.substr( equals + 1 );

	const std::map< std::string, unsigned int > byName = parameterIndex( plugin );
	const auto found                                   = byName.find( name );
	if( found == byName.end() )
	{
		fprintf( stderr, "no parameter called '%s'\n", name.c_str() );
		return false;
	}

	plugin.SetFloatParameter( found->second, std::stof( value ) );
	return true;
}

//---------------------------------------------------------------------------
// Rendering
//---------------------------------------------------------------------------
void render( IdlerPlugin& plugin, const Surface& surface, GLuint input = 0 )
{
	// A synthetic spectrum, written the way the host writes one. Without it the
	// two Audio knobs measurably do nothing offline and the sweep would report
	// them dead. A fixed shape rather than anything time-driven, so renders
	// stay reproducible: bass-heavy like programme material.
	for( unsigned int bin = 0; bin < kAudioBins; ++bin )
	{
		const float across = static_cast< float >( bin ) / static_cast< float >( kAudioBins - 1 );
		const float level  = 0.7f * ( 1.0f - across ) * ( 1.0f - across ) +
		                    0.2f * ( 0.5f + 0.5f * std::sin( 25.0f * across ) );
		plugin.SetParamElementValue( PT_AUDIO, bin, level );
	}

	glBindFramebuffer( GL_FRAMEBUFFER, surface.fbo );
	glViewport( 0, 0, surface.width, surface.height );
	glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
	glClear( GL_COLOR_BUFFER_BIT );

	plugin.Render( surface.width, surface.height, input, 1.0f, 1.0f, surface.fbo );
	glFinish();
}

bool prepare( IdlerPlugin& plugin, int width, int height )
{
	FFGLViewportStruct viewport{};
	viewport.x      = 0;
	viewport.y      = 0;
	viewport.width  = static_cast< unsigned int >( width );
	viewport.height = static_cast< unsigned int >( height );

	if( plugin.InitGL( &viewport ) != FF_SUCCESS )
	{
		fprintf( stderr, "InitGL failed -- see the log\n" );
		return false;
	}
	return true;
}

//---------------------------------------------------------------------------
// Measuring
//---------------------------------------------------------------------------

/// Fraction of pixels with any visible content, and the mean luminance of
/// those that have.
struct Coverage
{
	double litFraction = 0.0;
	double meanLuma    = 0.0;
};

Coverage measure( const std::vector< unsigned char >& pixels )
{
	size_t lit      = 0;
	double luminance = 0.0;
	const size_t count = pixels.size() / 4;

	for( size_t i = 0; i < count; ++i )
	{
		const unsigned char* p = &pixels[ i * 4 ];
		const double l = ( 0.2126 * p[ 0 ] + 0.7152 * p[ 1 ] + 0.0722 * p[ 2 ] ) / 255.0;
		if( p[ 3 ] > 4 && l > 0.02 )
		{
			++lit;
			luminance += l;
		}
	}

	Coverage coverage;
	coverage.litFraction = count > 0 ? static_cast< double >( lit ) / static_cast< double >( count ) : 0.0;
	coverage.meanLuma    = lit > 0 ? luminance / static_cast< double >( lit ) : 0.0;
	return coverage;
}

//---------------------------------------------------------------------------
// The tests
//---------------------------------------------------------------------------

/// Every saver, at several times, must draw something.
bool runCoverage( int width, int height )
{
	Surface surface = makeSurface( width, height );

	IdlerPlugin plugin( false );
	if( !prepare( plugin, width, height ) )
		return false;

	// Times chosen to be awkward. Zero because a saver that only works once the
	// clock has run is broken for anyone triggering a clip; a long one because
	// the growing savers must have cleared and restarted by then.
	const float times[] = { 0.0f, 0.37f, 7.5f, 63.25f, 240.0f };

	bool ok = true;

	for( int saver = 0; saver < static_cast< int >( SaverKind::Count ); ++saver )
	{
		plugin.SetFloatParameter( PT_SAVER, static_cast< float >( saver ) );

		// Each saver gets its own preset, which is where its scene controls are
		// set to something that suits it. Testing all eleven on Mystify's
		// defaults would prove very little.
		plugin.SetFloatParameter( PT_PRESET, static_cast< float >( saver + 1 ) );

		for( float when : times )
		{
			plugin.SetTimeOverride( when );
			render( plugin, surface );

			const Coverage coverage = measure( readBytes( surface ) );
			const size_t triangles = plugin.LastScene().mesh.TriangleCount();

			// The bar is literally zero, and deliberately so. An earlier
			// version wanted a tenth of a percent of the frame and failed 3D
			// Pipes at t = 0 -- where a network two cells long really is a few
			// dozen pixels, because the saver starts empty and grows, which is
			// what it did on the machine. The operator's answer to wanting it
			// part-grown on trigger is the Phase offset, not a saver that
			// cheats. So what is checked here is the failure that is actually a
			// failure: a saver that emits no geometry, or emits geometry that
			// lands nowhere.
			const bool drewSomething = triangles > 0 && coverage.litFraction > 0.0;

			printf( "%-22s t=%7.2f  lit %6.2f%%  luma %.3f  tris %7zu  %s\n",
			        SaverName( static_cast< SaverKind >( saver ) ), when,
			        coverage.litFraction * 100.0, coverage.meanLuma, triangles,
			        drewSomething ? "ok" : "DREW NOTHING" );

			if( !drewSomething )
				ok = false;
		}
	}

	plugin.DeInitGL();
	releaseSurface( surface );
	return ok;
}

/// The mesh each saver builds, checked without looking at the picture.
bool runGeometry( int width, int height )
{
	Surface surface = makeSurface( width, height );

	IdlerPlugin plugin( false );
	if( !prepare( plugin, width, height ) )
		return false;

	bool ok = true;

	for( int saver = 0; saver < static_cast< int >( SaverKind::Count ); ++saver )
	{
		plugin.SetFloatParameter( PT_SAVER, static_cast< float >( saver ) );
		plugin.SetFloatParameter( PT_PRESET, static_cast< float >( saver + 1 ) );
		plugin.SetTimeOverride( 11.5f );
		render( plugin, surface );

		const Mesh& mesh = plugin.LastScene().mesh;

		// Indices in range. An out-of-range index is undefined behaviour in the
		// draw and usually reads as random triangles flying off the frame.
		bool indicesValid = true;
		for( uint32_t index : mesh.indices )
			if( index >= mesh.vertices.size() )
			{
				indicesValid = false;
				break;
			}

		// Every normal unit length. A zero-length normal becomes a NaN once
		// normalised, and one NaN normal takes the lighting of the whole draw
		// call with it -- so the symptom is a saver that goes black with
		// nothing to say why.
		int badNormals   = 0;
		int nonFinite    = 0;
		Vec3 low{ 1e30f, 1e30f, 1e30f };
		Vec3 high{ -1e30f, -1e30f, -1e30f };

		for( const Vertex& v : mesh.vertices )
		{
			const float length = Length( v.normal );
			if( !( std::fabs( length - 1.0f ) < 0.02f ) )
				++badNormals;

			if( !std::isfinite( v.position.x ) || !std::isfinite( v.position.y ) ||
			    !std::isfinite( v.position.z ) )
			{
				++nonFinite;
				continue;
			}

			low  = { std::min( low.x, v.position.x ), std::min( low.y, v.position.y ), std::min( low.z, v.position.z ) };
			high = { std::max( high.x, v.position.x ), std::max( high.y, v.position.y ), std::max( high.z, v.position.z ) };
		}

		const bool pass = indicesValid && badNormals == 0 && nonFinite == 0 && !mesh.Empty();

		printf( "%-22s tris %7zu  verts %7zu  bbox [%.2f %.2f %.2f]..[%.2f %.2f %.2f]  %s\n",
		        SaverName( static_cast< SaverKind >( saver ) ), mesh.TriangleCount(), mesh.vertices.size(),
		        low.x, low.y, low.z, high.x, high.y, high.z, pass ? "ok" : "FAIL" );

		if( !indicesValid )
			printf( "    index out of range\n" );
		if( badNormals != 0 )
			printf( "    %d normals are not unit length\n", badNormals );
		if( nonFinite != 0 )
			printf( "    %d non-finite positions\n", nonFinite );
		if( mesh.Empty() )
			printf( "    empty mesh\n" );

		ok = ok && pass;
	}

	plugin.DeInitGL();
	releaseSurface( surface );
	return ok;
}

/**
    The maze walk has to keep going somewhere.

    This is the check for a bug that every other test in here passed. The walk
    turns round at a dead end, and it used to prefer carrying straight on at
    each junction on the way back -- which, travelling backwards, is exactly
    the corridor it arrived down. So it retraced its approach perfectly, hit
    the dead end at the other end of it, and did it again: the camera
    ping-ponged along the same handful of cells for minutes at a time. Every
    frame was correct, the replay was byte-identical, the geometry was valid
    and the coverage was lit. It just was not going anywhere.

    So measure where the camera has BEEN. The eye comes out of the view matrix
    -- `eye = -R^T t` -- quantises to a cell, and a window of consecutive ticks
    has to touch more than a handful of distinct ones. Before the fix the worst
    window here saw three cells; after it, twenty.

    Sampled a tick apart because a tick is a cell of travel: sampling per frame
    would count the same cell dozens of times and say nothing.
*/
bool runWalk( int width, int height )
{
	// Each case is a maze size and a turn preference. Low Density is the one
	// that used to trap, because it is the setting that most wants to carry
	// straight on -- and the shipped 3D Maze preset sits near it.
	struct Case
	{
		float seed;
		float density;
		float complexity;
	};
	const Case cases[] = {
		{ 0.00f, 0.00f, 0.40f },// the worst case for the old rule
		{ 0.37f, 0.40f, 0.40f },// the factory 3D Maze preset
		{ 0.71f, 1.00f, 1.00f },// the largest maze, most fidgety walk
	};

	// A tick is one cell of travel; see Maze.cpp.
	constexpr float kTickRate = 1.6f;
	constexpr int kTicks      = 1200;// 12 minutes of playback
	constexpr int kWindow     = 100; // ~1 minute
	constexpr int kFloor      = 12;  // distinct cells that window must touch

	Surface surface = makeSurface( width, height );

	IdlerPlugin plugin( false );
	if( !prepare( plugin, width, height ) )
		return false;

	bool ok = true;

	for( const Case& test : cases )
	{
		plugin.SetFloatParameter( PT_SAVER, static_cast< float >( SaverKind::Maze ) );
		plugin.SetFloatParameter( PT_SEED, test.seed );
		plugin.SetFloatParameter( PT_DENSITY, test.density );
		plugin.SetFloatParameter( PT_COMPLEXITY, test.complexity );

		std::vector< std::pair< int, int > > cells;
		cells.reserve( kTicks );

		for( int tick = 0; tick < kTicks; ++tick )
		{
			// Mid-tick, so the camera is between two cells rather than sitting
			// exactly on the boundary where the rounding could go either way.
			plugin.SetTimeOverride( ( static_cast< float >( tick ) + 0.5f ) / kTickRate );
			render( plugin, surface );

			const float* m = plugin.LastScene().view.m;
			const float eyeX =
				-( m[ 0 ] * m[ 12 ] + m[ 1 ] * m[ 13 ] + m[ 2 ] * m[ 14 ] );
			const float eyeZ =
				-( m[ 8 ] * m[ 12 ] + m[ 9 ] * m[ 13 ] + m[ 10 ] * m[ 14 ] );

			cells.emplace_back( static_cast< int >( std::floor( eyeX ) ),
			                    static_cast< int >( std::floor( eyeZ ) ) );
		}

		int worst      = kWindow + 1;
		int worstStart = 0;
		for( size_t start = 0; start + kWindow <= cells.size(); ++start )
		{
			std::vector< std::pair< int, int > > window( cells.begin() + static_cast< long >( start ),
			                                             cells.begin() + static_cast< long >( start + kWindow ) );
			std::sort( window.begin(), window.end() );
			const int distinct =
				static_cast< int >( std::unique( window.begin(), window.end() ) - window.begin() );
			if( distinct < worst )
			{
				worst      = distinct;
				worstStart = static_cast< int >( start );
			}
		}

		std::vector< std::pair< int, int > > all = cells;
		std::sort( all.begin(), all.end() );
		const int seen = static_cast< int >( std::unique( all.begin(), all.end() ) - all.begin() );

		const bool pass = worst >= kFloor;
		printf( "3D Maze  seed %.2f  density %.2f  complexity %.2f   %d cells in %d ticks, "
		        "worst %d-tick window %d (from tick %d)  %s\n",
		        static_cast< double >( test.seed ), static_cast< double >( test.density ),
		        static_cast< double >( test.complexity ), seen, kTicks, kWindow, worst, worstStart,
		        pass ? "ok" : "FAIL" );

		if( !pass )
			printf( "    the walk is stuck: fewer than %d distinct cells in a window\n", kFloor );

		ok = ok && pass;
	}

	plugin.DeInitGL();
	releaseSurface( surface );
	return ok;
}

/**
    The replay cache must not change the answer.

    A frame rendered cold and the same frame reached by running the clock up to
    it have to be **byte-identical**. Anything less means the cache is a second
    implementation of the growth, and a cache that is nearly right passes every
    visual check there is.

    Only the two growing savers have a cache; the other nine are run anyway,
    because a saver that quietly acquires state is exactly the regression this
    would otherwise miss.
*/
bool runReplay( int width, int height )
{
	Surface surface = makeSurface( width, height );

	IdlerPlugin plugin( false );
	if( !prepare( plugin, width, height ) )
		return false;

	constexpr float kTarget = 43.5f;

	bool ok = true;

	for( int saver = 0; saver < static_cast< int >( SaverKind::Count ); ++saver )
	{
		plugin.SetFloatParameter( PT_SAVER, static_cast< float >( saver ) );
		plugin.SetFloatParameter( PT_PRESET, static_cast< float >( saver + 1 ) );

		// Cold: nothing has been rendered at any other time.
		plugin.InvalidateReplay();
		plugin.SetTimeOverride( kTarget );
		render( plugin, surface );
		const std::vector< unsigned char > cold = readBytes( surface );

		// Warm: walk the clock up to the same instant, so the cache is carried
		// forward the whole way.
		plugin.InvalidateReplay();
		for( float when = 0.0f; when < kTarget; when += 0.25f )
		{
			plugin.SetTimeOverride( when );
			render( plugin, surface );
		}
		plugin.SetTimeOverride( kTarget );
		render( plugin, surface );
		const std::vector< unsigned char > warm = readBytes( surface );

		size_t differing = 0;
		for( size_t i = 0; i < cold.size() && i < warm.size(); ++i )
			if( cold[ i ] != warm[ i ] )
				++differing;

		const bool identical = ( differing == 0 );
		printf( "%-22s cold vs warm: %s%s\n", SaverName( static_cast< SaverKind >( saver ) ),
		        identical ? "identical" : "DIFFER",
		        identical ? "" : ( "  (" + std::to_string( differing ) + " bytes)" ).c_str() );

		ok = ok && identical;
	}

	plugin.DeInitGL();
	releaseSurface( surface );
	return ok;
}

/// A contact sheet: every saver, one tile each, on its own preset.
bool runSheet( const std::string& path, int tileWidth, int tileHeight )
{
	const int columns = 4;
	const int rows    = ( static_cast< int >( SaverKind::Count ) + columns - 1 ) / columns;

	const int sheetWidth  = tileWidth * columns;
	const int sheetHeight = tileHeight * rows;

	Surface surface = makeSurface( tileWidth, tileHeight );

	IdlerPlugin plugin( false );
	if( !prepare( plugin, tileWidth, tileHeight ) )
		return false;

	std::vector< unsigned char > sheet( static_cast< size_t >( sheetWidth ) * sheetHeight * 4, 0 );

	for( int saver = 0; saver < static_cast< int >( SaverKind::Count ); ++saver )
	{
		plugin.SetFloatParameter( PT_SAVER, static_cast< float >( saver ) );
		plugin.SetFloatParameter( PT_PRESET, static_cast< float >( saver + 1 ) );
		plugin.SetTimeOverride( 18.0f );
		render( plugin, surface );

		const std::vector< unsigned char > tile = flipRows( readBytes( surface ), tileWidth, tileHeight );

		const int column = saver % columns;
		const int row    = saver / columns;

		for( int y = 0; y < tileHeight; ++y )
		{
			const size_t source = static_cast< size_t >( y ) * tileWidth * 4;
			const size_t destination =
				( ( static_cast< size_t >( row * tileHeight + y ) ) * sheetWidth + column * tileWidth ) * 4;
			std::memcpy( sheet.data() + destination, tile.data() + source,
			             static_cast< size_t >( tileWidth ) * 4 );
		}

		printf( "tile %2d  %s\n", saver, SaverName( static_cast< SaverKind >( saver ) ) );
	}

	plugin.DeInitGL();
	releaseSurface( surface );

	if( !writePng( path, sheetWidth, sheetHeight, sheet ) )
	{
		fprintf( stderr, "could not write %s\n", path.c_str() );
		return false;
	}
	printf( "wrote %s (%dx%d)\n", path.c_str(), sheetWidth, sheetHeight );
	return true;
}

//---------------------------------------------------------------------------
// The cue sheet, and the sequence render that plays it.
//
// The project video is RENDERED, not filmed: an FFGL plugin has no window, its
// control surface is Resolume's inspector, and driving Arena means clicking a
// clip grid drawn with nothing in the accessibility tree. So the footage comes
// from here instead -- real frames through the real shipped plugin class.
//
// Nothing about the piece lives in the video toolkit. `tools/video.cues` is a
// timed list of parameter moves on the video's own clock, in this repo, next to
// the plugin it drives -- so the edit and the code cannot drift apart.
//---------------------------------------------------------------------------
struct Cue
{
	double from = 0.0;
	double to   = 0.0;
	std::string name;
	float first  = 0.0f;
	float second = 0.0f;
	bool ramp    = false;
};

bool parseCues( const std::string& path, std::vector< Cue >& cues )
{
	FILE* file = fopen( path.c_str(), "rb" );
	if( file == nullptr )
	{
		fprintf( stderr, "cannot open cue sheet %s\n", path.c_str() );
		return false;
	}

	char line[ 1024 ];
	int number = 0;
	while( fgets( line, sizeof( line ), file ) != nullptr )
	{
		++number;
		std::string text = line;

		const size_t hash = text.find( '#' );
		if( hash != std::string::npos )
			text = text.substr( 0, hash );

		const size_t firstReal = text.find_first_not_of( " \t\r\n" );
		if( firstReal == std::string::npos )
			continue;
		text = text.substr( firstReal );

		const size_t split = text.find_first_of( " \t" );
		if( split == std::string::npos )
			continue;

		const std::string when = text.substr( 0, split );
		std::string assignment = text.substr( split );

		const size_t assignStart = assignment.find_first_not_of( " \t" );
		if( assignStart == std::string::npos )
			continue;
		assignment = assignment.substr( assignStart );
		while( !assignment.empty() && ( assignment.back() == '\n' || assignment.back() == '\r' ||
		                                assignment.back() == ' ' || assignment.back() == '\t' ) )
			assignment.pop_back();

		Cue cue;
		const size_t timeRange = when.find( ".." );
		if( timeRange != std::string::npos )
		{
			cue.from = std::strtod( when.substr( 0, timeRange ).c_str(), nullptr );
			cue.to   = std::strtod( when.substr( timeRange + 2 ).c_str(), nullptr );
			cue.ramp = true;
		}
		else
		{
			cue.from = cue.to = std::strtod( when.c_str(), nullptr );
		}

		const size_t equals = assignment.find( '=' );
		if( equals == std::string::npos )
		{
			fprintf( stderr, "%s:%d: expected Name=value\n", path.c_str(), number );
			fclose( file );
			return false;
		}

		cue.name                = assignment.substr( 0, equals );
		const std::string value = assignment.substr( equals + 1 );

		const size_t valueRange = value.find( ".." );
		if( cue.ramp && valueRange != std::string::npos )
		{
			cue.first  = std::strtof( value.substr( 0, valueRange ).c_str(), nullptr );
			cue.second = std::strtof( value.substr( valueRange + 2 ).c_str(), nullptr );
		}
		else
		{
			cue.first = cue.second = std::strtof( value.c_str(), nullptr );
			cue.ramp  = false;
		}

		cues.push_back( cue );
	}

	fclose( file );
	return true;
}

int renderSequence( const std::string& directory, const std::string& cuePath, int width, int height,
                    double seconds, double fps, bool effect, const std::string& text )
{
	std::vector< Cue > cues;
	if( !cuePath.empty() && !parseCues( cuePath, cues ) )
		return 1;

	IdlerPlugin plugin( effect );
	if( !prepare( plugin, width, height ) )
		return 1;

	if( !text.empty() )
	{
		const std::map< std::string, unsigned int > names = parameterIndex( plugin );
		const auto found = names.find( "Text" );
		if( found != names.end() )
			plugin.SetTextParameter( found->second, text.c_str() );
	}

	// Every cue is checked against the real parameter list before a single frame
	// is rendered. A typo in a name would otherwise be a cue that silently never
	// fires, and the only symptom would be a video that is subtly less
	// interesting than the sheet says it is.
	const std::map< std::string, unsigned int > byName = parameterIndex( plugin );
	for( const Cue& cue : cues )
	{
		if( byName.find( cue.name ) == byName.end() )
		{
			fprintf( stderr, "cue names '%s', which is not a parameter\n", cue.name.c_str() );
			return 1;
		}
	}

	Surface surface   = makeSurface( width, height );
	const GLuint clip = effect ? makeTestClip( width, height ) : 0;

	const int frames = static_cast< int >( seconds * fps + 0.5 );
	int written      = 0;

	for( int frame = 0; frame < frames; ++frame )
	{
		const double now = static_cast< double >( frame ) / fps;

		// Applied in file order every frame rather than tracked as state, so a
		// later cue on the same parameter simply wins -- which is what reading
		// the sheet top to bottom would lead you to expect.
		for( const Cue& cue : cues )
		{
			if( now < cue.from )
				continue;

			float value = cue.second;
			if( cue.ramp && now < cue.to && cue.to > cue.from )
			{
				const double t = ( now - cue.from ) / ( cue.to - cue.from );
				// Smoothstep rather than linear. A parameter that starts and
				// stops abruptly reads as a jump cut even when the value in
				// between is right.
				const double eased = t * t * ( 3.0 - 2.0 * t );
				value              = static_cast< float >( cue.first + ( cue.second - cue.first ) * eased );
			}

			plugin.SetFloatParameter( byName.at( cue.name ), value );
		}

		// The host clock and a steady 120bpm transport, so Sync has something
		// real to lock to. The time is NOT pinned: the plugin free-runs off the
		// host clock exactly as it does in Resolume, which is the only way
		// footage can honestly show Speed doing anything.
		plugin.SetClockScaleForTest( 1.0 );//seconds, said out loud rather than inferred
		plugin.SetTime( now );
		plugin.SetBeatInfo( 120.0f, static_cast< float >( std::fmod( now / 2.0, 1.0 ) ) );

		render( plugin, surface, clip );

		char path[ 1024 ];
		snprintf( path, sizeof( path ), "%s/frame%05d.png", directory.c_str(), frame );

		const std::vector< unsigned char > image = flipRows( readBytes( surface ), width, height );
		if( !writePng( path, width, height, image ) )
		{
			fprintf( stderr, "could not write %s\n", path );
			releaseSurface( surface );
			return 1;
		}

		++written;
		if( written % 60 == 0 )
			printf( "  %d / %d frames\n", written, frames );
	}

	releaseSurface( surface );
	if( clip != 0 )
		glDeleteTextures( 1, &clip );
	plugin.DeInitGL();

	printf( "wrote %d frames to %s at %g fps (%.1f seconds)\n", written, directory.c_str(), fps,
	        written / fps );
	return 0;
}

/**
    Every saver through BOTH renderers, compared.

    The OFX build cannot use the GL path -- an OFX host hands over a buffer and
    most of them never offer a context -- so it rasterises `Scene` in software.
    Two renderers for one plugin is a divergence waiting to happen, and the
    divergence people would actually hit is a preset that looks right in
    Resolume and wrong in Resolve.

    This does not demand identical pixels, and could not: a GPU's fill rule,
    its interpolation precision and its `fwidth` are all its own. What it
    demands is that the two agree about the PICTURE -- where the geometry
    landed, and roughly how bright it is. Coverage disagreement is the sharp
    test (a wrong matrix, a flipped y, a missing near clip all show up as
    coverage in the wrong pixels), and mean channel error catches shading that
    drifted.
*/
bool rasterCheck( int width, int height, const std::string& sheetPath )
{
	Surface surface = makeSurface( width, height );

	IdlerPlugin plugin( false );
	if( !prepare( plugin, width, height ) )
		return false;

	const size_t pixels = static_cast< size_t >( width ) * static_cast< size_t >( height );
	std::vector< float > software( pixels * 4 );

	bool ok = true;

	// A number agreeing with a number is not evidence that either is a picture.
	// Every real bug in this plugin was found by looking at a contact sheet, so
	// --raster can write one: GL on the left of each pair, software on the
	// right. If the software column were blank, every coverage figure above
	// would still read 100%.
	const int tileW  = width;
	const int tileH  = height;
	const int sheetW = tileW * 2;
	const int sheetH = tileH * static_cast< int >( SaverKind::Count );
	std::vector< unsigned char > sheet;
	if( !sheetPath.empty() )
		sheet.assign( static_cast< size_t >( sheetW ) * sheetH * 4, 0 );

	printf( "%-22s  coverage  mean err  max err\n", "saver" );

	for( int saver = 0; saver < static_cast< int >( SaverKind::Count ); ++saver )
	{
		plugin.SetFloatParameter( PT_SAVER, static_cast< float >( saver ) );
		plugin.SetFloatParameter( PT_PRESET, static_cast< float >( saver + 1 ) );
		plugin.SetTimeOverride( 11.5f );
		render( plugin, surface );

		// The GL readback is the composite pass's output, which for the source
		// plugin with Mix at 1 is the scene target unchanged -- so it is the
		// scene render being compared, not the compositor.
		const std::vector< unsigned char > gl = readBytes( surface );

		Rasterise( plugin.LastScene(), software.data(), width, height, plugin.LastEdgeWidth() );

		double sum        = 0.0;
		double worst      = 0.0;
		size_t disagree   = 0;
		size_t litEither  = 0;

		for( size_t i = 0; i < pixels; ++i )
		{
			// readBytes gives rows bottom-up, as GL stores them; the software
			// buffer is top-down. Comparing them without this flip produces a
			// beautifully symmetrical failure on savers that happen to be
			// symmetrical, which is most of them.
			const size_t x       = i % static_cast< size_t >( width );
			const size_t y       = i / static_cast< size_t >( width );
			const size_t flipped = ( static_cast< size_t >( height ) - 1 - y )
			                       * static_cast< size_t >( width ) + x;

			for( int c = 0; c < 4; ++c )
			{
				const double a = gl[ flipped * 4 + c ] / 255.0;
				const double b = software[ i * 4 + c ];
				const double d = std::fabs( a - b );
				sum += d;
				worst = std::max( worst, d );
			}

			const bool glLit = gl[ flipped * 4 + 3 ] > 8;
			const bool swLit = software[ i * 4 + 3 ] > 8.0f / 255.0f;
			if( glLit || swLit )
				++litEither;
			if( glLit != swLit )
				++disagree;
		}

		if( !sheet.empty() )
		{
			for( size_t i = 0; i < pixels; ++i )
			{
				const size_t x       = i % static_cast< size_t >( width );
				const size_t y       = i / static_cast< size_t >( width );
				const size_t flipped = ( static_cast< size_t >( height ) - 1 - y )
				                       * static_cast< size_t >( width ) + x;
				const size_t row     = static_cast< size_t >( saver ) * tileH + y;

				unsigned char* left = sheet.data() + ( row * sheetW + x ) * 4;
				for( int c = 0; c < 4; ++c )
					left[ c ] = gl[ flipped * 4 + c ];

				unsigned char* right = sheet.data() + ( row * sheetW + tileW + x ) * 4;
				for( int c = 0; c < 4; ++c )
				{
					const float v = software[ i * 4 + c ];
					right[ c ] = static_cast< unsigned char >(
					    std::min( std::max( v, 0.0f ), 1.0f ) * 255.0f + 0.5f );
				}
			}
		}

		const double mean     = sum / static_cast< double >( pixels * 4 );
		const double coverage = litEither == 0 ? 1.0
		                                       : 1.0 - static_cast< double >( disagree )
		                                                   / static_cast< double >( litEither );

		// Thresholds set from what the two actually do, not from hope. Edge
		// pixels differ on every triangle, so a saver made of thin lines has a
		// larger honest disagreement than one made of solids.
		const bool pass = coverage > 0.90 && mean < 0.02;
		if( !pass )
			ok = false;

		printf( "%-22s  %6.1f%%  %8.4f  %7.4f  %s\n", SaverName( static_cast< SaverKind >( saver ) ),
		        coverage * 100.0, mean, worst, pass ? "ok" : "MISMATCH" );
	}

	if( !sheet.empty() )
	{
		if( writePng( sheetPath.c_str(), sheetW, sheetH, sheet ) )
			printf( "\nwrote %s (GL left, software right)\n", sheetPath.c_str() );
		else
			printf( "\ncould not write %s\n", sheetPath.c_str() );
	}

	releaseSurface( surface );
	plugin.DeInitGL();
	printf( "\n%s\n", ok ? "GL and software renderers agree" : "renderers DISAGREE" );
	return ok;
}

//---------------------------------------------------------------------------
/// Prove a Speed change does not move the picture.
///
/// The time either side of the change is read directly rather than comparing
/// rendered frames: several savers are periodic, and a periodic picture can
/// match across a jump for the wrong reason -- it landed a whole number of
/// cycles away. The number says it outright.
//---------------------------------------------------------------------------
int runSpeedTest()
{
	int failures = 0;

	auto check = [ &failures ]( const char* what, double got, double want, double tol ) {
		const bool ok = std::abs( got - want ) <= tol;
		printf( "speed %-34s got=%-12.6f want=%-12.6f %s\n", what, got, want, ok ? "ok" : "FAILED" );
		if( !ok )
			++failures;
	};

	// Slider positions, not cycles per second.
	struct Step
	{
		const char* name;
		float slider;
	};
	const Step steps[] = {
		{ "default -> 0.10 (slower)", 0.10f },
		{ "0.10 -> 0.95 (much faster)", 0.95f },
		{ "0.95 -> 0.00 (stopped)", 0.00f },
		{ "0.00 -> 0.80 (running again)", 0.80f },
	};

	IdlerPlugin plugin( false );
	plugin.SetClockScaleForTest( 1.0 );//seconds, said out loud rather than inferred

	// An hour in, which is where the old arithmetic hurt most and where a live
	// operator actually is when they reach for the slider.
	double host = 3600.0;
	plugin.SetTime( host );
	plugin.TickClockForTest();

	// Untouched, the anchor must leave the old expression exactly as it was --
	// this is what keeps tools/sweep.py and every rendered-frame test honest.
	// The plugin's own default is asked for rather than written down here: a
	// test that hard-codes it goes quietly wrong the day the default moves.
	check( "untouched == clock * speed", plugin.CurrentTimeForTest(),
	       host * SpeedFromParam( plugin.GetFloatParameter( PT_SPEED ) ), 1e-3 );

	for( const Step& step : steps )
	{
		const float before = plugin.CurrentTimeForTest();

		// The same instant, a new speed: nothing about the clock has moved, so
		// nothing about the picture may either.
		plugin.SetFloatParameter( PT_SPEED, step.slider );
		plugin.TickClockForTest();
		check( step.name, plugin.CurrentTimeForTest(), before, 1e-3 );

		// And then it must actually run at the new rate.
		const float resumed = plugin.CurrentTimeForTest();
		host += 1.0;
		plugin.SetTime( host );
		plugin.TickClockForTest();
		check( "  one second later", plugin.CurrentTimeForTest() - resumed,
		       SpeedFromParam( step.slider ), 1e-3 );
	}

	// Bar sync is deliberately NOT anchored: its contract is that a cycle
	// boundary lands on the bar line, so it must still be the plain transport
	// product. If the anchor ever leaks into it, beat sync stops meaning
	// anything.
	{
		IdlerPlugin bar( false );
		bar.SetClockScaleForTest( 1.0 );
		bar.SetFloatParameter( PT_SYNC, static_cast< float >( Sync::Bar ) );
		bar.SetBeatInfo( 120.0f, 0.25f );//120bpm: a bar is two seconds
		bar.SetTime( 8.0 );
		bar.TickClockForTest();
		const float before = bar.CurrentTimeForTest();

		bar.SetFloatParameter( PT_SPEED, 0.95f );
		bar.TickClockForTest();
		const float after = bar.CurrentTimeForTest();

		const bool jumped = std::abs( after - before ) > 1e-3;
		printf( "speed %-34s %s\n", "Bar sync still re-locks", jumped ? "ok" : "FAILED" );
		if( !jumped )
			++failures;
	}

	printf( "%s\n", failures == 0 ? "speed: all ok" : "speed: FAILURES" );
	return failures == 0 ? 0 : 1;
}

void usage()
{
	printf(
		"idtest -- the Idler offline harness\n"
		"\n"
		"  --out PATH        render one frame\n"
		"  --sheet PATH      a contact sheet of all eleven savers\n"
		"  --list            parameters, with their types and defaults\n"
		"  --geometry        the mesh each saver builds, checked\n"
		"  --replay          the replay cache does not change the answer\n"
		"  --coverage        every saver draws something, at several times\n"
		"  --walk            the maze walk roams rather than pacing a few cells\n"
		"  --speed           a Speed change does not move the picture\n"
		"  --raster          the software rasteriser agrees with the GL one\n"
		"  --raster-sheet P  and write a GL-vs-software comparison sheet\n"
		"  --effect          render the effect variant over a test clip\n"
		"\n"
        "  --sequence DIR    render a cue sheet to numbered PNGs (the video)\n"
		"  --script PATH     the cue sheet\n"
		"  --seconds N       sequence length (default 45)\n"
		"  --fps N           sequence frame rate (default 30)\n"
		"  --text STRING     the Marquee / 3D Text string\n"
		"\n"
		"  --time SECONDS    pin the clock (default 12)\n"
		"  --hosttime SEC    drive the real clock instead of pinning\n"
		"  --size WxH        render size (default 960x540)\n"
		"  --saver N         which saver, by index\n"
		"  --preset N        apply factory preset N (1-based)\n"
		"  --set Name=value  any parameter, by its host name\n" );
}
} // namespace

int main( int argc, char** argv )
{
	std::string outPath;
	std::string sheetPath;
	bool wantList     = false;
	bool wantGeometry = false;
	bool wantRaster   = false;
	std::string rasterSheet;
	bool wantReplay   = false;
	bool wantCoverage = false;
	bool wantWalk     = false;
	bool wantSpeed    = false;
	bool wantEffect   = false;

	float time  = 12.0f;
	// Drive the REAL host clock instead of pinning the time.
	//
	// Pinning replaces the clock, which is exactly what most tests want -- but
	// it also means Speed, the millisecond-vs-seconds normalisation and the
	// audio integration are all bypassed, so they look dead to a sweep that
	// only ever pins. This renders two frames a frame-interval apart, which is
	// the minimum that gives the clock a delta to work with.
	bool useHostClock = false;
	int width   = 960;
	int height  = 540;
	int saver   = -1;
	int preset  = -1;
	std::string sequenceDir;
	std::string cuePath;
	std::string text;
	double seconds = 45.0;
	double fps     = 30.0;
	std::vector< std::string > settings;

	for( int i = 1; i < argc; ++i )
	{
		const std::string argument = argv[ i ];
		auto next                  = [ & ]() -> std::string { return ( i + 1 < argc ) ? argv[ ++i ] : std::string(); };

		if( argument == "--out" )
			outPath = next();
		else if( argument == "--sheet" )
			sheetPath = next();
		else if( argument == "--list" )
			wantList = true;
		else if( argument == "--geometry" )
			wantGeometry = true;
		else if( argument == "--raster" )
			wantRaster = true;
		else if( argument == "--raster-sheet" )
			rasterSheet = next();
		else if( argument == "--replay" )
			wantReplay = true;
		else if( argument == "--coverage" )
			wantCoverage = true;
		else if( argument == "--walk" )
			wantWalk = true;
		else if( argument == "--speed" )
			wantSpeed = true;
		else if( argument == "--effect" )
			wantEffect = true;
		else if( argument == "--time" )
			time = std::stof( next() );
		else if( argument == "--hosttime" )
		{
			time         = std::stof( next() );
			useHostClock = true;
		}
		else if( argument == "--saver" )
			saver = std::stoi( next() );
		else if( argument == "--preset" )
			preset = std::stoi( next() );
		else if( argument == "--set" )
			settings.push_back( next() );
		else if( argument == "--sequence" )
			sequenceDir = next();
		else if( argument == "--script" )
			cuePath = next();
		else if( argument == "--seconds" )
			seconds = std::stod( next() );
		else if( argument == "--fps" )
			fps = std::stod( next() );
		else if( argument == "--text" )
			text = next();
		else if( argument == "--size" )
		{
			const std::string size = next();
			const size_t cross     = size.find( 'x' );
			if( cross != std::string::npos )
			{
				width  = std::stoi( size.substr( 0, cross ) );
				height = std::stoi( size.substr( cross + 1 ) );
			}
		}
		else
		{
			usage();
			return argument == "--help" ? 0 : 1;
		}
	}

	if( outPath.empty() && sheetPath.empty() && sequenceDir.empty() && !wantList && !wantGeometry &&
	    !wantReplay && !wantCoverage && !wantRaster && !wantWalk && !wantSpeed )
	{
		usage();
		return 1;
	}

	// Ahead of the GL context on purpose: this one needs no GPU, so it still
	// runs on a machine that cannot make a context at all.
	if( wantSpeed )
		return runSpeedTest();

	CGLContextObj context = createContext();
	if( context == nullptr )
	{
		fprintf( stderr, "could not create an OpenGL 4.1 core context\n" );
		return 1;
	}

	int status = 0;

	if( wantList )
	{
		IdlerPlugin plugin( false );
		for( unsigned int i = 0; i < plugin.GetNumParams(); ++i )
			printf( "%3u  %-22s type %2u  default %.4f\n", i,
			        plugin.GetParamName( i ) ? plugin.GetParamName( i ) : "?",
			        plugin.GetParamType( i ), plugin.GetFloatParameter( i ) );
	}

	if( !sequenceDir.empty() && status == 0 )
	{
		status = renderSequence( sequenceDir, cuePath, width, height, seconds, fps, wantEffect, text );
		CGLDestroyContext( context );
		return status;
	}

	if( wantRaster && status == 0 )
		status = rasterCheck( width, height, rasterSheet ) ? 0 : 1;

	if( wantGeometry && status == 0 )
		status = runGeometry( width, height ) ? 0 : 1;

	if( wantCoverage && status == 0 )
		status = runCoverage( width, height ) ? 0 : 1;

	if( wantReplay && status == 0 )
		status = runReplay( width, height ) ? 0 : 1;

	if( wantWalk && status == 0 )
		status = runWalk( width, height ) ? 0 : 1;

	if( !sheetPath.empty() && status == 0 )
		status = runSheet( sheetPath, 480, 270 ) ? 0 : 1;

	if( !outPath.empty() && status == 0 )
	{
		Surface surface = makeSurface( width, height );

		IdlerPlugin plugin( wantEffect );
		if( !prepare( plugin, width, height ) )
			status = 1;
		else
		{
			if( saver >= 0 )
				plugin.SetFloatParameter( PT_SAVER, static_cast< float >( saver ) );
			if( preset >= 0 )
				plugin.SetFloatParameter( PT_PRESET, static_cast< float >( preset ) );
			for( const std::string& setting : settings )
				if( !applySetting( plugin, setting ) )
					status = 1;

			const GLuint clip = wantEffect ? makeTestClip( width, height ) : 0;

			if( useHostClock )
			{
				// Two frames, so UpdateClock has a delta to decide its units
				// from and UpdateAudio has one to filter over.
				plugin.SetClockScaleForTest( 1.0 );//seconds, said out loud rather than inferred
				plugin.SetTime( static_cast< double >( time ) - 1.0 / 30.0 );
				render( plugin, surface, clip );
				plugin.SetTime( static_cast< double >( time ) );
				render( plugin, surface, clip );
			}
			else
			{
				plugin.SetTimeOverride( time );
				render( plugin, surface, clip );
			}

			const std::vector< unsigned char > pixels = flipRows( readBytes( surface ), width, height );
			if( !writePng( outPath, width, height, pixels ) )
			{
				fprintf( stderr, "could not write %s\n", outPath.c_str() );
				status = 1;
			}
			else
			{
				const Coverage coverage = measure( pixels );
				printf( "wrote %s (%dx%d)  lit %.2f%%  tris %zu\n", outPath.c_str(), width, height,
				        coverage.litFraction * 100.0, plugin.LastScene().mesh.TriangleCount() );
			}

			if( clip != 0 )
				glDeleteTextures( 1, &clip );
			plugin.DeInitGL();
		}

		releaseSurface( surface );
	}

	CGLDestroyContext( context );
	return status;
}
