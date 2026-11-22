#pragma once
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace horse_races::bookmaking {
// This confirms LML selected our files, NOT that the engine rendered pixels.
// Never disable the visible sprite path merely because the INI requests native.
class NativeResourceProbe {
    std::string pending_;
    std::uintmax_t offset_=0;
    unsigned mask_=0;
public:
    unsigned Mask() const{return mask_;}
    void Reset(){pending_.clear();offset_=0;mask_=0;}
    void Consume(std::string_view bytes){
        for(char c:bytes){
            if(c!='\n'){
                if(pending_.size()<2048)pending_+=c;
                continue;
            }
            std::transform(pending_.begin(),pending_.end(),pending_.begin(),
                [](unsigned char v){return v>='A'&&v<='Z'?char(v+32):char(v);});
            for(const std::string_view prefix:{
                "global full replace: addondlc:/thejockeyclub text/native_ui/",
                "replaced file from archive addondlc:/thejockeyclub text/native_ui/"}){
                const auto begin=pending_.find(prefix);
                if(begin==std::string::npos)continue;
                const auto name=std::string_view(pending_).substr(begin+prefix.size());
                if(name=="scoretimer_preloader_flow.ymt" || name=="scoretimer_preloader_flow.ymt\r")mask_|=1;
                if(name=="meter_with_icon.ymt" || name=="meter_with_icon.ymt\r")mask_|=2;
            }
            pending_.clear();
        }
    }
    unsigned Poll(const std::filesystem::path& path,bool current_session){
        if(!current_session){Reset();return 0;}
        std::error_code error;
        const auto size=std::filesystem::file_size(path,error);
        if(error)return 0;
        if(size<offset_)Reset();
        if(mask_==3 || size==offset_)return mask_;
        std::ifstream file(path,std::ios::binary);
        if(!file)return 0;
        file.seekg(static_cast<std::streamoff>(offset_));
        std::array<char,65536> bytes{}; // bounded work, called at most once/second
        file.read(bytes.data(),bytes.size());
        const auto count=file.gcount();
        if(count>0){Consume({bytes.data(),static_cast<std::size_t>(count)});offset_+=count;}
        return mask_;
    }
};
} // namespace horse_races::bookmaking
