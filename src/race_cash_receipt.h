#pragma once
#include "race_transaction_feed.h"
#include <cmath>
#include <deque>
#include <optional>

namespace horse_races::transaction_feed {
// Payment and presentation have different lifetimes. Cash is credited once
// when the ticket settles, even if the player quits on the results screen.
// Only its stock transaction toast waits for an unobscured gameplay HUD.
// This class NEVER writes the wallet and NEVER draws a money widget.
class CashReceipts {
    std::deque<int> pending_;
    double retry_at_=0,reveal_until_=0;
public:
    struct Posted {int cents=0,handle=0;};
    bool Queue(int cents) {
        if(cents<=0)return false;
        pending_.push_back(cents);return true;
    }
    std::size_t Pending() const {return pending_.size();}
    bool NeedsHud(double now) const {return !pending_.empty() || now<reveal_until_;}
    std::optional<Posted> Tick(double now,bool hud_ready) {
        if(!hud_ready || !std::isfinite(now))return {};
        if(now<reveal_until_ || !pending_.empty())
            HUD::_ENABLE_HUD_CONTEXT_THIS_FRAME(MISC::GET_HASH_KEY("HUD_CTX_INPUT_REVEAL_HUD"));
        if(pending_.empty() || now<retry_at_)return {};
        const int cents=pending_.front();
        const int handle=Cash(cents);
        retry_at_=now+1;
        if(!handle)return {};
        pending_.pop_front();reveal_until_=now+3.5;
        return Posted{cents,handle};
    }
};
} // namespace horse_races::transaction_feed
