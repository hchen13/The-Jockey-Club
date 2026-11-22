// Private implementation of race_showcase.cpp; included inside its original namespace.
// Horse tack inspection and verified entry reveal.
Hash AssetCategory(Ped ped, int asset_index)
{
    // feud1.c (1436), func_1376: this index belongs to the ASSET list, not the
    // separate category list exposed by 0xCCB97B51893C662F. Those orders differ!
    return static_cast<Hash>(PED::_0x9B90842304C938A7(ped, asset_index, 0));
}

std::string TackFingerprint(Ped horse)
{
    std::ostringstream out;
    const int count = std::clamp(PED::_GET_NUM_COMPONENTS_IN_PED(horse), 0, 128);
    // Log all horse drawables: component categories differ across tack bundles/builds.
    for (int i = 0; i < count; ++i) {
        Hash d{}, a{}, n{}, m{};
        if (PED::GET_META_PED_ASSET_GUIDS(horse, i, &d, &a, &n, &m)) {
            out << std::hex << AssetCategory(horse, i) << ':' << d << ':' << a << ';';
        }
    }
    return out.str();
}

void ShowEntry()
{
    Entry& entry = g_entries[g_cursor];
    if(!entry.appearance.Reveal())return;
    entry.tack = TackFingerprint(entry.horse);
    TASK::TASK_STAND_STILL(entry.rider, -1);
    PED::SET_PED_KEEP_TASK(entry.rider, true);
    entry.actual_preset=entry.appearance.current;
    const std::string label=EntryLabel(entry);
    LabelEntry(entry);
    Log(label + " site_rider=" + g_site.rider_model + " authored_outfit=" + std::to_string(g_site.rider_outfit) +
        " actual_rider_model=" + (entry.donor_name ? entry.donor_name : g_site.rider_model) +
        " requested_preset=" + std::to_string(entry.donor_preset) +" actual_preset="+std::to_string(entry.actual_preset)+
        " rider_components=" + std::to_string(entry.clothing_changes) +
        " clothing_status=" + entry.clothing_status +
        " horse=" + entry.horse_name + " saddle=native_NPC_random components=" + entry.tack);
    entry.shown = true;
    Notify((g_site.all_rider_presets ? label :
        std::string(i18n::Text("Race display: pair #", "骑手展示：第 ")) + std::to_string(g_cursor + 1)) +
        " | "+PageLabel());
    NextEntry();
}
