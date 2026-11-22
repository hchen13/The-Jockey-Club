#pragma once
#include "horse_race_ledger.h"
#include <filesystem>
#include <functional>
#include <span>
#include <utility>

namespace horse_races::horse_ledger {
std::string Sha256(std::span<const std::uint8_t> bytes);
struct StableFileHash {
    std::string sha256;
    std::uint64_t size{}, write_filetime{}, read_start_tick_ms{}, read_end_tick_ms{};
};
std::optional<StableFileHash> HashStableFile(const std::filesystem::path&, std::string& error);
enum class LookupStatus { Loaded, Missing, Corrupt, IoError };
struct LookupResult {
    LookupStatus status{LookupStatus::Corrupt};
    std::optional<Snapshot> snapshot;
    std::string error;
};
struct SlotBinding {
    Revision archive;
    std::uint32_t slot{};
    std::string game_sha256;
    std::uint64_t generation{};
    bool operator==(const SlotBinding&)const=default;
};
struct SlotLookupResult {
    LookupStatus status{LookupStatus::Corrupt};
    std::optional<SlotBinding> binding;
    std::optional<Snapshot> snapshot;
    std::string error;
};
struct SlotResolveResult {
    LookupStatus status{LookupStatus::Corrupt};
    std::optional<SlotBinding> binding;
    std::optional<Snapshot> snapshot;
    std::optional<SlotBinding> current_binding;
    std::string error;
};
struct SlotVersion { SlotBinding binding; Snapshot snapshot; };
struct JournalIdentity {
    std::string profile, event_id;
    bool operator==(const JournalIdentity&) const = default;
};
enum class WriteResult { Written, AlreadyPresent, Conflict, Error };
enum class SaleStage { Prepared=1, RemovalEntered=2, Removed=3, PayoutEntered=4, Paid=5 };
enum class MembershipStage { Prepared=1, PaymentEntered=2, Paid=3 };
class Store {
public:
    explicit Store(std::filesystem::path root):root_(std::move(root)){}
    WriteResult Publish(const Snapshot&, std::string& error);
    SlotLookupResult LookupSlot(const std::string& profile,std::uint32_t slot)const;
    SlotResolveResult ResolveSlot(const std::string& profile,std::uint32_t slot,
        const std::string& game_sha256)const;
    // Session roots are memory-only. A race/payment may legitimately outlive
    // several slot replacements; ancestors are provenance, never owning roots.
    bool RetainRevisions(const std::vector<Revision>&,std::string& error);
    bool HoldSnapshot(const Snapshot&,std::string& error);
    void SetJournalPins(const std::vector<JournalIdentity>&);
    void RetireUnpinnedJournals();
    // Offline conversion/audit only: current versions, with every owned slot
    // validated. Unknown files are rejected instead of silently discarded.
    std::optional<std::vector<SlotVersion>> ExportSlots(std::string& error)const;
    WriteResult ImportSlot(const SlotVersion&,std::string& error);
    static std::string SnapshotDigest(const Snapshot&);
    WriteResult PublishSlot(const Snapshot&,const SlotBinding&,
        const std::optional<SlotBinding>& expected,std::string& error,
        const std::function<bool()>& fence={});
    // Audit only. Receipts never become active snapshots or replay wallet payments.
    WriteResult Journal(const Revision& base, const Win&, bool paid, std::string& error);
    // Immutable audit only; stages require their predecessor and never replay.
    WriteResult SaleJournal(const Revision& base, const Sale&, SaleStage, std::string& error);
    WriteResult MembershipJournal(const Revision&, const Membership&, MembershipStage, std::string& error);
    LookupResult Lookup(const Revision&) const;
    std::optional<Snapshot> Load(const Revision&, std::string& error) const;
    const std::filesystem::path& Root() const { return root_; }
private:
    std::filesystem::path root_;
    std::vector<Snapshot> retained_;
    std::vector<JournalIdentity> journal_pins_;
    bool retire_unpinned_journals_{};
    LookupResult LookupLegacy(const Revision&)const;
    std::vector<SlotVersion> ReadSlotVersions(const std::filesystem::path&)const;
    WriteResult WriteAudit(const Revision&,const std::string& event_id,
        const char* kind,const std::vector<std::uint8_t>& payload,
        unsigned stage,unsigned final_stage,std::string& error);
};
}
