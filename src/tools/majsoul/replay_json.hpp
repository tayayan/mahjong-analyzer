#ifndef MAHJONG_CPP_TOOLS_MAJSOUL_REPLAY_JSON
#define MAHJONG_CPP_TOOLS_MAJSOUL_REPLAY_JSON

#include <filesystem>
#include <string>

#include "replay_types.hpp"

namespace mahjong::tools::majsoul
{

/*! Schema identifier the reader accepts. */
inline constexpr const char *ReplaySchema = "majsoul-replay/1";

/**
 * @brief Parses a replay JSON document. See docs/majsoul_analyzer.md for the schema.
 * @param json Document text.
 * @param source Name used in error messages.
 * @return Reconstructed game record.
 * @throws std::runtime_error if the document is malformed.
 */
GameRecord parse_replay_json(const std::string &json, const std::string &source);

/**
 * @brief Reads and parses a normalized replay JSON file.
 * @param path Path to the JSON file.
 * @return Reconstructed game record.
 * @throws std::runtime_error if the file cannot be read or is malformed.
 */
GameRecord read_replay_json(const std::filesystem::path &path);

} // namespace mahjong::tools::majsoul

#endif // MAHJONG_CPP_TOOLS_MAJSOUL_REPLAY_JSON
