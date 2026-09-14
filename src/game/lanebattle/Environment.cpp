#include "Environment.h"
#include "LaneBattle.h"
#include "Art.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

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
void animate(World& w,Entity e,float interval,int frame) {
    auto& s=*w.getComponent<Sprite>(e);s.srcW=32;s.srcH=32;s.srcX=frame*32;
    Animation a;a.frameCount=6;a.frameWidth=32;a.secondsPerFrame=interval;a.frame=frame;
    w.addComponent(e,a);
}
const char* effectNames[]={"CLOUDS","FOG","DUST / EMBERS","SMOKE","FLAMES","WATER","VEGETATION","LIGHTING"};
}
void EnvironmentSystem::controls(InputManager& input) {
    if(input.wasKeyPressed(SDL_SCANCODE_F4)) selectedEffect=AmbientEffect((static_cast<int>(selectedEffect)+1)%static_cast<int>(AmbientEffect::Count));
    auto& c=effects[static_cast<int>(selectedEffect)];
    if(input.wasKeyPressed(SDL_SCANCODE_F5)) c.enabled=!c.enabled;
    if(input.wasKeyPressed(SDL_SCANCODE_LEFTBRACKET)) c.intensity=std::max(0.0f,c.intensity-0.1f);
    if(input.wasKeyPressed(SDL_SCANCODE_RIGHTBRACKET)) c.intensity=std::min(1.0f,c.intensity+0.1f);
    if(input.wasKeyPressed(SDL_SCANCODE_COMMA)) c.speed=std::max(0.0f,c.speed-0.25f);
    if(input.wasKeyPressed(SDL_SCANCODE_PERIOD)) c.speed=std::min(3.0f,c.speed+0.25f);
    if(input.wasKeyPressed(SDL_SCANCODE_BACKSLASH)) cloudDirection=-cloudDirection;
}
std::string EnvironmentSystem::description() const {
    const auto& c=effects[static_cast<int>(selectedEffect)];char buffer[150];
    std::snprintf(buffer,sizeof(buffer),"F4 %s F5 %s  [ ] ALPHA %.1f  , . SPEED %.2f  BACKSLASH CLOUD DIRECTION",effectNames[static_cast<int>(selectedEffect)],c.enabled?"ON":"OFF",c.intensity,c.speed);
    return buffer;
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
    auto* fog=cache->load("assets/lanebattle/vector/mist-cycle.svg");
    auto* flame=cache->load("assets/lanebattle/vector/flame-cycle.svg");
    auto* reeds=cache->load("assets/lanebattle/vector/reeds-cycle.svg");
    auto* ripples=cache->load("assets/lanebattle/vector/ripples-cycle.svg");
    auto* fleck=cache->load("assets/lanebattle/vector/ash.svg");
    auto* ember=cache->load("assets/lanebattle/vector/ember.svg");
    auto* wisp=cache->load("assets/lanebattle/vector/wisp.svg");
    if(!landscape||!reference||!fog||!fleck||!ember||!wisp||!flame||!reeds||!ripples) return false;
#if SDL_VERSION_ATLEAST(2,0,12)
    SDL_SetTextureScaleMode(landscape,SDL_ScaleModeNearest);
    SDL_SetTextureScaleMode(reference,SDL_ScaleModeNearest);
#endif
    clear(w);current_=choice;
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
    // Clouds, fog banks and dust are POOLS, made here and spawned into at random
    // while the scene runs (see spawn). They start idle and invisible; the
    // pre-fill further down scatters a few so a scene never opens on an empty
    // sky, and the rest arrive over time, up to effects.txt's limits.
    for(int i=0;i<kCloudPool;++i) {
        auto e=add(w,owned_,-1000,0,240,80,-4,reference);
        auto& s=*w.getComponent<Sprite>(e);s.srcX=0;s.srcY=int(rh*0.14f);s.srcW=int(rw*0.27f);s.srcH=int(rh*0.16f);s.a=0;
        moving_.push_back({e,-1000,0,float(i),0});
        moving_.back().active=false;
    }
    for(int i=0;i<kFogPool;++i) {
        auto e=add(w,owned_,-1000,340,300,48,-2,fog);
        auto& s=*w.getComponent<Sprite>(e);s.a=0;
        if(choice==Environment::BloodMoon) {s.r=180;s.g=75;s.b=100;}
        if(choice==Environment::Ashen) {s.r=110;s.g=100;s.b=100;}
        moving_.push_back({e,-1000,340,float(i),1});
        moving_.back().active=false;
        animate(w,e,0.8f+float(i%6)*0.13f,i%6);
    }
    for(int i=0;i<kDustPool;++i) {
        bool fire=choice==Environment::Ashen,ghost=choice==Environment::Marsh;
        auto e=add(w,owned_,-1000,300,ghost?6:3,ghost?8:3,-1,fire?ember:ghost?wisp:fleck);
        auto& s=*w.getComponent<Sprite>(e);s.a=0;
        if(choice==Environment::Desert) {s.r=190;s.g=156;s.b=96;}
        moving_.push_back({e,-1000,300,float(i)*0.73f,2});
        moving_.back().active=false;
    }
    // Scene-specific low effects: reflection streaks, fissure smoke, powder,
    // dust devils, ruin debris, or supernatural background pulses.
    for(int i=0;i<12;++i) {
        auto e=add(w,owned_,float(i*87),400,choice==Environment::Marsh?28:2,2,-1);
        auto& s=*w.getComponent<Sprite>(e);s.r=110;s.g=125;s.b=137;s.a=35;
        moving_.push_back({e,float(i*87),400,float(i)*1.7f,3});
        if(choice==Environment::Ashen||choice==Environment::Marsh) {
            s.texture=choice==Environment::Marsh?ripples:fog;
            animate(w,e,0.35f+float(i%4)*0.11f,i%6);
            if(choice==Environment::Marsh) moving_.back().y=352+float(i%3)*5;
        }
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
    if(choice==Environment::Ashen) for(int i=0;i<5;++i) {
        auto e=add(w,owned_,float(90+i*177),394,20,24,-1,flame);
        animate(w,e,0.10f+float(i)*0.017f,i%6);
        moving_.push_back({e,float(90+i*177),394,float(i),6});
    }
    if(choice==Environment::Marsh||choice==Environment::Reference) for(int i=0;i<8;++i) {
        auto e=add(w,owned_,float(i*139-24),394,24,24,-1,reeds);
        animate(w,e,0.35f+float(i%3)*0.1f,i%6);
        moving_.push_back({e,float(i*139-24),394,float(i),7});
    }
    for(auto& m:moving_) {
        m.clock=m.phase;
        m.effect=m.kind==0?AmbientEffect::Clouds:m.kind==1?AmbientEffect::Fog:
            m.kind==4?AmbientEffect::Lighting:m.kind==6?AmbientEffect::Flames:m.kind==7?AmbientEffect::Vegetation:
            m.kind==3&&choice==Environment::Ashen?AmbientEffect::Smoke:
            m.kind==3&&choice==Environment::Marsh?AmbientEffect::Water:AmbientEffect::Dust;
        w.addComponent(m.id,AmbientElement{m.effect});
    }
    // A scene opens with some of its ambience already in the air — three in
    // five of each limit, scattered anywhere — rather than an empty sky that
    // fills up over the first half-minute. The rest arrive at random.
    auto& cameras=w.view<Camera>();
    camera_=cameras.empty()?0.0f:cameras.begin()->second.x;
    spawnDebt_[0]=spawnDebt_[1]=spawnDebt_[2]=0;
    static const char* groups[]={"CLOUDS","FOG","DUST"};
    const int pools[]={kCloudPool,kFogPool,kDustPool};
    for(int kind=0;kind<3;++kind) {
        const AmbientTuning* tuning=ambientTuning(groups[kind]);
        const int limit=std::min(pools[kind],tuning?tuning->limit:pools[kind]);
        int placed=0;
        for(auto& m:moving_) {
            if(m.kind!=kind||placed>=limit*3/5) continue;
            spawn(m,true);++placed;
        }
    }
    update(w,0);return true;
}

int EnvironmentSystem::activeCount(int kind) const {
    int count=0;
    for(const auto& m:moving_) if(m.kind==kind&&m.active) ++count;
    return count;
}

// Gives an idle cloud, fog bank or speck a size, a place and a speed.
//
// Size is the whole trick. Each one is born small, medium or large from its
// group's mix in effects.txt, and the size decides more than the picture: a
// small cloud is FAR, so it is fainter and drifts more slowly, and a large one
// is near and passes faster. Three sizes that moved alike would look like
// three stamps; three that move differently look like distance.
//
// `anywhere` is for the scene's opening scatter. Otherwise a cloud or fog bank
// enters at the edge its wind blows from — it drifts IN, rather than blinking
// into existence in the middle of the sky.
void EnvironmentSystem::spawn(Moving& m,bool anywhere) {
    static const char* groups[]={"CLOUDS","FOG","DUST"};
    const AmbientTuning* tuning=ambientTuning(groups[std::clamp(m.kind,0,2)]);
    const SizeClass size=pickSize(tuning?tuning->mix:SizeMix{},presentationBetween(0,1));
    const float depth=size==SizeClass::Small?0.6f:size==SizeClass::Large?1.4f:1.0f;
    m.scale=sizeScale(size)*presentationBetween(0.9f,1.1f);
    m.alpha=size==SizeClass::Small?0.65f:size==SizeClass::Large?1.0f:0.85f;
    m.active=true;m.age=0;m.fade=anywhere?1.0f:0.0f;m.life=0;
    m.phase=presentationBetween(0,6.2832f);m.clock=m.phase;
    if(m.kind==0) {
        const float base=tuning?tuning->size:220.0f;
        m.width=std::max(8,int(base*m.scale));m.height=std::max(4,m.width/3);
        m.velocity=4.0f*depth;
        m.y=presentationBetween(40,150);
        const float screenX=anywhere?presentationBetween(-float(m.width),960.0f)
                                   :(cloudDirection>0?-float(m.width)-30.0f:990.0f);
        m.x=screenX+camera_*0.04f;
    } else if(m.kind==1) {
        const float base=tuning?tuning->size:280.0f;
        m.width=std::max(16,int(base*m.scale));m.height=std::max(10,int(float(m.width)*0.16f));
        const float direction=presentationBetween(0,1)<0.5f?-1.0f:1.0f;
        m.velocity=direction*presentationBetween(3,6)*depth;
        // Low, and always clear of the ground line: fog behind the units, never
        // across the lane they fight on.
        m.y=presentationBetween(330,std::max(331.0f,410.0f-float(m.height)));
        m.x=anywhere?presentationBetween(-float(m.width),960.0f):(direction>0?-float(m.width)-20.0f:980.0f);
    } else {
        const bool ghost=current_==Environment::Marsh;
        const float base=tuning?tuning->size:3.0f;
        m.width=std::max(1,int(std::lround((ghost?6.0f:base)*m.scale)));
        m.height=std::max(1,int(std::lround((ghost?8.0f:base)*m.scale)));
        const float fall=current_==Environment::Ashen?-12.0f:current_==Environment::Frozen?12.0f:3.0f;
        m.x=presentationBetween(0,1000);
        // Specks that clearly rise or fall enter from the edge they come from;
        // the ones that barely move just appear, somewhere in the band.
        m.y=anywhere||std::fabs(fall)<8?presentationBetween(280,400):(fall<0?404.0f:276.0f);
        m.life=presentationBetween(4,10);
    }
}
void EnvironmentSystem::clear(World& w) {
    for(auto e:owned_) w.destroyLater(e);
    owned_.clear();moving_.clear();
}
void EnvironmentSystem::update(World& w,float dt) {
    if(!std::isfinite(dt)||dt<0) return;
    dt=std::min(dt,0.1f);
    float camera=0;
    auto& cameras=w.view<Camera>();
    if(!cameras.empty()) camera=cameras.begin()->second.x;
    camera_=camera;

    // New clouds, fog banks and specks, at random, while each group is under
    // its limit. Paced by the group's own motion time, so a group whose speed
    // is zero (or which is switched off) freezes completely: nothing moves and
    // nothing new appears. The debt is capped so a long wait at the limit
    // does not turn into a burst the moment there is room.
    {
        static const char* groups[]={"CLOUDS","FOG","DUST"};
        const AmbientEffect controlOf[]={AmbientEffect::Clouds,AmbientEffect::Fog,AmbientEffect::Dust};
        const int pools[]={kCloudPool,kFogPool,kDustPool};
        for(int kind=0;kind<3;++kind) {
            const AmbientTuning* tuning=ambientTuning(groups[kind]);
            const auto& control=effects[static_cast<int>(controlOf[kind])];
            if(!tuning||!control.enabled) continue;
            const float motionDt=dt*std::clamp(std::isfinite(control.speed)?control.speed:0.0f,0.0f,3.0f);
            if(motionDt<=0) continue;
            const int limit=std::min(pools[kind],tuning->limit);
            int live=activeCount(kind);
            spawnDebt_[kind]=std::min(spawnDebt_[kind]+tuning->spawn*motionDt,1.0f+tuning->spawn*0.1f);
            while(spawnDebt_[kind]>=1.0f) {
                spawnDebt_[kind]-=1.0f;
                if(live>=limit) {spawnDebt_[kind]=0;break;}
                for(auto& m:moving_) if(m.kind==kind&&!m.active) {spawn(m,false);++live;break;}
            }
        }
    }

    for(auto& m:moving_) {
        auto* t=w.getComponent<Transform>(m.id);auto* s=w.getComponent<Sprite>(m.id);if(!s||!t) continue;
        auto& control=effects[static_cast<int>(m.effect)];
        control.speed=std::isfinite(control.speed)?std::clamp(control.speed,0.0f,3.0f):0;
        control.intensity=std::isfinite(control.intensity)?std::clamp(control.intensity,0.0f,1.0f):0;
        const float motionDt=control.enabled?dt*control.speed:0;
        m.clock+=motionDt;
        const float time_=m.clock; // Every element owns its cycle phase.
        const float strength=strong?1.7f:1.0f;
        if(auto* a=w.getComponent<Animation>(m.id)) {
            a->playing=control.enabled&&control.speed>0;
            const float base=m.kind==6?0.12f:m.kind==1?0.9f:0.4f;
            a->secondsPerFrame=(base+std::fmod(m.phase,0.17f))/std::max(0.01f,control.speed);
        }
        // An idle member of a spawned group is simply invisible until spawn
        // gives it somewhere to be.
        if(m.kind<=2&&!m.active) {s->a=0;continue;}
        if(m.kind==0) {
            m.x+=motionDt*m.velocity*cloudDirection*strength;
            const float screenX=m.x-camera*0.04f;
            // Past the downwind edge: done, and back in the pool.
            if((cloudDirection>0&&screenX>1000)||(cloudDirection<0&&screenX+float(m.width)<-40)) {m.active=false;s->a=0;continue;}
            t->x=std::round(screenX);
            s->width=m.width+int(std::round(std::sin(time_*0.25f)*3));
            s->height=m.height+int(std::round(std::sin(time_*0.19f)*1));
            t->y=m.y+std::round(std::sin(time_*0.12f));
            m.fade=std::min(1.0f,m.fade+motionDt*0.5f);
            s->a=static_cast<unsigned char>(110*m.alpha*m.fade*std::clamp(std::min((t->x+float(m.width))/80,(1040-t->x)/80),0.0f,1.0f));
        }
        if(m.kind==1) {
            m.x+=motionDt*m.velocity*strength;
            if((m.velocity>0&&m.x>1000)||(m.velocity<0&&m.x+float(m.width)<-40)) {m.active=false;s->a=0;continue;}
            t->x=std::round(m.x);t->y=std::round(m.y);
            s->width=m.width;s->height=m.height;
            m.fade=std::min(1.0f,m.fade+motionDt*0.4f);
            s->a=static_cast<unsigned char>((current_==Environment::Desert?14:38)*strength*m.alpha*m.fade*(0.7f+0.3f*std::sin(time_*0.35f)));
        }
        if(m.kind==2) {
            m.age+=motionDt;
            m.x+=motionDt*8*strength;float fall=current_==Environment::Ashen?-12.0f:current_==Environment::Frozen?12.0f:3.0f;
            m.y+=motionDt*fall*strength;
            // A speck lives a few seconds, or until it leaves the band — then it
            // fades, and the pool gets it back for somewhere else.
            if(m.age>=m.life||m.y<272||m.y>408) {m.active=false;s->a=0;continue;}
            t->x=std::round(wrap(m.x+20,1000)-20+std::sin(time_*0.5f+m.phase)*5);
            t->y=std::round(m.y);
            s->width=m.width;s->height=m.height;
            const float fadeIn=std::min(1.0f,m.age/0.5f),fadeOut=std::min(1.0f,(m.life-m.age)/1.0f);
            s->a=static_cast<unsigned char>((20+25*(1+std::sin(time_+m.phase)))*strength*m.alpha*std::min(fadeIn,fadeOut));
        }
        if(m.kind==3) {
            float cycle=wrap(time_*0.15f+m.phase,1);
            float fade=std::sin(cycle*3.14159265f);
            t->x=std::round(m.x);t->y=m.y;s->a=static_cast<unsigned char>(fade*35*strength);
            if(current_==Environment::Marsh) {t->x=std::round(m.x+std::sin(time_+m.phase)*3);s->width=60;s->height=12;s->r=180;s->g=220;s->b=220;s->a=static_cast<unsigned char>(80*fade);}
            else if(current_==Environment::Ashen) {t->y=std::round(412-cycle*50);s->width=4+int(cycle*12);s->height=4+int(cycle*10);s->r=85;s->g=76;s->b=78;}
            else if(current_==Environment::Desert) {t->x=std::round(m.x+std::sin(time_*2+m.phase)*8);t->y=std::round(414-cycle*55);s->width=3+int(cycle*12);s->r=180;s->g=147;s->b=94;}
            else if(current_==Environment::BloodMoon) {s->width=70;s->height=2;s->r=130;s->g=38;s->b=60;s->a=static_cast<unsigned char>(fade*16);}
            else if(current_==Environment::Frozen) {t->x=std::round(wrap(m.x+time_*12,1000)-20);s->width=32;s->height=2;s->r=150;s->g=172;s->b=184;}
            else {t->y=std::round(240+cycle*174);s->width=2;s->height=3;s->a=cycle<0.65f?0:static_cast<unsigned char>(fade*80);}
        }
        if(m.kind==4) s->a=static_cast<unsigned char>((2+2*std::sin(time_*0.6f))*strength);
        if(m.kind==5) t->x=std::round(std::sin(time_*1.2f+m.phase));
        if(m.kind==6||m.kind==7) s->a=m.kind==6?150:160;
        s->a=control.enabled?static_cast<unsigned char>(s->a*control.intensity):0;
    }
}
}
