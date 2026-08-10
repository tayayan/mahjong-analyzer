#ifdef _WIN32
// Must precede every other header: windows.h otherwise defines min/max macros that
// break std::numeric_limits<>::max().
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// WIN32_LEAN_AND_MEAN leaves out ShellExecuteW, used to open the finished report.
#include <conio.h>
#include <shellapi.h>
#endif

#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <boost/dll.hpp>
#include <spdlog/spdlog.h>

#include "discard_analyzer.hpp"
#include "replay_json.hpp"
#include "report.hpp"
#include "tenhou_log.hpp"

#ifdef LANG_EN
#define MAJSOUL_PROMPT "\nPress any key to close..."
#else
#define MAJSOUL_PROMPT u8"\n何かキーを押すと終了します..."
#endif

using namespace mahjong;
using namespace mahjong::tools::majsoul;

namespace
{

struct Options
{
    /*! Saved log or replay JSON to analyze. */
    std::filesystem::path input;

    /*! Where to keep the replay JSON the log was converted into. */
    std::filesystem::path out_replay_path;

    std::filesystem::path html_path;

    int seat = PlayerIndex::Null;
    int max_candidates = 6;
    int tile_style = TileStyle::Unicode;
    bool quiet = false;
    bool open_report = true;
    AnalyzerConfig analyzer;
};

void print_usage(std::ostream &os)
{
    os << R"(Usage: analyze_majsoul_paipu [options] <log file>

Evaluates every discard the analyzed player made and reports its expected score, its
rank among all discard candidates, and the discard that maximizes the expected score.

Drop a log file on the executable to analyze it: the report is written next to the log
and opened in a browser.

Input:
  <file.html>            An mjai-reviewer report saved from the browser. It embeds the
                         full game log of every round.
  <file.json>            A Tenhou JSON log, or a replay JSON written by --out-replay.

Output:
  --html <file>          Write the HTML report here.
  --out-replay <file>    Keep the replay JSON the log was converted into.
  --candidates <n>       Discard candidates listed per decision (0 = all, default 6).
  --tile-style <style>   "unicode" writes tiles as Mahjong Tiles characters (default),
                         "mpsz" writes them as 3m/0p/1z. Switch to mpsz if the console
                         font has no glyphs for the Mahjong Tiles block.
  --no-open              Do not open the report in a browser after a dropped-file run.
  --quiet                Do not print progress to stderr.

Analysis:
  --seat <n>             Seat to analyze. Defaults to the seat the report reviews.
  --max-shanten <n>      Skip decisions with a shanten number above this. The default
                         of 6 evaluates every hand.
  --max-shanten-vs-riichi <n>
                         Once an opponent has declared riichi, skip decisions with a
                         shanten number above this (default 1): folding rather than
                         pushing is the normal choice there. Pass 6 to disable.
  --extra <n>            Search range of the expected score calculator (default 1).
  --include-riichi       Also evaluate forced discards made after declaring riichi.
  --simple-wall          Only remove the analyzed hand, its melds and the dora
                         indicators from the wall, ignoring every player's discards.
  --no-shanten-down      Disallow shanten down.
  --no-tegawari          Disallow tegawari.

  -h, --help             Show this help.
)";
}

[[noreturn]] void fail(const std::string &message)
{
    throw std::runtime_error(message);
}

std::string next_argument(const std::vector<std::string> &args, std::size_t &i)
{
    if (i + 1 >= args.size()) {
        fail(fmt::format("Option {} requires a value.", args[i]));
    }
    return args[++i];
}

int parse_int(const std::string &text, const std::string &option)
{
    try {
        return std::stoi(text);
    }
    catch (const std::exception &) {
        fail(fmt::format("Option {} requires an integer but got \"{}\".", option, text));
    }
}

Options parse_options(const std::vector<std::string> &args, bool &show_help)
{
    Options options;
    options.analyzer.calc.extra = 1;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string &arg = args[i];

        if (arg == "-h" || arg == "--help") {
            show_help = true;
            return options;
        }
        else if (arg == "--html") {
            options.html_path = next_argument(args, i);
        }
        else if (arg == "--out-replay") {
            options.out_replay_path = next_argument(args, i);
        }
        else if (arg == "--candidates") {
            options.max_candidates = parse_int(next_argument(args, i), arg);
        }
        else if (arg == "--tile-style") {
            const std::string value = next_argument(args, i);
            options.tile_style = parse_tile_style(value);
            if (options.tile_style < 0) {
                fail(fmt::format(
                    "Unknown --tile-style \"{}\". Use \"unicode\" or \"mpsz\".", value));
            }
        }
        else if (arg == "--seat") {
            options.seat = parse_int(next_argument(args, i), arg);
        }
        else if (arg == "--max-shanten") {
            options.analyzer.max_shanten = parse_int(next_argument(args, i), arg);
        }
        else if (arg == "--max-shanten-vs-riichi") {
            options.analyzer.max_shanten_under_riichi =
                parse_int(next_argument(args, i), arg);
        }
        else if (arg == "--extra") {
            options.analyzer.calc.extra = parse_int(next_argument(args, i), arg);
        }
        else if (arg == "--include-riichi") {
            options.analyzer.include_riichi = true;
        }
        else if (arg == "--simple-wall") {
            options.analyzer.use_visible_wall = false;
        }
        else if (arg == "--no-shanten-down") {
            options.analyzer.calc.enable_shanten_down = false;
        }
        else if (arg == "--no-tegawari") {
            options.analyzer.calc.enable_tegawari = false;
        }
        else if (arg == "--no-open") {
            options.open_report = false;
        }
        else if (arg == "--quiet") {
            options.quiet = true;
        }
        else if (!arg.empty() && arg[0] == '-') {
            fail(fmt::format("Unknown option: {}", arg));
        }
        else if (options.input.empty()) {
            options.input = arg;
        }
        else {
            fail(fmt::format("Unexpected argument: {}", arg));
        }
    }

    return options;
}

/**
 * @brief Returns whether the tool owns its console window.
 *
 * True when a file was dropped on the executable or it was double-clicked: Explorer
 * creates a console just for us, so it disappears the moment main returns. Launched
 * from a shell, the shell is attached to the same console too.
 */
bool owns_console()
{
#ifdef _WIN32
    DWORD pids[2] = {};
    return GetConsoleProcessList(pids, 2) <= 1;
#else
    return false;
#endif
}

/**
 * @brief Keeps the console open so a dropped-file run can be read.
 */
void wait_before_closing(const bool enabled)
{
    if (!enabled) {
        return;
    }

    std::cout << MAJSOUL_PROMPT << std::flush;
#ifdef _WIN32
    // Read the console directly: stdin may be redirected even though the window is
    // ours, in which case std::cin would hit EOF and close the window immediately.
    _getch();
#else
    std::cin.get();
#endif
}

/**
 * @brief Opens the report in the default browser.
 */
void open_in_browser(const std::filesystem::path &path)
{
#ifdef _WIN32
    ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
    static_cast<void>(path);
#endif
}

/**
 * @brief Report path used when a file is dropped on the executable.
 */
std::filesystem::path default_report_path(const std::filesystem::path &input)
{
    std::filesystem::path report = input;
    report.replace_filename(input.stem().string() + "_report.html");
    return report;
}

/**
 * @brief Reads the input file and turns it into a game record.
 */
GameRecord load_game(const Options &options)
{
    if (!std::filesystem::exists(options.input)) {
        fail(fmt::format("No such file: {}", options.input.string()));
    }

    if (!options.quiet) {
        std::cerr << fmt::format("Reading {} ...\n", options.input.filename().string());
    }

    const std::string replay = convert_saved_log_file(options.input, options.seat,
                                                      options.input.string());

    if (!options.out_replay_path.empty()) {
        std::ofstream ofs(options.out_replay_path, std::ios::binary);
        if (!ofs) {
            fail(fmt::format("Failed to write {}.", options.out_replay_path.string()));
        }
        ofs << replay;
    }

    return parse_replay_json(replay, options.input.filename().string());
}

} // namespace

int main(int argc, char *argv[])
{
#ifdef _WIN32
    // The report is UTF-8: Japanese labels and Mahjong Tiles characters both need the
    // console to be in UTF-8, otherwise they come out as garbage.
    SetConsoleOutputCP(CP_UTF8);
#endif

    // A file dropped on the executable gets its own console, which would vanish with
    // the results still on it.
    const bool standalone = owns_console();
    const std::vector<std::string> args(argv + 1, argv + argc);

    if (args.empty()) {
        print_usage(std::cout);
        wait_before_closing(standalone);
        return 1;
    }

    try {
        bool show_help = false;
        Options options = parse_options(args, show_help);
        if (show_help) {
            print_usage(std::cout);
            return 0;
        }

        if (options.input.empty()) {
            fail("No input given: pass a saved log file.");
        }
        if (standalone && options.html_path.empty()) {
            // Dropped on the executable: the console is not where the results will be
            // read, so write the report next to the log.
            options.html_path = default_report_path(options.input);
        }

        GameRecord game = load_game(options);

        if (options.seat != PlayerIndex::Null) {
            if (options.seat < 0 || options.seat >= game.num_players()) {
                fail(fmt::format("--seat must be in [0, {}).", game.num_players()));
            }
            game.target_seat = options.seat;
        }

        const auto start = std::chrono::steady_clock::now();

        ProgressCallback progress;
        if (!options.quiet) {
            progress = [](const int done, const int total) {
                std::cerr << fmt::format("\rAnalyzing {}/{}", done, total);
                if (done == total) {
                    std::cerr << std::endl;
                }
            };
        }

        const AnalysisResult result =
            analyze_discards(game, options.analyzer, progress);

        const auto end = std::chrono::steady_clock::now();
        const auto elapsed_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

        write_text_report(std::cout, game, result, options.max_candidates,
                          options.tile_style);

        if (!options.html_path.empty()) {
            write_html_report(options.html_path, game, result, options.tile_style);
            std::cout << fmt::format("\nHTML report: {}\n", options.html_path.string());
            if (standalone && options.open_report) {
                open_in_browser(options.html_path);
            }
        }

        if (!options.quiet) {
            std::cerr << fmt::format("Analyzed {} decisions in {} ms.\n",
                                     result.summary.num_evaluated, elapsed_ms);
        }

        wait_before_closing(standalone);
        return 0;
    }
    catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        wait_before_closing(standalone);
        return 1;
    }
}
