#ifndef MAHJONG_CPP_TOOLS_MAJSOUL_TILE_GLYPH
#define MAHJONG_CPP_TOOLS_MAJSOUL_TILE_GLYPH

#include <string>
#include <vector>

#include "mahjong/types/types.hpp"

namespace mahjong::tools::majsoul
{

/**
 * @brief How tiles are written in the report.
 */
namespace TileStyle
{

/*! Unicode Mahjong Tiles (U+1F000-U+1F021), e.g. "🀉🀊🀋". */
inline constexpr int Unicode = 0;

/*! mpsz notation, e.g. "345m". */
inline constexpr int Mpsz = 1;

} // namespace TileStyle

/**
 * @brief Rendering options for tiles.
 */
struct GlyphOptions
{
    int style = TileStyle::Unicode;

    /*!
     * Emit HTML rather than plain text. Red fives become a <span class="aka">, which
     * is the only way to tell them apart: Unicode has no red five codepoint.
     */
    bool html = false;
};

/**
 * @brief Renders a single tile.
 *
 * In Unicode style a red five is the glyph of the normal five followed by "赤", or a
 * red-coloured span when @p options selects HTML.
 */
std::string to_glyph(int tile, const GlyphOptions &options);

/**
 * @brief Renders a hand in tile order: manzu, pinzu, souzu, then honors.
 */
std::string to_glyphs(const Hand &hand, const GlyphOptions &options);

/**
 * @brief Renders a list of tiles in the given order.
 */
std::string to_glyphs(const std::vector<int> &tiles, const GlyphOptions &options);

/**
 * @brief Renders a meld as its tiles followed by the meld type.
 */
std::string to_glyphs(const Meld &meld, const GlyphOptions &options);

/**
 * @brief Parses a --tile-style value. Returns -1 if the name is unknown.
 */
int parse_tile_style(const std::string &name);

/**
 * @brief CSS font stack listing the fonts known to carry the Mahjong Tiles block.
 */
const char *tile_font_stack();

} // namespace mahjong::tools::majsoul

#endif // MAHJONG_CPP_TOOLS_MAJSOUL_TILE_GLYPH
