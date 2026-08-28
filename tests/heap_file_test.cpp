// HeapFile unit tests: the full record lifecycle (insert / read / delete /
// scan), slotted-page behavior (slot reuse, full pages, multi-page), RID
// validation, corrupt-page resilience, and persistence across reopen.

#include "test_framework.h"
#include "../storage/heap_file.h"
#include <cstring>

namespace {
// Records per single page: (PAGE_SIZE - header) / (record + slot entry).
constexpr int RECORDS_PER_PAGE =
    (PAGE_SIZE - (int)sizeof(PageHeader)) /
    (int)(sizeof(Record) + sizeof(Slot));

Record make_record(int i) {
    return Record{i, 20 + (i % 50)};
}
}  // namespace

TEST(heap_file_insert_returns_sequential_rids) {
    ctest::TmpFile f{"hf_rids"};
    DiskManager dm(f.path);
    BufferPool pool(&dm);
    HeapFile heap(&pool);

    for (int i = 0; i < 5; i++) {
        RID rid = heap.insert(make_record(i));
        CHECK(rid.is_valid());
        CHECK_EQ(rid.page_id, 0);
        CHECK_EQ(rid.slot_id, i);   // slots 0,1,2,3,4 in order
    }
}

TEST(heap_file_insert_multiple_records_in_single_session_no_overwrite) {
    // Regression test for the v1 bug where every insert re-initialized the
    // current page and silently overwrote the previous record.
    ctest::TmpFile f{"hf_overwrite"};
    DiskManager dm(f.path);
    BufferPool pool(&dm);
    HeapFile heap(&pool);

    for (int i = 0; i < 5; i++) {
        heap.insert(make_record(i));
    }
    // all five must be live, with distinct ids
    int found = 0;
    for (int slot = 0; slot < 5; slot++) {
        Record r;
        if (heap.get_record(0, slot, r)) {
            CHECK_EQ(r.id, slot);
            found++;
        }
    }
    CHECK_EQ(found, 5);
}

TEST(heap_file_get_record_returns_inserted_data) {
    ctest::TmpFile f{"hf_read"};
    DiskManager dm(f.path);
    BufferPool pool(&dm);
    HeapFile heap(&pool);

    Record in{123, 45};
    RID rid = heap.insert(in);

    Record out{0, 0};
    CHECK(heap.get_record(rid.page_id, rid.slot_id, out));
    CHECK_EQ(out.id, in.id);
    CHECK_EQ(out.age, in.age);
}

TEST(heap_file_delete_is_logical_and_reversible_slot) {
    ctest::TmpFile f{"hf_delete"};
    DiskManager dm(f.path);
    BufferPool pool(&dm);
    HeapFile heap(&pool);

    for (int i = 0; i < 3; i++) {
        heap.insert(make_record(i));
    }

    // delete the middle record
    CHECK(heap.Delete(0, 1));
    Record r;
    CHECK_EQ(heap.get_record(0, 1, r), false);      // gone
    CHECK(heap.get_record(0, 0, r));                // neighbors intact
    CHECK(heap.get_record(0, 2, r));

    // deleting the same slot again reports "nothing deleted"
    CHECK_EQ(heap.Delete(0, 1), false);

    // the freed slot is reused by the next insert (same RID comes back)
    RID reused = heap.insert(make_record(99));
    CHECK_EQ(reused.page_id, 0);
    CHECK_EQ(reused.slot_id, 1);
    CHECK(heap.get_record(0, 1, r));
    CHECK_EQ(r.id, 99);
}

TEST(heap_file_page_fills_exactly_and_next_page_starts) {
    ctest::TmpFile f{"hf_full"};
    DiskManager dm(f.path);
    BufferPool pool(&dm);
    HeapFile heap(&pool);

    // fill page 0 completely: RECORDS_PER_PAGE inserts must land on page 0
    for (int i = 0; i < RECORDS_PER_PAGE; i++) {
        RID rid = heap.insert(make_record(i));
        CHECK_EQ(rid.page_id, 0);
    }
    // the next insert must NOT fit and must move to page 1
    RID rid = heap.insert(make_record(RECORDS_PER_PAGE));
    CHECK(rid.is_valid());
    CHECK_EQ(rid.page_id, 1);
    CHECK_EQ(rid.slot_id, 0);

    // a full page must not be corrupted: its records are still readable
    Record r;
    CHECK(heap.get_record(0, 0, r));
    CHECK_EQ(r.id, 0);
    CHECK(heap.get_record(0, RECORDS_PER_PAGE - 1, r));
    CHECK_EQ(r.id, RECORDS_PER_PAGE - 1);
}

TEST(heap_file_records_per_page_matches_layout_math) {
    // cross-check: the empirical capacity equals the layout-derived number
    // (4096 - 6) / (8 + 6) = 292 records per page with current sizes
    CHECK_EQ(RECORDS_PER_PAGE, 292);
    CHECK_EQ((int)HeapFile::MAX_SLOT_COUNT, 681);
}

TEST(heap_file_multiple_pages_and_scan) {
    ctest::TmpFile f{"hf_multi"};
    DiskManager dm(f.path);
    BufferPool pool(&dm);
    HeapFile heap(&pool);

    // enough records to span 4+ pages (more than the 3 pool frames:
    // exercises eviction during both insert and scan)
    const int N = 4 * RECORDS_PER_PAGE + 10;

    for (int i = 0; i < N; i++) {
        RID rid = heap.insert(make_record(i));
        CHECK(rid.is_valid());
    }

    // sequential scan must see every record exactly once
    CHECK_EQ(heap.scan_all(), N);

    // after an explicit flush, every page must exist on disk
    pool.flush_all();
    CHECK(dm.get_page_count() >= 4);

    // spot-check data integrity at page boundaries
    Record r;
    CHECK(heap.get_record(0, RECORDS_PER_PAGE - 1, r));
    CHECK_EQ(r.id, RECORDS_PER_PAGE - 1);
    CHECK(heap.get_record(1, 0, r));
    CHECK_EQ(r.id, RECORDS_PER_PAGE);
}

TEST(heap_file_scan_empty_database) {
    ctest::TmpFile f{"hf_empty"};
    DiskManager dm(f.path);
    BufferPool pool(&dm);
    HeapFile heap(&pool);

    CHECK_EQ(heap.scan_all(), 0);
}

TEST(heap_file_invalid_rids_are_rejected) {
    ctest::TmpFile f{"hf_badrid"};
    DiskManager dm(f.path);
    BufferPool pool(&dm);
    HeapFile heap(&pool);

    heap.insert(make_record(1));

    Record r;
    CHECK_EQ(heap.get_record(999, 999, r), false);   // page does not exist
    CHECK_EQ(heap.get_record(0, 999, r), false);     // slot does not exist
    CHECK_EQ(heap.get_record(999, 0, r), false);
    CHECK_EQ(heap.Delete(999, 999), false);          // delete must not crash
    CHECK_EQ(heap.Delete(0, 999), false);
    // the real record is untouched by the failed operations
    CHECK(heap.get_record(0, 0, r));
    CHECK_EQ(r.id, 1);
}

TEST(heap_file_reopen_retrieves_persisted_records) {
    ctest::TmpFile f{"hf_reopen"};
    const int N = 700;   // spans 3 pages

    {
        DiskManager dm(f.path);
        BufferPool pool(&dm);
        HeapFile heap(&pool);
        for (int i = 0; i < N; i++) {
            heap.insert(make_record(i));
        }
        CHECK(pool.flush_all() >= 1);
    }

    DiskManager dm2(f.path);
    BufferPool pool2(&dm2);
    HeapFile heap2(&pool2);

    CHECK_EQ(dm2.get_page_count(), 3);
    CHECK_EQ(heap2.scan_all(), N);

    // every RID still resolves to its original bytes
    for (int i = 0; i < N; i++) {
        Record r;
        CHECK(heap2.get_record(i / RECORDS_PER_PAGE,
                               i % RECORDS_PER_PAGE, r));
        CHECK_EQ(r.id, i);
        CHECK_EQ(r.age, 20 + (i % 50));
    }
}

TEST(heap_file_insert_after_reopen_appends) {
    ctest::TmpFile f{"hf_append"};
    {
        DiskManager dm(f.path);
        BufferPool pool(&dm);
        HeapFile heap(&pool);
        for (int i = 0; i < 10; i++) {
            heap.insert(make_record(i));
        }
        pool.flush_all();
    }
    {
        DiskManager dm(f.path);
        BufferPool pool(&dm);
        HeapFile heap(&pool);
        RID rid = heap.insert(make_record(10));
        CHECK(rid.is_valid());
        CHECK_EQ(rid.page_id, 0);       // still appending to the last page
        CHECK_EQ(rid.slot_id, 10);
        CHECK_EQ(heap.scan_all(), 11);  // 10 old + 1 new
        pool.flush_all();
    }
    {
        DiskManager dm(f.path);
        BufferPool pool(&dm);
        HeapFile heap(&pool);
        CHECK_EQ(heap.scan_all(), 11);  // survives another restart
    }
}

TEST(heap_file_deletions_persist_across_reopen) {
    ctest::TmpFile f{"hf_del_persist"};
    {
        DiskManager dm(f.path);
        BufferPool pool(&dm);
        HeapFile heap(&pool);
        for (int i = 0; i < 10; i++) {
            heap.insert(make_record(i));
        }
        CHECK(heap.Delete(0, 2));
        CHECK(heap.Delete(0, 5));
        pool.flush_all();
    }
    DiskManager dm2(f.path);
    BufferPool pool2(&dm2);
    HeapFile heap2(&pool2);

    CHECK_EQ(heap2.scan_all(), 8);
    Record r;
    CHECK_EQ(heap2.get_record(0, 2, r), false);
    CHECK_EQ(heap2.get_record(0, 5, r), false);
    CHECK(heap2.get_record(0, 0, r));
    CHECK_EQ(r.id, 0);
}

TEST(heap_file_corrupt_page_does_not_crash) {
    // A page full of garbage (e.g. someone overwrote the file) must be
    // skipped with an error, not crash the engine or memcpy out of bounds.
    ctest::TmpFile f{"hf_corrupt"};
    {
        DiskManager dm(f.path);
        BufferPool pool(&dm);
        HeapFile heap(&pool);
        for (int i = 0; i < 5; i++) {
            heap.insert(make_record(i));
        }
        pool.flush_all();
    }
    {
        // corrupt page 0 behind the engine's back
        DiskManager dm(f.path);
        char garbage[PAGE_SIZE];
        std::memset(garbage, 0xFF, PAGE_SIZE);
        // header claims: 60000 slots, free_space_offset = 1 -> all insane
        dm.write_page(0, garbage);
    }
    DiskManager dm2(f.path);
    BufferPool pool2(&dm2);
    HeapFile heap2(&pool2);

    CHECK_EQ(heap2.scan_all(), 0);          // page skipped, no crash
    Record r;
    CHECK_EQ(heap2.get_record(0, 0, r), false);
    CHECK_EQ(heap2.Delete(0, 0), false);    // rejected, no crash
}

TEST(heap_file_free_space_calculation_survives_deletes) {
    // Deleting records frees slots (reusable) but record bytes are never
    // reclaimed: a page full of deleted records still refuses new records
    // once its byte space is exhausted. Verify the accounting stays exact.
    ctest::TmpFile f{"hf_freespace"};
    DiskManager dm(f.path);
    BufferPool pool(&dm);
    HeapFile heap(&pool);

    // fill page 0 completely, then delete everything in it
    for (int i = 0; i < RECORDS_PER_PAGE; i++) {
        heap.insert(make_record(i));
    }
    for (int slot = 0; slot < RECORDS_PER_PAGE; slot++) {
        CHECK(heap.Delete(0, slot));
    }
    CHECK_EQ(heap.scan_all(), 0);   // page is empty but its space is used up

    // inserting must go to a fresh page, not corrupt the exhausted one
    RID rid = heap.insert(make_record(1));
    CHECK_EQ(rid.page_id, 1);
    CHECK_EQ(heap.scan_all(), 1);
}

RUN_TESTS()
