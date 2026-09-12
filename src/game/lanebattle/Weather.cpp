#include "Weather.h"
#include "LaneBattle.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace lanebattle {
using namespace engine;
namespace {
constexpr const char* root = "assets/lanebattle/vector/";
struct DriftingCloud {};
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
    for (int i=0;i<144;++i) {
        Entity e=sprite(world,0,0,2,12,i%4==0?kForeLayer:kNearLayer,nullptr);
        world.getComponent<Sprite>(e)->a=0;
        particles_.push_back({e,float((unsigned(i+1)*2654435761u)%10000)/10000.0f,0.45f+float(i%3)*0.275f});
    }
    veil_=sprite(world,0,90,960,220,kNearLayer,nullptr);
    bolt_=sprite(world,630,90,80,180,kNearLayer,cache?cache->load(std::string(root)+"lightning-branch.svg"):nullptr);
    for(int i=0;i<24;++i) {
        auto e=sprite(world,float(i*41),414,6,2,kNearLayer,nullptr);
        world.getComponent<Sprite>(e)->a=0;splashes_.push_back(e);
    }
    world.getComponent<Sprite>(veil_)->a=0;
    world.getComponent<Sprite>(bolt_)->a=0;
    update(world,0);
}
void WeatherSystem::clear(World& world) {
    for (const auto& p:particles_) world.destroyLater(p.entity);
    for(auto e:splashes_) world.destroyLater(e);
    splashes_.clear();
    if(veil_) world.destroyLater(veil_);
    if(bolt_) world.destroyLater(bolt_);
    if(atmosphere_) world.destroyLater(atmosphere_);
    particles_.clear(); veil_=bolt_=atmosphere_=0; active_=WeatherPreset::Count;
    particleTexture_=nullptr; time_=flash_=0; nextFlash_=7;
}
void WeatherSystem::controls(InputManager& input) {
    if(input.wasKeyPressed(SDL_SCANCODE_F6)) settings.preset=WeatherPreset((index(settings.preset)+1)%static_cast<int>(WeatherPreset::Count));
    if(input.wasKeyPressed(SDL_SCANCODE_F7)) settings.intensity=settings.intensity>=0.99f?0.0f:std::min(1.0f,settings.intensity+0.25f);
    if(input.wasKeyPressed(SDL_SCANCODE_F8)) settings.speed=settings.speed>=3?0:settings.speed+0.5f;
    if(input.wasKeyPressed(SDL_SCANCODE_F9)) settings.wind=settings.wind>=1?-1:settings.wind+0.25f;
    if(input.wasKeyPressed(SDL_SCANCODE_F10)) settings.opacity=settings.opacity>=0.99f?0:std::min(1.0f,settings.opacity+0.25f);
    if(input.wasKeyPressed(SDL_SCANCODE_F11)) settings.lightning=LightningMode((static_cast<int>(settings.lightning)+1)%3);
}
std::string WeatherSystem::description() const {
    char text[200];
    const char* light=settings.lightning==LightningMode::Off?"OFF":settings.lightning==LightningMode::Reduced?"REDUCED":"NORMAL";
    std::snprintf(text,sizeof(text),"%s  DENSITY %.2f  SPEED %.1f  WIND %.2f  ALPHA %.2f  FLASH %s",weatherName(settings.preset),settings.intensity,settings.speed,settings.wind,settings.opacity,light);
    return text;
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
    if(active_!=settings.preset) {
        active_=settings.preset; time_=flash_=0; nextFlash_=7;
        particleTexture_=cache_?cache_->load(std::string(root)+style.asset):nullptr;
        for(auto& p:particles_) {p.x=p.seed*1260-150;p.y=170+wrap(p.seed*719,230);}
    }
    time_+=dt*settings.speed;
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
    for (std::size_t i=0;i<particles_.size();++i) {
        auto& p=particles_[i];
        auto* s=world.getComponent<Sprite>(p.entity); auto* t=world.getComponent<Transform>(p.entity);
        if(!s||!t) continue;
        bool fog=active_==WeatherPreset::Fog;
        s->texture=particleTexture_; s->width=std::max(1,int(style.w*p.depth)); s->height=std::max(1,int(style.h*p.depth));
        s->r=active_==WeatherPreset::Sandstorm?210:255;s->g=active_==WeatherPreset::Sandstorm?165:255;s->b=active_==WeatherPreset::Sandstorm?95:255;
        // Integrate positions so changing speed or wind never teleports a particle.
        const float margin=fog?300.0f:150.0f;
        const float span=fog?1440.0f:1260.0f;
        const float drift=fog?32.0f:(active_==WeatherPreset::Sandstorm?240.0f:90.0f)*p.depth;
        p.x=wrap(p.x+margin+dt*settings.speed*settings.wind*drift,span)-margin;
        p.y=170+wrap(p.y-170+dt*settings.speed*style.fall*p.depth,230);
        float x=p.x, y=p.y;
        if(fog) y=320+float(i%3)*25;
        if(active_==WeatherPreset::Snow||active_==WeatherPreset::Ash||active_==WeatherPreset::DarkStorm)
            x+=std::sin(time_*1.3f+p.seed*20)*12*p.depth;
        t->x=std::round(x); t->y=std::round(y);
        // Rain slants with wind. Snow/hail retain their blocky silhouettes.
        t->rotation=(active_==WeatherPreset::Rain||active_==WeatherPreset::BloodRain||active_==WeatherPreset::Thunderstorm)?-std::atan2(settings.wind*90,std::abs(style.fall)):0;
        float alpha=settings.opacity*(i%4==0?60.0f:125.0f)*p.depth;
        // Fade across the vertical recycling boundary instead of popping.
        const float edge= fog?1.0f:std::min(1.0f,std::min(y-170.0f,400.0f-y)/18.0f);
        s->a=i<std::size_t(style.count*settings.intensity)?static_cast<unsigned char>(alpha*std::max(0.0f,edge)):0;
        if(fog) s->layer=kNearLayer;
        else s->layer=i%4==0?kForeLayer:kNearLayer;
        // Missing SVG support must not turn fog into solid giant rectangles.
        if(!particleTexture_ && fog) s->a=0;
    }
    for(std::size_t i=0;i<splashes_.size();++i) {
        auto* s=world.getComponent<Sprite>(splashes_[i]);if(!s) continue;
        bool wet=active_==WeatherPreset::Rain||active_==WeatherPreset::BloodRain||active_==WeatherPreset::Thunderstorm||active_==WeatherPreset::Hail;
        float phase=wrap(time_*2+float(i)*0.317f,1);
        s->width=2+int(phase*7);s->r=active_==WeatherPreset::BloodRain?140:130;s->g=active_==WeatherPreset::BloodRain?48:155;s->b=active_==WeatherPreset::BloodRain?58:175;
        s->a=wet&&i<std::size_t(24*settings.intensity)&&phase<0.3f?static_cast<unsigned char>((1-phase/0.3f)*80*settings.opacity):0;
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
