// Private implementation of race_bookmaker_runtime.cpp; included inside its original namespace.
// Wager, camera and spectator prompt ownership and labels.
struct NativeSpectatorPromptApi {
    using PromptHandle=Prompt;
    using Control=Hash;
    bool Valid(Prompt p) const{return p && HUD::_UI_PROMPT_IS_VALID(p);}
    void Delete(Prompt p) const{HUD::_UI_PROMPT_DELETE(p);}
    Prompt Begin() const{return HUD::_UI_PROMPT_REGISTER_BEGIN();}
    void SetControl(Prompt p,Hash control) const{HUD::_UI_PROMPT_SET_CONTROL_ACTION(p,control);}
    void SetText(Prompt p,const char* label) const{HUD::_UI_PROMPT_SET_TEXT(p,tr::Literal(label));}
    void SetPriority(Prompt p,int priority) const{HUD::_UI_PROMPT_SET_PRIORITY(p,priority);}
    void SetOrdering(Prompt p,Hash control) const{HUD::_UI_PROMPT_SET_ORDERING_AS_INPUT_TYPE(p,control);}
    void SetAttribute(Prompt p,int attribute,bool value) const{HUD::_UI_PROMPT_SET_ATTRIBUTE(p,attribute,value);}
    void SetHold(Prompt p,int duration) const{HUD::_UI_PROMPT_SET_HOLD_MODE(p,duration);}
    void SetStandard(Prompt p) const{HUD::_UI_PROMPT_SET_STANDARD_MODE(p,false);}
    void End(Prompt p) const{HUD::_UI_PROMPT_REGISTER_END(p);}
    void SetGroup(Prompt p,Hash group) const{HUD::_UI_PROMPT_SET_GROUP(p,group,0);}
    void SetVisible(Prompt p,bool value) const{HUD::_UI_PROMPT_SET_VISIBLE(p,value);}
    void SetEnabled(Prompt p,bool value) const{HUD::_UI_PROMPT_SET_ENABLED(p,value);}
    void ActivateGroup(Hash group,const char* heading) const{
        HUD::_UI_PROMPT_SET_ACTIVE_GROUP_THIS_FRAME(group,tr::Literal(heading),1,0,0,0);
    }
};
void RemovePrompts(){
    if(Same(clerk_id))MISC::UNREGISTER_INTERACTION_LOCKON_PROMPT(clerk);
    for(Prompt p:{watch_prompt,bet_prompt})if(p && HUD::_UI_PROMPT_IS_VALID(p))HUD::_UI_PROMPT_DELETE(p);
    watch_prompt=bet_prompt=0;}
void RemoveStakePrompts(){
    NativeSpectatorPromptApi api;bm::RemoveStakePrompts(api,stake_prompts);
}
void EnsureStakePrompts(){
    // Keep the same frontend actions as the menu's actual input handling;
    // poker-only actions do not necessarily expose glyphs outside poker.
    NativeSpectatorPromptApi api;
    bm::EnsureStakePrompts(api,stake_prompts,MISC::GET_HASH_KEY("TJC_BOOKMAKER_STAKE"),
        INPUT_FRONTEND_UP,INPUT_FRONTEND_DOWN,INPUT_FRONTEND_CANCEL,INPUT_FRONTEND_ACCEPT,
        tr::Text("AMOUNT","金额"),tr::Text("BACK","返回"),tr::Text("BET","下注"));
}
void EnablePrompts(bool enabled){for(Prompt p:{watch_prompt,bet_prompt})if(p && HUD::_UI_PROMPT_IS_VALID(p)){
    HUD::_UI_PROMPT_SET_VISIBLE(p,enabled);HUD::_UI_PROMPT_SET_ENABLED(p,enabled);
}}
Prompt MakePrompt(const std::string& title,const char* control){
    const Prompt p=HUD::_UI_PROMPT_REGISTER_BEGIN();
    if(!p)return 0;
    HUD::_UI_PROMPT_SET_CONTROL_ACTION(p,MISC::GET_HASH_KEY(control));
    HUD::_UI_PROMPT_SET_TEXT(p,tr::Literal(title));
    HUD::_UI_PROMPT_SET_PRIORITY(p,0);
    HUD::_UI_PROMPT_SET_TRANSPORT_MODE(p,0);
    HUD::_UI_PROMPT_SET_ATTRIBUTE(p,18,true);
    HUD::_UI_PROMPT_SET_STANDARD_MODE(p,false);
    HUD::_UI_PROMPT_REGISTER_END(p);
    HUD::_UI_PROMPT_SET_VISIBLE(p,true);HUD::_UI_PROMPT_SET_ENABLED(p,true);return p;
}
void CreatePrompts(){RemovePrompts();watch_prompt=MakePrompt(tr::Text("Watch race","观赛"),"INPUT_INTERACT_LOCKON_POS");
    bet_prompt=MakePrompt(tr::Text("Bet on race","赌马"),"INPUT_INTERACT_LOCKON_NEG");
    if(!HUD::_UI_PROMPT_IS_VALID(watch_prompt) || !HUD::_UI_PROMPT_IS_VALID(bet_prompt)){
        RemovePrompts();Log("clerk prompts could not be registered");return;}
    const int group=HUD::_UI_PROMPT_GET_GROUP_ID_FOR_TARGET_ENTITY(clerk);
    HUD::_UI_PROMPT_SET_GROUP(watch_prompt,group,0);HUD::_UI_PROMPT_SET_GROUP(bet_prompt,group,0);
    PED::_SET_PED_PROMPT_NAME(clerk,tr::Literal(Venue().clerk_name));
    PED::SET_PED_CONFIG_FLAG(clerk,130,true);
    PED::SET_PED_CONFIG_FLAG(clerk,178,false);
    PED::SET_PED_CONFIG_FLAG(clerk,297,true);
    PED::SET_PED_CONFIG_FLAG(clerk,301,false);
    PED::SET_PED_CONFIG_FLAG(clerk,315,true);
    PED::SET_PED_CONFIG_FLAG(clerk,317,false);
    PED::SET_PED_CONFIG_FLAG(clerk,331,false);
    PED::SET_PED_CONFIG_FLAG(clerk,493,false);
    if(!MISC::REGISTER_INTERACTION_LOCKON_PROMPT(clerk,"",15.0F,15.0F,32776,
        0.0F,0.0F,watch_prompt,false,-1)){
        Log("clerk lock-on registration refused");RemovePrompts();
    }else EnablePrompts(false);
}
bm::SpectatorCameraPromptLabels CameraPromptLabels(){
    return {tr::Text("Follow Rider","跟拍骑手"),tr::Text("Cinematic Camera","电影模式"),
        tr::Text("Leave Cinematic Camera","退出电影模式"),
        tr::Text("Horse Info","查看马匹属性"),tr::Text("View Rankings","查看排名"),
        tr::Text("Show Rankings","展开排名"),tr::Text("Hide Rankings","收起排名")};
}
void RemoveCameraPrompts(){
    NativeSpectatorPromptApi api;
    bm::RemoveSpectatorCameraPrompts(api,camera_prompts);
    bm::RemoveSpectatorLeavePrompt(api,leave_race_prompt);leave_race_hold.Reset();
}
void EnsureCameraPrompts(){
    NativeSpectatorPromptApi api;
    bm::EnsureSpectatorCameraPrompts(api,camera_prompts,MISC::GET_HASH_KEY("TJC_SPECTATOR_CAMERA"),
        INPUT_AIM,INPUT_CINEMATIC_CAM,INPUT_INTERACT_LOCKON_TARGET_INFO,CameraPromptLabels());
}
std::string LocalLabel(const char* key,const char* fallback){const char* value=HUD::_GET_LABEL_TEXT_2(key);
    return value && *value && std::string_view(value)!="NULL" && std::string_view(value)!=key?value:fallback;}
std::string HorseLabel(const Jockey& jockey){const auto* labels=horse_catalog::current.Labels(jockey.horse_model);
    if(!labels)return tr::Text("Horse","马匹");
    const auto breed=LocalLabel(labels->breed,tr::Text("Horse","马匹"));
    const auto coat=LocalLabel(labels->coat,"");
    const auto short_name=story_horses::BreedOnlyText(breed,coat);
    return short_name.empty()?breed:short_name;
}
