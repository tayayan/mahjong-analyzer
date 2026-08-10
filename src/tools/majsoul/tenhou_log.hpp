#ifndef MAHJONG_CPP_TOOLS_MAJSOUL_TENHOU_LOG
#define MAHJONG_CPP_TOOLS_MAJSOUL_TENHOU_LOG

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace mahjong::tools::majsoul
{

/**
 * @brief One Tenhou-format log found in a saved report.
 */
struct TenhouLog
{
    /*! Seat the report was written for, taken from the viewer link's "tw=". */
    int viewed_seat = 0;

    /*! The log document as JSON text. */
    std::string json;
};

/**
 * @brief Pulls every Tenhou JSON log out of an mjai-reviewer HTML report.
 *
 * The report embeds one log per kyoku in the paipu viewer's iframe.
 */
std::vector<TenhouLog> extract_tenhou_logs(const std::string &text);

/**
 * @brief Converts saved log text into the replay JSON the analyzer reads.
 *
 * Accepts an mjai-reviewer HTML report, a bare Tenhou JSON log, or a replay JSON that
 * has already been converted, in which case it is returned unchanged.
 *
 * @param text File contents.
 * @param seat_override Seat to analyze, or PlayerIndex::Null to use the report's.
 * @param url Paipu URL to record in the replay, may be empty.
 * @return Replay JSON text.
 * @throws std::runtime_error if no usable log is present.
 */
std::string convert_saved_log(const std::string &text, int seat_override,
                              const std::string &url);

/**
 * @brief Reads a saved log file and converts it. See convert_saved_log().
 */
std::string convert_saved_log_file(const std::filesystem::path &path,
                                   int seat_override, const std::string &url);

} // namespace mahjong::tools::majsoul

#endif // MAHJONG_CPP_TOOLS_MAJSOUL_TENHOU_LOG
