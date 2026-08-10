#include "replay_json.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>

#include <rapidjson/document.h>
#include <spdlog/spdlog.h>

namespace mahjong::tools::majsoul
{

namespace
{

using rapidjson::Value;

[[noreturn]] void fail(const std::string &message)
{
    throw std::runtime_error(message);
}

const Value &member(const Value &value, const char *name, const std::string &context)
{
    const auto it = value.FindMember(name);
    if (it == value.MemberEnd()) {
        fail(fmt::format("{}: required field \"{}\" is missing.", context, name));
    }
    return it->value;
}

int get_int(const Value &value, const char *name, const std::string &context)
{
    const Value &field = member(value, name, context);
    if (!field.IsInt()) {
        fail(fmt::format("{}: field \"{}\" must be an integer.", context, name));
    }
    return field.GetInt();
}

std::string get_string(const Value &value, const char *name, const std::string &context)
{
    const Value &field = member(value, name, context);
    if (!field.IsString()) {
        fail(fmt::format("{}: field \"{}\" must be a string.", context, name));
    }
    return field.GetString();
}

std::string get_string_or(const Value &value, const char *name,
                          const std::string &fallback)
{
    const auto it = value.FindMember(name);
    if (it == value.MemberEnd() || !it->value.IsString()) {
        return fallback;
    }
    return it->value.GetString();
}

bool get_bool_or(const Value &value, const char *name, const bool fallback)
{
    const auto it = value.FindMember(name);
    if (it == value.MemberEnd() || !it->value.IsBool()) {
        return fallback;
    }
    return it->value.GetBool();
}

const Value &get_array(const Value &value, const char *name,
                       const std::string &context)
{
    const Value &field = member(value, name, context);
    if (!field.IsArray()) {
        fail(fmt::format("{}: field \"{}\" must be an array.", context, name));
    }
    return field;
}

/**
 * @brief Converts an mpsz tile string such as "3p", "1z" or "0m" (red five) to a tile id.
 */
int parse_tile(const std::string &text, const std::string &context)
{
    if (text.size() != 2) {
        fail(fmt::format("{}: \"{}\" is not a tile.", context, text));
    }

    const int number = text[0] - '0';
    const char suit = text[1];

    if (suit == 'z') {
        if (number < 1 || number > 7) {
            fail(fmt::format("{}: \"{}\" is not a tile.", context, text));
        }
        return Tile::East + number - 1;
    }

    if (number < 0 || number > 9) {
        fail(fmt::format("{}: \"{}\" is not a tile.", context, text));
    }

    switch (suit) {
    case 'm':
        return number == 0 ? Tile::RedManzu5 : Tile::Manzu1 + number - 1;
    case 'p':
        return number == 0 ? Tile::RedPinzu5 : Tile::Pinzu1 + number - 1;
    case 's':
        return number == 0 ? Tile::RedSouzu5 : Tile::Souzu1 + number - 1;
    default:
        fail(fmt::format("{}: \"{}\" is not a tile.", context, text));
    }
}

int parse_tile_field(const Value &value, const char *name, const std::string &context)
{
    return parse_tile(get_string(value, name, context), context);
}

std::vector<int> parse_tile_array(const Value &array, const std::string &context)
{
    std::vector<int> tiles;
    tiles.reserve(array.Size());
    for (const auto &element : array.GetArray()) {
        if (!element.IsString()) {
            fail(fmt::format("{}: tile entries must be strings.", context));
        }
        tiles.push_back(parse_tile(element.GetString(), context));
    }
    return tiles;
}

int parse_game_mode(const std::string &text, const std::string &context)
{
    if (text == "yonma") {
        return GameMode::Yonma;
    }
    if (text == "sanma") {
        return GameMode::Sanma;
    }
    fail(fmt::format("{}: unknown game_mode \"{}\".", context, text));
}

int parse_game_length(const std::string &text, const std::string &context)
{
    if (text == "tonpu") {
        return GameLength::Tonpu;
    }
    if (text == "hanchan") {
        return GameLength::Hanchan;
    }
    fail(fmt::format("{}: unknown game_length \"{}\".", context, text));
}

int parse_round_wind(const Value &value, const std::string &context)
{
    const Value &field = member(value, "round_wind", context);
    if (field.IsInt()) {
        const int index = field.GetInt();
        if (index < 0 || index > 3) {
            fail(fmt::format("{}: round_wind must be in [0, 3].", context));
        }
        return Tile::East + index;
    }

    if (field.IsString()) {
        const std::string text = field.GetString();
        if (text == "east") {
            return Tile::East;
        }
        if (text == "south") {
            return Tile::South;
        }
        if (text == "west") {
            return Tile::West;
        }
        if (text == "north") {
            return Tile::North;
        }
    }

    fail(fmt::format("{}: unknown round_wind.", context));
}

int parse_meld_type(const std::string &text, const std::string &context)
{
    if (text == "chi") {
        return MeldType::Chi;
    }
    if (text == "pon") {
        return MeldType::Pon;
    }
    if (text == "ankan") {
        return MeldType::Ankan;
    }
    if (text == "daiminkan") {
        return MeldType::Daiminkan;
    }
    if (text == "kakan") {
        return MeldType::Kakan;
    }
    fail(fmt::format("{}: unknown meld_type \"{}\".", context, text));
}

int parse_ryukyoku_type(const std::string &text)
{
    if (text == "nine_terminals") {
        return RyukyokuType::NineTerminals;
    }
    if (text == "four_winds") {
        return RyukyokuType::FourWinds;
    }
    if (text == "four_riichi") {
        return RyukyokuType::FourRiichi;
    }
    if (text == "three_ron") {
        return RyukyokuType::ThreeRon;
    }
    return RyukyokuType::Exhaustive;
}

RuleFlags parse_rule_flags(const Value &root)
{
    RuleFlags flags = RuleFlag::None;

    const auto it = root.FindMember("rules");
    const bool red_dora = it != root.MemberEnd() && it->value.IsObject()
                              ? get_bool_or(it->value, "red_dora", true)
                              : true;
    const bool ura_dora = it != root.MemberEnd() && it->value.IsObject()
                              ? get_bool_or(it->value, "ura_dora", true)
                              : true;
    const bool open_tanyao = it != root.MemberEnd() && it->value.IsObject()
                                 ? get_bool_or(it->value, "open_tanyao", true)
                                 : true;

    if (red_dora) {
        flags |= RuleFlag::RedDora;
    }
    if (ura_dora) {
        flags |= RuleFlag::UraDora;
    }
    if (open_tanyao) {
        flags |= RuleFlag::OpenTanyao;
    }

    return flags;
}

/**
 * @brief Converts an absolute seat to a seat relative to the acting player.
 */
int to_relative_seat(const int actor, const int from, const int num_players)
{
    const int offset = ((from - actor) % num_players + num_players) % num_players;
    switch (offset) {
    case 0:
        return SeatType::Self;
    case 1:
        return SeatType::Shimocha;
    case 2:
        return SeatType::Toimen;
    case 3:
        return SeatType::Kamicha;
    default:
        return SeatType::Null;
    }
}

int to_seat_wind(const int seat, const int dealer, const int num_players)
{
    return Tile::East + ((seat - dealer) % num_players + num_players) % num_players;
}

void check_seat(const int seat, const int num_players, const std::string &context,
                const char *name)
{
    if (seat < 0 || seat >= num_players) {
        fail(fmt::format("{}: {} {} is out of range [0, {}).", context, name, seat,
                         num_players));
    }
}

// A red five occupies both its own slot and the slot of the corresponding normal tile.
void add_tile(Hand &hand, const int tile)
{
    ++hand[tile];
    if (Tile::is_red(tile)) {
        ++hand[Tile::to_normal(tile)];
    }
}

void remove_tile(PlayerState &player, const int tile, const std::string &context)
{
    const int normal = Tile::to_normal(tile);
    if (player.hand[tile] <= 0 || player.hand[normal] <= 0) {
        fail(fmt::format("{}: tried to remove {} which is not in hand.", context,
                         Tile::name(tile)));
    }

    --player.hand[tile];
    if (Tile::is_red(tile)) {
        --player.hand[normal];
    }
}

/**
 * @brief Removes the concealed tiles consumed by a call from the actor's hand.
 */
void remove_meld_tiles(PlayerState &player, const Meld &meld,
                       const std::string &context)
{
    if (meld.type == MeldType::Ankan) {
        for (const int tile : meld.tiles) {
            remove_tile(player, tile, context);
        }
        return;
    }

    // A kakan only adds the fourth tile to an existing pon.
    if (meld.type == MeldType::Kakan) {
        remove_tile(player, meld.discarded_tile, context);
        return;
    }

    bool skipped_called_tile = false;
    for (const int tile : meld.tiles) {
        if (!skipped_called_tile && tile == meld.discarded_tile) {
            skipped_called_tile = true;
            continue;
        }
        remove_tile(player, tile, context);
    }
}

/**
 * @brief Replaces the upgraded pon with the kakan meld instead of appending it.
 */
void apply_kakan(PlayerState &player, const Meld &meld, const std::string &context)
{
    const int base = Tile::to_normal(meld.discarded_tile);
    for (auto &existing : player.melds) {
        if (existing.type == MeldType::Pon &&
            Tile::to_normal(existing.tiles.front()) == base) {
            existing = meld;
            return;
        }
    }

    fail(fmt::format("{}: kakan of {} has no matching pon.", context,
                     Tile::name(meld.discarded_tile)));
}

Meld parse_meld(const Value &event, const int actor, const int num_players,
                const std::string &context)
{
    Meld meld;
    meld.type = parse_meld_type(get_string(event, "meld_type", context), context);
    meld.tiles = parse_tile_array(get_array(event, "tiles", context), context);

    const std::size_t expected_size =
        meld.type == MeldType::Chi || meld.type == MeldType::Pon ? 3 : 4;
    if (meld.tiles.size() != expected_size) {
        fail(fmt::format("{}: {} must have {} tiles but has {}.", context,
                         MeldType::name(meld.type), expected_size, meld.tiles.size()));
    }

    if (meld.type == MeldType::Ankan) {
        meld.discarded_tile = Tile::Null;
        meld.from = SeatType::Self;
        return meld;
    }

    meld.discarded_tile = parse_tile_field(event, "called_tile", context);

    const int from = get_int(event, "from", context);
    check_seat(from, num_players, context, "from");
    meld.from = to_relative_seat(actor, from, num_players);

    return meld;
}

std::vector<PlayerState> parse_initial_players(const Value &round, const int dealer,
                                               const int num_players,
                                               const std::string &context)
{
    const Value &hands = get_array(round, "hands", context);
    if (static_cast<int>(hands.Size()) != num_players) {
        fail(fmt::format("{}: hands must have {} entries but has {}.", context,
                         num_players, hands.Size()));
    }

    const Value &scores = get_array(round, "scores", context);
    if (static_cast<int>(scores.Size()) != num_players) {
        fail(fmt::format("{}: scores must have {} entries but has {}.", context,
                         num_players, scores.Size()));
    }

    std::vector<PlayerState> players;
    players.reserve(num_players);

    for (int seat = 0; seat < num_players; ++seat) {
        if (!hands[seat].IsArray()) {
            fail(fmt::format("{}: hands[{}] must be an array.", context, seat));
        }

        const auto tiles = parse_tile_array(hands[seat], context);
        if (tiles.size() != 13) {
            fail(fmt::format("{}: hands[{}] must have 13 tiles but has {}. The drawn "
                             "14th tile of the dealer must be a draw event.",
                             context, seat, tiles.size()));
        }

        PlayerState player;
        for (const int tile : tiles) {
            add_tile(player.hand, tile);
        }
        player.seat_wind = to_seat_wind(seat, dealer, num_players);
        player.score = scores[seat].IsInt() ? scores[seat].GetInt() : 0;
        players.push_back(player);
    }

    return players;
}

/**
 * @brief Appends one event to the record and advances the running player states.
 */
void apply_event(RoundRecord &record, const Value &event, const int num_players,
                 const std::string &context)
{
    const std::string type = get_string(event, "type", context);

    if (type == "draw") {
        const int actor = get_int(event, "actor", context);
        check_seat(actor, num_players, context, "actor");
        const int tile = parse_tile_field(event, "tile", context);
        add_tile(record.last.players[actor].hand, tile);
        record.events.push_back(DrawEvent{actor, tile});
        return;
    }

    if (type == "discard") {
        const int actor = get_int(event, "actor", context);
        check_seat(actor, num_players, context, "actor");
        const int tile = parse_tile_field(event, "tile", context);
        remove_tile(record.last.players[actor], tile, context);
        record.events.push_back(
            DiscardEvent{actor, tile, get_bool_or(event, "tsumogiri", false)});
        return;
    }

    if (type == "call") {
        const int actor = get_int(event, "actor", context);
        check_seat(actor, num_players, context, "actor");
        const Meld meld = parse_meld(event, actor, num_players, context);
        PlayerState &player = record.last.players[actor];
        remove_meld_tiles(player, meld, context);
        if (meld.type == MeldType::Kakan) {
            apply_kakan(player, meld, context);
        }
        else {
            player.melds.push_back(meld);
        }
        record.events.push_back(CallEvent{actor, meld});
        return;
    }

    if (type == "nuki") {
        const int actor = get_int(event, "actor", context);
        check_seat(actor, num_players, context, "actor");
        remove_tile(record.last.players[actor], Tile::North, context);
        ++record.last.players[actor].nuki_count;
        record.events.push_back(NukiEvent{actor});
        return;
    }

    if (type == "riichi") {
        const int actor = get_int(event, "actor", context);
        check_seat(actor, num_players, context, "actor");
        ++record.last.table.kyotaku;
        record.events.push_back(RiichiEvent{actor});
        return;
    }

    if (type == "dora") {
        const int tile = parse_tile_field(event, "tile", context);
        record.last.table.dora_indicators.push_back(tile);
        record.events.push_back(DoraOpenEvent{tile});
        return;
    }

    if (type == "tsumo") {
        const int winner = get_int(event, "winner", context);
        check_seat(winner, num_players, context, "winner");
        record.events.push_back(
            TsumoEvent{winner, parse_tile_field(event, "tile", context)});
        return;
    }

    if (type == "ron") {
        const int winner = get_int(event, "winner", context);
        const int loser = get_int(event, "loser", context);
        check_seat(winner, num_players, context, "winner");
        check_seat(loser, num_players, context, "loser");
        record.events.push_back(
            RonEvent{winner, loser, parse_tile_field(event, "tile", context)});
        return;
    }

    if (type == "ryukyoku") {
        record.events.push_back(
            RyukyokuEvent{parse_ryukyoku_type(get_string_or(event, "reason", ""))});
        return;
    }

    fail(fmt::format("{}: unknown event type \"{}\".", context, type));
}

RoundRecord parse_round(const Value &round, const int num_players,
                        const std::string &context)
{
    const int dealer = get_int(round, "dealer", context);
    check_seat(dealer, num_players, context, "dealer");

    RoundRecord record;
    record.initial.round.round_wind = parse_round_wind(round, context);
    record.initial.round.round_number = get_int(round, "round_number", context);
    record.initial.round.honba = get_int(round, "honba", context);
    record.initial.round.dealer = dealer;
    record.initial.table.kyotaku = get_int(round, "kyotaku", context);
    record.initial.table.dora_indicators =
        parse_tile_array(get_array(round, "dora_indicators", context), context);
    record.initial.players = parse_initial_players(round, dealer, num_players, context);
    record.last = record.initial;

    const Value &events = get_array(round, "events", context);
    for (rapidjson::SizeType i = 0; i < events.Size(); ++i) {
        if (!events[i].IsObject()) {
            fail(fmt::format("{}: events[{}] must be an object.", context, i));
        }
        apply_event(record, events[i], num_players,
                    fmt::format("{} events[{}]", context, i));
    }

    return record;
}

std::vector<PlayerProfile> parse_players(const Value &root, const std::string &context)
{
    const Value &players = get_array(root, "players", context);
    if (players.Size() != 3 && players.Size() != 4) {
        fail(fmt::format("{}: players must have 3 or 4 entries but has {}.", context,
                         players.Size()));
    }

    std::vector<PlayerProfile> ret;
    ret.reserve(players.Size());
    for (rapidjson::SizeType i = 0; i < players.Size(); ++i) {
        const Value &entry = players[i];
        if (!entry.IsObject()) {
            fail(fmt::format("{}: players[{}] must be an object.", context, i));
        }

        PlayerProfile profile;
        profile.seat = static_cast<int>(i);
        profile.name = get_string_or(entry, "name", "");
        profile.level = get_string_or(entry, "level", "");
        ret.push_back(profile);
    }

    return ret;
}

} // namespace

GameRecord parse_replay_json(const std::string &json, const std::string &source)
{
    // Editors on Windows often prepend a UTF-8 BOM, which RapidJSON rejects.
    const std::string_view body =
        json.rfind("\xEF\xBB\xBF", 0) == 0 ? std::string_view(json).substr(3)
                                           : std::string_view(json);

    rapidjson::Document doc;
    if (doc.Parse(body.data(), body.size()).HasParseError()) {
        fail(fmt::format("{}: failed to parse JSON (offset {}).", source,
                         doc.GetErrorOffset()));
    }
    if (!doc.IsObject()) {
        fail(fmt::format("{}: the document root must be an object.", source));
    }

    const std::string schema = get_string_or(doc, "schema", "");
    if (schema != ReplaySchema) {
        fail(fmt::format("{}: unsupported schema \"{}\", expected \"{}\".", source,
                         schema, ReplaySchema));
    }

    GameRecord game;
    game.meta.schema = schema;
    game.meta.uuid = get_string_or(doc, "uuid", "");
    game.meta.url = get_string_or(doc, "url", "");
    game.table_config.game_mode =
        parse_game_mode(get_string(doc, "game_mode", source), source);
    game.table_config.rule_flags = parse_rule_flags(doc);
    game.game_length = parse_game_length(get_string(doc, "game_length", source), source);
    game.players = parse_players(doc, source);

    const int num_players = game.num_players();
    const int expected_players =
        game.table_config.game_mode == GameMode::Sanma ? 3 : 4;
    if (num_players != expected_players) {
        fail(fmt::format("{}: {} requires {} players but {} were given.", source,
                         GameMode::name(game.table_config.game_mode), expected_players,
                         num_players));
    }

    game.target_seat = get_int(doc, "target_seat", source);
    check_seat(game.target_seat, num_players, source, "target_seat");

    const Value &rounds = get_array(doc, "rounds", source);
    game.rounds.reserve(rounds.Size());
    for (rapidjson::SizeType i = 0; i < rounds.Size(); ++i) {
        if (!rounds[i].IsObject()) {
            fail(fmt::format("{}: rounds[{}] must be an object.", source, i));
        }
        game.rounds.push_back(
            parse_round(rounds[i], num_players, fmt::format("{} rounds[{}]", source, i)));
    }

    return game;
}

GameRecord read_replay_json(const std::filesystem::path &path)
{
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        fail(fmt::format("Failed to open replay JSON: {}", path.string()));
    }

    std::ostringstream buffer;
    buffer << ifs.rdbuf();

    return parse_replay_json(buffer.str(), path.filename().string());
}

} // namespace mahjong::tools::majsoul
