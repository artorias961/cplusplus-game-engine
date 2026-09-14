#include "Weather.h"
#include "Art.h"
#include "LaneBattle.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace lanebattle {
using namespace engine;
namespace {
constexpr const char* root = "assets/lanebattle/vector/";
struct DriftingCloud {};
// `count` is the old fixed particle count, kept as the fallback limit when
// effects.txt has no entry for a preset. The limit that actually applies is
// the one in effects.txt — see weatherTuning().
struct Style { const char* name; const char* asset; int w, h; float fall; int count; };
constexpr Style styles[] = {
    {"OFF", "rain.svg", 2, 12, 0, 0},
    {"RAIN", "rain.svg", 2, 14, 240, 144},
    {"SNOW", "snow.svg", 6, 6, 36, 100},
    {"HAIL", "hail.svg", 5, 5, 330, 110},
    {"BLOOD RAIN", "blood.svg", 3, 15, 160, 110},
    {"THUNDERSTORM", "rain.svg", 3, 18, 320, 144},
    {"FOG", "fog.svg", 300, 60, 0, 12},
    {"ASH", "ash.svg", 4, 4, 22, 90},
    {"EMBERS", "ember.svg", 4, 6, -42, 80},
    {"DARK STORM", "wisp.svg", 7, 10, -65, 110},
    {"SANDSTORM", "ash.svg", 5, 2, 8, 144}
};
int index(WeatherPreset p) { return std::clamp(static_cast<int>(p), 0, static_cast<int>(WeatherPreset::Count)-1); }
float wrap(float v, float span) { return v - std::floor(v / span) * span; }
float finiteClamp(float v, float lo, float hi) { return std::isfinite(v) ? std::clamp(v, lo, hi) : lo; }
Entity sprite(World& w, float x, float y, int width, int height, int layer, SDL_Texture* texture) {
    Entity e = w.createEntity();
    w.addComponent(e, Transform{x,y});
    Sprite s; s.width=width; s.height=height; s.layer=layer; s.texture=texture; s.screenSpace=true;
    w.addComponent(e,s); return e;
}

// The band weather lives in: above the lane, below the HUD at the top.
constexpr float kBandTop = 170.0f;
constexpr float kBandBottom = 400.0f;
constexpr float kMargin = 150.0f;          // how far off-screen a particle may drift
constexpr float kSpan = 960.0f + 2 * kMargin;

// How a size class reads as DEPTH. A small particle is far away: it falls more
// slowly, it is fainter, and it stays behind the fight. A large one is near:
// it falls faster, and drifts in FRONT of the units — kept faint, so it never
// hides what the player is watching. Three sizes that all behaved the same
// would just be noise; three that move differently are a sense of distance.
struct Depth { float fall; float alpha; int layer; };
Depth depthOf(SizeClass size) {
    switch (size) {
        case SizeClass::Small: return {0.6f, 0.65f, kNearLayer};
        case SizeClass::Large: return {1.35f, 0.7f, kForeLayer};
        default:               return {1.0f, 0.9f, kNearLayer};
    }
}

int limitFor(WeatherPreset preset) {
    const Style& style = styles[index(preset)];
    const WeatherTuning* tuning = weatherTuning(style.name);
    return std::clamp(tuning ? tuning->limit : style.count, 0, WeatherSystem::kPoolSize);
}
}
const char* weatherName(WeatherPreset preset) { return styles[index(preset)].name; }
bool buildVectorScenery(World& world, TextureCache* cache, std::vector<Entity>& owned) {
    if (!cache) return false;
    const char* names[] = {"sky.svg", "mountains.svg", "ruins.svg", "vegetation.svg", "cloud.svg"};
    SDL_Texture* textures[5]{};
    for (int i=0;i<5;++i) {
        textures[i]=cache->load(std::string(root)+names[i]);
        if (!textures[i]) return false;
    }
    owned.push_back(sprite(world,0,0,960,420,kSkyLayer,textures[0]));
    const float depth[] = {0, kFarParallax, kMidParallax, kNearParallax};
    for (int i=1;i<4;++i) {
        for (int tile=0;tile<3;++tile) {
            Entity e=sprite(world,static_cast<float>(tile*960),0,960,420,kSkyLayer+i,textures[i]);
            auto& s=*world.getComponent<Sprite>(e); s.screenSpace=false; s.parallax=depth[i];
            owned.push_back(e);
        }
    }
    for (int i=0;i<6;++i) {
        Entity e=sprite(world,static_cast<float>(i*330),110.0f+float(i%3)*24,180,48,kFarLayer,textures[4]);
        auto& s=*world.getComponent<Sprite>(e); s.screenSpace=false; s.parallax=0.12f; s.a=90;
        world.addComponent(e,DriftingCloud{});
        owned.push_back(e);
    }
    return true;
}
void WeatherSystem::start(World& world, TextureCache* cache) {
    clear(world); cache_=cache;
    atmosphere_=sprite(world,0,0,960,420,kNearLayer,nullptr);
    world.getComponent<Sprite>(atmosphere_)->a=0;
    // The pool is made once, here, and is the hard ceiling on every preset.
    // Nothing is created or destroyed while weather changes; a particle is
    // "spawned" by being given a place, a size and a reason to be visible.
    for (int i=0;i<kPoolSize;++i) {
        Entity e=sprite(world,0,0,2,12,kNearLayer,nullptr);
        world.getComponent<Sprite>(e)->a=0;
        Particle p; p.entity=e;
        particles_.push_back(p);
    }
    veil_=sprite(world,0,90,960,220,kNearLayer,nullptr);
    bolt_=sprite(world,630,90,80,180,kNearLayer,cache?cache->load(std::string(root)+"lightning-branch.svg"):nullptr);
    for(int i=0;i<kSplashPool;++i) {
        auto e=sprite(world,0,414,6,2,kNearLayer,nullptr);
        world.getComponent<Sprite>(e)->a=0;
        splashes_.push_back({e,0.0f,1.0f});
    }
    world.getComponent<Sprite>(veil_)->a=0;
    world.getComponent<Sprite>(bolt_)->a=0;
    update(world,0);
}
void WeatherSystem::clear(World& world) {
    for (const auto& p:particles_) world.destroyLater(p.entity);
    for (const auto& s:splashes_) world.destroyLater(s.entity);
    splashes_.clear();
    if(veil_) world.destroyLater(veil_);
    if(bolt_) world.destroyLater(bolt_);
    if(atmosphere_) world.destroyLater(atmosphere_);
    particles_.clear(); veil_=bolt_=atmosphere_=0; active_=WeatherPreset::Count;
    particleTexture_=nullptr; time_=flash_=0; nextFlash_=7; spawnDebt_=0;
}
void WeatherSystem::controls(InputManager& input) {
    if(input.wasKeyPressed(SDL_SCANCODE_F6)) settings.preset=WeatherPreset((index(settings.preset)+1)%static_cast<int>(WeatherPreset::Count));
    if(input.wasKeyPressed(SDL_SCANCODE_F7)) settings.intensity=settings.intensity>=0.99f?0.0f:std::min(1.0f,settings.intensity+0.25f);
    if(input.wasKeyPressed(SDL_SCANCODE_F8)) settings.speed=settings.speed>=3?0:settings.speed+0.5f;
    if(input.wasKeyPressed(SDL_SCANCODE_F9)) settings.wind=settings.wind>=1?-1:settings.wind+0.25f;
    if(input.wasKeyPressed(SDL_SCANCODE_F10)) settings.opacity=settings.opacity>=0.99f?0:std::min(1.0f,settings.opacity+0.25f);
    if(input.wasKeyPressed(SDL_SCANCODE_F11)) settings.lightning=LightningMode((static_cast<int>(settings.lightning)+1)%3);
    if(input.wasKeyPressed(SDL_SCANCODE_F12)) settings.impacts=!settings.impacts;
}
std::string WeatherSystem::description() const {
    char text[200];
    const char* light=settings.lightning==LightningMode::Off?"OFF":settings.lightning==LightningMode::Reduced?"REDUCED":"NORMAL";
    std::snprintf(text,sizeof(text),"%s  DENSITY %.2f  SPEED %.1f  WIND %.2f  ALPHA %.2f  FLASH %s",weatherName(settings.preset),settings.intensity,settings.speed,settings.wind,settings.opacity,light);
    return text;
}
int WeatherSystem::activeParticles() const {
    int count = 0;
    for (const auto& p : particles_) if (p.active) ++count;
    return count;
}
int WeatherSystem::particleLimit() const {
    return static_cast<int>(static_cast<float>(limitFor(settings.preset)) * settings.intensity);
}
std::size_t WeatherSystem::entityCount() const {
    return particles_.size() + splashes_.size() + (veil_ ? 1 : 0) + (bolt_ ? 1 : 0) +
           (atmosphere_ ? 1 : 0);
}

// Gives one idle particle a place, a size and a direction.
//
// `anywhere` scatters it through the whole band, for the first handful after a
// change of weather — otherwise every new preset would start from an empty sky
// and take a second or two to arrive. After that, particles are born where
// weather comes FROM: falling ones at the top, rising ones at the bottom, and
// the ones that mostly drift sideways (fog, sand) at the upwind edge.
void WeatherSystem::spawn(bool anywhere) {
    auto idle = std::find_if(particles_.begin(), particles_.end(),
                             [](const Particle& p) { return !p.active; });
    if (idle == particles_.end()) return;
    Particle& p = *idle;

    const Style& style = styles[index(active_)];
    const WeatherTuning* tuning = weatherTuning(style.name);
    const SizeClass size = pickSize(tuning ? tuning->mix : SizeMix{},
                                    presentationBetween(0.0f, 1.0f));
    const Depth depth = depthOf(size);
    p.size = sizeScale(size) * presentationBetween(0.9f, 1.1f);
    p.fall = depth.fall;
    p.drift = depth.fall;
    p.alpha = depth.alpha;
    p.layer = depth.layer;
    p.seed = presentationBetween(0.0f, 1.0f);

    const bool fog = active_ == WeatherPreset::Fog;
    if (anywhere) {
        p.x = presentationBetween(-kMargin, 960.0f + kMargin);
        p.y = presentationBetween(kBandTop, kBandBottom);
    } else if (style.fall >= 15.0f) {
        p.x = presentationBetween(-kMargin, 960.0f + kMargin);
        p.y = kBandTop;
    } else if (style.fall <= -15.0f) {
        p.x = presentationBetween(-kMargin, 960.0f + kMargin);
        p.y = kBandBottom;
    } else {
        p.x = settings.wind >= 0.0f ? -kMargin + 10.0f : 960.0f + kMargin - 10.0f;
        p.y = presentationBetween(kBandTop, kBandBottom);
    }
    // Fog lies low, behind the units, whatever its size — a large fog bank in
    // front of the fight would hide the fight.
    if (fog) {
        p.y = presentationBetween(300.0f, 360.0f);
        p.layer = kNearLayer;
    }
    p.active = true;
    p.retiring = false;
    p.fade = anywhere ? 1.0f : 0.0f;
}

void WeatherSystem::update(World& world, float dt) {
    dt=finiteClamp(dt,0,0.1f);
    // Scenery drift is independent of the selected weather and its speed.
    for(auto& [e,cloud]:world.view<DriftingCloud>()) {
        (void)cloud;
        if(auto* t=world.getComponent<Transform>(e)) t->x=wrap(t->x+240+dt*3,1980)-240;
    }
    settings.intensity=finiteClamp(settings.intensity,0,1); settings.speed=finiteClamp(settings.speed,0,3);
    settings.wind=finiteClamp(settings.wind,-1,1); settings.opacity=finiteClamp(settings.opacity,0,1);
    settings.preset=WeatherPreset(index(settings.preset));
    const auto& style=styles[index(settings.preset)];
    const WeatherTuning* tuning = weatherTuning(style.name);
    const int allowed = particleLimit();
    if(active_!=settings.preset) {
        active_=settings.preset; time_=flash_=0; nextFlash_=7; spawnDebt_=0;
        particleTexture_=cache_?cache_->load(std::string(root)+style.asset):nullptr;
        for(auto& p:particles_) {p.active=false;p.retiring=false;}
        for(int i=0;i<allowed*2/5;++i) spawn(true);
    }
    const float step=dt*settings.speed;   // motion time: zero when speed is zero
    time_+=step;
    bool storm=active_==WeatherPreset::Thunderstorm||active_==WeatherPreset::DarkStorm;
    if(auto* s=world.getComponent<Sprite>(atmosphere_)) {
        s->r=active_==WeatherPreset::DarkStorm?25:19;
        s->g=active_==WeatherPreset::DarkStorm?17:26;
        s->b=active_==WeatherPreset::DarkStorm?43:40;
        s->a=static_cast<unsigned char>((storm?105:active_==WeatherPreset::BloodRain?35:0)*settings.opacity*settings.intensity);
        if(active_==WeatherPreset::BloodRain) {s->r=70;s->g=20;s->b=32;}
    }
    flash_=std::max(0.0f,flash_-dt);
    if(!storm||settings.lightning==LightningMode::Off||settings.intensity==0||settings.opacity==0) flash_=0;
    else {
        nextFlash_-=dt;
        if(nextFlash_<=0) { flash_=0.10f; nextFlash_=7.0f+3.0f*(1-settings.intensity); }
    }

    // Density lowered below what is showing: the extras RETIRE, fading out,
    // rather than vanishing mid-air.
    int live = 0;
    for (const auto& p : particles_) if (p.active && !p.retiring) ++live;
    for (auto& p : particles_) {
        if (live <= allowed) break;
        if (p.active && !p.retiring) { p.retiring = true; --live; }
    }

    // New particles, at random, while there is room. Motion time rather than
    // wall time, so speed zero freezes the sky completely — nothing moves AND
    // nothing new appears. The debt is cleared whenever the limit is reached,
    // so room opening up after a long wait brings a trickle, not a burst; and
    // capped at a tenth of a second's worth, so one long frame cannot owe a
    // flood. The cap was a flat four once, which quietly halved heavy rain at
    // low frame rates: at ten frames a second, four a frame is forty a second
    // against the eighty effects.txt asked for.
    if (step > 0.0f && tuning) {
        const float owedAtMost = 1.0f + tuning->spawn * settings.intensity * 0.1f;
        spawnDebt_ = std::min(spawnDebt_ + tuning->spawn * settings.intensity * step, owedAtMost);
        while (spawnDebt_ >= 1.0f) {
            spawnDebt_ -= 1.0f;
            if (live >= allowed) { spawnDebt_ = 0.0f; break; }
            spawn(false);
            ++live;
        }
    }

    const bool fog=active_==WeatherPreset::Fog;
    const bool wet=active_==WeatherPreset::Rain||active_==WeatherPreset::BloodRain||
                   active_==WeatherPreset::Thunderstorm||active_==WeatherPreset::Hail;
    const bool settles=active_==WeatherPreset::Snow||active_==WeatherPreset::Ash;
    const float baseW = tuning ? tuning->width : static_cast<float>(style.w);
    const float baseH = tuning ? tuning->height : static_cast<float>(style.h);
    const float drift=fog?32.0f:(active_==WeatherPreset::Sandstorm?240.0f:90.0f);

    for (auto& p : particles_) {
        auto* s=world.getComponent<Sprite>(p.entity); auto* t=world.getComponent<Transform>(p.entity);
        if(!s||!t) continue;
        if(!p.active) { s->a=0; continue; }
        s->texture=particleTexture_;
        s->width=std::max(1,static_cast<int>(std::lround(baseW*p.size)));
        s->height=std::max(1,static_cast<int>(std::lround(baseH*p.size)));
        s->r=active_==WeatherPreset::Sandstorm?210:255;s->g=active_==WeatherPreset::Sandstorm?165:255;s->b=active_==WeatherPreset::Sandstorm?95:255;
        // Integrate positions so changing speed or wind never teleports a particle.
        p.x=wrap(p.x+kMargin+step*settings.wind*drift*p.drift,kSpan)-kMargin;
        p.y+=step*style.fall*p.fall;

        // Out of the band: a falling particle LANDS — and, if the weather is
        // wet or settles, leaves a mark where it hit — and a rising one leaves
        // the top. Either way it is back in the pool, free to be born again.
        if (style.fall > 0.0f && p.y > kBandBottom) {
            if (settings.impacts && (wet || settles)) {
                auto freeSplash = std::find_if(splashes_.begin(), splashes_.end(),
                                               [](const Splash& sp) { return sp.age >= 1.0f; });
                if (freeSplash != splashes_.end()) { freeSplash->x = p.x; freeSplash->age = 0.0f; }
            }
            p.active=false; s->a=0; continue;
        }
        if (style.fall < 0.0f && p.y < kBandTop) { p.active=false; s->a=0; continue; }

        if (p.retiring) {
            p.fade -= dt * 2.0f;
            if (p.fade <= 0.0f) { p.active=false; p.retiring=false; s->a=0; continue; }
        } else {
            p.fade = std::min(1.0f, p.fade + dt * 3.0f);
        }

        float x=p.x, y=p.y;
        if(active_==WeatherPreset::Snow||active_==WeatherPreset::Ash||active_==WeatherPreset::DarkStorm)
            x+=std::sin(time_*1.3f+p.seed*20)*12*p.fall;
        t->x=std::round(x); t->y=std::round(y);
        // Rain slants with wind. Snow/hail retain their blocky silhouettes.
        t->rotation=(active_==WeatherPreset::Rain||active_==WeatherPreset::BloodRain||active_==WeatherPreset::Thunderstorm)?-std::atan2(settings.wind*90,std::abs(style.fall)):0;
        float alpha=settings.opacity*125.0f*p.alpha*p.fade;
        if(active_==WeatherPreset::Embers) alpha*=0.7f+0.3f*std::sin(time_*5+p.seed*30);
        // Fade across the band's edges instead of popping in and out.
        const float edge= fog?1.0f:std::min(1.0f,std::min(y-kBandTop,kBandBottom-y)/18.0f);
        s->a=static_cast<unsigned char>(std::clamp(alpha*std::max(0.0f,edge),0.0f,255.0f));
        s->layer=p.layer;
        // Missing SVG support must not turn fog into solid giant rectangles.
        if(!particleTexture_ && fog) s->a=0;
    }

    // Splashes, hail bounces and settling specks: one per landing, from a pool
    // of a few, each playing out and returning. Where a drop hits is where it
    // shows, rather than at a row of fixed places along the ground.
    for (auto& splash : splashes_) {
        auto* s=world.getComponent<Sprite>(splash.entity); auto* t=world.getComponent<Transform>(splash.entity);
        if(!s||!t) continue;
        splash.age = std::min(1.0f, splash.age + step / 0.3f);
        if (splash.age >= 1.0f || !settings.impacts) { s->a=0; continue; }
        s->width=2+static_cast<int>(splash.age*7);
        s->r=active_==WeatherPreset::BloodRain?140:130;s->g=active_==WeatherPreset::BloodRain?48:155;s->b=active_==WeatherPreset::BloodRain?58:175;
        s->a=static_cast<unsigned char>((1-splash.age)*80*settings.opacity);
        t->x=std::round(splash.x);
        t->y=active_==WeatherPreset::Hail?414-std::round(std::sin(splash.age*3.14159265f)*7):414;
        if(settles) {s->width=2;s->height=1;s->a=static_cast<unsigned char>(s->a/2);}
        else s->height=2;
    }
    if(auto* s=world.getComponent<Sprite>(veil_)) {
        s->r=active_==WeatherPreset::DarkStorm?86:155; s->g=active_==WeatherPreset::DarkStorm?65:175; s->b=195;
        s->a=static_cast<unsigned char>((flash_/0.1f)*(settings.lightning==LightningMode::Normal?24:7)*settings.opacity*settings.intensity);
    }
    if(auto* s=world.getComponent<Sprite>(bolt_)) {
        s->a=s->texture?static_cast<unsigned char>((flash_/0.1f)*(settings.lightning==LightningMode::Normal?170:45)*settings.opacity*settings.intensity):0;
    }
}
}
