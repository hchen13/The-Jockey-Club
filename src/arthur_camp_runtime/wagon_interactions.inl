// Private implementation of arthur_camp.cpp; included inside its original namespace.
// Wagon prompts and native weapon-locker request lifecycle.
// ---- the wagon's interactions -------------------------------------------------
// John at Arthur's wagon: a plain prompt within reach of the chest opens the
// game's own gun locker, the WEAPON_LOCKER script started the way the
// epilogue's property script starts it (request, start with a 1024 stack,
// release; the script runs the WEAPON_LOCKER app and ends with it).
constexpr float kReach=1.8F;
struct Interaction { std::string key; Prompt prompt=0; bool was_pressed=false; };
Interaction g_locker;
enum class LockerFlow { Idle, Requesting, Running };
LockerFlow g_locker_flow=LockerFlow::Idle;
Clock::time_point g_locker_since{};
bool g_locker_seen=false;
const Placed* PlacedByKey(const std::string& key) {
    for(const auto& p:g_objects)if(p.key==key)return &p;
    return nullptr;
}
Prompt MakePrompt(const char* text,const char* control) {
    const Prompt prompt=HUD::_UI_PROMPT_REGISTER_BEGIN();
    if(!prompt)return 0;
    HUD::_UI_PROMPT_SET_CONTROL_ACTION(prompt,Key(control));
    HUD::_UI_PROMPT_SET_TEXT(prompt,i18n::Literal(text));
    HUD::_UI_PROMPT_SET_PRIORITY(prompt,0);
    HUD::_UI_PROMPT_SET_TRANSPORT_MODE(prompt,0);
    HUD::_UI_PROMPT_SET_STANDARD_MODE(prompt,true);
    HUD::_UI_PROMPT_REGISTER_END(prompt);
    HUD::_UI_PROMPT_SET_VISIBLE(prompt,false);
    HUD::_UI_PROMPT_SET_ENABLED(prompt,false);
    return prompt;
}
void MakeInteractions() {
    g_locker={};g_locker_flow=LockerFlow::Idle;g_locker_seen=false;
    if(const std::string& key=g_layout.interactions.locker;!key.empty()) {
        if(!PlacedByKey(key))Log("weapon locker prompt: prop "+key+" was not placed");
        else {
            g_locker.key=key;
            g_locker.prompt=MakePrompt(i18n::Text("Weapon locker","武器箱"),"INPUT_CONTEXT_Y");
            Log(g_locker.prompt?"weapon locker prompt on "+key:"no weapon locker prompt could be made");
        }
    }
}
void ReleaseInteractions() {
    if(g_locker.prompt && HUD::_UI_PROMPT_IS_VALID(g_locker.prompt))HUD::_UI_PROMPT_DELETE(g_locker.prompt);
    g_locker={};
    if(g_locker_flow!=LockerFlow::Idle)Log("an interaction was dropped with the camp");
    g_locker_flow=LockerFlow::Idle;
}
bool NearProp(const std::string& key,Ped player) {
    const Placed* p=PlacedByKey(key);
    if(!p || !Exists(p->handle))return false;
    const Vector3 a=ENTITY::GET_ENTITY_COORDS(player,true,false),b=ENTITY::GET_ENTITY_COORDS(p->handle,true,false);
    return std::hypot(a.x-b.x,a.y-b.y)<=kReach && std::abs(a.z-b.z)<=2.0F;
}
void TickLocker(const Clock::time_point& now) {
    const Hash script=Key("weapon_locker"),app=Key("WEAPON_LOCKER");
    switch(g_locker_flow) {
    case LockerFlow::Requesting:
        if(SCRIPTS::HAS_SCRIPT_WITH_NAME_HASH_LOADED(script)) {
            const int thread=SCRIPTS::START_NEW_SCRIPT_WITH_NAME_HASH(script,1024);
            SCRIPTS::SET_SCRIPT_WITH_NAME_HASH_AS_NO_LONGER_NEEDED(script);
            Log("weapon locker script started: thread "+std::to_string(thread));
            if(!thread){g_locker_flow=LockerFlow::Idle;return;}
            g_locker_flow=LockerFlow::Running;g_locker_since=now;g_locker_seen=false;
        } else if(now-g_locker_since>5s){Log("weapon locker script did not load in 5 s");g_locker_flow=LockerFlow::Idle;}
        return;
    case LockerFlow::Running: {
        const bool running=UIAPPS::IS_UIAPP_RUNNING_BY_HASH(app)!=0;
        if(running)g_locker_seen=true;
        if((g_locker_seen && !running) || (!g_locker_seen && now-g_locker_since>5s)) {
            Log(g_locker_seen?"weapon locker closed":"weapon locker app never opened");
            g_locker_flow=LockerFlow::Idle;
        }
        return;
    }
    case LockerFlow::Idle:return;
    }
}
void TickInteractions(const Clock::time_point& now) {
    TickLocker(now);
    const Ped player=PLAYER::PLAYER_PED_ID();
    const bool busy=g_locker_flow!=LockerFlow::Idle ||
        UIAPPS::IS_ANY_UIAPP_RUNNING() || PED::IS_PED_ON_MOUNT(player) || HUD::IS_PAUSE_MENU_ACTIVE();
    const auto offer=[&](Interaction& i)->bool{
        if(!i.prompt)return false;
        const bool show=!busy && NearProp(i.key,player);
        HUD::_UI_PROMPT_SET_VISIBLE(i.prompt,show);
        HUD::_UI_PROMPT_SET_ENABLED(i.prompt,show);
        const bool pressed=show && HUD::_UI_PROMPT_IS_PRESSED(i.prompt)!=0;
        const bool began=pressed && !i.was_pressed;i.was_pressed=pressed;
        return began;
    };
    if(offer(g_locker)) {
        SCRIPTS::REQUEST_SCRIPT_WITH_NAME_HASH(Key("weapon_locker"));
        g_locker_flow=LockerFlow::Requesting;g_locker_since=now;
        Log("weapon locker requested at "+g_locker.key);
    }
}
