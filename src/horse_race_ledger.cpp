#include "horse_race_ledger.h"
#include <algorithm>
#include <iomanip>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace horse_races::horse_ledger {
// Incomplete in the public header: callers can copy a capability, not create one.
class FrozenCapture {
public:
    const Ledger* issuer;
    std::shared_ptr<const int> epoch;
    Frozen payload;
    FrozenCapture(const Ledger* who,std::shared_ptr<const int> generation,Frozen value)
        :issuer(who),epoch(std::move(generation)),payload(std::move(value)){}
};
namespace {
bool Fail(std::string* reason, const char* text) { if (reason) *reason = text; return false; }
bool Text(const std::string& text, std::size_t max, bool required = true) {
    return (!required || !text.empty()) && text.size() <= max &&
        std::none_of(text.begin(), text.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
bool QualityValid(Evidence e) {
    return e == Evidence::EmptyBaseline || e == Evidence::ObservedCandidate || e == Evidence::Verified;
}
bool EraValid(SellerEra era,bool unknown=false){return era==SellerEra::Arthur || era==SellerEra::John || (unknown&&era==SellerEra::Unknown);}
constexpr std::size_t MaxWins = 100000;
}
bool Guid::Valid() const { return std::any_of(words.begin(), words.end(), [](auto w) { return w != 0; }); }
std::string Guid::Key() const {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < words.size(); ++i) { if (i) out << ':'; out << std::setw(8) << words[i]; }
    return out.str();
}
bool Revision::Valid() const {
    return Text(profile, 1024) && sha256.size() == 64 && std::all_of(sha256.begin(), sha256.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
bool Validate(const Win& w, std::string* reason) {
    if (reason) reason->clear();
    if (!w.horse.Valid()) return Fail(reason, "zero horse GUID");
    if (!Text(w.event_id, 128) || !Text(w.race_id, 128) || !Text(w.race_name, 256) || !Text(w.protagonist, 256) || !Text(w.horse_name, 256, false)) return Fail(reason, "invalid or oversized metadata");
    if (w.kind != RaceKind::Duel && w.kind != RaceKind::BreedLimited) return Fail(reason, "unsupported participant race kind");
    if (w.net_cents < 0 || (w.practice && w.net_cents != 0)) return Fail(reason, "invalid actual net winnings");
    if (w.opponents.size() > 32) return Fail(reason, "too many opponents");
    for (const auto& o : w.opponents) {
        if (!Text(o.character, 256, false) || o.speed < 0 || o.speed > 10 || o.acceleration < 0 || o.acceleration > 10) return Fail(reason, "invalid opponent metadata");
    }
    return true;
}
bool Validate(const Sale& s, std::string* reason) {
    if(reason)reason->clear();
    if(!s.horse.Valid() || !Text(s.event_id,128))return Fail(reason,"invalid sale identity");
    if(!EraValid(s.seller,true))return Fail(reason,"invalid sale seller");
    if(s.base_cents<0 || s.premium_cents<0 || s.premium_cents>std::numeric_limits<Cents>::max()-s.base_cents ||
        s.total_cents!=s.base_cents+s.premium_cents)return Fail(reason,"invalid sale totals");
    if(s.win_event_ids.empty() || s.win_event_ids.size()>MaxWins)return Fail(reason,"invalid sale win count");
    std::unordered_set<std::string> ids;
    for(const auto& id:s.win_event_ids)
        if(!Text(id,128) || id==s.event_id || !ids.insert(id).second)return Fail(reason,"invalid sale win IDs");
    return true;
}
bool Validate(const Membership& m,std::string* reason){
    if(reason)reason->clear();
    if(!Text(m.event_id,128)||m.event_id==m.qualifying_sale_id)return Fail(reason,"invalid membership identity");
    if(!EraValid(m.seller))return Fail(reason,"membership requires known protagonist");
    const bool legacy_honorary=m.fee_cents==0&&m.qualifying_sale_id.empty();
    if(!legacy_honorary&&((m.fee_cents!=0&&m.fee_cents!=kMembershipFeeCents)||!Text(m.qualifying_sale_id,128)))return Fail(reason,"invalid membership fee or qualification");
    return true;
}
bool Validate(const Snapshot& s, std::string* reason) {
    if (reason) reason->clear();
    if (!s.revision.Valid() || (s.parent && (!s.parent->Valid() || s.parent->profile != s.revision.profile))) return Fail(reason, "invalid revision or parent");
    if (!QualityValid(s.evidence) || (s.evidence == Evidence::EmptyBaseline && (!s.wins.empty() || !s.sales.empty() || !s.memberships.empty()))) return Fail(reason, "invalid evidence");
    if(s.progression&&(s.progression->empty()||s.progression->size()>4096||s.progression->find('\0')!=std::string::npos))return Fail(reason,"invalid progression payload");
    if(s.memberships.size()>2)return Fail(reason,"duplicate membership era");
    if (s.wins.size() > MaxWins || s.sales.size()>MaxWins) return Fail(reason, "history capacity exceeded");
    std::unordered_set<std::string> ids;
    std::unordered_map<std::string, Cents> totals;
    std::unordered_map<std::string, const Win*> by_event;
    std::unordered_map<std::string, std::size_t> official_counts;
    for (const auto& w : s.wins) {
        if (!Validate(w, reason)) return false;
        if (!ids.insert(w.event_id).second) return Fail(reason, "duplicate payment event ID");
        by_event.emplace(w.event_id,&w);
        if(!w.practice)++official_counts[w.horse.Key()];
        Cents& total = totals[w.horse.Key()];
        if (w.net_cents > std::numeric_limits<Cents>::max() - total) return Fail(reason, "premium overflow");
        total += w.net_cents;
    }
    std::unordered_set<std::string> sold;
    std::unordered_map<std::string,SellerEra> sale_sellers;
    for(const auto& sale:s.sales){
        if(!Validate(sale,reason))return false;
        if(!ids.insert(sale.event_id).second)return Fail(reason,"duplicate sale event ID");
        if(!sold.insert(sale.horse.Key()).second)return Fail(reason,"horse already sold");
        sale_sellers.emplace(sale.event_id,sale.seller);
        if(totals[sale.horse.Key()]!=sale.premium_cents || official_counts[sale.horse.Key()]!=sale.win_event_ids.size())return Fail(reason,"sale does not match official winnings");
        for(const auto& id:sale.win_event_ids){
            const auto found=by_event.find(id);
            if(found==by_event.end() || found->second->practice || found->second->horse!=sale.horse)return Fail(reason,"sale references another or missing win");
        }
    }
    std::unordered_set<int> members;
    for(const auto& member:s.memberships){
        if(!Validate(member,reason))return false;
        if(!ids.insert(member.event_id).second)return Fail(reason,"duplicate membership event ID");
        if(!members.insert(static_cast<int>(member.seller)).second)return Fail(reason,"duplicate membership era");
        if(member.fee_cents==0&&member.qualifying_sale_id.empty()){
            if(s.progression)return Fail(reason,"unified membership requires same protagonist sale");
            continue; // Legacy audit archives retain their original shape.
        }
        const auto sale=sale_sellers.find(member.qualifying_sale_id);
        if(sale==sale_sellers.end() || sale->second!=member.seller)return Fail(reason,"membership requires same protagonist sale");
    }
    return true;
}
bool Ledger::Restore(const Snapshot& snapshot) {
    Unbind();
    if (!Validate(snapshot)) return false;
    active_ = snapshot;
    wins_ = snapshot.wins;
    sales_ = snapshot.sales;
    memberships_=snapshot.memberships;progression_=snapshot.progression;
    return true;
}
void Ledger::Unbind() { active_.reset(); wins_.clear(); sales_.clear(); memberships_.clear();progression_.reset();capture_epoch_=std::make_shared<const int>(0); ++epoch_; mutation_ = 0; }
AddResult Ledger::Append(const Win& w, bool confirmed) {
    if (!active_) return AddResult::Unbound;
    if (!confirmed) return AddResult::Unconfirmed;
    if (!Validate(w)) return AddResult::Invalid;
    auto existing = std::find_if(wins_.begin(), wins_.end(), [&](const Win& prior) { return prior.event_id == w.event_id; });
    if (existing != wins_.end()) return *existing == w ? AddResult::Duplicate : AddResult::Conflict;
    if(Sold(w.horse) || std::any_of(sales_.begin(),sales_.end(),[&](const Sale& s){return s.event_id==w.event_id;}))return AddResult::Conflict;
    if(std::any_of(memberships_.begin(),memberships_.end(),[&](const Membership& m){return m.event_id==w.event_id;}))return AddResult::Conflict;
    const auto summary = SummaryFor(w.horse);
    if (wins_.size() >= MaxWins || !summary.valid || w.net_cents > std::numeric_limits<Cents>::max() - summary.premium_cents) return AddResult::Invalid;
    wins_.push_back(w); ++mutation_;
    return AddResult::Added;
}
AddResult Ledger::MarkSold(const Sale& sale,bool confirmed){
    if(!active_)return AddResult::Unbound;
    if(!confirmed)return AddResult::Unconfirmed;
    if(!Validate(sale))return AddResult::Invalid;
    for(const auto& prior:sales_){
        if(prior.event_id==sale.event_id)return prior==sale?AddResult::Duplicate:AddResult::Conflict;
        if(prior.horse==sale.horse)return AddResult::Conflict;
    }
    if(std::any_of(wins_.begin(),wins_.end(),[&](const Win& w){return w.event_id==sale.event_id;}))return AddResult::Conflict;
    if(std::any_of(memberships_.begin(),memberships_.end(),[&](const Membership& m){return m.event_id==sale.event_id;}))return AddResult::Conflict;
    Snapshot proposed{active_->revision,active_->parent,Evidence::ObservedCandidate,wins_,sales_,memberships_,progression_};
    proposed.sales.push_back(sale);
    if(!Validate(proposed))return AddResult::Invalid;
    sales_.push_back(sale);++mutation_;
    return AddResult::Added;
}
bool Ledger::HasQualifyingSale(SellerEra era)const{
    return EraValid(era)&&std::any_of(sales_.begin(),sales_.end(),[&](const Sale& sale){return sale.seller==era;});
}
bool Ledger::IsMember(SellerEra era)const{
    return EraValid(era)&&std::any_of(memberships_.begin(),memberships_.end(),[&](const Membership& member){return member.seller==era;});
}
bool Ledger::ClearMarketHistory(SellerEra era){
    if(!active_ || !EraValid(era))return false;
    std::erase_if(memberships_,[&](const Membership& member){return member.seller==era;});
    std::erase_if(sales_,[&](const Sale& sale){return sale.seller==era;});
    // A deletion changes quotes, but does not invalidate an already packed save.
    ++mutation_;return true;
}
AddResult Ledger::AddMembership(const Membership& member,bool confirmed){
    if(!active_)return AddResult::Unbound;
    if(!confirmed)return AddResult::Unconfirmed;
    if(!Validate(member))return AddResult::Invalid;
    for(const auto& prior:memberships_){
        if(prior.event_id==member.event_id)return prior==member?AddResult::Duplicate:AddResult::Conflict;
        if(prior.seller==member.seller)return AddResult::Conflict;
    }
    if(std::any_of(wins_.begin(),wins_.end(),[&](const Win& w){return w.event_id==member.event_id;}) ||
        std::any_of(sales_.begin(),sales_.end(),[&](const Sale& s){return s.event_id==member.event_id;}))return AddResult::Conflict;
    // New registrations always require a sale; Restore retains legacy fee0/empty compatibility.
    if(member.qualifying_sale_id.empty())return AddResult::Invalid;
    Snapshot proposed{active_->revision,active_->parent,Evidence::ObservedCandidate,wins_,sales_,memberships_,progression_};
    proposed.memberships.push_back(member);if(!Validate(proposed))return AddResult::Invalid;
    memberships_.push_back(member);++mutation_;return AddResult::Added;
}
const std::optional<std::string>& Ledger::Progression()const{return progression_;}
bool Ledger::SetProgression(std::string value){
    if(!active_||value.empty()||value.size()>4096||value.find('\0')!=std::string::npos)return false;
    if(progression_&&*progression_==value)return true;
    Snapshot proposed{active_->revision,active_->parent,Evidence::ObservedCandidate,wins_,sales_,memberships_,value};
    if(!Validate(proposed))return false;
    progression_=std::move(value);++mutation_;return true;
}
std::optional<Frozen> Ledger::Freeze() const {
    if (!active_) return std::nullopt;
    Frozen frozen{epoch_,mutation_,active_->revision,wins_,sales_,memberships_,progression_};
    frozen.capture=std::make_shared<const FrozenCapture>(this,capture_epoch_,frozen);
    return frozen;
}
bool Ledger::CanAcceptSaved(const Frozen& f)const{
    if(!active_||!f.capture||f.capture->issuer!=this||f.capture->epoch!=capture_epoch_||
       f.epoch!=epoch_||f.parent!=active_->revision||f.mutation>mutation_)return false;
    const auto& issued=f.capture->payload;
    if(f.epoch!=issued.epoch||f.mutation!=issued.mutation||f.wins!=issued.wins||
       f.sales!=issued.sales||f.memberships!=issued.memberships||f.progression!=issued.progression)return false;
    return true;
}
bool Ledger::AcceptSaved(const Frozen& f,const Revision& revision,Evidence evidence){
    if(!CanAcceptSaved(f))return false;
    Snapshot saved{revision,f.parent,evidence,f.wins,f.sales,f.memberships,f.progression};
    if(!Validate(saved)||revision.profile!=active_->revision.profile)return false;
    active_=std::move(saved); // Never roll current working state back to a past capture.
    return true;
}
std::optional<Revision> Ledger::Active() const { return active_ ? std::optional<Revision>(active_->revision) : std::nullopt; }
Evidence Ledger::Quality() const { return active_ ? active_->evidence : Evidence::ObservedCandidate; }
std::uint64_t Ledger::Epoch() const { return epoch_; }
std::uint64_t Ledger::Mutation() const { return mutation_; }
const std::vector<Win>& Ledger::Wins() const { return wins_; }
const std::vector<Sale>& Ledger::Sales() const {return sales_;}
const std::vector<Membership>& Ledger::Memberships() const {return memberships_;}
bool Ledger::Sold(const Guid& horse) const {return std::any_of(sales_.begin(),sales_.end(),[&](const Sale& s){return s.horse==horse;});}
Summary Ledger::SummaryFor(const Guid& horse) const {
    Summary s{};
    if (!active_ || !horse.Valid()) return s;
    s.valid = true;
    for (const auto& w : wins_) if (w.horse == horse) {
        if (w.practice) ++s.practice_wins;
        else {
            ++s.official_wins;
            if (w.net_cents > std::numeric_limits<Cents>::max() - s.premium_cents) { s.valid = false; return s; }
            s.premium_cents += w.net_cents;
        }
    }
    return s;
}
std::optional<Cents> Ledger::Quote(const Guid& horse, Cents base,bool allow_candidate) const {
    const auto s = SummaryFor(horse);
    if ((Quality()!=Evidence::Verified && !(allow_candidate && (Quality()==Evidence::ObservedCandidate || Quality()==Evidence::EmptyBaseline))) || Sold(horse) || !s.valid || s.official_wins == 0 || base < 0 || s.premium_cents > std::numeric_limits<Cents>::max() - base) return std::nullopt;
    return base + s.premium_cents;
}
} // namespace horse_races::horse_ledger
