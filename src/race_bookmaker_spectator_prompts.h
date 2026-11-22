#pragma once

#include "race_bookmaker_spectator_hud.h"
#include <array>
#include <initializer_list>

namespace horse_races::bookmaking {

// Keep the established slots: the wager text updater owns slot 3. The amount
// row binds both existing directional actions to one native prompt, as stock
// scripts do for two controls in one prompt. Input processing is independent.
template<class Api>
void RemoveStakePrompts(Api& api,std::array<typename Api::PromptHandle,4>& prompts){
    for(auto& prompt:prompts){if(api.Valid(prompt))api.Delete(prompt);prompt={};}
}

template<class Api>
void EnsureStakePrompts(Api& api,std::array<typename Api::PromptHandle,4>& prompts,
    typename Api::Control group,typename Api::Control up,typename Api::Control down,
    typename Api::Control back,typename Api::Control bet,
    const char* amount_label,const char* back_label,const char* bet_label){
    if(api.Valid(prompts[0]) && api.Valid(prompts[2]) && api.Valid(prompts[3]))return;
    RemoveStakePrompts(api,prompts);
    const auto add=[&](std::size_t slot,typename Api::Control control,
        typename Api::Control secondary,const char* label){
        const auto prompt=api.Begin();if(!prompt)return;
        prompts[slot]=prompt;
        api.SetControl(prompt,control);
        if(secondary)api.SetControl(prompt,secondary);
        api.SetText(prompt,label);api.SetPriority(prompt,0);
        api.SetAttribute(prompt,18,true);api.SetStandard(prompt);api.End(prompt);
        api.SetGroup(prompt,group);api.SetVisible(prompt,true);api.SetEnabled(prompt,true);
    };
    add(0,up,down,amount_label);
    add(2,back,{},back_label);
    add(3,bet,{},bet_label);
    if(!api.Valid(prompts[0]) || !api.Valid(prompts[2]) || !api.Valid(prompts[3]))
        RemoveStakePrompts(api,prompts);
}

// Independent of camera mode: normal, followed and cinematic views share
// one ordinary exit. Registration/cleanup never touches another UI's prompt.
template<class Api>
void RemoveSpectatorLeavePrompt(Api& api,typename Api::PromptHandle& prompt){
    if(api.Valid(prompt))api.Delete(prompt);
    prompt={};
}
template<class Api>
void UpdateSpectatorLeavePrompt(Api& api,typename Api::PromptHandle& prompt,
    typename Api::Control group,typename Api::Control back,const char* label,bool ready){
    if(!api.Valid(prompt) && ready){
        prompt=api.Begin();
        if(prompt){
            api.SetControl(prompt,back);api.SetText(prompt,label);
            api.SetPriority(prompt,1);api.SetOrdering(prompt,back);
            api.SetAttribute(prompt,18,true);api.SetHold(prompt,1000);api.End(prompt);
            api.SetGroup(prompt,group);
        }
    }
    if(!api.Valid(prompt))return;
    api.SetText(prompt,label);api.SetVisible(prompt,ready);api.SetEnabled(prompt,ready);
}

template<class Handle>
struct SpectatorCameraPrompts {
    Handle follow{},cinematic{},leave_cinematic{},q{};
};

struct SpectatorCameraPromptLabels {
    const char* follow;
    const char* cinematic;
    const char* leave_cinematic;
    const char* horse_info;
    const char* follow_rankings;
    const char* show_rankings;
    const char* hide_rankings;
};

template<class Api>
void RemoveSpectatorCameraPrompts(Api& api,
    SpectatorCameraPrompts<typename Api::PromptHandle>& prompts){
    for(auto* slot:{&prompts.follow,&prompts.cinematic,&prompts.leave_cinematic,&prompts.q}){
        if(api.Valid(*slot))api.Delete(*slot);
        *slot={};
    }
}

template<class Api>
void EnsureSpectatorCameraPrompts(Api& api,
    SpectatorCameraPrompts<typename Api::PromptHandle>& prompts,
    typename Api::Control group,typename Api::Control aim,
    typename Api::Control cinematic,typename Api::Control q,
    const SpectatorCameraPromptLabels& labels){
    if(api.Valid(prompts.follow) && api.Valid(prompts.cinematic) &&
        api.Valid(prompts.leave_cinematic) && api.Valid(prompts.q))return;
    RemoveSpectatorCameraPrompts(api,prompts);
    const auto add=[&](const char* label,typename Api::Control control,bool hold){
        const auto prompt=api.Begin();
        if(!prompt)return typename Api::PromptHandle{};
        api.SetControl(prompt,control);
        api.SetText(prompt,label);
        api.SetPriority(prompt,1);
        api.SetOrdering(prompt,control);
        api.SetAttribute(prompt,18,true);
        if(hold)api.SetHold(prompt,700);
        else api.SetStandard(prompt);
        api.End(prompt);
        // The game's stock stake prompts also join their group after End.
        api.SetGroup(prompt,group);
        api.SetVisible(prompt,true);
        api.SetEnabled(prompt,true);
        return prompt;
    };
    prompts.follow=add(labels.follow,aim,true);
    prompts.cinematic=add(labels.cinematic,cinematic,true);
    prompts.leave_cinematic=add(labels.leave_cinematic,cinematic,false);
    prompts.q=add(labels.horse_info,q,false);
    if(!api.Valid(prompts.follow) || !api.Valid(prompts.cinematic) ||
        !api.Valid(prompts.leave_cinematic) || !api.Valid(prompts.q))
        RemoveSpectatorCameraPrompts(api,prompts);
}

template<class Api>
void UpdateSpectatorCameraPrompts(Api& api,
    const SpectatorCameraPrompts<typename Api::PromptHandle>& prompts,
    typename Api::Control group,const SpectatorCameraPromptLabels& labels,
    const char* heading,bool input_ready,bool cinematic,bool following,
    SpectatorPromptMode q_mode,bool rankings_visible){
    const auto show=[&](typename Api::PromptHandle prompt,bool visible){
        if(!api.Valid(prompt))return;
        api.SetVisible(prompt,visible);api.SetEnabled(prompt,visible);
    };
    show(prompts.follow,input_ready && !cinematic);
    show(prompts.cinematic,input_ready && !cinematic && !following);
    show(prompts.leave_cinematic,input_ready && cinematic);
    show(prompts.q,input_ready && !cinematic && q_mode!=SpectatorPromptMode::None);
    if(api.Valid(prompts.q)){
        const char* label=q_mode==SpectatorPromptMode::HorseInfo?labels.horse_info:
            q_mode==SpectatorPromptMode::FollowRankings?labels.follow_rankings:
            (rankings_visible?labels.hide_rankings:labels.show_rankings);
        api.SetText(prompts.q,label);
    }
    if(input_ready && api.Valid(prompts.follow) && api.Valid(prompts.cinematic) &&
        api.Valid(prompts.leave_cinematic) && api.Valid(prompts.q))
        api.ActivateGroup(group,heading);
}

} // namespace horse_races::bookmaking
