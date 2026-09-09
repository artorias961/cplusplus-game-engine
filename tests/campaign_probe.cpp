#define SDL_MAIN_HANDLED

// ---------------------------------------------------------------------------
// campaign_probe.cpp — plays the whole campaign, several ways, and prints what
// happened.
//
// Not a test. It measures rather than asserts, like engine_bench, and it is
// deliberately not registered with ctest: it takes about a minute and its
// output is a table to read, not a pass or a fail.
//
// It exists because the stage table cannot be tuned by looking at it. That is
// not a guess — it is the recorded history of this file's subject:
//
//   * the first attempt at the curve left THREE stages nobody could win;
//   * the second put an unbeatable wall at stage three, because a lean
//     composition beats a padded one once income is high enough;
//   * the third shipped, and then the hero, the spells and the griffin all
//     landed on top of a curve measured before any of them existed.
//
// Every one of those was invisible in the numbers and obvious after one run of
// something like this.
//
// WHAT IT PRINTS
//
// A row per stage, a column per kind of player. The player kinds are ordered
// by how much of the game they know, and the shape you want down each column
// is a run of wins that turns into losses — with the turn coming LATER for the
// players who know more. A stage that every column wins is asking nothing. A
// stage that every column loses cannot be beaten. A column that never loses
// means the knowledge it represents is not needed anywhere.
//
//   NAIVE   soldiers only, no upgrades. A first-time player.
//   MIXED   soldier/soldier/archer. The player who has learned to mix.
//   AIR     mixed plus griffins. The player using the whole bar.
//   ECON    mixed, buying INCOME whenever it can afford to.
//   HERO    mixed plus the one hero summon.
//   FULL    mixed, griffins, economy, hero and spells. Everything the game has.
// ---------------------------------------------------------------------------

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "Harness.h"
#include "LaneBattle.h"

using namespace engine;
using lanebattle::Castle;
using lanebattle::Session;
using lanebattle::Unit;

namespace {

constexpr int kRunner = 0;
constexpr int kSoldier = 1;
constexpr int kArcher = 2;
constexpr int kGriffin = 3;
constexpr int kPikeman = 4;
constexpr int kOgre = 5;
constexpr int kBallista = 6;

// How long a battle is given before it is called a draw. Long enough that a
// draw means "neither side can finish this", not "the clock ran out".
constexpr int kMaxSeconds = 400;

// What one run of a stage is allowed to use.
struct Strategy {
    const char* name;
    std::vector<int> cycle;  // roster indices, sent in this order
    bool hero = false;       // summon it, once, when the front line is engaged
    bool spells = false;     // cast whatever is affordable
    bool economy = false;    // buy INCOME whenever it can be afforded
    bool cannon = false;     // shell the enemy line out of surplus

    // Which path the hero walks. Ignored unless `hero` is set.
    //
    // This is the fairness criterion for the hero tree, and it is a criterion
    // rather than an opinion: no path may win more stages than the others, and
    // each must win at least one stage the other two lose. A path that wins
    // everything is the old hero back again; a path that wins nothing is a
    // trap for whoever picked it, and picking is permanent.
    int path = 0;

    // Levels of the GRANARY perk this player walked in with. The economy that
    // survives measurement lives here rather than inside the battle.
    int granary = 0;
};

struct Outcome {
    int result = 0;      // +1 won, -1 lost, 0 neither
    float seconds = 0.0f;
    float ownCastle = 0.0f;
    float enemyCastle = 0.0f;
    int shots = 0;       // cannon shots that actually left the barrel
    int upgrades = 0;    // INCOME levels actually bought
};

// One battle, driven the way a player would drive it.
Outcome play(int stage, const Strategy& strategy) {
    World world;
    SceneStack scenes;
    harness::Harness driver(world, scenes);

    // The roster and the stage table are loaded by the CALLER, not here.
    //
    // This used to reset and reload them on entry, which silently destroyed
    // the threshold sweep: the sweep writes a scratch stage, calls this, and
    // this immediately replaced it with the shipped table and played THE
    // BORDER instead. Every player then "beat" every income up to 1.80, which
    // is a perfectly believable-looking table of numbers meaning nothing at
    // all. A fresh World per battle is what keeps runs independent; the roster
    // is global on purpose and belongs to whoever set it up.

    // Saving is pointed away from the real campaign: this plays hundreds of
    // battles and every win writes a save.
    std::string scratch = "campaign_probe_save.txt";
    if (char* base = SDL_GetBasePath()) {
        scratch = std::string(base) + scratch;
        SDL_free(base);
    }
    lanebattle::setSavePath(scratch);

    scenes.push(lanebattle::makeTitleScene());
    driver.step();
    lanebattle::campaignOf(world).stagesUnlocked = stage + 1;
    lanebattle::campaignOf(world).heroPath = strategy.path;
    lanebattle::campaignOf(world)
        .perks[static_cast<int>(lanebattle::Perk::Granary)] = strategy.granary;

    // A strategy carries what it sends.
    //
    // Derived from the cycle rather than declared separately, because two
    // copies of "which units is this player using" is exactly the kind of pair
    // that drifts — and a strategy whose cycle names a unit its loadout does
    // not carry would silently send nothing, which is how three columns of
    // this table once measured a game that was not being played.
    {
        lanebattle::Campaign& campaign = lanebattle::campaignOf(world);
        for (int slot = 0; slot < lanebattle::kLoadoutSlots; ++slot) {
            campaign.loadout[slot] = -1;
        }
        int slot = 0;
        for (int kind : strategy.cycle) {
            if (slot >= lanebattle::kLoadoutSlots) break;
            if (lanebattle::inLoadoutOf(campaign, kind)) continue;
            campaign.loadout[slot++] = kind;
        }
    }
    driver.tap(SDL_SCANCODE_SPACE);
    driver.step(2);
    driver.tap(SDL_SCANCODE_RETURN);
    driver.step(2);

    Session* session = lanebattle::findSession(world);
    if (!session) return Outcome{};

    // The bar slot for a roster index, since the keys are bound to slots.
    auto keyFor = [](int kind) -> SDL_Scancode {
        static const SDL_Scancode keys[] = {SDL_SCANCODE_1, SDL_SCANCODE_2,
                                            SDL_SCANCODE_3, SDL_SCANCODE_4,
                                            SDL_SCANCODE_5};
        for (int slot = 0; slot < lanebattle::visibleButtonCount(); ++slot) {
            if (lanebattle::kindForButton(slot) == kind) return keys[slot];
        }
        return SDL_SCANCODE_UNKNOWN;
    };

    int index = 0;
    SDL_Scancode holding = SDL_SCANCODE_UNKNOWN;
    bool heroSent = false;
    int cannonWait = 0;
    int shotsFired = 0;

    const int totalFrames = 60 * kMaxSeconds;
    int frame = 0;
    for (; frame < totalFrames; ++frame) {
        if (holding != SDL_SCANCODE_UNKNOWN) {
            driver.release(holding);
            holding = SDL_SCANCODE_UNKNOWN;
        }

        // Economy BEFORE units, and out of plain affordability.
        //
        // This ran after the spending loop and reserved a further 140 gold on
        // top of the price, and the result was an ECON column identical to
        // MIXED down to the second: the unit cycle spends whatever it can see,
        // so the purse never reached the threshold and the upgrade was never
        // once bought. A strategy that quietly does nothing looks exactly like
        // a system that does not matter, which is the wrong conclusion to draw
        // about a system.
        //
        // Buying first is also the honest version of the trade: an income
        // upgrade costs you the units you would have sent while paying for it.
        //
        // Bought only while the field is held, which is what separates using
        // the economy from gambling on it. Buying greedily the moment it is
        // affordable measures WORSE than never buying at all — three stages
        // against five — because the units not sent while paying for it lose
        // the line, and in this game a lost line does not come back. That is a
        // real property of the design and worth keeping in view; it is just
        // not what a competent player does.
        // Bought whenever affordable, which is the OPENING the upgrade is now
        // priced for.
        //
        // The old rule was "only while the field is held", and it was correct
        // for the old price: 120 gold for +4/s paid back in thirty seconds
        // against battles decided in sixty, so buying early cost you the line
        // and buying late arrived after the outcome. Greedy measured WORSE
        // than never buying — three stages against five.
        //
        // At 80 for +5 the payback is sixteen seconds, which is inside the
        // window where it can still change the battle. Modelling a player who
        // waits until they are already winning would now be modelling the
        // wrong player, and would measure the upgrade as inert for a reason
        // that no longer applies.
        // Two levels early, then stop — which is what a person does and what
        // neither of the previous models did.
        //
        // "Whenever affordable" never stops: it keeps buying the third and
        // fourth level at rising prices right through the fight, so the player
        // is permanently one soldier poorer and measures WORSE than never
        // buying at all. "Only while winning" never starts in time. Both
        // measured the upgrade as inert, for opposite reasons, and neither is
        // a player.
        // Back to "only while the field is held", which is the best of the
        // three models measured — four stages against two for either flavour
        // of buying early. It is not that this player is clever; it is that
        // in-battle income cannot be bought without losing the line, so the
        // least-bad player is the one who barely buys it.
        //
        // The economy that WORKS is GRANARY, a permanent perk bought between
        // battles, and the GRAIN column measures that instead.
        if (strategy.economy &&
            lanebattle::frontLineX(world, true) > lanebattle::kWorldWidth / 2.0f) {
            const int income = static_cast<int>(lanebattle::Upgrade::Income);
            const float cost =
                lanebattle::upgradeCost(income, session->upgrades[income]);
            if (session->gold >= cost) {
                session->gold -= cost;
                ++session->upgrades[income];
            }
        }

        // The cannon, shelled at whatever the enemy has pushed to.
        //
        // Left out of the first version of this probe, and its absence was
        // doing real damage to the conclusions: the sweep said nothing below
        // the hero could answer an enemy that fields archers, which would have
        // been a finding about the game rather than about a player who never
        // touched the one system built for exactly that job. A probe that
        // ignores a mechanic measures a game that does not have it.
        //
        // Out of surplus only — a shot is 30 gold that could have been most of
        // a runner, and shelling instead of fielding is how a human loses with
        // it.
        // `cannonWait` is not politeness, it is correctness. A tap re-issued
        // every frame never releases — the harness sees the button still
        // wanted and holds it down — and the game only fires a shot on the
        // RELEASE. Clicking every frame therefore fires nothing at all, and
        // produced a GUNS column identical to MIXED down to the second: the
        // same shape of silent no-op the ECON column had, and the same wrong
        // conclusion waiting behind it.
        if (cannonWait > 0) --cannonWait;

        // Two things kept this column at ZERO shots across three campaigns, and
        // both were the probe rather than the game:
        //
        //   * it ran AFTER the unit-spending loop and reserved 120 gold on top
        //     of the 30-gold shell, so the purse never reached the threshold.
        //     Exactly the bug the ECON column had, in the same place, for the
        //     same reason.
        //   * it aimed at the ENEMY front line, which is frequently off screen
        //     — the camera follows YOURS — and a click outside the window is
        //     not a click. Real players drag the view; the probe cannot, so it
        //     aims just ahead of its own line instead, which is where the enemy
        //     is whenever the two are engaged and is always on screen.
        //
        // Reserve is one soldier now rather than two. Shelling instead of
        // fielding is still how a human loses with it, so the trade is kept —
        // it is just no longer set so high that the trade never comes up.
        if (strategy.cannon && cannonWait == 0 &&
            session->cannonCooldown <= 0.0f &&
            session->gold >= lanebattle::kCannonCost + 60.0f) {
            if (Camera* camera = lanebattle::findCamera(world)) {
                const float mine = lanebattle::frontLineX(world, true);
                const float from = lanebattle::kCastleMargin +
                                   lanebattle::kCastleWidth / 2.0f;
                const float aim = mine + 60.0f;
                if (aim - from <= lanebattle::kCannonRange) {
                    const float screenX = aim - camera->x;
                    if (screenX > 4.0f &&
                        screenX < static_cast<float>(lanebattle::kWindowWidth) - 4.0f) {
                        driver.clickAt(static_cast<int>(screenX), 410);
                        cannonWait = 3;  // let the press become a release
                    }
                }
            }
        }
        // Reads FORWARD through the cycle for something sendable rather than
        // waiting on whatever came next — otherwise a cycle of two soldiers
        // spends most of its time waiting out the first one's cooldown, which
        // measures a bad player rather than a bad game.
        const int length = static_cast<int>(strategy.cycle.size());
        for (int step = 0; step < length; ++step) {
            const int kind = strategy.cycle[(index + step) % length];
            if (session->spawnCooldowns[kind] > 0.0f) continue;
            if (session->gold < lanebattle::unitKind(kind).cost) continue;

            const SDL_Scancode key = keyFor(kind);
            if (key == SDL_SCANCODE_UNKNOWN) continue;
            holding = key;
            driver.hold(holding);
            index = (index + step + 1) % length;
            break;
        }

        // The hero goes in once the fight is actually joined, which is what a
        // player does: sending it into an empty field wastes it.
        if (strategy.hero && !heroSent) {
            const float mine = lanebattle::frontLineX(world, true);
            const float theirs = lanebattle::frontLineX(world, false);
            if (theirs - mine < 500.0f) {
                driver.tap(SDL_SCANCODE_H);
                heroSent = true;
            }
        }


        // RAGE is the unaimed one, so it needs no click and no camera maths.
        // Cast on cooldown once the lines have met, which is when it is worth
        // anything.
        if (strategy.spells &&
            session->mana >= lanebattle::spellKind(2).manaCost) {
            const float mine = lanebattle::frontLineX(world, true);
            const float theirs = lanebattle::frontLineX(world, false);
            if (theirs - mine < 300.0f) driver.tap(SDL_SCANCODE_C);
        }

        const float cooldownBefore = session->cannonCooldown;
        driver.step();
        // A shot is counted when the cooldown goes up, which only happens
        // inside fireAt. Counting clicks instead would count intent, and the
        // whole question here is whether intent turned into a shell.
        if (session->cannonCooldown > cooldownBefore) ++shotsFired;

        if (session->gameOver) break;
    }

    Outcome outcome;
    outcome.shots = shotsFired;
    outcome.upgrades = session->upgrades[static_cast<int>(lanebattle::Upgrade::Income)];
    outcome.seconds = static_cast<float>(frame) / 60.0f;
    outcome.result = session->gameOver ? (session->playerWon ? 1 : -1) : 0;

    for (int side = 0; side < 2; ++side) {
        const Entity castle = lanebattle::findCastle(world, side == 0);
        float health = 0.0f;
        if (castle != kInvalidEntity) {
            if (Castle* c = world.getComponent<Castle>(castle)) health = c->health;
        }
        (side == 0 ? outcome.ownCastle : outcome.enemyCastle) = health;
    }
    return outcome;
}

const char* verdict(const Outcome& outcome) {
    if (outcome.result == 1) return "WIN ";
    if (outcome.result == -1) return "loss";
    return "draw";
}

// Plays a stage that is not in the table: an income, a castle and a
// composition, assembled here. Used by the threshold sweep below.
Outcome playCustom(const Strategy& strategy, float income, float castle,
                   const char* composition, int waveSize) {
    // A scratch stage is appended to the roster through the same data path the
    // game uses, so the sweep measures the real loader rather than a private
    // back door.
    std::string text = "[stage]\nname = SWEEP\nenemy_income = ";
    text += std::to_string(income);
    text += "\nenemy_castle_health = " + std::to_string(static_cast<int>(castle));
    text += "\nwave_size = " + std::to_string(waveSize);
    text += "\ncomposition = " + std::string(composition) + "\n";

    std::string path = "campaign_probe_sweep.txt";
    if (char* base = SDL_GetBasePath()) {
        path = std::string(base) + path;
        SDL_free(base);
    }
    {
        std::FILE* file = std::fopen(path.c_str(), "w");
        if (!file) return Outcome{};
        std::fputs(text.c_str(), file);
        std::fclose(file);
    }

    lanebattle::resetBalance();
    lanebattle::loadBalance(lanebattle::kBalancePath);
    lanebattle::loadBalance(path);  // stages REPLACE, so this is the only one

    return play(0, strategy);
}

// The highest enemy income each strategy can still beat, for one composition.
//
// This is the measurement the stage table actually needs and the one I kept
// trying to guess. Outcomes here are a step function rather than a curve — a
// stage is either held untouched or lost outright, with almost nothing in
// between — so "make stage six a bit harder" is not a thing that can be aimed.
// Finding where each strategy's step sits, and then placing stages between
// different strategies' steps, is.
void sweep(const std::vector<Strategy>& strategies, const char* composition,
           int waveSize) {
    std::printf("  composition %-12s wave %d\n", composition, waveSize);
    std::printf("    %-6s  beats up to\n", "player");

    for (const Strategy& strategy : strategies) {
        float best = 0.0f;
        for (float income = 0.40f; income <= 3.01f; income += 0.10f) {
            // Castle health rides with income on the same slope the shipped
            // table uses, so one number moves the difficulty rather than two.
            const float castle = 300.0f + income * 850.0f;
            const Outcome outcome =
                playCustom(strategy, income, castle, composition, waveSize);
            if (outcome.result == 1) best = income;
            else break;  // past the step; nothing above it will win either
        }
        if (best <= 0.0f) {
            std::printf("    %-6s  nothing\n", strategy.name);
        } else {
            std::printf("    %-6s  %.2f\n", strategy.name, best);
        }
    }
    std::printf("\n");
}

}  // namespace

int main(int argc, char** argv) {
    const bool detail = argc > 1 && std::strcmp(argv[1], "--detail") == 0;

    lanebattle::resetBalance();
    lanebattle::loadBalance(lanebattle::kBalancePath);

    const std::vector<Strategy> strategies = {
        {"NAIVE", {kSoldier}},
        {"MIXED", {kSoldier, kSoldier, kArcher}},
        {"AIR",   {kSoldier, kArcher, kGriffin}},
        {"ECON",  {kSoldier, kSoldier, kArcher}, false, false, true},
        // The same army walking in with two levels of GRANARY — the economy
        // moved out of the battle and into the campaign, where buying it does
        // not cost the soldier that holds the line.
        {"GRAIN", {kSoldier, kSoldier, kArcher}, false, false, false, false, 0, 2},
        {"GUNS",  {kSoldier, kSoldier, kArcher}, false, false, false, true},
        // The three roles the loadout added, each carried in place of
        // something the MIXED column relies on. If a new unit cannot get a
        // column near MIXED's it is not a choice, it is a trap.
        {"OGRE",  {kOgre, kSoldier, kArcher}},
        {"PIKE",  {kSoldier, kPikeman, kArcher}},
        {"BALL",  {kSoldier, kSoldier, kBallista}},
        // The whole point of a loadout, in one column: a player who brings the
        // anti-air AND the siege piece instead of two of one. If carrying both
        // does not beat carrying either, then four slots are decoration.
        {"COMBO", {kSoldier, kSoldier, kPikeman, kBallista}},
        {"WARDN", {kSoldier, kSoldier, kArcher}, true, false, false, false,
         static_cast<int>(lanebattle::HeroPath::Warden)},
        {"FALCN", {kSoldier, kSoldier, kArcher}, true, false, false, false,
         static_cast<int>(lanebattle::HeroPath::Falconer)},
        {"CHAPL", {kSoldier, kSoldier, kArcher}, true, false, false, false,
         static_cast<int>(lanebattle::HeroPath::Chaplain)},
        {"FULL",  {kSoldier, kArcher, kGriffin}, true, true, true, true,
         static_cast<int>(lanebattle::HeroPath::Falconer)},
    };

    if (argc > 1 && std::strcmp(argv[1], "--sweep") == 0) {
        std::printf(
            "\nThreshold sweep - the highest enemy income each player still\n"
            "beats, per composition. Place a stage between two rows and it\n"
            "asks for the capability that separates them.\n\n");

        // SOLO is a diagnostic, not a player: the hero and nothing else, no
        // units bought at all. If it wins, the hero is not a swing in a battle
        // — it is a battle.
        std::vector<Strategy> withSolo = strategies;
        withSolo.push_back(Strategy{"SOLO", {}, true, false, false});

        // Three compositions, chosen for the question actually being asked:
        // where does a stage that FIELDS FLYERS have to sit for anti-air to be
        // worth carrying, and does putting it mid-campaign leave the hero
        // paths where they were?
        //
        // The old five-composition list swept ground armies this game already
        // understands. A sweep is expensive — every strategy against every
        // income until it loses — so it is worth pointing at the open question
        // rather than re-confirming settled ones.
        sweep(withSolo, "1,1,0", 3);      // the ground baseline, for reference
        sweep(withSolo, "1,1,2", 3);      // an enemy that shoots back
        sweep(withSolo, "1,2,2,1", 4);    // archer-heavy: the BALLISTA's niche
        return 0;
    }

    const int stages = lanebattle::stageCount();

    std::printf("\nCampaign probe - %d stages, %d ways of playing each\n\n",
                stages, static_cast<int>(strategies.size()));

    // The table it actually loaded, printed before anything is measured.
    //
    // Assets are COPIED next to the binary at build time, so editing
    // assets/lanebattle/units.txt and re-running this without rebuilding
    // measures the previous table and prints a completely believable result.
    // That happened, repeatedly, and cost several rounds of "the change had no
    // effect" — which is the same failure mode mutate.bat guards against by
    // checking its mutation applied. Printing the inputs is that guard.
    std::printf("  loaded table (rebuild if this is not what you just edited)\n");
    std::printf("    %-16s %6s %8s %5s  %s\n", "stage", "income", "castle",
                "wave", "sends");
    for (int stage = 0; stage < stages; ++stage) {
        const lanebattle::StageKind& kind = lanebattle::stageKind(stage);
        std::printf("    %-16s %6.2f %8.0f %5d  %s\n", kind.name,
                    kind.enemyIncome, kind.enemyCastleHealth, kind.waveSize,
                    kind.composition);
    }
    const int hero = lanebattle::heroKindIndex();
    if (hero >= 0) {
        std::printf("    hero %.0f hp, %.0f damage\n\n",
                    lanebattle::unitKind(hero).health,
                    lanebattle::unitKind(hero).damage);
    }

    std::printf("  stage  %-16s", "name");
    for (const Strategy& strategy : strategies) {
        std::printf(" %-6s", strategy.name);
    }
    std::printf("\n  -----  ----------------");
    for (std::size_t i = 0; i < strategies.size(); ++i) std::printf(" ------");
    std::printf("\n");

    std::vector<int> winsPerStrategy(strategies.size(), 0);

    for (int stage = 0; stage < stages; ++stage) {
        std::printf("  %5d  %-16s", stage + 1, lanebattle::stageKind(stage).name);

        std::vector<Outcome> outcomes;
        for (std::size_t s = 0; s < strategies.size(); ++s) {
            const Outcome outcome = play(stage, strategies[s]);
            outcomes.push_back(outcome);
            if (outcome.result == 1) ++winsPerStrategy[s];
            std::printf(" %-6s", verdict(outcome));
        }
        std::printf("\n");

        if (detail) {
            std::printf("         %-16s", "  seconds");
            for (const Outcome& outcome : outcomes) {
                std::printf(" %-6.0f", outcome.seconds);
            }
            std::printf("\n         %-16s", "  your castle");
            for (const Outcome& outcome : outcomes) {
                std::printf(" %-6.0f", outcome.ownCastle);
            }
            // Shots and upgrades are printed because two columns of this table
            // were once identical to MIXED for the same reason: the strategy
            // never actually did the thing it was named after. A count of what
            // happened is the difference between measuring a system and
            // measuring a typo.
            std::printf("\n         %-16s", "  shots fired");
            for (const Outcome& outcome : outcomes) {
                std::printf(" %-6d", outcome.shots);
            }
            std::printf("\n         %-16s", "  income lvls");
            for (const Outcome& outcome : outcomes) {
                std::printf(" %-6d", outcome.upgrades);
            }
            std::printf("\n");
        }
    }

    std::printf("\n  stages won      ");
    for (std::size_t s = 0; s < strategies.size(); ++s) {
        std::printf(" %-6d", winsPerStrategy[s]);
    }
    std::printf("\n\n");

    std::printf(
        "  What to look for. Down each column, wins should turn into losses,\n"
        "  and the turn should come LATER for the columns further right. A row\n"
        "  that is all wins asks the player nothing. A row that is all losses\n"
        "  cannot be beaten. A column that never loses means the part of the\n"
        "  game it represents is never needed.\n\n");

    return 0;
}
