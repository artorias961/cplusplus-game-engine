# Animated environments

The environment preview and Lane Battle share `EnvironmentSystem` and `WeatherSystem`. The original ruined valley remains scene 0; six generated landscapes add storm ruins, a frozen citadel, a blood-moon wasteland, an ashen battlefield, a haunted marsh and a desert necropolis.

## Running

```powershell
cmake --build build --config Debug --target weather_preview lanebattle
build/Debug/weather_preview.exe
```

| Key | Action |
| --- | --- |
| F2 | Next environment |
| F3 | Calm / strong ambience, selecting that scene's default weather |
| F6 | Next compatible weather, including OFF |
| F7 | Intensity |
| F8 | Particle speed (zero freezes weather particles) |
| F9 | Wind from left to right |
| F10 | Weather opacity |
| F11 | Lightning reduced, normal, off |
| Left / right | Move camera to inspect parallax |

F2 preserves weather when compatible; otherwise it selects OFF. F6 changes weather without rebuilding the scenery or restarting ambience. Settings are presentation-only and do not affect the campaign save or combat. The game uses the same function keys; pausing pauses ambience and weather together.

## Scene motion

All scenes have drifting PNG clouds, multiple low mist layers and restrained ambient flecks. The reference also has occasional falling debris. Frozen ruins add low powder gusts; the blood moon adds a separate pixel moon and faint pulses; the ashen field adds rising sparks and smoke; marsh water uses moving reflection streaks and ghost lights; desert scenery adds sand wisps and narrow one-pixel heat-shimmer strips. Strong mode raises scene motion while selectable weather supplies the heavier precipitation or fog.

Rain, snow, hail, blood rain, thunderstorms, fog, ash, embers, supernatural storms and sandstorms use bounded reusable ECS sprites. Rain has independent ground splashes. Lightning uses a separate genuine branching SVG and a low-alpha backdrop pulse: 0.1 seconds, at least seven seconds apart, reduced by default. OFF disables both the bolt and the flash immediately.

## Layers and pixel art

Detailed environments are PNG artwork; clouds are source rectangles from the preserved reference PNG. Independent layers contain sky/background art, moving clouds/moon, landscape, lighting, low fog, ambient motion, weather and gameplay. Ground remains at y=420; nothing alters collision, unit size, lane perspective or HUD placement. PNG sampling uses nearest filtering and moving artwork is positioned at integer pixels.

The reference's clouds and landscape can be selected separately because its original PNG has real transparency. The generated alternatives include mountains and ruins in a single painted layer; those cannot move independently without reconstructing hidden scenery. Mirrored neighbouring landscape tiles cover the finite camera range with gentle 0.10 parallax. Clouds use a slower camera response. This deliberately avoids making unsupported claims that the original paintings are fully separated parallax atlases.

The image generator twice returned a painted checkerboard instead of real alpha. Those generation sources are retained for review but are not suitable as transparency layers. The plain-sky fallback PNGs avoid that artifact, at the cost of combining their static sky and landscape. All moving effects remain independent. Prompt/source records live beside the assets.

PNG detail is normalized to the 960-pixel view through nearest sampling; generated source pixel clusters are not a rigorously hand-authored common tile grid. Artwork is sharper than filtered scaling, but a production pixel-by-pixel cleanup pass would still improve exact pixel consistency. No PNG is embedded inside an SVG.

## Verification

```powershell
build/Debug/weather_preview.exe --verify manual_testing/environment_shots
ctest --test-dir build -C Debug -R weather_render_tests --output-on-failure
```

Verification enumerates every scene/weather combination offered by the preview, in calm and strong states, and checks three camera positions per combination. It exports PNGs plus `combinations.json`, checks lane and HUD pixels, verifies units never move from presentation updates, verifies entity cleanup, tests zero-speed motion, wind changes, input clamping and lightning constraints. These are real engine captures, not browser mockups.

The completed run passed 72 combinations and 216 camera checks with zero failures. Representative screenshots from all seven scenes were visually inspected. Game logic, renderer tests and game startup also passed. Open `manual_testing/environment_shots/index.html` for the screenshot gallery.

Storm audio, dynamic accumulation, environmental damage and physically simulated water are outside this presentation feature. Fog/smoke/dust-devil effects are deliberately stylized and low-opacity. Scenes switch immediately; individual atmospheric motions loop continuously. CPU particle pools and transparent overdraw still cost work; GPU performance has not been benchmarked.
