#include "lyric_map.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

// ============================================================================
// Wire format (normative; docs/linguamap.md is the prose version)
//
//   #!linguamap 1
//   @title        Song title
//   @source       jp
//   @translit     tr
//   @columns      jp,tr,ru,en
//   @columnTitles ru=Русский,en=English,jp=日本語,tr=Romaji
//   @pos          n=#1a5fc4,...
//   @posDark      n=#7fb2f0,...
//   @posDefault   part
//   @timeFormat   lrc
//   @offset       0
//
//   [00:12.40]
//   夕暮れ{起:} {見上げ:v}   <- source line; markers are {text:class}
//   \tyuugure sora o miage  <- column lines, one per @columns language, source omitted
//   \tВ сумерках смотрим вверх на небо —
//
// A marker groups the text before the colon into one unit of the given class. A '{' that
// does not form a marker is literal text, so no escaping is needed for lyrics containing
// braces. Whitespace between markers is a separator, never part of a unit.
// ============================================================================

namespace
{
    constexpr std::string_view MAGIC = "#!linguamap";
    constexpr int SUPPORTED_VERSION = 1;

    std::string_view trim(std::string_view str)
    {
        size_t start = 0;
        while((start < str.size()) && ((str[start] == ' ') || (str[start] == '\t'))) start++;
        size_t end = str.size();
        while((end > start) && ((str[end - 1] == ' ') || (str[end - 1] == '\t'))) end--;
        return str.substr(start, end - start);
    }

    bool is_separator(char c)
    {
        return (c == ' ') || (c == '\t');
    }

    // True for a UTF-8 continuation byte (0b10xxxxxx). The parser works on bytes, so every
    // place that inspects a single byte to decide whether it starts something must skip
    // continuation bytes: otherwise a multi-byte character would be treated as punctuation
    // or as the start of a new word. (Valid UTF-8 is a precondition; invalid input is
    // decoded to U+FFFD by the caller long before it reaches here.)
    bool is_utf8_continuation(char c)
    {
        return (uint8_t(c) & 0xC0) == 0x80;
    }

    // Full-width and ideographic spaces that appear inside CJK lyric text. They are not
    // word separators for colouring purposes: they belong to the unit they touch, exactly
    // like the ASCII space inside a fixed chant phrase ("Jumpin' girls!"), so that a
    // two-word unit is not silently split in half.
    bool is_inline_space(std::string_view text, size_t index)
    {
        if(index + 2 >= text.size()) return false;
        const uint8_t b0 = uint8_t(text[index]);
        const uint8_t b1 = uint8_t(text[index + 1]);
        const uint8_t b2 = uint8_t(text[index + 2]);
        return (b0 == 0xE3) && (b1 == 0x80) && (b2 == 0x80); // U+3000 IDEOGRAPHIC SPACE
    }

    std::vector<std::string_view> split(std::string_view str, char separator)
    {
        std::vector<std::string_view> result;
        size_t start = 0;
        while(true)
        {
            const size_t end = str.find(separator, start);
            result.push_back(trim((end == std::string_view::npos) ? str.substr(start) : str.substr(start, end - start)));
            if(end == std::string_view::npos) break;
            start = end + 1;
        }
        return result;
    }

    // `key=value,key=value,...` -> map. The first '=' wins, so a value may contain '='.
    // Neither ',' nor '=' can be escaped; that is why the format allows exactly one header
    // per physical line.
    bool parse_key_value_list(std::string_view value,
                              std::map<std::string, std::string>& out,
                              std::string& out_error)
    {
        out.clear();
        for(std::string_view item : split(value, ','))
        {
            if(item.empty()) continue;

            const size_t equals_index = item.find('=');
            if(equals_index == std::string_view::npos)
            {
                out_error = "expected 'code=value' but found '" + std::string(item) + "'";
                return false;
            }

            const std::string key(trim(item.substr(0, equals_index)));
            const std::string val(trim(item.substr(equals_index + 1)));
            if(key.empty())
            {
                out_error = "empty key in '" + std::string(item) + "'";
                return false;
            }
            if(!out.emplace(key, val).second)
            {
                out_error = "duplicate key '" + key + "'";
                return false;
            }
        }
        return true;
    }

    struct UnitParseResult
    {
        std::vector<linguamap::UnitSpan> units;
        std::vector<std::string> separators; // separators[i] follows units[i]
        bool saw_marker = false;
        bool ok = true;
        std::string error;
    };

    // Splits `text` into units according to the marker syntax.
    //
    // Rules, chosen so that *any* text an author writes parses and round-trips:
    //
    //  * `{text:code}` is a unit of class `code`; `{text:}` uses `@posDefault`.
    //  * `{}` is a *separator placeholder*: it emits no text and marks where a separator
    //    belongs inside a multi-word unit, e.g. `{Jumpin'{}girls:interj}`.
    //  * A `{` that does not form a marker is literal text, so lyrics may contain braces.
    //  * Text outside markers becomes units of `default_pos`, one per whitespace-separated
    //    word. Multi-byte characters (including U+3000) never split a word.
    //  * `separators[i]` is the text that *precedes* `units[i]`, so
    //    `for i: separators[i] + units[i].text` reproduces the input byte for byte, no
    //    matter what whitespace the author used. That exactness keeps the source column
    //    faithful; a mismatch is a parser bug and is reported, never ignored.
    class UnitBuilder
    {
    public:
        UnitBuilder(UnitParseResult& out, colorization::Pos default_pos)
            : m_result(out)
            , m_default_pos(default_pos)
        {
        }

        void add_char_byte(char c)
        {
            m_pending.push_back(c);
        }

        void add_text(std::string_view text)
        {
            m_pending.append(text.data(), text.size());
        }

        void add_separator_byte(char c)
        {
            // Whitespace ends whatever plain word was in progress, so that the whitespace
            // can become the next unit's separator.
            flush_plain_run();
            m_separator.push_back(c);
        }

        void separator_placeholder()
        {
            // Emits nothing; only separates the plain runs on either side so the marker
            // syntax reads naturally: `{Jumpin'{}girls:interj}`.
            flush_plain_run();
        }

        void add_unit(std::string_view text, colorization::Pos pos)
        {
            flush_plain_run();
            push_unit(std::string(text), pos);
        }

        void finish()
        {
            flush_plain_run();
            if(m_result.units.empty())
            {
                // An empty source line is legal (instrumental gaps keep their time slot).
                m_result.separators.push_back(std::move(m_separator));
                m_result.units.push_back({ std::string(), m_default_pos });
                return;
            }

            // Anything left over (trailing whitespace) follows the last unit.
            if(m_result.separators.size() != m_result.units.size())
            {
                m_result.separators.resize(m_result.units.size());
            }
            m_result.separators.back() += m_separator;
            m_separator.clear();
        }

    private:
        // Emits every whitespace-separated word of the accumulated plain text.
        void flush_plain_run()
        {
            size_t index = 0;
            while(index < m_pending.size())
            {
                while((index < m_pending.size()) && is_separator(m_pending[index])) index++;
                if(index >= m_pending.size()) break;

                const size_t start = index;
                while((index < m_pending.size()) && !is_separator(m_pending[index])) index++;
                push_unit(m_pending.substr(start, index - start), m_default_pos);
            }
            m_pending.clear();
        }

        void push_unit(std::string text, colorization::Pos pos)
        {
            m_result.separators.push_back(m_separator);
            m_separator.clear();
            m_result.units.push_back({ std::move(text), pos });
        }

        UnitParseResult& m_result;
        colorization::Pos m_default_pos;
        std::string m_separator; // whitespace that will precede the next unit
        std::string m_pending; // unmarked, non-whitespace text not yet emitted
    };

    UnitParseResult parse_units(std::string_view text, colorization::Pos default_pos)
    {
        UnitParseResult result;
        UnitBuilder builder(result, default_pos);

        size_t index = 0;
        while(index < text.size())
        {
            const char c = text[index];

            if(c != '{')
            {
                if(is_inline_space(text, index))
                {
                    // A full-width space belongs to the run it touches: it must not split
                    // a two-word unit such as a chant phrase.
                    builder.add_text(text.substr(index, 3));
                    index += 3;
                    continue;
                }
                if(is_utf8_continuation(c))
                {
                    builder.add_char_byte(c); // never a boundary
                    index++;
                    continue;
                }
                if(is_separator(c))
                {
                    builder.add_separator_byte(c);
                }
                else
                {
                    builder.add_char_byte(c);
                }
                index++;
                continue;
            }

            // `{...}`: a unit marker, a separator placeholder, or literal text.
            const size_t close_index = text.find('}', index + 1);
            bool consumed = false;
            if(close_index != std::string_view::npos)
            {
                const std::string_view body = text.substr(index + 1, close_index - index - 1);
                const size_t colon_index = body.rfind(':');
                const bool has_colon = (colon_index != std::string_view::npos);
                const std::string_view unit_text = has_colon ? body.substr(0, colon_index) : body;
                const std::string_view code = has_colon ? trim(body.substr(colon_index + 1)) : std::string_view();
                const bool text_is_plain = unit_text.find_first_of("{}:") == std::string_view::npos;

                if(unit_text.empty() && code.empty())
                {
                    builder.separator_placeholder();
                    consumed = true;
                }
                else if(text_is_plain && !unit_text.empty() && has_colon)
                {
                    colorization::Pos pos = default_pos;
                    if(!code.empty())
                    {
                        pos = colorization::from_code(code);
                        if(pos == colorization::Pos::Unknown)
                        {
                            result.ok = false;
                            result.error = "unknown part-of-speech code '" + std::string(code)
                                           + "' in a unit marker (known codes: n, npr, v, adj, adjna, adv, "
                                             "pron, conj, interj, part, q)";
                            return result;
                        }
                    }

                    builder.add_unit(unit_text, pos);
                    result.saw_marker = true;
                    consumed = true;
                }
            }

            if(consumed)
            {
                index = close_index + 1;
            }
            else
            {
                builder.add_char_byte('{'); // literal brace
                index++;
            }
        }

        builder.finish();
        return result;
    }

    // Rebuilds the plain (unmarked) text of a line from its units and separators. Used for
    // the source column, which must be byte-exact, and validated against the input by
    // `parse_units`' callers so that a parser bug cannot silently corrupt lyrics.
    std::string strip_markers(std::string_view /*marked*/, const UnitParseResult& units)
    {
        std::string result;
        for(size_t i = 0; i < units.units.size(); i++)
        {
            if(i < units.separators.size()) result += units.separators[i];
            result += units.units[i].text;
        }
        if(units.separators.size() > units.units.size()) result += units.separators.back();
        return result;
    }

} // namespace

// ============================================================================
// Time grammar
//
// The bundle uses exactly the LRC stamp syntax the plugin already reads and writes
// (`[mm:ss.cc]` and `[h:mm:ss.cc]`, with 1..n sub-second digits), so an author can copy a
// timestamp out of the editor and paste it into a bundle. `linguamap_timestamp_grammar_matches_lrc`
// in the plugin's test suite proves the two implementations agree.
// ============================================================================

bool linguamap::parse_timestamp(std::string_view tag, double& out_timestamp)
{
    if((tag.size() < 4) || (tag.front() != '[') || (tag.back() != ']')) return false;

    const size_t second_separator = tag.rfind('.');
    const size_t minute_second_separator = tag.rfind(':');
    if((second_separator == std::string_view::npos) || (minute_second_separator == std::string_view::npos))
    {
        return false;
    }
    if(second_separator < minute_second_separator) return false;

    const size_t hour_minute_separator =
        (minute_second_separator == 0) ? std::string_view::npos : tag.rfind(':', minute_second_separator - 1);

    const auto parse_uint = [](std::string_view str, uint64_t& out) -> bool
    {
        if(str.empty()) return false;
        uint64_t value = 0;
        for(const char c : str)
        {
            if((c < '0') || (c > '9')) return false;
            value = (value * 10) + uint64_t(c - '0');
        }
        out = value;
        return true;
    };

    const std::string_view subsec_str = tag.substr(second_separator + 1, tag.size() - second_separator - 2);
    const std::string_view sec_str = tag.substr(minute_second_separator + 1,
                                                second_separator - minute_second_separator - 1);
    std::string_view min_str;
    std::string_view hour_str;
    if(hour_minute_separator == std::string_view::npos)
    {
        min_str = tag.substr(1, minute_second_separator - 1);
    }
    else
    {
        min_str = tag.substr(hour_minute_separator + 1, minute_second_separator - hour_minute_separator - 1);
        hour_str = tag.substr(1, hour_minute_separator - 1);
    }

    uint64_t subsec = 0;
    uint64_t sec = 0;
    uint64_t min = 0;
    uint64_t hour = 0;
    if(!parse_uint(subsec_str, subsec) || !parse_uint(sec_str, sec) || !parse_uint(min_str, min))
    {
        return false;
    }
    const bool has_hour = !hour_str.empty();
    if(has_hour && !parse_uint(hour_str, hour)) return false;

    double subsec_coefficient = 1.0;
    for(size_t i = 0; i < subsec_str.size(); i++) subsec_coefficient *= 0.1;

    double timestamp = 0.0;
    timestamp += double(subsec) * subsec_coefficient;
    timestamp += double(sec);
    timestamp += double(min) * 60.0;
    if(has_hour) timestamp += double(hour) * 3600.0;

    out_timestamp = timestamp;
    return true;
}

std::string linguamap::print_timestamp(double timestamp)
{
    // Mirrors `parsers::lrc::print_timestamp`, including the round-up case, so that a
    // bundle we reject for non-monotonic times would also be rejected by the editor.
    double total_seconds_flt = std::floor(timestamp);
    int total_seconds = static_cast<int>(total_seconds_flt);
    int time_hours = total_seconds / 3600;
    int time_minutes = (total_seconds - 3600 * time_hours) / 60;
    int time_seconds = total_seconds - (time_hours * 3600) - (time_minutes * 60);
    int time_centisec = static_cast<int>(std::round((timestamp - total_seconds_flt) * 100.0));

    if(time_centisec == 100)
    {
        time_centisec = 0;
        time_seconds++;
        if(time_seconds == 60)
        {
            time_seconds = 0;
            time_minutes++;
            if(time_minutes == 60)
            {
                time_minutes = 0;
                time_hours++;
            }
        }
    }

    char buffer[32];
    if(time_hours == 0)
    {
        std::snprintf(buffer, sizeof(buffer), "[%02d:%02d.%02d]", time_minutes, time_seconds, time_centisec);
    }
    else
    {
        std::snprintf(buffer,
                      sizeof(buffer),
                      "[%02d:%02d:%02d.%02d]",
                      time_hours,
                      time_minutes,
                      time_seconds,
                      time_centisec);
    }
    return std::string(buffer);
}

bool linguamap::looks_like_bundle(std::string_view text_utf8)
{
    size_t offset = 0;
    if((text_utf8.size() >= 3) && (uint8_t(text_utf8[0]) == 0xEF) && (uint8_t(text_utf8[1]) == 0xBB)
       && (uint8_t(text_utf8[2]) == 0xBF))
    {
        offset = 3;
    }
    if(text_utf8.size() < offset + MAGIC.size()) return false;
    if(text_utf8.substr(offset, MAGIC.size()) != MAGIC) return false;

    // Require the separator that introduces the version so that an unrelated file whose
    // first line merely starts with a similar word is not claimed as a bundle.
    if(text_utf8.size() <= offset + MAGIC.size()) return false;
    const char next = text_utf8[offset + MAGIC.size()];
    return (next == ' ') || (next == '\t');
}

linguamap::ParseResult linguamap::parse(std::string_view text)
{
    ParseResult result;

    const auto fail = [&result](size_t line, std::string message) -> ParseResult
    {
        result.map.reset();
        result.error = std::move(message);
        result.error_line = line;
        return result;
    };

    // ----- magic and version ------------------------------------------------
    size_t cursor = 0;
    if((text.size() >= 3) && (uint8_t(text[0]) == 0xEF) && (uint8_t(text[1]) == 0xBB) && (uint8_t(text[2]) == 0xBF))
    {
        cursor = 3; // A BOM is tolerated on read; the exporter never writes one.
    }
    if((text.size() < cursor + MAGIC.size()) || (text.substr(cursor, MAGIC.size()) != MAGIC))
    {
        return fail(1, "missing '#!linguamap' header: this file is not a LinguaMap bundle");
    }
    cursor += MAGIC.size();

    while((cursor < text.size()) && is_separator(text[cursor])) cursor++;

    size_t version = 0;
    size_t version_digits = 0;
    while((cursor < text.size()) && (text[cursor] >= '0') && (text[cursor] <= '9'))
    {
        version = (version * 10) + size_t(text[cursor] - '0');
        version_digits++;
        cursor++;
    }
    if(version_digits == 0)
    {
        return fail(1, "the '#!linguamap' header has no format version");
    }
    if(version != size_t(SUPPORTED_VERSION))
    {
        return fail(1,
                    detail::cat("this bundle is in LinguaMap format ",
                                version,
                                " but only format ",
                                SUPPORTED_VERSION,
                                " is supported; it was probably written by a newer LinguaMap"));
    }

    Map map;
    map.format_version = SUPPORTED_VERSION;
    for(size_t i = 0; i < colorization::count(); i++)
    {
        map.pos_light[i] = colorization::POS_INFO[i].light;
        map.pos_dark[i] = colorization::POS_INFO[i].dark;
    }

    std::set<std::string> seen_headers;
    std::map<std::string, std::string> column_titles;
    colorization::Pos default_pos = colorization::Pos::Part; // `@posDefault`
    std::vector<std::string> declared_columns; // `@columns`, in declared order

    std::optional<Line> pending; // the block being assembled
    std::vector<std::string> expected_columns; // languages whose column lines are still to come
    size_t pending_start_line = 0;
    bool pending_is_timed = false;

    // NOTE: A plain bool + value rather than std::optional<double>: GCC 12 emits a
    //       spurious maybe-uninitialized warning for the latter, and this is simpler anyway.
    bool has_next_timestamp = false; // set by a stamp line, consumed by the next source line
    double next_timestamp = 0.0;
    std::optional<std::string> stamp_line_source; // tolerated `[stamp]source` form
    size_t next_timestamp_line = 0;

    bool block_section_started = false;
    size_t line_number = 1;

    // Reads the next physical line, accepting LF, CRLF and a lone CR. False at EOF.
    const auto next_line = [&](std::string_view& out_line) -> bool
    {
        if(cursor >= text.size()) return false;

        const size_t line_start = cursor;
        size_t line_end = cursor;
        while((line_end < text.size()) && (text[line_end] != '\n') && (text[line_end] != '\r')) line_end++;

        out_line = text.substr(line_start, line_end - line_start);
        if((line_end < text.size()) && (text[line_end] == '\r') && (line_end + 1 < text.size())
           && (text[line_end + 1] == '\n'))
        {
            cursor = line_end + 2;
        }
        else
        {
            cursor = line_end + (line_end < text.size() ? 1 : 0);
        }
        return true;
    };

    const auto finish_block = [&]() -> bool
    {
        if(!pending.has_value()) return true;

        if(!expected_columns.empty())
        {
            // Column position is stable: a missing line would silently shift every column
            // of the block, which is exactly the corruption this format refuses to allow.
            const size_t got = pending->columns.size();
            const size_t want = pending->columns.size() + expected_columns.size();
            result.error = detail::cat("block starting at line ",
                                       pending_start_line,
                                       " has ",
                                       got,
                                       " column line(s) but ",
                                       want,
                                       " are required");
            result.error_line = pending_start_line;
            return false;
        }

        if(pending_is_timed && (pending->timestamp == DBL_MAX))
        {
            result.error = "block has no timestamp even though the bundle is timed (@timeFormat lrc)";
            result.error_line = pending_start_line;
            return false;
        }

        if(!map.lines.empty() && (map.lines.back().timestamp != DBL_MAX) && (pending->timestamp != DBL_MAX)
           && (pending->timestamp <= map.lines.back().timestamp))
        {
            result.error = detail::cat("timestamp ",
                                       pending->timestamp,
                                       "s does not come after the previous block's ",
                                       map.lines.back().timestamp,
                                       "s; timestamps must be strictly increasing");
            result.error_line = pending_start_line;
            return false;
        }

        map.lines.push_back(std::move(pending.value()));
        pending.reset();
        return true;
    };

    std::string_view line;
    while(next_line(line))
    {
        const size_t this_line = line_number;
        line_number++;

        std::string_view view = line;
        if(this_line == 1)
        {
            if((view.size() >= 3) && (uint8_t(view[0]) == 0xEF) && (uint8_t(view[1]) == 0xBB)
               && (uint8_t(view[2]) == 0xBF))
            {
                view.remove_prefix(3);
            }
        }

        const std::string_view trimmed = trim(view);

        // Blank lines and comments are cosmetic anywhere, including between a block's
        // source line and its column lines.
        if(trimmed.empty()) continue;
        if(trimmed.front() == '#') continue;

        const bool is_indented = (view.size() > 0)
                                 && ((view[0] == '\t') || ((view.size() >= 2) && (view[0] == ' ') && (view[1] == ' ')));

        if(is_indented)
        {
            // A column line. Text starting with '@' is still column text: only unindented
            // lines can be headers.
            if(!pending.has_value())
            {
                return fail(this_line, "indented column line appears before any source line");
            }
            if(expected_columns.empty())
            {
                const size_t expected = (declared_columns.size() > 0) ? declared_columns.size() - 1 : 0;
                return fail(this_line, detail::cat("more column lines than the ", expected, " required by @columns"));
            }

            Column column;
            column.language = expected_columns.front();
            expected_columns.erase(expected_columns.begin());

            const auto title_iter = column_titles.find(column.language);
            column.title = (title_iter != column_titles.end()) ? title_iter->second : column.language;

            UnitParseResult units = parse_units(trimmed, default_pos);
            if(!units.ok)
            {
                return fail(this_line, units.error);
            }
            column.units = std::move(units.units);
            column.separators = std::move(units.separators);
            pending->columns.push_back(std::move(column));
            continue;
        }

        // ----- unindented lines ---------------------------------------------
        const std::string_view first_word = trimmed.substr(0, trimmed.find_first_of(" \t"));

        const bool is_stamp = (trimmed.front() == '[') && (map.time_format == "lrc");
        const bool is_header = !is_stamp && (first_word.size() > 1) && (first_word.front() == '@')
                               && !block_section_started;

        if(is_header)
        {
            const std::string key(first_word.substr(1));
            const std::string value(
                (first_word.size() == trimmed.size()) ? std::string() : std::string(trim(trimmed.substr(first_word.size()))));

            if(!seen_headers.insert(key).second)
            {
                return fail(this_line, detail::cat("duplicate '@", key, "' header"));
            }

            if(key == "columns")
            {
                for(std::string_view item : split(value, ','))
                {
                    if(item.empty())
                    {
                        return fail(this_line, "@columns contains an empty language");
                    }
                    if(std::find(declared_columns.begin(), declared_columns.end(), std::string(item))
                       != declared_columns.end())
                    {
                        return fail(this_line, detail::cat("@columns lists '", item, "' twice"));
                    }
                    declared_columns.emplace_back(item);
                }
                if(declared_columns.empty())
                {
                    return fail(this_line, "@columns must list at least the source language");
                }
            }
            else if(key == "source")
            {
                map.source_language = value;
            }
            else if(key == "translit")
            {
                map.translit_language = value;
            }
            else if(key == "timeFormat")
            {
                if((value != "lrc") && (value != "none"))
                {
                    return fail(this_line, detail::cat("unknown @timeFormat '", value, "': expected 'lrc' or 'none'"));
                }
                map.time_format = value;
            }
            else if(key == "title")
            {
                map.title = value;
            }
            else if(key == "credit")
            {
                map.credit = value;
            }
            else if(key == "offset")
            {
                const char* begin = value.c_str();
                char* end = nullptr;
                const double parsed = std::strtod(begin, &end);
                if((end == begin) || (*end != '\0') || !std::isfinite(parsed))
                {
                    return fail(this_line, detail::cat("@offset '", value, "' is not a number"));
                }
                map.offset = parsed;
            }
            else if(key == "posDefault")
            {
                default_pos = colorization::from_code(value);
                if(default_pos == colorization::Pos::Unknown)
                {
                    return fail(this_line, detail::cat("@posDefault '", value, "' is not a known part-of-speech code"));
                }
            }
            else if((key == "pos") || (key == "posDark"))
            {
                std::map<std::string, std::string> entries;
                std::string list_error;
                if(!parse_key_value_list(value, entries, list_error))
                {
                    return fail(this_line, detail::cat("malformed @", key, ": ", list_error));
                }

                for(const auto& [code, hex] : entries)
                {
                    const colorization::Pos pos = colorization::from_code(code);
                    if(pos == colorization::Pos::Unknown)
                    {
                        map.has_unknown_pos_codes = true;
                        result.warnings.push_back(detail::cat("unknown part-of-speech code '",
                                                              code,
                                                              "' in @",
                                                              key,
                                                              "; its units are drawn in the main text colour"));
                        continue;
                    }

                    uint32_t rgb = 0;
                    if(!colorization::parse_hex(hex, rgb))
                    {
                        return fail(this_line, detail::cat("'", hex, "' in @", key, " is not a '#rrggbb' colour"));
                    }

                    if(key == "pos")
                    {
                        map.pos_light[size_t(pos)] = rgb;
                    }
                    else
                    {
                        map.pos_dark[size_t(pos)] = rgb;
                    }
                    map.palette_from_file = true;
                }
            }
            else if(key == "columnTitles")
            {
                std::string list_error;
                if(!parse_key_value_list(value, column_titles, list_error))
                {
                    return fail(this_line, detail::cat("malformed @columnTitles: ", list_error));
                }
            }
            else
            {
                // Unknown keys are warnings, not errors: a v1 reader must survive a v1.x
                // writer adding optional data.
                result.warnings.push_back(detail::cat("ignored unknown header '@", key, "'"));
            }
            continue;
        }

        // ----- timestamp lines ----------------------------------------------
        if(is_stamp)
        {
            const size_t close_index = trimmed.find(']');
            if(close_index == std::string_view::npos)
            {
                return fail(this_line, "timestamp line has no closing ']'");
            }

            double timestamp = 0.0;
            const std::string_view stamp = trimmed.substr(0, close_index + 1);
            if(!parse_timestamp(stamp, timestamp))
            {
                return fail(this_line, detail::cat("malformed timestamp '", stamp, "'"));
            }

            if(!finish_block()) return fail(result.error_line, result.error);
            if(has_next_timestamp)
            {
                return fail(this_line, "timestamp line is not followed by a source line before the next timestamp");
            }

            has_next_timestamp = true;
            next_timestamp = timestamp + map.offset;
            next_timestamp_line = this_line;

            // Tolerated for files written by the existing LRC exporters:
            // `[00:12.40]<the block's source verbatim>`. It is checked against the source
            // line that follows, so the two can never disagree silently.
            const std::string_view trailing = trim(trimmed.substr(close_index + 1));
            if(!trailing.empty())
            {
                stamp_line_source = std::string(trailing);
            }
            continue;
        }

        if(trimmed.front() == '[')
        {
            if(map.time_format.empty())
            {
                return fail(this_line,
                            "a '[' line appears before the required '@timeFormat' header; every header must come "
                            "before the first block");
            }
            // `@timeFormat none`: a '[' line is ordinary source text, by design.
        }

        // ----- a block's source line ----------------------------------------
        if(map.time_format.empty())
        {
            return fail(this_line,
                        "a lyric block appears before the required '@timeFormat' header; every header must come "
                        "before the first block");
        }
        if(declared_columns.empty())
        {
            return fail(this_line, "a lyric block appears before the required '@columns' header");
        }
        if(map.source_language.empty())
        {
            return fail(this_line, "a lyric block appears before the required '@source' header");
        }

        if(!finish_block()) return fail(result.error_line, result.error);

        Line block;
        UnitParseResult units = parse_units(trimmed, default_pos);
        if(!units.ok)
        {
            return fail(this_line, units.error);
        }

        // The source text is what remains once markers are removed: that, not the marked
        // line, is what the plain lyric view shows and what the columns must line up with.
        block.source = strip_markers(trimmed, units);
        if(block.source.empty() && !trimmed.empty())
        {
            return fail(this_line, "the source line becomes empty once its unit markers are removed");
        }
        block.source_units = std::move(units.units);
        block.source_separators = std::move(units.separators);
        block.source_pos = default_pos;

        if(has_next_timestamp)
        {
            block.timestamp = next_timestamp;
            has_next_timestamp = false;
        }

        if(stamp_line_source.has_value())
        {
            if(stamp_line_source.value() != block.source)
            {
                return fail(this_line,
                            detail::cat("the source on the stamp line ('",
                                        stamp_line_source.value(),
                                        "') does not match the source line that follows ('",
                                        block.source,
                                        "')"));
            }
            result.warnings.push_back(detail::cat("line ",
                                                  next_timestamp_line,
                                                  ": the source text on the stamp line was redundant and has been "
                                                  "ignored"));
            stamp_line_source.reset();
        }

        pending = std::move(block);
        pending_start_line = this_line;
        pending_is_timed = (map.time_format == "lrc");
        block_section_started = true;

        expected_columns.clear();
        for(const std::string& language : declared_columns)
        {
            if(language != map.source_language) expected_columns.push_back(language);
        }
    }

    if(!finish_block()) return fail(result.error_line, result.error);
    if(has_next_timestamp)
    {
        return fail(next_timestamp_line, "a timestamp line is not followed by a source line");
    }

    // ----- required headers and cross-checks --------------------------------
    if(declared_columns.empty())
    {
        return fail(0, "missing required '@columns' header");
    }
    if(map.source_language.empty())
    {
        return fail(0, "missing required '@source' header");
    }
    if(map.time_format.empty())
    {
        return fail(0, "missing required '@timeFormat' header");
    }

    const auto source_iter = std::find(declared_columns.begin(), declared_columns.end(), map.source_language);
    if(source_iter == declared_columns.end())
    {
        return fail(0, detail::cat("@source '", map.source_language, "' is not listed in @columns"));
    }
    if(source_iter != declared_columns.begin())
    {
        // The source is the block's own first line, so its position is fixed by the
        // grammar. A file that lists it elsewhere is corrected with a warning instead of
        // being rejected for a harmless authoring order.
        result.warnings.push_back(
            detail::cat("the source language '", map.source_language, "' was moved to the front of @columns"));
        declared_columns.erase(source_iter);
        declared_columns.insert(declared_columns.begin(), map.source_language);
    }

    if(!map.translit_language.empty()
       && (std::find(declared_columns.begin(), declared_columns.end(), map.translit_language)
           == declared_columns.end()))
    {
        result.warnings.push_back(detail::cat("@translit '",
                                              map.translit_language,
                                              "' is not one of @columns; the transliteration column will not be "
                                              "drawn"));
        map.translit_language.clear();
    }

    if(map.lines.empty())
    {
        return fail(0, "the bundle contains no lyric blocks");
    }

    // `finish_block` guarantees these; repeating the check here catches a future change
    // to block assembly that forgets to.
    const size_t required_columns = declared_columns.size() - 1;
    for(const Line& block : map.lines)
    {
        if(block.columns.size() != required_columns)
        {
            return fail(0,
                        detail::cat("internal error: block '",
                                    block.source,
                                    "' has ",
                                    block.columns.size(),
                                    " column(s), expected ",
                                    required_columns));
        }
        if(block.source_units.size() != block.source_separators.size())
        {
            return fail(0,
                        detail::cat("internal error: block '",
                                    block.source,
                                    "' has ",
                                    block.source_units.size(),
                                    " units but ",
                                    block.source_separators.size(),
                                    " separators"));
        }
    }

    map.column_languages = std::move(declared_columns);
    result.map = std::move(map);
    return result;
}
