#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "alpha.h"
#include "delay_buffer.h"
#include "output_sink.h"
#include "types.h"

namespace hft {

struct ResolvedLead {
    const Alpha*  src = nullptr;
    std::size_t   channel = 0;
    bool          tick = false; // true if delay is in alpha-updating ticks else nanoseconds
    std::uint64_t delay = 0;    // nanosecons or alpha-updating ticks
    bool          isReturn = false;
};

// Resolves a channel name to its output index for an alpha (empty = first).
inline std::size_t channelIndex(const Alpha& a, const std::string& channel) {
    if (channel.empty()) {
        return 0;
    }
    const std::vector<std::string>& names = a.outputNames();
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (names[i] == channel) {
            return i;
        }
    }
    throw std::runtime_error("alpha '" + std::string(a.name()) +
                             "' has no channel '" + channel + "'");
}

// Gathers the alphas' outputs and emits it to the sink.
// Keeps the replay loop trivial.
class SymbolSampler {
public:
    SymbolSampler(std::uint16_t locate, std::vector<const Alpha*> alphas,
                  std::vector<ResolvedLead> leads, OutputSink& sink)
        : locate_(locate), alphas_(std::move(alphas)),
          leads_(std::move(leads)), sink_(sink), delayedBuffers(leads_.size()) {}

    // No leads -> emit now; else defer until the leads resolve.
    void sample(std::uint64_t eventIndex, std::uint64_t tickIndex, Ts ts) {
        if (leads_.empty()) {
            scratch_.clear();
            gather(scratch_);
            emit(eventIndex, ts, scratch_);
            return;
        }
        std::unique_ptr<RowState> row = std::make_unique<RowState>();
        row->eventIndex = eventIndex;
        row->ts = ts;
        gather(row->values);
        const std::size_t base = row->values.size(); // first lead slot
        row->values.resize(base + leads_.size(), std::numeric_limits<double>::quiet_NaN());
        row->remaining = static_cast<int>(leads_.size());
        RowState* row_ptr = row.get();
        for (std::size_t i = 0; i < leads_.size(); ++i) {
            const ResolvedLead& lead = leads_[i];
            if ((lead.isReturn) && (lead.src->output()[lead.channel] == 0.0)) {
                --row->remaining;
                continue;  // shortcircut
            }
            const std::uint64_t due = (lead.tick ? tickIndex : ts) + lead.delay;
            delayedBuffers[i].push(due, Entry{row_ptr, lead.src->output()[lead.channel], base + i});
        }
        pending_.push_back(std::move(row));
    }

    // Release lead cells whose delay has elapsed (reading its value at
    // t+delay), then emit any completed rows in order.
    void advance(Ts ts, std::uint64_t updates) {
        for (std::size_t i = 0; i < leads_.size(); ++i) {
            const ResolvedLead& lead = leads_[i];
            const std::uint64_t now = lead.tick ? updates : ts;
            delayedBuffers[i].releaseDue(now, [&](Entry& e) {
                const double cur = lead.src->output()[lead.channel];
                e.row->values[e.cell] =
                    lead.isReturn ? (e.initialValue != 0.0 ? (cur - e.initialValue) / e.initialValue : std::numeric_limits<double>::quiet_NaN()) : cur;
                --e.row->remaining;
            });
        }
        emitCompleted();
    }

    // End of stream: unresolved leads stay NaN; emit everything still pending.
    void flush() {
        for (DelayBuffer<Entry>& db : delayedBuffers) {
            db.drain([](Entry& e) { --e.row->remaining; });
        }
        while (!pending_.empty()) {
            emit(*pending_.front());
            pending_.pop_front();
        }
    }

private:
    struct RowState {
        std::uint64_t eventIndex = 0;
        Ts ts = 0;
        std::vector<double> values; // signals, then one slot per lead (NaN until filled)
        int remaining = 0;          // leads left to resolve
    };
    struct Entry {
        RowState* row;
        double initialValue; // for return transform
        std::size_t cell;
    };

    void gather(std::vector<double>& dst) const {
        for (const Alpha* a : alphas_) {
            dst.insert(dst.end(), a->output().begin(), a->output().end());
        }
    }
    void emit(std::uint64_t index, Ts ts, const std::vector<double>& values) {
        sink_.onRow(SampleRow{locate_, index, ts,
                              std::span<const double>(values.data(), values.size())});
    }
    void emit(const RowState& r) { emit(r.eventIndex, r.ts, r.values); }
    void emitCompleted() {
        while (!pending_.empty() && pending_.front()->remaining == 0) {
            emit(*pending_.front());
            pending_.pop_front();
        }
    }

    std::uint16_t locate_ = 0;
    std::vector<const Alpha*> alphas_;
    std::vector<ResolvedLead> leads_;
    OutputSink& sink_;
    std::vector<double> scratch_;                   // reused gather buffer (no-lead path)
    std::vector<DelayBuffer<Entry>> delayedBuffers; // one timed-release buffer per lead
    std::deque<std::unique_ptr<RowState>> pending_; // completes FIFO in sample order
};

}
