#pragma once
// ---------------------------------------------------------------------------
// LaneBattle.h — a castle-versus-castle lane battler, in the genre of Cartoon
// Wars and Age of War.
//
// The whole game is one loop: gold accrues, you spend it to send units right,
// they walk until they meet an enemy, they fight, and whoever's castle falls
// first loses. Unit types, upgrades and spells are all decoration on that.
//
// Slice 1 built only that loop, on purpose, and needed NO new engine code at
// all: Transform and Velocity for walking, MovementSystem to do the walking,
// Sprite to draw, Lifetime for the death debris, destroyLater so a unit can
// die in the middle of the loop that killed it, and the scene stack for
// title/play/pause/game-over.
//
// Slice 2 widened the battlefield to two and a half screens, which is the
// first time in this project that world coordinates and screen coordinates
// have differed. That pulled exactly one thing out of the engine — the
// world-to-screen rule itself, moved into engine/View.h so it could be tested
// without a window — and everything else it needed was already there.
//
// As always, only what a test or main() needs is exposed.
// ---------------------------------------------------------------------------

#include <memory>
#include <string>
#include <vector>

#include "engine/Audio.h"
#include "engine/Resources.h"
#include "engine/Components.h"
#include "engine/ECS.h"
#include "engine/Scene.h"

namespace lanebattle {

// --- Layout ----------------------------------------------------------------

// The window is the VIEW. The battlefield is wider than it, which is the whole
// point of this slice: until now every game in this project fit its world into
// its window, so world coordinates and screen coordinates were the same number
// and the renderer's camera was never really exercised.
constexpr int kWindowWidth = 960;
constexpr int kWindowHeight = 540;

constexpr float kWorldWidth = 2400.0f;  // two and a half screens

// Everything stands on this line. A lane battler is really a one-dimensional
// game with a picture drawn around it: only x ever decides anything.
constexpr float kGroundY = 420.0f;

constexpr float kCastleWidth = 70.0f;
constexpr float kCastleHeight = 130.0f;
constexpr float kCastleMargin = 30.0f;
constexpr float kCastleHealth = 800.0f;

// --- The roster ------------------------------------------------------------
//
// Slices 1 and 2 had one unit type, and measuring a whole battle showed why
// that was fatal: with one type, the only decision is *when* to spend, both
// sides trade evenly, and a player who mirrors the opponent deadlocks the
// field forever. Three types make the question "when AND what", which is the
// smallest change that puts a real decision in the loop.
//
// This is a table, deliberately. Every number that balances the game is in one
// place and in rows rather than scattered through the rules, which is how the
// genre does it — the reference game keeps its equivalent in files exported
// from a spreadsheet. When there are enough rows that recompiling to tweak one
// hurts, the table moves to a file and nothing else has to change. That is a
// later slice, and it is not this one: six numbers do not hurt yet.
struct UnitKind {
    const char* name;
    float cost;
    float health;
    float damage;
    float range;         // how close before it stops to fight
    float attackDelay;   // seconds between blows
    float speed;         // pixels per second

    // How long THIS kind's button is unavailable after sending one. Each kind
    // has its own, which is what the genre does and is not the small detail it
    // looks like.
    //
    // With a single shared cooldown the only limit on spending was gold, so a
    // banked purse went entirely into whichever unit was best and composition
    // was a preference rather than a constraint. Per-kind timers mean a large
    // purse CANNOT be spent on one type: to use it you have to send something
    // else, which is the whole point of having more than one.
    //
    // They mostly do not bind at base income — a soldier takes four seconds to
    // afford and two to recharge — and start mattering exactly when the player
    // has money to burn, which is when the interesting decision exists.
    float cooldown;

    float width;
    float height;

    // --- The sky ------------------------------------------------------------
    //
    // Two flags that between them end the one-dimensional assumption this game
    // has rested on since its first slice.
    //
    // Everything until now decided on x alone: who is ahead, who is in reach,
    // who blocks whom. Altitude adds a second question that distance cannot
    // answer — *can* this unit even be attacked by that one — and it is a
    // category, not a measurement. A soldier standing directly beneath a
    // griffin is as close as anything can be and still cannot touch it.
    //
    // Range stays horizontal on purpose. Measuring it as a real 2D distance
    // was tried on paper and is worse: an archer's 135 would shrink to about
    // 28 pixels of horizontal reach against something 130 above it, which
    // makes the one unit that answers flyers unable to answer them.
    bool flying;   // lives in the sky, and only `hitsAir` units can reach it
    bool hitsAir;  // can attack things in the sky

    unsigned char leftR, leftG, leftB;     // your colours
    unsigned char rightR, rightG, rightB;  // theirs

    // --- Artwork, when there is any -----------------------------------------
    //
    // Empty `sheet` means "draw the coloured block and the stick figure", which
    // is every unit today: this game loads no images at all. Naming a sheet
    // switches that unit to frame animation with no code change — which is the
    // whole point of putting it here rather than in the renderer.
    //
    // A sheet is one PNG with the frames of a walk cycle laid out left to
    // right, resolved against the executable like every other asset. Facing is
    // handled by `Sprite.flipX`, so you draw the unit walking ONE way and the
    // opponent's copy is mirrored for free — do not draw both.
    //
    // This is slice 5b, which sat deferred for ten slices because building a
    // frame animator for art that does not exist is speculative work. The
    // engine half is real and tested; this half is the data path, so that art
    // arriving is a line in units.txt rather than a change to the game.
    const char* sheet;
    int frameWidth;
    int frameHeight;
    int frameCount;
    float frameSeconds;
};

// How high the sky is. Flyers sit here instead of standing on kGroundY.
constexpr float kFlyingY = 244.0f;

// The roster's hard ceiling. A data file may add unit types, but each side
// needs one cooldown timer per kind stored on the Session, and an unbounded
// component is a worse thing to own than a documented limit.
constexpr int kMaxUnitKinds = 16;

// How many unit types you may bring into one battle. You own every type and
// carry this many — see "The loadout" further down for why four.
constexpr int kLoadoutSlots = 4;

// --- The hero --------------------------------------------------------------
//
// One summon per battle, and if it falls it stays fallen until the stage is
// finished or restarted. No respawn, no second chance.
//
// That single rule is what makes it interesting. A hero you can re-summon is
// an ability on a cooldown, and the only question is whether it is off
// cooldown. A hero you get once is a decision: send it early and it fights
// alone and dies for very little, hold it too long and you may have already
// lost the field it was meant to win.
constexpr const char* kHeroName = "HERO";

// The roster index of the hero, or -1 if a data file removed it. Found by
// name, so reordering the roster or adding units in front of it cannot
// silently promote a soldier to a champion.
int heroKindIndex();

// Marks the one hero entity. Used to notice when it dies.
struct Hero {};

// The hero button's position is defined with the spawn bar it must not
// overlap, further down.
bool heroButtonHit(float screenX, float screenY);

// The three roles the genre is built on, and the shape of each:
//
//   RUNNER  — cheap and fast, dies to anything, wins on gold-for-damage.
//   SOLDIER — the slice-1 unit. Beats a runner one-to-one and loses to two.
//   ARCHER  — outranges everything, but cannot survive being reached. Useless
//             alone; the reason to own a front line.
//
// These numbers are a starting point that was then measured rather than
// argued about: see the matchup probe in the tests.
// These are the DEFAULTS, not the roster. `assets/lanebattle/units.txt`
// overrides any of them and may add more; a missing or broken file leaves
// these exactly as they are, which is why the game still runs from a bare
// build directory and why the tests are deterministic without touching a
// filesystem. Read the live roster through `unitKind()` — never through this.
constexpr UnitKind kDefaultUnitKinds[] = {
    // name      cost   hp     dmg   range  delay  speed  cool   w      h     fly   air    yours          theirs
    {"RUNNER",   35.0f, 60.0f,  8.0f,  30.0f, 0.45f, 150.0f, 1.1f, 16.0f, 26.0f, false, false, 150, 215, 255, 255, 175, 150, nullptr, 0, 0, 0, 0.0f},
    {"SOLDIER",  60.0f, 110.0f, 14.0f, 34.0f, 0.60f,  95.0f, 1.9f, 24.0f, 36.0f, false, false, 110, 190, 240, 235, 130, 110, nullptr, 0, 0, 0, 0.0f},
    {"ARCHER",   95.0f, 55.0f,  20.0f, 135.0f, 0.95f,  70.0f, 3.0f, 18.0f, 34.0f, false, true,   90, 140, 210, 200,  95, 130, nullptr, 0, 0, 0, 0.0f},
    // The griffin. The only thing on this list that flies, and the reason the
    // archer stopped being optional: nothing else in the roster can touch it.
    {"GRIFFIN", 115.0f, 95.0f,  17.0f,  40.0f, 0.70f, 112.0f, 3.6f, 26.0f, 24.0f, true,  true,  200, 170, 250, 250, 160, 200, nullptr, 0, 0, 0, 0.0f},

    // --- Three more, so that the loadout has something to choose between ----
    //
    // A loadout is meaningless with four unit types and four slots. The
    // reference game's proportions are the argument for going the other way:
    // a 14 KB unit table behind 2,161 unit frames, against our four sellable
    // rows. You are meant to own more than you can bring.
    //
    // Each of these is a ROLE the roster did not have, and each is answerable,
    // which is the rule the griffin established — a unit nothing can counter is
    // the hero bug again in a cheaper costume.
    //
    //   PIKEMAN   the budget answer to the sky. Reaches air like an archer but
    //             at melee range, with a soldier's build. Buying it instead of
    //             an archer trades reach for a body that survives contact.
    //   OGRE      the wall. Four soldiers of health for under three soldiers of
    //             gold, and slow enough that it arrives after the fight starts.
    //             Ground only, so a griffin walks all over it.
    //   BALLISTA  the longest reach in the game and the softest body behind it.
    //             Hits air. Useless without a line in front, which is the
    //             archer's lesson taken further.
    {"PIKEMAN",  55.0f, 105.0f, 13.0f,  46.0f, 0.70f,  88.0f, 1.8f, 20.0f, 34.0f, false, true,  120, 205, 190, 225, 145, 120, nullptr, 0, 0, 0, 0.0f},
    {"OGRE",    160.0f, 280.0f, 30.0f,  38.0f, 1.10f,  62.0f, 4.5f, 34.0f, 46.0f, false, false, 140, 160, 235, 235, 140, 140, nullptr, 0, 0, 0, 0.0f},
    {"BALLISTA",100.0f, 60.0f,  44.0f, 185.0f, 1.50f,  52.0f, 3.2f, 26.0f, 30.0f, false, false, 170, 195, 225, 230, 175, 155, nullptr, 0, 0, 0, 0.0f},
    // The hero. A roster row like any other, so it walks, fights, queues,
    // animates and is targeted by the same code as everything else — but it is
    // NOT sold from the spawn bar, does not take a number key, and never
    // appears in a stage's composition. Those exclusions are what make it a
    // hero rather than an expensive soldier.
    //
    // GROUND ONLY, and that is the whole design of the hero tree rather than a
    // nerf. This row used to reach the sky "because a champion that loses to a
    // bird is not much of one", and the consequence was a unit that was better
    // than every other unit at everything, which measurement caught: it beat
    // every composition at every income the probe could build.
    //
    // Reaching the sky is now the FALCONER path — something bought, at the
    // price of the health the other paths keep. A baseline that already had it
    // would have made specialising a downgrade, and a choice that costs you
    // something you already had is not a choice anybody makes twice.
    // Retuned down from 620 health and 46 damage, which measurement said was
    // not a swing but a win button: with the old numbers the hero beat every
    // composition the probe could build at every income it could reach, and a
    // campaign it cannot lose is a campaign the rest of the game is decoration
    // on. At 360 and 32 it is still worth roughly three soldiers of health and
    // three of damage, free and instant — a decision about WHEN, which is what
    // it was always meant to be — and the probe now finds stages it does not
    // save you from.
    {"HERO",      0.0f, 360.0f, 32.0f,  44.0f, 0.50f,  88.0f, 0.0f, 30.0f, 48.0f, false, false, 250, 235, 140, 255, 120,  90, nullptr, 0, 0, 0, 0.0f},
};
constexpr int kDefaultUnitKindCount =
    static_cast<int>(sizeof(kDefaultUnitKinds) / sizeof(kDefaultUnitKinds[0]));

// The live roster. A file may override rows and append new ones, so both the
// contents and the count are runtime values now.
const UnitKind& unitKind(int kind);  // clamped; never indexes off the end
int unitKindCount();

// Reads `path` (resolved against the executable) over the top of the roster.
// Returns false if the file was not found, in which case nothing changed and
// the defaults stand. Each `[unit]` section either updates the row whose name
// it matches or appends a new one, and any field it omits keeps its previous
// value — so a file that only wants to make archers cheaper says exactly that
// and nothing else.
bool loadBalance(const std::string& path);

// Puts the roster back to the compiled-in defaults. Exists for the tests: the
// roster is global mutable state, and a test that loads a file must not change
// the meaning of the one that runs after it.
void resetBalance();

// The path the game loads at startup.
constexpr const char* kBalancePath = "assets/lanebattle/units.txt";

// The widest and tallest of them, for the few places that need a bound before
// knowing which kind they are dealing with.
constexpr float kMaxUnitWidth = 24.0f;

// --- Tuning ----------------------------------------------------------------

constexpr float kStartingGold = 150.0f;
constexpr float kGoldPerSecond = 14.0f;
// The floor under every unit's own cooldown, so one keypress can never become
// an army even if a data file sets a cooldown of zero.
constexpr float kMinSpawnCooldown = 0.35f;

// Killing something pays, as a fraction of what it cost its owner.
//
// This is the mechanic the first three attempts at this game were missing, and
// without it the design cannot work. With income as the only source of gold,
// two competent players earn identically no matter what happens on the field,
// so a won fight buys nothing and the front line returns to the middle — which
// is exactly what measuring slices 1, 2 and the first cut of 3 all showed:
// good play drew, bad play lost, and nothing in between existed.
//
// Paying for kills makes a favourable trade compound. Killing a 95-gold archer
// with two 35-gold runners is now worth doing twice, and an advantage on the
// field turns into an advantage in the purse, which turns back into more
// units. That loop is what lets skill decide a battle instead of arithmetic.
constexpr float kKillRewardFraction = 0.45f;

// --- The castle cannon -----------------------------------------------------
//
// Until now there was nothing to do between spending decisions but watch. The
// cannon is the genre's answer: a slow, aimed shot that rewards paying
// attention to where the fighting actually is.
//
// Aiming is a click on the field, which is the first thing in this project to
// need `screenToWorld`. That inverse was written in slice 4 and had no caller
// at all — the spawn bar is screen-space, so it hit-tests against the cursor
// directly. A shot is different: the click is in window pixels and the ground
// it lands on is up to a screen and a half away.
//
// A click that turns into a drag is not a shot. Press, move more than a few
// pixels, and it is a camera drag; press and release in about the same place
// and it is a shot. That is the standard tap-versus-drag rule and it is what
// lets one button do both without a modifier key.
// A shot COSTS GOLD, which is the whole reason this is a decision rather than
// a wall.
//
// The first version was free, on a cooldown, permanent and perfectly aimed —
// and measuring it showed the consequence immediately: a mixed army that won
// in 195 seconds lost in 247, and taking the cannon's damage away restored the
// old result exactly. Free defensive damage that never runs out makes the last
// stretch in front of a castle a killing field nobody can cross, so defending
// beats attacking for both sides and every game ends 800-800.
//
// Charging for it turns "how much damage does the cannon do" into "how much
// damage per gold", which is a number that can be compared against a unit and
// therefore balanced. A side that shells constantly fields a smaller army,
// which is exactly the trade that should exist.
// Priced, and that price is load-bearing. The RANGE was the bug.
//
// `campaign_probe` measured this cannon firing **zero shots** — every column,
// every stage, thirteen players, not once. So it was rebuilt, and the obvious
// rebuild was the genre's shape: free, cooldown-limited, aimed by hand,
// reaching the fighting instead of the gate.
//
// Free was measured twice and is wrong for THIS game, both times:
//
//   free, 760 reach   slice 8's stalemate exactly. Draws everywhere, including
//                     the last stage drawing for the only player who could
//                     previously win it.
//   free, whole field WORSE. Almost every column drawing on almost every stage.
//
// The second run is the informative one, because it killed the theory behind
// the first. The stalemate is not a geometry problem — it is not that a
// partial arc draws an uncrossable line on the map, which was the hypothesis
// and which full coverage should have removed. It is that **two guns firing
// forever erase both armies faster than either side can accumulate one.**
// Nobody is ever ahead, so nobody ever pushes, so the castles never fall.
//
// The 30-gold price is what stops that: a shell is army budget spent on
// something other than army, so shelling has an opportunity cost and both
// sides ration it. That was right all along. What was wrong was a reach of
// 420 from a castle at x=100 — it stopped at x=520 on a 2400-wide field, so
// the only enemy it could ever hit was one that had already crossed four
// fifths of the map, which is to say an enemy you had already lost to.
//
// So: keep the price, fix the reach. The mechanic the reference actually
// shares with this one is an aimed shot that reaches the FIGHT; its gun is
// free because its battlefield is a single screen and its economy is not
// ours. Copying that number rather than that idea is what the two runs above
// cost.
// Twelve, not thirty, and the arithmetic is the argument.
//
// With the reach fixed the gun finally fired — 36 shots on one stage, 89 on
// another — and the GUNS column got WORSE, from four stages won to two. The
// shells were not missing; they were being paid for at a terrible rate.
//
// A shell is 30 gold for about 50 damage once, counting the blast. A soldier
// is 60 gold for 110 health it soaks AND roughly 23 damage a second for as
// long as it survives. Per gold, the soldier is the better part of ten times
// the weapon. Ninety shells is two and a half thousand gold — forty soldiers —
// spent on something worth four.
//
// So the price has to sit where shelling is a real option rather than a
// self-inflicted wound. Twelve gold every 3.2 seconds is 3.75 gold a second
// against a 14/s income: about a quarter of your earnings, which is a tax you
// can feel and can choose to stop paying. That is the rhythm the genre has,
// and it is what "free with a cooldown" was reaching for — it is just that
// FREE, measured twice, erases both armies and ends in a draw.
constexpr float kCannonCost = 12.0f;
constexpr float kCannonDamage = 26.0f;
constexpr float kCannonBlastRadius = 46.0f;
constexpr float kCannonCooldown = 3.2f;   // seconds between shots

// Deliberately less than half the distance to the middle: the cannon defends
// the approach to your own castle, it does not contest the field. At 780 it
// covered a third of the world from each end and there was nowhere safe left.
// Far enough to reach the fighting.
//
// 420 from a castle at x=100 stopped at x=520, and the front line meets around
// the middle of a 2400-wide world. The gun could only ever hit an enemy that
// had already crossed four fifths of the map — which is to say, an enemy you
// had already lost to.
//
// Far enough to reach the fighting, which 420 never was.
//
// A castle sits at x=100 on a 2400-wide field and the front lines meet around
// the middle. 420 stopped at x=520; 1000 reaches x=1100 from your end and
// x=1300 from theirs, so the contested middle is inside both arcs and a shot
// can land where the battle actually is.
//
// This is the half of the rebuild that was genuinely broken. Reach is what
// decides whether the weapon can be USED; the price is what decides whether
// using it costs anything. Slice 8 fixed a stalemate by changing both at once
// and killed the weapon with the half it did not need to change.
//
// Safe against castle sniping regardless of reach: `explode` only damages
// UNITS, so a shell that lands on an enemy castle does nothing to it.
constexpr float kCannonRange = 1000.0f;
constexpr float kCannonFlightTime = 0.85f;
constexpr float kCannonGravity = 900.0f;  // pixels per second per second

// --- Spells ----------------------------------------------------------------
//
// Cast from MANA, which is its own pool and refills on its own — deliberately
// not from gold.
//
// Gold is already fought over by units, in-battle upgrades and cannon shots,
// and a fourth claimant would have made every spell a decision about whether
// to have an army. Mana cannot buy anything else, so a spell is never a
// sacrifice; the only question is which one and when, which is the question
// worth asking.
//
// Two of them are aimed and one is not. Arming an aimed spell takes over the
// next click on the field — the same click that otherwise fires the cannon —
// which is why the cannon and the spells share one input and one rule about
// who gets it.
enum class Spell { Meteor, Heal, Rage, Count };
constexpr int kSpellCount = static_cast<int>(Spell::Count);

struct SpellKind {
    const char* name;
    const char* hint;
    float manaCost;
    float radius;    // the area it covers; ignored when it is not aimed
    float power;     // damage, healing, or a damage multiplier
    float duration;  // seconds, for the ones that last
    bool aimed;
};

constexpr SpellKind kDefaultSpells[] = {
    {"METEOR", "Z",  55.0f, 120.0f, 70.0f, 0.0f, true},
    {"HEAL",   "X",  40.0f, 140.0f, 80.0f, 0.0f, true},
    {"RAGE",   "C",  70.0f,   0.0f,  1.6f, 8.0f, false},
};

const SpellKind& spellKind(int spell);

constexpr float kMaxMana = 100.0f;
constexpr float kManaPerSecond = 5.5f;
constexpr float kStartingMana = 40.0f;

// Where the spell buttons are, and which one is under a screen position.
constexpr float kSpellX = 690.0f;
constexpr float kSpellWidth = 254.0f;
constexpr float kSpellHeight = 30.0f;
constexpr float kSpellGap = 6.0f;

// The top of a castle, which is where the right-hand panel column has to stop.
//
// The panels are screen-space and the castles are not, so at one particular
// camera position — the far right, which is exactly where you look when you
// are attacking their gate — the enemy castle slid underneath the spell rows
// and lost the top quarter of itself behind three buttons. Nothing could see
// it: the castle was drawn correctly, at the right place, in the right layer,
// and a panel was on top of it, which is what panels are for.
//
// So the panel column is positioned FROM the castle rather than at a number
// that happened to look fine on an empty field.
constexpr float kCastleTopY = kGroundY - kCastleHeight;

constexpr float kSpellPanelHeight =
    kSpellCount * (kSpellHeight + kSpellGap) - kSpellGap;
constexpr float kSpellY = kCastleTopY - kSpellPanelHeight;

constexpr float spellTop(int index) {
    return kSpellY + static_cast<float>(index) * (kSpellHeight + kSpellGap);
}

int spellAt(float screenX, float screenY);

// --- In-battle upgrades ----------------------------------------------------
//
// Three things to spend gold on that are not units, so a full population cap
// stops being a reason to sit idle. Costs rise geometrically, which is what
// stops the right answer being "buy everything immediately".
//
// Both sides buy them. That symmetry has been load-bearing twice already: an
// opponent with a different economy made slice 1 unwinnable, and an opponent
// with no strategy made slice 3 a mirror. An opponent that cannot upgrade
// would lose every long game by construction.
// The roster is a runtime list because a data file may add unit types: the
// game treats every row the same way, so a fourth kind needs a table row and
// nothing else. Upgrades are not like that. Each one has its own rule written
// in code — INCOME changes a rate, WALLS heals a castle, SUPPLY raises a cap —
// and there is no generic "apply upgrade N". So their COUNT is fixed while
// their NUMBERS are data, and that is a distinction rather than an
// inconsistency: what a file can change is what the code treats uniformly.
enum class Upgrade { Income, Walls, Supply, Count };
constexpr int kUpgradeCount = static_cast<int>(Upgrade::Count);

struct UpgradeKind {
    const char* name;
    float baseCost;
    float costGrowth;  // multiplied in per level already bought
    float effect;      // per level: gold/second, castle health, or unit slots
};

constexpr UpgradeKind kDefaultUpgrades[] = {
    // Left exactly as it was, and that is a result rather than an oversight.
    //
    // This was cheapened twice trying to make the economy matter — 80 for +5,
    // then 45 — and both made the game WORSE, in a way worth writing down: the
    // OPPONENT buys these too, out of true surplus, without ever risking the
    // line it is holding. A player who buys INCOME loses the soldier that was
    // holding theirs. So cutting the price hands the AI an economy the player
    // still cannot safely take, and the naive on-ramp stage stopped being
    // winnable at all.
    //
    // The player's economy is GRANARY, a permanent perk bought between
    // battles. This one stays what it always was: something to spend a late
    // surplus on when the line is already held.
    {"INCOME", 120.0f, 1.65f,   4.0f},   // extra gold per second
    {"WALLS",  150.0f, 1.70f, 260.0f},   // extra castle health, healed on purchase
    {"SUPPLY", 200.0f, 1.85f,   3.0f},   // extra population slots
};

// The live upgrade table, which `assets/lanebattle/units.txt` may retune
// through `[upgrade]` sections exactly as it retunes units.
const UpgradeKind& upgradeKind(int upgrade);

// How many units one side may have on the field at once, before SUPPLY.
//
// This is the answer to "why isn't sending more always right?", and without it
// there is no answer: measuring slice 2 showed that banking two units' worth
// won, and banking six won barely faster, so there was nothing to think about
// past the first decision. A cap makes every slot spent an opportunity cost,
// which is what turns composition into a choice.
constexpr int kPopulationCap = 10;

// Units queue rather than standing inside one another. Without this the whole
// army piles onto the same pixel and fights as one enormous unit, which is
// what made massing unconditionally correct.
constexpr float kRankGap = 6.0f;             // clear space between queued units

// The opponent plays by exactly the same rules: the same purse, the same
// income, the same costs, the same cooldown. Its only advantage is that it
// never forgets to spend.
//
// It began as a free unit every three seconds, which quietly made the game
// unwinnable — the enemy out-produced the player by half again, and no amount
// of tuning the player's economy could fix an opponent that didn't have one.
// Symmetry costs nothing and makes the difficulty a single number to change.
constexpr float kEnemyIncomeMultiplier = 1.0f;

// --- Stages ----------------------------------------------------------------
//
// A campaign rather than one endless skirmish. Each stage is the same battle
// with the opponent's three levers set differently: how fast it earns, how
// much castle it has to chew through, and what it sends.
//
// That last one is what makes the stages feel different rather than merely
// harder. A stage fielding only soldiers is answered by archers behind a thin
// line; one fielding archers of its own has to be rushed before they set up.
// Turning up a difficulty number would have produced eight identical fights.
constexpr int kMaxComposition = 12;

struct StageKind {
    const char* name;
    float enemyIncome;        // multiplier on the base gold rate
    float enemyCastleHealth;
    int waveSize;             // units it banks for before spending
    const char* composition;  // roster indices, e.g. "1,1,2"
};

// These numbers were measured, not chosen. The first attempt ran 0.75 to 1.60
// on income and 600 to 1500 on castle health, which produced a campaign whose
// first stage a naive army could only draw and whose last three **no strategy
// could win at all**. Playing every stage with three different armies said so
// in one run; reading the table would never have.
// The COMPOSITION escalates as much as the numbers do, and that is not
// decoration — measuring showed it matters more than either dial.
//
// The second attempt raised income and castle health smoothly but let the
// compositions wander, and stage three came out a wall that nothing could beat
// while stage four was comfortable. Stage three fielded a lean "1,1,2" and
// stage four a "1,1,2,0" padded with a runner: a weak unit in the cycle
// spends gold that would otherwise have bought a soldier, so the higher
// income bought a worse army. Early stages are diluted on purpose; the last
// ones are lean.
// The FOURTH attempt, and the first one built from a measurement rather than
// from a guess that was then measured. `tests/campaign_probe.cpp` plays every
// stage seven ways and prints who beat what; these rows were placed between
// the thresholds it found.
//
// What the third attempt got wrong was not a number, it was the shape. Seven
// of its eight stages fell to a single unadapted army, and three whole systems
// — the sky, the economy, the cannon — were needed nowhere. The campaign had
// two gates in it and pretended to have eight.
//
// Each stage now asks for something, and the probe says so:
//
//     stage    asks for                     beaten by
//     1        nothing - learn the button   everyone
//     2-4      a mixed army                 MIXED and up
//     5        the sky                      AIR and up
//     6-7      the hero                     HERO and up
//     8        all of it                    FULL only
//
// Note how the difficulty moves. Stages 2-5 escalate on INCOME against one
// plain ground army; 6-8 hold income roughly still and escalate on
// COMPOSITION instead. That is not a stylistic choice — measuring says the
// composition dial is far the stronger of the two. Giving the enemy archers
// drops what a ground army can survive from about 1.4 income to about 0.6, a
// bigger swing than the entire income range of the campaign.
constexpr StageKind kDefaultStages[] = {
    {"THE BORDER",     0.40f,  480.0f, 2, "1,0"},
    {"RIVER CROSSING", 0.60f,  600.0f, 3, "1,1,0"},
    {"THE FOOTHILLS",  1.00f,  740.0f, 3, "1,1,0"},
    {"OLD ROAD",       1.25f,  860.0f, 3, "1,1,0"},
    // Air-HEAVY, and that word is the measurement rather than flavour. A
    // single griffin in a composition differentiates nothing — the sweep puts
    // every strategy within a whisker of each other against "1,3,1", because
    // one archer already answers one flyer. Two griffins in four collapses
    // every ground answer to 0.50 income and leaves the PIKEMAN at 1.30.
    //
    // The income DROPS here, which looks wrong next to a table that otherwise
    // climbs, and is not. Composition is far the stronger dial: an air wing at
    // 0.65 is a harder question than a ground army at 1.25, and holding the
    // income up as well would have put the stage past what the WARDEN and the
    // CHAPLAIN can survive — leaving the FALCONER the only path that can
    // finish the campaign, which is the exact failure the previous retune
    // fixed.
    {"THE EYRIE",      0.65f,  850.0f, 4, "1,3,3,1"},
    {"BLACK FIELD",    1.15f,  900.0f, 3, "1,1,2"},
    // Pure ground, and deliberately so. When stages 7 AND 8 both fielded
    // flyers, anti-air was mandatory twice over and the FALCONER was the only
    // path that could finish the campaign — measured, not guessed. A permanent
    // choice must not be able to strand a player.
    {"THE GATES",      1.55f, 1100.0f, 4, "1,1,0,1"},
    {"THE KEEP",       2.10f, 1300.0f, 4, "1,3,1,2"},
};
constexpr int kDefaultStageCount =
    static_cast<int>(sizeof(kDefaultStages) / sizeof(kDefaultStages[0]));
constexpr int kMaxStages = 24;

const StageKind& stageKind(int stage);
int stageCount();

// --- Permanent upgrades ----------------------------------------------------
//
// The in-battle upgrades are spent from a purse that resets every battle. These
// are bought between battles from money the campaign paid out, and they never
// go away — which is the difference between a game you can finish in one
// sitting and one worth coming back to.
//
// Three of them, deliberately touching things the in-battle upgrades do not,
// so the two systems are not the same choice at different speeds.
// GRANARY is where the economy actually works, and it is here rather than
// in the in-battle upgrades because that is the only place it CAN work.
//
// The in-battle INCOME upgrade was measured in five configurations — 120 for
// +4 and 80 for +5, each bought greedily and bought cautiously, and 45 for +5
// capped at two levels bought early. Every one of them was inert or actively
// harmful. Greedy buying lost stages a non-buyer won; cautious buying arrived
// after the outcome was settled.
//
// The reason is structural rather than numeric, which is why no price fixed
// it. Gold spent in a battle is gold not spent on the opening army, the
// opening army decides the line, and kill rewards mean a lost line compounds
// into a lost battle. An in-battle economy upgrade is therefore a bet against
// the one mechanic the whole design rests on.
//
// A PERMANENT upgrade has no such competition: it is bought from the campaign
// bank between battles, out of money that could never have been soldiers in
// the fight it affects. Same idea, moved somewhere it is not self-defeating.
enum class Perk { Damage, Fortify, Purse, Champion, Granary, Count };
constexpr int kPerkCount = static_cast<int>(Perk::Count);

struct PerkKind {
    const char* name;
    const char* effectText;
    float baseCost;
    float costGrowth;
    float effect;  // per level
};

constexpr PerkKind kDefaultPerks[] = {
    {"WEAPONS",  "+10% DAMAGE",    200.0f, 1.55f, 0.10f},
    {"RAMPARTS", "+150 CASTLE",    180.0f, 1.55f, 150.0f},
    {"TREASURY", "+40 START GOLD", 160.0f, 1.55f, 40.0f},
    {"CHAMPION", "+25% HERO",      240.0f, 1.60f, 0.25f},
    {"GRANARY",  "+2 GOLD/S",      220.0f, 1.55f, 2.0f},
};

const PerkKind& perkKind(int perk);
float perkCost(int perk, int owned);

// What a stage pays for winning it. Later stages pay more, and the first clear
// of one pays double — so pushing forward is worth more than farming a stage
// you have already beaten, without ever forbidding the farming.
constexpr float kStageRewardBase = 90.0f;
constexpr float kStageRewardPerStage = 55.0f;
constexpr float kFirstClearBonus = 2.0f;

float stageReward(int stage, bool firstClear);

// How far through the campaign the player has got, and what they own.
//
// A separate component from Session because Session is one battle and this
// outlives them: it is created once and read by the stage-select screen, the
// play scene, and the victory that unlocks the next stage.
// --- The hero's path -------------------------------------------------------
//
// The problem this solves is role compression: a hero that hits ground AND air,
// tanks, and out-damages everything has no identity, and measuring said the old
// one was not a swing in a battle but a substitute for playing well.
//
// The fix is not a clever mechanic, it is OPPORTUNITY COST. Every point spent
// on one axis is a point not spent on another, and — the part that actually
// bites — **reaching the sky is a path, not a freebie**. Two of the three
// paths cannot touch a griffin at all. That single exclusion is what stops the
// hero being strictly better than every unit at everything, because it means
// the hero can be answered.
//
// A path is chosen ONCE and kept. Respeccing would turn the decision into a
// menu you re-open per stage, which is the same reason the hero is one summon
// per battle rather than an ability on a cooldown: a choice you can take back
// is not a choice.
//
//   WARDEN     ground only. The most health, the least damage. A wall that
//              walks, and the only path that can hold a line by itself.
//   FALCONER   the only one that reaches the sky, and pays for it in health.
//              Fast and sharp; dies to a real front line.
//   CHAPLAIN   ground only, weak attack, and heals nearby friendly units while
//              it lives. Wins nothing on its own and makes an army outlast one.
//
// How to know it is fair, concretely: `campaign_probe` plays the campaign once
// per path. No path may win more stages than the others, and each must win at
// least one stage the other two lose. That is a criterion you can run, which
// is the whole reason the probe was built.
enum class HeroPath { None, Warden, Falconer, Chaplain, Count };
constexpr int kHeroPathCount = static_cast<int>(HeroPath::Count);

// How many upgrades a path offers. Every path has exactly this many, so the
// screen and the save format do not have to special-case one of them.
constexpr int kHeroUpgradesPerPath = 3;
constexpr int kHeroUpgradeCount = kHeroPathCount * kHeroUpgradesPerPath;

// Which number an upgrade row moves.
//
// Tagged in the table rather than implied by the row's position, so the code
// that applies a level is generic and a path can weight the three axes however
// it likes. The in-battle upgrades went the other way — a rule per upgrade
// written in code — because each of those does something structurally
// different. These all just scale a number, so they can be data.
enum class HeroStat { Health, Damage, Delay, Heal };

struct HeroUpgradeKind {
    const char* name;
    const char* effectText;
    HeroStat stat;
    float baseCost;
    float costGrowth;
    float effect;     // per level; a fraction for the scalers, health/s for Heal
    int maxLevel;     // the cap, which is what makes points scarce
};

struct HeroPathKind {
    const char* name;
    const char* blurb;

    // Multipliers on the roster's hero row, so the table stays the one place
    // the hero's baseline lives.
    float health;
    float damage;
    float attackDelay;

    bool hitsAir;

    // Heals friendlies within kHeroAuraRadius, in health per second. Zero for
    // everything but the chaplain.
    float healPerSecond;

    HeroUpgradeKind upgrades[kHeroUpgradesPerPath];
};

constexpr float kHeroAuraRadius = 150.0f;

// Caps are deliberately low and costs rise steeply. A path you can max out is
// a path with one ending; three levels of three things is 27 combinations and
// a bank that never quite covers all of it.
constexpr HeroPathKind kDefaultHeroPaths[] = {
    {"NONE", "NO PATH - THE HERO FIGHTS AS THE ROSTER WROTE IT",
     1.00f, 1.00f, 1.00f, false, 0.0f,
     {{"", "", HeroStat::Health, 0.0f, 1.0f, 0.0f, 0},
      {"", "", HeroStat::Health, 0.0f, 1.0f, 0.0f, 0},
      {"", "", HeroStat::Health, 0.0f, 1.0f, 0.0f, 0}}},

    {"WARDEN", "GROUND ONLY - A WALL THAT WALKS",
     1.45f, 0.85f, 1.00f, false, 0.0f,
     {{"PLATE", "+18% HEALTH",   HeroStat::Health, 180.0f, 1.60f, 0.18f, 3},
      {"HAFT",  "+12% DAMAGE",   HeroStat::Damage, 220.0f, 1.60f, 0.12f, 3},
      {"VIGIL", "-8% SWING GAP", HeroStat::Delay,  260.0f, 1.65f, 0.08f, 3}}},

    {"FALCONER", "REACHES THE SKY - AND PAYS FOR IT",
     0.70f, 1.25f, 0.85f, true, 0.0f,
     {{"TALON", "+16% DAMAGE",   HeroStat::Damage, 200.0f, 1.60f, 0.16f, 3},
      {"JESS",  "-8% SWING GAP", HeroStat::Delay,  240.0f, 1.60f, 0.08f, 3},
      {"HOOD",  "+10% HEALTH",   HeroStat::Health, 180.0f, 1.60f, 0.10f, 3}}},

    {"CHAPLAIN", "GROUND ONLY - MENDS THE LINE AROUND IT",
     1.10f, 0.60f, 1.15f, false, 7.0f,
     {{"LITANY", "+3/S HEALING", HeroStat::Heal,   200.0f, 1.65f, 3.0f,  3},
      {"CENSER", "+14% HEALTH",  HeroStat::Health, 190.0f, 1.60f, 0.14f, 3},
      {"RELIC",  "+8% DAMAGE",   HeroStat::Damage, 230.0f, 1.60f, 0.08f, 3}}},
};

const HeroPathKind& heroPath(int path);
int heroPathCount();

// What the next level of one of a path's upgrades costs.
float heroUpgradeCost(int path, int upgrade, int owned);

struct Campaign {
    int stagesUnlocked = 1;
    int currentStage = 0;
    int bank = 0;                    // gold carried between battles
    int perks[kPerkCount] = {};
    bool cleared[kMaxStages] = {};   // for the first-clear bonus

    // Which path the hero walks, and how far. `heroPath` of 0 is None, which
    // is the state a new campaign starts in — the hero is summonable but
    // unspecialised, so choosing is the first real decision the armoury offers.
    int heroPath = 0;
    int heroUpgrades[kHeroPathCount][kHeroUpgradesPerPath] = {};

    // What you walk in carrying. -1 is an empty slot; a fresh campaign is
    // filled in with the first sellable kinds so the game is playable before
    // anybody visits the army screen.
    int loadout[kLoadoutSlots] = {-1, -1, -1, -1};

    // How far each unit type has been trained, indexed by ROSTER position.
    //
    // By index rather than by name, unlike the perks and the hero path, and
    // that is a deliberate difference: a data file may add unit types, so
    // there is no fixed set of names to key on. The save writes them by name
    // anyway — see saveCampaign — because an index is only safe while the
    // roster it indexes is the same one.
    int unitLevels[kMaxUnitKinds] = {};
};

// --- Training ---------------------------------------------------------------
//
// Every unit type levels, bought between battles from the same bank the
// armoury and the hero spend.
//
// The cost is scaled by what the unit costs to field, so training an OGRE is
// dearer than training a RUNNER and the cheap units stay the cheap units. A
// flat price would have made the expensive rows strictly better to invest in,
// which is the same collapse the hero tree exists to prevent.
//
// Capped, and the cap is what stops levelling being an answer to difficulty:
// five levels is about a soldier and a half of health, which wins fights it
// was already close to winning and does not win a stage the composition
// cannot.
constexpr int kMaxUnitLevel = 5;
constexpr float kTrainHealthPerLevel = 0.08f;   // +8% health
constexpr float kTrainDamagePerLevel = 0.06f;   // +6% damage
constexpr float kTrainCostFactor = 2.2f;        // times the unit's own cost
constexpr float kTrainCostGrowth = 1.55f;

// What the next level of `kind` costs a player who owns `owned` already.
float trainCost(int kind, int owned);

// The multipliers a trained unit fights with.
float trainedHealth(int kind, int level);
float trainedDamage(int kind, int level);

// The levels this battle is being fought with. Set from the campaign when a
// battle starts, exactly like the loadout, and applied to YOUR units only —
// the opponent fields the roster as written, or every level bought would arm
// both sides equally and buy nothing.
void setTrainingLevels(const int* levels, int count);
void resetTraining();
int trainingLevel(int kind);

Campaign& campaignOf(engine::World& world);  // created on first use

// --- Saving ----------------------------------------------------------------
//
// Written to the per-user location SDL reports, not next to the executable:
// the folder a game is installed into is frequently read-only.
//
// A missing save is a new campaign, and a corrupt one is treated the same way.
// There is nothing here worth refusing to start over.
bool saveCampaign(const Campaign& campaign);
bool loadCampaign(Campaign& campaign);
std::string savePath();

// Points saving somewhere else. For tests, which must not write over a real
// player's campaign — and for anyone who wants two of them.
void setSavePath(const std::string& path);

// --- The view --------------------------------------------------------------
//
// The camera rides with your front line, because that is where your decisions
// land. With nothing on the field it drifts home to your own castle, which
// also happens to be the right place to be looking when you have just lost an
// army and something is walking towards your gate.
//
// LEFT and RIGHT take the view off the leash so you can scout; letting go
// hands it back after a moment. That override is not a luxury on a field this
// wide — without it the player cannot look at their own castle while the
// fighting is happening two screens away.
constexpr float kCameraFollowRate = 4.0f;    // how fast it catches up, per second
constexpr float kFreeLookSpeed = 640.0f;     // pixels per second while steering
constexpr float kFreeLookHold = 1.2f;        // seconds before following resumes

// The furthest left the view can sit is 0 and the furthest right is this: the
// camera stops at the edges of the world rather than showing empty space past
// them.
constexpr float kCameraMaxX = kWorldWidth - static_cast<float>(kWindowWidth);

// --- The minimap -----------------------------------------------------------
//
// A strip across the top standing in for the whole battlefield, with a marker
// for each castle and each side's front line. On a one-screen field this would
// be clutter; on a field two and a half screens wide it is the only way to
// know a push is happening while you are looking somewhere else.
// --- Animation -------------------------------------------------------------
//
// Units are drawn as a coloured block with an articulated stick figure over
// the top of it: two legs that swing while walking, and an arm that swings
// through an arc when a blow lands. The block is the silhouette and reads at a
// glance; the figure is what makes it obvious whether something is marching or
// fighting, which was previously visible only as a rectangle that had stopped.
//
// It is built out of `Polygon`, whose points are a plain vector this rebuilds
// every frame. No sprite sheets, no art files, and — the reason this route was
// taken over sprites — nothing to draw before it works.
//
// That does mean this slice does NOT deliver the `Animation` component and
// `Sprite.flip` the roadmap predicted. Those are for frame-based sprite
// animation, and building them now would be building for art that does not
// exist. They stay on the roadmap for whenever it does.
constexpr float kWalkCycleRate = 0.10f;   // radians of leg swing per pixel walked
constexpr float kLegSwing = 6.0f;         // how far a foot travels from centre
constexpr float kSwingDecayRate = 5.0f;   // how fast an attack swing plays out

// --- The spawn bar ---------------------------------------------------------
//
// One button per row of the roster, along the bottom of the screen. The number
// keys still work and always will — they are faster once you know the roster,
// and losing them to add a mouse would be a downgrade — but a bar is how this
// genre is actually played, and it is the only way a new player learns what
// the three types cost without reading a README.
//
// Buttons are screen-space, so they stay put while the field scrolls beneath
// them, and they are hit-tested directly against the cursor with no camera
// involved. That is the whole reason `screenToWorld` is *not* used here: a
// screen-space element is already in the space the mouse reports.
constexpr float kButtonWidth = 130.0f;
constexpr float kButtonHeight = 46.0f;
constexpr float kButtonGap = 10.0f;
constexpr float kButtonY = kWindowHeight - kButtonHeight - 14.0f;
constexpr float kButtonX = 16.0f;

// Where button `index` sits. Kept here rather than in the .cpp because both
// the game and its tests need to know — a test that clicks a button has to
// work out where it is, and hard-coding that in two places is how a UI test
// stops testing the UI.
constexpr float buttonLeft(int index) {
    return kButtonX + static_cast<float>(index) * (kButtonWidth + kButtonGap);
}

// The hero button sits at the right-hand end of the bottom row, and the spawn
// bar is sized to stop before it.
//
// Both of these used to be independent constants, and they overlapped: the
// hero button covered what would have been the fourth and fifth spawn slots.
// With the built-in roster there is no fourth slot, so nothing showed it —
// but a data file adding one more unit type would have put its button
// underneath the hero's, and since the hero is hit-tested first, clicking that
// unit would have summoned the hero instead. Derived from each other now, so
// they cannot drift apart again.
constexpr float kHeroButtonWidth = 168.0f;
constexpr float kHeroButtonX =
    static_cast<float>(kWindowWidth) - kHeroButtonWidth - 16.0f;

// How many buttons fit across the bottom before they reach the hero's. The
// roster's length is a runtime value, so this is a real limit rather than an
// arithmetic curiosity: a data file with ten unit types would otherwise draw
// four of them into the void, or worse, underneath something else.
constexpr int kMaxVisibleButtons =
    static_cast<int>((kHeroButtonX - kButtonGap - kButtonX) /
                     (kButtonWidth + kButtonGap));

// --- The loadout -----------------------------------------------------------
//
// You own every unit type. You bring four.
//
// This is the one mechanic the reference game has that this one did not, and
// its asset structure is the argument: 2,161 unit frames behind a 14 KB unit
// table, against a spawn bar five buttons wide. Owning more than you can field
// is the whole shape of the genre's meta — the decision moves out of the
// battle and into what you walked in carrying.
//
// It also closes a hole this file has had since the spawn bar was built. The
// bar showed the first five sellable rows and everything past them was
// unreachable; a data file adding a sixth produced a unit that could be paid
// for and never sent. "Which five?" was answered by roster order. Now it is
// answered by the player.
//
// FOUR rather than five, so the choice bites. Five slots against seven kinds
// is a mild preference; four is a decision you can get wrong. The constant
// itself lives up beside kMaxUnitKinds, because Campaign needs it long before
// this point in the file.

// The kinds the bar sells this battle, in bar order. Set from the campaign
// when a battle starts; falls back to the first sellable kinds so that a bare
// World — a test, a probe — behaves exactly as it did before loadouts existed.
void setLoadout(const int* kinds, int count);
void resetLoadout();

// What slot `slot` is carrying, or -1. This is the ONE answer to "what can the
// player send": the bar, the number keys and the title screen all ask it.
int loadoutKind(int slot);

// Is this kind in the loadout at all?
bool inLoadout(int kind);

// The same question about a CAMPAIGN's loadout rather than the one a battle is
// currently being fought with. Two different things: the campaign holds what
// you have chosen, the global holds what this battle was started with.
bool inLoadoutOf(const Campaign& campaign, int kind);

// Every sellable kind, in roster order — what the army screen offers and what
// a loadout is chosen FROM. Excludes the hero, which is never sold.
int sellableKindCount();
int sellableKind(int index);

// The loadout can never ask the bar for more buttons than it can draw.
//
// Two constants that have to agree, checked by the compiler rather than by
// somebody remembering — this file has had four bugs from two copies of one
// fact disagreeing, and every one of them was found by accident.
static_assert(kLoadoutSlots <= kMaxVisibleButtons,
              "the bar cannot draw as many buttons as the loadout carries");

// The roster clamped to what fits. Kinds past this have no plate, no label,
// no hit box and no key: the bar is the only way to send anything, and the
// number keys are bound to its SLOTS rather than to roster rows.
//
// That used to read "their number keys still work, so nothing is unreachable",
// which was wrong in a way no shipped roster could show. The keys stopped at
// four and counted rows including the hero, while the bar stopped at five and
// skipped it, so a file with six sellable kinds produced one that neither
// could reach. Binding both to the same list is what makes the sentence above
// true rather than hopeful — and it means a roster longer than the bar is a
// genuine limit, which is the honest thing for it to be until there is a way
// to choose which units you bring.
int visibleButtonCount();

// Which unit kind a bar slot sells, or -1 for an empty slot. The bar skips the
// hero, so slot and kind are not the same number — exposed for the same reason
// `buttonAt` is, so a test can point at a button without re-deriving the rule.
int kindForButton(int slot);

// Dragging the field scrolls the view, which is the genre's other mouse verb.
// A few pixels of slop before a press counts as a drag, so a slightly shaky
// click on a button is still a click.
constexpr float kDragThreshold = 4.0f;

constexpr float kMinimapX = 300.0f;
constexpr float kMinimapY = 20.0f;
constexpr float kMinimapWidth = 360.0f;
constexpr float kMinimapHeight = 12.0f;
constexpr float kMinimapMarkerWidth = 6.0f;

// --- Scenery ---------------------------------------------------------------
//
// Three bands of distance behind the fighting and one in front of it, each
// sliding past at its own rate. Without them a two-and-a-half-screen field
// scrolls past a flat colour and reads as a corridor; with them it reads as a
// place, and — the practical part — you can tell how far you have scrolled
// without looking at the minimap.
//
// The numbers are the fraction of the camera's movement each band gets. The
// ground is 1.0 by definition; the foreground is over 1.0, which is what sells
// depth in the other direction and is the case a boolean could never express.
constexpr float kFarParallax = 0.18f;
constexpr float kMidParallax = 0.45f;
constexpr float kNearParallax = 0.72f;
constexpr float kForeParallax = 1.30f;

// A band at parallax p slides `kCameraMaxX * p` pixels over a full sweep of
// the camera, so it needs to be that much wider than the window to avoid
// running out and showing the void behind it.
constexpr float bandWidth(float parallax) {
    return static_cast<float>(kWindowWidth) + kCameraMaxX * parallax + 40.0f;
}

// Layers are drawn low to high, so scenery behind the fight is negative and
// the foreground sits above everything except the HUD.
constexpr int kSkyLayer = -4;
constexpr int kFarLayer = -3;
constexpr int kMidLayer = -2;
constexpr int kNearLayer = -1;

constexpr int kFieldLayer = 0;
constexpr int kForeLayer = 1;
constexpr int kHudLayer = 5;
constexpr int kOverlayLayer = 10;
constexpr int kOverlayTextLayer = 11;

// --- Components ------------------------------------------------------------

// Which side something belongs to. Left walks right; right walks left.
struct Team {
    bool leftSide = true;
};

struct Unit {
    int kind = 1;  // an index into kUnitKinds
    float health = 0.0f;

    // What full health means for THIS unit, which is not always what the
    // roster says. The hero is scaled by CHAMPION on the way out of the gate,
    // so its maximum is a property of the individual rather than of its row —
    // and anything that has to cap health has to ask the unit, not the table.
    //
    // Healing asked the table, which meant a heal cast on a championed hero
    // clamped it DOWN to the roster's number: a spell that hurt, and only for
    // the player who had paid for the perk.
    float maxHealth = 0.0f;

    // Whether THIS unit can reach the sky, seeded from its roster row.
    //
    // A per-entity copy rather than a read of the table, because the hero's
    // reach is decided by the path its owner chose rather than by the row it
    // was spawned from — and "can it be answered" is the single rule that
    // stops a hero being better than every unit at everything. Everything else
    // on the field never changes it, so for every other unit this is exactly
    // what the table says.
    bool hitsAir = false;

    float timeUntilAttack = 0.0f;

    // Where the walk cycle has got to, advanced by distance travelled rather
    // than by time — so a runner's legs move faster than a soldier's for free,
    // and nothing slides with its feet still.
    float phase = 0.0f;

    // Counts down from 1 to 0 after a blow lands, driving the arm through its
    // arc. Separate from timeUntilAttack because the swing should be quick and
    // the same length for every unit, while the delay between blows is a
    // balance number that differs per kind.
    float swing = 0.0f;

    // The stick figure drawn over this unit, so a death can take it along in
    // the same frame. Relying on the orphan sweep alone left the figure alive
    // for a frame after its unit — invisible on screen, but it meant "one
    // figure per unit" was briefly false during every fight, and a test that
    // has to tolerate a transient cannot catch a real leak.
    engine::Entity figure = engine::kInvalidEntity;
};

// The stick figure drawn over a unit. One per unit, owning no state of its own
// — everything about it is recomputed each frame from its owner.
//
// It is a separate entity rather than more fields on the unit because the
// renderer draws components, and a unit needs to be both a filled block and a
// line drawing. Keeping the link one-way (figure knows its owner, not the
// reverse) means nothing has to remember to tidy up: a figure whose owner has
// gone is an orphan, and the animation pass sweeps orphans.
struct Figure {
    engine::Entity owner = engine::kInvalidEntity;
};

struct Castle {
    float health = kCastleHealth;
};

// Debris from a dying unit, so a death reads as an event rather than a
// disappearance. Uses Lifetime to clean itself up.
struct Shard {};

// A shot in the air. It carries which side fired it so a cannonball cannot
// kill its own army, and where it is aimed so it knows when it has arrived —
// a fixed flight time is simpler and more accurate than watching for the
// ground, and it means a click always lands exactly where it was clicked.
struct Cannonball {
    bool leftSide = true;
    float timeLeft = kCannonFlightTime;
};

// The whole battle's state, on one entity — the singleton-component pattern
// used by the other games, which is what lets a test read the gold without
// the scene exposing anything.
struct Session {
    float gold = kStartingGold;
    float enemyGold = kStartingGold;
    bool gameOver = false;
    bool playerWon = false;

    // One timer per unit kind, per side. This is what makes a button bar a
    // decision: a full purse cannot be poured into a single type, so spending
    // it means sending something else.
    float spawnCooldowns[kMaxUnitKinds] = {};
    float enemySpawnCooldowns[kMaxUnitKinds] = {};

    // Where the opponent is in its composition cycle, and how much of the
    // current wave it still owes. Zero means it is banking for the next one.
    int enemyWaveIndex = 0;
    int enemyWaveRemaining = 0;

    // The stage's settings, copied in when the battle starts. Held here rather
    // than read from the stage table on every use, so a battle is decided by
    // one snapshot taken at the start — and so a test can set up a fight
    // without inventing a stage to hold it.
    // The hero: summoned at most once, and once it has fallen it stays
    // fallen. Two flags rather than one, because "not yet summoned" and
    // "summoned and dead" have to look different on the button.
    bool heroSummoned = false;
    bool heroFallen = false;

    int composition[kMaxComposition] = {1, 1, 2, 0, 1, 0};
    int compositionLength = 6;
    int enemyWaveSize = 3;
    float enemyIncome = 1.0f;

    // What GRANARY adds to YOUR earnings, copied in when the battle starts.
    //
    // On the Session rather than read from the Campaign mid-fight, for the
    // same reason the stage's numbers are: a battle is decided by one snapshot
    // taken at the start. Yours only — the opponent earns what its stage says.
    float bonusGoldPerSecond = 0.0f;

    // Counts down while the player is steering the view by hand. Above zero
    // the camera obeys the arrow keys or the drag; at zero it goes back to
    // following.
    float freeLookSeconds = 0.0f;

    // A drag in progress: where it started, and whether it has moved far
    // enough to count as one rather than as a click.
    bool dragging = false;
    float dragStartX = 0.0f;
    float dragStartCameraX = 0.0f;

    // A press that has not yet decided whether it is a drag or a shot.
    bool pressPending = false;
    float pressX = 0.0f;
    float pressY = 0.0f;

    // Mana, and the spell waiting for a click. -1 means nothing is armed and
    // a click on the field fires the cannon as it always did.
    float mana = kStartingMana;
    int armedSpell = -1;
    float rageSeconds = 0.0f;

    float cannonCooldown = 0.0f;
    float enemyCannonCooldown = kCannonCooldown;  // it does not open fire instantly

    // How many of each upgrade each side has bought.
    int upgrades[kUpgradeCount] = {};
    int enemyUpgrades[kUpgradeCount] = {};
};

// --- Queries (used by the game and by its tests) ---------------------------

Session* findSession(engine::World& world);
engine::Entity findCastle(engine::World& world, bool leftSide);
int countUnits(engine::World& world, bool leftSide);

// The view. Exposed because the camera is now real game state — where it is
// pointing decides what the player can see — and a test can check it without
// a window ever opening.
engine::Camera* findCamera(engine::World& world);

// Where a side's advance has reached: the x of its frontmost unit, or its own
// castle when it has nothing on the field. This is what the camera follows and
// what the minimap marks.
float frontLineX(engine::World& world, bool leftSide);

// How many stick figures are on the field. Exposed only so a test can prove
// they are cleaned up: a unit that dies leaving its figure behind is an entity
// leak that nothing else would notice until the frame rate did.
int countFigures(engine::World& world);

// Advances every unit's walk cycle and attack swing and rebuilds its figure,
// then sweeps away figures whose owner has died. Called once per frame.
void animateUnits(engine::World& world, float dt);

// --- Upgrades (used by the game and by its tests) --------------------------

// What the next level of `upgrade` costs a side that has already bought
// `owned` of them. Geometric, so the fifth costs far more than the first.
float upgradeCost(int upgrade, int owned);

// The values a side actually plays with, once its upgrades are counted.
float goldPerSecondFor(const Session& session, bool leftSide);
int populationCapFor(const Session& session, bool leftSide);
float castleMaxHealthFor(const Session& session, bool leftSide);

// Where the upgrade buttons are, and which one is under a screen position
// (-1 for none). Exposed for the same reason `buttonAt` is: a test that clicks
// one should not have to re-derive the layout.
constexpr float kUpgradeX = 690.0f;
constexpr float kUpgradeWidth = 254.0f;
constexpr float kUpgradeHeight = 30.0f;
constexpr float kUpgradeGap = 6.0f;

// The mana bar sits between the two panels, and used to be an unlabelled blue
// strip flush against the bottom of the upgrade panel — which read as a
// progress bar belonging to SUPPLY rather than as a resource of its own. It
// has a gap and a label now.
constexpr float kManaBarHeight = 16.0f;
constexpr float kManaBarGap = 8.0f;
constexpr float kManaBarY = kSpellY - kManaBarGap - kManaBarHeight;

// Stacked upward from the mana bar for the same reason the spells are stacked
// upward from the castle: so that moving one of them cannot silently land on
// another.
constexpr float kUpgradePanelHeight =
    kUpgradeCount * (kUpgradeHeight + kUpgradeGap) - kUpgradeGap;
constexpr float kUpgradeY = kManaBarY - kManaBarGap - kUpgradePanelHeight;

constexpr float upgradeTop(int index) {
    return kUpgradeY + static_cast<float>(index) * (kUpgradeHeight + kUpgradeGap);
}

int upgradeAt(float screenX, float screenY);

// How many cannonballs are in the air. For tests; nothing else needs it.
int countCannonballs(engine::World& world);

// Which spawn-bar button is under this screen position, or -1 for none.
// Exposed because it is the rule the UI is built on, and a test should be able
// to check it without inferring it from what happened afterwards.
int buttonAt(float screenX, float screenY);

// Units of one kind on one side. `kind` of -1 counts all of them, which is
// what the population cap is measured against.
int countUnitsOfKind(engine::World& world, bool leftSide, int kind);

// Spawns one unit for a side, ignoring cost and the population cap. Exposed so
// tests can set up a fight directly instead of waiting for gold to accrue.
engine::Entity spawnUnit(engine::World& world, bool leftSide, int kind = 1);

// --- Wiring ----------------------------------------------------------------

void setAudioDevice(engine::AudioDevice* audio);

// Where unit artwork is loaded from, when a roster row names a sheet.
//
// Wired the same way the audio device is, and for the same reason: the rules
// live in a library that must run with no window, so anything owned by the
// Engine is handed in rather than reached for. Left null — which is what every
// test and both simulators do — a unit with a sheet simply falls back to its
// coloured block, so nothing needs a GPU to be tested.
void setTextureCache(engine::TextureCache* textures);

// --- Scenes ----------------------------------------------------------------

std::unique_ptr<engine::Scene> makeTitleScene();
std::unique_ptr<engine::Scene> makeStageSelectScene();

// Plays whichever stage the Campaign says is current.
std::unique_ptr<engine::Scene> makePlayScene();

// Where the stage rows are drawn, and which one is under a screen position
// (-1 for none). Exposed for the same reason the spawn bar's layout is: a test
// that clicks a stage should not have to re-derive where it sits.
constexpr float kStageX = 260.0f;
constexpr float kStageY = 120.0f;
constexpr float kStageWidth = 440.0f;
constexpr float kStageHeight = 34.0f;
constexpr float kStageGap = 6.0f;

constexpr float stageTop(int index) {
    return kStageY + static_cast<float>(index) * (kStageHeight + kStageGap);
}

// Where the two lines of instructions sit under the list. The list has to stop
// before them.
constexpr float kStageHintY = 470.0f;

// How many stage rows fit above the instructions.
//
// Derived rather than assumed, for the third time in this file — the hero
// button and the spawn bar had exactly this bug, and this one was one stage
// away from joining them. `kMaxStages` is 24 and a data file may supply that
// many, while only eight rows fit: a ninth would have drawn straight across
// "CLICK A BATTLE, OR ENTER FOR THE LATEST", a tenth across "Q TO QUIT", and
// everything past that off the bottom of the window where it can be neither
// seen nor clicked.
//
// The shipped campaign has exactly eight, which is why nothing showed it and
// why this fix changes nothing you can see today.
constexpr int kMaxVisibleStages =
    static_cast<int>((kStageHintY - kStageY) / (kStageHeight + kStageGap));

// The stage list clamped to what fits. A campaign longer than this is a real
// limit and an honest one: ENTER always plays the newest stage unlocked, so a
// long campaign can still be progressed to the end — the rows past the eighth
// just cannot be picked out of the list to replay. Making them pickable means
// scrolling the list, which is a feature rather than a bug fix.
int visibleStageCount();

int stageAt(float screenX, float screenY);

// The armoury, down the left of the stage list: one row per permanent upgrade,
// bought from the bank.
constexpr float kPerkX = 20.0f;
constexpr float kPerkY = 130.0f;
constexpr float kPerkWidth = 220.0f;
constexpr float kPerkHeight = 44.0f;
constexpr float kPerkGap = 8.0f;

constexpr float perkTop(int index) {
    return kPerkY + static_cast<float>(index) * (kPerkHeight + kPerkGap);
}

int perkAt(float screenX, float screenY);

// --- The hero screen -------------------------------------------------------
//
// Its own scene rather than more rows on the stage list, because choosing a
// path is a different kind of decision from choosing a battle and the armoury
// column has no room left. Reached with H from the stage list.
std::unique_ptr<engine::Scene> makeHeroScene();

// The three paths, laid out across the middle.
constexpr float kPathX = 60.0f;
constexpr float kPathY = 150.0f;
constexpr float kPathWidth = 270.0f;
constexpr float kPathHeight = 92.0f;
constexpr float kPathGap = 15.0f;

constexpr float pathLeft(int index) {
    return kPathX + static_cast<float>(index) * (kPathWidth + kPathGap);
}

// Which path plate is under a screen position, or -1. Paths are numbered from
// 1 (None is not offered), so this returns a HeroPath value directly.
int pathAt(float screenX, float screenY);

// The chosen path's three upgrades, stacked underneath.
constexpr float kHeroUpgradeX = 240.0f;
constexpr float kHeroUpgradeY = 300.0f;
constexpr float kHeroUpgradeWidth = 480.0f;
constexpr float kHeroUpgradeHeight = 44.0f;
constexpr float kHeroUpgradeGap = 8.0f;

constexpr float heroUpgradeTop(int index) {
    return kHeroUpgradeY +
           static_cast<float>(index) * (kHeroUpgradeHeight + kHeroUpgradeGap);
}

int heroUpgradeAt(float screenX, float screenY);

// --- The army screen -------------------------------------------------------
//
// One row per unit type you own. Clicking the row's left side carries or drops
// it; clicking TRAIN buys a level. Reached with A from the stage list.
//
// Both halves on one screen deliberately. Choosing what to bring and choosing
// what to invest in are the same question asked twice — there is no point
// training an OGRE you never carry — and putting them on separate screens
// would let a player answer one without seeing the other.
std::unique_ptr<engine::Scene> makeArmyScene();

constexpr float kArmyX = 130.0f;
constexpr float kArmyY = 118.0f;
constexpr float kArmyWidth = 700.0f;
constexpr float kArmyHeight = 40.0f;
constexpr float kArmyGap = 6.0f;

// The TRAIN button lives at the right-hand end of a row.
constexpr float kTrainWidth = 150.0f;
constexpr float kTrainX = kArmyX + kArmyWidth - kTrainWidth;

constexpr float armyTop(int index) {
    return kArmyY + static_cast<float>(index) * (kArmyHeight + kArmyGap);
}

// How many rows fit above the instructions at the bottom.
constexpr float kArmyHintY = 476.0f;
constexpr int kMaxVisibleArmyRows =
    static_cast<int>((kArmyHintY - kArmyY) / (kArmyHeight + kArmyGap));

// Which army row is under a screen position, or -1. Rows are indexes into the
// SELLABLE kinds, not into the roster.
int armyRowAt(float screenX, float screenY);

// True when that position is on the row's TRAIN button rather than its
// carry/drop half.
bool armyTrainHit(float screenX, float screenY);

}  // namespace lanebattle
