# Reserve characters — generated sources, cleanup pending

This folder is isolated artwork for future use. Nothing here is registered with gameplay, spawn lists, or runtime asset loading.

All 40 requested designs have generated PNG source sheets: 20 friendly and 20 enemy, organized by allegiance. They follow the original six-row animation intent: idle, movement, attack/casting, hit, stunned, death. Original source grids were not uniformly production-ready, and these new sources also require cleanup.

**These are not finished transparent sprite exports.** All 40 sources are 1254 × 1254 RGB; 30 have magenta backgrounds and 10 have painted checkerboards. A targeted image-generator transparency retry also failed. Frame boundaries, anchors, pixel density and some equipment/status details need correction. Permission for local script-based background removal and frame alignment is pending; no such image edits have been performed.

Open [the animated review page](review/index.html) for all six groups, six labeled contact sheets, animation state selection, pause, speed, replay and scale controls. [INVENTORY.md](INVENTORY.md) lists every PNG and outstanding QA concerns. The preview exposes the source-grid problems; it does not certify usable animation alignment. Its 40 images and playback/pause/one-shot controls passed browser checks.

Exact per-character prompts and original generation paths are in [manifest.json](manifest.json). Images were created with the built-in image_gen tool using the original friendly soldier and enemy griffin sheets as direct visual references. Proposed timing metadata is marked as review-only because the original collection did not supply per-frame timings.

Existing PNGs and game code are untouched. No assets here are registered with gameplay or automatic loading.
