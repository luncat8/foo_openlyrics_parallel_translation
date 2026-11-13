#include "stdafx.h"

#include "logging.h"
#include "lyric_map.h"
#include "lyric_source.h"
#include "preferences.h"
#include "tag_util.h"
#include "win32_util.h"

// Reads LinguaMap bundles from disk: `<Artist> - <Title>.lmz.txt` next to (and derived
// exactly like) the `.lrc`/`.txt` files that `LocalFileSource` reads, so the user's
// *filename format* and *lyrics directory* preferences govern bundles too.
//
// This source is deliberately read-only: the plugin never writes a bundle, because a
// bundle's content (unit segmentation, translations, palette) is authored in the LinguaMap
// tool and a round-trip through the plugin's plain-lyric save path would destroy it.

// NOTE: This GUID is part of users' saved source configurations. It must never change.
// clang-format off: GUIDs should be one line
static const GUID src_guid = { 0x2f4a9c71, 0x8d3b, 0x4e6a, { 0x9c, 0x15, 0x7a, 0x62, 0xd0, 0x4b, 0x8e, 0x33 } };
// clang-format on

class LinguamapFileSource : public LyricSourceBase
{
    const GUID& id() const final
    {
        return src_guid;
    }
    std::tstring_view friendly_name() const final
    {
        return _T("LinguaMap word maps");
    }
    bool is_local() const final
    {
        return true;
    }
    bool is_dummy() const final
    {
        return false;
    }

    std::vector<LyricDataRaw> search(metadb_handle_ptr track,
                                     const metadb_v2_rec_t& track_info,
                                     abort_callback& abort) final;
    bool lookup(LyricDataRaw& data, abort_callback& abort) final;

    std::string save(metadb_handle_ptr track,
                     const metadb_v2_rec_t& track_info,
                     bool is_timestamped,
                     std::string_view lyrics,
                     bool allow_overwrite,
                     abort_callback& abort) final;
    bool delete_persisted(metadb_handle_ptr track, const std::string& path) final;

    std::tstring get_file_path(metadb_handle_ptr track, const LyricData& lyrics) final;
};
static const LyricSourceFactory<LinguamapFileSource> src_factory;

std::vector<LyricDataRaw> LinguamapFileSource::search(metadb_handle_ptr track,
                                                      const metadb_v2_rec_t& track_info,
                                                      abort_callback& /*abort*/)
{
    std::string file_path_prefix = preferences::saving::filename(track, track_info);
    if(file_path_prefix.empty())
    {
        LOG_ERROR("Failed to determine the LinguaMap bundle path");
        return {};
    }

    const std::string file_path = file_path_prefix + ".lmz.txt";
    LOG_INFO("Querying for a LinguaMap bundle at %s...", file_path.c_str());

    std::vector<LyricDataRaw> output;
    try
    {
        // `g_exists` is case-insensitive on Windows' file systems, matching how Explorer
        // and `LocalFileSource` behave; we deliberately do not lower-case the path.
        if(!filesystem::g_exists(file_path.c_str(), fb2k::mainAborter()))
        {
            LOG_INFO("No LinguaMap bundle found at %s", file_path.c_str());
            return {};
        }

        LyricDataRaw result = {};
        result.source_id = id();
        result.source_path = file_path;
        result.artist = track_metadata(track_info, "artist");
        result.album = track_metadata(track_info, "album");
        result.title = track_metadata(track_info, "title");

        // A bundle carries its own timing, so it is always a "synced" candidate: if it is
        // untimed (@timeFormat none) the parser reports that and the plain view is used.
        result.type = LyricType::Synced;
        result.lookup_id = file_path;
        output.push_back(std::move(result));
    }
    catch(const std::exception& e)
    {
        LOG_WARN("Failed to look for a LinguaMap bundle at %s: %s", file_path.c_str(), e.what());
    }

    return output;
}

bool LinguamapFileSource::lookup(LyricDataRaw& data, abort_callback& abort)
{
    const std::string& file_path = data.lookup_id;
    LOG_INFO("Reading LinguaMap bundle %s...", file_path.c_str());

    try
    {
        file_ptr file;
        filesystem::g_open_read(file, file_path.c_str(), abort);

        // `read_till_eof` rather than `read_string_raw`: the same reason `LocalFileSource`
        // uses it (mojibake and truncated reads on some encodings otherwise).
        pfc::array_t<uint8_t> file_bytes;
        file->read_till_eof(file_bytes, abort);

        data.text_bytes.assign(file_bytes.begin(), file_bytes.end());
        LOG_INFO("Read %zu bytes of LinguaMap bundle", data.text_bytes.size());
        return true;
    }
    catch(const std::exception& e)
    {
        LOG_WARN("Failed to read LinguaMap bundle %s: %s", file_path.c_str(), e.what());
        return false;
    }
}

std::string LinguamapFileSource::save(metadb_handle_ptr /*track*/,
                                      const metadb_v2_rec_t& /*track_info*/,
                                      bool /*is_timestamped*/,
                                      std::string_view /*lyrics*/,
                                      bool /*allow_overwrite*/,
                                      abort_callback& /*abort*/)
{
    // Never reached in practice: this source is not offered as a save target (see the
    // saving preferences, which only list writable sources). Refusing loudly here means a
    // future change cannot silently overwrite an authored bundle with plain lyrics.
    LOG_WARN("Refusing to save lyrics into a LinguaMap bundle: bundles are authored outside OpenLyrics");
    throw std::runtime_error("LinguaMap bundles are read-only in OpenLyrics");
}

bool LinguamapFileSource::delete_persisted(metadb_handle_ptr /*track*/, const std::string& /*path*/)
{
    // Deleting a bundle would throw away hand-authored translations, so it is not offered.
    LOG_WARN("Refusing to delete a LinguaMap bundle from OpenLyrics");
    return false;
}

std::tstring LinguamapFileSource::get_file_path(metadb_handle_ptr /*track*/, const LyricData& lyrics)
{
    if(lyrics.source_id == src_guid)
    {
        return to_tstring(lyrics.source_path);
    }
    return _T("");
}
