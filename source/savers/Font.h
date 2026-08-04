#pragma once

#include <vector>

#include "../Vec.h"

/**
    A stroke font, for Scrolling Marquee and 3D Text.

    ## Why the plugin carries its own font

    Both of those savers drew text with whatever the machine's font engine gave
    them. A plugin cannot do that: it has to look the same in Resolume on a Mac,
    Resolume on Windows and Resolve on either, and asking the host OS for a font
    guarantees it will not. Vendoring a real outline font would mean vendoring a
    TrueType parser and a polygon triangulator that can handle holes, for two
    savers.

    So the letterforms here are **strokes** -- polylines down the middle of each
    letter -- and the thickness is added by the saver. Scrolling Marquee widens
    them into quads with `Mesh::AddPolyline`; 3D Text sweeps them into solid
    slabs with real depth. Both get a weight control for free, which an outline
    font would not have given.

    The typeface is a plain geometric sans, drawn on a grid, because that is
    what reads at speed and at a distance -- which is the only thing either
    saver is for.

    ## What it does not have

    **No lowercase.** Lowercase letters are drawn as smaller capitals. A stroke
    font's lowercase is a second full alphabet of a different construction --
    bowls, ascenders, descenders -- for a marquee that is nearly always set in
    capitals anyway. Small caps is a real typographic setting rather than a
    failure to render, and it is what you get.

    **No kerning.** Advance is the glyph's own width plus a fixed side bearing,
    measured off the strokes rather than stored, so a glyph cannot get out of
    step with its own advance.

    ## The coordinate grid

    Glyphs are authored on a grid 8 wide and 12 tall, baseline at y = 0 and cap
    height at y = 12, encoded two characters to a point: `'0'`-`'9'` then
    `'A'`-`'F'` for 10 to 15. A space starts a new stroke.

    `GetGlyph` returns them scaled so that **cap height is exactly 1.0** and the
    baseline is y = 0, because every caller wants to size text by its cap
    height.
*/
namespace idler::font
{

/// One polyline down the middle of part of a letter.
using Stroke = std::vector< Vec2 >;

struct Glyph
{
	std::vector< Stroke > strokes;

	/// How far to move along after drawing this glyph, in cap-height units.
	float advance = 0.0f;

	/// Rightmost point reached, in cap-height units. Zero for a space.
	float width = 0.0f;
};

/// The glyph for `c`. Lowercase is folded to small capitals; anything with no
/// glyph comes back empty but still advances, so an unsupported character
/// leaves a gap rather than closing up the line.
const Glyph& GetGlyph( char c );

/// Total advance of `text`, in cap-height units.
float MeasureText( const char* text );

/// How much smaller a small capital is than a full one.
constexpr float kSmallCapScale = 0.72f;

} // namespace idler::font
