#ifndef MAHJONG_CPP_TOOLS_MAJSOUL_REPLAY_TYPES
#define MAHJONG_CPP_TOOLS_MAJSOUL_REPLAY_TYPES

#include <string>
#include <vector>

#include "mahjong/types/types.hpp"

namespace mahjong::tools::majsoul
{

/**
 * @brief Metadata of a fetched game record.
 */
struct GameMeta
{
    /*! Game record uuid contained in the paipu URL. */
    std::string uuid;

    /*! Paipu URL the record was fetched from. */
    std::string url;

    /*! Schema version of the normalized replay JSON. */
    std::string schema;
};

/**
 * @brief Player profile in a game record.
 */
struct PlayerProfile
{
    /*! Seat index in [0, num_players). */
    int seat = PlayerIndex::Null;

    /*! Nickname. */
    std::string name;

    /*! Rank string as shown by the client, e.g. "雀豪★2". */
    std::string level;
};

/**
 * @brief Reconstructed game record.
 */
struct GameRecord
{
    GameMeta meta;

    /*! Table configuration shared by all rounds. */
    TableConfig table_config;

    /*! Tonpu or hanchan. */
    int game_length = GameLength::Null;

    /*! Seat index of the player to be analyzed. */
    int target_seat = PlayerIndex::Null;

    std::vector<PlayerProfile> players;

    std::vector<RoundRecord> rounds;

    int num_players() const noexcept
    {
        return static_cast<int>(players.size());
    }
};

} // namespace mahjong::tools::majsoul

#endif // MAHJONG_CPP_TOOLS_MAJSOUL_REPLAY_TYPES
