#pragma once
// ---------------------------------------------------------------------------
// Art.h — the generated artwork, turned into things on the battlefield, and the
// knobs that decide how much of it there is.
//
// Three data files meet here:
//
//   assets/lanebattle/art.txt      FACTS about each image: its grid, and where
//                                  the feet or the centre are in a cell. Written
//                                  by `art_probe`, which measures them — a
//                                  generator asked for 288-pixel sheets and
//                                  delivered 1254-pixel ones, so these numbers
//                                  cannot be taken from what was requested.
//   assets/lanebattle/effects.txt  DESIGN: how many of each effect may exist at
//                                  once, how often they appear, and how big.
//   assets/lanebattle/units.txt    which sheet each unit wears, and how tall it
//                                  stands on the battlefield.
//
// Everything in this file is PRESENTATION. None of it may change a battle's
// outcome: a unit's art is a separate entity that follows it, its footprint for
// combat is untouched, and the randomness it uses comes from its own dice, never
// from anything a fight reads. Tests and both simulators run without a texture
// cache, and in that case every function here quietly does nothing.
// ---------------------------------------------------------------------------

#include <random>
#include <string>

#include "engine/ECS.h"

namespace lanebattle {

// --- The presentation's own dice -------------------------------------------
//
// Separate from anything gameplay reads, so that where a raindrop falls can
// never decide who wins. Seeded from the clock in the game; a test or the
// weather verifier seeds it with a fixed number so its pictures repeat.
std::mt19937& presentationRandom();
void seedPresentation(unsigned seed);
float presentationBetween(float low, float high);  // uniform in [low, high)

// --- Size classes ------------------------------------------------------------
//
// Everything that appears in numbers — raindrops, clouds, sword arcs — comes in
// three sizes, so a crowd of them reads as depth and variety instead of a grid
// of copies. How MANY of each is a mix per effect in effects.txt; how much
// bigger a large one is than a small one is global, in effects.txt [sizes].
enum class SizeClass { Small, Medium, Large, Count };

struct SizeMix {
    int small = 50;   // of every hundred spawned, this many are small,
    int medium = 35;  // this many medium,
    int large = 15;   // and this many large. Need not add up to a hundred.
};

// Which class a roll in [0, 1) lands in, for this mix. Pure, so it can be
// tested without dice.
SizeClass pickSize(const SizeMix& mix, float roll);
float sizeScale(SizeClass size);
const char* sizeName(SizeClass size);

// --- Sheets: art.txt -----------------------------------------------------------

struct SheetInfo {
    std::string file;        // resolved against the executable, like any asset
    int frameWidth = 0;
    int frameHeight = 0;
    int columns = 6;         // frames per row
    int rows = 1;            // a unit sheet has one row per Pose
    float anchorX = 0.0f;    // the point placed ON the target: a unit's feet,
    float anchorY = 0.0f;    //   an effect's centre — in cell pixels
    float figureHeight = 0.0f;  // how tall the standing figure is, in cell pixels
};

constexpr const char* kArtPath = "assets/lanebattle/art.txt";
bool loadArt(const std::string& path);
void resetArt();
const SheetInfo* sheetInfo(const std::string& file);
int sheetCount();

// --- Tuning: effects.txt ----------------------------------------------------------

// A picture shown when something happens in a fight: a blow landing, a shell
// bursting, a spell going off.
struct EffectKind {
    std::string name;       // what units.txt's `blow` and the code ask for
    std::string sheet;
    float size = 32.0f;     // how big a MEDIUM one is on screen, in pixels
    SizeMix mix;
    int chance = 100;       // percent of the times it COULD appear that it does
    int limit = 6;          // never more than this many at once
    float seconds = 0.3f;   // how long it plays
    bool flies = false;     // a projectile, travelling to its target
    float speed = 500.0f;   // how fast, if it flies
};

// Weather: how much of a preset may be on screen, and how fast it arrives.
struct WeatherTuning {
    std::string name;       // matches weatherName(): RAIN, SNOW, ...
    int limit = 100;        // most particles at once, at full density
    float spawn = 80.0f;    // new particles per second, at full density
    float width = 2.0f;     // a MEDIUM particle, in pixels
    float height = 12.0f;
    SizeMix mix;
};

// The ambient scenery that drifts across every scene: clouds, fog banks and
// the dust, sparks or ghost lights that suit it.
struct AmbientTuning {
    std::string name;       // CLOUDS, FOG or DUST
    int limit = 4;
    float spawn = 0.25f;    // new ones per second
    float size = 200.0f;    // a MEDIUM one's width, in pixels
    SizeMix mix;
};

constexpr const char* kEffectsPath = "assets/lanebattle/effects.txt";
bool loadEffects(const std::string& path);
void resetEffects();
int effectLimit();          // every combat effect together
const EffectKind* effectKind(const std::string& name);
int effectKindCount();
const EffectKind& effectKindAt(int index);
const WeatherTuning* weatherTuning(const std::string& name);
const AmbientTuning* ambientTuning(const std::string& name);

// Both files, from where the game keeps them. What main() and the tools call.
void loadPresentation();

// --- Poses -----------------------------------------------------------------
//
// The order the unit sheets were drawn in: one row each, six frames.
enum class Pose { Idle, Move, Attack, Hurt, Stunned, Death, Count };

// What a unit should be showing, in priority order: a blow in progress beats a
// flinch, a flinch beats walking, and walking beats standing still. Pure.
Pose choosePose(bool moving, bool attacking, bool hurt);

// The frame interval for a walk, from how fast the unit actually moves: one
// full cycle per `stride` pixels travelled, so a runner's legs go faster than
// an ogre's without either being told — held inside the range where a
// six-frame pixel-art cycle reads as walking at all (about 6 to 14 frames a
// second). Outside it, a fast unit strobes and a slow one sticks.
float walkFrameSeconds(float speed, float stride, int frames);

// How far a figure's body lifts, in pixels, at a point `progress` (0..1) of its
// cycle. A walker rises twice a cycle — once per step — and a flyer once, on
// its downstroke. The generated sheets keep the body level in every frame, so
// without this a unit's legs cycle while its body glides at one height, which
// reads as sliding, and a griffin reads as a cut-out being pulled on a wire.
float strideLift(float progress, float height, bool flying);

// --- Components ------------------------------------------------------------------

// A unit's artwork. Lives on the unit's Figure entity, in place of the stick
// figure's Polygon: the unit keeps its own Sprite as its footprint for combat
// (invisible once art is on), and this follows it.
struct ArtFigure {
    const SheetInfo* sheet = nullptr;
    float scale = 1.0f;
    int pose = -1;
    float lastSwing = 0.0f;
    float lastHealth = -1.0f;
    float attackLeft = 0.0f;
    float hurtLeft = 0.0f;

    // Rows of cell pixels skipped at the top of every frame: where the row
    // ABOVE in the sheet spills into this one. Generated sheets are not
    // padded, and the feet of one pose drew as specks over the head of the
    // next — most visibly above a flying griffin.
    int inset = 0;

    // Where a walk had got to when it was interrupted, so a unit that stops
    // for a moment in a queue picks its stride up again instead of restarting
    // it from the first frame every time.
    int walkFrame = 0;
    float walkElapsed = 0.0f;
    float sinceWalk = 1000.0f;
    float stillFor = 0.0f;  // how long it has stood still; brief stops do not count
    bool flying = false;
};

// What is left of a unit with artwork: its death row playing out where it fell,
// then fading. Presentation only — the unit itself is already gone, paid for
// and out of the fight in the same frame it died, exactly as before.
struct Corpse {
    float age = 0.0f;
    float groundY = 0.0f;   // where a falling flyer stops
    float fall = 0.0f;
};

struct CombatEffect {
    int kind = 0;
};

// --- In battle -----------------------------------------------------------------

// The unit's art figure, or kInvalidEntity when there is no art for it (no
// texture cache, no sheet, or a sheet that will not load) — in which case the
// caller gives it a stick figure as before.
engine::Entity createArtFigure(engine::World& world, engine::Entity owner,
                               int kind, bool leftSide);

// Swaps the sheet an art figure wears. How a hero turns into the path its
// owner chose. Does nothing if the sheet cannot be loaded.
void reskinFigure(engine::World& world, engine::Entity figure,
                  const char* sheet);

// Once per frame, for a Figure that has art.
void poseArtFigure(engine::World& world, engine::Entity figure, float dt);

// At a death: leaves the corpse behind and returns it.
engine::Entity leaveCorpse(engine::World& world, engine::Entity figure);
void updateCorpses(engine::World& world, float dt);

// A picture at (x, y), in world space, if the limits and the dice allow it.
// Returns the effect, or kInvalidEntity when it was not shown — which is the
// COMMON case by design: not every blow needs a flash.
engine::Entity spawnEffect(engine::World& world, const std::string& name,
                           float x, float y, bool facingLeft);

// A projectile flying from one point to another, arriving when it arrives.
// Cosmetic: the blow it illustrates has already landed.
engine::Entity spawnProjectile(engine::World& world, const std::string& name,
                               float fromX, float fromY, float toX, float toY);

int countEffects(engine::World& world, int kind = -1);

// Corpses and effects: the battle's decoration, which a battle clears up with
// its units when it ends.
bool isBattleDressing(engine::World& world, engine::Entity entity);

}  // namespace lanebattle
