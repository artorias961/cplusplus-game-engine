# Reserve character inventory

40 character source PNGs generated with the built-in image_gen tool, plus one unsuccessful transparency retry. **Zero sheets currently pass the requested transparent, aligned export requirements.**

All source images are 1254 × 1254 RGB; 30 have magenta backgrounds and 10 have painted checkerboards. The targeted alpha retry also returned RGB with no transparency. Existing character files are untouched.

## Review

Open [animated review](review/index.html). It includes all six allegiance/category groups, state selection, speed, pause, one-shot replay and target-size comparison. Six labeled PNG contact sheets are linked there. The source animation previews use provisional equal subdivisions; visibly cropped weapons, borrowed neighboring pixels and jumps expose the need for frame extraction and alignment. Playback controls passed automated browser checks; animation anatomy and seamlessness are not certified.

## Asset conventions

Requested layout matches the original collection: six columns and six rows, idle → movement → attack/casting → hit → stunned → death. Idle, movement and stun loop; other states stop on their last frame in the preview. Ground, airborne and caster prompts differ appropriately. Large independent spell effects/projectiles were excluded; small staff-tip glows may remain in the drawings.

The original collection did not contain per-frame timing metadata. `manifest.json` supplies explicitly proposed review timings, not established game timings. The 48/64/96-pixel target cells are logical intent; the actual generated source cells are approximately 209 pixels and must not be treated as final game resolution.

## Outstanding corrections

- Remove opaque backgrounds while preserving navy outlines, pale armor, wing membranes and internal transparent gaps.
- Extract all poses into uniform cells; stabilize foot/body anchors without normalizing away intentional attack or falling motion.
- Repair frame-to-frame equipment inconsistencies: for example Grave Warden has borrowed sword/shield details in later rows, and Lantern Keeper includes unwanted status marks.
- Review wing anatomy, turn/facing consistency, walk contact, hit reactions and loop seams in each sequence.
- Remove stray pixels and separate any baked status effects. Exact palette/pixel-density and relative-scale cleanup remains necessary.
- No automatic image-cleanup script has been run. Permission for script-based background removal and alignment is pending.

## Files

Grid-contact counts below are conservative color-based diagnostics, **not proven clipping counts**. They identify frames needing visual review.

| Character | Allegiance | Category | Source PNG | Target cell | Potential grid contacts |
| --- | --- | --- | --- | ---: | ---: |
| Grave Warden | friendly | ground | [grave-warden-source-v1.png](friendly/grave-warden-source-v1.png) | 64 | 14 / 36 |
| Lantern Keeper | friendly | ground | [lantern-keeper-source.png](friendly/lantern-keeper-source.png) | 48 | 4 / 36 |
| Ruin Scavenger | friendly | ground | [ruin-scavenger-source.png](friendly/ruin-scavenger-source.png) | 48 | 5 / 36 |
| Dawnshield Guardian | friendly | ground | [dawnshield-guardian-source.png](friendly/dawnshield-guardian-source.png) | 64 | 25 / 36 |
| Silverwood Archer | friendly | ground | [silverwood-archer-source.png](friendly/silverwood-archer-source.png) | 64 | 4 / 36 |
| Rune Cannoneer | friendly | ground | [rune-cannoneer-source.png](friendly/rune-cannoneer-source.png) | 64 | 5 / 36 |
| Ash Knight | enemy | ground | [ash-knight-source.png](enemy/ash-knight-source.png) | 64 | 26 / 36 |
| Plague Apothecary | enemy | ground | [plague-apothecary-source.png](enemy/plague-apothecary-source.png) | 64 | 21 / 36 |
| Hollow Archer | enemy | ground | [hollow-archer-source.png](enemy/hollow-archer-source.png) | 64 | 6 / 36 |
| Bone Hound | enemy | ground | [bone-hound-source.png](enemy/bone-hound-source.png) | 64 | 8 / 36 |
| Crypt Brute | enemy | ground | [crypt-brute-source.png](enemy/crypt-brute-source.png) | 96 | 12 / 36 |
| Chainbound Reaver | enemy | ground | [chainbound-reaver-source.png](enemy/chainbound-reaver-source.png) | 96 | 23 / 36 |
| Crow Familiar | friendly | flying | [crow-familiar-source.png](friendly/crow-familiar-source.png) | 48 | 5 / 36 |
| Gryphon Rider | friendly | flying | [gryphon-rider-source.png](friendly/gryphon-rider-source.png) | 96 | 26 / 36 |
| Skyguard Lancer | friendly | flying | [skyguard-lancer-source.png](friendly/skyguard-lancer-source.png) | 96 | 26 / 36 |
| Runejet Skirmisher | friendly | flying | [runejet-skirmisher-source.png](friendly/runejet-skirmisher-source.png) | 64 | 4 / 36 |
| Lantern Wisp | friendly | flying | [lantern-wisp-source.png](friendly/lantern-wisp-source.png) | 48 | 3 / 36 |
| Clockwork Falcon | friendly | flying | [clockwork-falcon-source.png](friendly/clockwork-falcon-source.png) | 64 | 2 / 36 |
| Cloud Witch | friendly | flying | [cloud-witch-source.png](friendly/cloud-witch-source.png) | 96 | 0 / 36 |
| Dawn Drake | friendly | flying | [dawn-drake-source.png](friendly/dawn-drake-source.png) | 96 | 9 / 36 |
| Carrion Bat | enemy | flying | [carrion-bat-source.png](enemy/carrion-bat-source.png) | 64 | 8 / 36 |
| Broken Gargoyle | enemy | flying | [broken-gargoyle-source.png](enemy/broken-gargoyle-source.png) | 96 | 9 / 36 |
| Plague Harpy | enemy | flying | [plague-harpy-source.png](enemy/plague-harpy-source.png) | 96 | 6 / 36 |
| Blood Imp | enemy | flying | [blood-imp-source.png](enemy/blood-imp-source.png) | 64 | 36 / 36 |
| Bone Wyvern | enemy | flying | [bone-wyvern-source.png](enemy/bone-wyvern-source.png) | 96 | 15 / 36 |
| Shrieking Wraith | enemy | flying | [shrieking-wraith-source.png](enemy/shrieking-wraith-source.png) | 64 | 9 / 36 |
| Ash Moth | enemy | flying | [ash-moth-source.png](enemy/ash-moth-source.png) | 96 | 4 / 36 |
| Void Watcher | enemy | flying | [void-watcher-source.png](enemy/void-watcher-source.png) | 64 | 4 / 36 |
| Ember Wizard | friendly | spellcaster | [ember-wizard-source.png](friendly/ember-wizard-source.png) | 64 | 10 / 36 |
| Cinder Warlock | enemy | spellcaster | [cinder-warlock-source.png](enemy/cinder-warlock-source.png) | 64 | 18 / 36 |
| Frost Sage | friendly | spellcaster | [frost-sage-source.png](friendly/frost-sage-source.png) | 64 | 1 / 36 |
| Frostbound Lich | enemy | spellcaster | [frostbound-lich-source.png](enemy/frostbound-lich-source.png) | 64 | 24 / 36 |
| Storm Mage | friendly | spellcaster | [storm-mage-source.png](friendly/storm-mage-source.png) | 64 | 33 / 36 |
| Tempest Cultist | enemy | spellcaster | [tempest-cultist-source.png](enemy/tempest-cultist-source.png) | 64 | 26 / 36 |
| Dawn Cleric | friendly | spellcaster | [dawn-cleric-source.png](friendly/dawn-cleric-source.png) | 64 | 21 / 36 |
| Blood Acolyte | enemy | spellcaster | [blood-acolyte-source.png](enemy/blood-acolyte-source.png) | 64 | 24 / 36 |
| Grove Summoner | friendly | spellcaster | [grove-summoner-source.png](friendly/grove-summoner-source.png) | 64 | 7 / 36 |
| Marsh Witch | enemy | spellcaster | [marsh-witch-source.png](enemy/marsh-witch-source.png) | 64 | 7 / 36 |
| Arcane Scholar | friendly | spellcaster | [arcane-scholar-source.png](friendly/arcane-scholar-source.png) | 64 | 8 / 36 |
| Grave Necromancer | enemy | spellcaster | [grave-necromancer-source.png](enemy/grave-necromancer-source.png) | 64 | 9 / 36 |

Full exact prompts and original output paths: [manifest.json](manifest.json). Initial generation specification: [generation-plan.json](generation-plan.json). Read-only pixel diagnostics: [source-validation.json](source-validation.json). Copy hashes: [copy-validation.json](copy-validation.json).

No runtime source, spawn configuration, automatic asset manifest or loading code was changed.
