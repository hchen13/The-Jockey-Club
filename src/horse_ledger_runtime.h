#pragma once
#include "horse_race_ledger.h"
#include "horse_individual_identity.h"
#include <functional>

namespace horse_races::horse_ledger::runtime {
// One loaded world owns one working archive. Proven startup/manual loads and
// completed save writes pair the archive with its exact native slot and bytes.
// Receipts are audit evidence only, never a replay queue.
struct Options { bool validation{}; std::string profile; bool sales{}; };
void Initialize(const std::string& directory, void (*log)(const std::string&), Options options = {});
void Tick();
std::uint64_t Timeline();
// Read the world already observed by Tick; display code must not force another save probe every frame.
std::uint64_t ObservedTimeline();
struct ProgressView { std::uint64_t generation{}; std::string encoded; };
// A view of the current archive, never a separate global progress file.
std::optional<ProgressView> ReadProgress();
// Read-only persistent archive failure for existing UI; empty during ordinary loading.
std::string ArchiveIssue();
bool WriteProgress(std::uint64_t expected_generation, const std::string& expected,
    const std::string& replacement);
void BeginRace(int horse, Win metadata);
void Outcome(bool won, bool accident = false);
void CancelRace();
void PaymentQueued(std::uint64_t timeline);
enum class PaymentDecision { Allow, Defer, Discard };
PaymentDecision BeforePayment(int amount, std::uint64_t queued_timeline);
void AfterPayment(int amount);
void PaymentFailed();

struct MarketQuote {
    individual_identity::Identity identity;
    Revision revision;
    std::uint64_t timeline{}, epoch{}, mutation{};
    Sale sale;
    std::vector<Win> wins;
    std::vector<Win> live_wins; // Entire working history, including other horses/practice.
};
std::optional<MarketQuote> QuoteHorse(int horse, Cents base, std::string& reason);
bool CheckQuote(const MarketQuote&, int horse, std::string& reason);
enum class MarketTransactionState { Ready, Saving, Invalid };
// Global availability only, before horse/member qualification. Saving/loading
// may hide interaction prompts without displaying a technical reason.
MarketTransactionState TradingReadiness(std::string& reason);
MarketTransactionState ValidateSaleForCommit(const MarketQuote&,int horse,std::string& reason);
enum class RemovalOutcome { NotStarted, Unchanged, Removed, Uncertain };
struct SaleCompletion { bool sold{}, reload_required{}; std::string message; };
// The callback is synchronous, must not yield, and never pays. The runtime owns
// durable phase receipts, one cash operation, and the save-bound sold event.
SaleCompletion CommitSale(const MarketQuote&, int horse,
    const std::function<RemovalOutcome(std::string&)>& remove);

SellerEra CurrentSeller();
// Read-only provider for progression; it never writes progression.ini or pumps.
bool IsMember(SellerEra);
using HonoraryQuery=std::function<bool()>;
// Read-only, current-protagonist qualification query. Displayed prices must be
// passed as expected_fee so eligibility changes cannot change a held purchase.
bool CanPurchaseMembership(std::string& reason,const HonoraryQuery& honorary_eligible={});
struct MembershipCompletion { bool purchased{}, reload_required{}; std::string message; };
MembershipCompletion PurchaseMembership(SellerEra expected,
    const std::function<bool()>& betting_unlocked,const HonoraryQuery& honorary_eligible={},
    std::optional<Cents> expected_fee={});
// F11 clears only the current protagonist's loaded sale/member history. The
// change follows the next game save; retained older slot generations stay recoverable.
bool ClearMembershipRecords(bool busy, std::string& reason);
}
