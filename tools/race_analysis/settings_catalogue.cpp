// Offline settings contract. All reads/defaults and domain writes use native helpers.
#include "../../src/race_tracking_margin_config.h"
#include "../../src/race_opponent_difficulty.h"
#include "../../src/race_corridor_override.h"
#include "../../src/race_bgm_volume.h"
#include "../../src/race_progression.h"
#include "../../src/race_host_horses.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <functional>
#include <stdexcept>
#include <iomanip>
using namespace horse_races;
namespace {
void Need(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
std::string Quote(std::string_view value){std::string out="\"";for(unsigned char c:value){if(c=='"'||c=='\\'){out+='\\';out+=c;}else if(c=='\n')out+="\\n";else if(c=='\r')out+="\\r";else if(c=='\t')out+="\\t";else if(c<32)out+=' ';else out+=c;}return out+'"';}
struct Scalar {
    enum Kind {Number,Boolean,Null} kind=Null;double number{};bool boolean{};
    Scalar()=default;Scalar(double n):kind(Number),number(n){} Scalar(int n):Scalar(static_cast<double>(n)){} Scalar(bool b):kind(Boolean),boolean(b){}
    std::string Json()const {if(kind==Null)return "null";if(kind==Boolean)return boolean?"true":"false";std::ostringstream out;out<<std::setprecision(12)<<number;return out.str();}
};
Scalar ParseScalar(std::string_view text){
    if(text=="null")return {};if(text=="true")return Scalar(true);if(text=="false")return Scalar(false);
    // JSON number grammar, including full-consumption and finite range checks.
    std::size_t i=0;if(i<text.size()&&text[i]=='-')++i;
    Need(i<text.size(),"expected JSON scalar");
    if(text[i]=='0')++i;else {Need(text[i]>='1'&&text[i]<='9',"expected JSON number");while(i<text.size()&&text[i]>='0'&&text[i]<='9')++i;}
    if(i<text.size()&&text[i]=='.'){++i;const auto first=i;while(i<text.size()&&text[i]>='0'&&text[i]<='9')++i;Need(i>first,"invalid JSON fraction");}
    if(i<text.size()&&(text[i]=='e'||text[i]=='E')){++i;if(i<text.size()&&(text[i]=='+'||text[i]=='-'))++i;const auto first=i;while(i<text.size()&&text[i]>='0'&&text[i]<='9')++i;Need(i>first,"invalid JSON exponent");}
    Need(i==text.size(),"invalid JSON number suffix");double number{};const auto result=std::from_chars(text.data(),text.data()+text.size(),number);
    Need(result.ec==std::errc{}&&result.ptr==text.data()+text.size()&&std::isfinite(number),"non-finite or invalid number");return Scalar(number);
}
struct Field {std::string key,label,type,help;Scalar value,default_value;double minimum{},maximum{},step=1;bool nullable=false;std::vector<int> options;};
struct Group {std::string id,label;std::vector<Field> fields;std::string default_ability;};
using Catalogue=std::vector<Group>;
Field Number(std::string key,std::string label,double value,double fallback,double lo,double hi,double step=1,std::string help="下一场比赛生效") {return {key,label,"number",help,Scalar(value),Scalar(fallback),lo,hi,step};}
Field Boolean(std::string key,std::string label,bool value,bool fallback,std::string help="下一场比赛生效") {return {key,label,"boolean",help,Scalar(value),Scalar(fallback)};}
bool Station(std::string_view key){return std::find(progression::kKeys.begin(),progression::kKeys.end(),key)!=progression::kKeys.end();}
// Match plugin.cpp LoadFloat("General", "EnabledOnStartup", "1",0,1,1)
// and its >= .5 threshold, including the native std::stof prefix semantics.
bool StartupSpeedEnabled(const std::string& ini){
    char text[2048]{};GetPrivateProfileStringA("General","EnabledOnStartup","1",text,sizeof(text),ini.c_str());
    try {const float value=std::stof(text);if(std::isfinite(value)&&value>=0&&value<=1)return value>=.5F;}catch(...){}
    return true;
}
std::string DefaultAbility(const opponent_difficulty::Rule& defaults,const hosts::Profile& host,float shared_cornering){
    // F11's disabled acceleration range is an editor seed, not the natural
    // horse rule. Export that rule directly from the runtime host profile.
    std::ostringstream out;
    out<<"{\"speed\":{\"minimum\":"<<defaults.speed.minimum<<",\"maximum\":"<<defaults.speed.maximum
       <<"},\"acceleration\":{\"mode\":"<<Quote(host.accel_minimum>0?"absolute":"natural");
    if(host.accel_minimum>0)out<<",\"minimum\":"<<defaults.acceleration.minimum<<",\"maximum\":"<<defaults.acceleration.maximum;
    else out<<",\"bonus_minimum\":"<<host.natural_acceleration_bonus_min<<",\"bonus_maximum\":"<<host.natural_acceleration_bonus_max;
    out<<"},\"cornering\":{\"mode\":\"shared\",\"value\":"<<shared_cornering
       <<"},\"level_cap\":"<<horse_attributes::DisplayLevel(9)<<'}';
    return out.str();
}
Catalogue Describe(const std::string& ini,const std::string& station,double half){
    Catalogue result;
    result.push_back({"general","比赛设置",{
        Number("margin","赛道安全余量",std::round(tracking_margin::Read(ini)*100.0)/100.0,tracking_margin::kDefaultMetres,0,5,.1),
        Number("line_percent","NPC 走线强度",std::clamp(static_cast<int>(GetPrivateProfileIntA("RaceAI","RacingLinePercent",100,ini.c_str())),0,100),100,0,100,5),
        Number("shared_cornering","默认过弯能力",opponent_difficulty::SharedCornering(ini),14,5,30),
        Number("own_horse_percent","帮派自有马概率",opponent_difficulty::OwnHorsePercent(ini,host_horses::kOwnHorseChancePercent),host_horses::kOwnHorseChancePercent,20,100)
    }});
    if(station!="-"){
        Need(Station(station),"unknown canonical station");Need(std::isfinite(half)&&half>=5&&half<=30,"invalid route half-width");
        auto field=Number("corridor","赛道半宽",half,half,5,20,.5);field.nullable=true;
        const auto width=corridor_override::Read(ini,station.c_str());field.value=width?Scalar(*width):Scalar{};
        field.help="仅此赛道；恢复默认会删除覆盖值。正在比赛时，下场比赛生效。";
        result.push_back({"course","赛道边界",{field}});
    }
    for(std::size_t i=0;i<opponent_difficulty::kCount;++i){
        const auto opponent=opponent_difficulty::At(i);const auto rule=opponent_difficulty::Read(ini,i),defaults=opponent_difficulty::Defaults(i);const auto prefix=std::string("npc.")+opponent.key;
        Group group{prefix,opponent.chinese,{
            Boolean(prefix+".speed_custom","自定义速度",rule.speed.enabled,defaults.speed.enabled),
            Number(prefix+".speed_min","最低速度",rule.speed.minimum,defaults.speed.minimum,1,10),Number(prefix+".speed_max","最高速度",rule.speed.maximum,defaults.speed.maximum,1,10),
            Boolean(prefix+".acceleration_custom","自定义加速度",rule.acceleration.enabled,defaults.acceleration.enabled),
            Number(prefix+".acceleration_min","最低加速度",rule.acceleration.minimum,defaults.acceleration.minimum,1,10),Number(prefix+".acceleration_max","最高加速度",rule.acceleration.maximum,defaults.acceleration.maximum,1,10),
            Number(prefix+".cornering","过弯能力",rule.cornering,defaults.cornering,0,30)
        }};
        if(opponent.host)group.default_ability=DefaultAbility(defaults,*opponent.host,opponent_difficulty::SharedCornering(ini));
        auto& corner=group.fields.back();corner.type="select";corner.options.push_back(0);for(int n=5;n<=30;++n)corner.options.push_back(n);corner.help="默认跟随共享过弯能力；自定义范围 5–30。";
        result.push_back(std::move(group));
    }
    Group music{"music","比赛音乐音量",{}};
    for(std::size_t i=0;i<bgm::volume::kCount;++i){const auto group=static_cast<bgm::volume::Group>(i);const auto& spec=bgm::volume::specs[i];music.fields.push_back(Number(std::string("bgm.")+spec.key,spec.chinese,bgm::volume::ReadPercent(ini,group),spec.default_percent,0,100,1,"该类曲目下次播放时生效"));}
    result.push_back(std::move(music));
    result.push_back({"startup","启动设置",{
        Boolean("speed_enabled","启动时启用马速覆盖",StartupSpeedEnabled(ini),true,"下次启动游戏生效；不切换当前游戏中的开关"),
        Boolean("show_speed","启动时显示实时速度",GetPrivateProfileIntA("UI","ShowSpeed",0,ini.c_str())!=0,false,"下次启动游戏生效；不切换当前游戏中的开关")
    }});
    return result;
}
std::string Json(const Catalogue& groups){
    std::ostringstream out;out<<"{\"groups\":[";bool firstGroup=true;
    for(const auto& group:groups){if(!firstGroup)out<<',';firstGroup=false;out<<"{\"id\":"<<Quote(group.id)<<",\"label\":"<<Quote(group.label);
        if(!group.default_ability.empty())out<<",\"default_ability\":"<<group.default_ability;
        out<<",\"fields\":[";bool firstField=true;
        for(const auto& f:group.fields){if(!firstField)out<<',';firstField=false;out<<"{\"key\":"<<Quote(f.key)<<",\"label\":"<<Quote(f.label)<<",\"type\":"<<Quote(f.type)<<",\"value\":"<<f.value.Json()<<",\"default_value\":"<<f.default_value.Json()<<",\"help\":"<<Quote(f.help);
            if(f.type!="boolean")out<<",\"min\":"<<f.minimum<<",\"max\":"<<f.maximum<<",\"step\":"<<f.step;
            if(f.nullable)out<<",\"nullable\":true";
            if(!f.options.empty()){out<<",\"options\":[";for(std::size_t i=0;i<f.options.size();++i){if(i)out<<',';const auto v=f.options[i];out<<"{\"value\":"<<v<<",\"label\":"<<Quote(v==0?"默认":std::to_string(v))<<'}';}out<<']';}
            out<<'}';
        }out<<"]}";
    }out<<"],\"timing\":\"比赛设置下场比赛生效；音量下次播放生效；启动设置下次启动游戏生效。\"}";return out.str();
}
void Validate(const Field& field,const Scalar& value){
    if(value.kind==Scalar::Null){Need(field.nullable,"field cannot be null: "+field.key);return;}
    if(field.type=="boolean"){Need(value.kind==Scalar::Boolean,"expected boolean: "+field.key);return;}
    Need(value.kind==Scalar::Number&&value.number>=field.minimum&&value.number<=field.maximum,"number outside bounds: "+field.key);
    if(!field.options.empty())Need(std::find(field.options.begin(),field.options.end(),value.number)!=field.options.end(),"unknown option: "+field.key);
    const double ticks=(value.number-field.minimum)/field.step;
    Need(std::abs(ticks-std::round(ticks))<1e-6,"value does not match step: "+field.key);
}
std::string Apply(const std::string& ini,const std::string& station,double half,const std::filesystem::path& patch){
    auto catalogue=Describe(ini,station,half);std::map<std::string,Field*> fields;for(auto& group:catalogue)for(auto& field:group.fields)fields.emplace(field.key,&field);
    std::ifstream input(patch,std::ios::binary);Need(bool(input),"cannot read patch");std::string line;std::set<std::string> changed;
    while(std::getline(input,line)){if(!line.empty()&&line.back()=='\r')line.pop_back();if(line.empty())continue;Need(line.size()<=1024,"oversized patch line");const auto tab=line.find('\t');Need(tab!=std::string::npos&&line.find('\t',tab+1)==std::string::npos,"expected key TAB JSON scalar");const auto key=line.substr(0,tab);Need(fields.contains(key),"unknown field: "+key);Need(changed.insert(key).second,"duplicate patch key: "+key);const auto value=ParseScalar(std::string_view(line).substr(tab+1));Validate(*fields.at(key),value);fields.at(key)->value=value;}
    Need(input.eof(),"patch read failed");Need(changed.size()<=256,"too many changes");
    std::map<std::size_t,opponent_difficulty::Rule> rules;
    for(std::size_t i=0;i<opponent_difficulty::kCount;++i){const auto prefix=std::string("npc.")+opponent_difficulty::At(i).key;bool touched=false;for(const auto& key:changed)if(key.starts_with(prefix+"."))touched=true;if(!touched)continue;
        const auto get=[&](const char* suffix)->const Scalar&{return fields.at(prefix+suffix)->value;};
        opponent_difficulty::Rule rule{{get(".speed_custom").boolean,static_cast<int>(get(".speed_min").number),static_cast<int>(get(".speed_max").number)},
            {get(".acceleration_custom").boolean,static_cast<int>(get(".acceleration_min").number),static_cast<int>(get(".acceleration_max").number)},static_cast<int>(get(".cornering").number)};
        Need(rule.Valid()&&opponent_difficulty::Parse(opponent_difficulty::Encode(rule))==rule,"invalid combined NPC range: "+prefix);rules.emplace(i,rule);
    }
    if(changed.empty())return Json(catalogue);
    // Root supplies its own temporary copy. Stage again so even a domain write
    // failure cannot partially alter that input; only a complete result replaces it.
    const std::filesystem::path stage=ini+".catalogue-"+std::to_string(GetCurrentProcessId())+".ini";
    Need(!std::filesystem::exists(stage),"staging collision");
    struct Cleanup{std::filesystem::path path;~Cleanup(){std::error_code error;std::filesystem::remove(path,error);}} cleanup{stage};
    std::filesystem::copy_file(ini,stage);const auto target=stage.string();
    for(const auto& [index,rule]:rules)Need(opponent_difficulty::Save(target,index,rule),"NPC write failed");
    for(const auto& key:changed){if(key.starts_with("npc."))continue;const auto& value=fields.at(key)->value;const auto n=static_cast<int>(std::lround(value.number));bool saved=false;
        if(key=="margin")saved=tracking_margin::Save(target,static_cast<float>(value.number));
        else if(key=="line_percent")saved=config::SetRaw(target.c_str(),"RaceAI","RacingLinePercent",n);
        else if(key=="shared_cornering")saved=config::SetRaw(target.c_str(),"RaceAI","CorneringEnvelopeMps2",n);
        else if(key=="own_horse_percent")saved=config::SetRaw(target.c_str(),"RacePrototype","GangOwnHorseChancePercent",n);
        else if(key=="speed_enabled")saved=config::SetRaw(target.c_str(),"General","EnabledOnStartup",value.boolean?1:0);
        else if(key=="show_speed")saved=config::SetRaw(target.c_str(),"UI","ShowSpeed",value.boolean?1:0);
        else if(key=="corridor")saved=value.kind==Scalar::Null?corridor_override::Clear(target,station.c_str()):corridor_override::Save(target,station.c_str(),static_cast<float>(value.number));
        else if(key.starts_with("bgm."))for(std::size_t i=0;i<bgm::volume::kCount;++i)if(key==std::string("bgm.")+bgm::volume::specs[i].key)saved=bgm::volume::Save(target,static_cast<bgm::volume::Group>(i),n);
        Need(saved,"setting write failed: "+key);
    }
    const auto result=Json(Describe(target,station,half));WritePrivateProfileStringA(nullptr,nullptr,nullptr,target.c_str());
    const HANDLE file=CreateFileW(stage.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    Need(file!=INVALID_HANDLE_VALUE,"cannot open staged INI for flush");const bool flushed=FlushFileBuffers(file)!=0;CloseHandle(file);Need(flushed,"INI flush failed");
    Need(MoveFileExW(stage.c_str(),std::filesystem::path(ini).c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0,"temporary INI replacement failed");return result;
}
}
int main(int argc,char** argv){try{Need(argc==5||argc==6,"usage: race_settings_catalogue describe|apply ini station|- route_half_width [patch.tsv]");const std::string command=argv[1],ini=std::filesystem::absolute(argv[2]).string(),station=argv[3];const auto half=ParseScalar(argv[4]);Need(half.kind==Scalar::Number,"route width must be number");Need((command=="describe"&&argc==5)||(command=="apply"&&argc==6),"invalid command");std::cout<<(command=="describe"?Json(Describe(ini,station,half.number)):Apply(ini,station,half.number,argv[5]))<<'\n';return 0;}catch(const std::exception& error){std::cout<<"{\"error\":"<<Quote(error.what())<<"}\n";return 2;}}
