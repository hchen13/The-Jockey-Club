#pragma once
#include <cmath>
#include <cstdint>
#include <string>

namespace horse_races::bg_text {
// BG drawing is deferred, but the native submission copies the text immediately.
// VAR_STRING is NOT persistent storage: its global 4 KiB ring can still be
// referenced by vanilla prompts. Literal overlays must not allocate in it.
using Backend=bool(*)(const char*,float,float);
inline Backend backend=nullptr;
inline bool Display(const std::string& text,float x,float y) {
    return backend && !text.empty() && std::isfinite(x) && std::isfinite(y) &&
        backend(text.c_str(),x,y);
}
void Initialize();

struct Slot {
    std::uintptr_t queue=0,records=0;
    std::uint32_t count=0,capacity=0;
    bool valid=false;
};
inline constexpr std::uint32_t kLiteralHash=0xCE12543A; // joaat("LITERAL_STRING")
// Capture/submit/bind run on one script fiber without a yield. Bind only the
// record just appended by our draw. Never prime other slots, edit the text
// ring, or retain pointers to a queue across frames / loads.
template<class Api> bool Submit(Api& api,const char* text,float x,float y) {
    const auto before=api.Capture();
    if(!before.valid || before.count>=before.capacity)return false;
    api.Draw(text,x,y);
    const auto after=api.Capture();
    if(!after.valid || after.queue!=before.queue || after.records!=before.records ||
        after.capacity!=before.capacity || after.count!=before.count+1)return false;
    return api.BindLiteral(before.records+96*static_cast<std::uintptr_t>(before.count));
}
} // namespace horse_races::bg_text
