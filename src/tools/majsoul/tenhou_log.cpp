#include "tenhou_log.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <spdlog/spdlog.h>

#include "mahjong/types/types.hpp"
#include "replay_json.hpp"

namespace mahjong::tools::majsoul
{

namespace
{

using rapidjson::Value;

[[noreturn]] void fail(const std::string &message)
{
    throw std::runtime_error(message);
}

// -- tiles -----------------------------------------------------------------

/**
 * @brief Converts a Tenhou tile code to mpsz notation.
 *
 * 11-19 manzu, 21-29 pinzu, 31-39 souzu, 41-47 honors, 51/52/53 the red fives.
 */
std::string to_tile(const int code)
{
    switch (code) {
    case 51:
        return "0m";
    case 52:
        return "0p";
    case 53:
        return "0s";
    default:
        break;
    }

    const int suit = code / 10;
    const int number = code % 10;
    if (suit < 1 || suit > 4 || number < 1 || number > 9 ||
        (suit == 4 && number > 7)) {
        fail(fmt::format("{} is not a Tenhou tile code.", code));
    }

    return std::string(1, static_cast<char>('0' + number)) + "mpsz"[suit - 1];
}

std::string to_normal(const std::string &tile)
{
    if (tile.size() == 2 && tile[0] == '0') {
        return std::string("5") + tile[1];
    }
    return tile;
}

// -- log tokens ------------------------------------------------------------

/**
 * @brief One entry of a draw or discard list: either a tile code or a call string.
 */
struct Token
{
    bool is_text = false;
    int number = 0;
    std::string text;
};

const char *meld_type_of(const char letter)
{
    switch (letter) {
    case 'c':
        return "chi";
    case 'p':
        return "pon";
    case 'm':
        return "daiminkan";
    case 'k':
        return "kakan";
    case 'a':
        return "ankan";
    default:
        return nullptr;
    }
}

/**
 * @brief Whether the token is a call rather than a tile or a riichi discard.
 *
 * The letter marks the called tile, so it can sit anywhere in the string: "c333435"
 * but also "4444p44". A riichi discard ("r17") carries a letter that is not one of the
 * call letters.
 */
bool is_call(const Token &token)
{
    if (!token.is_text) {
        return false;
    }
    return std::any_of(token.text.begin(), token.text.end(),
                       [](const char c) { return meld_type_of(c) != nullptr; });
}

struct ParsedMeld
{
    std::string type;
    std::vector<std::string> tiles;
    int called_index = 0;

    /*! Seats forward the called tile came from. 0 for a concealed kan. */
    int from_offset = 0;
};

ParsedMeld parse_meld(const std::string &token)
{
    char letter = '\0';
    for (const char c : token) {
        if (meld_type_of(c) != nullptr) {
            letter = c;
            break;
        }
    }
    if (letter == '\0') {
        fail(fmt::format("\"{}\" is not a call.", token));
    }

    ParsedMeld meld;
    meld.type = meld_type_of(letter);
    meld.called_index = -1;

    for (std::size_t i = 0; i < token.size();) {
        if (token[i] == letter) {
            meld.called_index = static_cast<int>(meld.tiles.size());
            ++i;
            continue;
        }
        if (i + 1 >= token.size()) {
            fail(fmt::format("\"{}\" is malformed.", token));
        }
        meld.tiles.push_back(to_tile(std::stoi(token.substr(i, 2))));
        i += 2;
    }

    if (meld.called_index < 0) {
        fail(fmt::format("\"{}\" has no called tile.", token));
    }

    if (meld.type == "ankan") {
        meld.from_offset = 0;
        return meld;
    }

    // First tile -> from the player to the left, second -> across, third -> to the
    // right. A kan lists four tiles and can carry the mark in the last slot.
    switch (meld.called_index) {
    case 0:
        meld.from_offset = 3;
        break;
    case 1:
        meld.from_offset = 2;
        break;
    default:
        meld.from_offset = 1;
        break;
    }

    return meld;
}

std::vector<Token> to_tokens(const Value &array)
{
    std::vector<Token> tokens;
    if (!array.IsArray()) {
        return tokens;
    }

    tokens.reserve(array.Size());
    for (const auto &element : array.GetArray()) {
        Token token;
        if (element.IsString()) {
            token.is_text = true;
            token.text = element.GetString();
        }
        else if (element.IsNumber()) {
            token.number = element.GetInt();
        }
        else {
            fail("a draw or discard entry is neither a number nor a string.");
        }
        tokens.push_back(std::move(token));
    }

    return tokens;
}

// -- round replay ----------------------------------------------------------

/**
 * @brief Replays one kyoku of a Tenhou log into the normalized event list.
 */
class RoundBuilder
{
  public:
    RoundBuilder(const Value &kyoku, const int num_players)
        : num_players_(num_players)
    {
        const auto needed = static_cast<rapidjson::SizeType>(4 + 3 * num_players);
        if (!kyoku.IsArray() || kyoku.Size() < needed) {
            fail("the kyoku array is too short.");
        }

        const Value &seed = kyoku[0];
        if (!seed.IsArray() || seed.Size() < 3) {
            fail("the kyoku seed is malformed.");
        }
        round_index_ = seed[0].GetInt();
        dealer_ = round_index_ % num_players;
        honba_ = seed[1].GetInt();
        kyotaku_ = seed[2].GetInt();

        for (const auto &score : kyoku[1].GetArray()) {
            scores_.push_back(score.GetInt());
        }

        // Every indicator of the kyoku is listed up front, including the ones a kan
        // reveals later, so they are held back and released one kan at a time.
        for (const auto &indicator : kyoku[2].GetArray()) {
            pending_dora_.push_back(to_tile(indicator.GetInt()));
        }
        if (!pending_dora_.empty()) {
            first_dora_ = pending_dora_.front();
            pending_dora_.erase(pending_dora_.begin());
        }

        for (int seat = 0; seat < num_players; ++seat) {
            std::vector<std::string> hand;
            for (const auto &tile : kyoku[4 + 3 * seat].GetArray()) {
                hand.push_back(to_tile(tile.GetInt()));
            }
            hands_.push_back(hand);
            initial_hands_.push_back(hand);
            draws_.push_back(to_tokens(kyoku[5 + 3 * seat]));
            discards_.push_back(to_tokens(kyoku[6 + 3 * seat]));
        }

        draw_at_.assign(num_players, 0);
        discard_at_.assign(num_players, 0);
        last_drawn_.assign(num_players, std::string());
        melds_.resize(num_players);

        if (kyoku.Size() > needed) {
            result_ = &kyoku[needed];
        }
    }

    template <typename Writer> void write(Writer &writer);

  private:
    struct Meld
    {
        std::string type;
        std::vector<std::string> tiles;
        int from = 0;
    };

    struct Event
    {
        std::string type;
        int actor = 0;
        std::string tile;
        bool tsumogiri = false;

        // Calls.
        std::string meld_type;
        std::vector<std::string> tiles;
        std::string called_tile;
        int from = 0;
        bool has_called_tile = false;

        // Results.
        int winner = 0;
        int loser = 0;
        std::string reason;
    };

    std::string take(const int seat, const std::string &tile)
    {
        auto &hand = hands_[seat];
        const auto it = std::find(hand.begin(), hand.end(), tile);
        if (it == hand.end()) {
            fail(fmt::format("seat {} used {} which is not in hand.", seat, tile));
        }
        hand.erase(it);
        return tile;
    }

    void reveal_kan_dora()
    {
        if (pending_dora_.empty()) {
            return;
        }
        Event event;
        event.type = "dora";
        event.tile = pending_dora_.front();
        pending_dora_.erase(pending_dora_.begin());
        events_.push_back(std::move(event));
    }

    /**
     * @brief Returns the player whose next draw entry is a call taken from @p seat.
     */
    std::optional<int> caller_of(const int seat) const
    {
        for (int other = 0; other < num_players_; ++other) {
            if (other == seat ||
                draw_at_[other] >= static_cast<int>(draws_[other].size())) {
                continue;
            }
            const Token &token = draws_[other][draw_at_[other]];
            if (!is_call(token)) {
                continue;
            }
            const ParsedMeld meld = parse_meld(token.text);
            if ((other + meld.from_offset) % num_players_ == seat) {
                return other;
            }
        }
        return std::nullopt;
    }

    void apply_call(const int seat, const std::string &token)
    {
        const ParsedMeld parsed = parse_meld(token);

        if (parsed.type == "ankan") {
            for (const auto &tile : parsed.tiles) {
                take(seat, tile);
            }
            Event event;
            event.type = "call";
            event.actor = seat;
            event.meld_type = "ankan";
            event.tiles = parsed.tiles;
            event.from = seat;
            events_.push_back(event);
            melds_[seat].push_back({"ankan", parsed.tiles, seat});
            reveal_kan_dora();
            return;
        }

        const std::string called = parsed.tiles[parsed.called_index];
        const int source = (seat + parsed.from_offset) % num_players_;

        if (parsed.type == "kakan") {
            // Only the fourth tile leaves the hand; the pon becomes a kan.
            const std::string added = take(seat, called);
            auto pon = std::find_if(melds_[seat].begin(), melds_[seat].end(),
                                    [&](const Meld &meld) {
                                        return meld.type == "pon" &&
                                               to_normal(meld.tiles.front()) ==
                                                   to_normal(added);
                                    });
            if (pon == melds_[seat].end()) {
                fail(fmt::format("seat {} upgraded {} without a matching pon.", seat,
                                 added));
            }

            Event event;
            event.type = "call";
            event.actor = seat;
            event.meld_type = "kakan";
            event.tiles = parsed.tiles;
            event.called_tile = added;
            event.has_called_tile = true;
            event.from = pon->from;
            events_.push_back(event);

            pon->type = "kakan";
            pon->tiles = parsed.tiles;
            reveal_kan_dora();
            return;
        }

        for (int index = 0; index < static_cast<int>(parsed.tiles.size()); ++index) {
            if (index != parsed.called_index) {
                take(seat, parsed.tiles[index]);
            }
        }

        Event event;
        event.type = "call";
        event.actor = seat;
        event.meld_type = parsed.type;
        event.tiles = parsed.tiles;
        event.called_tile = called;
        event.has_called_tile = true;
        event.from = source;
        events_.push_back(event);
        melds_[seat].push_back({parsed.type, parsed.tiles, source});

        if (parsed.type == "daiminkan") {
            reveal_kan_dora();
        }
    }

    bool apply_draw(const int seat)
    {
        if (draw_at_[seat] >= static_cast<int>(draws_[seat].size())) {
            return false;
        }

        const Token token = draws_[seat][draw_at_[seat]++];
        if (is_call(token)) {
            apply_call(seat, token.text);
            last_drawn_[seat].clear();
            return true;
        }

        const std::string tile = to_tile(token.number);
        hands_[seat].push_back(tile);

        Event event;
        event.type = "draw";
        event.actor = seat;
        event.tile = tile;
        events_.push_back(event);

        last_drawn_[seat] = tile;
        last_draw_ = {seat, tile};
        return true;
    }

    bool apply_discard(const int seat)
    {
        while (discard_at_[seat] < static_cast<int>(discards_[seat].size())) {
            const Token token = discards_[seat][discard_at_[seat]++];

            // A kan declared on one's own turn sits in the discard list, followed by
            // the replacement draw and the real discard.
            if (is_call(token)) {
                apply_call(seat, token.text);
                if (!apply_draw(seat)) {
                    return false;
                }
                continue;
            }

            bool riichi = false;
            int code = token.number;
            if (token.is_text) {
                riichi = token.text[0] == 'r';
                code = std::stoi(token.text.substr(riichi ? 1 : 0));
            }

            const std::string tile = code == 60 ? last_drawn_[seat] : to_tile(code);
            if (tile.empty()) {
                fail(fmt::format("seat {} discarded the drawn tile without drawing.",
                                 seat));
            }

            Event event;
            event.type = "discard";
            event.actor = seat;
            event.tile = tile;
            event.tsumogiri = code == 60 || tile == last_drawn_[seat];
            take(seat, tile);
            events_.push_back(event);

            if (riichi) {
                Event declaration;
                declaration.type = "riichi";
                declaration.actor = seat;
                events_.push_back(declaration);
            }

            last_discard_ = {seat, tile};
            last_drawn_[seat].clear();
            return true;
        }

        return false;
    }

    void append_result()
    {
        if (result_ == nullptr || !result_->IsArray() || result_->Empty() ||
            !(*result_)[0].IsString()) {
            return;
        }

        const std::string kind = (*result_)[0].GetString();
        if (kind != u8"和了") {
            Event event;
            event.type = "ryukyoku";
            event.reason = kind == u8"九種九牌" ? "nine_terminals" : "exhaustive";
            events_.push_back(event);
            return;
        }

        if (result_->Size() < 3 || !(*result_)[2].IsArray() ||
            (*result_)[2].Size() < 2) {
            return;
        }

        const int winner = (*result_)[2][0].GetInt();
        const int from_who = (*result_)[2][1].GetInt();

        Event event;
        event.winner = winner;
        if (winner == from_who) {
            if (last_draw_.second.empty()) {
                return;
            }
            event.type = "tsumo";
            event.tile = last_draw_.second;
        }
        else {
            if (last_discard_.second.empty()) {
                return;
            }
            event.type = "ron";
            event.loser = from_who;
            event.tile = last_discard_.second;
        }
        events_.push_back(event);
    }

    void replay()
    {
        int seat = dealer_;
        while (true) {
            if (!apply_draw(seat)) {
                break;
            }
            if (!apply_discard(seat)) {
                break;
            }

            const auto caller = caller_of(seat);
            seat = caller ? *caller : (seat + 1) % num_players_;
        }

        append_result();
    }

    int num_players_;
    int round_index_ = 0;
    int dealer_ = 0;
    int honba_ = 0;
    int kyotaku_ = 0;
    std::vector<int> scores_;
    std::string first_dora_;
    std::vector<std::string> pending_dora_;
    std::vector<std::vector<std::string>> hands_;
    std::vector<std::vector<std::string>> initial_hands_;
    std::vector<std::vector<Token>> draws_;
    std::vector<std::vector<Token>> discards_;
    std::vector<std::vector<Meld>> melds_;
    std::vector<int> draw_at_;
    std::vector<int> discard_at_;
    std::vector<std::string> last_drawn_;
    std::pair<int, std::string> last_discard_;
    std::pair<int, std::string> last_draw_;
    std::vector<Event> events_;
    const Value *result_ = nullptr;
};

template <typename Writer> void RoundBuilder::write(Writer &writer)
{
    replay();

    writer.StartObject();
    writer.Key("round_wind");
    writer.Int(round_index_ / num_players_);
    writer.Key("round_number");
    writer.Int(dealer_ + 1);
    writer.Key("honba");
    writer.Int(honba_);
    writer.Key("kyotaku");
    writer.Int(kyotaku_);
    writer.Key("dealer");
    writer.Int(dealer_);

    writer.Key("scores");
    writer.StartArray();
    for (const int score : scores_) {
        writer.Int(score);
    }
    writer.EndArray();

    writer.Key("hands");
    writer.StartArray();
    for (const auto &hand : initial_hands_) {
        writer.StartArray();
        for (const auto &tile : hand) {
            writer.String(tile.c_str());
        }
        writer.EndArray();
    }
    writer.EndArray();

    writer.Key("dora_indicators");
    writer.StartArray();
    if (!first_dora_.empty()) {
        writer.String(first_dora_.c_str());
    }
    writer.EndArray();

    writer.Key("events");
    writer.StartArray();
    for (const auto &event : events_) {
        writer.StartObject();
        writer.Key("type");
        writer.String(event.type.c_str());

        if (event.type == "draw") {
            writer.Key("actor");
            writer.Int(event.actor);
            writer.Key("tile");
            writer.String(event.tile.c_str());
        }
        else if (event.type == "discard") {
            writer.Key("actor");
            writer.Int(event.actor);
            writer.Key("tile");
            writer.String(event.tile.c_str());
            writer.Key("tsumogiri");
            writer.Bool(event.tsumogiri);
        }
        else if (event.type == "riichi" || event.type == "nuki") {
            writer.Key("actor");
            writer.Int(event.actor);
        }
        else if (event.type == "call") {
            writer.Key("actor");
            writer.Int(event.actor);
            writer.Key("meld_type");
            writer.String(event.meld_type.c_str());
            writer.Key("tiles");
            writer.StartArray();
            for (const auto &tile : event.tiles) {
                writer.String(tile.c_str());
            }
            writer.EndArray();
            writer.Key("from");
            writer.Int(event.from);
            if (event.has_called_tile) {
                writer.Key("called_tile");
                writer.String(event.called_tile.c_str());
            }
        }
        else if (event.type == "dora") {
            writer.Key("tile");
            writer.String(event.tile.c_str());
        }
        else if (event.type == "tsumo") {
            writer.Key("winner");
            writer.Int(event.winner);
            writer.Key("tile");
            writer.String(event.tile.c_str());
        }
        else if (event.type == "ron") {
            writer.Key("winner");
            writer.Int(event.winner);
            writer.Key("loser");
            writer.Int(event.loser);
            writer.Key("tile");
            writer.String(event.tile.c_str());
        }
        else if (event.type == "ryukyoku") {
            writer.Key("reason");
            writer.String(event.reason.c_str());
        }

        writer.EndObject();
    }
    writer.EndArray();

    writer.EndObject();
}

// -- document level --------------------------------------------------------

std::string member_string(const Value &value, const char *name)
{
    const auto it = value.FindMember(name);
    if (it == value.MemberEnd() || !it->value.IsString()) {
        return "";
    }
    return it->value.GetString();
}

int member_int(const Value &value, const char *name)
{
    const auto it = value.FindMember(name);
    if (it == value.MemberEnd() || !it->value.IsInt()) {
        return 0;
    }
    return it->value.GetInt();
}

bool contains(const std::string &haystack, const char *needle)
{
    return haystack.find(needle) != std::string::npos;
}

struct Rules
{
    bool red_dora = true;
    bool ura_dora = true;
    bool open_tanyao = true;
    std::string game_length = "hanchan";
};

Rules read_rules(const Value &doc)
{
    Rules rules;

    const auto it = doc.FindMember("rule");
    if (it == doc.MemberEnd() || !it->value.IsObject()) {
        return rules;
    }

    const Value &rule = it->value;
    const std::string display = member_string(rule, "disp");

    rules.red_dora = member_int(rule, "aka51") != 0 || member_int(rule, "aka52") != 0 ||
                     member_int(rule, "aka53") != 0 || member_int(rule, "aka") > 0 ||
                     contains(display, u8"赤");
    rules.open_tanyao = display.empty() || contains(display, u8"喰");
    rules.game_length =
        contains(display, u8"東") && !contains(display, u8"南") ? "tonpu" : "hanchan";

    return rules;
}

/**
 * @brief Replaces the HTML entities that survive inside an attribute value.
 */
std::string unescape_html(const std::string &text)
{
    static const std::pair<const char *, const char *> entities[] = {
        {"&quot;", "\""}, {"&#34;", "\""},  {"&apos;", "'"}, {"&#39;", "'"},
        {"&lt;", "<"},    {"&gt;", ">"},    {"&amp;", "&"},
    };

    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] == '&') {
            bool replaced = false;
            for (const auto &[entity, literal] : entities) {
                const std::size_t length = std::strlen(entity);
                if (text.compare(i, length, entity) == 0) {
                    out += literal;
                    i += length;
                    replaced = true;
                    break;
                }
            }
            if (replaced) {
                continue;
            }
        }
        out += text[i++];
    }

    return out;
}

/**
 * @brief Returns the JSON object starting at @p start, honouring strings and escapes.
 */
std::string scan_object(const std::string &text, const std::size_t start)
{
    int depth = 0;
    bool in_string = false;
    bool escaped = false;

    for (std::size_t i = start; i < text.size(); ++i) {
        const char c = text[i];
        if (in_string) {
            if (escaped) {
                escaped = false;
            }
            else if (c == '\\') {
                escaped = true;
            }
            else if (c == '"') {
                in_string = false;
            }
            continue;
        }

        if (c == '"') {
            in_string = true;
        }
        else if (c == '{') {
            ++depth;
        }
        else if (c == '}') {
            if (--depth == 0) {
                return text.substr(start, i - start + 1);
            }
        }
    }

    return "";
}

} // namespace

std::vector<TenhouLog> extract_tenhou_logs(const std::string &text)
{
    std::vector<TenhouLog> logs;

    for (std::size_t pos = text.find("tenhou.net/"); pos != std::string::npos;
         pos = text.find("tenhou.net/", pos + 1)) {
        // The viewer link looks like ".../5/?tw=3#json={...}".
        const std::size_t json_at = text.find("#json=", pos);
        if (json_at == std::string::npos || json_at > pos + 200) {
            continue;
        }

        const std::size_t brace = text.find('{', json_at);
        if (brace == std::string::npos) {
            continue;
        }

        const std::string object = scan_object(text, brace);
        if (object.empty()) {
            continue;
        }

        TenhouLog log;
        if (const std::size_t tw = text.find("tw=", pos);
            tw != std::string::npos && tw < json_at) {
            log.viewed_seat = std::atoi(text.c_str() + tw + 3);
        }
        log.json = unescape_html(object);
        logs.push_back(std::move(log));

        pos = brace + object.size();
    }

    return logs;
}

std::string convert_saved_log(const std::string &text, const int seat_override,
                              const std::string &url)
{
    // Editors on Windows often prepend a UTF-8 BOM.
    std::string body = text;
    if (body.rfind("\xEF\xBB\xBF", 0) == 0) {
        body.erase(0, 3);
    }

    const std::size_t first = body.find_first_not_of(" \t\r\n");
    const bool looks_like_json =
        first != std::string::npos && (body[first] == '{' || body[first] == '[');

    std::vector<std::string> documents;
    int seat = seat_override;

    if (looks_like_json) {
        rapidjson::Document doc;
        if (doc.Parse(body.c_str()).HasParseError()) {
            fail("the file is not valid JSON.");
        }
        // An already converted replay is passed through unchanged.
        if (doc.IsObject() && member_string(doc, "schema") == ReplaySchema) {
            return body;
        }
        documents.push_back(body);
        if (seat == PlayerIndex::Null) {
            seat = 0;
        }
    }
    else {
        const auto logs = extract_tenhou_logs(body);
        if (logs.empty()) {
            fail("no Tenhou log was found in the file. Save the report from "
                 "mjai-reviewer, or pass a Tenhou JSON log.");
        }
        for (const auto &log : logs) {
            documents.push_back(log.json);
        }
        if (seat == PlayerIndex::Null) {
            seat = logs.front().viewed_seat;
        }
    }

    rapidjson::Document first_doc;
    if (first_doc.Parse(documents.front().c_str()).HasParseError()) {
        fail("the Tenhou log is not valid JSON.");
    }

    const auto names = first_doc.FindMember("name");
    const auto ranks = first_doc.FindMember("dan");
    const int num_players =
        names != first_doc.MemberEnd() && names->value.IsArray() &&
                names->value.Size() == 3
            ? 3
            : 4;

    if (seat < 0 || seat >= num_players) {
        fail(fmt::format("seat {} is out of range [0, {}).", seat, num_players));
    }

    const Rules rules = read_rules(first_doc);

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);

    writer.StartObject();
    writer.Key("schema");
    writer.String(ReplaySchema);
    writer.Key("uuid");
    writer.String("");
    writer.Key("url");
    writer.String(url.c_str());
    writer.Key("game_mode");
    writer.String(num_players == 3 ? "sanma" : "yonma");
    writer.Key("game_length");
    writer.String(rules.game_length.c_str());

    writer.Key("rules");
    writer.StartObject();
    writer.Key("red_dora");
    writer.Bool(rules.red_dora);
    writer.Key("ura_dora");
    writer.Bool(rules.ura_dora);
    writer.Key("open_tanyao");
    writer.Bool(rules.open_tanyao);
    writer.EndObject();

    writer.Key("target_seat");
    writer.Int(seat);

    writer.Key("players");
    writer.StartArray();
    for (int i = 0; i < num_players; ++i) {
        writer.StartObject();
        writer.Key("seat");
        writer.Int(i);
        writer.Key("name");
        if (names != first_doc.MemberEnd() && names->value.IsArray() &&
            static_cast<rapidjson::SizeType>(i) < names->value.Size() &&
            names->value[i].IsString()) {
            writer.String(names->value[i].GetString());
        }
        else {
            writer.String(fmt::format("Player {}", i).c_str());
        }
        writer.Key("level");
        if (ranks != first_doc.MemberEnd() && ranks->value.IsArray() &&
            static_cast<rapidjson::SizeType>(i) < ranks->value.Size() &&
            ranks->value[i].IsString()) {
            writer.String(ranks->value[i].GetString());
        }
        else {
            writer.String("");
        }
        writer.EndObject();
    }
    writer.EndArray();

    writer.Key("rounds");
    writer.StartArray();

    int num_rounds = 0;
    for (const auto &text_doc : documents) {
        rapidjson::Document doc;
        if (doc.Parse(text_doc.c_str()).HasParseError()) {
            fail("a Tenhou log is not valid JSON.");
        }

        const auto log = doc.FindMember("log");
        if (log == doc.MemberEnd() || !log->value.IsArray()) {
            continue;
        }

        for (const auto &kyoku : log->value.GetArray()) {
            RoundBuilder(kyoku, num_players).write(writer);
            ++num_rounds;
        }
    }

    writer.EndArray();
    writer.EndObject();

    if (num_rounds == 0) {
        fail("the log contains no rounds.");
    }

    return buffer.GetString();
}

std::string convert_saved_log_file(const std::filesystem::path &path,
                                   const int seat_override, const std::string &url)
{
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        fail(fmt::format("Failed to open log file: {}", path.string()));
    }

    std::ostringstream buffer;
    buffer << ifs.rdbuf();

    return convert_saved_log(buffer.str(), seat_override, url);
}

} // namespace mahjong::tools::majsoul
