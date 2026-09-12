#define SDL_MAIN_HANDLED
#include "Weather.h"
#include "Environment.h"
#include "LaneBattle.h"
#include "engine/Engine.h"
#include <SDL_image.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>

using namespace engine;
using namespace lanebattle;

int main(int argc, char** argv) {
    const bool verify=argc>1 && std::string(argv[1])=="--verify";
    if(verify) { SDL_setenv("SDL_VIDEODRIVER","dummy",1); SDL_setenv("SDL_AUDIODRIVER","dummy",1); }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,"0");
    Engine engine("Lane Battle - Environment and Weather Preview",960,540);
    World world;
    EnvironmentSystem environment;
    if(!environment.select(world,&engine.textures(),Environment::Reference)) return 1;
    auto cameraEntity=world.createEntity();world.addComponent(cameraEntity,Camera{});
    auto rect=[&](float x,float y,int w,int h,int layer,unsigned char r,unsigned char g,unsigned char b) {
        Entity e=world.createEntity(); world.addComponent(e,Transform{x,y});
        Sprite s; s.width=w; s.height=h; s.layer=layer; s.screenSpace=true; s.r=r;s.g=g;s.b=b;
        world.addComponent(e,s); return e;
    };
    rect(0,420,960,120,0,38,42,52);
    rect(0,460,960,80,kHudLayer,20,24,34);
    rect(30,290,70,130,0,70,120,165); rect(860,290,70,130,0,165,80,65);
    setTextureCache(&engine.textures());
    for(int i=0;i<6;++i) {
        Entity e=spawnUnit(world,i<3,i%3);
        world.getComponent<Transform>(e)->x=300.0f+float(i)*50;
    }
    animateUnits(world,0);
    auto label=[&](const char* value,int y) {
        Entity e=world.createEntity(); world.addComponent(e,Transform{16,float(y)});
        Text t; t.value=value;t.scale=1;t.screenSpace=true;t.layer=kHudLayer;world.addComponent(e,t);return e;
    };
    Entity title=label("",466),status=label("",482);
    label("F2 SCENE   F3 CALM/STRONG   F6 WEATHER   F7 DENSITY   F8 SPEED   F9 WIND",500);
    label("F10 OPACITY   F11 FLASHES   ARROWS CAMERA   ESC CLOSE",516);
    WeatherSystem weather; weather.start(world,&engine.textures());
    world.flushDestroyed();
    auto refresh=[&](){
        world.getComponent<Text>(title)->value=std::string(environmentName(environment.current()))+(environment.strong?" - STRONG":" - CALM");
        world.getComponent<Text>(status)->value=weather.description();
    };
    if(!verify) {
        engine.run(world,[&](World& w,InputManager& input,float dt){
            if(input.wasKeyPressed(SDL_SCANCODE_F2)) {
                auto next=Environment((static_cast<int>(environment.current())+1)%static_cast<int>(Environment::Count));
                if(environment.select(w,&engine.textures(),next)&&!compatibleWeather(next,weather.settings.preset))
                    weather.settings.preset=WeatherPreset::Clear;
            }
            if(input.wasKeyPressed(SDL_SCANCODE_F3)) {
                environment.strong=!environment.strong;
                weather.settings.preset=environment.strong?strongWeather(environment.current()):WeatherPreset::Clear;
            }
            const auto previous=weather.settings.preset;
            weather.controls(input);
            if(input.wasKeyPressed(SDL_SCANCODE_F6)) weather.settings.preset=nextCompatibleWeather(environment.current(),previous);
            auto& camera=*w.getComponent<Camera>(cameraEntity);
            camera.x=std::clamp(camera.x+dt*300*(input.isKeyDown(SDL_SCANCODE_RIGHT)-input.isKeyDown(SDL_SCANCODE_LEFT)),0.0f,kCameraMaxX);
            environment.update(w,dt);weather.update(w,dt);refresh();
        });
        setTextureCache(nullptr); return 0;
    }
    int failures=0,combinations=0;
    auto check=[&](bool ok,const char* why){if(!ok){++failures;std::cerr<<why<<'\n';}};
    const char* requiredEffects[]={"rain.svg","blood.svg","snow.svg","hail.svg","fog.svg","ash.svg","ember.svg","wisp.svg","lightning-branch.svg"};
    for(auto* file:requiredEffects) check(engine.textures().load(std::string("assets/lanebattle/vector/")+file)!=nullptr,"Weather texture failed to decode");
    std::filesystem::path output=argc>2?argv[2]:"environment_shots";
    std::filesystem::create_directories(output);
    std::ofstream manifest(output/"combinations.json");manifest<<"[\n";
    std::map<Entity,Transform> unitPositions;
    for(auto& [e,u]:world.view<Unit>()) {(void)u;unitPositions[e]=*world.getComponent<Transform>(e);}
    const auto nonEnvironmentCount=world.entities().size()-environment.entityCount();
    for(int scene=0;scene<static_cast<int>(Environment::Count);++scene) {
        auto choice=Environment(scene);
        check(environment.select(world,&engine.textures(),choice),"Environment PNG failed to load");
        world.flushDestroyed();
        check(world.entities().size()==nonEnvironmentCount+environment.entityCount(),"Scene switching leaked entities");
        for(int preset=0;preset<static_cast<int>(WeatherPreset::Count);++preset) {
            if(!compatibleWeather(choice,WeatherPreset(preset))) continue;
            for(int state=0;state<2;++state) {
                environment.strong=state!=0;
                auto loop=WeatherPreset::Clear;
                for(int step=0;step<static_cast<int>(WeatherPreset::Count);++step) {
                    loop=nextCompatibleWeather(choice,loop);
                    check(compatibleWeather(choice,loop),"Preview cycles to incompatible weather");
                }
                weather.settings.preset=WeatherPreset(preset);weather.settings.intensity=1;weather.settings.opacity=1;
                for(int tick=0;tick<90;++tick) {environment.update(world,0.1f);weather.update(world,0.1f);}
                refresh();
                for(int view=0;view<3;++view) {
                    world.getComponent<Camera>(cameraEntity)->x=float(view)*kCameraMaxX/2;
                    environment.update(world,0);
                    engine.drawWorld(world);
                    int w=0,h=0;auto pixels=engine.captureFrame(w,h);
                    check(w==960&&h==540&&!pixels.empty(),"Frame capture failed");
                    if(pixels.empty()) continue;
                    check(pixels[450*960+500]==0xff262a34u,"Atmosphere obscures lower lane");
                    check(pixels[459*960+500]==0xff262a34u,"Atmosphere crosses ground boundary");
                    check(pixels[461*960+500]==0xff141822u,"Atmosphere obscures HUD");
                    if(view==0) {
                        std::string filename=std::to_string(scene)+"-"+std::to_string(preset)+"-"+std::to_string(state)+".png";
                        SDL_Surface* surface=SDL_CreateRGBSurfaceWithFormatFrom(pixels.data(),w,h,32,w*4,SDL_PIXELFORMAT_ARGB8888);
                        check(surface!=nullptr,"Screenshot surface failed");
                        if(surface) {check(IMG_SavePNG(surface,(output/filename).string().c_str())==0,"Screenshot write failed");SDL_FreeSurface(surface);}
                        if(combinations) manifest<<",\n";
                        manifest<<"{\"scene\":\""<<environmentName(choice)<<"\",\"weather\":\""<<weatherName(WeatherPreset(preset))<<"\",\"state\":\""<<(state?"strong":"calm")<<"\",\"image\":\""<<filename<<"\"}";
                        ++combinations;
                    }
                }
                for(auto& [e,t]:unitPositions) {
                    auto* now=world.getComponent<Transform>(e);
                    check(now&&now->x==t.x&&now->y==t.y,"Weather moved a gameplay unit");
                }
            }
        }
    }
    manifest<<"\n]\n";manifest.close();
    weather.settings.intensity=std::numeric_limits<float>::quiet_NaN();weather.settings.wind=100;weather.update(world,0);
    check(weather.settings.intensity==0&&weather.settings.wind==1,"Invalid settings not clamped");
    weather.settings.preset=WeatherPreset::Rain;weather.settings.speed=0;weather.update(world,0);
    const auto rainTexture=engine.textures().load("assets/lanebattle/vector/rain.svg");
    Entity sample=0;
    for(auto& [e,s]:world.view<Sprite>()) if(s.texture==rainTexture) {sample=e;break;}
    check(sample!=0,"No rain particle available");
    if(sample) {
        const auto before=*world.getComponent<Transform>(sample);
        weather.update(world,0.1f);
        const auto frozen=*world.getComponent<Transform>(sample);
        check(before.x==frozen.x&&before.y==frozen.y,"Zero speed does not freeze particles");
        weather.settings.wind=-1;weather.update(world,0);
        check(world.getComponent<Transform>(sample)->x==before.x,"Changing wind teleports particles");
    }
    weather.settings.speed=1;weather.settings.intensity=1;weather.settings.opacity=1;
    weather.settings.preset=WeatherPreset::Thunderstorm;weather.settings.lightning=LightningMode::Normal;
    bool sawFlash=false;int flashFrames=0;
    for(int tick=0;tick<660;++tick) {
        weather.update(world,1.0f/60);
        for(auto& [e,s]:world.view<Sprite>()) if(s.width==960&&s.height==220&&s.a>0) {
            sawFlash=true;++flashFrames;check(s.a<=24,"Flash exceeds opacity cap");
        }
    }
    check(sawFlash&&flashFrames<=14,"Lightning is absent or too persistent");
    weather.settings.lightning=LightningMode::Off;weather.update(world,0);
    for(auto& [e,s]:world.view<Sprite>()) if(s.width==960&&s.height==220) check(s.a==0,"Disabled lightning remains visible");
    const auto count=world.entities().size();
    weather.clear(world);world.flushDestroyed();
    check(world.entities().size()==count-171,"Weather teardown leaked entities");
    const auto remaining=world.entities().size()-environment.entityCount();
    environment.clear(world);world.flushDestroyed();
    check(world.entities().size()==remaining,"Environment teardown leaked entities");
    setTextureCache(nullptr);
    std::cout<<"Environment verification: "<<failures<<" failures; "<<combinations<<" combinations, three camera positions each. Screenshots in "<<output<<'\n';
    return failures?1:0;
}
