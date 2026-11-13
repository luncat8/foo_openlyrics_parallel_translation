#pragma once

// The LinguaMap bundle model and parser.
//
// Deliberately free of Windows, foobar2000 and the rest of the plugin so that the same
// code is compiled into the plugin *and* into a small command-line checker
// (`tools/linguamap_check`) that CI can run on the repository's example bundles. That is
// what keeps the documented format and the loader honest with each other.
//
// Message building deliberately avoids `std::format`: it is not available in every
// compiler that can build the portable checker (GCC 12 has no <format>), and the messages
// only ever join a handful of fragments. No exceptions escape `parse()`.

#include <algorithm>
#include <array>
#include <cfloat>
#include <cstdint>
#include <cstdio>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "colorization.h"

namespace linguamap
{
    namespace detail
    {
        // Minimal, allocation-light string building so that the parser's diagnostics do not
        // depend on <format> or <sstream> being available.
        inline std::string to_text(std::string_view value)
        {
            return std::string(value);
        }
        inline std::string to_text(const std::string& value)
        {
            return value;
        }
        inline std::string to_text(const char* value)
        {
            return (value != nullptr) ? std::string(value) : std::string();
        }
        inline std::string to_text(size_t value)
        {
            return std::to_string(value);
        }
        inline std::string to_text(int value)
        {
            return std::to_string(value);
        }
        inline std::string to_text(double value)
        {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%.2f", value);
            return std::string(buffer);
        }

        template<typename... TParts>
        std::string cat(const TParts&... parts)
        {
            std::string result;
            ((result += to_text(parts)), ...);
            return result;
        }
    } // namespace detail

    // One unit (a word, or a fixed multi-word chunk such as a chant phrase) inside one
    // column of one line.
    //
    // `pos` is the part of speech of the *source-language* head that this unit expresses,
    // not of the target-language word itself. That is the whole point of the parallel
    // colouring: the Russian and the English rendering of one meaning share a hue.
    struct UnitSpan
    {
        std::string text; // UTF-8
        colorization::Pos pos = colorization::Pos::Unknown; // Unknown => render in the main text colour
    };

    struct Column
    {
        std::string language; // The `@columns` key, e.g. "ru"
        std::string title; // `@columnTitles`, or the language key when there is no title
        // The column's units, in this language's word order. Concatenating them (plus the
        // separators recorded in the line) reproduces the column's text exactly.
        std::vector<UnitSpan> units;
        std::vector<std::string> separators; // separators[i] follows units[i]
    };

    struct Line
    {
        // Seconds. `DBL_MAX` means the bundle is untimed (`@timeFormat none`), matching the
        // plugin's existing convention for untimed lines, so scroll/highlight code needs no
        // special case.
        double timestamp = DBL_MAX;

        // The source line: byte-exact (after UTF-8 decoding), including interior and
        // trailing whitespace the author fixed. This is the block's own first drawn row.
        std::string source;

        // Units of the source line, in source word order. Empty when the source line
        // carried no unit markers at all (the whole line is then one unit of the bundle's
        // default class, see `Line::source_pos`).
        std::vector<UnitSpan> source_units;
        std::vector<std::string> source_separators;

        colorization::Pos source_pos = colorization::Pos::Unknown; // Class of an unmarked whole line

        // One entry per language in `Map::column_languages` *except* the source language,
        // in that order. Always exactly that many entries: a block with a missing column
        // line is rejected rather than silently misaligned.
        std::vector<Column> columns;
    };

    struct Map
    {
        int format_version = 0; // The `#!linguamap N` version; 1 today

        std::string source_language; // `@source`
        std::string translit_language; // `@translit`, empty when the bundle has none
        std::vector<std::string> column_languages; // `@columns`, source first

        // The resolved palette, light and dark, as 0xRRGGBB, already blended with the
        // built-in table for codes the bundle did not override.
        std::array<uint32_t, colorization::count()> pos_light = {};
        std::array<uint32_t, colorization::count()> pos_dark = {};

        bool palette_from_file = false; // False => nothing overrode the built-in table
        bool has_unknown_pos_codes = false;

        std::string title; // `@title`
        std::string credit; // `@credit`
        double offset = 0.0; // `@offset`, already applied to every `Line::timestamp`
        std::string time_format; // "lrc" or "none"

        std::vector<Line> lines;

        bool is_valid() const
        {
            return (format_version == 1) && !lines.empty() && !column_languages.empty();
        }

        bool is_timed() const
        {
            return time_format == "lrc";
        }

        // The colour of `pos` for the current theme, as 0xRRGGBB. Unknown codes fall back
        // to `fallback` so that a map from a newer LinguaMap still renders (as a warning,
        // never as a gap).
        uint32_t colour_of(colorization::Pos pos, bool dark_mode, uint32_t fallback) const
        {
            if(size_t(pos) >= colorization::count()) return fallback;
            return dark_mode ? pos_dark[size_t(pos)] : pos_light[size_t(pos)];
        }
    };

    struct ParseResult
    {
        std::optional<Map> map;
        std::string error; // Empty on success
        size_t error_line = 0; // 1-based; 0 when the problem is not tied to a line
        std::vector<std::string> warnings; // Human-readable, for the diagnostics dialog
    };

    // True when `text_utf8` begins with the `#!linguamap` magic (after an optional BOM).
    // This is the cheap test callers use to decide *which* parser a piece of retrieved text
    // needs, before paying for a full parse.
    bool looks_like_bundle(std::string_view text_utf8);

    // Parses one bundle. Never throws.
    ParseResult parse(std::string_view text_utf8);

    // The LRC timestamp grammar the bundle uses. `print_timestamp` is provided so that
    // tools and tests can round-trip; the plugin itself keeps using
    // `parsers::lrc::print_timestamp` when writing lyrics, and a test proves the two
    // grammars agree so that a bundle cannot drift from what the editor can write.
    bool parse_timestamp(std::string_view tag, double& out_timestamp);
    std::string print_timestamp(double timestamp);
} // namespace linguamap
