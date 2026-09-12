# Lane Battle GBA art collection

47 original PNG outputs generated with the built-in image generation tool. These are generated source sheets, **not yet production-ready sprites**.

## Files

- `units/friendly/`: 11 unit sheets, including the three hero paths.
- `units/enemy/`: 11 enemy unit sheets.
- `effects/`: 14 combat effect and projectile sheets.
- `scenery/layers/`: 7 scenery outputs.
- `scenery/animated/`: grass, banner, torch and leaves sheets.

`manifest.json` records each asset, its requested dimensions, prompt and original source. `validation.json` records actual PNG dimensions and verifies copies against original SHA-256 hashes. `generation-status.json` records collection status. Full generation prompts are also in `prompts/`.

## Required cleanup before integration

Generated dimensions do not reliably match the requested grids. Do not configure the engine using the requested cell size until frames have been extracted, aligned and checked. Resizing an entire sheet alone will not fix inconsistent spacing, proportions or planted-foot anchors. Pixel edges and palette sizes also need cleanup.

The intended unit row order is idle, movement, attack, take damage, stunned, death. Validate each sequence in playback; attack, hurt and death must play once. Movement, idle and stun should loop. Flying units need their own airborne movement and falling poses.

Known visual problems include a missing enemy ballista attack frame, an extra bird in the friendly Falconer sheet, and a shield in the shield-spark effect. Several scenery outputs combine layers: sky includes terrain, mountains include ruins, foreground includes substantial ruins, and clouds include mountains and ruins. These need isolation or regeneration. Horizontal scenery seams and animation loop continuity are not verified.

All 47 requested asset slots have a generated PNG. This does not mean that all production requirements have passed. Assets have not been integrated into the game.
