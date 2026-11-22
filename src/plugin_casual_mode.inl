// Consume the F11 notification in the script loop, after the setting is saved.
void TickCasualModeChange(){
    if(const auto casual=horse_settings::TakeCasualModeChange()){
        horse_races::ResetCatchup();
        Log(*casual?"Casual mode enabled: race catch-up disabled":"Casual mode disabled: race catch-up enabled");
    }
}
