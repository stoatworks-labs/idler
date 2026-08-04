#include "Font.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <mutex>

namespace idler::font
{
namespace
{
/// Grid height. Cap height is the full 12; everything is divided by this so a
/// capital is exactly 1.0 tall.
constexpr float kGridHeight = 12.0f;

/// Space either side of a glyph, in grid units.
constexpr float kSideBearing = 3.0f;

/// Advance of a space character, in grid units.
constexpr float kSpaceAdvance = 7.0f;

/**
    The letterforms.

    Two characters to a point, `'0'`-`'9'` then `'A'`-`'F'` for 10 to 15, on the
    8x12 grid described in Font.h. A space starts a new stroke.

    Indexed by `character - 32`, so entry 0 is the space and the table runs to
    the end of printable ASCII. An empty string means "no glyph": it advances
    and draws nothing.
*/
const char* const kGlyphs[] = {
	/* 32 ' ' */ "",
	/* 33 '!' */ "4C43 4140",
	/* 34 '"' */ "3C3A 5C5A",
	/* 35 '#' */ "303C 505C 1474 1878",
	/* 36 '$' */ "8A6C2C0A0826668482602002 4C40",
	/* 37 '%' */ "008C 0A2C 6082",
	/* 38 '&' */ "80272A4C6A680301205084",
	/* 39 '\''*/ "4C4A",
	/* 40 '(' */ "6C282460",
	/* 41 ')' */ "2C686420",
	/* 42 '*' */ "266A 2A66 464B",
	/* 43 '+' */ "1676 4349",
	/* 44 ',' */ "4220",
	/* 45 '-' */ "1676",
	/* 46 '.' */ "3040413130",
	/* 47 '/' */ "008C",
	/* 48 '0' */ "2060828A6C2C0A0220",
	/* 49 '1' */ "2A4C40 2060",
	/* 50 '2' */ "0A2C6C8A880080",
	/* 51 '3' */ "0C8C37678582602002",
	/* 52 '4' */ "606C0484",
	/* 53 '5' */ "8C0C07578582602002",
	/* 54 '6' */ "8A6C2C080220608285672705",
	/* 55 '7' */ "0C8C30",
	/* 56 '8' */ "27090B2C6C8B896727 2705022060828567",
	/* 57 '9' */ "022060848A6C2C0A07256587",
	/* 58 ':' */ "4847 4241",
	/* 59 ';' */ "4847 4220",
	/* 60 '<' */ "8A0682",
	/* 61 '=' */ "1474 1878",
	/* 62 '>' */ "0A8602",
	/* 63 '?' */ "0A2C6C8A884543 4140",
	/* 64 '@' */ "2060828A6C2C0A0220 3646",
	/* 65 'A' */ "004C80 2464",
	/* 66 'B' */ "000C 0C5C7A785606 066684826000",
	/* 67 'C' */ "8A6C2C0A02206082",
	/* 68 'D' */ "000C 0C4C8A824000",
	/* 69 'E' */ "80000C8C 0656",
	/* 70 'F' */ "000C8C 0656",
	/* 71 'G' */ "8A6C2C0A022060828656",
	/* 72 'H' */ "000C 808C 0686",
	/* 73 'I' */ "404C",
	/* 74 'J' */ "6C62402002",
	/* 75 'K' */ "000C 8C0580",
	/* 76 'L' */ "0C0080",
	/* 77 'M' */ "000C468C80",
	/* 78 'N' */ "000C808C",
	/* 79 'O' */ "2060828A6C2C0A0220",
	/* 80 'P' */ "000C 0C6C8A886606",
	/* 81 'Q' */ "2060828A6C2C0A0220 5380",
	/* 82 'R' */ "000C 0C6C8A886606 4680",
	/* 83 'S' */ "8A6C2C0A0826668482602002",
	/* 84 'T' */ "0C8C 4C40",
	/* 85 'U' */ "0C022060828C",
	/* 86 'V' */ "0C408C",
	/* 87 'W' */ "0C2047608C",
	/* 88 'X' */ "008C 0C80",
	/* 89 'Y' */ "0C468C 4640",
	/* 90 'Z' */ "0C8C0080",
	/* 91 '[' */ "6C2C2060",
	/* 92 '\\'*/ "0C80",
	/* 93 ']' */ "2C6C6020",
	/* 94 '^' */ "284C68",
	/* 95 '_' */ "0080"
};

constexpr int kFirstGlyph = 32;
constexpr int kGlyphCount = static_cast< int >( sizeof( kGlyphs ) / sizeof( kGlyphs[ 0 ] ) );

int HexValue( char c )
{
	if( c >= '0' && c <= '9' )
		return c - '0';
	if( c >= 'A' && c <= 'F' )
		return 10 + ( c - 'A' );
	return -1;
}

Glyph Decode( const char* encoded )
{
	Glyph glyph;
	if( encoded == nullptr )
		return glyph;

	Stroke current;
	float maxX = 0.0f;

	for( const char* p = encoded; *p != '\0'; )
	{
		if( *p == ' ' )
		{
			// A stroke of one point draws nothing and is almost certainly a
			// typo in the table above, so it is dropped rather than emitted.
			if( current.size() >= 2 )
				glyph.strokes.push_back( current );
			current.clear();
			++p;
			continue;
		}

		const int x = HexValue( p[ 0 ] );
		const int y = ( p[ 1 ] != '\0' ) ? HexValue( p[ 1 ] ) : -1;
		if( x < 0 || y < 0 )
			break;

		current.push_back( { static_cast< float >( x ) / kGridHeight,
		                     static_cast< float >( y ) / kGridHeight } );
		maxX = std::max( maxX, static_cast< float >( x ) );
		p += 2;
	}

	if( current.size() >= 2 )
		glyph.strokes.push_back( current );

	glyph.width = maxX / kGridHeight;

	// Advance is measured off the strokes rather than stored, so a glyph and
	// its advance cannot get out of step when one is edited.
	glyph.advance = ( glyph.strokes.empty() ? kSpaceAdvance : maxX + kSideBearing ) / kGridHeight;

	return glyph;
}

/// Decoded once, on first use. `std::call_once` rather than a function-local
/// static per glyph because this is reached from the render thread and the
/// table is shared by both plugin instances.
const std::array< Glyph, 128 >& Table()
{
	static std::array< Glyph, 128 > table;
	static std::once_flag once;
	std::call_once( once, [] {
		for( int i = 0; i < kGlyphCount; ++i )
		{
			const int code = kFirstGlyph + i;
			if( code < 128 )
				table[ static_cast< size_t >( code ) ] = Decode( kGlyphs[ i ] );
		}
	} );
	return table;
}

const Glyph& Empty()
{
	static const Glyph empty = [] {
		Glyph g;
		g.advance = kSpaceAdvance / kGridHeight;
		return g;
	}();
	return empty;
}
} // namespace

const Glyph& GetGlyph( char c )
{
	const unsigned char code = static_cast< unsigned char >( c );
	if( code >= 128 )
		return Empty();

	// Lowercase folds to a capital; the caller scales it. See Font.h on why
	// there is no separate lowercase alphabet.
	const unsigned char folded =
		( code >= 'a' && code <= 'z' ) ? static_cast< unsigned char >( code - 'a' + 'A' ) : code;

	if( folded < kFirstGlyph )
		return Empty();

	return Table()[ folded ];
}

float MeasureText( const char* text )
{
	if( text == nullptr )
		return 0.0f;

	float total = 0.0f;
	for( const char* p = text; *p != '\0'; ++p )
	{
		const bool small = ( *p >= 'a' && *p <= 'z' );
		total += GetGlyph( *p ).advance * ( small ? kSmallCapScale : 1.0f );
	}
	return total;
}

} // namespace idler::font
