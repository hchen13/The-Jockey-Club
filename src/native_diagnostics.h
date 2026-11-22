#pragma once
#include <main.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>

namespace horse_diagnostics {
// Only our script calls these wrappers. Never read ScriptHook's shared last-call
// globals: another ASI can overwrite them after our script has failed/yielded.
struct NativeTrace {
    volatile std::uint64_t hash=0, arguments[8]{};
    volatile unsigned count=0;
    volatile bool executing=false;
    const char* volatile stage="script entry";
};
inline NativeTrace trace;
inline void MarkFaultStage(const char* literal) noexcept {trace.stage=literal;}
// Where our share of a frame goes (docs/refactor/00-plan.md 2b, step 0). Stage() charges the time since
// the previous boundary, and the natives called meanwhile, to the stage that ends. Scope charges a
// function's own time to its name and hands the rest back to whoever called it, so the figures are
// exclusive and add up to the frame. The yield is the game's frame and is never counted as ours.
// Names are literals: pointer identity is the key. Everything runs on the script's own thread.
inline constexpr const char* kYield="script yield";
struct StageCost {const char* name=nullptr;double total_ms=0,frame_ms=0,worst_frame_ms=0;std::uint64_t natives=0;};
struct CostBook {
    using Clock=std::chrono::steady_clock;
    std::array<StageCost,64> stages{};
    const char* open=nullptr;                     // The stage being timed; null across the yield.
    Clock::time_point since{},window_began{},report_due{};
    std::uint64_t natives=0;                      // Called since `since`.
    double frame_ms=0,frames_ms=0,worst_frame_ms=0;
    std::uint64_t frame_natives=0,frames_natives=0,frames=0,unbooked=0;
    bool enabled=true;
};
inline CostBook costs;
inline void Stage(const char* literal) {
    trace.stage=literal;
    auto& c=costs;
    if(!c.enabled)return;
    const auto now=CostBook::Clock::now();
    if(c.open) {
        const double ms=std::chrono::duration<double,std::milli>(now-c.since).count();
        StageCost* slot=nullptr;
        for(auto& s:c.stages)if(s.name==c.open || !s.name){slot=&s;break;}
        if(slot){slot->name=c.open;slot->total_ms+=ms;slot->frame_ms+=ms;slot->natives+=c.natives;}
        else ++c.unbooked;
        c.frame_ms+=ms;c.frame_natives+=c.natives;
    }
    if(literal==kYield) {
        for(auto& s:c.stages){if(!s.name)break;s.worst_frame_ms=std::max(s.worst_frame_ms,s.frame_ms);s.frame_ms=0;}
        c.frames_ms+=c.frame_ms;c.worst_frame_ms=std::max(c.worst_frame_ms,c.frame_ms);
        c.frames_natives+=c.frame_natives;++c.frames;c.frame_ms=0;c.frame_natives=0;
        c.open=nullptr;
    } else c.open=literal;
    c.since=now;c.natives=0;
}
struct Scope {
    const char* outer;
    explicit Scope(const char* literal):outer(costs.open){Stage(literal);}
    ~Scope(){if(outer)Stage(outer);else costs.open=nullptr;}   // Outside a frame (start-up) nothing is charged.
    Scope(const Scope&)=delete;
    Scope& operator=(const Scope&)=delete;
};
// Called straight after Stage(kYield): one line per window, the ten dearest stages, then a clean book.
// A stage reads "mean per frame / worst frame, natives per frame".
template<class Sink> void ReportCosts(Sink&& log,double every_seconds=30) {
    auto& c=costs;
    if(!c.enabled)return;
    const auto now=CostBook::Clock::now();
    const auto window=std::chrono::duration_cast<CostBook::Clock::duration>(std::chrono::duration<double>(every_seconds));
    if(c.report_due==CostBook::Clock::time_point{}){c.window_began=now;c.report_due=now+window;return;}
    if(now<c.report_due)return;
    if(c.frames) {
        const double seconds=std::chrono::duration<double>(now-c.window_began).count();
        const double frames=static_cast<double>(c.frames);
        std::array<const StageCost*,64> order{};std::size_t used=0;
        for(const auto& s:c.stages)if(s.name)order[used++]=&s;
        std::sort(order.begin(),order.begin()+static_cast<std::ptrdiff_t>(used),
            [](const StageCost* a,const StageCost* b){return a->total_ms>b->total_ms;});
        char text[192];
        std::snprintf(text,sizeof text,"frame cost over %.0f s: %llu frames (%.0f fps); ours mean %.3f ms, worst frame %.2f ms, %.0f natives/frame",
            seconds,static_cast<unsigned long long>(c.frames),seconds>0?frames/seconds:0.0,c.frames_ms/frames,c.worst_frame_ms,
            static_cast<double>(c.frames_natives)/frames);
        std::string line=text;
        for(std::size_t i=0;i<used && i<10;++i) {
            std::snprintf(text,sizeof text," | %s %.3f/%.2f ms %.0f n",order[i]->name,order[i]->total_ms/frames,
                order[i]->worst_frame_ms,static_cast<double>(order[i]->natives)/frames);
            line+=text;
        }
        if(c.unbooked)line+=" | "+std::to_string(c.unbooked)+" entries had no slot";
        log(line);
    }
    c.stages={};c.frames_ms=c.worst_frame_ms=0;c.frames_natives=c.frames=c.unbooked=0;
    c.window_began=now;c.report_due=now+window;
}
inline void Init(UINT64 hash) {
    trace.hash=hash;trace.count=0;trace.executing=false;
    for(auto& argument:trace.arguments)argument=0;
    ::nativeInit(hash);
}
inline void Push(UINT64 value) {
    const auto count=trace.count;
    if(count<8)trace.arguments[count]=value;
    trace.count=count+1;
    ::nativePush64(value);
}
inline PUINT64 Call() {
    trace.executing=true;
    ++costs.natives;
    const auto result=::nativeCall();
    trace.executing=false;
    return result;
}
} // namespace horse_diagnostics
