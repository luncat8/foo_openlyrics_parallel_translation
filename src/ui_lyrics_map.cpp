#include "stdafx.h"

#include "logging.h"
#include "math_util.h"
#include "preferences.h"
#include "ui_lyrics_map.h"
#include "win32_util.h"

namespace
{
    COLORREF blend(COLORREF from, COLORREF to, double factor)
    {
        if(factor <= 0.0) return from;
        if(factor >= 1.0) return to;
        return lerp(from, to, factor);
    }

    // POS colours are the grammar. Past blocks keep their hue and are only blended toward
    // the past text colour, so scrolling back still reads as a word map. The active block is
    // never recoloured here: it is marked by a background tint and a left rule in the panel,
    // because painting over the grammar colours would destroy exactly the information the
    // eye is looking for.
    COLORREF apply_state(const MapBlockStyle& style, COLORREF base, bool is_transliteration)
    {
        COLORREF colour = base;

        if(style.past_factor > 0.0)
        {
            const double blend_amount = double(preferences::display::map_past_blend_percent()) / 100.0;
            colour = blend(colour, style.past_text_colour, style.past_factor * blend_amount);
        }

        if(style.upcoming_factor > 0.0)
        {
            // Lifting every unit toward the highlight colour marks the block that is about
            // to take over, which is what the plain renderer does by lerping the whole line.
            // Doing it per unit keeps the grammar visible throughout the transition.
            colour = blend(colour, style.highlight_colour, style.upcoming_factor);
        }

        if(is_transliteration)
        {
            // The transliteration mirrors the source, so it must not compete with the
            // translations for attention: a small constant nudge toward the main colour.
            colour = blend(colour, style.main_text_colour, 0.25);
        }

        return colour;
    }

    // One unit of text that has been placed on one physical row.
    struct PlacedUnit
    {
        std::tstring text;
        COLORREF colour = RGB(0, 0, 0);
        int unit_index = -1;
        int x = 0; // left edge, relative to the row's own left edge
        int width = 0;
    };

    // Splits a single over-long word so that it can be drawn at all. Returns the length (in
    // characters) that fits in `available_width`, or 0 when not even one character fits.
    size_t fit_prefix_length(const std::tstring& text, int available_width, const MapMeasureFunction& measure)
    {
        if(text.empty() || (available_width <= 0)) return 0;
        if(measure(text.substr(0, 1)) > available_width) return 0;

        size_t low = 1;
        size_t high = text.size();
        size_t best = 1;
        while(low <= high)
        {
            const size_t middle = low + ((high - low) / 2);
            if(measure(text.substr(0, middle)) <= available_width)
            {
                best = middle;
                low = middle + 1;
            }
            else
            {
                if(middle == 0) break;
                high = middle - 1;
            }
        }
        return best;
    }
} // namespace

bool is_dark_mode()
{
    // `g_is_dark_mode` is documented as safe to call without checking that
    // `ui_config_manager` exists (it returns false on foobar2000 < 2.0), and it is a static
    // member of a core SDK service, so it resolves against the shared SDK library that the
    // project already links. The alternative, `fb2k::isDarkMode()`, lives in the SDK's
    // DarkMode.cpp helper, which this project does not compile.
    return ui_config_manager::g_is_dark_mode();
}

bool is_language_hidden(std::string_view language)
{
    const std::string hidden = from_tstring(preferences::display::map_hidden_columns());

    size_t start = 0;
    while(start <= hidden.size())
    {
        const size_t comma = hidden.find(',', start);
        const size_t end = (comma == std::string::npos) ? hidden.size() : comma;
        std::string item = hidden.substr(start, end - start);

        // Trim, so that "tr, de" works as well as "tr,de": a hand-typed preference must not
        // fail over a space.
        const size_t first = item.find_first_not_of(" \t");
        const size_t last = item.find_last_not_of(" \t");
        item = (first == std::string::npos) ? std::string() : item.substr(first, last - first + 1);

        if(!item.empty() && (item == language)) return true;
        if(comma == std::string::npos) break;
        start = comma + 1;
    }
    return false;
}

std::tstring block_plain_text(const LyricMap& map, const MapLine& line)
{
    std::tstring result = line.source;

    for(const MapColumn& column : line.columns)
    {
        if(is_language_hidden(column.language)) continue;

        const bool is_transliteration = !map.translit_language.empty()
                                        && (column.language == map.translit_language);
        if(is_transliteration && !preferences::display::map_show_transliteration()) continue;

        // An empty cell (the exporter writes one for a line a language does not cover)
        // contributes nothing, rather than a bare label with nothing after it.
        std::tstring text;
        for(size_t i = 0; i < column.units.size(); i++)
        {
            if(i < column.separators.size()) text += to_tstring(column.separators[i]);
            text += column.units[i].text;
        }
        if(text.find_first_not_of(_T(" \t")) == std::tstring::npos) continue;

        result += _T('\n');
        result += to_tstring(column.title);
        result += _T(": ");
        result += text;
    }
    return result;
}

MapBlockLayout layout_block(const LyricMap& map,
                            const MapLine& line,
                            int line_index,
                            const MapBlockStyle& style,
                            const CRect& clip_rect,
                            CPoint origin,
                            int row_height,
                            MapMeasureFunction measure,
                            MapColourFunction colour_of,
                            std::optional<std::pair<int, int>> spotlight_unit)
{
    MapBlockLayout layout;

    // NOTE: CRect's members are LONG, so mixed int/LONG comparisons are narrowed explicitly:
    //       MSVC and GCC disagree about which std::max overload wins, and NOMINMAX does not
    //       help with that.
    const int indent = std::max(0, style.indent_px);
    const int clip_width = int(clip_rect.Width());
    const int source_width = std::max(1, clip_width);
    const int column_width = std::max(1, clip_width - indent);

    int top = int(origin.y);

    // Lays out one logical row (one line of one language): packs its units onto physical
    // rows of `width` pixels, then places every run at its exact position.
    //
    // Wrapping is decided for the row as a whole rather than per unit, so units that fit
    // share a physical row, and positions come from the same `measure` the caller paints
    // with. That is why the height reported here is exactly the height that gets painted.
    const auto emit_row = [&](const std::vector<MapUnitSpan>& units,
                              const std::vector<std::string>& separators,
                              bool is_transliteration,
                              int row_index,
                              int left,
                              int width,
                              bool is_source_row,
                              int block_index)
    {
        std::vector<std::vector<PlacedUnit>> physical_rows(1);

        int x = 0;
        const auto start_new_row = [&]()
        {
            physical_rows.emplace_back();
            x = 0;
        };

        for(size_t i = 0; i < units.size(); i++)
        {
            const MapUnitSpan& unit = units[i];
            if(unit.text.empty()) continue;

            const bool spotlit = !spotlight_unit.has_value()
                                 || ((spotlight_unit->first == row_index) && (size_t(spotlight_unit->second) == i));

            COLORREF base = map.colour_of(unit.pos, style.dark_mode, style.main_text_colour);
            if(style.spotlight_active && !spotlit)
            {
                base = blend(base, style.past_text_colour, style.spotlight_dim);
            }
            const COLORREF colour = apply_state(style, base, is_transliteration);

            // The whitespace that precedes a unit rides with it, so a run never starts a
            // physical row with whitespace (both GDI and DirectWrite want that for wrapping).
            std::tstring text;
            if(i < separators.size()) text += to_tstring(separators[i]);
            text += unit.text;

            std::tstring outstanding = text;
            while(!outstanding.empty())
            {
                if((x > 0) && (measure(outstanding) > (width - x)))
                {
                    // The whole remaining word does not fit next to what is already on this
                    // row, and there is something on this row: wrap first.
                    start_new_row();
                }

                const int available = width - x;
                const size_t fit = fit_prefix_length(outstanding, available, measure);
                if(fit == 0)
                {
                    if(x == 0)
                    {
                        // Not even one character fits the row: place it anyway so that the
                        // text is clipped rather than silently dropped, and stop.
                        const int width_px = measure(outstanding);
                        physical_rows.back().push_back({ outstanding, colour, int(i), x, width_px });
                        x += width_px;
                        outstanding.clear();
                        break;
                    }
                    start_new_row();
                    continue;
                }

                const std::tstring piece = outstanding.substr(0, fit);
                const int width_px = measure(piece);
                physical_rows.back().push_back({ piece, colour, int(i), x, width_px });
                x += width_px;

                outstanding = outstanding.substr(fit);
                if(!outstanding.empty()) start_new_row();
            }
        }

        const bool any_text = !physical_rows.back().empty() || (physical_rows.size() > 1);
        if(!any_text && is_source_row)
        {
            // A blank source line (an instrumental gap) still occupies its row: the cursor
            // needs something to highlight and the block keeps its rhythm.
            physical_rows.back().push_back({ std::tstring(), style.main_text_colour, 0, 0, 0 });
        }

        for(const std::vector<PlacedUnit>& row : physical_rows)
        {
            if(row.empty()) continue;

            // Alignment is applied per physical row, so that a wrapped continuation hugs
            // the same edge as the row above it instead of drifting to the left.
            int row_width = 0;
            for(const PlacedUnit& unit : row)
            {
                row_width = std::max(row_width, unit.x + unit.width);
            }
            int row_offset = 0;
            switch(style.alignment)
            {
                case MapAlignment::Left: row_offset = 0; break;
                case MapAlignment::Centre: row_offset = (width - row_width) / 2; break;
                case MapAlignment::Right: row_offset = width - row_width; break;
                default: break;
            }

            for(const PlacedUnit& unit : row)
            {
                MapRun run;
                run.text = unit.text;
                run.colour = unit.colour;
                run.row = row_index;
                run.unit_index = unit.unit_index;
                run.line_index = block_index;
                const int x = left + row_offset + unit.x;
                run.rect = CRect(x, top, x + unit.width, top + row_height);
                layout.runs.push_back(std::move(run));
            }
            top += row_height;
        }
    };

    // ----- source row ------------------------------------------------------
    const std::vector<MapUnitSpan> source_units =
        line.source_units.empty() ? std::vector<MapUnitSpan> { { line.source, colorization::Pos::Unknown } }
                                  : line.source_units;
    std::vector<std::string> source_separators = line.source_separators;
    if(source_separators.empty()) source_separators.emplace_back();

    emit_row(source_units, source_separators, false, 0, int(origin.x), source_width, true, line_index);

    // ----- continuation rows ----------------------------------------------
    // Columns are data, not code: whatever the bundle declares is drawn, in the bundle's
    // order. Only the source is special (it is the block's own first row).
    int row_index = 1;
    for(const MapColumn& column : line.columns)
    {
        if(is_language_hidden(column.language)) continue;

        const bool is_transliteration = !map.translit_language.empty()
                                        && (column.language == map.translit_language);
        if(is_transliteration && !preferences::display::map_show_transliteration()) continue;

        bool has_text = false;
        for(const MapUnitSpan& unit : column.units)
        {
            has_text |= !unit.text.empty();
        }
        if(!has_text) continue; // an empty cell adds no row at all, not a blank one

        emit_row(column.units,
                 column.separators,
                 is_transliteration,
                 row_index,
                 int(origin.x) + indent,
                 column_width,
                 false,
                 line_index);
        row_index++;
    }

    layout.content_height = std::max(0, top - int(origin.y));
    layout.height = layout.content_height + row_height; // the gap between blocks
    return layout;
}
