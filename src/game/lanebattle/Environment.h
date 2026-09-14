#pragma once
#include "Weather.h"

namespace lanebattle {
enum class Environment { Reference, Storm, Frozen, BloodMoon, Ashen, Marsh, Desert, Count };
enum class AmbientEffect { Clouds, Fog, Dust, Smoke, Flames, Water, Vegetation, Lighting, Count };
struct AmbientControl { bool enabled=true; float intensity=1; float speed=1; };
// Public tag lets diagnostics inspect real game entities without owning them.
struct AmbientElement { AmbientEffect effect; };
const char* environmentName(Environment);
WeatherPreset strongWeather(Environment);
bool compatibleWeather(Environment, WeatherPreset);
WeatherPreset nextCompatibleWeather(Environment, WeatherPreset);

// Art and ambient motion own separate entities from selectable weather.
// Changing weather never rebuilds the landscape or restarts its animations.
class EnvironmentSystem {
public:
    bool select(engine::World&, engine::TextureCache*, Environment);
    void clear(engine::World&);
    void update(engine::World&, float dt);
    void controls(engine::InputManager&);
    std::string description() const;
    AmbientControl effects[static_cast<int>(AmbientEffect::Count)];
    AmbientEffect selectedEffect=AmbientEffect::Clouds;
    float cloudDirection=1;
    Environment current() const { return current_; }
    bool strong = false;
    std::size_t entityCount() const { return owned_.size(); }

    // Clouds, fog banks and dust spawn at RANDOM, over time, up to the limits
    // in effects.txt [ambient], each one small, medium or large. These pools
    // are the hard ceiling: made once when the scene is built, so switching
    // scenes or spawning never allocates, and no limit in a data file can ask
    // for more than exists.
    static constexpr int kCloudPool = 8;
    static constexpr int kFogPool = 8;
    static constexpr int kDustPool = 40;
    // How many of a group are showing right now: 0 clouds, 1 fog, 2 dust.
    int activeCount(int kind) const;
private:
    struct Moving {
        engine::Entity id; float x,y,phase; int kind; float clock=0; float velocity=0;
        AmbientEffect effect=AmbientEffect::Dust;
        // The spawned groups (kinds 0-2) only. Everything else is always on.
        bool active=true;
        float scale=1;       // its size against a medium one
        float alpha=1;       // its faintness, from its size
        float fade=1;        // 0..1, fading in when born and out when done
        float life=0;        // how long it lives, for dust; 0 = until it leaves
        float age=0;
        int width=0, height=0;
    };
    void spawn(Moving& m, bool anywhere);
    std::vector<engine::Entity> owned_;
    std::vector<Moving> moving_;
    float spawnDebt_[3] = {0, 0, 0};
    float camera_ = 0;   // last seen camera x, so a cloud spawns at the visible edge
    Environment current_ = Environment::Reference;
};
}
