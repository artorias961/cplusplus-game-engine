# Reserve character inventory

All **40 character PNG sheets now have real alpha transparency**. Clean copies use plain character names, such as `friendly/grave-warden.png`. Untouched generated originals retain `-source` in their names. The failed alpha retry is also preserved as a source artifact.

## Review

Open [animated review](review/index.html) for all six groups, six labeled transparent contact sheets, animation selection, pause, replay, scale, and dark/light/green backdrop controls. The default preview and full-sheet links use cleaned copies. Historical `*-sources.png` contact sheets intentionally show the original opaque artwork.

## What was fixed

Magenta mattes and painted checkerboards were removed by the authorized local cleanup script. Small enclosed checker areas were identified separately; contaminated outside outline pixels were corrected using neighboring outline colors. All exports are RGBA with real transparent pixels. Source dimensions and frame positions are unchanged. Source PNG hashes match the original generated files.

## Remaining animation work

This fix addresses the backgrounds. It does not certify production-ready animation sheets. Generated frame spacing still needs extraction/alignment; some equipment, pose continuity and baked status marks need artistic corrections. Review all states before engine integration. Large spell/projectile effects were excluded from generation prompts, although small staff-tip glows remain.

The intended layout is six columns and six rows: idle, movement, attack/casting, damage, stunned, death. Idle, movement and stun loop; other rows stop in the preview. Manifest timings are proposed review timings because the original collection did not provide per-frame timings. Logical 48/64/96-pixel cells describe intended scale; the actual source sheets remain 1254 × 1254. No whole-sheet resizing or frame relocation was performed during transparency cleanup.

## Files

| Character | Allegiance | Category | Transparent PNG | Target cell |
| --- | --- | --- | --- | ---: |
| Grave Warden | friendly | ground | [grave-warden.png](friendly/grave-warden.png) | 64 |
| Lantern Keeper | friendly | ground | [lantern-keeper.png](friendly/lantern-keeper.png) | 48 |
| Ruin Scavenger | friendly | ground | [ruin-scavenger.png](friendly/ruin-scavenger.png) | 48 |
| Dawnshield Guardian | friendly | ground | [dawnshield-guardian.png](friendly/dawnshield-guardian.png) | 64 |
| Silverwood Archer | friendly | ground | [silverwood-archer.png](friendly/silverwood-archer.png) | 64 |
| Rune Cannoneer | friendly | ground | [rune-cannoneer.png](friendly/rune-cannoneer.png) | 64 |
| Ash Knight | enemy | ground | [ash-knight.png](enemy/ash-knight.png) | 64 |
| Plague Apothecary | enemy | ground | [plague-apothecary.png](enemy/plague-apothecary.png) | 64 |
| Hollow Archer | enemy | ground | [hollow-archer.png](enemy/hollow-archer.png) | 64 |
| Bone Hound | enemy | ground | [bone-hound.png](enemy/bone-hound.png) | 64 |
| Crypt Brute | enemy | ground | [crypt-brute.png](enemy/crypt-brute.png) | 96 |
| Chainbound Reaver | enemy | ground | [chainbound-reaver.png](enemy/chainbound-reaver.png) | 96 |
| Crow Familiar | friendly | flying | [crow-familiar.png](friendly/crow-familiar.png) | 48 |
| Gryphon Rider | friendly | flying | [gryphon-rider.png](friendly/gryphon-rider.png) | 96 |
| Skyguard Lancer | friendly | flying | [skyguard-lancer.png](friendly/skyguard-lancer.png) | 96 |
| Runejet Skirmisher | friendly | flying | [runejet-skirmisher.png](friendly/runejet-skirmisher.png) | 64 |
| Lantern Wisp | friendly | flying | [lantern-wisp.png](friendly/lantern-wisp.png) | 48 |
| Clockwork Falcon | friendly | flying | [clockwork-falcon.png](friendly/clockwork-falcon.png) | 64 |
| Cloud Witch | friendly | flying | [cloud-witch.png](friendly/cloud-witch.png) | 96 |
| Dawn Drake | friendly | flying | [dawn-drake.png](friendly/dawn-drake.png) | 96 |
| Carrion Bat | enemy | flying | [carrion-bat.png](enemy/carrion-bat.png) | 64 |
| Broken Gargoyle | enemy | flying | [broken-gargoyle.png](enemy/broken-gargoyle.png) | 96 |
| Plague Harpy | enemy | flying | [plague-harpy.png](enemy/plague-harpy.png) | 96 |
| Blood Imp | enemy | flying | [blood-imp.png](enemy/blood-imp.png) | 64 |
| Bone Wyvern | enemy | flying | [bone-wyvern.png](enemy/bone-wyvern.png) | 96 |
| Shrieking Wraith | enemy | flying | [shrieking-wraith.png](enemy/shrieking-wraith.png) | 64 |
| Ash Moth | enemy | flying | [ash-moth.png](enemy/ash-moth.png) | 96 |
| Void Watcher | enemy | flying | [void-watcher.png](enemy/void-watcher.png) | 64 |
| Ember Wizard | friendly | spellcaster | [ember-wizard.png](friendly/ember-wizard.png) | 64 |
| Cinder Warlock | enemy | spellcaster | [cinder-warlock.png](enemy/cinder-warlock.png) | 64 |
| Frost Sage | friendly | spellcaster | [frost-sage.png](friendly/frost-sage.png) | 64 |
| Frostbound Lich | enemy | spellcaster | [frostbound-lich.png](enemy/frostbound-lich.png) | 64 |
| Storm Mage | friendly | spellcaster | [storm-mage.png](friendly/storm-mage.png) | 64 |
| Tempest Cultist | enemy | spellcaster | [tempest-cultist.png](enemy/tempest-cultist.png) | 64 |
| Dawn Cleric | friendly | spellcaster | [dawn-cleric.png](friendly/dawn-cleric.png) | 64 |
| Blood Acolyte | enemy | spellcaster | [blood-acolyte.png](enemy/blood-acolyte.png) | 64 |
| Grove Summoner | friendly | spellcaster | [grove-summoner.png](friendly/grove-summoner.png) | 64 |
| Marsh Witch | enemy | spellcaster | [marsh-witch.png](enemy/marsh-witch.png) | 64 |
| Arcane Scholar | friendly | spellcaster | [arcane-scholar.png](friendly/arcane-scholar.png) | 64 |
| Grave Necromancer | enemy | spellcaster | [grave-necromancer.png](enemy/grave-necromancer.png) | 64 |

[Exact generation prompts and paths](manifest.json) · [Transparency validation](transparency-validation.json) · [Source hash checks](copy-validation.json) · [Original grid diagnostics](source-validation.json).

The read-only source audit retains historical background findings. Current delivery status is in the transparency validation and manifest. No runtime source, spawn lists or automatic loading configuration was changed.
