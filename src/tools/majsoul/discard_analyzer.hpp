#ifndef MAHJONG_CPP_TOOLS_MAJSOUL_DISCARD_ANALYZER
#define MAHJONG_CPP_TOOLS_MAJSOUL_DISCARD_ANALYZER

#include <functional>
#include <vector>

#include "mahjong/core/expected_score_calculator.hpp"
#include "replay_types.hpp"

namespace mahjong::tools::majsoul
{

/**
 * @brief Reason a discard was not evaluated.
 */
namespace SkipReason
{

inline constexpr int None = 0;
/*! The player had already declared riichi, so the discard was forced. */
inline constexpr int Riichi = 1;
/*! The hand was further from tenpai than the configured limit. */
inline constexpr int TooFarFromTenpai = 2;
/*! The hand was not in a 14-tile discard form, so it could not be evaluated. */
inline constexpr int NotADecision = 3;
/*! An opponent had declared riichi and the hand was too far from tenpai to push. */
inline constexpr int OpponentRiichi = 4;

} // namespace SkipReason

struct AnalyzerConfig
{
    /*! Base configuration handed to ExpectedScoreCalculator. */
    ExpectedScoreCalculator::Config calc;

    /*! Skip decisions whose shanten number exceeds this limit. 6 evaluates every hand. */
    int max_shanten = 6;

    /*!
     * Once an opponent has declared riichi, skip decisions whose shanten number
     * exceeds this limit: folding rather than pushing is the normal choice, and tile
     * efficiency is not what the decision turns on. 6 disables the rule.
     */
    int max_shanten_under_riichi = 1;

    /*!
     * At or below this shanten number the search range is widened by one.
     *
     * Close to tenpai the discard is often a close call, and the wider search changes
     * which discard comes out on top in roughly one decision in twelve. Widening it
     * further is not worth the wait: going from a range of 2 to 3 was measured at
     * twenty-one times the run time to change one more decision in twenty-four, by
     * which point the expected score has all but converged.
     */
    int wide_search_shanten = 1;

    /*!
     * At or above this shanten number the expected score is calculated without the
     * extra search range.
     *
     * The calculator explores every hand within `calc.extra` of the current shanten
     * number, and that search explodes far from tenpai. On a measured hanchan the five
     * 4-shanten decisions took 52 of the 56 seconds the whole game needed, while the
     * 37 decisions at 2 shanten or better took one second between them. Those distant
     * discards are not close calls, so the budget is spent nearer to tenpai instead.
     */
    int narrow_search_shanten = 4;

    /*!
     * Hard ceiling on shanten + extra.
     *
     * Beyond this the search is not merely slow: a 5-shanten hand with the default
     * extra range has been measured at over three minutes, and it exhausts a 100 MB
     * stack and terminates the process. This cap keeps any combination of the settings
     * above out of that territory.
     */
    int max_search_depth = 5;

    /*! Evaluate forced discards made after declaring riichi. */
    bool include_riichi = false;

    /*!
     * Remove tiles visible in every player's discards and melds from the wall.
     * When false, only the analyzed hand, its melds and the dora indicators are removed,
     * matching the assumptions of the standard "nanikiru" simulator.
     */
    bool use_visible_wall = true;
};

/**
 * @brief One discard candidate and its statistics at the decision turn.
 */
struct DiscardCandidate
{
    int tile = Tile::Null;
    double exp_score = 0.0;
    double win_prob = 0.0;
    double tenpai_prob = 0.0;
    int shanten = 0;

    /*! Number of distinct necessary tile types. */
    int necessary_types = 0;

    /*! Number of necessary tiles left. */
    int necessary_tiles = 0;
};

/**
 * @brief A single discard decision of the analyzed player.
 */
struct DiscardDecision
{
    /*! Index into GameRecord::rounds. */
    int round_index = 0;

    RoundState round;

    /*! Table state as of the decision, i.e. only the dora indicators revealed so far. */
    TableState table;

    /*! Player state before the discard, with the drawn tile still in hand. */
    PlayerState player;

    /*! 1-based turn number, counted as the number of discards already made plus one. */
    int turn = 1;

    /*! Tile just drawn, or Tile::Null when the discard follows a call. */
    int drawn_tile = Tile::Null;

    /*! Tile the player actually discarded. */
    int actual_tile = Tile::Null;

    bool tsumogiri = false;

    /*! This discard is the riichi declaration. */
    bool declares_riichi = false;

    /*! The player had already declared riichi, so the discard was not a free choice. */
    bool forced_by_riichi = false;

    /*! At least one opponent had declared riichi. */
    bool opponent_riichi = false;

    /*! The expected score was calculated with a smaller search range than requested. */
    bool narrowed_search = false;

    /*! The expected score was calculated with a wider search range than requested. */
    bool widened_search = false;

    /*! Number of tiles not visible to the player. */
    int unseen_tiles = 0;

    int shanten = 0;

    /*! Why the decision was not evaluated. SkipReason::None when candidates are filled. */
    int skip_reason = SkipReason::None;

    /*! Candidates sorted by expected score, highest first. */
    std::vector<DiscardCandidate> candidates;

    /*! 1-based rank of the actual discard among the candidates. */
    int actual_rank = 0;

    double actual_exp_score = 0.0;
    double best_exp_score = 0.0;

    /*! best_exp_score - actual_exp_score, always >= 0. */
    double exp_score_loss = 0.0;

    bool evaluated() const noexcept
    {
        return skip_reason == SkipReason::None && !candidates.empty();
    }
};

struct AnalysisSummary
{
    int num_decisions = 0;
    int num_evaluated = 0;
    int num_best = 0;
    int num_skipped_riichi = 0;
    int num_skipped_shanten = 0;
    int num_skipped_opponent_riichi = 0;
    int num_narrowed_search = 0;
    int num_widened_search = 0;
    int num_skipped_other = 0;
    double total_loss = 0.0;
    double mean_loss = 0.0;
    double mean_rank = 0.0;
    double best_rate = 0.0;
};

struct AnalysisResult
{
    std::vector<DiscardDecision> decisions;
    AnalysisSummary summary;
};

/*! Called with (completed, total) as evaluation progresses. */
using ProgressCallback = std::function<void(int, int)>;

/**
 * @brief Replays the record and evaluates every discard of the analyzed player.
 * @param game Game record to analyze.
 * @param config Analyzer configuration.
 * @param progress Optional progress callback.
 * @return Decisions in chronological order together with a summary.
 */
AnalysisResult analyze_discards(const GameRecord &game, const AnalyzerConfig &config,
                                const ProgressCallback &progress = {});

} // namespace mahjong::tools::majsoul

#endif // MAHJONG_CPP_TOOLS_MAJSOUL_DISCARD_ANALYZER
