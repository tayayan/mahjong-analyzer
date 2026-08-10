#ifndef MAHJONG_CPP_ROUND_EVENT_HPP
#define MAHJONG_CPP_ROUND_EVENT_HPP

#include <variant>

#include "meld.hpp"

namespace mahjong
{

struct DrawEvent
{
    /*! Acting player index. */
    int actor;

    /*! Drawn tile. */
    int tile;
};

struct DiscardEvent
{
    /*! Acting player index. */
    int actor;

    /*! Discarded tile. */
    int tile;

    /*! Whether the discarded tile was drawn and discarded immediately. */
    bool tsumogiri;
};

struct CallEvent
{
    /*! Acting player index. */
    int actor;

    /*! Called meld. */
    Meld meld;
};

struct RiichiEvent
{
    /*! Acting player index. */
    int actor;
};

/**
 * @brief Extraction of a north tile as nuki dora. Sanma only.
 *
 * Added in this fork: without it a sanma replay loses track of the extracted tiles and
 * the hand size no longer adds up. See docs/majsoul_analyzer.md.
 */
struct NukiEvent
{
    /*! Acting player index. */
    int actor;
};

struct DoraOpenEvent
{
    /*! Opened dora indicator tile. */
    int dora_indicator;
};

struct RonEvent
{
    /*! Winning player index. */
    int winner;

    /*! Losing player index. */
    int loser;

    /*! Winning tile. */
    int winning_tile;
};

struct TsumoEvent
{
    /*! Winning player index. */
    int winner;

    /*! Winning tile. */
    int winning_tile;
};

struct RyukyokuEvent
{
    /*! Exhaustive or abortive draw type. */
    int type;
};

using RoundEvent =
    std::variant<DrawEvent, DiscardEvent, CallEvent, RiichiEvent, NukiEvent,
                 DoraOpenEvent, RonEvent, TsumoEvent, RyukyokuEvent>;

} // namespace mahjong

#endif // MAHJONG_CPP_ROUND_EVENT_HPP
