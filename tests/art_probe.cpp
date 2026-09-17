// Before ANY include: SDL.h renames main() unless told otherwise.
#define SDL_MAIN_HANDLED

// ---------------------------------------------------------------------------
// art_probe.cpp — is this art usable, and where does it go?
//
// The fourth instrument, next to engine_bench (is it slow?), campaign_probe (is
// it fair?) and ui_shots (is it readable?). Like them it cannot fail. It reads
// sprite sheets pixel by pixel and prints what an engine needs to know about
// them and what an image generator will not reliably tell you:
//
//   - whether the background is really TRANSPARENT. The generator behind this
//     project's art has twice painted a checkerboard where the alpha should be,
//     and a picture of transparency looks exactly like transparency in most
//     image viewers. An opaque background is a coloured rectangle round every
//     unit in the game.
//   - where each frame's content actually sits in its cell, row by row. The
//     sheets were ASKED for as 288-pixel grids and arrived as 1254-pixel ones;
//     "requested" and "delivered" are different numbers and only the second
//     one can be sliced.
//   - where the FEET are. A walk cycle drawn a few pixels higher in one frame
//     than the next bobs on screen, and a unit anchored by the centre of its
//     picture rather than by its feet floats whenever it raises a sword.
//   - whether a frame bleeds into its neighbour's cell, which slices a piece of
//     one pose onto the edge of another.
//
// `--art` writes those measurements as `assets/lanebattle/art.txt`, which is
// where the game reads every sheet's grid, feet and size from. See Art.h.
//
// USAGE (paths resolve against the executable, like every other asset)
//
//     art_probe                                   every sheet in the art folder
//     art_probe <path> <columns> <rows>           one sheet
//     art_probe --art > assets/lanebattle/art.txt  measure everything into art.txt
//     art_probe --show <path> <columns> <rows>    haze magenta, grid green, as a PNG
//
// Run it on every new image before wiring it in. It takes a second.
// ---------------------------------------------------------------------------

#include <SDL.h>
#include <SDL_image.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace {

// Below this alpha a pixel counts as background. Not zero: generated art has a
// faint haze of nearly-clear pixels round its edges, and counting those as
// content would make every bounding box the size of its cell.
constexpr int kContentAlpha = 40;

std::string besideExecutable(const std::string& path) {
    if (!path.empty() && (path[0] == '/' || path.find(':') != std::string::npos)) {
        return path;
    }
    std::string full = path;
    if (char* base = SDL_GetBasePath()) {
        full = std::string(base) + path;
        SDL_free(base);
    }
    return full;
}

struct Cell {
    bool empty = true;
    int minX = 0, maxX = 0, minY = 0, maxY = 0;  // in cell coordinates
    int footCentre = 0;   // centre of the content's bottom band: the feet
    bool touchesEdge = false;
    int edgePixels = 0;   // content pixels lying on the cell's own border
};

int median(std::vector<int> values) {
    if (values.empty()) return 0;
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

// The pixels, as 32-bit RGBA regardless of how the PNG stored them.
struct Pixels {
    SDL_Surface* surface = nullptr;
    explicit Pixels(const std::string& path) {
        SDL_Surface* loaded = IMG_Load(besideExecutable(path).c_str());
        if (!loaded) return;
        surface = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGBA32, 0);
        SDL_FreeSurface(loaded);
    }
    ~Pixels() {
        if (surface) SDL_FreeSurface(surface);
    }
    int width() const { return surface ? surface->w : 0; }
    int height() const { return surface ? surface->h : 0; }
    const Uint8* at(int x, int y) const {
        return static_cast<const Uint8*>(surface->pixels) + y * surface->pitch + x * 4;
    }
    int alpha(int x, int y) const { return at(x, y)[3]; }
};

Cell measureCell(const Pixels& image, int left, int top, int width, int height) {
    Cell cell;
    cell.minX = width;
    cell.minY = height;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (image.alpha(left + x, top + y) < kContentAlpha) continue;
            cell.empty = false;
            cell.minX = std::min(cell.minX, x);
            cell.maxX = std::max(cell.maxX, x);
            cell.minY = std::min(cell.minY, y);
            cell.maxY = std::max(cell.maxY, y);
        }
    }
    if (cell.empty) return cell;

    // Content that reaches the cell's own border almost certainly continues in
    // the next cell — one pose sliced in two. Counted rather than flagged,
    // because a few stray pixels on a border and a sword cut in half are very
    // different problems that a yes/no answer would report identically.
    for (int x = 0; x < width; ++x) {
        if (image.alpha(left + x, top) >= kContentAlpha) ++cell.edgePixels;
        if (image.alpha(left + x, top + height - 1) >= kContentAlpha) ++cell.edgePixels;
    }
    for (int y = 1; y < height - 1; ++y) {
        if (image.alpha(left, top + y) >= kContentAlpha) ++cell.edgePixels;
        if (image.alpha(left + width - 1, top + y) >= kContentAlpha) ++cell.edgePixels;
    }
    cell.touchesEdge = cell.edgePixels > 0;

    // The feet: the horizontal centre of whatever is in the bottom eighth of the
    // content. The centre of the WHOLE picture moves every time a sword swings
    // or a wing lifts; the feet are what a unit stands on, and anchoring there
    // is what stops a figure sliding about as it animates.
    const int band = std::max(2, (cell.maxY - cell.minY) / 8);
    long sum = 0;
    int count = 0;
    for (int y = cell.maxY - band; y <= cell.maxY; ++y) {
        for (int x = cell.minX; x <= cell.maxX; ++x) {
            if (image.alpha(left + x, top + y) < kContentAlpha) continue;
            sum += x;
            ++count;
        }
    }
    cell.footCentre = count > 0 ? static_cast<int>(sum / count) : (cell.minX + cell.maxX) / 2;
    return cell;
}

// Is the background genuinely clear, or a picture of clear?
void describeBackground(const Pixels& image) {
    long clear = 0;
    long haze = 0;
    const long total = static_cast<long>(image.width()) * image.height();
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const int a = image.alpha(x, y);
            if (a == 0) ++clear;
            else if (a < kContentAlpha) ++haze;
        }
    }
    const int corners[4] = {
        image.alpha(0, 0), image.alpha(image.width() - 1, 0),
        image.alpha(0, image.height() - 1),
        image.alpha(image.width() - 1, image.height() - 1)};
    const bool cornersClear =
        corners[0] == 0 && corners[1] == 0 && corners[2] == 0 && corners[3] == 0;
    const int percent = static_cast<int>(clear * 100 / std::max(1L, total));

    if (percent < 5) {
        std::printf("  background: OPAQUE (%d%% clear) - this is a coloured rectangle\n"
                    "              round every sprite. Regenerate with real alpha.\n",
                    percent);
    } else {
        std::printf("  background: transparent (%d%% of pixels clear, corners %s)\n",
                    percent, cornersClear ? "clear" : "NOT clear");
    }

    // Haze: pixels that are nearly but not quite clear. Invisible on a dark
    // background, which is what every image viewer and every screenshot in
    // this project uses — and a faint grey box round the sprite the moment it
    // stands in front of a bright sky.
    const double hazePercent = static_cast<double>(haze) * 100.0 / std::max(1L, total);
    if (hazePercent >= 1.0) {
        std::printf("  haze:       %.1f%% of pixels are faint (alpha 1-%d) - may show as a\n"
                    "              smudge on a light background\n",
                    hazePercent, kContentAlpha - 1);
    } else {
        std::printf("  haze:       %.2f%% faint pixels - clean\n", hazePercent);
    }
}

void probe(const std::string& path, int columns, int rows) {
    Pixels image(path);
    if (!image.surface) {
        std::printf("\n%s\n  could not load: %s\n", path.c_str(), IMG_GetError());
        return;
    }
    const int cellW = image.width() / columns;
    const int cellH = image.height() / rows;
    std::printf("\n%s\n  %dx%d, read as %d x %d cells of %dx%d%s\n", path.c_str(),
                image.width(), image.height(), columns, rows, cellW, cellH,
                (image.width() % columns || image.height() % rows)
                    ? "  (does NOT divide evenly - the grid is a guess)"
                    : "");
    describeBackground(image);

    std::printf("  row  frames  foot(y)  feet(x)  height  width  bleeds\n");
    std::vector<int> standingFeet, standingCentres, standingHeights;
    for (int row = 0; row < rows; ++row) {
        std::vector<int> feet, centres, heights, widths;
        int frames = 0;
        int bleeding = 0;
        for (int column = 0; column < columns; ++column) {
            const Cell cell =
                measureCell(image, column * cellW, row * cellH, cellW, cellH);
            if (cell.empty) continue;
            ++frames;
            bleeding = std::max(bleeding, cell.edgePixels);
            feet.push_back(cell.maxY);
            centres.push_back(cell.footCentre);
            heights.push_back(cell.maxY - cell.minY + 1);
            widths.push_back(cell.maxX - cell.minX + 1);
        }
        // The worst frame's border count. A handful is stray noise; dozens is a
        // pose that runs into the next cell and will be sliced.
        char bleeds[32];
        if (bleeding == 0) std::snprintf(bleeds, sizeof bleeds, "-");
        else std::snprintf(bleeds, sizeof bleeds, "%d px%s", bleeding,
                           bleeding > 12 ? "  <- sliced" : "");
        std::printf("  %3d  %6d  %7d  %7d  %6d  %5d  %s\n", row, frames,
                    median(feet), median(centres), median(heights),
                    median(widths), bleeds);
        // The standing rows — idle and walking — are what the anchor is taken
        // from. An attack lunges and a death falls over; neither is where the
        // unit actually stands.
        if (row < 2 || rows == 1) {
            standingFeet.insert(standingFeet.end(), feet.begin(), feet.end());
            standingCentres.insert(standingCentres.end(), centres.begin(), centres.end());
            standingHeights.insert(standingHeights.end(), heights.begin(), heights.end());
        }
    }
    std::printf("  units.txt: frame_width = %d  frame_height = %d  frame_count = %d"
                "  art_foot = %d  art_centre = %d   (figure ~%d px tall)\n",
                cellW, cellH, columns, median(standingFeet),
                median(standingCentres), median(standingHeights));
}

// Writes a copy of the sheet where you can SEE the problems the table reports:
// clear pixels black, faint "haze" pixels bright magenta, content as painted,
// and the grid the table assumed drawn in green. Haze is invisible on the dark
// backgrounds every viewer uses, which is the whole reason this exists.
void show(const std::string& path, int columns, int rows) {
    Pixels image(path);
    if (!image.surface) {
        std::printf("could not load %s: %s\n", path.c_str(), IMG_GetError());
        return;
    }
    SDL_Surface* out = SDL_CreateRGBSurfaceWithFormat(
        0, image.width(), image.height(), 32, SDL_PIXELFORMAT_RGBA32);
    if (!out) return;
    const int cellW = image.width() / columns;
    const int cellH = image.height() / rows;
    for (int y = 0; y < image.height(); ++y) {
        Uint8* row = static_cast<Uint8*>(out->pixels) + y * out->pitch;
        for (int x = 0; x < image.width(); ++x) {
            const Uint8* in = image.at(x, y);
            Uint8* px = row + x * 4;
            const bool gridLine = (x % cellW == 0) || (y % cellH == 0);
            if (gridLine) {
                px[0] = 0; px[1] = 200; px[2] = 0;
            } else if (in[3] == 0) {
                px[0] = px[1] = px[2] = 0;
            } else if (in[3] < kContentAlpha) {
                px[0] = 255; px[1] = 0; px[2] = 255;
            } else {
                px[0] = in[0]; px[1] = in[1]; px[2] = in[2];
            }
            px[3] = 255;
        }
    }
    const std::string name =
        std::filesystem::path(path).stem().string() + "-probe.png";
    const std::string destination = besideExecutable(name);
    if (IMG_SavePNG(out, destination.c_str()) == 0) {
        std::printf("wrote %s\n", destination.c_str());
    }
    SDL_FreeSurface(out);
}

// The facts art.txt holds about one sheet, measured.
//
// For a unit (several rows, one per pose) the anchor is the FEET, taken from
// the idle and walking rows only — an attack lunges and a death falls over, and
// neither is where the unit stands — and `figure` is how tall it stands. For a
// one-row strip (an effect, animated scenery) the anchor is the middle of the
// picture and `figure` is its larger side, which is what "a 30-pixel sword arc"
// should mean whether the arc is tall or wide.
struct Measured {
    bool ok = false;
    int cellW = 0, cellH = 0;
    int anchorX = 0, anchorY = 0, figure = 0;
};

Measured measureSheet(const std::string& path, int columns, int rows) {
    Measured m;
    Pixels image(path);
    if (!image.surface) return m;
    m.cellW = image.width() / columns;
    m.cellH = image.height() / rows;
    std::vector<int> ax, ay, size;
    const int standingRows = rows == 1 ? 1 : std::min(rows, 2);
    for (int row = 0; row < standingRows; ++row) {
        for (int column = 0; column < columns; ++column) {
            const Cell cell =
                measureCell(image, column * m.cellW, row * m.cellH, m.cellW, m.cellH);
            if (cell.empty) continue;
            if (rows == 1) {
                ax.push_back((cell.minX + cell.maxX) / 2);
                ay.push_back((cell.minY + cell.maxY) / 2);
                size.push_back(std::max(cell.maxX - cell.minX, cell.maxY - cell.minY) + 1);
            } else {
                ax.push_back(cell.footCentre);
                ay.push_back(cell.maxY);
                size.push_back(cell.maxY - cell.minY + 1);
            }
        }
    }
    if (ax.empty()) return m;
    m.ok = true;
    m.anchorX = median(ax);
    m.anchorY = median(ay);
    m.figure = median(size);
    return m;
}

// Defined below, with the rest of the motion measurement.
unsigned pingPongRows(const std::string& path, int columns, int rows);

// Prints art.txt: every unit sheet, effect strip and animated scenery strip in
// the art folder, measured. Redirect it into assets/lanebattle/art.txt.
void writeArtFile() {
    const std::string root = "assets/lanebattle/lane-battle-gba-art/";
    const std::filesystem::path base = besideExecutable(root);
    std::vector<std::string> files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(base)) {
        if (entry.path().extension() != ".png") continue;
        files.push_back(std::filesystem::relative(entry.path(), base).generic_string());
    }
    std::sort(files.begin(), files.end());

    std::printf(
        "# ---------------------------------------------------------------------------\n"
        "# art.txt - facts about each generated image, MEASURED by art_probe.\n"
        "#\n"
        "# Do not edit by hand. When an image changes, regenerate this file:\n"
        "#\n"
        "#     build/Release/art_probe --art > assets/lanebattle/art.txt\n"
        "#\n"
        "# The generator was asked for 288-pixel grids and delivered 1254-pixel ones,\n"
        "# so the only trustworthy numbers are the ones read off the pixels.\n"
        "#\n"
        "#   frame_width, frame_height  one cell\n"
        "#   columns, rows              frames per row; a unit has one row per pose:\n"
        "#                              idle, walk, attack, hurt, stunned, death\n"
        "#   anchor_x, anchor_y         the point placed on the target, in cell\n"
        "#                              pixels: a unit's FEET, an effect's centre\n"
        "#   figure                     a unit's standing height, or an effect's\n"
        "#                              larger side - what on-screen sizes scale\n"
        "#   ping_pong                  looping rows to play back and forth, because\n"
        "#                              their last drawing does not lead back into\n"
        "#                              their first (see art_probe --motion)\n"
        "# ---------------------------------------------------------------------------\n");
    for (const std::string& file : files) {
        int columns = 1, rows = 1;
        if (file.rfind("units/", 0) == 0) {
            columns = 6;
            rows = 6;
        } else if (file.rfind("effects/", 0) == 0 ||
                   file.rfind("scenery/animated/", 0) == 0) {
            columns = 6;
        } else {
            continue;  // the scenery layers are paintings, not sheets
        }
        const Measured m = measureSheet(root + file, columns, rows);
        if (!m.ok) continue;
        std::printf("\n[sheet]\nfile         = %s%s\nframe_width  = %d\nframe_height = %d\n"
                    "columns      = %d\nrows         = %d\nanchor_x     = %d\n"
                    "anchor_y     = %d\nfigure       = %d\n",
                    root.c_str(), file.c_str(), m.cellW, m.cellH, columns, rows,
                    m.anchorX, m.anchorY, m.figure);
        if (rows > 1) {
            const unsigned mask = pingPongRows(root + file, columns, rows);
            if (mask != 0) {
                std::string list;
                for (int row = 0; row < rows; ++row) {
                    if (!((mask >> row) & 1u)) continue;
                    if (!list.empty()) list += ",";
                    list += std::to_string(row);
                }
                std::printf("ping_pong    = %s\n", list.c_str());
            }
        }
    }
}

// How different two drawings are: the share of their combined silhouette that
// only one of them covers. 0% is the same drawing; 100% shares nothing.
//
// Silhouettes rather than colours, because the question is whether the SHAPE
// moves — a wing, a leg — and a shimmer of shading is not a movement.
double silhouetteChange(const Pixels& image, int ax, int bx, int top, int width, int height) {
    long either = 0;
    long onlyOne = 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const bool a = image.alpha(ax + x, top + y) >= 128;
            const bool b = image.alpha(bx + x, top + y) >= 128;
            if (a || b) ++either;
            if (a != b) ++onlyOne;
        }
    }
    return either > 0 ? static_cast<double>(onlyOne) / static_cast<double>(either) : 0.0;
}

// Whether each looping row of a unit sheet plays as a cycle.
//
// Built when units were said to limp and flyers not to flap, and the frame
// logic was measured innocent: every drawing was on screen for the same time.
// The drawings were not innocent. A row plays 0-1-2-3-4-5 and then jumps back
// to 0, and in a generated sheet nothing promises that 5 leads into 0 — so the
// jump can be the biggest change in the whole row, once a cycle, every cycle,
// which on a walker is a hitch in the stride and on a flyer is a wing that
// snaps back instead of beating. The seam is compared with the ordinary steps.
// The seam, as a multiple of the row's typical step, past which a looping row
// plays back and forth instead. Set where the friendly soldier's walk falls
// (1.4): the jump back is the biggest change in its row, and it was the unit
// first described as limping. The friendly archer's walk (1.1) is a real
// cycle and keeps looping.
constexpr double kSeamRatio = 1.3;

// The seam and the typical step for one row: `seam / step` is the ratio above.
struct Seam {
    double seam = 0.0;
    double step = 0.0;
    double widest = 0.0;
    std::vector<double> steps;
    double ratio() const { return step > 0.0 ? seam / step : 0.0; }
};

Seam measureSeam(const Pixels& image, int row, int columns, int rows) {
    Seam s;
    const int cellW = image.width() / columns;
    const int cellH = image.height() / rows;
    // The top of each cell is skipped, as the game skips it: it holds the row
    // above's feet, which would count as movement that is not in this row.
    const int inset = rows > 1 ? static_cast<int>(std::lround(cellH * 0.04)) : 0;
    const int top = row * cellH + inset;
    for (int c = 0; c + 1 < columns; ++c) {
        s.steps.push_back(silhouetteChange(image, c * cellW, (c + 1) * cellW, top, cellW,
                                           cellH - inset));
    }
    s.seam = silhouetteChange(image, (columns - 1) * cellW, 0, top, cellW, cellH - inset);
    std::vector<double> sorted = s.steps;
    std::sort(sorted.begin(), sorted.end());
    s.step = sorted.empty() ? 0.0 : sorted[sorted.size() / 2];
    for (int c = 1; c < columns; ++c) {
        s.widest = std::max(s.widest, silhouetteChange(image, 0, c * cellW, top, cellW,
                                                       cellH - inset));
    }
    return s;
}

// The rows a unit sheet should play back and forth: its LOOPING rows — idle
// and move, the ones the game repeats — whose seam is past kSeamRatio. Attack,
// hurt and death play once and are never looped, so their seams do not matter.
constexpr int kLoopingRows = 2;

unsigned pingPongRows(const std::string& path, int columns, int rows) {
    const Pixels image(path);
    if (!image.surface || rows < kLoopingRows) return 0;
    unsigned mask = 0;
    for (int row = 0; row < kLoopingRows; ++row) {
        if (measureSeam(image, row, columns, rows).ratio() >= kSeamRatio) mask |= 1u << row;
    }
    return mask;
}

void probeMotion(const std::string& path, int columns, int rows) {
    const Pixels image(path);
    if (!image.surface) {
        std::printf("  %s: could not load\n", path.c_str());
        return;
    }
    static const char* kRows[] = {"idle", "move", "attack"};
    std::printf("  %s\n", path.c_str());
    for (int row = 0; row < std::min(rows, 3); ++row) {
        const Seam s = measureSeam(image, row, columns, rows);
        std::printf("    %-6s steps", kRows[row]);
        for (double step : s.steps) std::printf(" %3.0f", step * 100.0);
        const char* verdict = row >= kLoopingRows          ? "plays once"
                              : s.ratio() >= kSeamRatio ? "BACK AND FORTH: jumps at the seam"
                                                        : "loops";
        std::printf("   seam %3.0f (%.1fx a step)   moves %3.0f%%  %s\n", s.seam * 100.0,
                    s.ratio(), s.widest * 100.0, verdict);
    }
}

void probeMotionEverything() {
    const std::string root = "assets/lanebattle/lane-battle-gba-art/units/";
    const std::filesystem::path base = besideExecutable(root);
    std::vector<std::string> files;
    if (std::filesystem::exists(base)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(base)) {
            if (entry.path().extension() == ".png") {
                files.push_back(std::filesystem::relative(entry.path(), base).generic_string());
            }
        }
    }
    std::sort(files.begin(), files.end());
    std::printf("Silhouette change between neighbouring drawings, in percent. A row whose\n"
                "seam (last drawing back to the first) is far bigger than its steps\n"
                "hitches once a cycle when it loops.\n\n");
    for (const std::string& file : files) probeMotion(root + file, 6, 6);
}

// Everything in the art folder, with the grid each kind of sheet was drawn to.
void probeEverything() {
    const std::string root = "assets/lanebattle/lane-battle-gba-art/";
    const std::filesystem::path base = besideExecutable(root);
    if (!std::filesystem::exists(base)) {
        std::printf("No art folder at %s\n", base.string().c_str());
        return;
    }
    std::vector<std::string> files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(base)) {
        if (entry.path().extension() != ".png") continue;
        files.push_back(std::filesystem::relative(entry.path(), base).generic_string());
    }
    std::sort(files.begin(), files.end());
    for (const std::string& file : files) {
        // Units are six states by six frames; effects and animated scenery are
        // one strip of six. The scenery layers are single paintings.
        if (file.rfind("units/", 0) == 0) {
            probe(root + file, 6, 6);
        } else if (file.rfind("effects/", 0) == 0 ||
                   file.rfind("scenery/animated/", 0) == 0) {
            probe(root + file, 6, 1);
        } else {
            probe(root + file, 1, 1);
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    SDL_SetMainReady();
    if (IMG_Init(IMG_INIT_PNG) == 0) {
        std::printf("SDL_image could not start: %s\n", IMG_GetError());
        return 1;
    }
    if (argc >= 2 && std::string(argv[1]) == "--art") {
        writeArtFile();
    } else if (argc >= 2 && std::string(argv[1]) == "--motion") {
        probeMotionEverything();
    } else if (argc >= 5 && std::string(argv[1]) == "--show") {
        show(argv[2], std::max(1, std::atoi(argv[3])), std::max(1, std::atoi(argv[4])));
    } else if (argc >= 4) {
        probe(argv[1], std::max(1, std::atoi(argv[2])), std::max(1, std::atoi(argv[3])));
    } else if (argc == 1) {
        probeEverything();
    } else {
        std::printf("usage: art_probe                             every sheet in the art folder\n"
                    "       art_probe <path> <cols> <rows>        one sheet\n"
                    "       art_probe --art                       print art.txt, measured\n"
                    "       art_probe --show <path> <cols> <rows> write <name>-probe.png: haze\n"
                    "                                             magenta, grid green\n");
    }
    IMG_Quit();
    return 0;
}
