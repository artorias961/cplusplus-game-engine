#pragma once
#include "engine/ECS.h"
#include "engine/Input.h"
#include "engine/Resources.h"
#include <vector>

namespace lanebattle {
enum class WeatherPreset { Clear, Rain, Snow, Hail, BloodRain, Thunderstorm, Fog, Ash, Embers, DarkStorm, Sandstorm, Count };
enum class LightningMode { Off, Reduced, Normal };
struct WeatherSettings {
    WeatherPreset preset = WeatherPreset::Clear;
    float intensity = 0.55f; // 0..1, particle density
    float speed = 1.0f;     // 0..3, motion only
    float wind = 0.25f;     // -1..1, left to right
    float opacity = 0.65f;  // 0..1
    LightningMode lightning = LightningMode::Reduced;
    bool impacts = true;
};
const char* weatherName(WeatherPreset preset);
// Returns false without changing the world if SVGs cannot load.
bool buildVectorScenery(engine::World&, engine::TextureCache*, std::vector<engine::Entity>&);

// Bounded reusable particles, rendered with normal ECS Sprites. No gameplay RNG,
// collision, damage, or per-frame texture loading. Owns only its own entities.
//
// Particles SPAWN at random, over time, up to a limit — they do not all exist
// at once in a fixed scatter. The pool below is the hard ceiling and is made
// once (so switching weather never allocates); the limit and spawn rate for
// each preset come from effects.txt, and each particle is born small, medium
// or large, which sets how fast it falls, how faint it is, and whether it
// drifts behind the fight or in front of it. See Art.h for the size classes.
class WeatherSystem {
public:
    static constexpr int kPoolSize = 160;
    static constexpr int kSplashPool = 24;

    WeatherSettings settings;
    void start(engine::World&, engine::TextureCache*);
    void clear(engine::World&);
    void update(engine::World&, float dt);
    void controls(engine::InputManager&);
    std::string description() const;

    // How many particles are showing now, and how many the current preset and
    // density allow. The first never exceeds the second; that is the point.
    int activeParticles() const;
    int particleLimit() const;
    // Every entity this system owns, for teardown checks.
    std::size_t entityCount() const;
private:
    struct Particle {
        engine::Entity entity = 0;
        float x = 0, y = 0;
        float fall = 1, drift = 1;   // this particle's multipliers, from its size
        float size = 1;              // its scale against the medium particle
        float alpha = 1;             // its faintness, from its size
        float fade = 0;              // 0..1: fades in when born, out when retired
        float seed = 0;              // a phase for sway and flicker
        int layer = 0;
        bool active = false;
        bool retiring = false;
    };
    struct Splash { engine::Entity entity = 0; float x = 0; float age = 1; };
    void spawn(bool anywhere);
    std::vector<Particle> particles_;
    std::vector<Splash> splashes_;
    float spawnDebt_ = 0;
    engine::Entity veil_ = 0, bolt_ = 0, atmosphere_ = 0;
    engine::TextureCache* cache_ = nullptr;
    SDL_Texture* particleTexture_ = nullptr;
    float time_ = 0, flash_ = 0, nextFlash_ = 7;
    WeatherPreset active_ = WeatherPreset::Count;
};
}
