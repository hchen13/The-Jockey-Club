// Private implementation of race_bookmaker_runtime.cpp; included inside its original namespace.
// Native catalog titles, subtitles and horse handling presentation.
Hash HandlingHash(const Jockey& jockey){
    const int rank=invoke<int>(0x958DD43D41F89A47,jockey.horse_hash,4);
    const char* key=rank<=1?"HORSE_HANDLING_HEAVY":rank<=3?"HORSE_HANDLING_STANDARD":
        rank<=5?"HORSE_HANDLING_RACE":"HORSE_HANDLING_ELITE";
    return MISC::GET_HASH_KEY(key);
}
void Text(const std::string& value,float x,float y,float scale,int r=245,int g=239,int b=225){
    const bool latin=std::all_of(value.begin(),value.end(),[](unsigned char c){return c<128;});
    const std::string formatted="~s~<FONT FACE='"+std::string(latin?"$Font5":"$body")+"'>"+value+"</FONT>";
    UIDEBUG::_BG_SET_TEXT_SCALE(0,scale);UIDEBUG::_BG_SET_TEXT_COLOR(r,g,b,255);
    bg_text::Display(formatted,x,y);
}
Hash NativeCatalogTitle(bool results=false){
    auto title=bookmaker_title::ResolveType(Venue().shared_attributes,tr::Chinese());
    if(!title.hash)title=bookmaker_title::Resolve(Venue().key,tr::Chinese(),results);
    Log(std::string("native catalog title venue=")+Venue().key+" label="+title.label+
        " hash="+std::to_string(title.hash)+" exact="+std::to_string(title.exact)+
        (title.exact?"; native GenericShop.Title owns presentation":
            "; fallback: verify lml/TheJockeyClub Text/install.xml and tjc_titles.gxt2 are enabled"));
    return title.hash;
}
Hash NativeCatalogSubtitle(bool results=false){
    const auto place=bookmaker_title::ResolveVenue(Venue().key,tr::Chinese(),results);
    Log(std::string("native catalog subtitle venue=")+Venue().key+" label="+place.label+
        " hash="+std::to_string(place.hash)+" exact="+std::to_string(place.exact));
    return place.hash;
}
void Rect(float x,float y,float w,float h,int r,int g,int b,int a=255){GRAPHICS::DRAW_RECT(x,y,w,h,r,g,b,a,false,true);}
void DrawMenu(){
    if(menu.Current()==bm::Menu::Page::Closed || HUD::IS_PAUSE_MENU_ACTIVE())return;
    if(menu.Current()==bm::Menu::Page::Stake){
        // The picker is gone at this point. Native prompt glyphs occupy the
        // same lower-right lane as poker's AMOUNT / RAISE controls.
        EnsureStakePrompts();
        if(stake_prompts[0] && HUD::_UI_PROMPT_IS_VALID(stake_prompts[0])){
            HUD::_UI_PROMPT_SET_TEXT(stake_prompts[3],tr::Literal(tr::Text("BET (","下注（")+
                Dollars(menu.Stake())+tr::Text(")","）")));
            const auto& rider=jockeys[menu.RiderIndex()];
            const std::string heading=std::string(tr::Text(rider.profile->name,rider.profile->chinese))+" - "+
                (menu.SelectedMarket()==bm::Market::Win?tr::Text("Win","独赢"):
                    tr::Text("Place","位置"));
            HUD::_UI_PROMPT_SET_ACTIVE_GROUP_THIS_FRAME(MISC::GET_HASH_KEY("TJC_BOOKMAKER_STAKE"),
                tr::Literal(heading),1,0,0,0);
        }
        return;
    }
    RemoveStakePrompts();
}
void DrawNotice(){if(Now()<notice_until && !HUD::IS_PAUSE_MENU_ACTIVE())Text(current_notice,.45F,.82F,.3F);}
