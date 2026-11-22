#include "horse_ledger_binding.h"
#include <cstdio>
#include <algorithm>

namespace horse_races::horse_ledger::binding {
namespace {
bool Slot(std::uint32_t value) { return value <= 15; }
bool Early(std::uint32_t state) { return state >= 1 && state <= 6; }
bool WriteOp(std::uint32_t operation) { return operation == 6 || operation == 7; }
bool Name(const save_observer::Raw& raw, std::uint32_t slot) {
    char expected[16]{};
    std::snprintf(expected, sizeof(expected), "SRDR3%04u", slot);
    const auto end = std::find(raw.filename.begin(), raw.filename.end(), '\0');
    return std::string(raw.filename.begin(), end) == expected;
}
}
void Detector::Reset() { previous_.reset(); load_.reset(); write_.reset(); load_blocked_ = false; }
std::vector<Action> Detector::Consume(const save_observer::Observation& observation) {
    std::vector<Action> result;
    const auto& r = observation.raw;
    auto emit = [&](Kind kind, const char* reason, const Pending* pending = nullptr) {
        Action action{kind};
        action.tick_ms = observation.tick_ms; action.sequence = observation.sequence;
        action.pack_counter = pending ? pending->pack_counter : r.pack_counter;
        action.disk_counter = r.disk_counter;
        action.observed_pack_counter = r.pack_counter;
        action.slot = pending ? pending->slot : std::numeric_limits<std::uint32_t>::max();
        action.request_tick_ms = pending ? pending->tick_ms : observation.tick_ms;
        action.reason = reason; result.push_back(std::move(action));
    };
    if (!observation.healthy) {
        emit(Kind::Uncertain, observation.issue.empty() ? "unhealthy observer" : observation.issue.c_str());
        Reset(); return result;
    }
    if (!previous_) { previous_ = observation; return result; }
    if (observation.sequence == previous_->sequence && observation.raw == previous_->raw && observation.tick_ms >= previous_->tick_ms) return result;
    if (observation.sequence != previous_->sequence + 1 || observation.tick_ms < previous_->tick_ms) {
        emit(Kind::Uncertain, "observation sequence gap or time reversal");
        Reset(); previous_ = observation; return result;
    }
    const auto& p = previous_->raw;
    const bool had_blocked_load = load_blocked_;
    const bool request = p.pending == 0 && r.pending == 1 && r.load_phase == 0 && r.load_state <= 1;
    if (request) {
        if (write_) { emit(Kind::Uncertain, "load overlaps disk write", &*write_); write_.reset(); }
        if (load_ || load_blocked_ || !Slot(r.load_slot)) {
            emit(Kind::Uncertain, "overlapping or invalid load request"); load_.reset(); load_blocked_ = true;
        } else {
            load_ = Pending{r.load_slot, 3, r.pack_counter, observation.tick_ms};
            emit(Kind::LoadBegin, "witnessed load request", &*load_);
        }
    }
    const bool had_load_this_observation = load_.has_value();
    if (load_) {
        if (r.load_state == 2) {
            emit(Kind::LoadFailed, "explicit outer load failure", &*load_); load_.reset(); load_blocked_ = true;
        } else if (r.load_slot != load_->slot || r.load_state > 2 || (r.load_state == 1 && r.load_phase == 3 && (!Name(r, load_->slot) || r.operation != 3))) {
            emit(Kind::Uncertain, "load slot, filename or operation mismatch", &*load_); load_.reset(); load_blocked_ = true;
        } else if (r.load_state == 1 && r.load_phase == 3 && r.pending == 1 && r.operation == 3 && Name(r, load_->slot)) {
            load_->busy = true;
        } else if (load_->busy && r.pending == 0 && r.load_state == 1 && r.load_phase == 3 &&
                   r.operation == 3 && Name(r, load_->slot)) {
            // The request flag can clear one observation before outer state1.
            // This is still the witnessed load, never a successful terminal.
            // Retain its original request/slot until the verified outer0 edge.
        } else if (r.pending == 0) {
            if (load_->busy && r.load_state == 0 && r.load_phase == 3 && Name(r, load_->slot)) emit(Kind::LoadCandidate, "observed load terminal; world readiness unproven", &*load_);
            else emit(Kind::Uncertain, "load ended without witnessed successful busy phase", &*load_);
            load_.reset();
            // Absorb the observed terminal's retained operation3 until idle;
            // it is not a second load and cannot establish a fresh request.
            load_blocked_ = true;
        }
    }
    if (!had_load_this_observation && !load_blocked_ &&
        (r.pending != 0 || r.load_state == 1 || r.operation == 3)) {
        emit(Kind::Uncertain, "load activity without witnessed request");
        write_.reset(); load_blocked_ = true;
    }
    if (load_blocked_ && r.pending == 0 && r.load_state == 0 && r.operation != 3) load_blocked_ = false;
    if (r.pack_counter != p.pack_counter) {
        emit(Kind::PackRequested, "pack counter changed; request boundary only");
        const auto delta = static_cast<std::uint32_t>(r.pack_counter - p.pack_counter);
        if (delta != 1) {
            const auto saveOnly=[](const save_observer::Raw& raw){
                return raw.pending==0&&raw.load_state==0&&
                    (raw.operation==0||raw.operation==2||raw.operation==6||raw.operation==7);
            };
            // A healthy contiguous pair can disprove a serializer boundary
            // without disproving the current world. Never use this exception
            // for the observation completing a load or clearing a blocked load.
            const bool captureOnly=previous_->healthy&&!had_load_this_observation&&
                !had_blocked_load&&!load_&&!load_blocked_&&saveOnly(p)&&saveOnly(r);
            emit(captureOnly?Kind::SaveCaptureUncertain:Kind::Uncertain,
                "pack counter jumped; capture boundary ambiguous");
            load_.reset(); write_.reset(); load_blocked_ = r.pending != 0;
            previous_ = observation; return result;
        }
    }
    const bool write_edge = WriteOp(r.operation) && Early(r.save_state) &&
        !(p.operation == r.operation && Early(p.save_state));
    if (write_edge) {
        if (load_ || load_blocked_ || r.pending != 0 || r.load_state != 0) {
            emit(Kind::Uncertain, "write request overlaps load"); write_.reset();
        } else if (write_ || (r.operation == 6 && r.save_slot > 14)) {
            emit(Kind::WriteRejected, "overlapping or invalid save-only write request", write_ ? &*write_ : nullptr); write_.reset();
        } else {
            const auto slot = r.operation == 7 ? 15u : r.save_slot;
            write_ = Pending{slot, r.operation, r.pack_counter, observation.tick_ms};
            emit(Kind::WriteBegin, "disk write began; serializer freeze not established", &*write_);
        }
    }
    if (write_) {
        const bool terminal = r.operation == 0 && r.save_state == 0 && r.disk_state == 0;
        const bool early_auto_stale_slot = write_->operation == 7 && Early(r.save_state);
        if (r.pending != 0 || r.load_state != 0 || load_ || load_blocked_) {
            emit(Kind::Uncertain, "write overlaps load activity", &*write_); write_.reset();
        } else if ((!early_auto_stale_slot && r.save_slot != write_->slot) ||
            (!terminal && r.operation != write_->operation) || r.save_state > 9 || r.disk_state == 2 || r.disk_state > 3 ||
            (write_->busy && !Name(r, write_->slot))) {
            emit(Kind::WriteRejected, "save-only write status, overlap or slot drift", &*write_); write_.reset();
        } else if (r.save_state == 7) {
            // Native BCA8C4 accepts result3 into phase8; result2 is failure.
            // Phase7 is set before enqueue, so disk0 may precede disk1. A
            // sampled result3 can witness the worker when polling missed1.
            if ((r.disk_state == 1 || r.disk_state == 3) && Name(r, write_->slot)) write_->busy = true;
            else if (r.disk_state != 0) { emit(Kind::WriteRejected, "disk worker filename or state mismatch", &*write_); write_.reset(); }
        } else if (r.save_state == 9) {
            if (Name(r, write_->slot)) write_->phase9 = true;
            else { emit(Kind::WriteRejected, "write phase9 filename mismatch", &*write_); write_.reset(); }
        } else if (terminal) {
            if (write_->busy && write_->phase9 && Name(r, write_->slot)) emit(Kind::WriteCandidate, "disk write observation only; exact success unproven", &*write_);
            else emit(Kind::WriteRejected, "write ended without busy, phase9 and filename evidence", &*write_);
            write_.reset();
        }
    }
    previous_ = observation;
    return result;
}
}
