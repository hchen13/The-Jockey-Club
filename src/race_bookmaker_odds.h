#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <map>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace horse_races::bookmaking::odds {
// A mathematical seed, not fabricated historical race results. Course pace
// estimates supply the initial time distribution; this installation's valid
// races gradually adjust it. No network or other player's data is involved.
// This is the sample/fingerprint schema, NOT the payout curve version. Changing
// only prices must not orphan compatible local finish-time observations.
inline constexpr int kPolicyVersion=1;
inline constexpr double kPriorWeight=24,kSeedLogDeviation=.10,kSeedDnf=.06;
inline constexpr double kSpeedWeight=.8,kAccelerationWeight=.2;
inline constexpr double kStrongBaseOdds=1.1,kWeakBaseOdds=20;
inline constexpr double kLocalQuoteElasticity=.25,kLocalQuoteMinimum=.8,kLocalQuoteMaximum=1.2;
inline constexpr std::size_t kMaxRecords=512,kMaxFileBytes=128*1024;
inline std::uint64_t Fingerprint(std::string_view value) {
    std::uint64_t hash=14695981039346656037ULL;
    for(unsigned char c:value){hash^=c;hash*=1099511628211ULL;}
    return hash;
}
inline std::string Key(std::string_view value) {
    std::ostringstream out;out<<std::hex<<std::setw(16)<<std::setfill('0')<<Fingerprint(value);
    return out.str();
}
inline bool ValidKey(std::string_view value) {
    return value.size()==16 && std::all_of(value.begin(),value.end(),[](char c){
        return (c>='0'&&c<='9') || (c>='a'&&c<='f');});
}
struct Record {
    double finishes=0,outs=0,sum_log=0,sum_log_squared=0;
    bool Valid() const {
        return std::isfinite(finishes) && std::isfinite(outs) && std::isfinite(sum_log) &&
            std::isfinite(sum_log_squared) && finishes>=0 && outs>=0 && finishes+outs<=100000 &&
            sum_log_squared>=0 && sum_log_squared<=4*finishes+.000001 &&
            std::abs(sum_log)<=2*finishes+.000001 &&
            sum_log*sum_log<=finishes*sum_log_squared+.0001;
    }
};
struct Prediction {std::string key;double seconds=0;};
struct Distribution {double log_mean=0,log_deviation=kSeedLogDeviation,dnf=kSeedDnf;};
class Calibration {
    std::map<std::string,Record> records_;
public:
    const auto& Records() const {return records_;}
    Distribution Predict(const Prediction& runner) const {
        Distribution result;
        result.log_mean=std::log(std::max(1.0,runner.seconds));
        const auto found=records_.find(runner.key);
        if(found==records_.end())return result;
        const auto& r=found->second;
        const double weight=kPriorWeight+r.finishes;
        const double mean=r.sum_log/weight;
        const double variance=(kPriorWeight*kSeedLogDeviation*kSeedLogDeviation+
            r.sum_log_squared)/weight-mean*mean;
        result.log_mean+=mean;
        result.log_deviation=std::sqrt(std::clamp(variance,.04*.04,.35*.35));
        result.dnf=std::clamp((kPriorWeight*kSeedDnf+r.outs)/
            (kPriorWeight+r.finishes+r.outs),.01,.75);
        return result;
    }
    // Whole-race validity is decided by the runtime before calling Observe.
    // A natural fall/out-of-bounds is a DNF. Invalid elapsed times are never
    // silently converted to DNFs or admitted as timing evidence.
    bool Observe(const Prediction& runner,bool finished,double elapsed=0) {
        if(!ValidKey(runner.key) || !std::isfinite(runner.seconds) || runner.seconds<=0)return false;
        double value=0;
        if(finished){
            const double ratio=elapsed/runner.seconds;
            if(!std::isfinite(ratio) || ratio<.4 || ratio>3)return false;
            value=std::log(ratio);
        }
        if(!records_.contains(runner.key) && records_.size()>=kMaxRecords)return false;
        auto& r=records_[runner.key];
        // A bounded effective history still adapts after thousands of races;
        // the prior prevents one lucky race from radically changing a quote.
        if(r.finishes+r.outs>=128){
            constexpr double decay=127.0/128.0;
            r.finishes*=decay;r.outs*=decay;r.sum_log*=decay;r.sum_log_squared*=decay;
        }
        if(finished){++r.finishes;r.sum_log+=value;r.sum_log_squared+=value*value;}
        else ++r.outs;
        return true;
    }
    std::string Serialize() const {
        std::ostringstream out;out<<"TJC_WFA_ODDS "<<kPolicyVersion<<'\n'<<std::setprecision(17);
        for(const auto& [key,r]:records_)
            out<<key<<' '<<r.finishes<<' '<<r.outs<<' '<<r.sum_log<<' '<<r.sum_log_squared<<'\n';
        return out.str();
    }
    bool Parse(std::string_view bytes) {
        if(bytes.size()>kMaxFileBytes)return false;
        std::istringstream input{std::string(bytes)};
        std::string magic;int version=0;
        if(!(input>>magic>>version) || magic!="TJC_WFA_ODDS" || version!=kPolicyVersion)return false;
        std::map<std::string,Record> candidate;
        std::string line;std::getline(input,line);
        if(line.find_first_not_of(" \t\r")!=std::string::npos)return false;
        while(std::getline(input,line)){
            if(line.find_first_not_of(" \t\r")==std::string::npos)continue;
            std::istringstream row(line);std::string key,extra;Record r;
            if(!(row>>key>>r.finishes>>r.outs>>r.sum_log>>r.sum_log_squared) || row>>extra ||
                !ValidKey(key) || !r.Valid() || candidate.contains(key) || candidate.size()>=kMaxRecords)return false;
            candidate.emplace(key,r);
        }
        records_=std::move(candidate);return true;
    }
};
inline std::vector<double> Probabilities(std::span<const Prediction> runners,const Calibration& calibration) {
    if(runners.size()<2 || runners.size()>10)return {};
    std::vector<Distribution> distributions;
    double low=1e100,high=0;
    for(const auto& runner:runners){
        if(!ValidKey(runner.key) || !std::isfinite(runner.seconds) || runner.seconds<=0)return {};
        const auto d=calibration.Predict(runner);distributions.push_back(d);
        low=std::min(low,std::exp(d.log_mean-6*d.log_deviation));
        high=std::max(high,std::exp(d.log_mean+6*d.log_deviation));
    }
    // Integral: this rider finishes at t and every other rider finishes later
    // or DNF. Normalize conditional on there being a winner. Simpson's rule
    // is deterministic, so reopening a menu cannot re-roll betting odds.
    constexpr int steps=512;
    constexpr double root_two=1.4142135623730951,root_two_pi=2.5066282746310005;
    const double h=(high-low)/steps;
    std::vector<double> p(runners.size(),0);
    for(int step=0;step<=steps;++step){
        const double t=low+step*h,log_t=std::log(t);
        const int weight=step==0 || step==steps?1:(step%2?4:2);
        for(std::size_t i=0;i<p.size();++i){
            const auto& d=distributions[i];const double z=(log_t-d.log_mean)/d.log_deviation;
            double integrand=(1-d.dnf)*std::exp(-.5*z*z)/(t*d.log_deviation*root_two_pi);
            for(std::size_t j=0;j<p.size();++j)if(j!=i){
                const auto& other=distributions[j];
                const double cdf=.5*(1+std::erf((log_t-other.log_mean)/(other.log_deviation*root_two)));
                integrand*=1-(1-other.dnf)*cdf;
            }
            p[i]+=weight*integrand;
        }
    }
    double sum=0;for(double value:p)sum+=value;
    if(!std::isfinite(sum) || sum<=0)return {};
    double bounded_sum=0;
    for(auto& value:p){value=std::clamp(value/sum,.000001,.999999);bounded_sum+=value;}
    for(auto& value:p)value/=bounded_sum;
    return p;
}
// Displayed 1..10 attribute levels, not the engine's 0..9 ranks. WFA can now
// draw five-speed hosts, and F11 may set any valid displayed band. Extend the
// same interpolation below six; never change the authored 6/1 or 10/10 quotes.
// Geometric interpolation spreads the weak end without collapsing all strong
// horses onto one tiny quote. These are authored gameplay prices, not fair
// probabilities derived by normalizing the whole field.
inline double BaseWinOdds(int speed_level,int acceleration_level) {
    if(speed_level<1 || speed_level>10 || acceleration_level<1 || acceleration_level>10)return 0;
    const double strength=kSpeedWeight*(speed_level-6)/4.0+
        kAccelerationWeight*(acceleration_level-1)/9.0;
    return kWeakBaseOdds*std::pow(kStrongBaseOdds/kWeakBaseOdds,strength);
}
// Stored as hundredths for the existing cash transaction API, but prices are
// always multiples of ten hundredths: at most ONE decimal everywhere.
inline int Quantize(double net_odds) {
    if(!std::isfinite(net_odds) || net_odds<=0)return 0;
    return static_cast<int>(std::lround(std::clamp(net_odds,.1,99.0)*10))*10;
}
inline int BaseQuote(int speed_level,int acceleration_level) {
    return Quantize(BaseWinOdds(speed_level,acceleration_level));
}
inline double LocalQuoteFactor(double seed_probability,double learned_probability) {
    if(!std::isfinite(seed_probability) || !std::isfinite(learned_probability) ||
        seed_probability<=0 || seed_probability>=1 || learned_probability<=0 || learned_probability>=1)return 0;
    // Compare the identical field WITH and WITHOUT local observations. This
    // preserves the existing time/DNF learning while making roster composition,
    // handling and cornering incapable of changing the unlearned attribute base.
    // The prior already damps small samples; quarter-log elasticity limits the
    // price response further, then caps it at +/-20% of that horse's base.
    const double log_ratio=std::log1p(-learned_probability)-std::log(learned_probability)-
        std::log1p(-seed_probability)+std::log(seed_probability);
    return std::exp(std::clamp(kLocalQuoteElasticity*log_ratio,
        std::log(kLocalQuoteMinimum),std::log(kLocalQuoteMaximum)));
}
inline int AttributeQuote(int speed_level,int acceleration_level,
    double seed_probability,double learned_probability) {
    const int base=BaseQuote(speed_level,acceleration_level);
    const double factor=LocalQuoteFactor(seed_probability,learned_probability);
    if(!base || !factor)return 0;
    // Round INSIDE the allowed envelope. Ordinary rounding could exceed 20%
    // at the strong end (e.g. a base 1.3 must not round upward to 1.6).
    const int lower=(base*8+99)/100*10,upper=base*12/100*10;
    return std::clamp(Quantize(base/100.0*factor),lower,upper);
}
inline std::string Label(int hundredths) {
    if(hundredths<=0)return {};
    const auto tenths=(static_cast<std::int64_t>(hundredths)+5)/10;
    std::string result=std::to_string(tenths/10);
    if(tenths%10)result+='.'+std::to_string(tenths%10);
    return result+"-to-1";
}
} // namespace horse_races::bookmaking::odds
