// Progress, music, race and camp page rendering.
// Included at the original definition site; declaration and initialization order are preserved.
void DrawSimplePage(const char* title,const std::vector<const char*>& labels,const std::vector<std::string>& values,int selected,const char* hint,float footer_height=0) {
    constexpr float x=.72F,width=.48F,left=x-width/2+.013F,top=.08F;
    const float height=.17F+static_cast<float>(labels.size())*.047F+footer_height;
    GRAPHICS::DRAW_RECT(x,top+height*.5F,width,height,12,10,9,235,false,true);
    GRAPHICS::DRAW_RECT(x,top+.03F,width,.06F,115,25,22,245,false,true);
    Text(title,left,top+.01F,.34F);
    for(std::size_t row=0;row<labels.size();++row) {
        const float y=top+.09F+static_cast<float>(row)*.047F;
        if(static_cast<int>(row)==selected)GRAPHICS::DRAW_RECT(x,y+.013F,width-.01F,.04F,105,47,36,220,false,true);
        Text(labels[row],left,y,.26F,static_cast<int>(row)==selected);
        Text(values[row],x+.008F,y,.25F,static_cast<int>(row)==selected);
    }
    Text(hint,left,top+.09F+static_cast<float>(labels.size())*.047F+.01F,.21F);
}
std::string NativeBlockSummary(std::uint32_t mask,bool unknown) {
    const std::array<const char*,13> names{
        tr::Text("Race history", "原生赛马历史"),tr::Text("Race availability", "原生赛马可用状态"),
        tr::Text("Story progress", "剧情进度"),tr::Text("Current region", "当前地区"),
        tr::Text("Regional quota", "地区赛马次数"),tr::Text("Another encounter", "其他随机事件占用"),
        tr::Text("Encounter slots or density", "随机事件名额或密度已满"),
        tr::Text("Native dispatch busy", "原生调度忙碌"),tr::Text("Native system", "原生系统限制"),
        tr::Text("Current location", "当前地点限制"),tr::Text("Player state", "玩家状态"),
        tr::Text("Human cargo or lasso", "携带或套索人物"),tr::Text("Law or conflict", "执法或冲突状态")};
    mask&=(1u<<names.size())-1;
    if(!mask)return {};
    std::size_t first=0;while(!(mask&(1u<<first)))++first;
    const bool multiple=(mask&(mask-1))!=0;
    std::string result=names[first];
    result+=multiple?tr::Text(" and other conditions unmet"," 等多种条件不满足"):
        tr::Text(" condition unmet"," 条件不满足");
    if(unknown)result+=tr::Text("; others unknown","；其他未知");
    return result;
}
void DrawEncounterPage() {
    constexpr float x=.68F,width=.56F,left=x-width/2+.013F,right=.70F,top=.08F;
    // A single current-save snapshot drives all read-only fields. No reward or
    // native condition is modified by this page or by its marker preference.
    const auto state=horse_races::NativeEncounterState();
    const auto status=[](horse_races::NativeRaceRewardState value) {
        switch(value) {
        case horse_races::NativeRaceRewardState::Yes:return tr::Text("Yes","是");
        case horse_races::NativeRaceRewardState::No:return tr::Text("No","否");
        default:return tr::Text("Unknown","未知");
        }
    };
    const auto cooldown=[&](const std::optional<int>& value,const char* unit) {
        return value && *value>=0?std::to_string(*value)+" "+unit:
            std::string(status(horse_races::NativeRaceRewardState::Unknown));
    };
    GRAPHICS::DRAW_RECT(x,top+.40F,width,.80F,12,10,9,235,false,true);
    GRAPHICS::DRAW_RECT(x,top+.03F,width,.06F,115,25,22,245,false,true);
    Text(tr::Text("Race Encounter Options","遭遇赛选项"),left,top+.01F,.34F);
    GRAPHICS::DRAW_RECT(x,top+.103F,width-.01F,.04F,105,47,36,220,false,true);
    Text(tr::Text("Encounter Markers","遭遇赛标记"),left,top+.09F,.27F,true);
    Text(horse_races::RandomRaceWorldMapMarkers()?tr::Text("Show","显示"):tr::Text("Hide","隐藏"),.79F,top+.09F,.26F,true);
    if(encounter_map_save_failed)
        Text(tr::Text("Save failed; previous setting retained.","保存失败，保留之前的设置。"),left,top+.133F,.20F);
    else if(state.rewards.bonding!=horse_races::NativeRaceRewardState::Yes ||
            state.rewards.speed!=horse_races::NativeRaceRewardState::Yes)
        Text(tr::Text("Possible race locations on the world map.","大地图上可能出现赛马的地点。"),left,top+.133F,.20F);
    GRAPHICS::DRAW_RECT(x,top+.163F,width-.026F,.0018F,120,100,82,200,false,true);
    Text(tr::Text("Native Race Rewards","原生赛马奖励"),left,top+.18F,.23F);
    // Two independent native rewards, not editable switches.
    Text(tr::Text("Reward 1","奖励 1"),left,top+.215F,.22F);
    Text(tr::Text("Reward 2","奖励 2"),right,top+.215F,.22F);
    Text(tr::Text("Faster Bonding When Spurring","策马时更快提升亲密度"),left,top+.245F,.21F);
    Text(tr::Text("Horse Speed Improved","马匹速度提升"),right,top+.245F,.21F);
    Text(status(state.rewards.bonding),left,top+.275F,.26F);
    Text(status(state.rewards.speed),right,top+.275F,.26F);
    GRAPHICS::DRAW_RECT(x,top+.315F,width-.026F,.0018F,120,100,82,200,false,true);
    Text(tr::Text("Native Encounter Conditions","原生遭遇赛条件"),left,top+.33F,.22F);
    const std::array<const char*,7> labels{
        tr::Text("Race Event Cooldown","赛马事件冷却"),
        tr::Text("Shared Encounter Cooldown","共用遭遇冷却"),
        tr::Text("Daytime (06:00-19:59)","白天（06:00-19:59）"),
        tr::Text("Mounted","骑乘状态"),
        tr::Text("Primary Horse Bonding (Level 3+)","登记主马亲密度（至少3级）"),
        tr::Text("Speed/Handling Eligible (Speed 3+ OR Handling Standard+)","速度（3格以上）或操控（标准以上）"),
        tr::Text("Other Native Restrictions","其他原生限制")};
    const std::array<std::string,7> values{
        cooldown(state.race_cooldown_steps,tr::Text("native deductions","次原生扣减")),
        cooldown(state.shared_cooldown_seconds,tr::Text("runtime s","游戏运行秒")),
        status(state.daytime),status(state.mounted),status(state.bonding),status(state.horse_attributes),
        status(state.other_native_conditions)};
    for(std::size_t row=0;row<labels.size();++row) {
        const float y=top+.37F+static_cast<float>(row)*.047F;
        const auto reason=row==6?NativeBlockSummary(state.native_block_reasons,state.native_reasons_unknown):std::string{};
        if(!reason.empty())Text(reason,left,y,.22F);
        else {Text(labels[row],left,y,row==5?.20F:.23F);
            Text(row==6 && state.native_reasons_unknown?status(horse_races::NativeRaceRewardState::Unknown):values[row],.765F,y,.22F);}
    }
    Text(tr::Text("ENTER / LEFT / RIGHT: Show/Hide   BACK: return   F11: close","回车／左右：显示或隐藏｜退格返回｜F11关闭"),left,top+.745F,.21F);
}
void DrawProgressPage() {
    DrawSimplePage(tr::Text("Race Progress Control","赛马进度控制"),
        {tr::Text("Reset all race progress","重置所有赛马进度"),tr::Text("Unlock all","全部解锁"),
         tr::Text("Unlock next entry","解锁下一条进度"),tr::Text("Revert One Progress Step","回退一步进度")},
        {tr::Text("Reset","重置"),
         tr::Text("Unlock","解锁"),tr::Text("Unlock","解锁"),tr::Text("Revert","回退")},progress_selected,
        "",.055F);
    Text(progress_summary+(progress_victories.empty()?"":"  |  "+progress_victories),.493F,.365F,.22F);
    Text(progress_status.empty()?tr::Text("Reset affects both campaigns; horse records are retained.","重置作用于两位主角的赛程，保留马匹战绩。"):progress_status.c_str(),.493F,.402F,.20F);
    Text(tr::Text("UP/DOWN: row  ENTER: act  BACK: return  F11: close","上下选项｜回车执行｜退格返回｜F11 关闭"),.493F,.450F,.21F);
}
void DrawMusicPage() {
    std::vector<const char*> labels;
    std::vector<std::string> values;
    for(std::size_t i=0;i<music::kCount;++i) {
        labels.push_back(tr::Text(music::specs[i].english,music::specs[i].chinese));
        values.push_back(std::to_string(music_values[i])+"%");
    }
    DrawSimplePage(tr::Text("Race BGM volume","赛马BGM音量"),labels,values,music_selected,
        tr::Text("UP/DOWN: row  LEFT/RIGHT: 5%  ENTER: default  BACK: return  F11: close",
                 "上下选项｜左右调节5%｜回车恢复默认｜退格返回｜F11关闭"),.055F);
    Text(music_save_failed?tr::Text("Save failed. Previous volume retained.","保存失败，保留原音量。"):
         music_saved?tr::Text("Saved. Applies when this category next starts.","已保存，该类曲目下次播放时生效。"):
         tr::Text("Auto-saved. Applies when this category next starts.","自动保存，该类曲目下次播放时生效。"),.493F,.535F,.20F);
}
void DrawRacePage() {
    const auto& cue=horse_races::cues::Settings();
    const auto on_off=[](bool v){return std::string(v?tr::Text("ON","开启"):tr::Text("OFF","关闭"));};
    const auto& survey=auto_survey_state;
    const auto& force_probe=horse_motion::force_trial::probe;
    const auto force_mode=[](horse_motion::force_trial::Mode mode) {
        return std::string(mode==horse_motion::force_trial::Mode::CenterOfMass?
            tr::Text("B COM","B 重心"):tr::Text("A point","A 原接口"));
    };
    const auto& force_report=force_probe.Report();
    const bool force_invalid=force_probe.HasDispatch() && force_report.valid==horse_motion::force_trial::Validity::Invalid;
    const bool force_pending=force_probe.HasDispatch() && force_report.actual==horse_motion::force_trial::Actual::None;
    const std::string force_value=!survey.running?
        (force_probe.Requested()?tr::Text("Next: B COM","下次：B 重心"):tr::Text("Next: A point","下次：A 原接口")):
        force_invalid?
        (force_report.expected==horse_motion::force_trial::Mode::CenterOfMass?tr::Text("B INVALID > A","B 无效 → A"):tr::Text("A INVALID","A 无效")):
        force_probe.Active()?(force_pending?
            std::string(force_report.expected==horse_motion::force_trial::Mode::CenterOfMass?tr::Text("B pending","B 待施力"):tr::Text("A pending","A 待施力")):
            force_mode(force_probe.CurrentMode()))+tr::Text(" / next "," / 下位 ")+(force_probe.Requested()?"B":"A"):
        force_probe.HasDispatch()?(force_report.expected==horse_motion::force_trial::Mode::CenterOfMass?tr::Text("B complete","B 已结束"):tr::Text("A complete","A 已结束")):
        force_probe.Requested()?tr::Text("REQUEST B COM","请求 B 重心"):tr::Text("OFF: A point","关：A 原接口");
    const std::string force_hint=std::string(tr::Text("Next dispatch: ","下位派出："))+force_mode(force_probe.NextMode())+". "+
        tr::Text("ENTER toggles A/B. Failed or mixed dispatches show INVALID and cannot count as A/B evidence; retry waits for a new dispatch. Only the watched trial NPC changes. Formal races, player and others keep A. Loading resets OFF. Falls learn/rebuild the line: compare revisions, horse and ranks before judging.",
            "回车切换 A/B。失效或混合派次会标无效，不得计入对照；同派次不再重试，下位重新固定。仅观察中的试跑 NPC 生效；正式比赛、玩家、其他骑手仍用 A，载档默认关。摔马会学习重算赛线，对照须核对路线版本、马型和能力。");
    const std::string survey_value=survey.running?
            std::string(tr::Text("RIDER ","第 "))+std::to_string(survey.riders)+tr::Text(", falls ","位骑手，摔 ")+std::to_string(survey.incidents)+tr::Text("","次"):
        survey.armed?std::string(tr::Text("ARMED: the next race","已武装：下一场比赛")):
        std::string(tr::Text("OFF","关闭"));
    DrawSimplePage(tr::Text("Race debug","赛马调试"),
        {tr::Text("Start rear","起跑站立动作"),tr::Text("Starter shot","发令枪"),
         tr::Text("Route recording (1 m per point; world cleared)","路线录制（每米一点；清空周围行人车马动物）"),
         tr::Text("Auto survey: trial riders","自动勘探：试跑骑手"),
         tr::Text("Trial force A/B (next rider)","试跑施力 A/B（下位骑手）")},
        {on_off(cue.rear_enabled),on_off(cue.starter_shot),
         route_recording?tr::Text("RECORDING","录制中"):tr::Text("OFF","关闭"),survey_value,force_value},
        race_selected,race_selected==RaceRouteRecord?
            tr::Text("ENTER starts where you stand, ENTER again stops; the file lands in TheJockeyClub/courses. Meanwhile nothing spawns and every ped, animal and vehicle nearby is removed.",
                "回车从你站的地方开始、再按一次结束；文件写入 TheJockeyClub/courses。录制期间不刷新任何东西，附近的行人、动物、车马都会被清掉。"):
        race_selected==RaceTrialForce?
            force_hint.c_str():
        race_selected==RaceAutoSurvey?
            tr::Text("ENTER arms a trial: the host rides your next race alone. The camera follows him; move the mouse to look around, with no button held. Your horse stays parked. A fall is learned, the line rebuilt, and the next rider starts 200 metres before it. ENTER again disarms or stops the trial.",
                "回车武装试跑：下一场由 host 独自跑线，镜头跟随他；无需按住右键，移动鼠标即可自由环视，你的马留在原地。摔倒后学习、重算赛线，下一位骑手从摔点前 200 米出发。再按回车取消武装或停止试跑。"):
            tr::Text("UP/DOWN: row  ENTER: toggle  BACK: return","上下选项｜回车切换｜退格返回"));
}
void DrawCampPage() {
    DrawSimplePage(tr::Text("Arthur's camp","亚瑟营地"),
        {tr::Text("Survey nearby scene","导出周围场景"),tr::Text("Scene recording (walk round it)","场景录制（绕着走一圈）"),
         tr::Text("Object aiming (right mouse, then left)","物件精细瞄准（右键瞄准，左键发射）"),
         tr::Text("Arthur: go and do this now","亚瑟：立刻去做这个"),
         tr::Text("Mary: go and do this now","玛丽：立刻去做这个"),
         tr::Text("Camp clutter: learn again","营地杂物：重新学习")},
        {tr::Text("Run","执行"),scene_recording?tr::Text("RECORDING","录制中"):tr::Text("OFF","关闭"),
         aim_mode?tr::Text("ON","开启"):tr::Text("OFF","关闭"),
         camp_activities.empty()?tr::Text("no camp","营地未建"):camp_activities[std::min(camp_activity,camp_activities.size()-1)],
         mary_activities.empty()?tr::Text("not here","她不在"):mary_activities[std::min(mary_activity,mary_activities.size()-1)],
         std::string(camp_sweep_learning?tr::Text("LEARNING","学习中"):tr::Text("DONE","已完成"))+" ("+std::to_string(camp_sweep_known)+tr::Text(" known)"," 件已知)")},
        camp_selected,camp_selected==CampSweep?
            tr::Text("Learning stops by itself once a minute near the camp finds nothing new. Accept to learn again if something was missed.",
                "在营地附近一分钟没发现新东西就自动停止。有漏网的就确认一次重新学习。"):
            camp_selected==CampArthur?
            tr::Text("Left/right chooses; accept sends him. He finishes what he is doing first, then walks over.",
                "左右选择，确认发送。他会先把手上的动作做完，再走过去。"):
            camp_selected==CampMary?
            tr::Text("Left/right chooses; accept sends her. She finishes what she is doing first, then walks over.",
                "左右选择，确认发送。她会先把手上的动作做完，再走过去。"):
            tr::Text("Aiming: hold the right mouse button for the crosshair, left fires; hits go to TheJockeyClub-aim.txt.",
                "瞄准：按住鼠标右键出现准星，左键发射；命中写入 TheJockeyClub-aim.txt。"));
}
