#include "stdafx.h"

#include "logging.h"
#include "lyric_map_plugin.h"
#include "win32_util.h"

namespace
{
    std::vector<MapUnitSpan> convert_spans(const std::vector<linguamap::UnitSpan>& spans)
    {
        std::vector<MapUnitSpan> result;
        result.reserve(spans.size());
        for(const linguamap::UnitSpan& span : spans)
        {
            result.push_back({ to_tstring(span.text), span.pos });
        }
        return result;
    }
} // namespace

std::shared_ptr<const LyricMap> lyric_map_plugin::convert(const linguamap::Map& parsed)
{
    if(!parsed.is_valid())
    {
        return nullptr;
    }

    auto map = std::make_shared<LyricMap>();
    map->format_version = parsed.format_version;
    map->source_language = parsed.source_language;
    map->translit_language = parsed.translit_language;
    map->column_languages = parsed.column_languages;
    for(size_t i = 0; i < colorization::count(); i++)
    {
        map->pos_light[i] = colorization::to_colorref(parsed.pos_light[i]);
        map->pos_dark[i] = colorization::to_colorref(parsed.pos_dark[i]);
    }
    map->palette_from_file = parsed.palette_from_file;
    map->has_unknown_pos_codes = parsed.has_unknown_pos_codes;
    map->title = parsed.title;
    map->credit = parsed.credit;
    map->offset = parsed.offset;
    map->time_format = parsed.time_format;

    // Column order is data, but every column carries its own label: the diagnostics dialog
    // and the preferences checklist both read them from here rather than re-deriving them.
    std::vector<std::string> column_titles;
    for(const std::string& language : parsed.column_languages)
    {
        column_titles.push_back(language);
    }

    map->lines.reserve(parsed.lines.size());
    for(const linguamap::Line& line : parsed.lines)
    {
        MapLine converted;
        converted.timestamp = line.timestamp;
        converted.source = to_tstring(line.source);
        converted.source_units = convert_spans(line.source_units);
        converted.source_separators = line.source_separators;

        converted.columns.reserve(line.columns.size());
        for(const linguamap::Column& column : line.columns)
        {
            MapColumn converted_column;
            converted_column.language = column.language;
            converted_column.title = column.title;
            converted_column.units = convert_spans(column.units);
            converted_column.separators = column.separators;
            converted.columns.push_back(std::move(converted_column));
        }

        map->lines.push_back(std::move(converted));
    }

    return map;
}

bool lyric_map_plugin::to_lyric_data(const LyricDataCommon& metadata,
                                     std::string_view bundle_text_utf8,
                                     LyricData& out_lyrics,
                                     std::string& out_error,
                                     size_t& out_error_line,
                                     std::vector<std::string>& out_warnings)
{
    const linguamap::ParseResult parsed = linguamap::parse(bundle_text_utf8);
    out_error = parsed.error;
    out_error_line = parsed.error_line;
    out_warnings = parsed.warnings;

    if(!parsed.map.has_value())
    {
        return false;
    }

    std::shared_ptr<const LyricMap> map = convert(parsed.map.value());
    if(map == nullptr)
    {
        out_error = "the bundle parsed but could not be converted";
        out_error_line = 0;
        return false;
    }

    LyricData lyrics(metadata);
    lyrics.lines.reserve(map->lines.size());
    for(const MapLine& line : map->lines)
    {
        lyrics.lines.push_back({ line.source, line.timestamp });
    }

    // `@offset` was applied per block when the bundle was parsed, so the resulting plain
    // lines are already in the track's time base: leaving a second offset here would apply
    // it twice.
    lyrics.timestamp_offset = 0.0;
    lyrics.map = std::move(map);
    out_lyrics = std::move(lyrics);
    return true;
}
