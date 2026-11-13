#pragma once

#include <guiddef.h>
#include <string>

#include "preferences.h"
#include "win32_util.h"

struct LyricMap; // The in-memory form of a LinguaMap bundle; see src/lyric_map.h

struct LyricDataCommon
{
    GUID source_id; // The source from which the lyrics were retrieved
    std::string source_path; // The path (on the originating source) at which the lyrics were found

    std::string artist; // The track artist, as reported by the source
    std::string album; // The track album, as reported by the source
    std::string title; // The track title, as reported by the source
    std::optional<int> duration_sec; // The duration of the track to which the lyrics apply, if provided by the source
};

// Raw lyric data as returned from the source
struct LyricDataRaw : public LyricDataCommon
{
    std::string lookup_id; // An ID used by the source to get the lyrics text after a search. Used only temporarily
                           // during searching.
    LyricType type; // The type of lyrics known to be contained in this text
    std::vector<uint8_t> text_bytes; // The raw bytes for the lyrics text, in an unspecified encoding

    LyricDataRaw() = default;
    explicit LyricDataRaw(LyricDataCommon common);
};

// Parsed lyric data
struct LyricDataLine
{
    std::tstring text;
    double timestamp;
};

struct LyricData : public LyricDataCommon
{
    std::optional<GUID> save_source; // The source to which the lyrics were last saved (if any)
    std::string save_path; // The path (on the save source) at which the lyrics can be found (if they've been saved)

    std::vector<std::string> tags;
    std::vector<LyricDataLine> lines;
    double timestamp_offset;

    // The word map this lyric text came from, when it came from a LinguaMap bundle.
    // Shared rather than copied because `LyricData` is copied on every lyric update and a
    // map holds thousands of small strings. Every existing code path works off `lines`,
    // which the parser fills from the bundle's sources, so a null map simply means "render
    // plain lyrics" and nothing else in the plugin changes.
    std::shared_ptr<const LyricMap> map;

    LyricData() = default;
    LyricData(const LyricData& other) = default;
    LyricData(LyricData&& other) = default;

    explicit LyricData(LyricDataCommon common);

    bool IsTimestamped() const;
    bool IsEmpty() const;

    // True when there is a usable word map to render. Kept separate from `map != nullptr`
    // so that a half-built map can never be drawn as if it were complete.
    bool HasMap() const;
    double LineTimestamp(int line_index) const;
    double LineTimestamp(size_t line_index) const;

    void RemoveTimestamps();

    LyricData& operator=(const LyricData& other) = default;
    LyricData& operator=(LyricData&& other) = default;
};
