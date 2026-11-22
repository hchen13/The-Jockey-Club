#pragma once

namespace horse_races::characters {
// Narrative identity is separate from model/outfit, route type and horse model.
// Future hosts must choose explicitly; being a gang model is not sufficient
// (Micah and Dutch are deliberately attackable in this mod's race policy).
enum class Id { OtherRival, MaryBeth, Charles, Sadie, Arthur, John, OtherGangAlly, Micah, Dutch, Pinkerton,
    Uncle, Karen, Pearson, Trelawny, RainsFall, Tilly, Javier, Bill };
constexpr bool ProtectFromFirearms(Id id)
{
    switch (id) {
    case Id::MaryBeth: case Id::Charles: case Id::Sadie:
    case Id::Arthur: case Id::John: case Id::OtherGangAlly:
    case Id::Uncle: case Id::Karen: case Id::Pearson: case Id::Trelawny: case Id::RainsFall: case Id::Tilly:
    case Id::Javier: case Id::Bill: return true;
    default: return false;
    }
}
}
