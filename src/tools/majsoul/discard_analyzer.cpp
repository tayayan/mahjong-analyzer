#include "discard_analyzer.hpp"

#include <algorithm>
#include <array>
#include <numeric>
#include <variant>

#include "mahjong/core/shanten_calculator.hpp"

namespace mahjong::tools::majsoul
{

namespace
{

constexpr int MaxTurn = 18;

using VisibleCount = std::array<int, Tile::Length>;

// Tiles are counted in both their own slot and the slot of the corresponding normal
// tile, matching how hands and melds are counted elsewhere in the library.
void mark_visible(VisibleCount &visible, const int tile)
{
    ++visible[tile];
    if (Tile::is_red(tile)) {
        ++visible[Tile::to_normal(tile)];
    }
}

/**
 * @brief Adds the tiles a call newly exposes.
 *
 * The called tile is already visible from the discard it was taken from, and the three
 * tiles of an upgraded pon are already visible from the call that formed it.
 */
void mark_meld_visible(VisibleCount &visible, const Meld &meld)
{
    if (meld.type == MeldType::Ankan) {
        for (const int tile : meld.tiles) {
            mark_visible(visible, tile);
        }
        return;
    }

    if (meld.type == MeldType::Kakan) {
        mark_visible(visible, meld.discarded_tile);
        return;
    }

    bool skipped_called_tile = false;
    for (const int tile : meld.tiles) {
        if (!skipped_called_tile && tile == meld.discarded_tile) {
            skipped_called_tile = true;
            continue;
        }
        mark_visible(visible, tile);
    }
}

/**
 * @brief Builds the wall of tiles the analyzed player cannot see.
 *
 * @param visible Tiles exposed in discards, melds and dora indicators of every player.
 */
MergedCount build_wall(const TableConfig &table_config, const PlayerState &player,
                       const VisibleCount &visible, const bool enable_reddora)
{
    MergedCount wall{0};
    const bool is_sanma = table_config.game_mode == GameMode::Sanma;

    for (int tile = 0; tile < 34; ++tile) {
        if (is_sanma && Tile::is_sanma_disabled(tile)) {
            continue;
        }
        wall[tile] = std::max(0, 4 - visible[tile] - player.hand[tile]);
    }

    if (enable_reddora) {
        for (int tile = 34; tile < 37; ++tile) {
            if (is_sanma && Tile::is_sanma_disabled(tile)) {
                continue;
            }
            wall[tile] = std::max(0, 1 - visible[tile] - player.hand[tile]);
        }
    }

    return wall;
}

// A red five occupies both its own slot and the slot of the corresponding normal
// tile, so both have to move together.
void add_to_hand(Hand &hand, const int tile)
{
    ++hand[tile];
    if (Tile::is_red(tile)) {
        ++hand[Tile::to_normal(tile)];
    }
}

void remove_from_hand(Hand &hand, const int tile)
{
    --hand[tile];
    if (Tile::is_red(tile)) {
        --hand[Tile::to_normal(tile)];
    }
}

int count_unseen(const MergedCount &wall)
{
    return std::accumulate(wall.begin(), wall.begin() + 34, 0);
}

/**
 * @brief A decision together with the wall it must be evaluated against.
 */
struct PendingDecision
{
    DiscardDecision decision;
    MergedCount wall;
};

/**
 * @brief Replays a round and collects every discard decision of the analyzed player.
 */
void collect_round(const GameRecord &game, const int round_index,
                   const AnalyzerConfig &config,
                   std::vector<PendingDecision> &decisions)
{
    const RoundRecord &record = game.rounds[round_index];
    const int target = game.target_seat;
    const bool enable_reddora =
        (game.table_config.rule_flags & RuleFlag::RedDora) != 0;

    std::vector<PlayerState> players = record.initial.players;
    TableState table = record.initial.table;

    VisibleCount visible{};
    for (const int indicator : table.dora_indicators) {
        mark_visible(visible, indicator);
    }
    for (const auto &player : players) {
        for (const auto &meld : player.melds) {
            mark_meld_visible(visible, meld);
        }
    }

    std::vector<bool> riichi(players.size(), false);
    std::vector<int> num_discards(players.size(), 0);
    std::vector<int> last_drawn(players.size(), Tile::Null);

    for (const auto &event : record.events) {
        if (const auto *draw = std::get_if<DrawEvent>(&event)) {
            add_to_hand(players[draw->actor].hand, draw->tile);
            last_drawn[draw->actor] = draw->tile;
            continue;
        }

        if (const auto *discard = std::get_if<DiscardEvent>(&event)) {
            const int actor = discard->actor;

            if (actor == target) {
                PendingDecision pending;
                DiscardDecision &decision = pending.decision;
                decision.round_index = round_index;
                decision.round = record.initial.round;
                decision.table = table;
                decision.player = players[actor];
                decision.turn = std::clamp(num_discards[actor] + 1, 1, MaxTurn);
                decision.drawn_tile = last_drawn[actor];
                decision.actual_tile = discard->tile;
                decision.tsumogiri = discard->tsumogiri;

                pending.wall =
                    config.use_visible_wall
                        ? build_wall(game.table_config, decision.player, visible,
                                     enable_reddora)
                        : create_wall(game.table_config, table, decision.player,
                                      enable_reddora);
                decision.unseen_tiles = count_unseen(pending.wall);

                decision.forced_by_riichi = riichi[actor];
                for (std::size_t seat = 0; seat < riichi.size(); ++seat) {
                    if (static_cast<int>(seat) != actor && riichi[seat]) {
                        decision.opponent_riichi = true;
                        break;
                    }
                }
                if (decision.forced_by_riichi && !config.include_riichi) {
                    decision.skip_reason = SkipReason::Riichi;
                }

                decisions.push_back(std::move(pending));
            }

            remove_from_hand(players[actor].hand, discard->tile);
            mark_visible(visible, discard->tile);
            ++num_discards[actor];
            last_drawn[actor] = Tile::Null;
            continue;
        }

        if (const auto *call = std::get_if<CallEvent>(&event)) {
            PlayerState &player = players[call->actor];
            mark_meld_visible(visible, call->meld);

            const Meld &meld = call->meld;
            if (meld.type == MeldType::Ankan) {
                for (const int tile : meld.tiles) {
                    remove_from_hand(player.hand, tile);
                }
                player.melds.push_back(meld);
            }
            else if (meld.type == MeldType::Kakan) {
                remove_from_hand(player.hand, meld.discarded_tile);
                const int base = Tile::to_normal(meld.discarded_tile);
                for (auto &existing : player.melds) {
                    if (existing.type == MeldType::Pon &&
                        Tile::to_normal(existing.tiles.front()) == base) {
                        existing = meld;
                        break;
                    }
                }
            }
            else {
                bool skipped_called_tile = false;
                for (const int tile : meld.tiles) {
                    if (!skipped_called_tile && tile == meld.discarded_tile) {
                        skipped_called_tile = true;
                        continue;
                    }
                    remove_from_hand(player.hand, tile);
                }
                player.melds.push_back(meld);
            }

            last_drawn[call->actor] = Tile::Null;
            continue;
        }

        if (const auto *nuki = std::get_if<NukiEvent>(&event)) {
            remove_from_hand(players[nuki->actor].hand, Tile::North);
            ++players[nuki->actor].nuki_count;
            mark_visible(visible, Tile::North);
            continue;
        }

        if (const auto *riichi_event = std::get_if<RiichiEvent>(&event)) {
            riichi[riichi_event->actor] = true;
            ++table.kyotaku;
            if (riichi_event->actor == target && !decisions.empty()) {
                // The riichi event follows the declaring discard.
                DiscardDecision &last = decisions.back().decision;
                if (last.round_index == round_index) {
                    last.declares_riichi = true;
                }
            }
            continue;
        }

        if (const auto *dora = std::get_if<DoraOpenEvent>(&event)) {
            table.dora_indicators.push_back(dora->dora_indicator);
            mark_visible(visible, dora->dora_indicator);
            continue;
        }

        // Wins and draws end the round; nothing after them is a discard decision.
        break;
    }
}

DiscardCandidate to_candidate(const ExpectedScoreCalculator::Stat &stat, const int turn)
{
    DiscardCandidate candidate;
    candidate.tile = stat.tile;
    candidate.shanten = stat.shanten;
    candidate.exp_score =
        turn < static_cast<int>(stat.exp_score.size()) ? stat.exp_score[turn] : 0.0;
    candidate.win_prob =
        turn < static_cast<int>(stat.win_prob.size()) ? stat.win_prob[turn] : 0.0;
    candidate.tenpai_prob =
        turn < static_cast<int>(stat.tenpai_prob.size()) ? stat.tenpai_prob[turn] : 0.0;
    candidate.necessary_types = static_cast<int>(stat.necessary_tiles.size());
    candidate.necessary_tiles =
        std::accumulate(stat.necessary_tiles.begin(), stat.necessary_tiles.end(), 0,
                        [](const int sum, const auto &entry) {
                            return sum + std::get<1>(entry);
                        });
    return candidate;
}

/**
 * @brief Ranks candidates by expected score, breaking ties deterministically.
 */
void sort_candidates(std::vector<DiscardCandidate> &candidates)
{
    std::sort(candidates.begin(), candidates.end(),
              [](const DiscardCandidate &a, const DiscardCandidate &b) {
                  if (a.exp_score != b.exp_score) {
                      return a.exp_score > b.exp_score;
                  }
                  if (a.win_prob != b.win_prob) {
                      return a.win_prob > b.win_prob;
                  }
                  if (a.tenpai_prob != b.tenpai_prob) {
                      return a.tenpai_prob > b.tenpai_prob;
                  }
                  return a.tile < b.tile;
              });
}

/**
 * @brief Finds the rank of the tile the player actually discarded.
 *
 * Falls back to the normal tile because red fives are merged when the red dora rule is
 * disabled.
 */
int find_actual_rank(const std::vector<DiscardCandidate> &candidates, const int tile)
{
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        if (candidates[i].tile == tile) {
            return static_cast<int>(i) + 1;
        }
    }

    const int normal = Tile::to_normal(tile);
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        if (candidates[i].tile == normal) {
            return static_cast<int>(i) + 1;
        }
    }

    return 0;
}

void evaluate(const GameRecord &game, const AnalyzerConfig &config,
              PendingDecision &pending)
{
    DiscardDecision &decision = pending.decision;

    const int num_tiles =
        decision.player.num_tiles() + decision.player.num_melds() * 3;
    if (num_tiles != 14) {
        // Not a discard decision the calculator can evaluate.
        decision.skip_reason = SkipReason::NotADecision;
        return;
    }

    decision.shanten = std::get<1>(ShantenCalculator::calc(
        decision.player.hand, decision.player.num_melds(), config.calc.shanten_type,
        game.table_config.game_mode));

    if (decision.skip_reason != SkipReason::None) {
        return;
    }
    // Against a riichi, a hand this far from tenpai is normally folded, and folding is
    // not a tile efficiency question, so the expected score would not be the metric to
    // judge the discard by.
    if (decision.opponent_riichi &&
        decision.shanten > config.max_shanten_under_riichi) {
        decision.skip_reason = SkipReason::OpponentRiichi;
        return;
    }
    if (decision.shanten > config.max_shanten) {
        decision.skip_reason = SkipReason::TooFarFromTenpai;
        return;
    }

    ExpectedScoreCalculator::Config calc_config = config.calc;
    calc_config.t_min = decision.turn;
    calc_config.t_max = MaxTurn;
    calc_config.sum = 0; // derived from the wall
    calc_config.enable_reddora =
        (game.table_config.rule_flags & RuleFlag::RedDora) != 0;
    calc_config.enable_uradora =
        (game.table_config.rule_flags & RuleFlag::UraDora) != 0;

    const auto [stats, searched] = ExpectedScoreCalculator::calc(
        calc_config, game.table_config, decision.round, decision.table, decision.player,
        pending.wall);
    static_cast<void>(searched);

    decision.candidates.reserve(stats.size());
    for (const auto &stat : stats) {
        if (stat.tile == Tile::Null) {
            continue;
        }
        decision.candidates.push_back(to_candidate(stat, decision.turn));
    }

    if (decision.candidates.empty()) {
        return;
    }

    sort_candidates(decision.candidates);

    decision.best_exp_score = decision.candidates.front().exp_score;
    decision.actual_rank = find_actual_rank(decision.candidates, decision.actual_tile);
    if (decision.actual_rank > 0) {
        decision.actual_exp_score =
            decision.candidates[decision.actual_rank - 1].exp_score;
        decision.exp_score_loss =
            std::max(0.0, decision.best_exp_score - decision.actual_exp_score);
    }
}

AnalysisSummary summarize(const std::vector<DiscardDecision> &decisions)
{
    AnalysisSummary summary;
    summary.num_decisions = static_cast<int>(decisions.size());

    int rank_sum = 0;
    for (const auto &decision : decisions) {
        if (decision.skip_reason == SkipReason::Riichi) {
            ++summary.num_skipped_riichi;
            continue;
        }
        if (decision.skip_reason == SkipReason::TooFarFromTenpai) {
            ++summary.num_skipped_shanten;
            continue;
        }
        if (decision.skip_reason == SkipReason::OpponentRiichi) {
            ++summary.num_skipped_opponent_riichi;
            continue;
        }
        if (!decision.evaluated() || decision.actual_rank == 0) {
            ++summary.num_skipped_other;
            continue;
        }

        ++summary.num_evaluated;
        summary.total_loss += decision.exp_score_loss;
        rank_sum += decision.actual_rank;
        if (decision.actual_rank == 1) {
            ++summary.num_best;
        }
    }

    if (summary.num_evaluated > 0) {
        summary.mean_loss = summary.total_loss / summary.num_evaluated;
        summary.mean_rank = static_cast<double>(rank_sum) / summary.num_evaluated;
        summary.best_rate =
            static_cast<double>(summary.num_best) / summary.num_evaluated;
    }

    return summary;
}

} // namespace

AnalysisResult analyze_discards(const GameRecord &game, const AnalyzerConfig &config,
                                const ProgressCallback &progress)
{
    std::vector<PendingDecision> pending;
    for (int i = 0; i < static_cast<int>(game.rounds.size()); ++i) {
        collect_round(game, i, config, pending);
    }

    const int total = static_cast<int>(pending.size());
    for (int i = 0; i < total; ++i) {
        evaluate(game, config, pending[i]);
        if (progress) {
            progress(i + 1, total);
        }
    }

    AnalysisResult result;
    result.decisions.reserve(pending.size());
    for (auto &entry : pending) {
        result.decisions.push_back(std::move(entry.decision));
    }
    result.summary = summarize(result.decisions);

    return result;
}

} // namespace mahjong::tools::majsoul
