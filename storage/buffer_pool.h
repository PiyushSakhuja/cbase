#pragma once
#include "page.h"
#include "disk_manager.h"
#include "config.h"
#include <cstdint>

// BufferPool: fixed set of in-memory page frames acting as a cache between
// HeapFile and DiskManager.
//
// Behavior:
//   - fetch_page() returns the cached frame on a hit, loads the page from
//     disk on a miss.
//   - When every frame is occupied, one frame is EVICTED (simple
//     round-robin order). If the evicted page is dirty it is written back
//     to disk first. This is deliberately NOT an LRU/clock policy: v1 of
//     this engine keeps replacement as simple as possible while staying
//     correct for databases of any size.
//   - flush_all() writes every dirty page back to disk and clears the
//     dirty flags. Nothing is written to disk outside these two paths.
//
// IMPORTANT invariant for callers: a Page* returned by fetch_page() is only
// guaranteed valid until the NEXT call to fetch_page(), which may evict the
// frame it points into. Use the pointer, then fetch again - do not stash it
// across fetches (HeapFile follows this rule).
class BufferPool{
 public:
    BufferPool(DiskManager * disk_manager);

    // Returns the frame holding `page_id`, loading it from disk if needed.
    // Returns nullptr only if page_id is invalid or the pool cannot load it.
    Page * fetch_page(int page_id);

    // Writes all dirty pages to disk. Returns how many pages were flushed.
    int flush_all();

    DiskManager* get_disk_manager();

    // --- Real, measured counters (used by the demo and tests) ---
    struct Stats {
        uint64_t hits;            // fetch_page served from a cached frame
        uint64_t misses;          // fetch_page had to load from disk
        uint64_t evictions;       // a frame was reclaimed to load a page
        uint64_t dirty_writebacks;// pages written to disk (evict + flush_all)
    };
    Stats stats() const;

    // Frame i (0 <= i < BUFFER_POOL_SIZE) for inspection; the frame is empty
    // when frame_at(i)->page_id == -1.
    const Page* frame_at(int index) const;

    int frame_count() const { return BUFFER_POOL_SIZE; }

    private:
    DiskManager* disk_manager_;
    Page pages[BUFFER_POOL_SIZE];
    int next_victim_;   // round-robin eviction cursor
    Stats stats_;
};
