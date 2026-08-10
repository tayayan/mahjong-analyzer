#include "tile_glyph.hpp"

#include "mahjong/core/string.hpp"

namespace mahjong::tools::majsoul
{

namespace
{

// Unicode Mahjong Tiles. The block orders the suits manzu, souzu, pinzu, which is not
// the order tile ids use, so each suit is mapped from its own base.
constexpr char32_t CharactersBase = 0x1F007; // 一萬
constexpr char32_t BamboosBase = 0x1F010;    // 一索
constexpr char32_t CirclesBase = 0x1F019;    // 一筒
constexpr char32_t EastWind = 0x1F000;       // 東
constexpr char32_t RedDragon = 0x1F004;      // 中
constexpr char32_t GreenDragon = 0x1F005;    // 發
constexpr char32_t WhiteDragon = 0x1F006;    // 白

// U+1F004 is the only tile with emoji presentation by default, so it renders as a wide
// colour glyph and breaks the alignment of a row of tiles. VS15 asks for text style.
constexpr char32_t VariationSelectorText = 0xFE0E;

void append_utf8(std::string &out, const char32_t code)
{
    if (code < 0x80) {
        out += static_cast<char>(code);
    }
    else if (code < 0x800) {
        out += static_cast<char>(0xC0 | (code >> 6));
        out += static_cast<char>(0x80 | (code & 0x3F));
    }
    else if (code < 0x10000) {
        out += static_cast<char>(0xE0 | (code >> 12));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    }
    else {
        out += static_cast<char>(0xF0 | (code >> 18));
        out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    }
}

/**
 * @brief Returns the Unicode code point of a tile, ignoring whether it is a red five.
 */
char32_t code_point(const int tile)
{
    const int normal = Tile::to_normal(tile);

    if (normal <= Tile::Manzu9) {
        return CharactersBase + (normal - Tile::Manzu1);
    }
    if (normal <= Tile::Pinzu9) {
        return CirclesBase + (normal - Tile::Pinzu1);
    }
    if (normal <= Tile::Souzu9) {
        return BamboosBase + (normal - Tile::Souzu1);
    }
    if (normal <= Tile::North) {
        return EastWind + (normal - Tile::East);
    }

    switch (normal) {
    case Tile::WhiteDragon:
        return WhiteDragon;
    case Tile::GreenDragon:
        return GreenDragon;
    default:
        return RedDragon;
    }
}

/**
 * @brief Returns the red five belonging to a tile, or Tile::Null if it has none.
 */
int red_five_of(const int tile)
{
    switch (tile) {
    case Tile::Manzu5:
        return Tile::RedManzu5;
    case Tile::Pinzu5:
        return Tile::RedPinzu5;
    case Tile::Souzu5:
        return Tile::RedSouzu5;
    default:
        return Tile::Null;
    }
}

std::string glyph_only(const int tile)
{
    const char32_t code = code_point(tile);

    std::string out;
    append_utf8(out, code);
    if (code == RedDragon) {
        append_utf8(out, VariationSelectorText);
    }
    return out;
}

void append_tile(std::string &out, const int tile, const GlyphOptions &options)
{
    if (options.style == TileStyle::Mpsz) {
        out += to_mpsz(std::vector<int>{tile});
        return;
    }

    if (!Tile::is_red(tile)) {
        out += glyph_only(tile);
        return;
    }

    if (options.html) {
        out += "<span class=\"aka\" title=\"";
        out += to_mpsz(std::vector<int>{tile});
        out += "\">";
        out += glyph_only(tile);
        out += "</span>";
    }
    else {
        out += glyph_only(tile);
        out += u8"赤";
    }
}

} // namespace

std::string to_glyph(const int tile, const GlyphOptions &options)
{
    std::string out;
    append_tile(out, tile, options);
    return out;
}

std::string to_glyphs(const Hand &hand, const GlyphOptions &options)
{
    if (options.style == TileStyle::Mpsz) {
        return to_mpsz(hand);
    }

    std::string out;
    for (int tile = 0; tile < 34; ++tile) {
        int count = hand[tile];
        if (count <= 0) {
            continue;
        }

        // A red five occupies both its own slot and the slot of the normal tile, so
        // the reds are drawn first and taken out of the normal count.
        const int red_tile = red_five_of(tile);
        int red_count = red_tile == Tile::Null ? 0 : hand[red_tile];

        for (; red_count > 0 && count > 0; --red_count, --count) {
            append_tile(out, red_tile, options);
        }
        for (int i = 0; i < count; ++i) {
            append_tile(out, tile, options);
        }
    }

    return out;
}

std::string to_glyphs(const std::vector<int> &tiles, const GlyphOptions &options)
{
    if (options.style == TileStyle::Mpsz) {
        return to_mpsz(tiles);
    }

    std::string out;
    for (const int tile : tiles) {
        append_tile(out, tile, options);
    }
    return out;
}

std::string to_glyphs(const Meld &meld, const GlyphOptions &options)
{
    if (options.style == TileStyle::Mpsz) {
        return to_string(meld);
    }

    // The meld type is ordinary text, so in HTML it stays outside the tile span and
    // keeps the body font.
    std::string out;
    if (options.html) {
        out += "<span class=\"tile\">";
        out += to_glyphs(meld.tiles, options);
        out += "</span>";
    }
    else {
        out += to_glyphs(meld.tiles, options);
    }

    out += " ";
    out += MeldType::name(meld.type);
    return out;
}

int parse_tile_style(const std::string &name)
{
    if (name == "unicode" || name == "tile" || name == "tiles") {
        return TileStyle::Unicode;
    }
    if (name == "mpsz" || name == "text") {
        return TileStyle::Mpsz;
    }
    return -1;
}

const char *tile_font_stack()
{
    return "\"Segoe UI Symbol\", \"Apple Symbols\", \"Noto Sans Symbols 2\", "
           "\"Noto Sans Symbols2\", Symbola, \"DejaVu Sans\", sans-serif";
}

} // namespace mahjong::tools::majsoul
