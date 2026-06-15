#pragma once

#include <cstdint>
#include <deque>
#include <utility>

namespace hft {

// A queue buffer: push items tagged with the key (a timestamp or a counter)
// at which they become "due", then release them when the condition satisfies.
//
// to read "the value of X at t+delay", push a handle keyed `now+delay` and,
// in the release callback, read X at that later moment.
template <class Item>
class DelayBuffer {
public:
    void push(std::uint64_t due_key, Item item) {
        queue_.push_back(Node{due_key, std::move(item)});
    }

    template <class Fn>
    void releaseDue(std::uint64_t now, Fn&& fn) {
        while (!queue_.empty() && queue_.front().due_key <= now) {
            fn(queue_.front().item);
            queue_.pop_front();
        }
    }

    // end-of-stream flush.
    template <class Fn>
    void drain(Fn&& fn) {
        while (!queue_.empty()) {
            fn(queue_.front().item);
            queue_.pop_front();
        }
    }

    bool empty() const { return queue_.empty(); }

private:
    struct Node {
        std::uint64_t due_key;
        Item item;
    };
    std::deque<Node> queue_;
};

}
