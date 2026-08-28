// Integration test: a real database lifecycle, end to end.
//
//   create database
//        |
//   insert several records (multiple pages)
//        |
//   flush
//        |
//   close database
//        |
//   reopen database
//        |
//   scan + verify every record
//        |
//   delete some records, flush, close, reopen again
//        |
//   verify deletions persisted and survivors are intact

#include "test_framework.h"
#include "../storage/heap_file.h"
#include <cstring>
#include <vector>

namespace {
constexpr int RECORDS_PER_PAGE =
    (PAGE_SIZE - (int)sizeof(PageHeader)) /
    (int)(sizeof(Record) + sizeof(Slot));
}

TEST(integration_full_database_lifecycle) {
    ctest::TmpFile f{"integration"};
    const int N = 750;   // ~3 pages of records

    // ---- phase 1: create + insert + flush + close ----
    std::vector<RID> rids;
    rids.reserve(N);
    {
        DiskManager dm(f.path);
        CHECK(dm.is_ok());
        BufferPool pool(&dm);
        HeapFile heap(&pool);

        for (int i = 0; i < N; i++) {
            Record r{i, 20 + (i % 50)};
            RID rid = heap.insert(r);
            CHECK(rid.is_valid());
            rids.push_back(rid);
        }

        // in-session scan sees everything before any flush
        CHECK_EQ(heap.scan_all(), N);

        int flushed = pool.flush_all();
        CHECK(flushed >= 1);
    }

    // ---- phase 2: reopen + scan + verify ----
    {
        DiskManager dm(f.path);
        CHECK(dm.is_ok());
        BufferPool pool(&dm);
        HeapFile heap(&pool);

        int pages = dm.get_page_count();
        CHECK(pages >= 2);   // 750 records cannot fit one page

        CHECK_EQ(heap.scan_all(), N);

        // verify EVERY record byte-for-byte through the RID read path
        int verified = 0;
        for (int i = 0; i < N; i++) {
            Record r;
            if (heap.get_record(rids[i].page_id, rids[i].slot_id, r)) {
                if (r.id == i && r.age == 20 + (i % 50)) {
                    verified++;
                }
            }
        }
        CHECK_EQ(verified, N);
    }

    // ---- phase 3: delete a few, flush, close, reopen, verify ----
    {
        DiskManager dm(f.path);
        BufferPool pool(&dm);
        HeapFile heap(&pool);

        // delete the first record of every page
        for (int page = 0; page < 3; page++) {
            CHECK(heap.Delete(page, 0));
        }
        pool.flush_all();
    }
    {
        DiskManager dm(f.path);
        BufferPool pool(&dm);
        HeapFile heap(&pool);

        CHECK_EQ(heap.scan_all(), N - 3);

        Record r;
        for (int page = 0; page < 3; page++) {
            CHECK_EQ(heap.get_record(page, 0, r), false);  // deletions stuck
        }
        // neighbors untouched
        CHECK(heap.get_record(0, 1, r));
        CHECK_EQ(r.id, 1);
    }

    // ---- phase 4: keep using the reopened database ----
    {
        DiskManager dm(f.path);
        BufferPool pool(&dm);
        HeapFile heap(&pool);

        // the engine appends to the LAST page (page 2); its slot 0 was
        // freed in phase 3, so it gets reused
        RID rid = heap.insert(Record{10001, 60});
        CHECK(rid.is_valid());
        CHECK_EQ(rid.page_id, 2);
        CHECK_EQ(rid.slot_id, 0);

        pool.flush_all();
    }
    {
        DiskManager dm(f.path);
        BufferPool pool(&dm);
        HeapFile heap(&pool);

        CHECK_EQ(heap.scan_all(), N - 3 + 1);
        Record r;
        CHECK(heap.get_record(2, 0, r));
        CHECK_EQ(r.id, 10001);
        CHECK_EQ(r.age, 60);
        // the deletions from phase 3 are still deletions
        CHECK_EQ(heap.get_record(0, 0, r), false);
        CHECK_EQ(heap.get_record(1, 0, r), false);
    }
}

TEST(integration_persistence_is_the_strongest_feature) {
    // A second, independent lifecycle: proves that a database written in
    // one run is fully usable (append + delete + read) in later runs.
    ctest::TmpFile f{"integration2"};

    // run 1: seed
    {
        DiskManager dm(f.path);
        BufferPool pool(&dm);
        HeapFile heap(&pool);
        for (int i = 0; i < 100; i++) {
            heap.insert(Record{i * 2, i});
        }
        pool.flush_all();
    }
    // run 2: append
    {
        DiskManager dm(f.path);
        BufferPool pool(&dm);
        HeapFile heap(&pool);
        RID rid = heap.insert(Record{12345, 99});
        CHECK(rid.is_valid());
        CHECK_EQ(rid.page_id, 0);
        CHECK_EQ(rid.slot_id, 100);
        pool.flush_all();
    }
    // run 3: verify + delete
    {
        DiskManager dm(f.path);
        BufferPool pool(&dm);
        HeapFile heap(&pool);
        CHECK_EQ(heap.scan_all(), 101);
        CHECK(heap.Delete(0, 100));
        pool.flush_all();
    }
    // run 4: final state
    {
        DiskManager dm(f.path);
        BufferPool pool(&dm);
        HeapFile heap(&pool);
        CHECK_EQ(heap.scan_all(), 100);
        Record r;
        CHECK(heap.get_record(0, 50, r));
        CHECK_EQ(r.id, 100);
    }
}

RUN_TESTS()
