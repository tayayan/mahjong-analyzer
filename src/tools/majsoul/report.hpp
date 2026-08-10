#ifndef MAHJONG_CPP_TOOLS_MAJSOUL_REPORT
#define MAHJONG_CPP_TOOLS_MAJSOUL_REPORT

#include <filesystem>
#include <iosfwd>
#include <string>

#include "discard_analyzer.hpp"
#include "replay_types.hpp"
#include "tile_glyph.hpp"

namespace mahjong::tools::majsoul
{

/**
 * @brief Describes a round as "東1局 0本場".
 */
std::string format_round(const RoundState &round);

/**
 * @brief Writes the analysis as a plain text report.
 * @param os Destination stream.
 * @param game Analyzed game record.
 * @param result Analysis result.
 * @param max_candidates Number of discard candidates listed per decision.
 * @param tile_style How tiles are written. See TileStyle.
 */
void write_text_report(std::ostream &os, const GameRecord &game,
                       const AnalysisResult &result, int max_candidates,
                       int tile_style);

/**
 * @brief Writes the analysis as a self-contained HTML report.
 * @param path Destination file.
 * @param game Analyzed game record.
 * @param result Analysis result.
 * @param tile_style How tiles are written. See TileStyle.
 * @throws std::runtime_error if the file cannot be written.
 */
void write_html_report(const std::filesystem::path &path, const GameRecord &game,
                       const AnalysisResult &result, int tile_style);

} // namespace mahjong::tools::majsoul

#endif // MAHJONG_CPP_TOOLS_MAJSOUL_REPORT
