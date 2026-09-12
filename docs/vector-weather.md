# Vector artwork and weather

## Artwork decision

The inspected runner sheet and mountain PNG show deliberate pixel silhouettes and limited shading. The generated unit sheets are 1254 square and still require frame alignment work. Vector tracing would change those pixels and cannot repair animation anatomy or anchors. All 47 original PNGs remain untouched. Unit and combat animation sheets remain raster assets; no PNG is embedded in an SVG.

Four new vector scenery layers (sky, mountains, ruins and vegetation), a cloud, and nine reusable weather shapes live in `assets/lanebattle/vector/`. They use genuine filled paths, stepped edges, muted navy/blue scenery and small two-tone highlights. They are deliberately simpler than the generated paintings. Ground and foreground retain the existing game geometry. The scenery tiles meet at matching edges and cover the full camera range.

The existing TextureCache calls SDL_image's IMG_LoadTexture once per path. The installed SDL_image 2.8.12 loads these SVGs successfully; SDL then draws cached textures through the normal Sprite renderer. No SVG parser or vector tessellation runs per frame. Sources remain editable vectors, but runtime images rasterize at their declared size: zooming far beyond that size will not yield unlimited sharpness. Tiny angled shapes can acquire rasterized edge coverage, so SVG is not a pixel-perfect substitute for hand-authored unit sprites. No browser filters, CSS, fonts or embedded animation are required.

An SDL_image build without SVG decoding will log failed loads. Scenery then retains the original procedural fallback. Missing particle textures fall back to small rectangles; missing fog and lightning are hidden. The preview verification fails on missing SVG support, rather than approving those fallbacks as artwork. No dependency was added.

## Preview and controls

Build and run from the repository root:

```powershell
cmake --build build --config Debug --target weather_preview lanebattle
build/Debug/weather_preview.exe
```

The preview uses the actual engine renderer, vector scenery, and game unit entities. Its bottom panel displays every setting. The same controls work during a battle:

| Key | Control |
| --- | --- |
| F6 | Next preset |
| F7 | Particle intensity, 0 to 1 |
| F8 | Motion speed, 0 to 3; zero freezes weather motion |
| F9 | Wind, -1 left through 0 still to +1 right |
| F10 | Opacity, 0 to 1 |
| F11 | Lightning reduced / normal / off |

Values cycle at their upper limit. Settings live in WeatherSettings and can also be assigned directly. They are presentation-only, reset on entering a new battle, and do not affect combat or campaign saves. Pausing the battle freezes weather with the scene. Cloud drift continues independently of weather selection and weather speed while the scene runs.

## Presets and layering

| Preset | Appearance |
| --- | --- |
| Clear | Unobstructed valley and slowly drifting clouds |
| Rain | Thin blue slanted streaks |
| Snow | Slow pale flakes with lateral sway |
| Hail | Small fast falling ice chips |
| Blood rain | Thick muted crimson drops and a restrained red background tint |
| Thunderstorm | Faster heavy rain, darkened scenery, optional lightning |
| Fog | Low translucent stepped mist banks behind units |
| Ash | Sparse grey irregular flecks settling and swaying |
| Embers | Small amber/red sparks rising from the field |
| Dark storm | Violet/teal rising wisps, dark purple atmosphere, optional lightning |

Art layers use the existing parallax factors. Weather particles use three sizes and speeds for depth; most draw behind combat, with a sparse foreground subset capped at low alpha. Particles occupy the atmospheric band above the lane; mist remains behind units. Weather overlays end above the bottom controls. HUD always draws last. Weather has no colliders, gameplay random-number calls, precipitation accumulation or damage.

Lightning is a separate vector bolt and low-opacity sky panel, lasting 0.1 seconds at intervals of at least seven seconds. Reduced mode is the default. Off suppresses both immediately. Intensity/opacity zero also suppress them. Speed controls particle motion, not flash cadence or independent cloud drift.

## Verification and limits

```powershell
build/Debug/weather_preview.exe --verify manual_testing/weather_shots
ctest --test-dir build -C Debug -R weather_render_tests --output-on-failure
```

The verification runs through all ten presets in SDL's software renderer, checks SVG loading/cache reuse, captures each frame, checks lane/HUD pixels, validates bounded entity counts and teardown, clamps invalid numeric controls, and checks lightning duration, opacity caps and disabling. PNG screenshots here are verification captures, not SVG source replacements.

The particle pool is bounded at 144 sprites plus three atmospheric entities. Clear hides that pool; it remains allocated to avoid allocation churn during switching. Transparent overdraw and per-entity sorting still have a cost, especially with the engine's map-based ECS. Hardware GPU performance has not been benchmarked. The SVG textures consume decoded pixel memory just as PNGs do; smaller source files do not imply less GPU memory.

These are ambient effects: hail has no ground bounce, rain has no splashes, storms have no thunder audio, and presets switch immediately rather than crossfading. The generated PNG unit animations are not repaired or integrated by this change.
