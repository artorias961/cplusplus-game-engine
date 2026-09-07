#pragma once
// ---------------------------------------------------------------------------
// Font.h — a 5x7 bitmap font, built into the binary.
//
// Drawing text is the one thing every game needs and no graphics API gives
// you for free. The usual answer is a font library (SDL2_ttf, FreeType) plus
// a .ttf file shipped alongside the executable — real glyph shapes, hinting,
// kerning, any size you like.
//
// This engine takes the older, smaller road: every character is a tiny grid
// of on/off pixels, stored right here in the source, and drawn with the same
// filled rectangles the renderer already uses for sprites. That means no new
// dependency, no font file to lose, and nothing to load at startup — the same
// reason the Sprite component draws colored boxes instead of textures.
//
// The cost is real and worth knowing: one size (scaled up by whole numbers
// only, so 2x and 3x are crisp but 1.5x is impossible), uppercase only, and
// no kerning. Swap in SDL2_ttf the day you want a real typeface.
//
// The glyphs below are written as art rather than hex so you can read them:
// each is seven rows of five characters, '#' for an on pixel and '.' for off.
// Adjacent string literals concatenate in C++, so each row stays visible on
// its own while the whole glyph is one 35-character string.
// ---------------------------------------------------------------------------

#include <string>

namespace engine {

constexpr int kGlyphWidth = 5;
constexpr int kGlyphHeight = 7;
constexpr int kGlyphSpacing = 1;  // blank columns between characters

struct Glyph {
    char character;
    const char* pixels;  // kGlyphWidth * kGlyphHeight, row by row
};

inline constexpr Glyph kGlyphs[] = {
    {' ', "....." "....." "....." "....." "....." "....." "....."},
    {'0', ".###." "#...#" "#..##" "#.#.#" "##..#" "#...#" ".###."},
    {'1', "..#.." ".##.." "..#.." "..#.." "..#.." "..#.." ".###."},
    {'2', ".###." "#...#" "....#" "...#." "..#.." ".#..." "#####"},
    {'3', "#####" "...#." "..#.." "...#." "....#" "#...#" ".###."},
    {'4', "...#." "..##." ".#.#." "#..#." "#####" "...#." "...#."},
    {'5', "#####" "#...." "####." "....#" "....#" "#...#" ".###."},
    {'6', "..##." ".#..." "#...." "####." "#...#" "#...#" ".###."},
    {'7', "#####" "....#" "...#." "..#.." ".#..." ".#..." ".#..."},
    {'8', ".###." "#...#" "#...#" ".###." "#...#" "#...#" ".###."},
    {'9', ".###." "#...#" "#...#" ".####" "....#" "...#." ".##.."},
    {'A', ".###." "#...#" "#...#" "#####" "#...#" "#...#" "#...#"},
    {'B', "####." "#...#" "#...#" "####." "#...#" "#...#" "####."},
    {'C', ".###." "#...#" "#...." "#...." "#...." "#...#" ".###."},
    {'D', "###.." "#..#." "#...#" "#...#" "#...#" "#..#." "###.."},
    {'E', "#####" "#...." "#...." "####." "#...." "#...." "#####"},
    {'F', "#####" "#...." "#...." "####." "#...." "#...." "#...."},
    {'G', ".###." "#...#" "#...." "#.###" "#...#" "#...#" ".###."},
    {'H', "#...#" "#...#" "#...#" "#####" "#...#" "#...#" "#...#"},
    {'I', ".###." "..#.." "..#.." "..#.." "..#.." "..#.." ".###."},
    {'J', "..###" "...#." "...#." "...#." "...#." "#..#." ".##.."},
    {'K', "#...#" "#..#." "#.#.." "##..." "#.#.." "#..#." "#...#"},
    {'L', "#...." "#...." "#...." "#...." "#...." "#...." "#####"},
    {'M', "#...#" "##.##" "#.#.#" "#.#.#" "#...#" "#...#" "#...#"},
    {'N', "#...#" "#...#" "##..#" "#.#.#" "#..##" "#...#" "#...#"},
    {'O', ".###." "#...#" "#...#" "#...#" "#...#" "#...#" ".###."},
    {'P', "####." "#...#" "#...#" "####." "#...." "#...." "#...."},
    {'Q', ".###." "#...#" "#...#" "#...#" "#.#.#" "#..#." ".##.#"},
    {'R', "####." "#...#" "#...#" "####." "#.#.." "#..#." "#...#"},
    {'S', ".####" "#...." "#...." ".###." "....#" "....#" "####."},
    {'T', "#####" "..#.." "..#.." "..#.." "..#.." "..#.." "..#.."},
    {'U', "#...#" "#...#" "#...#" "#...#" "#...#" "#...#" ".###."},
    {'V', "#...#" "#...#" "#...#" "#...#" "#...#" ".#.#." "..#.."},
    {'W', "#...#" "#...#" "#...#" "#.#.#" "#.#.#" "##.##" "#...#"},
    {'X', "#...#" "#...#" ".#.#." "..#.." ".#.#." "#...#" "#...#"},
    {'Y', "#...#" "#...#" ".#.#." "..#.." "..#.." "..#.." "..#.."},
    {'Z', "#####" "....#" "...#." "..#.." ".#..." "#...." "#####"},
    {':', "....." "..#.." "..#.." "....." "..#.." "..#.." "....."},
    {'-', "....." "....." "....." "#####" "....." "....." "....."},
    {'.', "....." "....." "....." "....." "....." "..#.." "..#.."},
    {',', "....." "....." "....." "....." "..#.." "..#.." ".#..."},
    {'!', "..#.." "..#.." "..#.." "..#.." "..#.." "....." "..#.."},
    {'?', ".###." "#...#" "....#" "...#." "..#.." "....." "..#.."},
    {'/', "....#" "....#" "...#." "..#.." ".#..." "#...." "#...."},
};

// Anything with no glyph draws as a hollow box, so a missing character is
// obvious on screen instead of silently vanishing.
inline constexpr const char* kMissingGlyph =
    "#####" "#...#" "#...#" "#...#" "#...#" "#...#" "#####";

// Looks up one character. Lowercase letters fold to uppercase, since this
// font has only one case.
inline const char* glyphFor(char c) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');

    for (const Glyph& glyph : kGlyphs) {
        if (glyph.character == c) return glyph.pixels;
    }
    return kMissingGlyph;
}

// Is the pixel at (col, row) of this glyph switched on?
inline bool glyphPixel(const char* glyph, int col, int row) {
    return glyph[row * kGlyphWidth + col] == '#';
}

// How wide a string will be once drawn, in screen pixels. Game code needs
// this to center or right-align text; there is no gap after the last
// character, hence the trailing subtraction.
inline int textWidth(const std::string& text, int scale) {
    if (text.empty()) return 0;
    const int advance = (kGlyphWidth + kGlyphSpacing) * scale;
    return static_cast<int>(text.size()) * advance - kGlyphSpacing * scale;
}

inline int textHeight(int scale) { return kGlyphHeight * scale; }

}  // namespace engine
