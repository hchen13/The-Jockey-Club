#pragma once
#include "fixed_horse_pool.h"
#include "horse_identity.h"
#include <cctype>
#include <map>
#include <set>
#include <string>
#include <string_view>

namespace horse_catalog {
inline std::string Upper(std::string_view value) {
    std::string result(value);
    for(char& c:result)c=static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return result;
}
inline bool ModelName(std::string_view value) {
    return value.starts_with("A_C_HORSE_") && value.size()>10 && value.size()<=100 &&
        std::all_of(value.begin(),value.end(),[](unsigned char c){return std::isalnum(c) || c=='_';});
}
inline std::string_view Tag(std::string_view block,std::string_view name) {
    const std::string open="<"+std::string(name)+">",close="</"+std::string(name)+">";
    auto start=block.find(open);if(start==std::string_view::npos)return {};
    start+=open.size();const auto end=block.find(close,start);
    return end==std::string_view::npos?std::string_view{}:block.substr(start,end-start);
}
// This is a bounded reader of horse compendium records, not an XML executor.
// No entity expansion, external DTDs, arbitrary files or translated-name parsing.
inline std::map<std::string,std::string> CompendiumBreeds(std::string_view xml) {
    std::map<std::string,std::string> result;
    constexpr std::string_view marker="<Item type=\"CCompendiumItemHorse\">";
    std::size_t offset=0;
    while((offset=xml.find(marker,offset))!=std::string_view::npos) {
        const auto next=xml.find(marker,offset+marker.size());
        auto block=xml.substr(offset,next==std::string_view::npos?xml.size()-offset:next-offset);
        const auto end=block.find("</UncommonCoats>");
        if(end==std::string_view::npos){offset+=marker.size();continue;}
        block=block.substr(0,end);
        std::string breed=Upper(Tag(block,"AttributeSet"));
        if(!breed.starts_with("HORSE_") || !breed.ends_with("_COMMON")){offset+=marker.size();continue;}
        breed=breed.substr(6,breed.size()-13);
        std::size_t key=0;
        while((key=block.find("key=\"",key))!=std::string_view::npos) {
            key+=5;const auto stop=block.find('"',key);if(stop==std::string_view::npos)break;
            auto model=Upper(block.substr(key,stop-key));
            if(ModelName(model))result.insert_or_assign(model,breed);
            key=stop+1;
        }
        offset+=marker.size();
    }
    return result;
}
struct Definition {
    std::string model,breed,coat,breed_label,coat_label,source="stock";
    std::uint32_t model_hash=0;
    int priority=0;
    story_horses::Labels labels{};
    void BindLabels(){labels={model.c_str(),breed_label.c_str(),coat_label.c_str()};}
};
struct Catalog {
    // map nodes keep all Choice::name and native-card label pointers stable.
    // Never clear while a roster or horse-information card still owns pointers.
    std::map<std::string,Definition,std::less<>> definitions;
    void Add(std::string model,std::string breed={},std::string source="stock") {
        if(!ModelName(model))return;
        auto [it,inserted]=definitions.try_emplace(model);
        auto& d=it->second;
        if(inserted){d.model=model;d.breed=horse_identity::StockBreed(model);d.coat=horse_identity::DefaultCoat(model);}
        if(!breed.empty())d.breed=horse_identity::CanonicalBreed(breed);
        if(source!="stock")d.source=std::move(source);
    }
    void Seed() {
        for(const auto* name:fixed_horses::kModels)Add(name);
        for(const auto* name:fixed_horses::kAuditOnlyModels)Add(name);
    }
    void ApplyCompendium(std::string_view xml) {
        for(const auto& [model,breed]:CompendiumBreeds(xml))Add(model,breed,"effective-compendium");
        // SUHM repurposes two SP identities. Enable each adapter ONLY when the
        // effective compendium actually assigns that identity to the new breed.
        // Never classify a non-PC Red Chestnut Arabian by its misleading name.
        const auto adapt=[&](const char* model,const char* breed,const char* ordinary){
            const auto it=definitions.find(model);
            if(it==definitions.end() || it->second.source!="effective-compendium" || it->second.breed!=breed)return;
            it->second.coat=horse_identity::DefaultCoat(ordinary);it->second.priority=10;
        };
        adapt("A_C_HORSE_MP_MANGY_BACKUP","MUSTANG","A_C_HORSE_MUSTANG_BLACKOVERO");
        adapt("A_C_HORSE_ARABIAN_REDCHESTNUT","TURKOMAN","A_C_HORSE_TURKOMAN_CHESTNUT");
    }
    template<class HasLabel> bool ResolveLabels(Definition& d,HasLabel has) {
        if(!d.breed_label.empty() && !d.coat_label.empty() &&
            has(d.breed_label.c_str()) && has(d.coat_label.c_str()))return true;
        const auto choose=[&](std::initializer_list<std::string> keys){
            for(const auto& key:keys)if(!key.empty() && has(key.c_str()))return key;
            return std::string{};
        };
        const auto* known=story_horses::FindForRace(d.model);
        std::string suffix=d.model.substr(10);
        if(d.model=="A_C_HORSE_MP_MANGY_BACKUP")suffix="MANGY_BACKUP";
        d.breed_label=choose({known?known->breed:"","BREED_"+suffix+"_BREED","BREED_"+suffix});
        // Display keys are not the persistent coat identity. Rockstar shortens
        // these labels (also used by SUHM/Horse Names and Stats); keep d.coat
        // unchanged so correcting text cannot rename an existing horse record.
        constexpr std::pair<std::string_view,std::string_view> display_aliases[]{
            {"COAT_CHESTNUTTOVERO","COAT_CHTOVERO"},
            {"COAT_MEALYDAPPLEBAY","COAT_MEALYDAPBAY"},
            {"COAT_PALOMINOBLAGDON","COAT_PALBLAGDON"},
            {"COAT_PIEBALD","COAT_PIE"},
            {"COAT_SPLASHEDBAY","COAT_SPLASHBAY"},
            {"COAT_SPLASHEDPIEBALD","COAT_SPLASHPIE"},
            {"COAT_CREMELLO","COAT_CREM"},
            {"COAT_DAPPLEDBUCKSKIN","COAT_DAPBUCKSKIN"},
            {"COAT_PIEBALDROAN","COAT_PIEROAN"},
            {"COAT_DAPPLEROSEGREY","COAT_DAPROSEGREY"},
            // short_update.c func_889: these special SP models are not in
            // kApproved, but their display coats are ordinary native labels.
            {"A_C_HORSE_EAGLEFLIES","COAT_SPLASHWHITE"},
            {"COAT_MANGE_01","COAT_BLANKET"},
            {"COAT_MANGE_02","COAT_BLUER"},
            {"COAT_MANGE_03","COAT_BLACKRAB"}};
        std::string alias;
        for(const auto& [identity,label]:display_aliases)if(d.coat==identity){alias=label;break;}
        d.coat_label=choose({known?known->coat:"",d.coat,alias});
        if(d.model=="A_C_HORSE_MP_MANGY_BACKUP" && d.priority>0)d.coat_label=choose({"COAT_NONE",d.coat});
        // A breed label is never a substitute for an unavailable coat label.
        // Otherwise the card silently reports e.g. Coat: Mustang.
        d.BindLabels();return !d.breed.empty() && !d.breed_label.empty() && !d.coat_label.empty();
    }
    template<class HashName,class Available,class ReadRanks,class HasLabel>
    std::vector<fixed_horses::Choice> Read(HashName hash,Available available,ReadRanks ranks,HasLabel has) {
        std::vector<fixed_horses::Choice> result;
        for(auto& [name,d]:definitions) {
            // Publish effective identity independently of race eligibility: a
            // player riding their own valid model is not governed by NPC pools.
            horse_identity::effective[name]={d.breed,d.coat};
            const auto model=hash(name.c_str());d.model_hash=model;
            if(!model || !available(model) || !ResolveLabels(d,has))continue;
            const auto raw=ranks(model);if(!raw.Valid())continue;
            result.push_back({d.model.c_str(),model,raw});
        }
        // Prefer a provider's SP alias over an equivalent raw online model. Do
        // not remove named mounts here: host ownership is a later policy layer.
        std::stable_sort(result.begin(),result.end(),[&](const auto& a,const auto& b){
            return definitions.at(a.name).priority>definitions.at(b.name).priority;
        });
        return result;
    }
    const story_horses::Labels* Labels(std::string_view model) const {
        const auto it=definitions.find(model);
        if(it==definitions.end() || it->second.breed_label.empty() || it->second.coat_label.empty())
            return story_horses::FindForRace(model);
        return &it->second.labels;
    }
    const story_horses::Labels* Labels(std::uint32_t model) const {
        if(!model)return nullptr;
        for(const auto& [name,d]:definitions)
            if(d.model_hash==model && !d.breed_label.empty() && !d.coat_label.empty())return &d.labels;
        return nullptr;
    }
};
inline Catalog current;
}
