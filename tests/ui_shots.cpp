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

#include <cstdio>
#include <string>
#include <vector>

#include "Harness.h"
#include "LaneBattle.h"
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
};

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
//     ui_shots --sheet lanebattle/soldier.png 32 48 6
//
// The path resolves against the binary, like every other asset.
int previewSheet(Engine& engine, const char* path, int frameWidth,
                 int frameHeight, int frameCount) {
    SDL_Texture* texture = engine.textures().load(path);
    if (!texture) {
        std::printf("  could not load %s (it resolves against the binary,\n"
                    "  so it wants to be next to %s)\n",
                    path, gOutputDirectory.c_str());
        return 1;
    }

    // Big enough to read, small enough to fit the window. A sheet wider than
    // the preview is scaled down rather than cropped, because a cropped
    // filmstrip silently hides the frames that did not fit.
    const int margin = 20;
    int scale = 4;
    while (scale > 1 &&
           (frameCount * frameWidth * scale + margin * 2 > lanebattle::kWindowWidth ||
            frameHeight * scale * 2 + margin * 3 > lanebattle::kWindowHeight)) {
        --scale;
    }

    World world;
    for (int frame = 0; frame < frameCount; ++frame) {
        // Row one as drawn, row two mirrored — the pair a unit needs, from the
        // one direction an artist has to draw.
        for (int flipped = 0; flipped < 2; ++flipped) {
            Entity entity = world.createEntity();
            world.addComponent(
                entity,
                Transform{static_cast<float>(margin + frame * frameWidth * scale),
                          static_cast<float>(margin +
                                             flipped * (frameHeight * scale + margin)),
                          0.0f});

            Sprite sprite;
            sprite.texture = texture;
            sprite.width = frameWidth * scale;
            sprite.height = frameHeight * scale;
            sprite.srcX = frame * frameWidth;
            sprite.srcY = 0;
            sprite.srcW = frameWidth;
            sprite.srcH = frameHeight;
            sprite.flipX = flipped != 0;
            sprite.screenSpace = true;
            world.addComponent(entity, sprite);
        }
    }

    std::printf("\n%s: %d frames of %dx%d, shown at %dx\n"
                "  top row as drawn, bottom row mirrored by Sprite.flipX\n\n",
                path, frameCount, frameWidth, frameHeight, scale);
    shoot(engine, world, "sheet-preview");
    return 0;
}

int main(int argc, char** argv) {
    // No window, no GPU. Set before the Engine touches SDL_Init.
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    SDL_SetMainReady();

    // `--sheet` is a different job from the screen tour, so it does not take
    // the output directory as argv[1]; it always writes beside the binary.
    const bool sheetMode = argc > 1 && std::strcmp(argv[1], "--sheet") == 0;

    if (argc > 1 && !sheetMode) {
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
                        "<frameCount>\n"
                        "  e.g. ui_shots --sheet lanebattle/soldier.png 32 48 6\n"
                        "  the path resolves against this binary, like any asset\n\n");
            return 1;
        }
        return previewSheet(engine, argv[2], SDL_atoi(argv[3]),
                            SDL_atoi(argv[4]), SDL_atoi(argv[5]));
    }

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
    {
        Battle battle;
        battle.start(0);
        battle.quietEnemy();
        const Entity theirs = lanebattle::spawnUnit(battle.world, false, kSoldier);
        const Entity myCastle = lanebattle::findCastle(battle.world, true);
        battle.world.getComponent<Transform>(theirs)->x =
            battle.world.getComponent<Transform>(myCastle)->x +
            lanebattle::kCastleWidth + 4.0f;
        battle.world.getComponent<Castle>(myCastle)->health = 1.0f;
        battle.driver.step(6);
        shoot(engine, battle.world, "12-defeat");
    }

    std::printf("\n%d screens written. Now go and look at them.\n\n", gShotCount);
    return 0;
}
