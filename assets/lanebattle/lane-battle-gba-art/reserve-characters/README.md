# Reserve characters — transparent PNG exports

All 40 characters now have real alpha transparency. Clean files use the character name, for example `friendly/grave-warden.png` or `enemy/bone-wyvern.png`. The original generated files keep their `-source` names and are preserved unchanged.

[Open the animated review](review/index.html) for the cleaned characters, six labeled contact sheets, animation states, pause/replay, speed, scale and dark/light/green backdrops. [INVENTORY.md](INVENTORY.md) links every transparent PNG.

The authorized local cleanup removes pink mattes and painted checkerboards, clears detected internal checker gaps and corrects contaminated outside edges. All 40 exports are RGBA; transparent pixels contain no painted backdrop. Dimensions and source frame positions are unchanged. [Transparency checks](transparency-validation.json) and [source hash checks](copy-validation.json) record the results.

Background cleanup does not fix the earlier generated animation-grid and pose problems. Frame extraction/alignment, equipment consistency and loop polishing remain necessary before gameplay integration. The preview uses provisional six-by-six divisions and explicitly proposed timing metadata.

Historical source contact sheets (`review/*-sources.png`) and source-validation.json intentionally retain the original failed-background findings. Use the unsuffixed contact sheets and transparency-validation.json for current output.

The original artwork was created with the built-in image_gen tool using the project's soldier and griffin sheets as direct references. Exact prompts and source paths remain in [manifest.json](manifest.json). Existing character artwork, game code, spawn lists and automatic loading remain untouched.
