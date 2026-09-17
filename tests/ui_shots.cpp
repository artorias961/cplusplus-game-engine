#define SDL_MAIN_HANDLED

// ---------------------------------------------------------------------------
// ui_shots.cpp — writes a PNG of every screen in the game, without a window.
//
// The gap this closes has been open since slice 4 and was written down in the
// plan three times before anything was done about it: nothing in this project
// could see the SCREEN. `render_tests` asserts that one sprite lands where the
// camera says, which is geometry; `lanebattle_tests` asserts about game state,
// which is arithmetic. Neither can notice that a panel is drawn on top of
// another panel, that a label runs off the edge of the window, or that the
// entire stage list is still being drawn over the battlefield — which it was,
// for weeks, until somebody started the game and looked at it.
//
// Same trick as render_tests: SDL's `dummy` video driver plus a software
// renderer gives a real back buffer with no window and no GPU, and
// SDL_RenderReadPixels reads it. The only new part is writing the result out
// as a PNG instead of asserting about individual pixels.
//
// This is NOT a test and is not registered with ctest. It cannot fail; it
// produces pictures, and a person has to look at them. That is the point — the
// class of bug it exists for is exactly the class no assertion catches.
//
//     ui_shots [output-directory]
//
// Default output is `ui_shots/` next to the binary.
// ---------------------------------------------------------------------------

#include <SDL.h>
#include <SDL_image.h>

#include <algorithm>
#include <cstdio>
#include <cstring>  // std::strcmp. See below: this line is why CI was red.
#include <map>
#include <string>
#include <vector>

// `<cstring>` is named rather than assumed. MSVC and macOS's libc++ both pull
// it in through `<string>`, so `std::strcmp` compiled on every machine this was
// written on — and GCC's libstdc++ does not, so the Linux job failed to BUILD.
// Worse than failing: a build failure skips every later step, so for three
// commits the Linux runner never ran a single test and nobody could tell.
// Include what you use; transitive includes are an accident of one library.

#include "Harness.h"
#include "LaneBattle.h"
#include "Art.h"
#include "engine/Engine.h"

using namespace engine;
using lanebattle::Castle;
using lanebattle::Session;
using lanebattle::Unit;

namespace {

constexpr int kRunner = 0;
constexpr int kSoldier = 1;
constexpr int kArcher = 2;
constexpr int kGriffin = 3;

std::string gOutputDirectory;
int gShotCount = 0;

// One frame of `world`, written to a PNG.
void shoot(Engine& engine, World& world, const char* name) {
    engine.drawWorld(world);

    int width = 0;
    int height = 0;
    std::vector<Uint32> pixels = engine.captureFrame(width, height);
    if (pixels.empty() || width <= 0 || height <= 0) {
        std::printf("  !! %s - nothing to capture\n", name);
        return;
    }

    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(
        pixels.data(), width, height, 32,
        width * static_cast<int>(sizeof(Uint32)), SDL_PIXELFORMAT_ARGB8888);
    if (!surface) {
        std::printf("  !! %s - %s\n", name, SDL_GetError());
        return;
    }

    const std::string path = gOutputDirectory + "/" + name + ".png";
    if (IMG_SavePNG(surface, path.c_str()) != 0) {
        std::printf("  !! %s - %s\n", name, IMG_GetError());
    } else {
        ++gShotCount;
        std::printf("  %s\n", path.c_str());
    }
    SDL_FreeSurface(surface);
}

// A battle set up and stepped, ready to be photographed.
struct Battle {
    World world;
    SceneStack scenes;
    harness::Harness driver;

    Battle() : scenes(), driver(world, scenes) {}

    void start(int stage) {
        scenes.push(lanebattle::makeTitleScene());
        driver.step();
        lanebattle::campaignOf(world).stagesUnlocked = stage + 1;
        driver.tap(SDL_SCANCODE_SPACE);
        driver.step(2);
        driver.tap(SDL_SCANCODE_RETURN);
        driver.step(2);
    }

    Session& session() { return *lanebattle::findSession(world); }

    void quietEnemy() {
        for (int kind = 0; kind < lanebattle::kMaxUnitKinds; ++kind) {
            session().enemySpawnCooldowns[kind] = 1.0e9f;
        }
        session().enemyCannonCooldown = 1.0e9f;
    }

    // Sends whatever in `cycle` is affordable and off cooldown, the way the
    // probe's players do, for up to `frames` frames or until the battle ends.
    // For the screens that have to be PLAYED to be honest: a defeat screen
    // explains what the battle did, so a staged battle gives it nothing to say.
    void play(const std::vector<std::pair<int, SDL_Scancode>>& cycle, int frames) {
        SDL_Scancode holding = SDL_SCANCODE_UNKNOWN;
        std::size_t next = 0;
        for (int frame = 0; frame < frames && !session().gameOver; ++frame) {
            if (holding != SDL_SCANCODE_UNKNOWN) {
                driver.release(holding);
                holding = SDL_SCANCODE_UNKNOWN;
            }
            for (std::size_t step = 0; step < cycle.size(); ++step) {
                const auto& [kind, key] = cycle[(next + step) % cycle.size()];
                if (session().spawnCooldowns[kind] > 0.0f) continue;
                if (session().gold < lanebattle::unitKind(kind).cost) continue;
                holding = key;
                driver.hold(key);
                next = (next + step + 1) % cycle.size();
                break;
            }
            driver.step();
        }
        if (holding != SDL_SCANCODE_UNKNOWN) driver.release(holding);
    }
};

int stageNamed(const char* name) {
    for (int stage = 0; stage < lanebattle::stageCount(); ++stage) {
        if (std::strcmp(lanebattle::stageKind(stage).name, name) == 0) return stage;
    }
    return 0;
}

}  // namespace

// --- Sheet preview ----------------------------------------------------------
//
// The one thing a hand-rolled engine genuinely lacks for art work is a way to
// LOOK at an animation without first wiring it into the game. A mature engine
// gives you a preview pane; here it is forty lines, in the same idiom as
// everything else in this folder — render it headlessly and write a PNG.
//
// It answers the two questions a delivered sheet raises: are my frames sliced
// where I think they are, and does mirroring look right? Both are geometry,
// both are invisible until drawn, and both are the sort of thing that
// otherwise gets found after an artist has drawn thirty of them.
//
//     ui_shots --sheet assets/lanebattle/lane-battle-gba-art/units/friendly/soldier.png 209 209 6 [row]
//
// The path resolves against the binary, like every other asset.
int previewSheet(Engine& engine, const char* path, int frameWidth,
                 int frameHeight, int frameCount, int row) {
    SDL_Texture* texture = engine.textures().load(path);
    if (!texture) {
        std::printf("  could not load %s (it resolves against the binary,\n"
                    "  so it wants to be next to %s)\n",
                    path, gOutputDirectory.c_str());
        return 1;
    }
    if (frameWidth <= 0 || frameHeight <= 0 || frameCount <= 0) {
        std::printf("  frame size and count must be positive\n");
        return 1;
    }

    // Big enough to read, small enough to fit the window. A sheet wider than
    // the preview is scaled DOWN rather than cropped, because a cropped
    // filmstrip silently hides the frames that did not fit.
    //
    // Fractional, which it was not. A whole-number scale bottoms out at 1, and
    // six 209-pixel frames at 1x are 1254 pixels wide in a 960-pixel window —
    // so the generated sheets had their last frames quietly cut off, in the
    // one tool whose comment promised that could not happen.
    const float margin = 20.0f;
    const float roomX = static_cast<float>(lanebattle::kWindowWidth) - margin * 2;
    const float roomY = static_cast<float>(lanebattle::kWindowHeight) - margin * 3;
    const float scale = std::min({4.0f, roomX / static_cast<float>(frameCount * frameWidth),
                                  roomY / static_cast<float>(frameHeight * 2)});
    const int shownW = std::max(1, static_cast<int>(frameWidth * scale));
    const int shownH = std::max(1, static_cast<int>(frameHeight * scale));

    World world;
    for (int frame = 0; frame < frameCount; ++frame) {
        // Row one as drawn, row two mirrored — the pair a unit needs, from the
        // one direction an artist has to draw.
        for (int flipped = 0; flipped < 2; ++flipped) {
            Entity entity = world.createEntity();
            world.addComponent(
                entity,
                Transform{margin + static_cast<float>(frame * shownW),
                          margin + static_cast<float>(flipped) * (shownH + margin),
                          0.0f});

            Sprite sprite;
            sprite.texture = texture;
            sprite.width = shownW;
            sprite.height = shownH;
            sprite.srcX = frame * frameWidth;
            sprite.srcY = row * frameHeight;
            sprite.srcW = frameWidth;
            sprite.srcH = frameHeight;
            sprite.flipX = flipped != 0;
            sprite.screenSpace = true;
            world.addComponent(entity, sprite);
        }
    }

    std::printf("\n%s: row %d, %d frames of %dx%d, shown at %.2fx\n"
                "  top row as drawn, bottom row mirrored by Sprite.flipX\n\n",
                path, row, frameCount, frameWidth, frameHeight, scale);
    shoot(engine, world, "sheet-preview");
    return 0;
}

// --- Motion study -------------------------------------------------------------
//
// A screenshot shows a moment; an animation is a sequence, and a sequence that
// is wrong — frames skipped, a cycle strobing, a figure jittering on the spot —
// looks fine in any single frame of it. This lays the sequence out.
//
// Every unit kind walks IN PLACE (its position is put back after each step, so
// the camera never has to chase it), then stands, then swings. It is sampled
// twenty times a second; the result is one PNG, a row per unit and a column per
// moment, and a log of which pose and frame each unit showed at each moment.
//
//     ui_shots --motion
int motionStudy(Engine& engine) {
    using lanebattle::ArtFigure;
    using lanebattle::Figure;
    World world;
    world.addComponent(world.createEntity(), Camera{});

    const int kinds = std::min(lanebattle::unitKindCount(), 8);
    std::vector<Entity> units;
    std::vector<float> homes;
    for (int kind = 0; kind < kinds; ++kind) {
        const Entity unit = lanebattle::spawnUnit(world, true, kind);
        const float x = 60.0f + static_cast<float>(kind) * 115.0f;
        world.getComponent<Transform>(unit)->x = x;
        units.push_back(unit);
        homes.push_back(x);
    }

    constexpr int kBoxW = 110, kBoxH = 90;
    constexpr int kSamples = 30, kStepsPerSample = 3;  // 20 samples a second
    constexpr int kWalkUntil = 14, kStandUntil = 20;   // then they swing
    const float dt = 1.0f / 60.0f;

    SDL_Surface* sheet = SDL_CreateRGBSurfaceWithFormat(
        0, kSamples * kBoxW, kinds * kBoxH, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!sheet) return 1;
    std::vector<std::string> log(static_cast<std::size_t>(kinds));

    for (int sample = 0; sample < kSamples; ++sample) {
        for (int step = 0; step < kStepsPerSample; ++step) {
            for (int i = 0; i < kinds; ++i) {
                Unit* unit = world.getComponent<Unit>(units[i]);
                Velocity* velocity = world.getComponent<Velocity>(units[i]);
                const lanebattle::UnitKind& stats = lanebattle::unitKind(unit->kind);
                velocity->dx = sample < kWalkUntil ? stats.speed : 0.0f;
                // A blow, as fight() would land one, at the start of the swing
                // phase and again every attack delay after it.
                if (sample >= kStandUntil && step == 0 &&
                    (sample - kStandUntil) %
                            std::max(1, static_cast<int>(stats.attackDelay * 20.0f)) == 0) {
                    unit->swing = 1.0f;
                }
            }
            engine::RunBuiltinSystems(world, dt);
            lanebattle::animateUnits(world, dt);
            // The treadmill: back where it started, so only the animation moves.
            for (int i = 0; i < kinds; ++i) {
                world.getComponent<Transform>(units[i])->x = homes[static_cast<std::size_t>(i)];
            }
            lanebattle::animateUnits(world, 0.0f);  // re-place the art on the reset feet
        }

        engine.drawWorld(world);
        int width = 0, height = 0;
        const std::vector<Uint32> pixels = engine.captureFrame(width, height);
        for (int i = 0; i < kinds; ++i) {
            const Unit* unit = world.getComponent<Unit>(units[i]);
            const Transform* body = world.getComponent<Transform>(units[i]);
            const lanebattle::UnitKind& stats = lanebattle::unitKind(unit->kind);
            const int feetX = static_cast<int>(body->x + stats.width / 2.0f);
            const int feetY = static_cast<int>(body->y + stats.height);
            // The box a unit is photographed in: centred on its feet, mostly above.
            for (int y = 0; y < kBoxH; ++y) {
                const int sy = feetY - kBoxH + 8 + y;
                Uint32* row = reinterpret_cast<Uint32*>(
                    static_cast<Uint8*>(sheet->pixels) + (i * kBoxH + y) * sheet->pitch);
                for (int x = 0; x < kBoxW; ++x) {
                    const int sx = feetX - kBoxW / 2 + x;
                    const bool inside = sx >= 0 && sx < width && sy >= 0 && sy < height;
                    // A line where the feet should be, so a bob or a float shows.
                    const bool groundLine = y == kBoxH - 8;
                    row[sample * kBoxW + x] =
                        groundLine ? 0xff305030u : (inside ? pixels[sy * width + sx] : 0xff000000u);
                }
            }
            char entry[32];
            const Animation* animation = world.getComponent<Animation>(unit->figure);
            const ArtFigure* art = world.getComponent<ArtFigure>(unit->figure);
            std::snprintf(entry, sizeof entry, "%c%d ",
                          art ? "IMAHSD?"[std::clamp(art->pose, 0, 6)] : '-',
                          animation ? animation->frame : -1);
            log[static_cast<std::size_t>(i)] += entry;
        }
    }

    const std::string path = gOutputDirectory + "/motion-study.png";
    IMG_SavePNG(sheet, path.c_str());
    SDL_FreeSurface(sheet);
    std::printf("\nMotion study: a row per unit, a column every 1/20 s. Walking, then\n"
                "standing, then swinging. Pose letters: I idle, M move, A attack,\n"
                "H hurt, D death; the number is the frame shown.\n\n");
    for (int i = 0; i < kinds; ++i) {
        const Unit* unit = world.getComponent<Unit>(units[i]);
        std::printf("  %-9s %s\n", lanebattle::unitKind(unit->kind).name,
                    log[static_cast<std::size_t>(i)].c_str());
    }
    std::printf("\n  %s\n", path.c_str());

    // The treadmill shows the cycles; a real battle shows what the cycles
    // survive. Units queue behind a front line, stop and start as it moves,
    // take hits, swing — and every change of pose restarts a row from its first
    // frame. Twenty seconds of a real stage, counting how often each unit's
    // pose changes and how often a walk is cut off before it ever finishes.
    {
        Battle battle;
        battle.start(2);
        int changes = 0, cutWalks = 0, samples = 0;
        std::map<Entity, int> lastPose;
        std::map<Entity, int> walkFrames;  // frames shown since the walk began
        for (int step = 0; step < 60 * 20; ++step) {
            // Keep both sides sending, so there is a queue to live in.
            if (step % 45 == 0) lanebattle::spawnUnit(battle.world, true, step % 90 ? 1 : 2);
            battle.driver.step();
            for (auto& [entity, art] : battle.world.view<ArtFigure>()) {
                const Animation* animation = battle.world.getComponent<Animation>(entity);
                auto previous = lastPose.find(entity);
                if (previous != lastPose.end() && previous->second != art.pose) {
                    ++changes;
                    if (previous->second == static_cast<int>(lanebattle::Pose::Move) &&
                        walkFrames[entity] < 6) {
                        ++cutWalks;  // left the walk before one full cycle
                    }
                    walkFrames[entity] = 0;
                }
                if (art.pose == static_cast<int>(lanebattle::Pose::Move) && animation) {
                    walkFrames[entity] = std::max(walkFrames[entity], animation->frame + 1);
                }
                lastPose[entity] = art.pose;
                ++samples;
            }
        }
        const float figureSeconds = static_cast<float>(samples) / 60.0f;
        std::printf("\n  A real battle, 20 s: %d pose changes over %.0f unit-seconds"
                    " (%.2f a second per unit),\n  %d walks cut off before one full"
                    " cycle.\n",
                    changes, figureSeconds,
                    figureSeconds > 0 ? changes / figureSeconds : 0.0f, cutWalks);
    }

    // What a player actually SEES, frame by frame, in a battle with flyers in
    // it: for each row of the sheet, how much of the time it is on screen, how
    // long each drawing stays up, and — the question — how often a stretch of
    // that row gets as far as its middle before something replaces it.
    {
        Battle battle;
        battle.start(4);  // THE EYRIE: griffins on their side
        constexpr int kPoses = 6;
        struct Tally {
            long frames = 0;                  // render frames this row was up
            long column[6] = {};              // ...showing each drawing
            int stretches = 0;                // times the row was put up
            int reachedMiddle = 0;            // ...and got to drawing 3 or later
            long changes = 0;                 // drawing changes seen
        };
        Tally tally[2][kPoses];               // [ground, air][pose]
        struct Seen { int pose = -1; int frame = -1; int furthest = -1; bool air = false; };
        std::map<Entity, Seen> seen;
        auto close = [&](const Seen& s) {
            if (s.pose < 0 || s.pose >= kPoses) return;
            Tally& t = tally[s.air ? 1 : 0][s.pose];
            ++t.stretches;
            if (s.furthest >= 3) ++t.reachedMiddle;
        };
        constexpr int kSeconds = 40;
        for (int step = 0; step < 60 * kSeconds; ++step) {
            if (step % 50 == 0) {
                static const int kSend[] = {1, 2, 3, 1};
                lanebattle::spawnUnit(battle.world, true, kSend[(step / 50) % 4]);
            }
            battle.driver.step();
            for (auto& [entity, art] : battle.world.view<ArtFigure>()) {
                const Animation* animation = battle.world.getComponent<Animation>(entity);
                if (!animation || art.pose < 0 || art.pose >= kPoses) continue;
                Seen& s = seen[entity];
                s.air = art.flying;
                if (s.pose != art.pose) {
                    close(s);
                    s.pose = art.pose;
                    s.furthest = -1;
                    s.frame = -1;
                }
                Tally& t = tally[s.air ? 1 : 0][s.pose];
                ++t.frames;
                const int column = std::clamp(animation->frame, 0, 5);
                ++t.column[column];
                if (s.frame != animation->frame) ++t.changes;
                s.frame = animation->frame;
                s.furthest = std::max(s.furthest, animation->frame);
            }
        }
        for (auto& [entity, s] : seen) close(s);

        std::printf("\n  What is on screen in THE EYRIE, %d s at 60 fps, per row of the sheet:\n"
                    "  %-12s %6s %9s %11s %12s   share of time on each drawing 0..5\n",
                    kSeconds, "", "time", "held", "stretches", "saw middle");
        static const char* kNames[] = {"idle", "move", "attack", "hurt", "stunned", "death"};
        for (int air = 0; air < 2; ++air) {
            long total = 0;
            for (int pose = 0; pose < kPoses; ++pose) total += tally[air][pose].frames;
            for (int pose = 0; pose < kPoses; ++pose) {
                const Tally& t = tally[air][pose];
                if (t.frames == 0) continue;
                std::printf("  %-6s %-5s %5.0f%% %6.1f fr %9d %10.0f%%   ",
                            air ? "air" : "ground", kNames[pose],
                            100.0 * static_cast<double>(t.frames) / static_cast<double>(std::max(1L, total)),
                            static_cast<double>(t.frames) / static_cast<double>(std::max(1L, t.changes)),
                            t.stretches,
                            100.0 * t.reachedMiddle / std::max(1, t.stretches));
                for (int c = 0; c < 6; ++c) {
                    std::printf("%3.0f ", 100.0 * static_cast<double>(t.column[c]) /
                                              static_cast<double>(t.frames));
                }
                std::printf("\n");
            }
        }
    }
    return 0;
}

int main(int argc, char** argv) {
    // No window, no GPU. Set before the Engine touches SDL_Init.
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    SDL_SetMainReady();

    // Said out loud rather than left to the default, because this tool photographs
    // a VICTORY, and staging one runs the real logic — which banks the reward and
    // saves. It did that to the player's own campaign until the default changed.
    lanebattle::setSavePath("ui_shots-scratch-campaign.txt");

    // `--sheet` is a different job from the screen tour, so it does not take
    // the output directory as argv[1]; it always writes beside the binary.
    const bool sheetMode = argc > 1 && std::strcmp(argv[1], "--sheet") == 0;
    const bool motionMode = argc > 1 && std::strcmp(argv[1], "--motion") == 0;

    if (argc > 1 && !sheetMode && !motionMode) {
        gOutputDirectory = argv[1];
    } else if (char* base = SDL_GetBasePath()) {
        gOutputDirectory = std::string(base) + "ui_shots";
        SDL_free(base);
    } else {
        gOutputDirectory = "ui_shots";
    }

    // No portable mkdir in the standard the rest of this project uses, so this
    // asks the shell. A directory that already exists is fine either way.
#ifdef _WIN32
    std::string command = "if not exist \"" + gOutputDirectory +
                          "\" mkdir \"" + gOutputDirectory + "\"";
#else
    std::string command = "mkdir -p '" + gOutputDirectory + "'";
#endif
    if (std::system(command.c_str()) != 0) {
        std::printf("could not create %s\n", gOutputDirectory.c_str());
    }

    lanebattle::resetBalance();
    lanebattle::loadBalance(lanebattle::kBalancePath);

    Engine engine("ui shots", lanebattle::kWindowWidth, lanebattle::kWindowHeight);

    if (sheetMode) {
        if (argc < 6) {
            std::printf("\nusage: ui_shots --sheet <path> <frameW> <frameH> "
                        "<frameCount> [row]\n"
                        "  e.g. ui_shots --sheet assets/lanebattle/lane-battle-gba-art/"
                        "units/friendly/soldier.png 209 209 6 2\n"
                        "  row is which pose, from 0: idle, walk, attack, hurt,\n"
                        "  stunned, death. art_probe prints the frame size.\n"
                        "  the path resolves against this binary, like any asset\n\n");
            return 1;
        }
        return previewSheet(engine, argv[2], SDL_atoi(argv[3]), SDL_atoi(argv[4]),
                            SDL_atoi(argv[5]), argc > 6 ? SDL_atoi(argv[6]) : 0);
    }

    // The screens as the PLAYER sees them: with the texture cache, so battles
    // draw the environment art, the unit sheets and the effects. Without this
    // every battle screenshot showed the no-art fallback — coloured blocks on
    // procedural hills — which is a game nobody plays any more, photographed
    // faithfully. Fixed dice, so the random weather and effects land in the
    // same places every run and two runs can be compared by eye.
    lanebattle::loadPresentation();
    lanebattle::setTextureCache(&engine.textures());
    lanebattle::seedPresentation(12345);
    if (motionMode) return motionStudy(engine);

    std::printf("\nWriting screens to %s\n\n", gOutputDirectory.c_str());

    // --- The menus ---------------------------------------------------------
    {
        World world;
        SceneStack scenes;
        harness::Harness driver(world, scenes);
        scenes.push(lanebattle::makeTitleScene());
        driver.step(2);
        shoot(engine, world, "01-title");

        driver.tap(SDL_SCANCODE_SPACE);
        driver.step(2);
        shoot(engine, world, "02-stage-select-new-campaign");
    }
    {
        // A campaign part-way through, with money in the bank: unlocked rows,
        // locked rows, and an armoury with things you can afford.
        World world;
        SceneStack scenes;
        harness::Harness driver(world, scenes);
        scenes.push(lanebattle::makeTitleScene());
        driver.step();
        lanebattle::Campaign& campaign = lanebattle::campaignOf(world);
        campaign.stagesUnlocked = 6;
        campaign.bank = 1400;
        campaign.perks[static_cast<int>(lanebattle::Perk::Damage)] = 2;
        campaign.perks[static_cast<int>(lanebattle::Perk::Champion)] = 1;
        driver.tap(SDL_SCANCODE_SPACE);
        driver.step(2);
        shoot(engine, world, "03-stage-select-part-way");

        // The hero screen, in both of its states: nothing chosen yet, and a
        // path taken with levels to buy inside it.
        driver.tap(SDL_SCANCODE_H);
        driver.step(3);
        shoot(engine, world, "03b-hero-no-path-chosen");
    }
    {
        World world;
        SceneStack scenes;
        harness::Harness driver(world, scenes);
        scenes.push(lanebattle::makeTitleScene());
        driver.step();
        lanebattle::Campaign& campaign = lanebattle::campaignOf(world);
        campaign.stagesUnlocked = 6;
        campaign.bank = 900;
        campaign.heroPath = static_cast<int>(lanebattle::HeroPath::Falconer);
        campaign.heroUpgrades[static_cast<int>(lanebattle::HeroPath::Falconer)][0] = 2;
        driver.tap(SDL_SCANCODE_SPACE);
        driver.step(2);
        driver.tap(SDL_SCANCODE_H);
        driver.step(3);
        shoot(engine, world, "03c-hero-path-taken");
    }
    {
        // The army screen: everything owned, four of it carried, one row
        // already trained.
        World world;
        SceneStack scenes;
        harness::Harness driver(world, scenes);
        scenes.push(lanebattle::makeTitleScene());
        driver.step();
        lanebattle::Campaign& campaign = lanebattle::campaignOf(world);
        campaign.stagesUnlocked = 6;
        campaign.bank = 1600;
        campaign.loadout[0] = lanebattle::sellableKind(1);  // SOLDIER
        campaign.loadout[1] = lanebattle::sellableKind(2);  // ARCHER
        campaign.loadout[2] = lanebattle::sellableKind(5);  // OGRE
        campaign.loadout[3] = -1;                           // an empty slot
        campaign.unitLevels[lanebattle::sellableKind(1)] = 2;
        driver.tap(SDL_SCANCODE_SPACE);
        driver.step(2);
        driver.tap(SDL_SCANCODE_A);
        driver.step(3);
        shoot(engine, world, "03d-army-loadout-and-training");
    }
    {
        // The army screen REFUSING something.
        //
        // Both of this screen's refusals were silent until recently, which is
        // indistinguishable from a click the game missed — so the player clicks
        // again, harder. The message that replaced the silence has to be
        // readable and has to not sit on top of the way out, and no assertion
        // anywhere can tell me whether it does.
        //
        // Carrying exactly one unit and clicking it is the refusal that matters
        // most: an empty loadout leaves a player unable to spawn anything, with
        // nothing on screen explaining why.
        World world;
        SceneStack scenes;
        harness::Harness driver(world, scenes);
        scenes.push(lanebattle::makeTitleScene());
        driver.step();
        lanebattle::Campaign& campaign = lanebattle::campaignOf(world);
        campaign.stagesUnlocked = 6;
        campaign.bank = 40;  // too little to train anything, which also shows
        campaign.loadout[0] = lanebattle::sellableKind(1);  // SOLDIER, alone
        for (int slot = 1; slot < lanebattle::kLoadoutSlots; ++slot) {
            campaign.loadout[slot] = -1;
        }
        driver.tap(SDL_SCANCODE_SPACE);
        driver.step(2);
        driver.tap(SDL_SCANCODE_A);
        driver.step(3);

        // Click the name of the only unit carried — the drop that cannot be
        // allowed.
        driver.clickAt(static_cast<int>(lanebattle::kArmyX) + 60,
                       static_cast<int>(lanebattle::armyTop(1)) + 20);
        driver.step(2);
        shoot(engine, world, "03e-army-refusing-a-drop");
    }

    // --- The battle, empty -------------------------------------------------
    {
        Battle battle;
        battle.start(0);
        battle.quietEnemy();
        battle.driver.step(2);
        shoot(engine, battle.world, "04-battle-opening");
    }

    // --- The battle, a real fight -----------------------------------------
    {
        Battle battle;
        battle.start(4);
        battle.session().gold = 4000.0f;
        battle.session().mana = lanebattle::kMaxMana;

        // A mixed line of both sides, met in the middle.
        for (int i = 0; i < 3; ++i) {
            const Entity mine = lanebattle::spawnUnit(battle.world, true, kSoldier);
            battle.world.getComponent<Transform>(mine)->x = 1080.0f - i * 40.0f;
        }
        for (int i = 0; i < 2; ++i) {
            const Entity mine = lanebattle::spawnUnit(battle.world, true, kArcher);
            battle.world.getComponent<Transform>(mine)->x = 980.0f - i * 36.0f;
        }
        const Entity runner = lanebattle::spawnUnit(battle.world, true, kRunner);
        battle.world.getComponent<Transform>(runner)->x = 1120.0f;

        for (int i = 0; i < 3; ++i) {
            const Entity theirs = lanebattle::spawnUnit(battle.world, false, kSoldier);
            battle.world.getComponent<Transform>(theirs)->x = 1180.0f + i * 40.0f;
        }
        const Entity theirArcher = lanebattle::spawnUnit(battle.world, false, kArcher);
        battle.world.getComponent<Transform>(theirArcher)->x = 1320.0f;

        // The sky, on both sides, which nothing has ever looked at.
        const Entity myGriffin = lanebattle::spawnUnit(battle.world, true, kGriffin);
        battle.world.getComponent<Transform>(myGriffin)->x = 1040.0f;
        const Entity theirGriffin = lanebattle::spawnUnit(battle.world, false, kGriffin);
        battle.world.getComponent<Transform>(theirGriffin)->x = 1240.0f;

        battle.driver.step(30);
        shoot(engine, battle.world, "05-battle-melee");

        // The hero, and a spell armed so the panel shows its selected state.
        battle.driver.tap(SDL_SCANCODE_H);
        battle.driver.step(4);
        battle.driver.tap(SDL_SCANCODE_Z);
        battle.driver.step(4);
        shoot(engine, battle.world, "06-battle-hero-and-armed-spell");

        // A cannonball in the air.
        battle.session().gold = 4000.0f;
        battle.session().cannonCooldown = 0.0f;
        battle.session().armedSpell = -1;
        battle.driver.clickAt(300, 410);
        battle.driver.step(2);
        battle.driver.step(12);
        shoot(engine, battle.world, "07-battle-cannon-in-flight");

        // Pause, drawn over a live battle.
        battle.driver.tap(SDL_SCANCODE_P);
        battle.driver.step(3);
        shoot(engine, battle.world, "08-battle-paused");
    }

    // --- The camera at both ends ------------------------------------------
    {
        Battle battle;
        battle.start(4);
        battle.quietEnemy();
        battle.driver.step(2);
        if (Camera* camera = lanebattle::findCamera(battle.world)) {
            camera->x = 0.0f;
        }
        shoot(engine, battle.world, "09-camera-at-your-castle");

        if (Camera* camera = lanebattle::findCamera(battle.world)) {
            camera->x = lanebattle::kCameraMaxX;
        }
        battle.driver.step();
        if (Camera* camera = lanebattle::findCamera(battle.world)) {
            camera->x = lanebattle::kCameraMaxX;
        }
        shoot(engine, battle.world, "10-camera-at-their-castle");
    }

    // --- Winning and losing ------------------------------------------------
    {
        Battle battle;
        battle.start(0);
        battle.quietEnemy();
        const Entity mine = lanebattle::spawnUnit(battle.world, true, kSoldier);
        const Entity theirCastle = lanebattle::findCastle(battle.world, false);
        battle.world.getComponent<Transform>(mine)->x =
            battle.world.getComponent<Transform>(theirCastle)->x -
            lanebattle::unitKind(kSoldier).range + 4.0f;
        battle.world.getComponent<Castle>(theirCastle)->health = 1.0f;
        battle.driver.step(6);
        shoot(engine, battle.world, "11-victory");
    }
    // A defeat PLAYED rather than staged, because the screen now explains what
    // the battle did: soldiers alone against THE EYRIE's air wing, which is the
    // loss the defeat screen was written to explain.
    {
        Battle battle;
        battle.scenes.push(lanebattle::makeTitleScene());
        battle.driver.step();
        lanebattle::Campaign& campaign = lanebattle::campaignOf(battle.world);
        const int eyrie = stageNamed("THE EYRIE");
        campaign.stagesUnlocked = eyrie + 1;
        for (int slot = 0; slot < lanebattle::kLoadoutSlots; ++slot) campaign.loadout[slot] = -1;
        campaign.loadout[1] = kSoldier;
        battle.driver.tap(SDL_SCANCODE_SPACE);
        battle.driver.step(2);
        battle.driver.tap(SDL_SCANCODE_RETURN);
        battle.driver.step(2);
        battle.play({{kSoldier, SDL_SCANCODE_2}},
                    static_cast<int>(lanebattle::kBattleSeconds * 60.0f));
        battle.driver.step(3);
        shoot(engine, battle.world, "12-defeat");
    }

    // And the clock: the mixed army that holds THE GATES and never breaks them,
    // a minute and a half in and then fast-forwarded — the red warning in the
    // last half-minute, the swarm arriving, and the battle it then decides,
    // played to the end.
    {
        Battle battle;
        battle.start(stageNamed("THE GATES"));
        const std::vector<std::pair<int, SDL_Scancode>> mixed = {
            {kSoldier, SDL_SCANCODE_2}, {kSoldier, SDL_SCANCODE_2}, {kArcher, SDL_SCANCODE_3}};
        battle.play(mixed, 90 * 60);
        battle.session().elapsed = lanebattle::kBattleSeconds - 25.0f;
        battle.play(mixed, 30);
        shoot(engine, battle.world, "13-battle-swarm-coming");

        battle.session().elapsed = lanebattle::kBattleSeconds - 0.05f;
        battle.play(mixed, 60 * 2);
        shoot(engine, battle.world, "14-the-swarm");

        battle.play(mixed, 60 * 300);
        battle.driver.step(3);
        shoot(engine, battle.world, "15-defeat-to-the-swarm");
    }

    std::printf("\n%d screens written. Now go and look at them.\n\n", gShotCount);
    return 0;
}
