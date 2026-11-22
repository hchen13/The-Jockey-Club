// Private implementation of race_showcase.cpp; included inside its original namespace.
// Page and entry labels, display completion and skipped-entry progression.
std::size_t PageBegin(){return g_page*kCount;}
std::size_t PageEnd(){return std::min(PageBegin()+kCount,g_entries.size());}
std::size_t PageCount(){return (g_entries.size()+kCount-1)/kCount;}
std::string PageLabel()
{
    return std::string(i18n::Text("Page ","第 "))+std::to_string(g_page+1)+"/"+std::to_string(PageCount())+
        i18n::Text(" | Left/Right: page; F11 display / F10: clear", " 页 | 左右方向键翻页；F11 展示项／F10 清理");
}
std::string EntryLabel(const Entry& entry)
{
    std::string label=g_site.all_rider_presets?std::string(HostOutfitLabel())+std::to_string(entry.donor_preset):
        std::string(i18n::Text("Test #","展示 #"))+std::to_string(&entry-g_entries.data()+1);
    if(!entry.failure_reason.empty())label+=entry.appearance_failed?
        i18n::Text(" [appearance FAILED]","【外观失败】"):i18n::Text(" [NOT REVIEWED: spawn failed]","【生成失败：未验收】");
    else if(!entry.appearance.failures.empty())label+=std::string(i18n::Text(" FAILED -> fallback #"," 原编号失败 → 回退 #"))+
        std::to_string(entry.actual_preset);
    else label+=i18n::Text(" [exact; inspect visually]","【原样显示，请目检】");
    if(entry.excluded)label+=i18n::Text(" [excluded from races]","【正式赛已排除】");
    return label;
}
void LabelEntry(Entry& entry)
{
    if(!entry.position_valid)return; // Never leave a failed-placement blip at (0,0,0).
    if(!entry.blip)entry.blip=MAP::BLIP_ADD_FOR_COORDS(408396114,entry.position);
    if(entry.blip)MAP::_SET_BLIP_NAME(entry.blip,i18n::Literal(EntryLabel(entry)));
}

std::size_t ShownCount()
{
    return static_cast<std::size_t>(std::count_if(g_entries.begin(), g_entries.end(),
        [](const Entry& entry) { return entry.shown; }));
}

void FinishDisplay()
{
    ReleaseOutfit(); // Keep live page model references, just like a live race opponent.
    const auto shown = ShownCount();
    SetPhase(Phase::Visible);
    std::size_t fallback=0,failed=0;
    for(std::size_t i=PageBegin();i<PageEnd();++i) {
        const Entry& entry=g_entries[i];
        fallback+=entry.shown && !entry.appearance.failures.empty();
        failed+=!entry.failure_reason.empty();
        LogEntry(entry,"AUDIT RESULT: "+EntryLabel(entry)+" detail="+entry.failure_reason);
    }
    Log("display complete: site=" + std::to_string(g_site.id) + " shown=" + std::to_string(shown) + "/" +
        std::to_string(PageEnd()-PageBegin())+" fallback="+std::to_string(fallback)+" failed="+std::to_string(failed));
    Notify(std::string(i18n::Text("Race display ON: ", "骑手展示已开启：")) + std::to_string(shown) +
        "/" + std::to_string(PageEnd()-PageBegin()) + i18n::Text(" shown, fallback "," 组显示，回退 ")+
        std::to_string(fallback)+i18n::Text(", failed ","，失败 ")+std::to_string(failed)+". "+PageLabel());
}

void NextEntry()
{
    ReleaseOutfit(); // A fresh request per rider; never assume a spawn request is reusable.
    if (++g_cursor >= PageEnd()) { FinishDisplay(); return; }
    g_candidate = 0;
    g_next_plan = g_next_placement_log = {};
    g_placement_failure.clear();
    SetPhase(Phase::Planning);
}

void SkipEntry(const std::string& reason)
{
    Entry& entry = g_entries[g_cursor];
    LogEntry(entry, "SKIPPED: " + reason);
    entry.actual_preset=entry.appearance.current;
    entry.appearance_failed=entry.appearance.active && entry.appearance.status==appearance::Status::Failed;
    entry.failure_reason=reason;
    DeleteEntry(entry);
    entry.skipped = true;
    LabelEntry(entry);
    Notify(EntryLabel(entry)+i18n::Text("; details in log", "；详情见日志"));
    NextEntry();
}
