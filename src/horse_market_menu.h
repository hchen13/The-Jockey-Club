#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>
namespace horse_races::horse_market {
struct WinningRow {std::string race_name;std::int64_t net_bonus_cents=0;};
struct SaleQuote {
    std::string horse_identity;
    std::uint64_t quote_version=0;
    std::string title;
    std::int64_t base_cents=1000;
    std::vector<WinningRow> wins;
    std::int64_t total_cents=1000;
    bool chinese=false;
};
// Presentation rows aggregate actual receipts by race; the ledger retains every win.
inline bool ValidSaleQuote(const SaleQuote& q){
    if(q.horse_identity.empty() || q.base_cents<0 || q.total_cents<0 || q.total_cents>2147483647)return false;
    std::int64_t sum=q.base_cents;
    std::unordered_set<std::string> names;
    for(const auto& win:q.wins){
        if(win.race_name.empty() || !names.insert(win.race_name).second || win.net_bonus_cents<=0 || win.net_bonus_cents>2147483647-sum)return false;
        sum+=win.net_bonus_cents;
    }
    return sum==q.total_cents;
}
inline std::string SaleMoney(std::int64_t cents){
    return "$"+std::to_string(cents/100)+"."+(cents%100<10?"0":"")+std::to_string(cents%100);
}
enum class SaleAction {SellRequested,BackClosed};
struct SaleEvent {SaleAction action;SaleQuote quote;};
// Caller revalidates full quote before sale. Keep ticking while Closing().
class SaleMenu {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    SaleMenu();
    ~SaleMenu();
    SaleMenu(const SaleMenu&)=delete;
    SaleMenu& operator=(const SaleMenu&)=delete;
    bool Open(SaleQuote quote);
    void Close(const std::string& reason = "external close");
    std::optional<SaleEvent> Tick();
    bool Active() const;
    // Also tick the bounded attack/aim tail after whole-player control returns.
    bool NeedsTick() const;
    bool Closing() const;
    std::string Summary() const;
    // Non-empty only for asynchronous UI startup failure, never normal Back.
    std::string Failure() const;
    std::vector<std::string> DrainDiagnostics();
};
}
