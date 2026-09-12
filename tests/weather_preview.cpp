#define SDL_MAIN_HANDLED
#include "Weather.h"
#include "LaneBattle.h"
#include "engine/Engine.h"
#include <SDL_image.h>
#include <filesystem>
#include <iostream>
#include <limits>

using namespace engine;
using namespace lanebattle;
int main(int argc, char** argv) {
    const bool verify=argc>1 && std::string(argv[1])=="--verify";
    if(verify) { SDL_setenv("SDL_VIDEODRIVER","dummy",1); SDL_setenv("SDL_AUDIODRIVER","dummy",1); }
    Engine engine("Lane Battle - Weather Preview",960,540);
    World world;
    std::vector<Entity> scenery;
    if(!buildVectorScenery(world,&engine.textures(),scenery)) { std::cerr<<"SVG scenery failed to load\n"; return 1; }
    auto rect=[&](float x,float y,int w,int h,int layer,unsigned char r,unsigned char g,unsigned char b) {
        Entity e=world.createEntity(); world.addComponent(e,Transform{x,y});
        Sprite s; s.width=w; s.height=h; s.layer=layer; s.screenSpace=true; s.r=r;s.g=g;s.b=b;
        world.addComponent(e,s); return e;
    };
    rect(0,420,960,120,0,38,42,52);
    rect(0,460,960,80,kHudLayer,20,24,34);
    rect(30,290,70,130,0,70,120,165); rect(860,290,70,130,0,165,80,65);
    // Real game units provide a gameplay readability reference.
    setTextureCache(&engine.textures());
    for(int i=0;i<6;++i) {
        Entity e=spawnUnit(world,i<3,i%3);
        world.getComponent<Transform>(e)->x=300.0f+float(i)*50;
    }
    auto label=[&](const char* value,int y) {
        Entity e=world.createEntity(); world.addComponent(e,Transform{16,float(y)});
        Text t; t.value=value;t.scale=1;t.screenSpace=true;t.layer=kHudLayer;world.addComponent(e,t);return e;
    };
    Entity status=label("",472);
    label("F6 PRESET   F7 INTENSITY   F8 SPEED   F9 WIND   F10 OPACITY   F11 LIGHTNING",492);
    label("ESC CLOSE - WEATHER DOES NOT CHANGE COMBAT - FLASHES DEFAULT TO REDUCED",512);
    WeatherSystem weather; weather.start(world,&engine.textures());
    animateUnits(world,0);
    if(!verify) {
        engine.run(world,[&](World& w,InputManager& input,float dt){weather.controls(input);weather.update(w,dt);w.getComponent<Text>(status)->value=weather.description();});
        setTextureCache(nullptr); return 0;
    }
    int failures=0;
    auto check=[&](bool ok,const char* why){if(!ok){++failures;std::cerr<<why<<'\n';}};
    const char* assets[]={"rain","blood","snow","hail","fog","ash","ember","wisp","lightning"};
    for(auto name:assets) check(engine.textures().load(std::string("assets/lanebattle/vector/")+name+".svg")!=nullptr,"SVG effect failed to decode");
    auto* cached=engine.textures().load("assets/lanebattle/vector/snow.svg");
    check(cached==engine.textures().load("assets/lanebattle/vector/snow.svg"),"SVG is not cached");
    std::filesystem::path output=argc>2?argv[2]:"weather_shots";
    std::filesystem::create_directories(output);
    const auto entityCount=world.entities().size();
    for(int preset=0;preset<10;++preset) {
        weather.settings.preset=WeatherPreset(preset); weather.settings.intensity=1;weather.settings.opacity=1;
        for(int tick=0;tick<480;++tick) weather.update(world,1.0f/60);
        world.getComponent<Text>(status)->value=weather.description();
        engine.drawWorld(world);
        int w=0,h=0;auto pixels=engine.captureFrame(w,h);
        check(w==960&&h==540&&!pixels.empty(),"Frame capture failed");
        if(pixels.empty()) continue;
        check(pixels[450*960+500]==0xff262a34u,"Weather obscures lower lane");
        check(pixels[465*960+500]==0xff141822u,"Weather obscures HUD");
        SDL_Surface* surface=SDL_CreateRGBSurfaceWithFormatFrom(pixels.data(),w,h,32,w*4,SDL_PIXELFORMAT_ARGB8888);
        check(surface!=nullptr,"Screenshot surface failed");
        if(surface) {check(IMG_SavePNG(surface,(output/(std::to_string(preset)+".png")).string().c_str())==0,"Screenshot write failed");SDL_FreeSurface(surface);}
        check(world.entities().size()==entityCount,"Preset switching leaked entities");
    }
    weather.settings.intensity=std::numeric_limits<float>::quiet_NaN();weather.settings.wind=100;weather.update(world,0);
    check(weather.settings.intensity==0&&weather.settings.wind==1,"Invalid settings not clamped");
    weather.settings.preset=WeatherPreset::Rain;weather.settings.speed=0;weather.update(world,0);
    const auto rainTexture=engine.textures().load("assets/lanebattle/vector/rain.svg");
    Entity sample=0;
    for(auto& [e,s]:world.view<Sprite>()) if(s.texture==rainTexture) {sample=e;break;}
    check(sample!=0,"No rain particle available");
    if(sample) {
        const Transform before=*world.getComponent<Transform>(sample);
        weather.update(world,0.1f);
        const Transform frozen=*world.getComponent<Transform>(sample);
        check(before.x==frozen.x&&before.y==frozen.y,"Zero speed does not freeze particles");
        weather.settings.wind=-1;weather.update(world,0);
        check(world.getComponent<Transform>(sample)->x==before.x,"Changing wind teleports particles");
    }
    weather.settings.speed=1;
    weather.settings.preset=WeatherPreset::Thunderstorm;weather.settings.intensity=1;weather.settings.lightning=LightningMode::Off;
    for(int tick=0;tick<600;++tick) weather.update(world,1.0f/60);
    // Off mode hides both dedicated flash entities, including after a mode change.
    for(auto& [e,s]:world.view<Sprite>()) if(s.width==960&&s.height==220) check(s.a==0,"Disabled lightning still flashes");
    weather.settings.lightning=LightningMode::Normal;
    bool sawFlash=false;
    int flashFrames=0;
    for(int tick=0;tick<660;++tick) {
        weather.update(world,1.0f/60);
        for(auto& [e,s]:world.view<Sprite>()) if(s.width==960&&s.height==220 && s.a>0) {sawFlash=true;++flashFrames;check(s.a<=24,"Flash exceeds opacity cap");}
    }
    check(sawFlash&&flashFrames<=14,"Lightning is absent or too persistent");
    weather.settings.lightning=LightningMode::Off;weather.update(world,0);
    for(auto& [e,s]:world.view<Sprite>()) if(s.width==960&&s.height==220) check(s.a==0,"Flash off did not take effect immediately");
    weather.clear(world);world.flushDestroyed();
    check(world.entities().size()==entityCount-147,"Weather teardown leaked entities");
    setTextureCache(nullptr);
    std::cout<<"Weather verification: "<<failures<<" failures; screenshots in "<<output<<'\n';
    return failures?1:0;
}
