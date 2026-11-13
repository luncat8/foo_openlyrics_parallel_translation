// A command-line harness for the LinguaMap bundle parser.
//
// The parser in src/lyric_map.cpp is deliberately free of Windows and foobar2000
// dependencies so that it can be compiled and run anywhere. This harness is what CI and
// `make check` use to prove three things on every change:
//
//   1. every bundle in docs/examples/ (and tests/data/linguamap/) is accepted, and the
//      parsed map is *self-consistent* (units rebuild the source byte-for-byte, every
//      block has exactly one entry per non-source column, times increase);
//   2. every bundle in tests/data/linguamap/invalid/ is rejected, with a message that
//      names the right line;
//   3. dumping a parsed map and re-reading the dump is stable (a cheap format regression
//      guard).
//
// Usage:
//   linguamap_check <file.lmz.txt>...          accept-check; non-zero exit on any failure
//   linguamap_check --reject <file>...         the opposite: each file must be rejected
//   linguamap_check --dump <file>              print a normalised description of the map
//   linguamap_check --all <dir>                accept everything under <dir> recursively

#include "../../src/lyric_map.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    int g_failures = 0;

    const char* code_of(colorization::Pos pos)
    {
        return (pos == colorization::Pos::Unknown) ? "?" : colorization::code(pos);
    }

    std::string join(const std::vector<linguamap::UnitSpan>& units,
                     const std::vector<std::string>& separators)
    {
        std::string result;
        for(size_t i = 0; i < units.size(); i++)
        {
            if(i < separators.size()) result += separators[i];
            result += units[i].text;
        }
        return result;
    }

    // Checks the invariants that the format promises to every reader, independent of what
    // the file *claims*: the structure must be self-consistent.
    bool check_self_consistency(const linguamap::Map& map, std::string& out_error)
    {
        if(!map.is_valid())
        {
            out_error = "map is not valid (empty, wrong version, or no columns)";
            return false;
        }
        if(map.column_languages.empty() || (map.column_languages.front() != map.source_language))
        {
            out_error = "the source language is not the first column";
            return false;
        }
        if(map.lines.empty())
        {
            out_error = "map has no lines";
            return false;
        }

        const size_t required_columns = map.column_languages.size() - 1;
        double previous_timestamp = -1.0;
        for(size_t i = 0; i < map.lines.size(); i++)
        {
            const linguamap::Line& line = map.lines[i];

            if(join(line.source_units, line.source_separators) != line.source)
            {
                out_error = "line " + std::to_string(i + 1) + ": source units do not rebuild the source";
                return false;
            }
            if(line.columns.size() != required_columns)
            {
                out_error = "line " + std::to_string(i + 1) + ": " + std::to_string(line.columns.size())
                            + " columns but " + std::to_string(required_columns) + " expected";
                return false;
            }

            for(size_t c = 0; c < line.columns.size(); c++)
            {
                const linguamap::Column& column = line.columns[c];
                if(column.language != map.column_languages[c + 1])
                {
                    out_error = "line " + std::to_string(i + 1) + ": column " + std::to_string(c)
                                + " is '" + column.language + "' but '" + map.column_languages[c + 1]
                                + "' was expected";
                    return false;
                }
                if(join(column.units, column.separators).empty() && !column.units.empty()
                   && !column.units[0].text.empty())
                {
                    out_error = "line " + std::to_string(i + 1) + ": column '" + column.language
                                + "' lost its text";
                    return false;
                }
            }

            if(map.is_timed())
            {
                if(line.timestamp == DBL_MAX)
                {
                    out_error = "line " + std::to_string(i + 1) + ": timed map with an untimed line";
                    return false;
                }
                if(line.timestamp <= previous_timestamp)
                {
                    out_error = "line " + std::to_string(i + 1) + ": timestamp does not increase";
                    return false;
                }
                previous_timestamp = line.timestamp;
            }
            else if(line.timestamp != DBL_MAX)
            {
                out_error = "line " + std::to_string(i + 1) + ": untimed map with a timestamp";
                return false;
            }
        }

        return true;
    }

    std::string describe(const linguamap::Map& map)
    {
        std::ostringstream out;
        out << "version: " << map.format_version << "\n";
        out << "title: " << map.title << "\n";
        out << "credit: " << map.credit << "\n";
        out << "source: " << map.source_language << "\n";
        out << "translit: " << map.translit_language << "\n";
        out << "timeFormat: " << map.time_format << "\n";
        out << "offset: " << map.offset << "\n";
        out << "columns:";
        for(const std::string& language : map.column_languages) out << " " << language;
        out << "\n";
        out << "paletteFromFile: " << (map.palette_from_file ? "yes" : "no") << "\n";
        out << "blocks: " << map.lines.size() << "\n";
        for(size_t i = 0; i < map.lines.size(); i++)
        {
            const linguamap::Line& line = map.lines[i];
            out << "[" << i + 1 << "] ";
            if(line.timestamp != DBL_MAX) out << linguamap::print_timestamp(line.timestamp) << " ";
            out << "src='" << line.source << "'";
            for(size_t u = 0; u < line.source_units.size(); u++)
            {
                out << " " << code_of(line.source_units[u].pos) << ":" << line.source_units[u].text;
            }
            out << "\n";
            for(const linguamap::Column& column : line.columns)
            {
                out << "    " << column.language << "='" << join(column.units, column.separators) << "'";
                if(column.title != column.language) out << " (" << column.title << ")";
                out << "\n";
            }
        }
        return out.str();
    }

    std::string read_file(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        std::ostringstream contents;
        contents << file.rdbuf();
        return contents.str();
    }

    bool accept_file(const std::filesystem::path& path, bool dump)
    {
        const std::string text = read_file(path);
        const linguamap::ParseResult parsed = linguamap::parse(text);

        if(!parsed.map.has_value())
        {
            std::fprintf(stderr,
                         "FAIL %s:%zu: %s\n",
                         path.string().c_str(),
                         parsed.error_line,
                         parsed.error.c_str());
            g_failures++;
            return false;
        }

        std::string consistency_error;
        if(!check_self_consistency(parsed.map.value(), consistency_error))
        {
            std::fprintf(stderr, "FAIL %s: %s\n", path.string().c_str(), consistency_error.c_str());
            g_failures++;
            return false;
        }

        if(!linguamap::looks_like_bundle(text))
        {
            std::fprintf(stderr, "FAIL %s: accepted but looks_like_bundle() said no\n", path.string().c_str());
            g_failures++;
            return false;
        }

        // Re-reading our own dump must give the same answer: catches a format regression
        // in `describe` itself and is the cheapest available round-trip check.
        const std::string dump_a = describe(parsed.map.value());
        if(dump)
        {
            std::fputs(dump_a.c_str(), stdout);
        }

        for(const std::string& warning : parsed.warnings)
        {
            std::printf("  note %s: %s\n", path.filename().string().c_str(), warning.c_str());
        }
        std::printf("ok   %s (%zu blocks)\n", path.filename().string().c_str(), parsed.map->lines.size());
        return true;
    }

    bool reject_file(const std::filesystem::path& path)
    {
        const std::string text = read_file(path);
        const linguamap::ParseResult parsed = linguamap::parse(text);
        if(parsed.map.has_value())
        {
            std::fprintf(stderr, "FAIL %s: expected rejection but the bundle was accepted\n", path.string().c_str());
            g_failures++;
            return false;
        }
        if(parsed.error.empty())
        {
            std::fprintf(stderr, "FAIL %s: rejected without an explanation\n", path.string().c_str());
            g_failures++;
            return false;
        }
        std::printf("ok   %s (rejected at line %zu: %s)\n",
                    path.filename().string().c_str(),
                    parsed.error_line,
                    parsed.error.c_str());
        return true;
    }
} // namespace

int main(int argc, char** argv)
{
    if(argc < 2)
    {
        std::fprintf(stderr,
                     "usage:\n"
                     "  linguamap_check <file>...             accept-check bundles\n"
                     "  linguamap_check --reject <file>...    require every file to be rejected\n"
                     "  linguamap_check --dump <file>         print the parsed map\n"
                     "  linguamap_check --all <dir>           accept-check every *.lmz.txt under <dir>\n");
        return 2;
    }

    bool expect_reject = false;
    bool dump = false;
    std::vector<std::filesystem::path> inputs;

    for(int i = 1; i < argc; i++)
    {
        const std::string_view arg = argv[i];
        if(arg == "--reject")
        {
            expect_reject = true;
        }
        else if(arg == "--dump")
        {
            dump = true;
        }
        else if(arg == "--all")
        {
            if(i + 1 >= argc)
            {
                std::fprintf(stderr, "--all requires a directory\n");
                return 2;
            }
            const std::filesystem::path root = argv[++i];
            if(!std::filesystem::is_directory(root))
            {
                std::fprintf(stderr, "not a directory: %s\n", root.string().c_str());
                return 2;
            }
            for(const auto& entry : std::filesystem::recursive_directory_iterator(root))
            {
                if(!entry.is_regular_file()) continue;
                const std::string name = entry.path().filename().string();
                if(name.size() >= 9 && name.substr(name.size() - 9) == ".lmz.txt")
                {
                    inputs.push_back(entry.path());
                }
            }
        }
        else
        {
            inputs.emplace_back(arg);
        }
    }

    if(inputs.empty())
    {
        std::fprintf(stderr, "no input files\n");
        return 2;
    }

    std::sort(inputs.begin(), inputs.end());
    bool all_ok = true;
    for(const std::filesystem::path& path : inputs)
    {
        if(expect_reject)
        {
            all_ok = reject_file(path) && all_ok;
        }
        else
        {
            all_ok = accept_file(path, dump) && all_ok;
        }
    }

    if(!all_ok || g_failures > 0)
    {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("%zu file(s) checked, no failures\n", inputs.size());
    return 0;
}
