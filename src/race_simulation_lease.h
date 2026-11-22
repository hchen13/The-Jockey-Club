#pragma once
#include "horse_motion_native.h"

namespace horse_motion {
// Keep ONLY active participant riders/horses at the same update/detail policy.
// This is not a global population/LOD edit. All persistent values have getters,
// exact identity-bound ownership and compare-before-restore cleanup.
struct RaceSimulationLease {
    struct Entry {
        Identity identity;
        float original_lod=0,written_lod=0;
        bool original_update=false,owns_lod=false,owns_update=false;
        void Release() {
            if(identity.Valid() && Identify(identity.horse)==identity) {
                if(owns_lod && std::abs(PED::_GET_PED_LOD_MULTIPLIER(identity.horse)-written_lod)<.001F)
                    PED::SET_PED_LOD_MULTIPLIER(identity.horse,original_lod);
                if(owns_update && PED::GET_PED_CONFIG_FLAG(identity.horse,133,true))
                    PED::SET_PED_CONFIG_FLAG(identity.horse,133,original_update);
            }
            *this={};
        }
    };
    std::vector<Entry> entries;
    std::uint64_t token=0;
    bool active=false;
    const char* refusal=nullptr; // Runtime audit: a false lease must not be silent.
    void Release(){for(auto& e:entries)e.Release();token=0;active=false;refusal=nullptr;}
    bool Reject(const char* why){Release();refusal=why;return false;}
    bool ApplyGroup(bool race_active,std::uint64_t race_token,const std::vector<Ped>& actors) {
        // The same field maximum as the town-speed exemption. A host plus
        // seven extra opponents and the player are nine rider/horse pairs;
        // the old twelve-entity cap silently disabled *every* simulation
        // lease in the larger races, exactly when separated riders need it.
        if(!race_active || !race_token)return Reject("not-racing");
        if(actors.size()<2 || actors.size()>kMaxExemptEntities || actors.size()%2)return Reject("roster-size");
        for(std::size_t i=0;i<actors.size();i+=2) {
            if(!actors[i] || !actors[i+1] || !PED::IS_PED_ON_MOUNT(actors[i]) ||
                PED::GET_MOUNT(actors[i])!=actors[i+1])return Reject("rider-unmounted");
        }
        for(Ped p:actors)if(std::count(actors.begin(),actors.end(),p)!=1)return Reject("duplicate-actor");
        std::vector<Identity> identities(actors.size());
        bool same=active && token==race_token && entries.size()==actors.size();
        for(std::size_t i=0;i<actors.size();++i) {
            identities[i]=Identify(actors[i]);
            if(!identities[i].Valid() || ENTITY::IS_ENTITY_DEAD(actors[i]))return Reject("actor-missing-or-dead");
            same=same && entries[i].identity==identities[i];
        }
        if(same)return true;
        Release();
        entries.resize(actors.size());
        // Read all baselines first; bad/unsupported LOD readback changes nobody.
        for(std::size_t i=0;i<actors.size();++i) {
            auto& e=entries[i];e.identity=identities[i];
            e.original_lod=PED::_GET_PED_LOD_MULTIPLIER(actors[i]);
            e.original_update=PED::GET_PED_CONFIG_FLAG(actors[i],133,true);
            if(!std::isfinite(e.original_lod) || e.original_lod<=0 || e.original_lod>100)return Reject("lod-readback-invalid");
        }
        for(auto& e:entries) {
            const Ped ped=e.identity.horse;
            if(e.original_lod<6) {
                PED::SET_PED_LOD_MULTIPLIER(ped,6.0F);
                e.written_lod=PED::_GET_PED_LOD_MULTIPLIER(ped);
                if(std::isfinite(e.written_lod) && e.written_lod>e.original_lod)e.owns_lod=true;
            }
            if(!e.original_update) {
                PED::SET_PED_CONFIG_FLAG(ped,133,true); // ForceNoTimesliceIntelligenceUpdate.
                e.owns_update=PED::GET_PED_CONFIG_FLAG(ped,133,true);
            }
        }
        token=race_token;active=true;refusal=nullptr;return true;
    }
};
} // namespace horse_motion
