#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <memory>
#include <string>
#include <vector>

namespace horse_races::horse_ledger {
using Cents = std::int64_t;
enum class SellerEra { Unknown=0, Arthur=1, John=2 };
inline constexpr Cents kMembershipFeeCents=100000;

struct Guid {
    std::array<std::uint32_t, 4> words{};
    bool Valid() const;
    std::string Key() const;
    bool operator==(const Guid&) const = default;
};
enum class RaceKind { Duel = 1, BreedLimited = 2 };
struct Opponent {
    std::string character;
    std::uint32_t model{};
    int speed{}, acceleration{};
    bool host{};
    bool operator==(const Opponent&) const = default;
};
struct Win {
    std::string event_id;
    Guid horse;
    std::string race_id, race_name, protagonist, horse_name;
    std::uint32_t horse_model{};
    RaceKind kind{RaceKind::Duel};
    std::vector<Opponent> opponents;
    Cents net_cents{};
    std::uint64_t unix_ms{};
    bool practice{}, opponent_accident{};
    bool operator==(const Win&) const = default;
};
struct Sale {
    std::string event_id;
    Guid horse;
    Cents base_cents{}, premium_cents{}, total_cents{};
    std::vector<std::string> win_event_ids;
    std::uint64_t unix_ms{};
    SellerEra seller{SellerEra::Unknown};
    bool operator==(const Sale&) const = default;
};
struct Membership {
    // New registrations require a same-era sale ID; honorary fee=0, otherwise100000.
    // Legacy fee0/empty qualifier remains valid for restoration, never new addition.
    std::string event_id;
    SellerEra seller{SellerEra::Unknown};
    Cents fee_cents{kMembershipFeeCents};
    std::string qualifying_sale_id;
    std::uint64_t unix_ms{};
    bool operator==(const Membership&) const = default;
};
struct Revision {
    std::string profile, sha256;
    bool Valid() const;
    bool operator==(const Revision&) const = default;
};
enum class Evidence { EmptyBaseline = 1, ObservedCandidate = 2, Verified = 3 };
struct Snapshot {
    Revision revision;
    std::optional<Revision> parent;
    Evidence evidence{Evidence::ObservedCandidate};
    std::vector<Win> wins;
    std::vector<Sale> sales;
    std::vector<Membership> memberships;
    std::optional<std::string> progression; // nullopt only for legacy archives
    bool operator==(const Snapshot&) const = default;
};
bool Validate(const Win&, std::string* reason = nullptr);
bool Validate(const Sale&, std::string* reason = nullptr);
bool Validate(const Membership&, std::string* reason = nullptr);
bool Validate(const Snapshot&, std::string* reason = nullptr);
enum class AddResult { Added, Duplicate, Unbound, Unconfirmed, Invalid, Conflict };
class FrozenCapture;
struct Frozen {
    std::uint64_t epoch{}, mutation{};
    Revision parent;
    std::vector<Win> wins;
    std::vector<Sale> sales;
    std::vector<Membership> memberships;
    std::optional<std::string> progression;
    std::shared_ptr<const FrozenCapture> capture; // issued only by Ledger::Freeze
};
struct Summary {
    std::uint64_t official_wins{}, practice_wins{};
    Cents premium_cents{};
    bool valid{};
};

// Pure domain model. Confirmation/evidence are caller assertions, not native proofs.
class Ledger {
public:
    bool Restore(const Snapshot&);
    void Unbind();
    AddResult Append(const Win&, bool confirmed = false);
    AddResult MarkSold(const Sale&, bool confirmed = false);
    AddResult AddMembership(const Membership&, bool confirmed = false);
    bool HasQualifyingSale(SellerEra) const;
    bool IsMember(SellerEra) const;
    // Ordinary working mutation; existing authentic captures remain valid.
    bool ClearMarketHistory(SellerEra);
    const std::optional<std::string>& Progression() const;
    bool SetProgression(std::string);
    std::optional<Frozen> Freeze() const;
    bool CanAcceptSaved(const Frozen&) const;
    bool AcceptSaved(const Frozen&, const Revision&, Evidence);
    std::optional<Revision> Active() const;
    Evidence Quality() const;
    std::uint64_t Epoch() const;
    std::uint64_t Mutation() const;
    const std::vector<Win>& Wins() const;
    const std::vector<Sale>& Sales() const;
    const std::vector<Membership>& Memberships() const;
    bool Sold(const Guid&) const;
    Summary SummaryFor(const Guid&) const;
    std::optional<Cents> Quote(const Guid&, Cents base, bool allow_candidate = false) const;
private:
    std::optional<Snapshot> active_;
    std::vector<Win> wins_;
    std::vector<Sale> sales_;
    std::vector<Membership> memberships_;
    std::optional<std::string> progression_;
    std::shared_ptr<const int> capture_epoch_=std::make_shared<const int>(0);
    std::uint64_t epoch_{}, mutation_{};
};
} // namespace horse_races::horse_ledger
