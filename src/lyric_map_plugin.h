#pragma once

#include "stdafx.h"

#include "colorization.h"
#include "lyric_map.h"

// The in-memory word map the plugin renders, and the conversion from the portable bundle
// model in `src/lyric_map.h`.
//
// Two types exist on purpose:
//
//   linguamap::Map   portable: UTF-8, 0xRRGGBB, no Windows headers. Compiled into the
//                    plugin *and* into tools/linguamap_check so the format can be tested
//                    without Windows.
//   LyricMap         plugin: tstring (the encoding GDI draws with) and COLORREF (the
//                    encoding GDI colours with). Built once per loaded bundle.
//
// The conversion is one-way and happens on the search task's thread, so a panel only ever
// touches the plugin type.

namespace colorization
{
    // The palette entry for `pos` as a COLORREF, or `fallback` for an unknown code (which
    // is a warning, not an error: a map from a newer LinguaMap must still draw).
    inline COLORREF to_colorref_or(uint32_t rgb, COLORREF fallback)
    {
        return (rgb == 0) ? fallback : to_colorref(rgb);
    }
} // namespace colorization

struct MapUnitSpan
{
    std::tstring text;
    colorization::Pos pos = colorization::Pos::Unknown;
};

struct MapColumn
{
    // The narrow `@columns` key, used for preference comparisons and diagnostics.
    std::string language;

    // The display name from `@columnTitles`, for the diagnostics dialog.
    std::string title;

    // The column's text split into drawable runs, in this language's own word order.
    // `separators[i]` precedes `units[i]`.
    std::vector<MapUnitSpan> units;
    std::vector<std::string> separators;
};

struct MapLine
{
    // Seconds; `DBL_MAX` when the bundle is untimed (`@timeFormat none`), which is the
    // same sentinel `LyricDataLine` uses, so scroll/highlight logic needs no special case.
    double timestamp = DBL_MAX;

    // The source line without unit markers, byte-exact with respect to the bundle.
    std::tstring source;

    // The source line split into coloured runs, in source word order.
    std::vector<MapUnitSpan> source_units;
    std::vector<std::string> source_separators;

    // One entry per language in `LyricMap::column_languages` except the source language.
    std::vector<MapColumn> columns;
};

struct LyricMap
{
    int format_version = 0;

    std::string source_language;
    std::string translit_language; // Empty when the bundle has no @translit
    std::vector<std::string> column_languages; // @columns, source first

    // Resolved palette (bundle overrides already applied) for the current theme.
    std::array<COLORREF, colorization::count()> pos_light = {};
    std::array<COLORREF, colorization::count()> pos_dark = {};

    bool palette_from_file = false;
    bool has_unknown_pos_codes = false;

    std::string title;
    std::string credit;
    double offset = 0.0;
    std::string time_format; // "lrc" or "none"

    std::vector<MapLine> lines;

    bool is_valid() const
    {
        return (format_version == 1) && !lines.empty() && !column_languages.empty();
    }

    bool is_timed() const
    {
        return time_format == "lrc";
    }

    // The colour of a unit of class `pos`, for the current theme. Unknown codes and
    // deliberately uncoloured runs fall back to `fallback` (the panel's main text colour).
    COLORREF colour_of(colorization::Pos pos, bool dark_mode, COLORREF fallback) const
    {
        if(size_t(pos) >= colorization::count()) return fallback;
        return dark_mode ? pos_dark[size_t(pos)] : pos_light[size_t(pos)];
    }
};

namespace lyric_map_plugin
{
    // Converts a parsed bundle into the plugin-side map. Never returns null for a valid
    // input; returns null when `parsed` is not a valid map.
    std::shared_ptr<const LyricMap> convert(const linguamap::Map& parsed);

    // Builds the `LyricData` the rest of the plugin consumes: one `lines` entry per block
    // whose text is the block's source, plus the shared map. Returns an empty `LyricData`
    // and fills `out_error` when the bundle cannot be used.
    bool to_lyric_data(const LyricDataCommon& metadata,
                       std::string_view bundle_text_utf8,
                       LyricData& out_lyrics,
                       std::string& out_error,
                       size_t& out_error_line,
                       std::vector<std::string>& out_warnings);
} // namespace lyric_map_plugin
