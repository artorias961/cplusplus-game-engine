# Independent background animation

The actual Lane Battle play scene and `weather_preview` both run the same environment and weather systems. Start `build/Debug/lanebattle.exe` and enter a battle, or run `build/Debug/weather_preview.exe` for immediate inspection.

## What is painted and what moves

The reference PNG contains clouds, mountains, ruins and ground, but the renderer crops its landscape below the clouds. Its clouds are drawn separately from another source rectangle. The six alternative runtime PNGs have cloud-free skies. Consequently there were no stationary cloud duplicates to erase, and no original PNG was modified.

Mountains, solid ruins, base terrain, painted tree silhouettes and base water remain painted into the landscape. Their only movement is camera parallax. Foreground animated reeds and water reflections overlay that artwork; this does not animate every painted tree or reconstruct a physically moving water surface.

| Element | Animation |
| --- | --- |
| Clouds | A pool of eight reference-painting clouds, spawned at RANDOM heights and times up to the limit in `effects.txt` (five), each small, medium or large — a small one is far, fainter and slower. They drift in at the upwind edge and leave at the other, with slow integer-pixel size/bob changes; not sprite-frame animations. Fog banks and dust spawn the same way. |
| Ground fog | Independently rolling layers with density pulses and six-frame mist sprites. |
| Ashen smoke | Rising, expanding, fading six-frame mist sprites. |
| Ashen flames | Six-frame flame sprites with varied starting phases. |
| Marsh water | Six-frame ripple overlays. |
| Reference/marsh vegetation | Six-frame rooted foreground reed tufts. |
| Rain, snow, hail, blood rain, ash, embers | Engine-driven particle movement. Rain/blood splash, hail bounces, snow/ash briefly settle; embers flicker. |
| Lightning | Occasional branching bolt and restrained scene flash, 0.1 seconds and at least seven seconds apart. Reduced by default. |
| Other ambience | Falling debris, powder gusts, ghost lights, supernatural pulses and sand/heat effects remain separate scene-dependent objects. |

Mist, flame, ripple and reed sheets are genuine vector shapes in six 32-pixel frames, loaded by SDL_image and advanced by the engine's existing `AnimationSystem`. Detailed landscape art remains PNG. Nearest sampling and integer positioning preserve crisp edges. Clouds retain the reference artwork; their subtle deformation is a compromise compared with a hand-drawn cloud frame cycle.

## Controls (game and preview)

| Key | Action |
| --- | --- |
| F2 | Switch scene |
| F3 | Calm/strong ambience and default weather |
| F4 | Select clouds, fog, dust, smoke, flames, water, vegetation or lighting |
| F5 | Enable/disable selected ambient group |
| `[` / `]` | Decrease/increase selected group's opacity/intensity |
| `,` / `.` | Decrease/increase selected group's speed; zero freezes it |
| Backslash | Reverse cloud directions |
| F6 | Switch compatible weather, including OFF |
| F7 / F8 / F9 / F10 | Weather intensity / speed / wind / opacity |
| F11 | Reduced/normal/off lightning |
| F12 | Toggle precipitation impacts |
| P (game) | Pause gameplay and atmosphere |

Scene-specific groups only appear where appropriate: flames/smoke in Ashen, water in Marsh, reeds in Reference/Marsh. Dust also controls small scene-specific flecks such as sparks and ghost lights. Weather OFF leaves ambient scenery running; disable ambient groups separately to obtain a still scene. Controls are presentation settings and do not alter combat or campaign saves.

## Verification

`build/Debug/weather_preview.exe --verify manual_testing/independent_animation_shots` passed with zero failures: 72 calm/strong scene/weather combinations at three camera positions each. Additional checks cover per-group disabling and zero-speed freezing. An actual `PlayScene` run confirms clouds change position, sprite frames advance, the cloud toggle hides them, and pausing freezes atmosphere. Before/after screenshots are in that output folder. Release `lanebattle_tests` and `render_tests` also passed.

Screenshots show sampled moments, not recordings. GPU performance has not been benchmarked; effects use bounded entity pools, but transparent layers add overdraw. Atmospheric effects are stylized, with no physical water simulation or snow accumulation.
