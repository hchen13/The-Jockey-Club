#pragma once
#include <algorithm>
#include <array>
#include <span>
#include <string_view>
#include <utility>
#include "horse_identity.h"

// Pure data, deliberately free of game natives so the route parser can validate
// a Breed= key without dragging the runtime in.
namespace horse_races::field_race {
// These stock reference rows validate route IDs and provide offline fixtures.
// They are NOT the runtime spawn whitelist: horse_catalog reads the current
// environment, and race_field applies reserved host pools and coat ownership.
inline constexpr std::array<std::string_view,5> kSaddlerCoats{
    "A_C_HORSE_KENTUCKYSADDLE_BLACK","A_C_HORSE_KENTUCKYSADDLE_BUTTERMILKBUCKSKIN_PC",
    "A_C_HORSE_KENTUCKYSADDLE_CHESTNUTPINTO","A_C_HORSE_KENTUCKYSADDLE_GREY","A_C_HORSE_KENTUCKYSADDLE_SILVERBAY"};
// Stock reference subset. Registered, identified expansion coats enter through
// the environment catalog rather than by extending this array for every mod.
inline constexpr std::array<std::string_view,3> kMustangSpawnCoats{
    "A_C_HORSE_MUSTANG_TIGERSTRIPEDBAY","A_C_HORSE_MUSTANG_WILDBAY","A_C_HORSE_MUSTANG_GRULLODUN"};
// The player is restricted by BREED, not by coat: if they somehow own another
// Mustang, it is still a Mustang and the race has no reason to turn it away.
inline constexpr std::array<std::string_view,7> kMustangAllowedCoats{
    "A_C_HORSE_MUSTANG_TIGERSTRIPEDBAY","A_C_HORSE_MUSTANG_WILDBAY","A_C_HORSE_MUSTANG_GRULLODUN",
    "A_C_HORSE_MUSTANG_GOLDENDUN","A_C_HORSE_MUSTANG_BLACKOVERO","A_C_HORSE_MUSTANG_BUCKSKIN",
    "A_C_HORSE_MUSTANG_CHESTNUTTOVERO"};

// The two protagonist stakes share these seven coats, with only the selected
// host coat removed from the jockey bag. Runtime metadata and loading still
// validate each identity, including SUHM's Silver Bay WINTER02_01.
inline constexpr std::array<std::string_view,7> kThoroughbredSpawnCoats{
    "A_C_HORSE_THOROUGHBRED_BLOODBAY","A_C_HORSE_THOROUGHBRED_DAPPLEGREY",
    "A_C_HORSE_THOROUGHBRED_BLACKCHESTNUT","A_C_HORSE_THOROUGHBRED_REVERSEDAPPLEBLACK",
    "A_C_HORSE_THOROUGHBRED_BRINDLE","A_C_HORSE_JOHN_ENDLESSSUMMER","A_C_HORSE_WINTER02_01"};
inline constexpr std::array<std::string_view,7> kThoroughbredAllowedCoats{
    "A_C_HORSE_THOROUGHBRED_BLOODBAY","A_C_HORSE_THOROUGHBRED_DAPPLEGREY",
    "A_C_HORSE_THOROUGHBRED_BLACKCHESTNUT","A_C_HORSE_THOROUGHBRED_REVERSEDAPPLEBLACK",
    "A_C_HORSE_THOROUGHBRED_BRINDLE","A_C_HORSE_JOHN_ENDLESSSUMMER","A_C_HORSE_WINTER02_01"};
inline constexpr std::array<std::string_view,1> kThoroughbredHostHorses{
    "A_C_HORSE_THOROUGHBRED_BRINDLE"};
// A host character can own more than one named horse - the gang mounts each
// have an "Endless Summer" epilogue variant - and their model names say nothing
// about breed, because they are named after the rider. Listing them in
// preference order lets the runtime pick the one whose breed the GAME reports
// as matching this race, instead of a model name somebody assumed was right.
inline constexpr std::array<std::string_view,2> kUncleHorses{
    "A_C_HORSE_GANG_UNCLE","A_C_HORSE_GANG_UNCLE_ENDLESSSUMMER"};
// Sadie's epilogue mount first: the user asked for the horse she rides at the
// end of the story, and it is the candidate most likely to be a Mustang.
inline constexpr std::array<std::string_view,2> kSadieHorses{
    "A_C_HORSE_GANG_SADIE_ENDLESSSUMMER","A_C_HORSE_GANG_SADIE"};
// A model's breed, from its name: an ordinary coat is A_C_HORSE_<BREED>_<COAT>,
// and a named horse - named after its rider - is stated here. This is data
// the race trusts; the game's own breed labels are logged but not relied on
// (a Kentucky Saddler coat's label came back as "8th" on 2026-09-14, which
// put Uncle on a stranger's horse).
inline constexpr std::array<std::pair<std::string_view,std::string_view>,13> kNamedMountBreeds{{
    {"A_C_HORSE_GANG_TRELAWNEY","APPALOOSA"},
    {"A_C_HORSE_GANG_SEAN","AMERICANSTANDARDBRED"},
    {"A_C_HORSE_GANG_CHARLES","APPALOOSA"},
    {"A_C_HORSE_JOHN_ENDLESSSUMMER","THOROUGHBRED"},
    {"A_C_HORSE_GANG_DUTCH","ARABIAN"},
    {"A_C_HORSE_GANG_UNCLE","KENTUCKYSADDLE"},
    {"A_C_HORSE_GANG_UNCLE_ENDLESSSUMMER","APPALOOSA"},   // The game: "Appaloosa - Few Spot Buckskin Coat". It was taken for a Saddler until 2026-09-21.
    {"A_C_HORSE_GANG_SADIE_ENDLESSSUMMER","MUSTANG"},
    {"A_C_HORSE_BUELL_WARVETS","DUTCHWARMBLOOD"},
    // Three the game files under a breed although their names say otherwise. The winter horse is a
    // story horse nothing here races; the row is what stops its name being read as a breed of its own.
    {"A_C_HORSE_WINTER02_01","THOROUGHBRED"},
    {"A_C_HORSE_EAGLEFLIES","AMERICANPAINT"},
    {"A_C_HORSE_GANG_JAVIER","AMERICANPAINT"},
    {"A_C_HORSE_GANG_BILL","ARDENNES"},
}};
inline std::string_view BreedToken(std::string_view model) {
    return horse_identity::Breed(model);
}
// Five more breeds with coats enough for a field (the user, 2026-09-21). Every coat listed ships with the
// PC game's story mode (the _PC ones came with the PC release); the host rides the first until one is named.
// The Arabian race (the user, 2026-09-21): black, white, red chestnut, warped brindle, rose grey bay,
// and the Count - Dutch's albino - which the host rides. The grey was left out then on the grounds that
// the game's own SP tables did not carry it; they do (pause_menu.c: BREED_ARABIAN_GREY, "Grey Coat"),
// so it rides too. The plain red chestnut is left out because it IS the PC one to look at - one coat,
// two models - and two jockeys in the same coat is what the draw exists to avoid.
inline constexpr std::array<std::string_view,6> kArabianCoats{
    "A_C_HORSE_ARABIAN_BLACK","A_C_HORSE_ARABIAN_GREY","A_C_HORSE_ARABIAN_REDCHESTNUT_PC","A_C_HORSE_ARABIAN_ROSEGREYBAY","A_C_HORSE_ARABIAN_WARPEDBRINDLE_PC","A_C_HORSE_ARABIAN_WHITE"};
inline constexpr std::array<std::string_view,1> kArabianHost{"A_C_HORSE_GANG_DUTCH"};   // Lent to the host, kept out of the field.
inline constexpr std::array<std::string_view,6> kArabianSpawn{
    "A_C_HORSE_ARABIAN_BLACK","A_C_HORSE_ARABIAN_GREY","A_C_HORSE_ARABIAN_REDCHESTNUT_PC","A_C_HORSE_ARABIAN_ROSEGREYBAY","A_C_HORSE_ARABIAN_WARPEDBRINDLE_PC","A_C_HORSE_ARABIAN_WHITE"};
// Four Standardbred coats are in story mode; the light buckskin has a model but no coat of its own in
// the game's compendium table (no text for BREED_AMERICANSTANDARDBRED_LIGHTBUCKSKIN), so it stays out.
// The host rides Ennis, Sean's silver tail buckskin (lent: host_horses::Lent), so the field leaves the
// ordinary silver tail buckskin alone and nobody on the grid looks like the host: three jockeys, three coats.
inline constexpr std::array<std::string_view,4> kAmericanStandardbredCoats{
    "A_C_HORSE_AMERICANSTANDARDBRED_BLACK","A_C_HORSE_AMERICANSTANDARDBRED_BUCKSKIN","A_C_HORSE_AMERICANSTANDARDBRED_PALOMINODAPPLE","A_C_HORSE_AMERICANSTANDARDBRED_SILVERTAILBUCKSKIN"};
inline constexpr std::array<std::string_view,1> kAmericanStandardbredHost{"A_C_HORSE_GANG_SEAN"};
inline constexpr std::array<std::string_view,3> kAmericanStandardbredSpawn{
    "A_C_HORSE_AMERICANSTANDARDBRED_BLACK","A_C_HORSE_AMERICANSTANDARDBRED_BUCKSKIN","A_C_HORSE_AMERICANSTANDARDBRED_PALOMINODAPPLE"};
inline constexpr std::array<std::string_view,4> kAmericanPaintCoats{
    "A_C_HORSE_AMERICANPAINT_GREYOVERO","A_C_HORSE_AMERICANPAINT_OVERO","A_C_HORSE_AMERICANPAINT_SPLASHEDWHITE","A_C_HORSE_AMERICANPAINT_TOBIANO"};
inline constexpr std::array<std::string_view,1> kAmericanPaintHost{"A_C_HORSE_AMERICANPAINT_GREYOVERO"};   // The host's own, kept out of the field.
inline constexpr std::array<std::string_view,3> kAmericanPaintSpawn{
    "A_C_HORSE_AMERICANPAINT_OVERO","A_C_HORSE_AMERICANPAINT_SPLASHEDWHITE","A_C_HORSE_AMERICANPAINT_TOBIANO"};
inline constexpr std::array<std::string_view,7> kTennesseeWalkerCoats{
    "A_C_HORSE_TENNESSEEWALKER_BLACKRABICANO","A_C_HORSE_TENNESSEEWALKER_CHESTNUT","A_C_HORSE_TENNESSEEWALKER_DAPPLEBAY","A_C_HORSE_TENNESSEEWALKER_FLAXENROAN","A_C_HORSE_TENNESSEEWALKER_GOLDPALOMINO_PC","A_C_HORSE_TENNESSEEWALKER_MAHOGANYBAY","A_C_HORSE_TENNESSEEWALKER_REDROAN"};
inline constexpr std::array<std::string_view,1> kTennesseeWalkerHost{"A_C_HORSE_TENNESSEEWALKER_MAHOGANYBAY"};   // Arthur's Chapter 1 mount; Tilly alone rides it here.
inline constexpr std::array<std::string_view,6> kTennesseeWalkerSpawn{
    "A_C_HORSE_TENNESSEEWALKER_BLACKRABICANO","A_C_HORSE_TENNESSEEWALKER_CHESTNUT","A_C_HORSE_TENNESSEEWALKER_DAPPLEBAY","A_C_HORSE_TENNESSEEWALKER_FLAXENROAN","A_C_HORSE_TENNESSEEWALKER_GOLDPALOMINO_PC","A_C_HORSE_TENNESSEEWALKER_REDROAN"};
// The Appaloosa race (the user, 2026-09-21): the host rides their own Appaloosa - Uncle his epilogue
// few spot buckskin, Charles his Taima, Trelawny his Gwydion (the game's own labels) - and the jockeys
// draw from the six ordinary coats. The black snowflake was left out on the grounds that story mode
// does not carry it; the player's compendium counts six Appaloosa coats and the game's own table lists
// all six (the user, 2026-09-22), so it rides.
inline constexpr std::array<std::string_view,6> kAppaloosaCoats{
    "A_C_HORSE_APPALOOSA_BLACKSNOWFLAKE","A_C_HORSE_APPALOOSA_BLANKET","A_C_HORSE_APPALOOSA_BROWNLEOPARD","A_C_HORSE_APPALOOSA_FEWSPOTTED_PC","A_C_HORSE_APPALOOSA_LEOPARD","A_C_HORSE_APPALOOSA_LEOPARDBLANKET"};
inline constexpr std::array<std::string_view,3> kAppaloosaHost{"A_C_HORSE_GANG_UNCLE_ENDLESSSUMMER","A_C_HORSE_GANG_CHARLES","A_C_HORSE_GANG_TRELAWNEY"};   // Named mounts, each for its owner only.
inline constexpr std::array<std::string_view,6> kAppaloosaSpawn{
    "A_C_HORSE_APPALOOSA_BLACKSNOWFLAKE","A_C_HORSE_APPALOOSA_BLANKET","A_C_HORSE_APPALOOSA_BROWNLEOPARD","A_C_HORSE_APPALOOSA_FEWSPOTTED_PC","A_C_HORSE_APPALOOSA_LEOPARD","A_C_HORSE_APPALOOSA_LEOPARDBLANKET"};
struct Breed {
    std::string_view id;         // The Breed= value in a route .ini.
    std::span<const std::string_view> host_horses; // Preference order.
    std::span<const std::string_view> spawn;
    std::span<const std::string_view> allowed;
    // Shown to the player when their mount is turned away at the entry prompt.
    std::string_view name, name_zh;
};
inline constexpr std::array<Breed,8> kBreeds{{
    {"KentuckySaddler",kUncleHorses,kSaddlerCoats,kSaddlerCoats,"Kentucky Saddler","肯塔基骑乘马"},
    {"Mustang",kSadieHorses,kMustangSpawnCoats,kMustangAllowedCoats,"Mustang","美洲野马"},
    {"Thoroughbred",kThoroughbredHostHorses,kThoroughbredSpawnCoats,kThoroughbredAllowedCoats,"Thoroughbred","纯血马"},
    {"Arabian",kArabianHost,kArabianSpawn,kArabianCoats,"Arabian","阿拉伯马"},
    {"AmericanStandardbred",kAmericanStandardbredHost,kAmericanStandardbredSpawn,kAmericanStandardbredCoats,"American Standardbred","美国标准种马"},
    {"AmericanPaint",kAmericanPaintHost,kAmericanPaintSpawn,kAmericanPaintCoats,"American Paint","美国花马"},
    {"TennesseeWalker",kTennesseeWalkerHost,kTennesseeWalkerSpawn,kTennesseeWalkerCoats,"Tennessee Walker","田纳西走马"},
    {"Appaloosa",kAppaloosaHost,kAppaloosaSpawn,kAppaloosaCoats,"Appaloosa","阿帕卢萨马"},
}};
// Host, rivals and the player all start on one grid, so the rival count is
// bounded by how many that grid seats. race_circuit_start.h asserts the match.
inline constexpr int kMaxFieldRivals=7;

inline const Breed* FindBreed(std::string_view id) {
    const auto it=std::find_if(kBreeds.begin(),kBreeds.end(),[&](const Breed& b){return b.id==id;});
    return it==kBreeds.end()?nullptr:&*it;
}
// Whether a model is this breed, by the same rule for coats and named horses.
inline bool SameBreed(const Breed& breed,std::string_view model) {
    if(breed.spawn.empty())return false;
    const auto token=BreedToken(model);
    return !token.empty() && token==BreedToken(breed.spawn.front());
}
inline bool SpawnableCoat(const Breed& breed,std::string_view name) {
    return std::find(breed.spawn.begin(),breed.spawn.end(),name)!=breed.spawn.end();
}
} // namespace horse_races::field_race
