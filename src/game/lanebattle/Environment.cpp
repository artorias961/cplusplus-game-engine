#include "Environment.h"
#include "LaneBattle.h"
#include <algorithm>
#include <cmath>

namespace lanebattle {
using namespace engine;
namespace {
struct Definition { const char* name; const char* file; unsigned char r,g,b; WeatherPreset weather; };
constexpr Definition definitions[] = {
    {"RUINED BORDER VALLEY","reference",25,30,47,WeatherPreset::Fog},
    {"STORM-BATTERED RUINS","storm",22,28,41,WeatherPreset::Thunderstorm},
    {"FROZEN CITADEL","frozen",38,49,66,WeatherPreset::Snow},
    {"BLOOD-MOON WASTELAND","blood",37,19,37,WeatherPreset::BloodRain},
    {"ASHEN BATTLEFIELD","ashen",31,25,30,WeatherPreset::Embers},
    {"HAUNTED MARSH","marsh",20,35,39,WeatherPreset::Fog},
    {"DESERT NECROPOLIS","desert",49,36,40,WeatherPreset::Sandstorm}
};
int idx(Environment e) { return std::clamp(static_cast<int>(e),0,6); }
float wrap(float v,float span) { return v-std::floor(v/span)*span; }
Entity add(World& w,std::vector<Entity>& owned,float x,float y,int width,int height,int layer,SDL_Texture* texture=nullptr) {
    auto e=w.createEntity(); w.addComponent(e,Transform{x,y});
    Sprite s;s.width=width;s.height=height;s.layer=layer;s.screenSpace=true;s.texture=texture;
    w.addComponent(e,s);owned.push_back(e);return e;
}
}
const char* environmentName(Environment e) { return definitions[idx(e)].name; }
WeatherPreset strongWeather(Environment e) { return definitions[idx(e)].weather; }
bool compatibleWeather(Environment e,WeatherPreset p) {
    if(p==WeatherPreset::Clear||p==WeatherPreset::Fog) return true;
    switch(e) {
        case Environment::Reference:return p!=WeatherPreset::Sandstorm && p!=WeatherPreset::Count;
        case Environment::Storm:return p==WeatherPreset::Rain||p==WeatherPreset::Hail||p==WeatherPreset::Thunderstorm;
        case Environment::Frozen:return p==WeatherPreset::Snow||p==WeatherPreset::Hail;
        case Environment::BloodMoon:return p==WeatherPreset::BloodRain||p==WeatherPreset::DarkStorm;
        case Environment::Ashen:return p==WeatherPreset::Ash||p==WeatherPreset::Embers||p==WeatherPreset::DarkStorm;
        case Environment::Marsh:return p==WeatherPreset::Rain||p==WeatherPreset::Thunderstorm||p==WeatherPreset::DarkStorm;
        case Environment::Desert:return p==WeatherPreset::Sandstorm;
        default:return false;
    }
}
WeatherPreset nextCompatibleWeather(Environment e,WeatherPreset p) {
    for(int i=1;i<=static_cast<int>(WeatherPreset::Count);++i) {
        auto next=WeatherPreset((static_cast<int>(p)+i)%static_cast<int>(WeatherPreset::Count));
        if(compatibleWeather(e,next)) return next;
    }
    return WeatherPreset::Clear;
}
bool EnvironmentSystem::select(World& w,TextureCache* cache,Environment choice) {
    if(!cache) return false;
    const auto& def=definitions[idx(choice)];
    const std::string suffix=choice==Environment::Reference?".png":"-opaque.png";
    auto* landscape=cache->load(std::string("assets/lanebattle/environments/")+def.file+suffix);
    auto* reference=cache->load("assets/lanebattle/environments/reference.png");
    auto* fog=cache->load("assets/lanebattle/vector/fog.svg");
    auto* fleck=cache->load("assets/lanebattle/vector/ash.svg");
    auto* ember=cache->load("assets/lanebattle/vector/ember.svg");
    auto* wisp=cache->load("assets/lanebattle/vector/wisp.svg");
    if(!landscape||!reference||!fog||!fleck||!ember||!wisp) return false;
#if SDL_VERSION_ATLEAST(2,0,12)
    SDL_SetTextureScaleMode(landscape,SDL_ScaleModeNearest);
    SDL_SetTextureScaleMode(reference,SDL_ScaleModeNearest);
#endif
    clear(w);current_=choice;time_=0;
    // The native PNG is retained. Source rectangles only select existing art;
    // there is no lossy rewrite or bitmap hidden inside an SVG.
    int tw=0,th=0;SDL_QueryTexture(landscape,nullptr,nullptr,&tw,&th);
    int rw=0,rh=0;SDL_QueryTexture(reference,nullptr,nullptr,&rw,&rh);
    for(int band=0;band<3;++band) {
        auto e=add(w,owned_,0,float(band*140),960,140,-8);
        auto& s=*w.getComponent<Sprite>(e);s.r=static_cast<unsigned char>(def.r+band*5);s.g=static_cast<unsigned char>(def.g+band*5);s.b=static_cast<unsigned char>(def.b+band*5);
    }
    // Moon is discrete pixel geometry; light pulse is independent from it.
    if(choice==Environment::BloodMoon) {
        for(int row=0;row<15;++row) {
            int half=int(std::sqrt(std::max(0,56*56-(row*8-56)*(row*8-56)))/4)*4;
            auto e=add(w,owned_,float(710-half),float(60+row*8),half*2,8,-4);
            auto& s=*w.getComponent<Sprite>(e);s.r=112;s.g=static_cast<unsigned char>(48+row);s.b=57;s.a=180;
        }
    }
    // A wide landscape with mirrored neighbouring tiles keeps camera edges
    // covered. Only gentle parallax is used: the playable ground never moves.
    for(int tile=-1;tile<3;++tile) {
        const bool original=choice==Environment::Reference;
        auto e=add(w,owned_,float(tile*960),original?200.0f:0.0f,960,original?220:420,-5,landscape);
        auto& s=*w.getComponent<Sprite>(e);s.screenSpace=false;s.parallax=0.10f;s.flipX=(tile%2!=0);
        if(original) {s.srcX=0;s.srcY=int(rh*0.425f);s.srcW=rw;s.srcH=int(rh*0.402f);}
    }
    for(int i=0;i<4;++i) {
        auto e=add(w,owned_,float(i*320-200),65.0f+float(i%3)*27,240,80,-4,reference);
        auto& s=*w.getComponent<Sprite>(e);s.srcX=0;s.srcY=int(rh*0.14f);s.srcW=int(rw*0.27f);s.srcH=int(rh*0.16f);s.a=110;
        moving_.push_back({e,float(i*320-200),65.0f+float(i%3)*27,float(i),0});
    }
    for(int i=0;i<6;++i) {
        auto e=add(w,owned_,float(i*230-280),335.0f+float(i%3)*22,300,48,-2,fog);
        auto& s=*w.getComponent<Sprite>(e);s.a=choice==Environment::Desert?15:40;
        if(choice==Environment::BloodMoon) {s.r=180;s.g=75;s.b=100;}
        if(choice==Environment::Ashen) {s.r=110;s.g=100;s.b=100;}
        moving_.push_back({e,float(i*230-280),335.0f+float(i%3)*22,float(i),1});
    }
    for(int i=0;i<32;++i) {
        bool fire=choice==Environment::Ashen,ghost=choice==Environment::Marsh;
        auto e=add(w,owned_,float((i*137)%1000),float(280+(i*31)%128),ghost?6:3,ghost?8:3,-1,fire?ember:ghost?wisp:fleck);
        auto& s=*w.getComponent<Sprite>(e);s.a=60;
        if(choice==Environment::Desert) {s.r=190;s.g=156;s.b=96;}
        moving_.push_back({e,float((i*137)%1000),float(280+(i*31)%128),float(i)*0.73f,2});
    }
    // Scene-specific low effects: reflection streaks, fissure smoke, powder,
    // dust devils, ruin debris, or supernatural background pulses.
    for(int i=0;i<12;++i) {
        auto e=add(w,owned_,float(i*87),400,choice==Environment::Marsh?28:2,2,-1);
        auto& s=*w.getComponent<Sprite>(e);s.r=110;s.g=125;s.b=137;s.a=35;
        moving_.push_back({e,float(i*87),400,float(i)*1.7f,3});
    }
    auto light=add(w,owned_,0,0,960,420,-3);
    auto& lighting=*w.getComponent<Sprite>(light);
    lighting.r=choice==Environment::BloodMoon?150:choice==Environment::Ashen?145:90;
    lighting.g=choice==Environment::BloodMoon?25:choice==Environment::Ashen?62:108;
    lighting.b=choice==Environment::BloodMoon?60:choice==Environment::Ashen?28:133;
    moving_.push_back({light,0,0,0,4});
    if(choice==Environment::Desert) {
        // Re-sample narrow native-art strips with a one-pixel displacement.
        // Only scenery shimmers; units and HUD use their normal transforms.
        for(int row=0;row<12;++row) {
            auto e=add(w,owned_,0,float(350+row*3),960,2,-4,landscape);
            auto& s=*w.getComponent<Sprite>(e);s.srcX=0;s.srcY=(350+row*3)*th/420;s.srcW=tw;s.srcH=std::max(1,2*th/420);s.a=80;
            moving_.push_back({e,0,float(350+row*3),float(row)*0.6f,5});
        }
    }
    update(w,0);return true;
}
void EnvironmentSystem::clear(World& w) {
    for(auto e:owned_) w.destroyLater(e);
    owned_.clear();moving_.clear();
}
void EnvironmentSystem::update(World& w,float dt) {
    if(!std::isfinite(dt)||dt<0) return;
    dt=std::min(dt,0.1f);time_+=dt;
    float camera=0;
    auto& cameras=w.view<Camera>();
    if(!cameras.empty()) camera=cameras.begin()->second.x;
    for(auto& m:moving_) {
        auto* t=w.getComponent<Transform>(m.id);auto* s=w.getComponent<Sprite>(m.id);if(!s||!t) continue;
        const float strength=strong?1.7f:1.0f;
        if(m.kind==0) {m.x+=dt*(current_==Environment::Storm?8:3)*strength;t->x=std::round(wrap(m.x+300-camera*0.04f,1440)-300);}
        if(m.kind==1) {m.x+=dt*(m.phase<3?5:-3)*strength;t->x=std::round(wrap(m.x+320,1500)-320);s->a=static_cast<unsigned char>((current_==Environment::Desert?14:38)*strength);}
        if(m.kind==2) {
            m.x+=dt*8*strength;float fall=current_==Environment::Ashen?-12.0f:current_==Environment::Frozen?12.0f:3.0f;
            m.y+=dt*fall*strength;
            t->x=std::round(wrap(m.x+20,1000)-20+std::sin(time_*0.5f+m.phase)*5);
            t->y=std::round(275+wrap(m.y-275,130));
            s->a=static_cast<unsigned char>((20+25*(1+std::sin(time_+m.phase)))*strength);
        }
        if(m.kind==3) {
            float cycle=wrap(time_*0.15f+m.phase,1);
            float fade=std::sin(cycle*3.14159265f);
            t->x=std::round(m.x);t->y=m.y;s->a=static_cast<unsigned char>(fade*35*strength);
            if(current_==Environment::Marsh) {t->x=std::round(m.x+std::sin(time_+m.phase)*3);s->width=24+int(fade*14);s->r=80;s->g=140;s->b=138;}
            else if(current_==Environment::Ashen) {t->y=std::round(412-cycle*50);s->width=4+int(cycle*12);s->height=4+int(cycle*10);s->r=85;s->g=76;s->b=78;}
            else if(current_==Environment::Desert) {t->x=std::round(m.x+std::sin(time_*2+m.phase)*8);t->y=std::round(414-cycle*55);s->width=3+int(cycle*12);s->r=180;s->g=147;s->b=94;}
            else if(current_==Environment::BloodMoon) {s->width=70;s->height=2;s->r=130;s->g=38;s->b=60;s->a=static_cast<unsigned char>(fade*16);}
            else if(current_==Environment::Frozen) {t->x=std::round(wrap(m.x+time_*12,1000)-20);s->width=32;s->height=2;s->r=150;s->g=172;s->b=184;}
            else {t->y=std::round(240+cycle*174);s->width=2;s->height=3;s->a=cycle<0.65f?0:static_cast<unsigned char>(fade*80);}
        }
        if(m.kind==4) s->a=static_cast<unsigned char>((2+2*std::sin(time_*0.6f))*strength);
        if(m.kind==5) t->x=std::round(std::sin(time_*1.2f+m.phase));
    }
}
}
