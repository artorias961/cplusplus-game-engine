// ---------------------------------------------------------------------------
// Art.cpp — see Art.h. The short version: every function here draws, none of
// them decides anything, and all of them do nothing without a texture cache.
// ---------------------------------------------------------------------------

#include "Art.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

#include "LaneBattle.h"
#include "engine/Components.h"
#include "engine/DataFile.h"
#include "engine/Resources.h"

using namespace engine;

namespace lanebattle {
namespace {

// --- Dice ---------------------------------------------------------------------

std::mt19937& dice() {
    // The clock as well as random_device, because some standard libraries have
    // shipped a random_device that returns the same number every run — and a
    // "random" sky that is identical at every launch is exactly the bug the
    // user asked to get rid of.
    static std::mt19937 generator(
        std::random_device{}() ^
        static_cast<unsigned>(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    return generator;
}

// --- Size classes -----------------------------------------------------------------

float gSizeScale[static_cast<int>(SizeClass::Count)] = {0.6f, 1.0f, 1.5f};

// --- Sheets -------------------------------------------------------------------------

std::vector<SheetInfo> gSheets;

// --- Tuning -------------------------------------------------------------------------
//
// The compiled-in defaults, used when effects.txt is missing or leaves a field
// out — the same rule the roster follows, so a bare build still has weather and
// a partial file changes only what it names.

constexpr const char* kFx = "assets/lanebattle/lane-battle-gba-art/effects/";

std::vector<EffectKind> defaultEffects() {
    auto fx = [](const char* name, const char* file, float size, int chance,
                 int limit, float seconds, bool flies = false,
                 float speed = 0.0f) {
        EffectKind kind;
        kind.name = name;
        kind.sheet = std::string(kFx) + file;
        kind.size = size;
        kind.chance = chance;
        kind.limit = limit;
        kind.seconds = seconds;
        kind.flies = flies;
        kind.speed = speed;
        return kind;
    };
    return {
        fx("DAGGER", "dagger-slash.png", 22, 55, 5, 0.24f),
        fx("SWORD", "sword-arc.png", 30, 60, 6, 0.28f),
        fx("SPEAR", "spear-spark.png", 24, 55, 5, 0.26f),
        fx("CLUB", "club-impact.png", 38, 80, 4, 0.32f),
        fx("CLAW", "griffin-claw.png", 32, 65, 4, 0.28f),
        fx("ARROW", "arrow.png", 22, 100, 8, 0.40f, true, 620.0f),
        fx("BOLT", "ballista-bolt.png", 32, 100, 4, 0.40f, true, 700.0f),
        fx("SHIELD", "shield-block.png", 30, 45, 3, 0.30f),
        fx("CANNON", "cannon-explosion.png", 88, 100, 3, 0.50f),
        fx("METEOR", "meteor-impact.png", 110, 100, 2, 0.60f),
        fx("HEAL", "healing.png", 40, 100, 8, 0.60f),
        fx("RAGE", "rage-aura.png", 44, 100, 10, 0.60f),
    };
}

std::vector<WeatherTuning> defaultWeather() {
    auto w = [](const char* name, int limit, float spawn, float width,
                float height, int small, int medium, int large) {
        WeatherTuning tuning;
        tuning.name = name;
        tuning.limit = limit;
        tuning.spawn = spawn;
        tuning.width = width;
        tuning.height = height;
        tuning.mix = SizeMix{small, medium, large};
        return tuning;
    };
    return {
        w("OFF", 0, 0, 2, 12, 50, 35, 15),
        w("RAIN", 90, 80, 2, 14, 45, 40, 15),
        w("SNOW", 70, 10, 7, 7, 40, 40, 20),
        w("HAIL", 60, 75, 4, 4, 50, 35, 15),
        w("BLOOD RAIN", 80, 50, 3, 14, 45, 40, 15),
        w("THUNDERSTORM", 120, 140, 2, 18, 40, 40, 20),
        w("FOG", 8, 0.3f, 260, 54, 30, 45, 25),
        w("ASH", 50, 4, 4, 4, 50, 35, 15),
        w("EMBERS", 45, 7, 3, 5, 50, 35, 15),
        w("DARK STORM", 60, 15, 6, 9, 45, 40, 15),
        w("SANDSTORM", 110, 6, 5, 2, 40, 40, 20),
    };
}

std::vector<AmbientTuning> defaultAmbient() {
    auto a = [](const char* name, int limit, float spawn, float size, int small,
                int medium, int large) {
        AmbientTuning tuning;
        tuning.name = name;
        tuning.limit = limit;
        tuning.spawn = spawn;
        tuning.size = size;
        tuning.mix = SizeMix{small, medium, large};
        return tuning;
    };
    return {
        a("CLOUDS", 5, 0.12f, 220, 30, 45, 25),
        a("FOG", 6, 0.15f, 280, 30, 40, 30),
        a("DUST", 28, 3.0f, 3, 50, 35, 15),
    };
}

std::vector<EffectKind> gEffects = defaultEffects();
std::vector<WeatherTuning> gWeather = defaultWeather();
std::vector<AmbientTuning> gAmbient = defaultAmbient();
int gEffectLimit = 24;

// The ONE place a mix is read from a file, so every section reads it the same way.
SizeMix readMix(const DataSection& section, const SizeMix& fallback) {
    SizeMix mix;
    mix.small = std::max(0, section.integer("small", fallback.small));
    mix.medium = std::max(0, section.integer("medium", fallback.medium));
    mix.large = std::max(0, section.integer("large", fallback.large));
    return mix;
}

// Layers. Corpses lie behind the living; effects sit on top of the fight but
// under the HUD.
constexpr int kCorpseLayer = kNearLayer;
constexpr int kEffectLayer = kForeLayer + 1;

// How long a corpse stays: its death row plays, it lies still, then it fades.
constexpr float kDeathSeconds = 0.6f;
constexpr float kCorpseHold = 0.8f;
constexpr float kCorpseFade = 0.5f;

}  // namespace

// --- Dice ------------------------------------------------------------------------

std::mt19937& presentationRandom() { return dice(); }
void seedPresentation(unsigned seed) { dice().seed(seed); }

float presentationBetween(float low, float high) {
    if (!(high > low)) return low;
    std::uniform_real_distribution<float> range(low, high);
    return range(dice());
}

// --- Size classes -------------------------------------------------------------------

SizeClass pickSize(const SizeMix& mix, float roll) {
    const int total = std::max(0, mix.small) + std::max(0, mix.medium) +
                      std::max(0, mix.large);
    if (total <= 0) return SizeClass::Medium;  // no mix at all: the middle one
    const float point = std::clamp(roll, 0.0f, 0.9999f) * static_cast<float>(total);
    if (point < static_cast<float>(mix.small)) return SizeClass::Small;
    if (point < static_cast<float>(mix.small + mix.medium)) return SizeClass::Medium;
    return SizeClass::Large;
}

float sizeScale(SizeClass size) {
    const int index = std::clamp(static_cast<int>(size), 0,
                                 static_cast<int>(SizeClass::Count) - 1);
    return gSizeScale[index];
}

const char* sizeName(SizeClass size) {
    switch (size) {
        case SizeClass::Small: return "SMALL";
        case SizeClass::Large: return "LARGE";
        default: return "MEDIUM";
    }
}

// --- Sheets ---------------------------------------------------------------------------

bool loadArt(const std::string& path) {
    DataFile file;
    if (!file.load(path)) return false;
    for (const DataSection* section : file.all("sheet")) {
        SheetInfo info;
        info.file = section->text("file", "");
        if (info.file.empty()) continue;
        info.frameWidth = section->integer("frame_width", 0);
        info.frameHeight = section->integer("frame_height", 0);
        info.columns = std::max(1, section->integer("columns", 6));
        info.rows = std::max(1, section->integer("rows", 1));
        // A sheet with no usable cell is not a sheet. Skipped rather than kept,
        // so nothing downstream ever divides by a zero it was handed here.
        if (info.frameWidth <= 0 || info.frameHeight <= 0) continue;
        info.anchorX = section->number("anchor_x", info.frameWidth / 2.0f);
        info.anchorY = section->number("anchor_y", static_cast<float>(info.frameHeight));
        info.figureHeight = section->number("figure", static_cast<float>(info.frameHeight));
        if (info.figureHeight <= 0.0f) info.figureHeight = static_cast<float>(info.frameHeight);

        // Replaces an entry for the same file, so loading twice is harmless.
        auto existing = std::find_if(gSheets.begin(), gSheets.end(),
                                     [&](const SheetInfo& s) { return s.file == info.file; });
        if (existing != gSheets.end()) *existing = info;
        else gSheets.push_back(info);
    }
    return true;
}

void resetArt() { gSheets.clear(); }

const SheetInfo* sheetInfo(const std::string& file) {
    for (const SheetInfo& info : gSheets) {
        if (info.file == file) return &info;
    }
    return nullptr;
}

int sheetCount() { return static_cast<int>(gSheets.size()); }

// --- Tuning -------------------------------------------------------------------------------

bool loadEffects(const std::string& path) {
    DataFile file;
    if (!file.load(path)) return false;

    if (const DataSection* sizes = file.first("sizes")) {
        // Kept in order and positive: a "large" smaller than a "small" is a typo,
        // and a zero scale makes things vanish in a way nobody would trace here.
        float small = sizes->number("small", gSizeScale[0]);
        float medium = sizes->number("medium", gSizeScale[1]);
        float large = sizes->number("large", gSizeScale[2]);
        small = std::clamp(small, 0.1f, 10.0f);
        medium = std::clamp(medium, small, 10.0f);
        large = std::clamp(large, medium, 10.0f);
        gSizeScale[0] = small;
        gSizeScale[1] = medium;
        gSizeScale[2] = large;
    }

    if (const DataSection* all = file.first("effects")) {
        gEffectLimit = std::clamp(all->integer("limit", gEffectLimit), 0, 200);
    }

    // Everything below MERGES by name, like units: a block overrides only the
    // fields it mentions, and a new name adds a new entry.
    for (const DataSection* section : file.all("effect")) {
        const std::string name = section->text("name", "");
        if (name.empty()) continue;
        auto it = std::find_if(gEffects.begin(), gEffects.end(),
                               [&](const EffectKind& k) { return k.name == name; });
        if (it == gEffects.end()) {
            gEffects.push_back(EffectKind{});
            it = gEffects.end() - 1;
            it->name = name;
        }
        it->sheet = section->text("sheet", it->sheet);
        it->size = std::clamp(section->number("size", it->size), 1.0f, 1000.0f);
        it->mix = readMix(*section, it->mix);
        it->chance = std::clamp(section->integer("chance", it->chance), 0, 100);
        it->limit = std::clamp(section->integer("limit", it->limit), 0, 200);
        it->seconds = std::clamp(section->number("seconds", it->seconds), 0.05f, 10.0f);
        it->flies = section->integer("flies", it->flies ? 1 : 0) != 0;
        it->speed = std::clamp(section->number("speed", it->speed), 1.0f, 5000.0f);
    }

    for (const DataSection* section : file.all("weather")) {
        const std::string name = section->text("name", "");
        auto it = std::find_if(gWeather.begin(), gWeather.end(),
                               [&](const WeatherTuning& t) { return t.name == name; });
        if (it == gWeather.end()) continue;  // a preset needs code to draw it
        it->limit = std::clamp(section->integer("limit", it->limit), 0, 400);
        it->spawn = std::clamp(section->number("spawn", it->spawn), 0.0f, 2000.0f);
        it->width = std::clamp(section->number("width", it->width), 1.0f, 1000.0f);
        it->height = std::clamp(section->number("height", it->height), 1.0f, 1000.0f);
        it->mix = readMix(*section, it->mix);
    }

    for (const DataSection* section : file.all("ambient")) {
        const std::string name = section->text("name", "");
        auto it = std::find_if(gAmbient.begin(), gAmbient.end(),
                               [&](const AmbientTuning& t) { return t.name == name; });
        if (it == gAmbient.end()) continue;
        it->limit = std::clamp(section->integer("limit", it->limit), 0, 200);
        it->spawn = std::clamp(section->number("spawn", it->spawn), 0.0f, 100.0f);
        it->size = std::clamp(section->number("size", it->size), 1.0f, 2000.0f);
        it->mix = readMix(*section, it->mix);
    }
    return true;
}

void resetEffects() {
    gEffects = defaultEffects();
    gWeather = defaultWeather();
    gAmbient = defaultAmbient();
    gEffectLimit = 24;
    gSizeScale[0] = 0.6f;
    gSizeScale[1] = 1.0f;
    gSizeScale[2] = 1.5f;
}

int effectLimit() { return gEffectLimit; }

const EffectKind* effectKind(const std::string& name) {
    for (const EffectKind& kind : gEffects) {
        if (kind.name == name) return &kind;
    }
    return nullptr;
}

int effectKindCount() { return static_cast<int>(gEffects.size()); }

const EffectKind& effectKindAt(int index) {
    return gEffects[std::clamp(index, 0, static_cast<int>(gEffects.size()) - 1)];
}

const WeatherTuning* weatherTuning(const std::string& name) {
    for (const WeatherTuning& tuning : gWeather) {
        if (tuning.name == name) return &tuning;
    }
    return nullptr;
}

const AmbientTuning* ambientTuning(const std::string& name) {
    for (const AmbientTuning& tuning : gAmbient) {
        if (tuning.name == name) return &tuning;
    }
    return nullptr;
}

void loadPresentation() {
    loadArt(kArtPath);
    loadEffects(kEffectsPath);
}

// --- Poses ----------------------------------------------------------------------------------

Pose choosePose(bool moving, bool attacking, bool hurt) {
    if (attacking) return Pose::Attack;
    if (hurt) return Pose::Hurt;
    if (moving) return Pose::Move;
    return Pose::Idle;
}

float walkFrameSeconds(float speed, float stride, int frames) {
    if (speed <= 0.0f || stride <= 0.0f || frames <= 0) return 0.15f;
    // Clamped at both ends: a unit crawling at one pixel a second should not
    // hold a frame for ten seconds, and one that sprints should not strobe.
    return std::clamp(stride / speed / static_cast<float>(frames), 0.03f, 0.25f);
}

// --- In battle --------------------------------------------------------------------------------

namespace {

// Places a figure's feet on its unit's feet. Whole pixels, because generated
// pixel art drawn at a fractional position shimmers as it moves.
void placeOnFeet(Transform& at, const Sprite& sprite, const ArtFigure& art,
                 float feetX, float feetY) {
    const float anchorX = sprite.flipX
                              ? static_cast<float>(art.sheet->frameWidth) - art.sheet->anchorX
                              : art.sheet->anchorX;
    at.x = std::round(feetX - anchorX * art.scale);
    at.y = std::round(feetY - art.sheet->anchorY * art.scale);
}

void setPose(Sprite& sprite, Animation& animation, const ArtFigure& art, Pose pose,
             float frameSeconds, bool loop) {
    const int row = std::clamp(static_cast<int>(pose), 0, art.sheet->rows - 1);
    sprite.srcY = row * art.sheet->frameHeight;
    sprite.srcX = 0;
    animation.frame = 0;
    animation.elapsed = 0.0f;
    animation.loop = loop;
    animation.playing = true;
    animation.frameCount = art.sheet->columns;
    animation.frameWidth = art.sheet->frameWidth;
    animation.secondsPerFrame = frameSeconds;
}

// Sizes a sprite to its sheet so the standing figure is `height` pixels tall.
void fitToHeight(Sprite& sprite, ArtFigure& art, float height) {
    art.scale = height / art.sheet->figureHeight;
    sprite.srcW = art.sheet->frameWidth;
    sprite.srcH = art.sheet->frameHeight;
    sprite.width = std::max(1, static_cast<int>(std::lround(art.sheet->frameWidth * art.scale)));
    sprite.height = std::max(1, static_cast<int>(std::lround(art.sheet->frameHeight * art.scale)));
}

SDL_Texture* loadSheet(const char* file) {
    TextureCache* cache = currentTextureCache();
    if (!cache || !file || file[0] == '\0') return nullptr;
    return cache->load(file);
}

}  // namespace

Entity createArtFigure(World& world, Entity owner, int kind, bool leftSide) {
    const UnitKind& stats = unitKind(kind);

    // The enemy wears its own colours when there is a sheet for them, and the
    // player's sheet mirrored when there is not.
    const char* file = (!leftSide && stats.enemySheet && stats.enemySheet[0] != '\0')
                           ? stats.enemySheet
                           : stats.sheet;
    if (!file || file[0] == '\0') return kInvalidEntity;
    const SheetInfo* sheet = sheetInfo(file);
    if (!sheet) return kInvalidEntity;  // an image nobody has measured
    SDL_Texture* texture = loadSheet(file);
    if (!texture) return kInvalidEntity;

    const Entity figure = world.createEntity();
    world.addComponent(figure, Transform{0.0f, 0.0f, 0.0f});
    world.addComponent(figure, Figure{owner});

    ArtFigure art;
    art.sheet = sheet;

    Sprite sprite;
    sprite.texture = texture;
    // Drawn facing right; the side walking left is the mirror image, which is
    // the whole reason Sprite.flipX exists.
    sprite.flipX = !leftSide;
    sprite.layer = kFieldLayer;
    const float height = stats.artHeight > 0.0f ? stats.artHeight : stats.height * 1.4f;
    fitToHeight(sprite, art, height);

    Animation animation;
    setPose(sprite, animation, art, Pose::Move,
            walkFrameSeconds(stats.speed, height * 0.9f, sheet->columns), true);
    art.pose = static_cast<int>(Pose::Move);

    world.addComponent(figure, sprite);
    world.addComponent(figure, animation);
    world.addComponent(figure, art);
    return figure;
}

void reskinFigure(World& world, Entity figure, const char* file) {
    ArtFigure* art = world.getComponent<ArtFigure>(figure);
    Sprite* sprite = world.getComponent<Sprite>(figure);
    const Figure* link = world.getComponent<Figure>(figure);
    if (!art || !sprite || !link || !file || file[0] == '\0') return;
    const SheetInfo* sheet = sheetInfo(file);
    SDL_Texture* texture = loadSheet(file);
    if (!sheet || !texture) return;

    const Unit* unit = world.getComponent<Unit>(link->owner);
    const UnitKind& stats = unitKind(unit ? unit->kind : 0);
    art->sheet = sheet;
    sprite->texture = texture;
    fitToHeight(*sprite, *art, stats.artHeight > 0.0f ? stats.artHeight : stats.height * 1.4f);
    art->pose = -1;  // re-chosen, with the new sheet's rows, on the next pass
}

void poseArtFigure(World& world, Entity figure, float dt) {
    ArtFigure* art = world.getComponent<ArtFigure>(figure);
    Figure* link = world.getComponent<Figure>(figure);
    Sprite* sprite = world.getComponent<Sprite>(figure);
    Animation* animation = world.getComponent<Animation>(figure);
    Transform* at = world.getComponent<Transform>(figure);
    if (!art || !link || !sprite || !animation || !at || !art->sheet) return;

    Unit* unit = world.getComponent<Unit>(link->owner);
    Transform* body = world.getComponent<Transform>(link->owner);
    Velocity* velocity = world.getComponent<Velocity>(link->owner);
    if (!unit || !body || !velocity) return;  // the orphan sweep deals with it

    const UnitKind& stats = unitKind(unit->kind);
    const float height = stats.artHeight > 0.0f ? stats.artHeight : stats.height * 1.4f;

    // A blow landed: fight() sets `swing` back to 1 on every one, so a jump
    // upwards is a new swing even when the last one had not finished decaying.
    if (unit->swing > art->lastSwing + 0.25f) {
        art->attackLeft = std::clamp(stats.attackDelay * 0.7f, 0.25f, 0.6f);
    }
    art->lastSwing = unit->swing;

    // Took damage: flinch, unless mid-blow. Interrupting an attack to flinch
    // makes a fight read as twitching rather than as trading blows.
    if (art->lastHealth >= 0.0f && unit->health < art->lastHealth - 0.01f &&
        art->attackLeft <= 0.0f) {
        art->hurtLeft = 0.22f;
    }
    art->lastHealth = unit->health;

    art->attackLeft = std::max(0.0f, art->attackLeft - dt);
    art->hurtLeft = std::max(0.0f, art->hurtLeft - dt);

    const bool moving = std::fabs(velocity->dx) > 0.01f;
    const Pose pose = choosePose(moving, art->attackLeft > 0.0f, art->hurtLeft > 0.0f);

    if (static_cast<int>(pose) != art->pose) {
        const int frames = art->sheet->columns;
        switch (pose) {
            case Pose::Attack: {
                const float duration = std::clamp(stats.attackDelay * 0.7f, 0.25f, 0.6f);
                setPose(*sprite, *animation, *art, pose, duration / frames, false);
                break;
            }
            case Pose::Hurt:
                setPose(*sprite, *animation, *art, pose, 0.22f / frames, false);
                break;
            case Pose::Move:
                setPose(*sprite, *animation, *art, pose,
                        walkFrameSeconds(stats.speed, height * 0.9f, frames), true);
                break;
            default:
                setPose(*sprite, *animation, *art, pose, 0.16f, true);
                break;
        }
        art->pose = static_cast<int>(pose);
    }

    // Feet on the unit's feet: the bottom centre of its footprint, which is
    // the ground for a soldier and the sky for a griffin.
    placeOnFeet(*at, *sprite, *art, body->x + stats.width / 2.0f, body->y + stats.height);
}

Entity leaveCorpse(World& world, Entity figure) {
    const ArtFigure* art = world.getComponent<ArtFigure>(figure);
    const Sprite* sprite = world.getComponent<Sprite>(figure);
    const Transform* at = world.getComponent<Transform>(figure);
    if (!art || !sprite || !at || !art->sheet) return kInvalidEntity;

    // A fresh entity rather than the figure repurposed: the figure still
    // carries its link to a unit that is about to stop existing, and "a Figure
    // whose owner is gone" is exactly what the orphan sweep deletes.
    const Entity corpse = world.createEntity();
    world.addComponent(corpse, *at);

    Sprite body = *sprite;
    body.layer = kCorpseLayer;
    Animation animation;
    ArtFigure pose = *art;
    setPose(body, animation, pose, Pose::Death, kDeathSeconds / art->sheet->columns, false);
    world.addComponent(corpse, body);
    world.addComponent(corpse, animation);

    Corpse remains;
    remains.groundY = kGroundY - art->sheet->anchorY * art->scale;
    world.addComponent(corpse, remains);
    return corpse;
}

void updateCorpses(World& world, float dt) {
    std::vector<Entity> gone;
    for (auto& [entity, corpse] : world.view<Corpse>()) {
        corpse.age += dt;
        Transform* at = world.getComponent<Transform>(entity);
        Sprite* sprite = world.getComponent<Sprite>(entity);
        if (!at || !sprite) {
            gone.push_back(entity);
            continue;
        }
        // A flyer does not die in mid-air: it falls, and lands where the
        // ground is. Everything on the ground is already there.
        if (at->y < corpse.groundY) {
            corpse.fall += 900.0f * dt;
            at->y = std::min(corpse.groundY, at->y + corpse.fall * dt);
        }
        const float fadeStart = kDeathSeconds + kCorpseHold;
        if (corpse.age >= fadeStart + kCorpseFade) {
            gone.push_back(entity);
        } else if (corpse.age > fadeStart) {
            const float left = 1.0f - (corpse.age - fadeStart) / kCorpseFade;
            sprite->a = static_cast<unsigned char>(255.0f * std::clamp(left, 0.0f, 1.0f));
        }
    }
    for (Entity entity : gone) world.destroyLater(entity);
}

int countEffects(World& world, int kind) {
    int count = 0;
    for (auto& [entity, effect] : world.view<CombatEffect>()) {
        (void)entity;
        if (kind < 0 || effect.kind == kind) ++count;
    }
    return count;
}

namespace {

// The shared half of an effect and a projectile: can it appear, and if so how
// big. Returns false when a limit or the dice say no.
bool admit(World& world, const std::string& name, int& index, const SheetInfo*& sheet,
           SDL_Texture*& texture, float& scale) {
    if (!currentTextureCache()) return false;
    index = -1;
    for (int i = 0; i < effectKindCount(); ++i) {
        if (effectKindAt(i).name == name) index = i;
    }
    if (index < 0) return false;
    const EffectKind& kind = effectKindAt(index);

    // The limits, checked before the dice so a full screen costs no roll. Both
    // are HARD: a big melee could otherwise fill the air with thirty flashes
    // and bury the thing the player is trying to watch.
    if (countEffects(world) >= effectLimit()) return false;
    if (countEffects(world, index) >= kind.limit) return false;
    if (presentationBetween(0.0f, 100.0f) >= static_cast<float>(kind.chance)) return false;

    sheet = sheetInfo(kind.sheet);
    texture = loadSheet(kind.sheet.c_str());
    if (!sheet || !texture) return false;

    const SizeClass size = pickSize(kind.mix, presentationBetween(0.0f, 1.0f));
    // A little jitter inside the class too, so two "medium" flashes side by
    // side are not visibly the same stamp.
    const float pixels = kind.size * sizeScale(size) * presentationBetween(0.9f, 1.1f);
    scale = pixels / sheet->figureHeight;
    return true;
}

Entity makeEffect(World& world, int index, const SheetInfo& sheet, SDL_Texture* texture,
                  float scale, float centreX, float centreY, bool facingLeft,
                  float seconds, bool loop) {
    const Entity effect = world.createEntity();

    Sprite sprite;
    sprite.texture = texture;
    sprite.srcW = sheet.frameWidth;
    sprite.srcH = sheet.frameHeight;
    sprite.width = std::max(1, static_cast<int>(std::lround(sheet.frameWidth * scale)));
    sprite.height = std::max(1, static_cast<int>(std::lround(sheet.frameHeight * scale)));
    sprite.flipX = facingLeft;
    sprite.layer = kEffectLayer;

    const float anchorX =
        facingLeft ? static_cast<float>(sheet.frameWidth) - sheet.anchorX : sheet.anchorX;
    world.addComponent(effect, Transform{std::round(centreX - anchorX * scale),
                                         std::round(centreY - sheet.anchorY * scale), 0.0f});
    world.addComponent(effect, sprite);

    Animation animation;
    animation.frameCount = sheet.columns;
    animation.frameWidth = sheet.frameWidth;
    animation.secondsPerFrame = seconds / static_cast<float>(std::max(1, sheet.columns));
    animation.loop = loop;
    world.addComponent(effect, animation);

    world.addComponent(effect, Lifetime{seconds});
    world.addComponent(effect, CombatEffect{index});
    return effect;
}

}  // namespace

Entity spawnEffect(World& world, const std::string& name, float x, float y,
                   bool facingLeft) {
    int index = -1;
    const SheetInfo* sheet = nullptr;
    SDL_Texture* texture = nullptr;
    float scale = 1.0f;
    if (!admit(world, name, index, sheet, texture, scale)) return kInvalidEntity;
    return makeEffect(world, index, *sheet, texture, scale, x, y, facingLeft,
                      effectKindAt(index).seconds, false);
}

Entity spawnProjectile(World& world, const std::string& name, float fromX, float fromY,
                       float toX, float toY) {
    int index = -1;
    const SheetInfo* sheet = nullptr;
    SDL_Texture* texture = nullptr;
    float scale = 1.0f;
    if (!admit(world, name, index, sheet, texture, scale)) return kInvalidEntity;
    const EffectKind& kind = effectKindAt(index);

    const float dx = toX - fromX;
    const float dy = toY - fromY;
    const float distance = std::sqrt(dx * dx + dy * dy);
    if (distance < 1.0f) return kInvalidEntity;
    const float flight = distance / kind.speed;
    const bool left = dx < 0.0f;

    const Entity shot =
        makeEffect(world, index, *sheet, texture, scale, fromX, fromY, left, flight, true);
    world.addComponent(shot, Velocity{dx / flight, dy / flight});
    // Pointed along its flight. A mirrored sprite points left, so the angle
    // that tilts it toward a target below is measured the other way round.
    if (Transform* at = world.getComponent<Transform>(shot)) {
        at->rotation = left ? std::atan2(-dy, -dx) : std::atan2(dy, dx);
    }
    return shot;
}

bool isBattleDressing(World& world, Entity entity) {
    return world.hasComponent<Corpse>(entity) || world.hasComponent<CombatEffect>(entity);
}

}  // namespace lanebattle
