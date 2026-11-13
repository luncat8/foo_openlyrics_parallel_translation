#pragma once

#include "stdafx.h"

#include "lyric_map_plugin.h"

// Layout of one word-map block: a source line plus its stacked language columns, shared by
// the GDI panel and the Direct2D external window.
//
// The two windows have genuinely different draw calls (TextOut vs DrawTextLayout) so those
// stay separate. *Geometry*, however, must be decided once: a block whose height is
// computed differently from the way it is painted makes the lyric scroll jump, which is
// exactly the bug this split exists to prevent. So this header owns the order of rows, the
// indent, the line breaking, the gaps and the colours, and each window only turns the
// resulting runs into its own draw calls.
//
// Line breaking lives here too, which means the caller only has to be able to answer "how
// wide is this text in my surface". That is `GetTextExtentPoint32` for GDI and
// `GetMetrics()` on an `IDWriteTextLayout` for Direct2D: both are cheap, and both are
// measured with exactly the font that will paint the run.

struct MapRun
{
    std::tstring text;
    COLORREF colour = RGB(0, 0, 0);

    // Where the run is painted, in the coordinate space the caller passed to `layout_block`.
    CRect rect;

    // 0 = the source row; 1.. = continuation (column) rows.
    int row = 0;

    // Index into `LyricMap::lines` of the block this run belongs to.
    int line_index = 0;

    // The index of the unit within its row, so the hover hit test can find every unit that
    // shares a meaning. -1 for the run that is only whitespace.
    int unit_index = -1;
};

struct MapBlockLayout
{
    std::vector<MapRun> runs;

    // Height of the whole block, including the gap that separates it from the next block.
    int height = 0;

    // Vertical extent of the painted rows, without the trailing gap. The active-block
    // background tint uses this so that it does not bleed into the next block.
    int content_height = 0;
};

// Horizontal alignment, mirroring the panel's own text alignment preference.
enum class MapAlignment
{
    Left,
    Centre,
    Right,
};

// How a block should be drawn, given the panel's current state.
struct MapBlockStyle
{
    bool dark_mode = false;
    MapAlignment alignment = MapAlignment::Left;

    // Left indent for the continuation rows, in pixels. The caller derives it from its own
    // font metrics when the user leaves the preference on "auto", so this file never has to
    // guess a font.
    int indent_px = 0;

    COLORREF main_text_colour = RGB(0, 0, 0);
    COLORREF past_text_colour = RGB(0, 0, 0);
    COLORREF highlight_colour = RGB(0, 0, 0);

    // 0.0 = entirely in the future, 1.0 = entirely in the past. Values in between are used
    // for the existing highlight fade, so the map scrolls and fades like the plain panel.
    double past_factor = 0.0;

    // 0.0 = still far away, 1.0 = about to become the active block. Mirrors the plain
    // renderer's "next line gains the highlight colour as it approaches", applied to the
    // POS colours rather than replacing them.
    double upcoming_factor = 0.0;

    // Multipliers for the hover spotlight: when it is active, every unit that is not part
    // of the spotlighted meaning is blended toward the past colour by this much.
    bool spotlight_active = false;
    float spotlight_dim = 0.75f;
};

// Answers "how many pixels wide is this text", using the caller's own surface and font.
using MapMeasureFunction = std::function<int(const std::tstring& text)>;

// The colour a unit of class `pos` has before the past/spotlight blending is applied.
// Supplied by the caller so that this file knows nothing about where the palette lives.
using MapColourFunction = std::function<COLORREF(colorization::Pos pos)>;

// Lays out one block. `origin.y` is the *top* of the block (not a text baseline): text is
// vertically centred inside each row, the same way the plain renderer centres a line, so a
// mapped line and a plain line of the same text sit at the same height.
//
// `row_height` is the height of one text row including the configured line gap; the caller
// computes it once from `TEXTMETRIC`/`DWRITE` metrics so that the map and the plain view
// agree on line spacing.
//
// `line_index` is copied onto every run so a caller can map a painted run back to its block
// without a second lookup. `spotlight_unit` is the (row, unit index) under the pointer, or
// nothing.
MapBlockLayout layout_block(const LyricMap& map,
                            const MapLine& line,
                            int line_index,
                            const MapBlockStyle& style,
                            const CRect& clip_rect,
                            CPoint origin,
                            int row_height,
                            MapMeasureFunction measure,
                            MapColourFunction colour_of,
                            std::optional<std::pair<int, int>> spotlight_unit);

// The block's text as the plain renderer would show it (source, then one "Language: text"
// row per visible column). Used when word maps are switched off, or for a line the map does
// not cover, so that turning the map off changes colours and layout but never the words.
std::tstring block_plain_text(const LyricMap& map, const MapLine& line);

// True when the user has hidden `language` via the "never draw these languages"
// preference. Exposed so the diagnostics dialog can explain an apparently missing column.
bool is_language_hidden(std::string_view language);

// Whether the panel is currently being drawn on a dark background, which selects the dark
// half of the POS palette. This is the SDK's own "safe on older foobar2000 too" query, so
// the component does not need to link the DarkMode helper just to ask this one question.
bool is_dark_mode();
