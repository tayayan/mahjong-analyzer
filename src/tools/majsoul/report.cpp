#include "report.hpp"

#include <algorithm>
#include <fstream>
#include <ostream>
#include <stdexcept>

#include <spdlog/spdlog.h>

#include "mahjong/core/string.hpp"

#ifdef LANG_EN
#define MAJSOUL_TEXT(eng, jp) eng
#else
#define MAJSOUL_TEXT(eng, jp) jp
#endif

namespace mahjong::tools::majsoul
{

namespace
{

std::string_view wind_name(const int wind)
{
    switch (wind) {
    case Tile::East:
        return MAJSOUL_TEXT("East", u8"東");
    case Tile::South:
        return MAJSOUL_TEXT("South", u8"南");
    case Tile::West:
        return MAJSOUL_TEXT("West", u8"西");
    case Tile::North:
        return MAJSOUL_TEXT("North", u8"北");
    default:
        return "?";
    }
}

std::string skip_reason_text(const int reason)
{
    switch (reason) {
    case SkipReason::Riichi:
        return MAJSOUL_TEXT("riichi declared (forced discard)", u8"立直中のためツモ切り");
    case SkipReason::TooFarFromTenpai:
        return MAJSOUL_TEXT("too far from tenpai (--max-shanten)",
                            u8"向聴数が上限超過 (--max-shanten)");
    case SkipReason::NotADecision:
        return MAJSOUL_TEXT("not a 14-tile discard form", u8"14枚形ではないため解析不可");
    case SkipReason::OpponentRiichi:
        return MAJSOUL_TEXT("an opponent declared riichi and the hand is too far from "
                            "tenpai (--max-shanten-vs-riichi)",
                            u8"他家立直中で向聴数が上限超過 (--max-shanten-vs-riichi)");
    default:
        return MAJSOUL_TEXT("no candidates", u8"候補なし");
    }
}

std::string rules_text(const GameRecord &game)
{
    std::string s;
    for (const RuleFlags rule : {RuleFlag::RedDora, RuleFlag::UraDora,
                                 RuleFlag::OpenTanyao}) {
        if (game.table_config.rule_flags & rule) {
            if (!s.empty()) {
                s += ", ";
            }
            s += RuleFlag::name(rule);
        }
    }
    return s.empty() ? MAJSOUL_TEXT("none", u8"なし") : s;
}

std::string game_length_text(const int length)
{
    return std::string(GameLength::name(length));
}

std::string escape_html(const std::string &text)
{
    std::string s;
    s.reserve(text.size());
    for (const char c : text) {
        switch (c) {
        case '&':
            s += "&amp;";
            break;
        case '<':
            s += "&lt;";
            break;
        case '>':
            s += "&gt;";
            break;
        case '"':
            s += "&quot;";
            break;
        case '\'':
            s += "&#39;";
            break;
        default:
            s += c;
            break;
        }
    }
    return s;
}

std::string format_melds(const PlayerState &player, const GlyphOptions &glyphs)
{
    if (player.melds.empty()) {
        return MAJSOUL_TEXT("none", u8"なし");
    }

    std::string s;
    for (const auto &meld : player.melds) {
        if (!s.empty()) {
            s += " ";
        }
        s += to_glyphs(meld, glyphs);
    }
    return s;
}

std::string format_necessary(const DiscardCandidate &candidate)
{
    return fmt::format(MAJSOUL_TEXT("{} kinds / {} tiles", u8"{}種{}枚"),
                       candidate.necessary_types, candidate.necessary_tiles);
}

/**
 * @brief Returns the decisions with the largest expected score loss, worst first.
 */
std::vector<const DiscardDecision *> worst_decisions(const AnalysisResult &result,
                                                     const std::size_t count)
{
    std::vector<const DiscardDecision *> ret;
    for (const auto &decision : result.decisions) {
        if (decision.evaluated() && decision.actual_rank > 1) {
            ret.push_back(&decision);
        }
    }

    std::sort(ret.begin(), ret.end(),
              [](const DiscardDecision *a, const DiscardDecision *b) {
                  return a->exp_score_loss > b->exp_score_loss;
              });

    if (ret.size() > count) {
        ret.resize(count);
    }
    return ret;
}

std::string decision_headline(const DiscardDecision &decision,
                              const GlyphOptions &glyphs)
{
    std::string s = fmt::format(MAJSOUL_TEXT("[turn {:>2}] hand {}", u8"[{:>2}巡目] 手牌 {}"),
                                decision.turn, to_glyphs(decision.player.hand, glyphs));

    if (decision.drawn_tile != Tile::Null) {
        s += fmt::format(MAJSOUL_TEXT(" draw {}", u8" ツモ {}"),
                         to_glyph(decision.drawn_tile, glyphs));
    }
    else {
        s += MAJSOUL_TEXT(" (after a call)", u8" (副露後)");
    }

    return s;
}

} // namespace

std::string format_round(const RoundState &round)
{
    return fmt::format(MAJSOUL_TEXT("{} {} Kyoku {} Honba", u8"{}{}局{}本場"),
                       wind_name(round.round_wind), round.round_number, round.honba);
}

void write_text_report(std::ostream &os, const GameRecord &game,
                       const AnalysisResult &result, const int max_candidates,
                       const int tile_style)
{
    const GlyphOptions glyphs{tile_style, false};
    const auto &target = game.players[game.target_seat];

    os << MAJSOUL_TEXT("=== Game ===\n", u8"=== 牌譜 ===\n");
    if (!game.meta.url.empty()) {
        os << fmt::format("URL: {}\n", game.meta.url);
    }
    os << fmt::format("uuid: {}\n", game.meta.uuid);
    os << fmt::format(MAJSOUL_TEXT("rules: {} / {} / {}\n", u8"ルール: {} / {} / {}\n"),
                      GameMode::name(game.table_config.game_mode),
                      game_length_text(game.game_length), rules_text(game));
    os << fmt::format(MAJSOUL_TEXT("analyzed player: seat {} {} {}\n",
                                   u8"解析対象: 席{} {} {}\n"),
                      game.target_seat, target.name, target.level);
    os << "\n";

    int current_round = -1;
    for (const auto &decision : result.decisions) {
        if (decision.round_index != current_round) {
            current_round = decision.round_index;
            os << fmt::format("--- {} ---\n", format_round(decision.round));
        }

        os << decision_headline(decision, glyphs) << "\n";
        os << fmt::format(MAJSOUL_TEXT("        melds {} / dora {} / unseen {} / "
                                       "shanten {}\n",
                                       u8"        副露 {} / ドラ表示 {} / 未確認 {}枚 / "
                                       u8"{}向聴\n"),
                          format_melds(decision.player, glyphs),
                          to_glyphs(decision.table.dora_indicators, glyphs),
                          decision.unseen_tiles, decision.shanten);

        if (!decision.evaluated()) {
            os << fmt::format(MAJSOUL_TEXT("        discarded {} - not evaluated ({})\n",
                                           u8"        実打 {} - 未解析 ({})\n"),
                              to_glyph(decision.actual_tile, glyphs),
                              skip_reason_text(decision.skip_reason));
            os << "\n";
            continue;
        }

        if (decision.actual_rank == 0) {
            os << fmt::format(MAJSOUL_TEXT("        discarded {} - not among candidates\n",
                                           u8"        実打 {} - 候補に見つかりません\n"),
                              to_glyph(decision.actual_tile, glyphs));
        }
        else {
            os << fmt::format(
                MAJSOUL_TEXT("        discarded {:<4} value {:>8.2f}  rank {}/{}  "
                             "loss {:>7.2f}{}\n",
                             u8"        実打 {:<4} 期待値 {:>8.2f}  順位 {}/{}  "
                             u8"損失 {:>7.2f}{}\n"),
                to_glyph(decision.actual_tile, glyphs), decision.actual_exp_score,
                decision.actual_rank, decision.candidates.size(),
                decision.exp_score_loss,
                decision.declares_riichi ? MAJSOUL_TEXT("  [riichi]", u8"  [立直宣言]")
                                         : "");
        }

        os << fmt::format(MAJSOUL_TEXT("        best      {:<4} value {:>8.2f}\n",
                                       u8"        最善 {:<4} 期待値 {:>8.2f}\n"),
                          to_glyph(decision.candidates.front().tile, glyphs),
                          decision.best_exp_score);

        const int shown =
            max_candidates <= 0
                ? static_cast<int>(decision.candidates.size())
                : std::min<int>(max_candidates,
                                static_cast<int>(decision.candidates.size()));

        os << fmt::format(MAJSOUL_TEXT("        {:>3} {:<4} {:>10} {:>9} {:>9} {:>4} {}\n",
                                       u8"        {:>3} {:<4} {:>10} {:>9} {:>9} {:>4} {}\n"),
                          "#", MAJSOUL_TEXT("tile", u8"打牌"),
                          MAJSOUL_TEXT("value", u8"期待値"), MAJSOUL_TEXT("win", u8"和了率"),
                          MAJSOUL_TEXT("tenpai", u8"聴牌率"),
                          MAJSOUL_TEXT("sht", u8"向聴"),
                          MAJSOUL_TEXT("acceptance", u8"受入"));

        for (int i = 0; i < shown; ++i) {
            const auto &candidate = decision.candidates[i];
            const bool is_actual = i + 1 == decision.actual_rank;
            os << fmt::format("        {:>3} {:<4} {:>10.2f} {:>8.2f}% {:>8.2f}% {:>4} {}{}\n",
                              i + 1, to_glyph(candidate.tile, glyphs), candidate.exp_score,
                              candidate.win_prob * 100, candidate.tenpai_prob * 100,
                              candidate.shanten, format_necessary(candidate),
                              is_actual ? MAJSOUL_TEXT("  <- discarded", u8"  <- 実打")
                                        : "");
        }
        os << "\n";
    }

    const auto &summary = result.summary;
    os << MAJSOUL_TEXT("=== Summary ===\n", u8"=== サマリー ===\n");
    os << fmt::format(MAJSOUL_TEXT("discards          : {}\n", u8"打牌数          : {}\n"),
                      summary.num_decisions);
    os << fmt::format(MAJSOUL_TEXT("evaluated         : {}\n", u8"解析した局面    : {}\n"),
                      summary.num_evaluated);
    os << fmt::format(MAJSOUL_TEXT("skipped (riichi)  : {}\n", u8"除外 (立直中)   : {}\n"),
                      summary.num_skipped_riichi);
    os << fmt::format(MAJSOUL_TEXT("skipped (shanten) : {}\n", u8"除外 (向聴上限) : {}\n"),
                      summary.num_skipped_shanten);
    os << fmt::format(MAJSOUL_TEXT("skipped (vs riichi): {}\n",
                                   u8"除外 (他家立直中) : {}\n"),
                      summary.num_skipped_opponent_riichi);
    if (summary.num_narrowed_search > 0) {
        os << fmt::format(
            MAJSOUL_TEXT("narrowed search   : {}\n", u8"探索を縮小      : {}\n"),
            summary.num_narrowed_search);
    }
    if (summary.num_widened_search > 0) {
        os << fmt::format(
            MAJSOUL_TEXT("widened search    : {}\n", u8"探索を拡大      : {}\n"),
            summary.num_widened_search);
    }
    if (summary.num_skipped_other > 0) {
        os << fmt::format(MAJSOUL_TEXT("skipped (other)   : {}\n",
                                       u8"除外 (その他)   : {}\n"),
                          summary.num_skipped_other);
    }
    os << fmt::format(MAJSOUL_TEXT("best discard rate : {} / {} ({:.1f}%)\n",
                                   u8"最善打牌一致    : {} / {} ({:.1f}%)\n"),
                      summary.num_best, summary.num_evaluated, summary.best_rate * 100);
    os << fmt::format(MAJSOUL_TEXT("mean rank         : {:.2f}\n",
                                   u8"平均順位        : {:.2f}\n"),
                      summary.mean_rank);
    os << fmt::format(MAJSOUL_TEXT("mean loss         : {:.2f}\n",
                                   u8"平均期待値損失  : {:.2f}\n"),
                      summary.mean_loss);
    os << fmt::format(MAJSOUL_TEXT("total loss        : {:.2f}\n",
                                   u8"合計期待値損失  : {:.2f}\n"),
                      summary.total_loss);

    const auto worst = worst_decisions(result, 5);
    if (!worst.empty()) {
        os << MAJSOUL_TEXT("\n=== Largest losses ===\n", u8"\n=== 損失の大きい局面 ===\n");
        for (const auto *decision : worst) {
            os << fmt::format(
                MAJSOUL_TEXT("{} turn {:>2}  {} -> {}  loss {:.2f}  (rank {}/{})\n",
                             u8"{} {:>2}巡目  実打 {} -> 最善 {}  損失 {:.2f}  (順位 {}/{})\n"),
                format_round(decision->round), decision->turn,
                to_glyph(decision->actual_tile, glyphs),
                to_glyph(decision->candidates.front().tile, glyphs),
                decision->exp_score_loss,
                decision->actual_rank, decision->candidates.size());
        }
    }
}

void write_html_report(const std::filesystem::path &path, const GameRecord &game,
                       const AnalysisResult &result, const int tile_style)
{
    const GlyphOptions glyphs{tile_style, true};
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) {
        throw std::runtime_error(
            fmt::format("Failed to open report file: {}", path.string()));
    }

    const auto &target = game.players[game.target_seat];
    const auto &summary = result.summary;

    ofs << R"(<!DOCTYPE html>
<html lang="ja">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>)"
        << escape_html(fmt::format(MAJSOUL_TEXT("Discard analysis {}",
                                                u8"打牌期待値レポート {}"),
                                   game.meta.uuid))
        << R"(</title>
<style>
:root { color-scheme: light dark; --fg:#1b1b1f; --bg:#ffffff; --muted:#606070;
        --line:#e2e2e8; --card:#f6f6f9; --good:#1a7f43; --bad:#c0392b; --warn:#b8860b; }
@media (prefers-color-scheme: dark) {
  :root { --fg:#e6e6ea; --bg:#16161a; --muted:#a0a0b0; --line:#2e2e36; --card:#1e1e24;
          --good:#57d68a; --bad:#ff7b6b; --warn:#e0b33c; }
}
body { margin:0; padding:24px; background:var(--bg); color:var(--fg);
       font-family:-apple-system,"Segoe UI",Meiryo,sans-serif; line-height:1.6; }
h1 { font-size:1.5rem; margin:0 0 4px; }
h2 { font-size:1.15rem; margin:32px 0 8px; border-bottom:1px solid var(--line);
     padding-bottom:4px; }
a { color:inherit; }
.meta { color:var(--muted); font-size:.9rem; margin-bottom:20px; }
.cards { display:flex; flex-wrap:wrap; gap:12px; margin:16px 0; }
.card { background:var(--card); border:1px solid var(--line); border-radius:10px;
        padding:12px 16px; min-width:150px; }
.card .label { font-size:.78rem; color:var(--muted); }
.card .value { font-size:1.4rem; font-weight:600; font-variant-numeric:tabular-nums; }
.scroll { overflow-x:auto; }
table { border-collapse:collapse; width:100%; font-size:.9rem; }
th, td { border-bottom:1px solid var(--line); padding:6px 10px; text-align:right;
         white-space:nowrap; }
th:first-child, td:first-child, th.l, td.l { text-align:left; }
thead th { color:var(--muted); font-weight:600; font-size:.8rem; }
tbody tr.best td { color:var(--good); }
tbody tr.loss td { color:var(--bad); }
tbody tr.skip td { color:var(--muted); }
tr.actual { background:color-mix(in srgb, var(--warn) 14%, transparent); }
code { font-family:ui-monospace,SFMono-Regular,Consolas,monospace; }
details { margin:4px 0 12px; }
summary { cursor:pointer; }
.num { font-variant-numeric:tabular-nums; }
)";

    // The Mahjong Tiles block only renders where a symbol font covers it, so name the
    // fonts that do and keep the rule off the surrounding Japanese text.
    ofs << ".tile { font-family:" << tile_font_stack()
        << "; font-size:1.35rem; line-height:1.3; white-space:nowrap; }\n"
        << ".aka { color:var(--bad); }\n";

    ofs << R"(</style>
</head>
<body>
)";

    ofs << "<h1>"
        << escape_html(MAJSOUL_TEXT("Discard expected value report",
                                    u8"打牌期待値レポート"))
        << "</h1>\n";

    ofs << "<div class=\"meta\">";
    if (!game.meta.url.empty()) {
        ofs << "<a href=\"" << escape_html(game.meta.url) << "\">"
            << escape_html(game.meta.url) << "</a><br>";
    }
    ofs << escape_html(fmt::format(
        MAJSOUL_TEXT("{} / {} / {} · analyzed: seat {} {} {}",
                     u8"{} / {} / {} ・ 解析対象: 席{} {} {}"),
        GameMode::name(game.table_config.game_mode),
        game_length_text(game.game_length), rules_text(game), game.target_seat,
        target.name, target.level));
    ofs << "</div>\n";

    const auto card = [&ofs](const std::string &label, const std::string &value) {
        ofs << "<div class=\"card\"><div class=\"label\">" << escape_html(label)
            << "</div><div class=\"value\">" << escape_html(value) << "</div></div>\n";
    };

    ofs << "<div class=\"cards\">\n";
    card(MAJSOUL_TEXT("evaluated", u8"解析局面数"),
         fmt::format("{}", summary.num_evaluated));
    card(MAJSOUL_TEXT("best discard rate", u8"最善打牌一致率"),
         fmt::format("{:.1f}%", summary.best_rate * 100));
    card(MAJSOUL_TEXT("mean rank", u8"平均順位"),
         fmt::format("{:.2f}", summary.mean_rank));
    card(MAJSOUL_TEXT("mean loss", u8"平均期待値損失"),
         fmt::format("{:.1f}", summary.mean_loss));
    card(MAJSOUL_TEXT("total loss", u8"合計期待値損失"),
         fmt::format("{:.0f}", summary.total_loss));
    ofs << "</div>\n";

    const auto worst = worst_decisions(result, 10);
    if (!worst.empty()) {
        ofs << "<h2>" << escape_html(MAJSOUL_TEXT("Largest losses", u8"損失の大きい局面"))
            << "</h2>\n<div class=\"scroll\"><table><thead><tr>";
        for (const char *header :
             {MAJSOUL_TEXT("round", u8"局"), MAJSOUL_TEXT("turn", u8"巡目"),
              MAJSOUL_TEXT("discarded", u8"実打"), MAJSOUL_TEXT("best", u8"最善"),
              MAJSOUL_TEXT("rank", u8"順位"), MAJSOUL_TEXT("loss", u8"損失")}) {
            ofs << "<th>" << escape_html(header) << "</th>";
        }
        ofs << "</tr></thead><tbody>\n";
        for (const auto *decision : worst) {
            ofs << "<tr class=\"loss\"><td class=\"l\">"
                << escape_html(format_round(decision->round)) << "</td><td>"
                << decision->turn << "</td><td><span class=\"tile\">"
                << to_glyph(decision->actual_tile, glyphs)
                << "</span></td><td><span class=\"tile\">"
                << to_glyph(decision->candidates.front().tile, glyphs)
                << "</span></td><td>" << decision->actual_rank << " / "
                << decision->candidates.size() << "</td><td class=\"num\">"
                << fmt::format("{:.1f}", decision->exp_score_loss) << "</td></tr>\n";
        }
        ofs << "</tbody></table></div>\n";
    }

    int current_round = -1;
    for (const auto &decision : result.decisions) {
        if (decision.round_index != current_round) {
            current_round = decision.round_index;
            ofs << "<h2>" << escape_html(format_round(decision.round)) << "</h2>\n";
        }

        // Tiles carry markup, so the line is assembled piece by piece instead of
        // being formatted and escaped as a whole.
        const auto tiles = [&ofs](const std::string &html) {
            ofs << "<span class=\"tile\">" << html << "</span>";
        };
        const bool is_best = decision.actual_rank == 1;

        ofs << "<details"
            << (decision.evaluated() && !is_best ? " open" : "") << "><summary>";
        ofs << escape_html(fmt::format(MAJSOUL_TEXT("turn {} · ", u8"{}巡目 ・ "),
                                       decision.turn));
        tiles(to_glyphs(decision.player.hand, glyphs));
        ofs << escape_html(fmt::format(MAJSOUL_TEXT(" · shanten {}", u8" ・ {}向聴"),
                                       decision.shanten));
        if (decision.drawn_tile != Tile::Null) {
            ofs << escape_html(MAJSOUL_TEXT(" · draw ", u8" ・ ツモ "));
            tiles(to_glyph(decision.drawn_tile, glyphs));
        }
        ofs << escape_html(MAJSOUL_TEXT(" · discarded ", u8" ・ 実打 "));
        tiles(to_glyph(decision.actual_tile, glyphs));

        if (!decision.evaluated()) {
            ofs << "</summary><p class=\"meta\">"
                << escape_html(skip_reason_text(decision.skip_reason))
                << "</p></details>\n";
            continue;
        }

        ofs << escape_html(fmt::format(
                   MAJSOUL_TEXT(" (rank {}/{}, loss {:.1f})",
                                u8" (順位 {}/{}, 損失 {:.1f})"),
                   decision.actual_rank, decision.candidates.size(),
                   decision.exp_score_loss))
            << "</summary>\n";

        // format_melds already carries its own markup, so it is not wrapped again.
        ofs << "<p class=\"meta\">" << escape_html(MAJSOUL_TEXT("melds ", u8"副露 "))
            << format_melds(decision.player, glyphs)
            << escape_html(MAJSOUL_TEXT(" · dora indicators ", u8" ・ ドラ表示 "));
        tiles(to_glyphs(decision.table.dora_indicators, glyphs));
        ofs << escape_html(fmt::format(MAJSOUL_TEXT(" · unseen {}", u8" ・ 未確認 {}枚"),
                                       decision.unseen_tiles))
            << "</p>\n";

        ofs << "<div class=\"scroll\"><table><thead><tr>";
        for (const char *header :
             {"#", MAJSOUL_TEXT("tile", u8"打牌"), MAJSOUL_TEXT("value", u8"期待値"),
              MAJSOUL_TEXT("win", u8"和了率"), MAJSOUL_TEXT("tenpai", u8"聴牌率"),
              MAJSOUL_TEXT("shanten", u8"向聴"),
              MAJSOUL_TEXT("acceptance", u8"受入")}) {
            ofs << "<th>" << escape_html(header) << "</th>";
        }
        ofs << "</tr></thead><tbody>\n";

        for (std::size_t i = 0; i < decision.candidates.size(); ++i) {
            const auto &candidate = decision.candidates[i];
            const bool is_actual =
                static_cast<int>(i) + 1 == decision.actual_rank;
            ofs << "<tr class=\"" << (is_actual ? "actual" : "") << "\"><td>" << i + 1
                << "</td><td class=\"l\"><span class=\"tile\">"
                << to_glyph(candidate.tile, glyphs)
                << "</span></td><td class=\"num\">"
                << fmt::format("{:.2f}", candidate.exp_score) << "</td><td class=\"num\">"
                << fmt::format("{:.2f}%", candidate.win_prob * 100)
                << "</td><td class=\"num\">"
                << fmt::format("{:.2f}%", candidate.tenpai_prob * 100)
                << "</td><td class=\"num\">" << candidate.shanten
                << "</td><td class=\"num\">" << escape_html(format_necessary(candidate))
                << "</td></tr>\n";
        }
        ofs << "</tbody></table></div></details>\n";
    }

    ofs << "</body>\n</html>\n";

    if (!ofs) {
        throw std::runtime_error(
            fmt::format("Failed to write report file: {}", path.string()));
    }
}

} // namespace mahjong::tools::majsoul
