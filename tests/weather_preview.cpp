#define SDL_MAIN_HANDLED
#include "Weather.h"
#include "Environment.h"
#include "LaneBattle.h"
#include "Art.h"
#include "engine/Engine.h"
#include "engine/Systems.h"
#include "Harness.h"
#include <SDL_image.h>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <utility>
#include <vector>

using namespace engine;
using namespace lanebattle;

int main(int argc, char** argv) {
    const bool verify=argc>1 && std::string(argv[1])=="--verify";
    if(verify) { SDL_setenv("SDL_VIDEODRIVER","dummy",1); SDL_setenv("SDL_AUDIODRIVER","dummy",1); }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,"0");
    Engine engine("Lane Battle - Environment and Weather Preview",960,540);
    // The art and the effect limits, as the game loads them — and, when
    // verifying, fixed dice, so random spawning lands in the same places every
    // run and a failure can be reproduced rather than chased.
    loadBalance(kBalancePath);  // the shipped roster, which is where unit sheets are named
    loadPresentation();
    if(verify) seedPresentation(2024);
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
    Entity ambientLabel=label("",440);
    label("F2 SCENE   F3 CALM/STRONG   F6 WEATHER   F7 DENSITY   F8 SPEED   F9 WIND",500);
    label("F10 OPACITY F11 FLASHES F12 IMPACTS  F4 EFFECT F5 TOGGLE  ARROWS CAMERA",516);
    WeatherSystem weather; weather.start(world,&engine.textures());
    world.flushDestroyed();
    auto refresh=[&](){
        world.getComponent<Text>(title)->value=std::string(environmentName(environment.current()))+(environment.strong?" - STRONG":" - CALM");
        world.getComponent<Text>(status)->value=weather.description();
        world.getComponent<Text>(ambientLabel)->value=environment.description();
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
            environment.controls(input);environment.update(w,dt);weather.update(w,dt);refresh();
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
                for(int tick=0;tick<90;++tick) {AnimationSystem(world,0.1f);environment.update(world,0.1f);weather.update(world,0.1f);}
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
                        manifest<<"{\"scene\":\""<<environmentName(choice)<<"\",\"weather\":\""<<weatherName(WeatherPreset(preset))<<"\",\"state\":\""<<(state?"strong":"calm")<<"\",\"particles\":"<<weather.activeParticles()<<",\"limit\":"<<weather.particleLimit()<<",\"clouds\":"<<environment.activeCount(0)<<",\"image\":\""<<filename<<"\"}";
                        ++combinations;
                    }
                }
                for(auto& [e,t]:unitPositions) {
                    auto* now=world.getComponent<Transform>(e);
                    check(now&&now->x==t.x&&now->y==t.y,"Weather moved a gameplay unit");
                }
                // The limits are HARD: nine seconds of full-density weather
                // later, nothing has more on screen than effects.txt allows.
                check(weather.activeParticles()<=weather.particleLimit(),"Weather exceeds its limit");
                if(weather.particleLimit()>0) check(weather.activeParticles()>0,"Weather with room to spawn shows nothing");
                {
                    const char* groups[]={"CLOUDS","FOG","DUST"};
                    const int pools[]={EnvironmentSystem::kCloudPool,EnvironmentSystem::kFogPool,EnvironmentSystem::kDustPool};
                    for(int kind=0;kind<3;++kind) {
                        const auto* tuning=ambientTuning(groups[kind]);
                        const int limit=std::min(pools[kind],tuning?tuning->limit:pools[kind]);
                        check(environment.activeCount(kind)<=limit,"Ambience exceeds its limit");
                    }
                }
            }
        }
    }
    manifest<<"\n]\n";manifest.close();
    weather.settings.intensity=std::numeric_limits<float>::quiet_NaN();weather.settings.wind=100;weather.update(world,0);
    check(weather.settings.intensity==0&&weather.settings.wind==1,"Invalid settings not clamped");
    // Rain at full density long enough for drops to exist, THEN frozen. Particles
    // spawn now rather than all existing at once, so at density zero — which
    // the clamping check above just left behind — there is correctly no rain to
    // sample, and a freeze test of nothing would pass for the wrong reason.
    weather.settings.intensity=1;weather.settings.wind=0.25f;weather.settings.speed=1;
    weather.settings.preset=WeatherPreset::Rain;
    for(int tick=0;tick<30;++tick) weather.update(world,1.0f/60);
    weather.settings.speed=0;weather.update(world,0);
    const auto rainTexture=engine.textures().load("assets/lanebattle/vector/rain.svg");
    Entity sample=0;
    for(auto& [e,s]:world.view<Sprite>()) if(s.texture==rainTexture&&s.a>0) {sample=e;break;}
    check(sample!=0,"No rain particle available");
    if(sample) {
        const auto before=*world.getComponent<Transform>(sample);
        weather.update(world,0.1f);
        const auto frozen=*world.getComponent<Transform>(sample);
        check(before.x==frozen.x&&before.y==frozen.y,"Zero speed does not freeze particles");
        weather.settings.wind=-1;weather.update(world,0);
        check(world.getComponent<Transform>(sample)->x==before.x,"Changing wind teleports particles");
    }
    // RANDOM, and repeatable. The weather used to be a fixed scatter, placed by
    // a formula on each particle's index; it now spawns from the presentation's
    // own dice. Two seeds must give two skies — or the "random" is a formula
    // again — and the same seed must give the same sky, or no screenshot can be
    // compared with the last one. And it must come in more than one size.
    auto rainSnapshot=[&](unsigned seed){
        seedPresentation(seed);
        weather.settings.preset=WeatherPreset::Clear;weather.settings.speed=1;weather.settings.intensity=1;
        weather.update(world,0);
        weather.settings.preset=WeatherPreset::Rain;weather.update(world,0);
        for(int tick=0;tick<60;++tick) weather.update(world,1.0f/60);
        std::vector<std::pair<int,int>> drops;std::set<int> heights;
        for(auto& [e,s]:world.view<Sprite>()) if(s.texture==rainTexture&&s.a>0) {
            const auto* t=world.getComponent<Transform>(e);
            drops.push_back({int(t->x),int(t->y)});heights.insert(s.height);
        }
        std::sort(drops.begin(),drops.end());
        return std::make_pair(drops,heights.size());
    };
    const auto first=rainSnapshot(7),again=rainSnapshot(7),other=rainSnapshot(8);
    check(!first.first.empty(),"Rain spawned nothing to compare");
    check(first.first==again.first,"The same seed does not give the same weather");
    check(first.first!=other.first,"Two seeds give identical weather: it is not random");
    check(first.second>=2,"Rain does not come in more than one size");
    seedPresentation(2024);

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
    // Test each independently controlled effect, including frame advancement.
    for(auto scene:{Environment::Reference,Environment::Ashen,Environment::Marsh}) {
        check(environment.select(world,&engine.textures(),scene),"Animated scene failed to load");world.flushDestroyed();
        for(int effect=0;effect<static_cast<int>(AmbientEffect::Count);++effect) {
            auto& control=environment.effects[effect];control.enabled=false;environment.update(world,0);
            for(auto& [e,tag]:world.view<AmbientElement>()) if(static_cast<int>(tag.effect)==effect)
                check(world.getComponent<Sprite>(e)->a==0,"Effect toggle leaves visible elements");
            control.enabled=true;control.speed=0;environment.update(world,0);
            std::map<Entity,Transform> stopped;
            for(auto& [e,tag]:world.view<AmbientElement>()) if(static_cast<int>(tag.effect)==effect) stopped[e]=*world.getComponent<Transform>(e);
            for(int step=0;step<20;++step) {AnimationSystem(world,0.05f);environment.update(world,0.05f);}
            for(auto& [e,t]:stopped) {const auto& now=*world.getComponent<Transform>(e);check(t.x==now.x&&t.y==now.y,"Zero effect speed does not freeze motion");}
            control.speed=1;
        }
    }
    // Enter the shipped PlayScene, not just the standalone preview.
    std::size_t battleEntities=0;  // reported at the end: what a real battle holds
    {
        World battle;SceneStack scenes;
        // A WARDEN, so the hero summoned below has a path whose art it must wear.
        campaignOf(battle).heroPath=static_cast<int>(HeroPath::Warden);
        scenes.push(makePlayScene());harness::Harness driver(battle,scenes);
        driver.step(2);
        auto* session=findSession(battle);
        check(session!=nullptr,"Actual battle did not start");
        if(session) {for(float& cooldown:session->enemySpawnCooldowns) cooldown=1e9f;session->enemyCannonCooldown=1e9f;}
        for(int scene=0;scene<4;++scene) {driver.tap(SDL_SCANCODE_F2);driver.step(2);}
        std::map<Entity,float> clouds;std::map<Entity,int> frames;
        for(auto& [e,tag]:battle.view<AmbientElement>()) {
            if(tag.effect==AmbientEffect::Clouds) clouds[e]=battle.getComponent<Transform>(e)->x;
            if(auto* animation=battle.getComponent<Animation>(e)) frames[e]=animation->frame;
        }
        // A POOL of clouds now, spawned into at random — so "how many cloud
        // entities" is the pool's size, and what matters is that some of them
        // are in the sky and none beyond the limit.
        check(!clouds.empty()&&!frames.empty(),"Actual game is missing independent animated entities");
        check(clouds.size()==std::size_t(EnvironmentSystem::kCloudPool),"The cloud pool is not the size it says");

        // The units wear their art in the real battle: an animated figure with
        // a texture follows each one, and the footprint it stands on is no
        // longer drawn.
        {
            const Entity mine=spawnUnit(battle,true,1),theirs=spawnUnit(battle,false,1);
            for(Entity u:{mine,theirs}) {
                const Unit* unit=battle.getComponent<Unit>(u);
                const bool art=unit&&battle.hasComponent<ArtFigure>(unit->figure);
                check(art,"A unit in the real battle has no art figure");
                if(art) {
                    check(battle.getComponent<Sprite>(unit->figure)->texture!=nullptr,"An art figure has no texture");
                    check(battle.getComponent<Sprite>(u)->a==0,"A unit with art still draws its block");
                }
            }
            check(battle.getComponent<Sprite>(battle.getComponent<Unit>(theirs)->figure)->flipX,
                  "The enemy's art does not face left");
            // They MOVE like it, too. The sheets keep the body level in every
            // frame, so a unit whose art is only flipped through frames glides;
            // the figure has to rise and fall with its stride, and a griffin
            // with its wingbeat. Watched for a second of real battle time: the
            // figure's height above its feet must take more than one value.
            {
                const Entity walker=spawnUnit(battle,true,1),flyer=spawnUnit(battle,true,3);
                std::set<int> walkerHeights,flyerHeights;
                for(int tick=0;tick<60;++tick) {
                    driver.step();
                    for(auto [unitEntity,seen]:{std::make_pair(walker,&walkerHeights),std::make_pair(flyer,&flyerHeights)}) {
                        const Unit* unit=battle.getComponent<Unit>(unitEntity);
                        if(!unit) continue;
                        const auto* figure=battle.getComponent<Transform>(unit->figure);
                        const auto* body=battle.getComponent<Transform>(unitEntity);
                        if(figure&&body) seen->insert(int(body->y-figure->y));
                    }
                }
                check(walkerHeights.size()>=2,"A walking unit's art does not bob: it glides");
                check(flyerHeights.size()>=3,"A flyer's art does not rise and fall with its wings");

                // And its wings BEAT rather than snap. The griffin's flight row
                // is a sequence, measured by art_probe; looped, the jump from its
                // last drawing to its first was the biggest change in the row and
                // the wing snapped back once a beat. Played back and forth, every
                // change of drawing is to a neighbour, and it turns at the end.
                int jumps=0,turns=0,previous=-1,previousPose=-1;
                float slowestHold=1.0f;  // the shortest a flight drawing is held
                for(int tick=0;tick<150;++tick) {
                    driver.step();
                    const Unit* unit=battle.getComponent<Unit>(flyer);
                    if(!unit) break;
                    const auto* art=battle.getComponent<ArtFigure>(unit->figure);
                    const auto* animation=battle.getComponent<Animation>(unit->figure);
                    if(!art||!animation) break;
                    if(art->pose==int(Pose::Move)) slowestHold=std::min(slowestHold,animation->secondsPerFrame);
                    if(art->pose==int(Pose::Move)&&previousPose==int(Pose::Move)&&animation->frame!=previous) {
                        if(std::abs(animation->frame-previous)>1) ++jumps;
                        if(previous==animation->frameCount-1) ++turns;
                    }
                    previous=animation->frame; previousPose=art->pose;
                }
                check(jumps==0,"A flyer's wings snap from the last drawing back to the first");
                check(turns>=1,"A flyer's wings never reach the end of the row and turn");
                // Slow enough to see: at 0.085 s a drawing nobody could watch the
                // griffin flap, because its downstroke was gone in an instant.
                check(slowestHold>=0.1f,"A flyer's wingbeat goes by too fast to see");
            }

            // Asking for far more effects than the limits allow gets the limits.
            for(int i=0;i<200;++i) spawnEffect(battle,i%2?"SWORD":"CANNON",float(300+i%40),380,false);
            check(countEffects(battle)<=effectLimit(),"Combat effects exceed the overall limit");
            for(int kind=0;kind<effectKindCount();++kind)
                check(countEffects(battle,kind)<=effectKindAt(kind).limit,"A combat effect exceeds its own limit");
            check(countEffects(battle)>0,"No combat effect could be shown at all");
            battleEntities=battle.entities().size();

            // The hero wears the art of the path its owner chose, not the
            // pathless knight — a WARDEN in anyone else's armour would tell the
            // player something false about what it can do.
            driver.tap(SDL_SCANCODE_H);driver.step(2);
            Entity hero=0;
            for(auto& [e,h]:battle.view<Hero>()) {(void)h;hero=e;}
            check(hero!=0,"The hero could not be summoned in the real battle");
            if(hero) {
                const Unit* unit=battle.getComponent<Unit>(hero);
                const char* wardenSheet=unitKind(heroKindIndex()).pathSheet[static_cast<int>(HeroPath::Warden)];
                auto* warden=wardenSheet?engine.textures().load(wardenSheet):nullptr;
                check(warden!=nullptr,"The WARDEN sheet is not named or does not load");
                check(unit&&battle.hasComponent<ArtFigure>(unit->figure)&&
                      battle.getComponent<Sprite>(unit->figure)->texture==warden,
                      "The hero does not wear its path's art");
            }
        }
        auto shootBattle=[&](const char* name) {
            engine.drawWorld(battle);int width=0,height=0;auto pixels=engine.captureFrame(width,height);
            SDL_Surface* surface=SDL_CreateRGBSurfaceWithFormatFrom(pixels.data(),width,height,32,width*4,SDL_PIXELFORMAT_ARGB8888);
            check(surface!=nullptr,"Actual game capture failed");
            if(surface) {check(IMG_SavePNG(surface,(output/name).string().c_str())==0,"Actual game capture write failed");SDL_FreeSurface(surface);}
        };
        shootBattle("game-animation-before.png");driver.step(107);
        bool moved=false,changedFrame=false;
        for(auto& [e,x]:clouds) moved|=battle.getComponent<Transform>(e)->x!=x;
        for(auto& [e,frame]:frames) changedFrame|=battle.getComponent<Animation>(e)->frame!=frame;
        check(moved&&changedFrame,"Animations do not advance in actual battle scene");
        shootBattle("game-animation-after.png");
        driver.tap(SDL_SCANCODE_F5);driver.step(2);
        for(auto& [e,x]:clouds) {(void)x;check(battle.getComponent<Sprite>(e)->a==0,"Game cloud toggle failed");}
        driver.tap(SDL_SCANCODE_P);driver.step(2);
        std::map<Entity,Transform> paused;
        for(auto& [e,tag]:battle.view<AmbientElement>()) {(void)tag;paused[e]=*battle.getComponent<Transform>(e);}
        driver.step(30);
        for(auto& [e,t]:paused) {auto* now=battle.getComponent<Transform>(e);check(now&&now->x==t.x&&now->y==t.y,"Paused game atmosphere still moves");}
    }
    const auto count=world.entities().size();
    const auto weatherEntities=weather.entityCount();
    weather.clear(world);world.flushDestroyed();
    check(world.entities().size()==count-weatherEntities,"Weather teardown leaked entities");
    const auto remaining=world.entities().size()-environment.entityCount();
    environment.clear(world);world.flushDestroyed();
    check(world.entities().size()==remaining,"Environment teardown leaked entities");
    setTextureCache(nullptr);
    std::cout<<"A real battle, two units and a hero in, holds "<<battleEntities<<" entities.\n";
    std::cout<<"Environment verification: "<<failures<<" failures; "<<combinations<<" combinations, three camera positions each. Screenshots in "<<output<<'\n';
    return failures?1:0;
}
