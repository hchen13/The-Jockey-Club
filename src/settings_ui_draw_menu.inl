// Main settings, difficulty and corridor page rendering.
// Included at the original definition site; declaration and initialization order are preserved.
void DrawMenu(bool override_enabled)
{
    constexpr float x = 0.77F, width = 0.34F, top = 0.10F;
    constexpr float first = 0.075F, step = 0.042F;
    const auto row_y=[&](int row){return top+first+row*step;};
    // The panel and the hints follow the row count: twelve rows overran a
    // panel sized for eight and stacked on the hints (2026-09-15).
    const float rows_end=row_y(kPlayerRows-1)+step;
    const float panel=rows_end+0.115F-top;
    GRAPHICS::DRAW_RECT(x, top + panel*.5F, width, panel, 12, 10, 9, 230, false, true);
    GRAPHICS::DRAW_RECT(x, top + 0.030F, width, 0.060F, 115, 25, 22, 245, false, true);
    Text(tr::Text("The Jockey Club", "The Jockey Club 赛马设置"), x - width/2 + 0.013F, top + 0.010F, 0.38F);
    const std::array<const char*, kRows> names{
        tr::Text("Horse Speed Override", "马速覆盖"),
        tr::Text("Show Live Speed", "显示实时速度"),
        tr::Text("Casual Mode", "休闲模式"),
        tr::Text("Race Encounter Options", "遭遇赛选项"),
        tr::Text("NPC Attributes", "NPC 属性"),
        tr::Text("Course Corridor", "赛道边界"),
        tr::Text("Race Progress Control", "赛马进度控制"),
        tr::Text("Clear membership records", "清除会员记录"),
        tr::Text("End & Reset Race", "结束并重置比赛"),
        tr::Text("Test race host", "测试赛马角色"),
        tr::Text("Dialogue as", "测试对话主角"),
        tr::Text("Host outfit display", "点位 Host 服装展示"),
        tr::Text("Record position & heading", "记录位置与朝向"),
        tr::Text("Race debug", "赛马调试"),
        tr::Text("Arthur's camp", "亚瑟营地"),
        tr::Text("Race BGM volume", "赛马BGM音量")};
    for (int row = 0; row < kPlayerRows; ++row) {
        const float y = row_y(row);
        if (row == selected) GRAPHICS::DRAW_RECT(x, y + 0.013F, width - 0.01F, 0.040F, 105, 47, 36, 220, false, true);
        Text(names[static_cast<std::size_t>(row)], x - width/2 + 0.013F, y, 0.30F, row == selected);
        const char* value=row==RowTestHost?test_host.c_str():
            row==RowTestListener?horse_races::dialogue_test::Name(test_listener):
            row==RowEndActivity?"F10":
            row==RowShowcase?(showcase_active?tr::Text("Clear", "清理"):tr::Text("Show", "展示")):
            row==RowCapturePosition?tr::Text("Record", "记录"):
            row==RowRacePage||row==RowCampPage?tr::Text("Open", "打开"):
            row==RowDifficulty?tr::Text("Open", "打开"):
            row==RowCorridor?tr::Text("Open", "打开"):
            row==RowProgressPage?progress_summary.c_str():
            row==RowMembershipReset?(Clock::now()<membership_reset_confirm_until?tr::Text("Confirm", "确认"):tr::Text("Clear", "清除")):
            row==RowSpeedOverride?(override_enabled?tr::Text("ON","开启"):tr::Text("OFF","关闭")):
            row==RowShowSpeed?(speed_visible?tr::Text("ON","开启"):tr::Text("OFF","关闭")):
            row==RowCasualMode?(horse_races::catchup::CasualMode()?tr::Text("ON","开启"):tr::Text("OFF","关闭")):
            row==RowEncounterPage?tr::Text("Open","打开"):
            tr::Text("Open", "打开");
        // One column and one size for every value: the labels were aligned and
        // the values beside them were not, because the long ones used to start
        // further left to fit. They fit at this size.
        Text(value,x+0.030F,y,0.26F,row == selected);

    }
    const auto progress_detail=progress_summary+(progress_victories.empty()?"":"  |  "+progress_victories);
    const auto* status=selected==RowEncounterPage?
        tr::Text("World-map markers, native rewards and encounter conditions.","大地图标记、原生奖励与遭遇条件。"):
        selected==RowCasualMode?
        (casual_mode_save_failed?tr::Text("Save failed; previous setting retained.","保存失败，保留之前的设置。"):
        tr::Text("ON: rivals gain no extra speed from falling behind.","开启后，对手不会因落后而额外加速。")):
        selected==RowProgressPage?progress_detail.c_str():
        selected==RowMembershipReset?
        (!membership_reset_status.empty()?membership_reset_status.c_str():Clock::now()<membership_reset_confirm_until?
            tr::Text("ENTER again within 5s: clear own sales + membership; keep wins.","5 秒内再次回车：清除当前主角出售与会员记录，保留胜场。"):
            tr::Text("This protagonist: clear sales + membership; keep wins; sell anew.","清除当前主角出售与会员记录，保留马匹胜场；须重新成功卖马。")):
        "";
    Text(status, x-width/2+0.013F, rows_end+0.004F, 0.20F);
    if(selected==RowProgressPage && !progress_status.empty())
        Text(progress_status,x-width/2+0.013F,rows_end+0.025F,0.18F);
    if(selected==RowMembershipReset)
        Text(tr::Text("After clearing, save the game to persist the change.","清除后须保存游戏，才会写入存档。"),x-width/2+0.013F,rows_end+0.025F,0.18F);
    Text(tr::Text("UP/DOWN: select   ENTER: toggle/open", "上下方向键选择，回车切换或打开"), x-width/2+0.013F, rows_end+0.052F, 0.23F);
    Text(tr::Text("F11 / BACKSPACE: close", "F11 / 退格键关闭"), x-width/2+0.013F, rows_end+0.077F, 0.23F);
}
void DrawDifficulty() {
    constexpr float x=.72F,width=.48F,left=x-width/2+.013F,top=.08F;
    const auto opponent=difficulty::At(difficulty_opponent);
    const auto mode=[&](bool speed)->std::string {
        const auto& range=speed?difficulty_rule.speed:difficulty_rule.acceleration;
        if(range.enabled)return tr::Text("Custom Range","自定义范围");
        if(!opponent.host)return tr::Text("Original tiers","原有随机增强");
        if(speed)return std::string(tr::Text("Default ","默认 "))+
            std::to_string(opponent.host->minimum)+"-"+std::to_string(opponent.host->maximum);
        if(opponent.host->accel_minimum<=0) {
            auto value=std::string(tr::Text("Horse Natural","马匹原加速度"));
            if(opponent.host->natural_acceleration_bonus_max>0)value+=" + ["+
                std::to_string(opponent.host->natural_acceleration_bonus_min)+", "+
                std::to_string(opponent.host->natural_acceleration_bonus_max)+"]";
            return value;
        }
        return std::string(tr::Text("Default ","默认 "))+std::to_string(opponent.host->accel_minimum)+
            "-"+std::to_string(opponent.host->accel_maximum);
    };
    GRAPHICS::DRAW_RECT(x,top+.325F,width,.65F,12,10,9,235,false,true);
    GRAPHICS::DRAW_RECT(x,top+.03F,width,.06F,115,25,22,245,false,true);
    Text(tr::Text("NPC Attributes","NPC 属性"),left,top+.01F,.34F);
    const std::array<const char*,kDifficultyRows> labels{tr::Text("Opponent","对手"),tr::Text("Speed mode","速度模式"),
        tr::Text("Speed minimum","最低速度"),tr::Text("Speed maximum","最高速度"),
        tr::Text("Acceleration mode","加速度模式"),tr::Text("Acceleration minimum","最低加速度"),
        tr::Text("Acceleration maximum","最高加速度"),tr::Text("Reset this opponent","恢复当前对手默认值"),
        tr::Text("Cornering Ability","过弯能力")};
    const std::array<std::string,kDifficultyRows> values{tr::Chinese()?opponent.chinese:opponent.name,mode(true),
        difficulty_rule.speed.enabled?std::to_string(difficulty_rule.speed.minimum):"--",
        difficulty_rule.speed.enabled?std::to_string(difficulty_rule.speed.maximum):"--",mode(false),
        difficulty_rule.acceleration.enabled?std::to_string(difficulty_rule.acceleration.minimum):"--",
        difficulty_rule.acceleration.enabled?std::to_string(difficulty_rule.acceleration.maximum):"--","",
        difficulty_rule.cornering>0?std::to_string(difficulty_rule.cornering)+" m/s²":
            std::string(tr::Text("Default ","默认 "))+std::to_string(cornering_envelope)+" m/s²"};
    for(int row=0;row<kDifficultyRows;++row) {
        const float y=top+.078F+row*.047F;
        if(row==difficulty_selected)GRAPHICS::DRAW_RECT(x,y+.013F,width-.01F,.04F,105,47,36,220,false,true);
        Text(labels[row],left,y,.26F,row==difficulty_selected);
        Text(values[row],x+.008F,y,.25F,row==difficulty_selected);
    }
    Text(difficulty_save_failed?tr::Text("Save failed; previous settings retained.","保存失败，保留之前的设置。"):
        tr::Text("Saved. Preview updates now; active races next time.","自动保存；未开赛立即更新，已开赛下场生效。"),left,top+.505F,.20F);
    Text(tr::Text("UP/DOWN: row  LEFT/RIGHT: edit  BACK: return","上下选项｜左右调整｜退格返回"),left,top+.567F,.21F);
}
std::string Metres(float value) {
    std::ostringstream text;text<<std::fixed<<std::setprecision(1)<<value;return text.str();
}
void DrawCorridor() {
    constexpr float x=.72F,width=.48F,left=x-width/2+.013F,top=.08F;
    const bool editable=corridor_view.applicable && !corridor_view.station_key.empty();
    GRAPHICS::DRAW_RECT(x,top+.29F,width,.58F,12,10,9,235,false,true);
    GRAPHICS::DRAW_RECT(x,top+.03F,width,.06F,115,25,22,245,false,true);
    Text(tr::Text("Course Corridor","赛道边界"),left,top+.01F,.34F);
    const std::array<const char*,kCorridorRows> labels{tr::Text("Highlight in world","世界内高亮边界"),
        tr::Text("Course","赛道"),tr::Text("Half-width","半宽"),tr::Text("Reset to route default","恢复路线默认值"),
        tr::Text("Track Width Usage","赛道宽度利用率"),tr::Text("Show Optimal Route","显示最佳路线"),
        tr::Text("NPC Tracking Margin","NPC 跟线余量")};
    const char* highlight=corridor_highlight?tr::Text("ON","开启"):tr::Text("OFF","关闭");
    const std::string half_width=!editable?"--":Metres(corridor_view.effective)+" m ("+
        (corridor_view.override_width?std::string(tr::Text("default ","默认 "))+Metres(corridor_view.route_default):tr::Text("default","默认"))+")";
    const std::array<std::string,kCorridorRows> values{highlight,
        editable?corridor_view.station_key:tr::Text("-- (open race, no corridor)","--（开放赛，无边界）"),half_width,"",
        std::to_string(corridor_line_percent)+"%",corridor_line_highlight?tr::Text("ON","开启"):tr::Text("OFF","关闭"),
        Metres(corridor_tracking_margin)+" m"};
    for(int row=0;row<kCorridorRows;++row) {
        const float y=top+.125F+row*.047F;
        if(row==corridor_selected)GRAPHICS::DRAW_RECT(x,y+.013F,width-.01F,.04F,105,47,36,220,false,true);
        Text(labels[row],left,y,.26F,row==corridor_selected);
        Text(values[row],x+.008F,y,.25F,row==corridor_selected);
    }
    Text(corridor_selected==6?tr::Text("0-5 m in 0.1 m steps; per-side NPC tracking clearance.","0–5 米，步进 0.1 米；NPC 每侧跟线余量。"):
        tr::Text("5-20 m in 0.5 m steps; judged width only, street clearance unchanged.","5–20 米，步进 0.5 米；仅判罚宽度，不改变街道清场。"),left,top+.435F,.21F);
    Text(corridor_save_failed?tr::Text("Save failed; previous settings retained.","保存失败，保留之前的设置。"):
        corridor_selected==6?tr::Text("Applies next race","下一场比赛生效"):
        !corridor_saved?tr::Text("Widening applies now; narrowing mid-race from the next race.","加宽立即生效；比赛中收窄下场生效。"):
        corridor_view.deferred?tr::Text("Saved. Highlight now; legal width from the next race.","已保存；高亮立即更新，判罚宽度下场生效。"):
        tr::Text("Saved. Applies now.","已保存，立即生效。"),left,top+.472F,.20F);
    Text(tr::Text("UP/DOWN: row  LEFT/RIGHT: edit  BACK: return","上下选项｜左右调整｜退格返回"),left,top+.525F,.21F);
}
