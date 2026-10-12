#include "hnpch.h"
#include "console_buffer.h"

#include <deque>
#include <mutex>

namespace Honey {

    namespace {
        std::mutex s_mutex;
        std::deque<ConsoleEntry> s_entries;
        uint64_t s_next_seq = 1;
    }

    void ConsoleBuffer::push(ConsoleEntry entry) {
        std::lock_guard lock(s_mutex);
        entry.seq = s_next_seq++;
        s_entries.push_back(std::move(entry));
        while (s_entries.size() > max_entries)
            s_entries.pop_front();
    }

    uint64_t ConsoleBuffer::copy_since(uint64_t last_seq, std::vector<ConsoleEntry>& out) {
        std::lock_guard lock(s_mutex);
        uint64_t newest = last_seq;
        // Entries are ordered by seq, so find the first newer one and copy the tail.
        size_t first = s_entries.size();
        while (first > 0 && s_entries[first - 1].seq > last_seq)
            --first;
        for (size_t i = first; i < s_entries.size(); ++i) {
            out.push_back(s_entries[i]);
            newest = s_entries[i].seq;
        }
        return newest;
    }

    void ConsoleBuffer::clear() {
        std::lock_guard lock(s_mutex);
        s_entries.clear();
    }

} // namespace Honey
