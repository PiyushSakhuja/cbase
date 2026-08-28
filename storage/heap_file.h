#pragma once

#include "buffer_pool.h"
#include <cstdint>

// Record layout inside a page. Fixed size (8 bytes), stored as raw bytes.
struct Record
{
    int id;
    int age;
};

// RID (Record Identifier): the permanent address of a record inside the
// database file, made of (page_id, slot_id).
//
// RIDs stay valid across logical deletions of OTHER records: a slot's index
// never changes, so a RID computed yesterday still addresses the same slot
// today. A RID whose record was deleted simply stops resolving
// (get_record returns false). RIDs are never reused within a page while
// their slot directory entry still exists; freed slots ARE reused for new
// records, which then get the old RID - this is standard slotted-page
// behavior.
struct RID {
    uint16_t page_id;
    uint16_t slot_id;

    // Sentinel used by insert() to report failure. (0xFFFF, 0xFFFF) can
    // never be produced by a successful insert because page ids and slot
    // counts are bounded well below 0xFFFF.
    static RID invalid() { return RID{0xFFFF, 0xFFFF}; }
    bool is_valid() const { return page_id != 0xFFFF || slot_id != 0xFFFF; }
};

// --- Slotted page on-disk layout (little-endian, see README) ---
//
//   offset 0
//   ┌────────────────────────────────┐
//   │ PageHeader (6 bytes)           │
//   ├────────────────────────────────┤
//   │ Slot 0                         │
//   │ Slot 1        (6 bytes each)   │  slot directory grows downward
//   │ ...                            │
//   ├────────────────────────────────┤  <- get_free_space_start()
//   │        free space              │
//   ├────────────────────────────────┤  <- header.free_space_offset
//   │ Record (8 bytes each)          │  records grow upward from page end
//   │ Record ...                     │
//   └────────────────────────────────┘  offset PAGE_SIZE-1
//
// Invariant kept by insert(): the free-space gap never disappears,
// i.e. free_space_offset >= sizeof(PageHeader) + slot_count*sizeof(Slot)
// always holds for a sane page. When a record no longer fits, the page is
// full and insertion moves to the next page.

struct PageHeader
{
    uint16_t record_count;      // live (non-deleted) records in this page
    uint16_t slot_count;        // slots in the directory (used + free)
    uint16_t free_space_offset; // records occupy [free_space_offset, PAGE_SIZE)
};

struct Slot
{
    uint16_t offset;  // where this record's bytes start
    uint16_t size;    // size of the record in bytes
    uint8_t is_used;  // 1 = slot occupied, 0 = free (logically deleted)
};

class HeapFile
{
public:
    explicit HeapFile(BufferPool *buffer_pool);

    // Appends a record to the current page, moving to a fresh page when the
    // current one is full. Freed slots (from Delete) are reused first.
    // Returns the record's RID, or RID::invalid() when the insert failed
    // (record too large, page id overflow, or unusable page).
    RID insert(const Record &record);

    // READ path: RID -> page -> buffer pool -> record.
    // Returns false when the RID does not address a live record.
    bool get_record(uint16_t page_id, uint16_t slot_id, Record &out);

    // Logical deletion: marks the slot free. Record bytes stay in place
    // until the slot is reused; the RID simply stops resolving.
    // Returns false when nothing was deleted (bad RID / already deleted).
    bool Delete(uint16_t page_id, uint16_t slot_id);

    // Sequential scan of every page. Prints live records and returns the
    // number of live records found.
    int scan_all();

    // Same traversal as scan_all() but silent. Used by the benchmark so
    // timings measure the storage engine, not console printing.
    int count_records();

    // Highest slot id the directory can ever hold: the whole page minus the
    // header, divided by one slot entry.
    static constexpr uint16_t MAX_SLOT_COUNT =
        (PAGE_SIZE - sizeof(PageHeader)) / sizeof(Slot);

private:
    BufferPool *buffer_pool_;

    // Pages 0 .. initialized_pages_-1 are known to have a valid header
    // (either read from disk at startup or initialized in this session).
    // This - not the file size - is what decides whether a page is "new",
    // because dirty pages only reach the file on flush/eviction.
    int initialized_pages_;

    // Page that receives the next insert. Only moves forward.
    int next_page_id_;

    void initialize_page(Page *page);

    PageHeader read_page_header(char *data);
    void write_page_header(char *data, const PageHeader &header);

    Slot read_slot(char *data, uint16_t slot_id);
    void write_slot(char *data, uint16_t slot_id, const Slot &slot);

    uint16_t get_free_space_start(const PageHeader &header);

    // Rejects headers that could not have been produced by this engine
    // (all-zero page, corrupt file, wrong endianness, ...).
    bool header_is_sane(const PageHeader &header);

    // Validates that a slot points to a live in-page record of the expected
    // fixed size before its bytes are copied out.
    bool slot_points_inside_page(const Slot &slot);
};
