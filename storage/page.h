#pragma once

#include "config.h"

// Page: one fixed-size block of raw bytes.
//
// A Page is intentionally a dumb byte container. All structure inside the
// bytes (header, slot directory, records) is interpreted by HeapFile, which
// is the only layer that understands the slotted-page layout.
class Page {
public:
    Page();

    // Raw access to the page contents. Callers must not read/write outside
    // [0, PAGE_SIZE) - HeapFile is responsible for enforcing that.
    char* get_data();
    void set_dirty(bool dirty);

    // True if the page has been modified since the last write-back to disk.
    // Dirty pages are written to disk on flush_all() or when their frame is
    // evicted from the buffer pool.
    bool is_dirty() const;

    // Clears the page contents, assigns a new page id and clears the dirty
    // flag. Used by the buffer pool when a frame is (re)loaded.
    void reset(int new_page_id);

    // -1 means the frame is empty (no page loaded).
    int page_id;

private:
    char data[PAGE_SIZE];
    bool dirty;
};
