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
};
const char* weatherName(WeatherPreset preset);
// Returns false without changing the world if SVGs cannot load.
bool buildVectorScenery(engine::World&, engine::TextureCache*, std::vector<engine::Entity>&);

// Bounded reusable particles, rendered with normal ECS Sprites. No gameplay RNG,
// collision, damage, or per-frame texture loading. Owns only its own entities.
class WeatherSystem {
public:
    WeatherSettings settings;
    void start(engine::World&, engine::TextureCache*);
    void clear(engine::World&);
    void update(engine::World&, float dt);
    void controls(engine::InputManager&);
    std::string description() const;
private:
    struct Particle { engine::Entity entity; float seed, depth; float x=0, y=0; };
    std::vector<Particle> particles_;
    std::vector<engine::Entity> splashes_;
    engine::Entity veil_ = 0, bolt_ = 0, atmosphere_ = 0;
    engine::TextureCache* cache_ = nullptr;
    SDL_Texture* particleTexture_ = nullptr;
    float time_ = 0, flash_ = 0, nextFlash_ = 7;
    WeatherPreset active_ = WeatherPreset::Count;
};
}
