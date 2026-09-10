# Repository review — September 9, 2026

The engine is a solid, deliberately small foundation for this game. Lane Battle has substantial gameplay systems, but combat correctness, readable feedback, and progression reliability need attention before more content or a larger engine architecture.

This review inspected the live code, development notes and Git history; built Release; ran all eight CTest entries; ran campaign_probe and engine_bench; generated 15 UI screenshots and inspected five; and compiled an isolated reproduction harness against the shipped libraries. All eight registered tests passed. The harness found the failures below. No production code was changed. This was a code and headless rendering review, not a human playtest or an exhaustive audit. The current commit at completion was f88f3d3.

**How the code works**

- CMake builds an engine static library and separate game libraries. Game entry points assemble the engine, world and initial scene. This dependency direction is a real strength.
- World stores component maps keyed by integer entities. Systems update those components. Destruction is deferred until a safe point.
- Engine::run measures frame time, gathers input, runs movement/lifetime/animation, updates game logic, flushes destruction and renders. Rendering combines sprites, polygons and text into one ordered list with camera and parallax transforms.
- SceneStack queues push/pop/replace operations. Only the top scene updates; components from lower scenes remain visible. Each scene manually tracks the entities it must remove.
- Lane Battle separates persistent Campaign data from per-battle Session data. Most rules and screens still live together in LaneBattle.cpp. Roster, training, loadout, audio and texture access also involve globals.
- PlayScene earns gold, processes upgrades/spawning, updates enemy cannon behavior, fights, removes deaths, heals, handles spells/projectiles, animates figures and updates camera/HUD. That ordering is central to several findings.
- Combat uses forward distance, range, allegiance and air eligibility, rather than the generic collision system. The opponent banks money for waves, then spends under cooldown and population rules.
- units.txt supplies balance overrides, while compiled defaults permit fallback. Campaign saves store progression and unit names.

**Confirmed problems, in suggested repair order**

1. **P1: the screenshot tool can overwrite the player's campaign.** tests/ui_shots.cpp:398 stages a victory and advances the real game logic, which calls saveCampaign. Unlike campaign_probe and lanebattle_tests, ui_shots never calls setSavePath. It therefore uses the normal player save destination. In this sandbox, running it created a fallback campaign.txt in the repository, containing a simulated stage-one victory and 180 gold. That generated fallback file was removed. Give every diagnostic tool its own explicit scratch save path, or supply a non-persisting campaign store. Add a test that a sentinel player save survives running the tool.

2. **P1: battle completion can pay repeatedly and change outcome.** LaneBattle.cpp:2337 handles castle death inside the attacker loop without terminating the battle calculation or guarding payout. Three attackers delivering finishing blows in one update awarded 360 gold, versus the intended first-clear reward of 180. With both castles at one health and one attacker at each end, the same update awarded 180 gold and then reported playerWon=false. Finalize a battle exactly once. Decide the simultaneous-destruction rule explicitly and apply it consistently before paying and saving.

3. **P2: dead units still attack and remain valid targets.** LaneBattle.cpp:2277 does not reject attackers with health <= 0; findTargetAhead at line 241 also accepts dead targets. Reproduction: a zero-health enemy soldier dealt 14 damage before deletion. This is reachable because spells/cannon explosions can kill after removeTheDead has already run, leaving those units until the next combat pass. Exclude dead entities from attacking, targeting and blocking; settle deaths after all damaging effects. If simultaneous attacks are intended, collect and resolve attacks explicitly instead of relying on deferred deletion.

4. **P2: Escape quits screens that promise to go back.** Engine.cpp:105 unconditionally quits on Escape, while ArmyScene and HeroScene handle the same key by popping the screen. The Army screenshot advertises “Q OR ESC TO GO BACK.” A real Engine loop supplied an Escape event stopped after one frame despite a five-frame limit. Leave navigation keys to the active scene; keep OS window-close handling in the engine. Cover this through the actual event loop, not only Harness.

5. **P2: quick presses and clicks disappear.** Input.h:34–109 compares held state before and after polling. A key-down followed by key-up in the same frame leaves both states released. Reproductions reported false for the key press, mouse press and mouse release. This can affect pause, purchases and cannon input when both events queue between updates. Record event edges during polling, separately from held state. Also retain event positions if multiple clicks in one frame must address different controls.

6. **P2: saved loadouts change shortcut positions.** saveCampaign skips empty slots and loadCampaign packs names into consecutive slots. The verified round trip was [-1, SOLDIER, -1, ARCHER] -> [SOLDIER, ARCHER, -1, -1]. This conflicts with toggleCarried's explicit promise to preserve shortcut positions. Separately, passing a completely empty chosen loadout to setLoadout restores the default roster because “no valid entries” means “no choice made.” Preserve slot indices in saves and distinguish an explicit empty choice from an uninitialized loadout. Alternatively, prohibit removing the last unit with clear UI feedback.

7. **P2: attack timing changes with frame rate.** LaneBattle.cpp:2305 resets the attack timer to its full interval, discarding overshoot. The same soldier attacking a stationary high-health castle for 60 seconds dealt 1,260 damage at 15 FPS and 1,400 at 30, 60 and 144 FPS. Preserve time remainder and define catch-up behavior, preferably within a fixed simulation tick. Verify multiple unit types and frame-time patterns; the existing campaign probe advances through the default 60 Hz harness.

**Additional hardening findings from inspection**

- DataSection::number accepts NaN, infinity and partial numeric strings; the NaN result was reproduced. Most unit numeric fields are copied directly from the file. Enforce finite values and game-specific ranges at the balance boundary. Invalid costs, dimensions and attack/animation intervals must not enter a battle. Extremely small positive animation intervals can also make the subtraction loop take an impractical number of iterations or stop making floating-point progress.
- DataWriter::save truncates the existing file in place and checks stream state before an explicit flush/close. Most gameplay callers ignore its result. Write to a sibling temporary file, check completion, replace the old file only after success, and report a save failure to the player. This is an inspected reliability risk, not an induced disk-failure test.
- Sprite sheets always start at srcY=0 and loop; game behavior does not select idle/walk/attack/death clips. The new pipeline is useful, but state-specific animation is still needed before art can communicate combat accurately.
- Cannon flight uses a continuous trajectory formula but advances with discrete movement and a countdown. Verify impact position at several frame rates, especially for enemy shots created before the projectile update. This remains a follow-up concern, not a confirmed reproduction in this review.

**Assessment of the engine**

Keep the library boundary, plain component data, queued transitions, shared built-in systems, headless harness and pixel tests. They make this code approachable and unusually inspectable for a small custom engine.

The next useful abstractions are modest: event-based input edges, a fixed simulation step, ownership of scene-created entities, common UI rectangles for drawing and hit testing, and explicit per-session access to balance/resources/persistence. Moving loadout/training out of globals would also prevent independent worlds from affecting each other.

There is little justification for rewriting the ECS now. On this machine engine_bench measured movement at about 0.092 ms for 1,600 entities. Generic pairwise collision was about 0.57 ms at 100 entities, 9.35 ms at 400 and 37.8 ms at 800. Those are synthetic measurements, not Lane Battle frame times. Lane Battle uses its own targeting and relatively small armies. Profile the actual game before adding spatial indexing, batching or packed storage.

Split LaneBattle.cpp by responsibility while preserving behavior: balance/loading, campaign/persistence, battle rules, presentation, and menu scenes. Extract one boundary at a time after the correctness fixes; a rewrite would make those fixes harder to verify.

**Assessment of the game**

The core ingredients support the intended Cartoon Wars inspiration: opposing castles, automatic marching combat, unit composition, timed spending, an active castle weapon and permanent progression. The game already has enough mechanics to support a focused playable slice. The missing information and feedback are more urgent than more unit types.

The latest campaign probe produced 14 draws in 112 stage/strategy combinations: 12.5% reached its 400-second cutoff. That cutoff belongs to the probe; the live game has no matching stalemate resolution. Only FULL won stage eight, while all three hero-path strategies won stages one through seven. These results describe the scripted strategies and their configured resources; they do not establish what all human players can do or how enjoyable progression is.

The inspected screenshots expose three practical obstacles: tiny Army/hero stat text, units whose combat roles are difficult to distinguish at a glance, and a defeat screen that offers no explanation. The stage list shows names but not enemy composition. A player can make a poor loadout decision before being given the information needed to improve it.

**Recommended development sequence**

1. Fix the confirmed failures, beginning with diagnostic save isolation and one-time battle completion. Add regression tests that reproduce their actual triggers.
2. Build one polished battle with distinctive silhouettes, visible attack/projectile effects, health and hit feedback, clear cooldowns, and larger selected-unit details. Use original art; the reference's assets are not needed.
3. Add enemy previews, role labels and a concise loss report: damage sources, units lost, idle gold, and an actionable hint. Let the player adjust the army and retry directly.
4. Design a stalemate rule and test it. Options include a telegraphed escalation phase or a battle timer with an explicit result. Avoid indefinitely increasing both sides' resources without checking whether that actually breaks deadlock.
5. Revisit stage eight after combat/timing fixes. Seek multiple viable approaches across distinct loadouts, and add imperfect-player simulations with delayed input, missed casts and less efficient spending. Simulate an earned campaign progression path as well as isolated battles with configured upgrades.
6. Run observed human playtests before expanding the roster. Watch whether new players understand what to buy, where damage comes from, when to use the cannon and why they lost. Use those observations to choose the next engine feature.

Reproductions are retained in build/review-src/review_probe.cpp, with a separate CMake project and executable under build/review-build. They link the existing Release libraries and use a scratch save in their execution directory. UI captures are under build/Release/ui_shots. All are local build artifacts. The review itself is the only added tracked-source document.
