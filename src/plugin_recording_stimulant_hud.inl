// Route recording and stimulant HUD configuration, screen measurement and rendering.
// Included at the original definition site; declaration and initialization order are preserved.
void TickRouteRecorder()
{
    const bool wanted=horse_settings::RouteRecordingWanted();
    if(!wanted && !g_route_recorder.active)return;
    const double now=MISC::GET_GAME_TIMER()/1000.0;
    const Ped player=PLAYER::PLAYER_PED_ID();
    if(!player || !ENTITY::DOES_ENTITY_EXIST(player)) {
        if(g_route_recorder.active){g_route_recorder.Stop(now,"player_gone",Log);horse_settings::SetRouteRecording(false);}
        return;
    }
    const Vector3 at=ENTITY::GET_ENTITY_COORDS(player,true,false);
    const Ped mount=PED::IS_PED_ON_MOUNT(player)?PED::GET_MOUNT(player):0;
    const float speed=ENTITY::GET_ENTITY_SPEED(mount?mount:player);
    if(wanted && !g_route_recorder.active && !g_route_recorder.Start(g_directory,now,at,Log))horse_settings::SetRouteRecording(false);
    else if(!wanted && g_route_recorder.active)g_route_recorder.Stop(now,"user_stopped",Log);
    if(g_route_recorder.active) {
        g_route_recorder.Tick(now,at,speed,player,Log);
        if(!g_route_recorder.active)horse_settings::SetRouteRecording(false);   // It ended on its own.
    }
}
// The sixth core (stimulant_hud.h): the settings, read again every second
// so it can be placed with the game running; the screen's size in pixels,
// taken from the game's window - GET_SCREEN_RESOLUTION answers 1280x720
// whatever the screen, and on a 3440x1440 screen the first core came out a
// third too wide (2026-09-19); and the drawing - the disc and the ring from
// one-pixel rows, the horseshoe from the game's blips texture dictionary in
// the ring's colour, like the game's own fortified cores.
void LoadStimulantHudConfig()
{
    auto& c=g_stimulant_hud;
    c.enabled=LoadFloat("StimulantHud","Enabled","1",0.0F,1.0F,1.0F)>=0.5F;
    c.radar_x=LoadFloat("StimulantHud","RadarX","0.080",0.0F,1.0F,0.080F);
    c.radar_y=LoadFloat("StimulantHud","RadarY","0.824",0.0F,1.0F,0.824F);
    c.radar_radius=LoadFloat("StimulantHud","RadarRadius","0.135",0.01F,0.5F,0.135F);
    c.orbit=LoadFloat("StimulantHud","Orbit","1.33",0.0F,4.0F,1.33F);
    c.angle_degrees=LoadFloat("StimulantHud","AngleDegrees","-33",-360.0F,360.0F,-33.0F);
    c.core_radius=LoadFloat("StimulantHud","CoreRadius","0.138",0.01F,1.0F,0.138F);
    c.ring_thickness=LoadFloat("StimulantHud","RingThickness","0.026",0.001F,0.5F,0.026F);
    c.ring_gap=LoadFloat("StimulantHud","RingGap","0",0.0F,0.5F,0.0F);
    c.icon_scale=LoadFloat("StimulantHud","IconScale","1.3",0.1F,4.0F,1.3F);
    c.r=static_cast<int>(LoadFloat("StimulantHud","ColourR","255",0.0F,255.0F,255.0F));
    c.g=static_cast<int>(LoadFloat("StimulantHud","ColourG","196",0.0F,255.0F,196.0F));
    c.b=static_cast<int>(LoadFloat("StimulantHud","ColourB","64",0.0F,255.0F,64.0F));
    c.screen_width=static_cast<int>(LoadFloat("StimulantHud","ScreenWidth","0",0.0F,16384.0F,0.0F));
    c.screen_height=static_cast<int>(LoadFloat("StimulantHud","ScreenHeight","0",0.0F,16384.0F,0.0F));
    c.calibrate=LoadFloat("StimulantHud","Calibrate","0",0.0F,1.0F,0.0F)>=0.5F;
    c.icon=LoadString("StimulantHud","Icon","blip_ambient_horse");
    if(!c.Valid()){Log("stimulant HUD: settings out of range; the core stays off");c.enabled=false;}
}
struct ScreenPixels {int width=0,height=0;};
BOOL CALLBACK LargestWindowOfThisProcess(HWND window,LPARAM out)
{
    DWORD pid=0;GetWindowThreadProcessId(window,&pid);
    if(pid!=GetCurrentProcessId() || !IsWindowVisible(window))return TRUE;
    RECT r{};if(!GetClientRect(window,&r))return TRUE;
    auto* best=reinterpret_cast<ScreenPixels*>(out);
    const int w=r.right-r.left,h=r.bottom-r.top;
    if(w>0 && h>0 && w*h>best->width*best->height){best->width=w;best->height=h;}
    return TRUE;
}
ScreenPixels g_stimulant_hud_screen;std::string g_stimulant_hud_screen_source;
void MeasureStimulantHudScreen()
{
    const auto& c=g_stimulant_hud;
    ScreenPixels s;std::string source;
    if(c.screen_width>0 && c.screen_height>0){s={c.screen_width,c.screen_height};source="the ini";}
    else {
        EnumWindows(LargestWindowOfThisProcess,reinterpret_cast<LPARAM>(&s));
        if(s.width>0 && s.height>0)source="the game's window";
        else {s={GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN)};source="the primary display";}
        if(s.width<=0 || s.height<=0){GRAPHICS::GET_SCREEN_RESOLUTION(&s.width,&s.height);source="the game";}
    }
    if(s.width!=g_stimulant_hud_screen.width || s.height!=g_stimulant_hud_screen.height || source!=g_stimulant_hud_screen_source) {
        int gw=0,gh=0;GRAPHICS::GET_SCREEN_RESOLUTION(&gw,&gh);
        Log("stimulant HUD: screen "+std::to_string(s.width)+"x"+std::to_string(s.height)+" from "+source+
            "; the game reports "+std::to_string(gw)+"x"+std::to_string(gh));
        g_stimulant_hud_screen=s;g_stimulant_hud_screen_source=source;
    }
}
Clock::time_point g_stimulant_hud_poll{};
horse_races::file_stamp::Stamp g_stimulant_hud_stamp;
void PollStimulantHudConfig()
{
    horse_diagnostics::Scope stage_cost("motion: stimulant hud config");
    if(Clock::now()<g_stimulant_hud_poll)return;
    g_stimulant_hud_poll=Clock::now()+std::chrono::seconds(1);
    // R-UI-13: these keys are hot-read while the HUD is being calibrated, not for ever. The
    // section's reads cost 3.9-6.4 ms (2026-09-21), in the same frame as every other one-second reader.
    const auto stamp=horse_races::file_stamp::Of(g_paths.MainConfig().string());
    if(!g_stimulant_hud.calibrate && g_stimulant_hud_stamp.Known() && stamp==g_stimulant_hud_stamp)return;
    g_stimulant_hud_stamp=stamp;
    LoadStimulantHudConfig();
    MeasureStimulantHudScreen();
}
// One row of the core: a piece from x0 to x1 (pixels right of the centre),
// one rect. The game takes only so many script rects a frame: with a pixel
// at each end for its coverage the horseshoe, drawn last, went missing
// (2026-09-19 18:30), so the rows are plain and the horseshoe is drawn
// between the disc and the ring.
void DrawCorePiece(float cx,float ry,float row_height,float x0,float x1,float fw,int r,int g,int b,int alpha)
{
    const float a=cx+x0,bb=cx+x1;
    if(!(bb>a) || alpha<=0)return;
    GRAPHICS::DRAW_RECT((a+bb)/2/fw,ry,(bb-a)/fw,row_height,r,g,b,alpha,false,false);
}
void DrawStimulantHud(double now)
{
    const auto& c=g_stimulant_hud;
    if(!c.enabled)return;
    const bool active=g_stimulant.Active(now);
    if(!active && !c.calibrate)return;
    horse_races::map_textures::Acquire(horse_races::map_textures::Dictionary::Story,
        horse_races::map_textures::Owner::StimulantHud);
    const int width=g_stimulant_hud_screen.width,height=g_stimulant_hud_screen.height;
    if(width<=0 || height<=0)return;
    const float fw=static_cast<float>(width),fh=static_cast<float>(height);
    const auto centre=horse_races::stimulant_hud::Centre(c,width,height);
    const double whole=horse_races::stimulant_hud::Whole(g_stimulant_config.special_seconds,
        g_stimulant_config.bonding_factor.data(),g_stimulant_config.bonding_factor.size());
    const float fraction=(c.calibrate && !active)?1.0F:
        (whole>0?static_cast<float>(std::clamp(g_stimulant.Remaining(now)/whole,0.0,1.0)):0.0F);
    const float radar_px=c.radar_radius*fh;
    const float disc=c.core_radius*radar_px,r_in=disc+c.ring_gap*radar_px,r_out=r_in+c.ring_thickness*radar_px;
    // One-pixel rows: the disc first, the horseshoe on it, the ring last; the
    // rows at the top and bottom of each circle at the fraction they cover.
    const int top=static_cast<int>(std::floor(centre.y-r_out)),bottom=static_cast<int>(std::ceil(centre.y+r_out));
    const float row_height=1.0F/fh;
    for(int row=top;row<=bottom;++row) {
        const float y=static_cast<float>(row)+.5F-centre.y;
        float half=0;
        if(!horse_races::stimulant_hud::DiscRow(y,disc,half))continue;
        const int alpha=static_cast<int>(220*std::clamp(disc-std::fabs(y)+.5F,0.0F,1.0F));
        DrawCorePiece(centre.x,(static_cast<float>(row)+.5F)/fh,row_height,-half,half,fw,0,0,0,alpha);
    }
    if(TXD::HAS_STREAMED_TEXTURE_DICT_LOADED("blips")) {
        const float box=c.icon_scale*disc;
        GRAPHICS::DRAW_SPRITE("blips",c.icon.c_str(),centre.x/fw,centre.y/fh,box/fw,box/fh,0.0F,c.r,c.g,c.b,255,false);
    }
    for(int row=top;row<=bottom;++row) {
        const float y=static_cast<float>(row)+.5F-centre.y;
        const float ry=(static_cast<float>(row)+.5F)/fh;
        horse_races::stimulant_hud::Span spans[4];
        const int n=horse_races::stimulant_hud::RingRow(y,r_in,r_out,fraction,spans);
        const float edge=std::clamp(r_out-std::fabs(y)+.5F,0.0F,1.0F);
        for(int i=0;i<n;++i) {
            const auto& s=spans[i];
            DrawCorePiece(centre.x,ry,row_height,s.x0,s.x1,fw,s.lit?c.r:55,s.lit?c.g:55,s.lit?c.b:55,static_cast<int>((s.lit?255:190)*edge));
        }
    }
}
horse_motion::PlayerMountRecovery g_mount_recovery;
