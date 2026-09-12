#pragma once
#include "Weather.h"

namespace lanebattle {
enum class Environment { Reference, Storm, Frozen, BloodMoon, Ashen, Marsh, Desert, Count };
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
    Environment current() const { return current_; }
    bool strong = false;
    std::size_t entityCount() const { return owned_.size(); }
private:
    struct Moving { engine::Entity id; float x,y,phase; int kind; };
    std::vector<engine::Entity> owned_;
    std::vector<Moving> moving_;
    Environment current_ = Environment::Reference;
    float time_ = 0;
};
}
