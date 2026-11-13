#pragma once

// The LinguaMap part-of-speech palette.
//
// Colour *is* grammar: a unit is coloured by the part of speech of its *source-language*
// head, in every language column, so that the same meaning shows the same hue wherever it
// appears. This table is copied verbatim from the LinguaMap repository's
// `engine/colorization.json` (itself extracted from the HTML template CSS), which is the
// single source of truth. Nobody invents colours here, and the plugin never renders a hue
// that comes from nowhere: a bundle may override entries with `@pos`/`@posDark`, and
// anything else falls back to this table.
//
// This header is deliberately free of Windows and foobar2000 dependencies so that the
// parser and its checks can be compiled and exercised outside the plugin (see
// `tools/linguamap_check`). Colours are stored as 0xRRGGBB, exactly as the wire format
// spells them, and converted to `COLORREF` at the one place that draws.

#include <cassert>
#include <cstdint>
#include <string_view>

namespace colorization
{
    enum class Pos : uint8_t
    {
        N, // noun
        Npr, // proper noun
        V, // verb
        Adj, // i-adjective
        Adjna, // na-adjective
        Adv, // adverb
        Pron, // pronoun
        Conj, // conjunction
        Interj, // interjection
        Part, // particle
        Q, // question word

        Count,
        Unknown = 0xFF,
    };

    struct PosInfo
    {
        const char* code; // The wire-format code used in `@pos` and in unit markers
        const char* label; // Human-readable class name, for the diagnostics dialog
        uint32_t light; // 0xRRGGBB
        uint32_t dark; // 0xRRGGBB
    };

    // NOTE: The order here must match `Pos` exactly: the table is indexed by the enum.
    //       A test asserts that the codes are in the same order as `colorization.json`.
    inline constexpr PosInfo POS_INFO[] = {
        { "n", "noun", 0x1a5fc4, 0x7fb2f0 },
        { "npr", "proper noun", 0x3240a8, 0x94a2f0 },
        { "v", "verb", 0xc22b35, 0xf28b8b },
        { "adj", "i-adj", 0x1e7c34, 0x82d18d },
        { "adjna", "na-adj", 0x647c11, 0xc6d46e },
        { "adv", "adverb", 0x7c2cae, 0xcb9ee0 },
        { "pron", "pronoun", 0x0a7a80, 0x6fcbcd },
        { "conj", "conjunction", 0xbc5a10, 0xf0a763 },
        { "interj", "interjection", 0xb13272, 0xf099c4 },
        { "part", "particle", 0x77705f, 0xaab2bc },
        { "q", "question word", 0x93700a, 0xe6c35f },
    };

    static_assert(sizeof(POS_INFO) / sizeof(POS_INFO[0]) == size_t(Pos::Count),
                  "POS_INFO must have exactly one entry per Pos, in the same order");

    inline constexpr size_t count()
    {
        return size_t(Pos::Count);
    }

    inline const PosInfo& info(Pos pos)
    {
        assert(size_t(pos) < count());
        return POS_INFO[size_t(pos)];
    }

    inline const char* code(Pos pos)
    {
        return (size_t(pos) < count()) ? POS_INFO[size_t(pos)].code : "?";
    }

    // Returns Pos::Unknown when `code` is not one of the known codes. Unknown codes are a
    // warning, never an error: a map written by a newer LinguaMap must still render.
    inline Pos from_code(std::string_view code)
    {
        for(size_t i = 0; i < count(); i++)
        {
            if(code == POS_INFO[i].code) return Pos(i);
        }
        return Pos::Unknown;
    }

    // Parses a `#rrggbb` colour (the only format the palette uses). Returns false for
    // anything else, including the `#rgb` shorthand, so that a typo is an error rather
    // than a guess.
    inline bool parse_hex(std::string_view hex, uint32_t& out)
    {
        if(hex.size() != 7) return false;
        if(hex[0] != '#') return false;

        uint32_t value = 0;
        for(size_t i = 1; i < hex.size(); i++)
        {
            const char c = hex[i];
            uint32_t digit = 0;
            if((c >= '0') && (c <= '9'))
            {
                digit = uint32_t(c - '0');
            }
            else if((c >= 'a') && (c <= 'f'))
            {
                digit = uint32_t(c - 'a') + 10;
            }
            else if((c >= 'A') && (c <= 'F'))
            {
                digit = uint32_t(c - 'A') + 10;
            }
            else
            {
                return false;
            }
            value = (value << 4) | digit;
        }

        out = value;
        return true;
    }

    // Formats `value` as `#rrggbb`. Used by the diagnostics dialog and by tests.
    inline std::string to_hex(uint32_t value)
    {
        static constexpr char digits[] = "0123456789abcdef";
        std::string result = "#000000";
        for(size_t i = 0; i < 6; i++)
        {
            result[6 - i] = digits[(value >> (4 * i)) & 0xF];
        }
        return result;
    }
} // namespace colorization

#if defined(_WIN32)
#include <wingdi.h>

namespace colorization
{
    // 0xRRGGBB -> COLORREF (which is 0x00BBGGRR).
    inline COLORREF to_colorref(uint32_t rgb)
    {
        return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
    }

    inline COLORREF to_colorref(Pos pos, bool dark_mode)
    {
        if(size_t(pos) >= count()) return RGB(0, 0, 0);
        return to_colorref(dark_mode ? info(pos).dark : info(pos).light);
    }
} // namespace colorization
#endif // _WIN32
