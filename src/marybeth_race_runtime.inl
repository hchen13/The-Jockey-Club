// Included inside horse_races' private namespace. All game calls stay on the
// ScriptHook script thread; the pure policy/approved-ID tables live in headers.
double MaryNow() { return std::chrono::duration<double>(Clock::now().time_since_epoch()).count(); }
marybeth_dialogue::Identity MaryIdentity(Ped entity)
{
    const auto identity=HorseIdentity(entity);
    return {identity.horse,identity.model,identity.address};
}
bool MaryOwned()
{
    return CurrentRules().Hosted() && g_mary.Owns(g_mary_instance,MaryIdentity(g_opponent_rider),
        MaryIdentity(PLAYER::PLAYER_PED_ID()));
}
void BeginMarySession()
{
    g_mary={};
    g_mary.id=++g_mary_instance;
    g_mary.rider=MaryIdentity(g_opponent_rider);
    g_mary.player=MaryIdentity(PLAYER::PLAYER_PED_ID());
    g_mary.phase=marybeth_dialogue::Phase::Offering;
    Log("Mary dialogue instance="+std::to_string(g_mary.id));
}
bool MaryInRange(float radius=40.0F)
{
    const Ped player=PLAYER::PLAYER_PED_ID();
    return MaryOwned() && !ENTITY::IS_ENTITY_DEAD(player) && !ENTITY::IS_ENTITY_DEAD(g_opponent_rider) &&
        DistanceSquared(ENTITY::GET_ENTITY_COORDS(player,true,false),
            ENTITY::GET_ENTITY_COORDS(g_opponent_rider,true,false))<=radius*radius;
}
bool MarySpeechSettled()
{
    return !g_mary.pending && MaryNow()-g_mary.last_speech>=0.8 &&
        !SpeechPlaying(g_opponent_rider) && !SpeechPlaying(PLAYER::PLAYER_PED_ID());
}
bool MaryFriendlyFocus(Ped player)
{
    Entity target=0;
    return MaryInRange(15.0F) && !HUD::IS_PAUSE_MENU_ACTIVE() &&
        PLAYER::GET_PLAYER_TARGET_ENTITY(PLAYER::PLAYER_ID(),&target) && target==g_opponent_rider &&
        !WEAPON::IS_PED_ARMED(player,7) && !PLAYER::IS_PLAYER_FREE_AIMING(PLAYER::PLAYER_ID()) &&
        !PED::IS_PED_SHOOTING(player) && !PED::IS_PED_IN_COMBAT(player,0) &&
        !PED::IS_PED_IN_COMBAT(g_opponent_rider,0) && !PED::IS_PED_IN_MELEE_COMBAT(player);
}
bool MaryHasRoadReference() { return HasRecordedCourse(); }
int MaryLocalLead();
bool MaryResultNearbyAndSlow()
{
    return MaryInRange(12.0F) && !HUD::IS_PAUSE_MENU_ACTIVE() &&
        ENTITY::GET_ENTITY_SPEED(PLAYER::PLAYER_PED_ID())<=2.2F &&
        ENTITY::GET_ENTITY_SPEED(g_opponent_horse)<=0.6F;
}
std::span<const marybeth_dialogue::Clip> FixedVoiceClips()
{
    switch(FixedProfile().id) {
    case characters::Id::Charles:return std::span<const marybeth_dialogue::Clip>(charles_dialogue::kClips);
    case characters::Id::Sadie:return std::span<const marybeth_dialogue::Clip>(sadie_dialogue::kClips);
    case characters::Id::MaryBeth:return std::span<const marybeth_dialogue::Clip>(marybeth_dialogue::kClips);
    case characters::Id::Uncle:return std::span<const marybeth_dialogue::Clip>(gang_dialogue::kUncle);
    case characters::Id::Karen:return std::span<const marybeth_dialogue::Clip>(gang_dialogue::kKaren);
    case characters::Id::Pearson:return std::span<const marybeth_dialogue::Clip>(gang_dialogue::kPearson);
    case characters::Id::Arthur:return std::span<const marybeth_dialogue::Clip>(gang_dialogue::kArthur);
    case characters::Id::John:return std::span<const marybeth_dialogue::Clip>(gang_dialogue::kJohn);
    case characters::Id::Trelawny:return std::span<const marybeth_dialogue::Clip>(gang_dialogue::kTrelawny);
    case characters::Id::RainsFall:return std::span<const marybeth_dialogue::Clip>(gang_dialogue::kRainsFall);
    case characters::Id::Tilly:return std::span<const marybeth_dialogue::Clip>(tilly_dialogue::kClips);
    case characters::Id::Javier:return std::span<const marybeth_dialogue::Clip>(javier_dialogue::kClips);
    case characters::Id::Bill:return std::span<const marybeth_dialogue::Clip>(bill_dialogue::kClips);
    default:return {};
    }
}
// Armed when Talk is taken, spoken as soon as nobody is mid-sentence. The
// rival's own queue also waits for silence, so arming this first is what puts
// the protagonist ahead of the reply that was written to follow him.
struct PlayerReply {bool armed=false,won=false;unsigned mask=0;double expires=0;std::uint64_t session=0;};
PlayerReply g_player_reply;
std::array<std::string_view,8> g_player_reply_last{};
// Asked for exactly the way this mod's working encounter-path call asks: the
// player's own voice, variation 0, no ambient voice switching.
bool SpeakPlayerResultLine(bool won) {
    const Ped player=PLAYER::PLAYER_PED_ID();
    if(!player || ENTITY::IS_ENTITY_DEAD(player))return false;
    const auto model=ENTITY::GET_ENTITY_MODEL(player);
    const auto protagonist=world::ProtagonistOf(model);
    if(protagonist==world::Protagonist::None)return false;
    const bool antagonize=player_voice::IncludeAntagonize(FixedProfile().id);
    const auto bucket=(protagonist==world::Protagonist::Arthur?4U:0U)+(won?2U:0U)+(antagonize?1U:0U);
    const auto choices=player_voice::Shuffled(won,antagonize,g_player_reply_last[bucket],
        [](int begin,int end){return MISC::GET_RANDOM_INT_IN_RANGE(begin,end);});
    for(std::size_t i=0;i<choices.size;++i) {
        const char* context=choices.lines[i];
        if(!AUDIO::DOES_CONTEXT_EXIST_FOR_THIS_PED(player,context,false))continue;
        if(!PlaySpeech(player,g_opponent_rider,context,nullptr,true))continue;
        g_player_reply_last[bucket]=context;
        Log(std::string("player race reply PLAYED context=")+context+" outcome="+(won?"won":"lost")+
            " host="+FixedProfile().key+" pool="+(antagonize?"talk+antagonize":"talk")+
            " player_model="+std::to_string(model)+" attempt="+std::to_string(i+1));
        return true;
    }
    Log("player race reply: every context refused; rival replies alone");
    return false;
}
unsigned FixedListenerMask() {
    return marybeth_dialogue::PlayerMask(dialogue_test::Model(g_test_listener,g_mary.player.model));
}
bool FixedHasVoice(marybeth_dialogue::Event event)
{
    const auto clips=FixedVoiceClips();
    const auto mask=FixedListenerMask();
    return std::any_of(clips.begin(),clips.end(),[=](const auto& clip){return clip.event==event && (clip.players&mask);});
}
std::optional<marybeth_dialogue::Event> FixedTalkEvent()
{
    using E=marybeth_dialogue::Event;
    const E specific=g_mary.won?E::TalkWin:E::TalkLost;
    const bool general=FixedHasVoice(E::Talk), outcome=FixedHasVoice(specific);
    if (outcome && (!general || MISC::GET_RANDOM_INT_IN_RANGE(0,3)==0)) return specific;
    if (general) return E::Talk;
    return std::nullopt;
}
void TickMaryVoice()
{
    if(g_player_reply.armed) {
        const Ped player=PLAYER::PLAYER_PED_ID();
        if(!MaryOwned() || g_player_reply.session!=g_mary.id || MaryNow()>g_player_reply.expires)g_player_reply={};
        else if(!SpeechPlaying(player) && !SpeechPlaying(g_opponent_rider)) {
            SpeakPlayerResultLine(g_player_reply.won);
            g_player_reply={};
            return; // The rival answers on a later frame, after this line.
        }
    }
    using namespace marybeth_dialogue;
    const auto clips=FixedVoiceClips();
    if (!g_mary.pending) return;
    if (!MaryOwned()) { g_mary.pending.reset(); return; }
    const double now=MaryNow();
    auto& pending=*g_mary.pending;
    const bool result_line=pending.event==Event::PlayerWon || pending.event==Event::PlayerLost;
    if (now>pending.expires || !Allowed(pending.event,g_mary.phase,true,g_mary.won,
            MaryHasRoadReference(),CLOCK::GET_CLOCK_HOURS())) {
        if (result_line) { g_mary.result_failed=true; Log("Mary result line expired without playback; not marked played"); }
        g_mary.pending.reset(); return;
    }
    if (result_line && !MaryResultNearbyAndSlow()) return;
    if (pending.event==Event::Passed || pending.event==Event::Ahead) {
        const int lead=MaryLocalLead();
        const int wanted=pending.event==Event::Passed?1:-1;
        if (lead==-wanted) { g_mary.pending.reset(); return; }
        if (lead!=wanted || (wanted==-1 && DistanceSquared(
                ENTITY::GET_ENTITY_COORDS(g_player_mount,true,false),
                ENTITY::GET_ENTITY_COORDS(g_opponent_horse,true,false))<12.0F*12.0F)) return;
    }
    // A line started under the blackout is cut off part way through: the staging
    // fade eats it. Hold anything queued for the start until the screen is back.
    if (CAM::IS_SCREEN_FADED_OUT() || CAM::IS_SCREEN_FADING_IN())return;
    // A trial parks the protagonist while the native camera/listener follows
    // the host. Only the existing burst/start cue may use that spectator
    // audience; ordinary race dialogue keeps its physical listener range.
    const bool trial_start=pending.event==Event::Start && GetTrialObservedHorse().value_or(0)!=0;
    if ((!trial_start && !MaryInRange()) || HUD::IS_PAUSE_MENU_ACTIVE() || now<g_mary.next_speech ||
        SpeechPlaying(g_opponent_rider) || SpeechPlaying(PLAYER::PLAYER_PED_ID())) return;
    const unsigned mask=FixedListenerMask();
    if (!mask) { g_mary.pending.reset(); return; }
    if (pending.clip<0) {
        std::vector<int> choices;
        for (std::size_t i=0;i<clips.size();++i)
            if (clips[i].event==pending.event && (clips[i].players&mask)) choices.push_back(static_cast<int>(i));
        if (choices.empty()) {
            if(result_line)g_mary.result_failed=true;
            Log(std::string(g_rival->voice)+" no eligible line for this event/listener; no other-character fallback");
            g_mary.pending.reset(); return;
        }
        const int previous=g_mary.last_variant[static_cast<std::size_t>(pending.event)]-1;
        if (choices.size()>1) choices.erase(std::remove(choices.begin(),choices.end(),previous),choices.end());
        const int draw=std::clamp(MISC::GET_RANDOM_INT_IN_RANGE(0,static_cast<int>(choices.size())),
            0,static_cast<int>(choices.size())-1);
        pending.clip=choices[draw];
    }
    if (static_cast<std::size_t>(pending.clip)>=clips.size() ||
        clips[pending.clip].event!=pending.event || !(clips[pending.clip].players&mask)) {
        if(result_line)g_mary.result_failed=true;
        Log("fixed voice rejected stale clip/event/listener selection");
        g_mary.pending.reset();return;
    }
    const Clip& clip=clips[static_cast<std::size_t>(pending.clip)];
    const char* voice=clip.native_voice?clip.native_voice:g_rival->voice;
    // Context lookup is relative to the ped's assigned voice. Switch BEFORE
    // lookup, not only in speech params; ES and normal can share a context/variant.
    // We only reach here when neither actor is speaking and this session owns the ped.
    AUDIO::SET_AMBIENT_VOICE_NAME(g_opponent_rider,voice);
    const bool played=AUDIO::DOES_CONTEXT_EXIST_FOR_THIS_PED(g_opponent_rider,clip.context,false) &&
        PlaySpeech(g_opponent_rider,PLAYER::PLAYER_PED_ID(),clip.context,voice,true,clip.variation);
    const float distance=std::sqrt(DistanceSquared(ENTITY::GET_ENTITY_COORDS(PLAYER::PLAYER_PED_ID(),true,false),
        ENTITY::GET_ENTITY_COORDS(g_opponent_rider,true,false)));
    Log(std::string(voice)+" voice instance="+std::to_string(g_mary.id)+" clip="+clip.id+" context="+clip.context+
        " variant="+std::to_string(clip.variation)+(played?" started":" unavailable")+
        " listener="+(mask==1?std::string("Arthur"):mask==2?std::string("John"):std::string("other"))+
        // The raw model settles who the mod thinks the protagonist is. It read
        // Arthur on a John save once, and that mattered: it also picked which
        // of the rival's lines were eligible.
        " player_model="+std::to_string(dialogue_test::Model(g_test_listener,g_mary.player.model))+
        " test_listener="+dialogue_test::Name(g_test_listener)+
        " distance_m="+std::to_string(distance)+" player_mps="+
        std::to_string(ENTITY::GET_ENTITY_SPEED(PLAYER::PLAYER_PED_ID())));
    if (played) {
        if (result_line) g_mary.result_attempted=true; // Only a native-started result counts as played.
        if (pending.event==Event::Passed || pending.event==Event::Ahead) {
            g_mary.next_pass=now+8.0; g_mary.next_banter=now+25.0;
        }
        g_mary.last_variant[static_cast<std::size_t>(pending.event)]=pending.clip+1;
        g_mary.last_speech=now; g_mary.next_speech=now+6.0;
        g_mary.pending.reset();
    } else {
        g_mary.next_speech=now+0.75;
        if (++pending.attempts>=3) {
            if (result_line) g_mary.result_failed=true;
            Log("Mary voice skipped after bounded retries; no random/unreviewed fallback");
            g_mary.pending.reset();
        }
    }
}
bool MaryHorseHealthy(Ped horse)
{
    if (!horse || !ENTITY::DOES_ENTITY_EXIST(horse) || ENTITY::IS_ENTITY_DEAD(horse)) return false;
    const int maximum=ENTITY::GET_ENTITY_MAX_HEALTH(horse,false);
    return maximum>0 && ENTITY::GET_ENTITY_HEALTH(horse)>=maximum*0.8 &&
        ATTRIBUTE::_GET_ATTRIBUTE_CORE_VALUE(horse,0)>=80 && ATTRIBUTE::_GET_ATTRIBUTE_CORE_VALUE(horse,1)>=80;
}
void QueueFixedGreeting(bool new_horse,bool healthy_horse)
{
    using E=marybeth_dialogue::Event;
    const auto clips=FixedVoiceClips();
    const unsigned mask=FixedListenerMask();
    std::vector<int> pool;
    for (std::size_t i=0;i<clips.size();++i) {
        const auto event=clips[i].event;
        if ((clips[i].players&mask) && (event==E::Greeting ||
                (new_horse && event==E::NewHorse) ||
                (healthy_horse && event==E::HealthyHorse)))
            pool.push_back(static_cast<int>(i));
    }
    if (pool.empty()) return;
    const int draw=std::clamp(MISC::GET_RANDOM_INT_IN_RANGE(0,static_cast<int>(pool.size())),
        0,static_cast<int>(pool.size())-1);
    const int selected=pool[draw];
    g_mary.Queue(clips[selected].event,MaryNow(),6.0);
    g_mary.pending->clip=selected;
    Log(std::string("fixed greeting pool selected ")+clips[selected].id+
        " new_horse="+(new_horse?"yes":"no")+" healthy="+(healthy_horse?"yes":"no"));
}
void TickMaryGreeting(Ped player)
{
    using E=marybeth_dialogue::Event;
    if (!MaryOwned() || g_state!=State::Offering) return;
    const bool focused=MaryFriendlyFocus(player) && !MISC::GET_MISSION_FLAG();
    const bool entered=focused && !g_fixed_greeting_focused;
    g_fixed_greeting_focused=focused;
    if (entered && !g_mary.greeting_done) {
        const Ped mount=PED::GET_MOUNT(player);
        auto& meeting=FixedMeeting();
        g_mary.new_horse=meeting.Observe(MaryIdentity(player),MaryIdentity(mount));
        g_mary.greeting_done=true;
        QueueFixedGreeting(g_mary.new_horse,MaryHorseHealthy(mount));
        g_fixed_greeting_next=Clock::now()+std::chrono::seconds(30);
    } else if (entered && Clock::now()>=g_fixed_greeting_next && MarySpeechSettled()) {
        g_mary.Queue(FixedListenerMask()==2 && FixedHasVoice(E::GreetingAgain) ?
            E::GreetingAgain:E::Greeting,MaryNow(),5.0);
        g_fixed_greeting_next=Clock::now()+std::chrono::seconds(30);
    }
    // Releasing lock-on is not a refusal; a short press may have queued the
    // greeting while another character was speaking. Let it play in range.
    TickMaryVoice();
}
// Same-direction nearby travel establishes a local pass without pretending the
// straight-line distance to the station is a race-route progress measurement.
int MaryLocalLead()
{
    // Lapping someone is not a change of race leader even when travelling next
    // to them on the same street. Only close, qualified race distance can pass.
    if(CurrentRules().uses_lap_engine && std::abs(g_player_progress_max-g_opponent_progress_max)>80)return 0;
    const Vector3 p=ENTITY::GET_ENTITY_COORDS(g_player_mount,true,false);
    const Vector3 r=ENTITY::GET_ENTITY_COORDS(g_opponent_horse,true,false);
    const Vector3 pv=ENTITY::GET_ENTITY_VELOCITY(g_player_mount,0);
    const Vector3 rv=ENTITY::GET_ENTITY_VELOCITY(g_opponent_horse,0);
    const float ps=std::hypot(pv.x,pv.y),rs=std::hypot(rv.x,rv.y);
    if (!destination::Finite(p) || !destination::Finite(r) || !destination::Finite(pv) ||
        !destination::Finite(rv) || ps<2.0F || rs<2.0F ||
        (pv.x*rv.x+pv.y*rv.y)/(ps*rs)<0.7F || std::abs(p.z-r.z)>5.0F) return 0;
    const float ax=pv.x/ps+rv.x/rs,ay=pv.y/ps+rv.y/rs,length=std::hypot(ax,ay);
    const float x=ax/length,y=ay/length;
    const float along=(p.x-r.x)*x+(p.y-r.y)*y,across=std::abs((p.x-r.x)*y-(p.y-r.y)*x);
    if (across>10.0F || std::abs(along)>50.0F) return 0;
    return along>2.0F?1:(along<-2.0F?-1:2); // 0 invalid, 2 valid side-by-side.
}
void TickMaryRacing()
{
    using E=marybeth_dialogue::Event;
    if (!MaryOwned() || g_mary.phase!=marybeth_dialogue::Phase::Racing) return;
    const double now=MaryNow();
    const bool touching=ENTITY::IS_ENTITY_TOUCHING_ENTITY(g_player_mount,g_opponent_horse) ||
        ENTITY::IS_ENTITY_TOUCHING_ENTITY(g_player_mount,g_opponent_rider) ||
        ENTITY::IS_ENTITY_TOUCHING_ENTITY(PLAYER::PLAYER_PED_ID(),g_opponent_horse);
    if (touching && !g_mary.touching && !g_mary.pending && now>=g_mary.next_speech) {
        g_mary.Queue(E::Collision,now,2.0);
        g_mary.next_banter=now+20.0;
    }
    g_mary.touching=touching;
    const int lead=MaryLocalLead();
    const int passed=g_mary.ObserveLead(lead,now);
    if (passed) {
        g_mary.pass_direction=passed; g_mary.pass_expires=now+8.0;
        Log(std::string("Mary pass observed: ")+(passed==1?"player passed rival":"rival passed player")+
            "; retained independently of idle-banter cooldown");
    }
    if (now>g_mary.pass_expires || (lead && lead==-g_mary.pass_direction)) g_mary.pass_direction=0;
    const bool final_lap=!CurrentRules().uses_lap_engine || g_laps.plan.Current(g_laps.player)+1==g_laps.plan.Total();
    const bool near_finish=final_lap && destination::Distance(ENTITY::GET_ENTITY_COORDS(PLAYER::PLAYER_PED_ID(),true,false),
        g_route_finish)<85.0F;
    const bool at_finish=final_lap && destination::Distance(ENTITY::GET_ENTITY_COORDS(PLAYER::PLAYER_PED_ID(),true,false),
        g_route_finish)<20.0F;
    if (!at_finish && g_mary.pass_direction && lead==g_mary.pass_direction && !g_mary.pending && now>=g_mary.next_pass) {
        // The reviewed rival-ahead line says "too far behind": don't play it
        // while side by side, nor invert the player-overtaking responses.
        const bool enough_gap=lead==1 || DistanceSquared(ENTITY::GET_ENTITY_COORDS(g_player_mount,true,false),
            ENTITY::GET_ENTITY_COORDS(g_opponent_horse,true,false))>=12.0F*12.0F;
        if (enough_gap) {
            g_mary.Queue(lead==1?E::Passed:E::Ahead,now,g_mary.pass_expires-now);
            g_mary.pass_direction=0;
        }
    }
    if (!near_finish && !g_mary.pending && now>=g_mary.next_banter) {
        if (lead==-1 && g_mary.stable_leader==-1 && now-g_mary.lead_since>=8.0 &&
            MaryInRange() && DistanceSquared(ENTITY::GET_ENTITY_COORDS(g_player_mount,true,false),
                ENTITY::GET_ENTITY_COORDS(g_opponent_horse,true,false))>=12.0F*12.0F) {
            g_mary.Queue(E::Ahead,now,3.0); g_mary.next_banter=now+30.0;
        }
    }
    TickMaryVoice();
}
bool MaryHorseShotByPlayer(Ped horse,int& previous_health)
{
    const Ped player=PLAYER::PLAYER_PED_ID();
    if (!horse || !ENTITY::DOES_ENTITY_EXIST(horse)) return false;
    const int health=ENTITY::GET_ENTITY_HEALTH(horse);
    const bool damaged=health<previous_health;
    previous_health=health;
    if (ENTITY::IS_ENTITY_DEAD(horse))
        return PED::GET_PED_SOURCE_OF_DEATH(horse)==player && WEAPON::IS_WEAPON_A_GUN(PED::GET_PED_CAUSE_OF_DEATH(horse));
    Hash weapon=0;
    return damaged && PED::IS_PED_SHOOTING(player) &&
        WEAPON::GET_CURRENT_PED_WEAPON(player,&weapon,true,0,false) && WEAPON::IS_WEAPON_A_GUN(weapon) &&
        ENTITY::HAS_ENTITY_BEEN_DAMAGED_BY_ENTITY(horse,player,true,true) &&
        WEAPON::_HAS_ENTITY_BEEN_DAMAGED_BY_WEAPON_RECENTLY(horse,weapon,1000);
}
void TickMaryResult()
{
    using E=marybeth_dialogue::Event;
    if (!MaryOwned() || ENTITY::IS_ENTITY_DEAD(PLAYER::PLAYER_PED_ID()) ||
        ENTITY::IS_ENTITY_DEAD(g_opponent_rider)) { BeginDeparture(); return; }
    if ((g_mary.result_attempted || g_mary.result_failed) && !MaryInRange(120.0F)) { BeginDeparture(); return; }
    if (g_result_phase==ResultPhase::Interrupted) {
        TickMaryVoice();
        if ((StateSeconds()>=3.0 && MarySpeechSettled()) || StateSeconds()>=12.0) BeginDeparture();
        return;
    }
    if (!g_opponent_horse || !ENTITY::DOES_ENTITY_EXIST(g_opponent_horse) ||
        ENTITY::IS_ENTITY_DEAD(g_opponent_horse)) { BeginDeparture(); return; }
    const double now=MaryNow();
    const Ped player=PLAYER::PLAYER_PED_ID();
    const float remaining=destination::Distance(ENTITY::GET_ENTITY_COORDS(g_opponent_rider,true,false),g_route_finish);
    const auto listener=ENTITY::GET_ENTITY_COORDS(player,true,false);
    const float listener_to_finish=destination::Distance(listener,g_route_finish);
    if(g_player_won || listener_to_finish<=65)g_mary.result_listener_seen=true;
    if(g_mary.result_listener_seen && listener_to_finish>80 && !MaryInRange(40)) {
        Log("fixed result: listener left finish area; skip unplayed presentation, keep settled result");
        BeginDeparture();return;
    }
    if (!g_fixed_result_waiting) {
        if (remaining>5.0F) {
            AdvanceRecordedRival(); // Keep the late finisher on the same course after settlement.
            if(CurrentRules().uses_lap_engine && !g_laps.Running() && (g_result_started_at==Clock::time_point{})) {
                StartOriginalOpponentRoute();
                g_result_started_at=Clock::now();g_fixed_last_task=Clock::now();
            }
            if (StateSeconds()>=120.0) { Log("Mary result: rival arrival timed out; no remote result speech");BeginDeparture(); return; }
            if(race_speed_policy::Enabled()) {
                invoke<Void>(0xBAAB791AA72C2821,g_opponent_rider,remaining<=20.0F?1.5F:2.5F);
                g_host_traffic_speed_owned=true;
            } else if(g_host_traffic_speed_owned) StartOriginalOpponentRoute();
            if (Clock::now()-g_fixed_last_task>std::chrono::seconds(10) &&
                ENTITY::GET_ENTITY_SPEED(g_opponent_horse)<0.5F) StartOriginalOpponentRoute();
            return;
        }
        TASK::TASK_STAND_STILL(g_opponent_horse,-1);
        TASK::TASK_STAND_STILL(g_opponent_rider,-1);
        g_fixed_result_waiting=true;
        g_offer_phase_started=Clock::now();
        Log("Mary result: rival at finish; waiting for nearby, slowed listener");
    }
    if(!FixedHasVoice(g_mary.won?E::PlayerWon:E::PlayerLost)) {
        // A deliberately silent preparation host must not wait 180s for an
        // unapproved result clip, claim it played, or offer an empty Talk option.
        Log("fixed host audio awaiting review; result settled, natural departure without dialogue");
        BeginDeparture();return;
    }
    // Finish adjudication is already locked. Only presentation waits here.
    if (!g_mary.result_attempted && g_result_phase!=ResultPhase::Interaction &&
        g_result_phase!=ResultPhase::OpponentReply && StateSeconds()>=180.0) {
            Log("Mary result: presentation ended without confirmed playback; settled winner unchanged");
            BeginDeparture();return;
    }
    if (!g_mary.result_attempted && !g_mary.result_failed) {
        // Reuse the native encounter's follow/converse task for a short,
        // local approach to a stopped finisher. Never chase a departing player.
        const bool local_listener=MaryInRange(40.0F) &&
            destination::Distance(ENTITY::GET_ENTITY_COORDS(player,true,false),g_route_finish)<=65.0F &&
            ENTITY::GET_ENTITY_SPEED(player)<=2.2F && !HUD::IS_PAUSE_MENU_ACTIVE();
        if (g_mary.result_approaching && (MaryInRange(8.0F) || !local_listener || now-g_mary.result_approach_at>=15.0)) {
            TASK::TASK_STAND_STILL(g_opponent_horse,-1);TASK::TASK_STAND_STILL(g_opponent_rider,-1);
            g_mary.result_approaching=false;g_mary.result_next_approach=now+8.0;
        } else if (!g_mary.result_approaching && !MaryInRange(12.0F) && local_listener && now>=g_mary.result_next_approach) {
            MaintainOriginalApproachTask(player,true);
            g_mary.result_approaching=true;g_mary.result_approach_at=now;
            Log("Mary result: short native follow/converse approach to nearby finisher");
        }
        if (g_mary.result_approaching || !MaryResultNearbyAndSlow()) {
            g_mary.result_ready_since=-1;
            if (now>=g_mary.result_next_log) {
                Log("Mary result waiting: distance_m="+std::to_string(std::sqrt(DistanceSquared(
                    ENTITY::GET_ENTITY_COORDS(player,true,false),ENTITY::GET_ENTITY_COORDS(g_opponent_rider,true,false))))+
                    " player_mps="+std::to_string(ENTITY::GET_ENTITY_SPEED(player))+
                    " rival_mps="+std::to_string(ENTITY::GET_ENTITY_SPEED(g_opponent_horse)));
                g_mary.result_next_log=now+10.0;
            }
            return;
        }
        if (g_mary.result_ready_since<0) g_mary.result_ready_since=now;
        if (now-g_mary.result_ready_since<0.8) return;
        TASK::TASK_LOOK_AT_ENTITY(g_opponent_rider,player,2000,2108,51,1);
        if (!g_mary.result_queued) {
            g_mary.result_queued=true;
            g_mary.Queue(g_player_won?E::PlayerWon:E::PlayerLost,now,180.0-StateSeconds());
            g_result_phase=ResultPhase::ScriptedRivalLine;
        }
    }
    TickMaryVoice();
    // Failed/expired audio is not a failed race or a played line. Keep normal
    // Talk available on this same living result instance, after bounded retries.
    // The ownership, interruption, arrival and live-horse guards above still apply.
    if ((g_mary.result_attempted || g_mary.result_failed) &&
        g_result_phase==ResultPhase::ScriptedRivalLine && MarySpeechSettled()) {
        // Speech completion, not a second stationary-listener gate, starts the
        // departure clock. A listener moving after the line cannot park the NPC
        // here forever. Optional Talk stays bound to this departing instance.
        g_mary.result_settled_since=now;
        if(g_mary.result_failed)Log("fixed result audio unavailable; enabling same-instance Talk without claiming playback");
        if(FixedHasVoice(E::Talk) || FixedHasVoice(g_mary.won?E::TalkWin:E::TalkLost))
            g_result_interaction_registered=CreateResultPrompts();
        g_mary_talk_pressed=true; // Require release before a new press.
        g_result_phase=ResultPhase::Interaction;
        g_offer_phase_started=Clock::now();
    }
    if(g_mary.result_settled_since>=0 && now-g_mary.result_settled_since>=1.5)
        StartFixedResultDepartureMotion();
    if(g_mary.result_departing && !g_mary.pending && MarySpeechSettled() &&
        destination::Distance(listener,g_route_start)<=65 && !MaryInRange(30) &&
        destination::Distance(ENTITY::GET_ENTITY_COORDS(g_opponent_rider,true,false),g_route_start)>50) {
        // In a circuit the finish is also the pickup. Once the old rider has
        // actually departed, release the task slot without waiting for Talk.
        BeginDeparture();return;
    }
    if (g_result_phase==ResultPhase::Interaction) {
        const bool focused=MaryFriendlyFocus(PLAYER::PLAYER_PED_ID());
        if (g_accept_prompt && HUD::_UI_PROMPT_IS_VALID(g_accept_prompt)) {
            HUD::_UI_PROMPT_SET_ENABLED(g_accept_prompt,focused && !g_mary.talk_used);
            HUD::_UI_PROMPT_SET_TEXT(g_accept_prompt,i18n::Literal(i18n::Text("Talk","交谈")));
        }
        PED::SET_PED_RESET_FLAG(g_opponent_rider,184,true);
        const bool pressed=g_result_interaction_registered && HUD::_UI_PROMPT_IS_PRESSED(g_accept_prompt);
        const bool selected=pressed && !g_mary_talk_pressed && focused && !g_mary.talk_used;
        g_mary_talk_pressed=pressed;
        if (selected) {
            const auto reply=FixedTalkEvent();
            if (!reply) { BeginDeparture();return; }
            g_mary.talk_used=true;
            DestroyOfferPrompts();
            g_player_reply={true,g_mary.won,FixedListenerMask(),MaryNow()+10.0,g_mary.id};
            // Charles has outcome-specific replies but no general Talk. Mary
            // and Sadie retain their existing choices; never invert the result.
            g_mary.Queue(*reply,MaryNow(),10.0);
            g_result_phase=ResultPhase::OpponentReply;
            g_offer_phase_started=Clock::now();
        } else if (Clock::now()-g_offer_phase_started>=std::chrono::seconds(30)) BeginDeparture();
    } else if (g_result_phase==ResultPhase::OpponentReply &&
        ((MarySpeechSettled() && Clock::now()-g_offer_phase_started>=std::chrono::seconds(2)) ||
         Clock::now()-g_offer_phase_started>=std::chrono::seconds(15))) BeginDeparture();
}
