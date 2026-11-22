#pragma once
#include "horse_catalog.h"
#include "game_natives.h"
#include <chrono>
#include <limits>

namespace horse_wheel {
// short_update.c func_225 fills this native container from a fixed SP model
// switch. Online coats can have real labels but no entry in that switch.
// Supplement only missing text in its existing visible card. We never own,
// create or remove the wheel, its stats, or any horse/inventory identity.
class Labels {
    using Clock=std::chrono::steady_clock;
    Clock::time_point next_{};
    Hash logged_model_=0;
    static std::string Read(Any parent,const char* field) {
        // The old SDK incorrectly declares this pointer-returning native Any.
        // 1491 wrapper f60374 writes RAX (64 bits), not EAX. Copy before the
        // next call: both reads use the same native scratch buffer.
        const char* text=invoke<const char*>(0x6323AD277C4A2AFB,parent,field);
        return text?text:"";
    }
    static bool Missing(std::string_view value){return value.empty() || value=="NULL";}
public:
    template<class Log> void Tick(const horse_catalog::Catalog& catalog,Log log,
                                 Clock::time_point now=Clock::now()) {
        if(now<next_)return;
        next_=now+std::chrono::milliseconds(100);
        if(NETWORK::NETWORK_IS_SESSION_ACTIVE())return;
        using namespace DATABINDING;
        const Any root=_DATABINDING_GET_DATA_CONTAINER_FROM_PATH("quick_select.target_horse_info");
        if(!root || root>std::numeric_limits<Hash>::max() || !DATABINDING_IS_ENTRY_VALID(static_cast<Hash>(root)) ||
            !_DATABINDING_READ_DATA_BOOL_FROM_PARENT(root,"visible"))return;
        const std::string name=Read(root,"horseName"),coat=Read(root,"horseCoat");
        const bool missing_name=Missing(name),missing_coat=Missing(coat);
        if(!missing_name && !missing_coat)return;
        // Use the SAME target native as short_update: this is not necessarily
        // the owned/main horse, nor the player's mount while on foot.
        const Ped horse=static_cast<Ped>(HUD::_0x0501D52D24EA8934(true));
        if(!horse || !ENTITY::DOES_ENTITY_EXIST(horse))return;
        const Hash model=ENTITY::GET_ENTITY_MODEL(horse);
        const auto* labels=catalog.Labels(model);
        if(!labels || !story_horses::TextReady(*labels,HUD::DOES_TEXT_LABEL_EXIST))return;
        const char* text=HUD::_GET_LABEL_TEXT_2(labels->breed);
        const std::string compound=text?text:"";
        text=HUD::_GET_LABEL_TEXT_2(labels->coat);
        const std::string color=text?text:"";
        if(Missing(compound) || Missing(color))return;
        const std::string breed=story_horses::BreedOnlyText(compound,color);
        if(missing_name)_DATABINDING_ADD_DATA_STRING(root,"horseName",breed.c_str());
        if(missing_coat)_DATABINDING_ADD_DATA_STRING(root,"horseCoat",labels->coat);
        if(logged_model_!=model) {
            logged_model_=model;
            log("horse wheel: missing native text supplemented horse="+std::to_string(horse)+
                " model="+std::to_string(model)+" breed="+labels->breed+" coat="+labels->coat);
        }
    }
};
}
