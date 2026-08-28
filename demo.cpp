// CBase demonstration: a guided, REAL walkthrough of the storage engine.
//
// Every number printed below comes from actual execution - buffer pool
// counters are read from the pool, flushed page counts from flush_all(),
// and "recovered" records are compared byte-for-byte with what was
// inserted. Nothing here is decorative output.
//
// Build: make demo    Run: ./build/cbase_demo

#include "storage/disk_manager.h"
#include "storage/buffer_pool.h"
#include "storage/heap_file.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static const char *DEMO_FILE = "cbase_demo.dat";

static void print_pool_state(const BufferPool &pool)
{
    std::printf("BUFFER POOL (%d frames)\n", pool.frame_count());
    for (int i = 0; i < pool.frame_count(); i++)
    {
        const Page *p = pool.frame_at(i);
        if (p->page_id == -1)
        {
            std::printf("  frame %d -> empty\n", i);
        }
        else
        {
            std::printf("  frame %d -> page %d, dirty = %s\n",
                        i, p->page_id, p->is_dirty() ? "true" : "false");
        }
    }
    BufferPool::Stats s = pool.stats();
    std::printf("  stats so far: %llu hit(s), %llu miss(es), "
                "%llu eviction(s), %llu page writeback(s)\n",
                (unsigned long long)s.hits, (unsigned long long)s.misses,
                (unsigned long long)s.evictions,
                (unsigned long long)s.dirty_writebacks);
}

int main()
{
    // Start from a clean slate so the demo is reproducible.
    std::remove(DEMO_FILE);

    // --------------------------------------------------------------
    std::printf("=== CBase storage engine demo ===\n\n");
    std::printf("[1] Creating database '%s' and inserting records\n",
                DEMO_FILE);

    std::vector<Record> inserted;
    std::vector<RID> rids;
    {
        DiskManager dm(DEMO_FILE);
        BufferPool pool(&dm);
        HeapFile heap(&pool);

        const int people[][2] = {
            {1, 29}, {2, 34}, {3, 41}, {4, 25}, {5, 38}, {6, 30}};

        for (auto &p : people)
        {
            Record r{p[0], p[1]};
            RID rid = heap.insert(r);
            if (!rid.is_valid())
            {
                std::printf("  insert of record %d FAILED\n", r.id);
                return 1;
            }
            inserted.push_back(r);
            rids.push_back(rid);
            std::printf("  INSERT record (ID: %d, Age: %d)  ->  RID (page=%u, slot=%u)\n",
                        r.id, r.age, rid.page_id, rid.slot_id);
        }

        std::printf("\n[2] In-memory state (records are NOT on disk yet)\n");
        print_pool_state(pool);

        std::printf("\n[3] SCAN (served from the buffer pool, no disk read)\n");
        int scanned = heap.scan_all();
        std::printf("  scan found %d live record(s)\n", scanned);

        // Deleting one record to show logical deletion + slot reuse.
        RID victim = rids[1]; // record (ID: 2)
        std::printf("\n[4] DELETE RID (page=%u, slot=%u)  (logical: slot marked free)\n",
                    victim.page_id, victim.slot_id);
        bool deleted = heap.Delete(victim.page_id, victim.slot_id);
        std::printf("  delete %s\n", deleted ? "ok" : "FAILED");

        Record should_be_gone;
        std::printf("  read RID (page=%u, slot=%u) after delete: %s\n",
                    victim.page_id, victim.slot_id,
                    heap.get_record(victim.page_id, victim.slot_id,
                                    should_be_gone)
                        ? "STILL VISIBLE (bug!)"
                        : "not found (correct)");

        int flushed = pool.flush_all();
        std::printf("\n[5] FLUSH: %d dirty page(s) written to '%s'\n",
                    flushed, DEMO_FILE);
        print_pool_state(pool);
    }
    // scope ends: DiskManager, BufferPool, HeapFile destroyed = "close"

    // --------------------------------------------------------------
    std::printf("\n[6] REOPENING the database from scratch (new objects, cold cache)\n");
    int recovered = 0;
    {
        DiskManager dm(DEMO_FILE);
        BufferPool pool(&dm);
        HeapFile heap(&pool);

        int pages = dm.get_page_count();
        std::printf("  database file contains %d page(s)\n", pages);

        // READ path: fetch every inserted record by its RID and compare
        // byte-for-byte with what we inserted.
        for (size_t i = 0; i < inserted.size(); i++)
        {
            Record got;
            bool is_deleted = (rids[i].page_id == rids[1].page_id &&
                               rids[i].slot_id == rids[1].slot_id);
            if (heap.get_record(rids[i].page_id, rids[i].slot_id, got))
            {
                bool match = (got.id == inserted[i].id &&
                              got.age == inserted[i].age);
                std::printf("  RID (page=%u, slot=%u) -> ID: %d Age: %d  %s\n",
                            rids[i].page_id, rids[i].slot_id, got.id, got.age,
                            match ? "(matches insert)" : "(MISMATCH!)");
                recovered += match ? 1 : 0;
            }
            else
            {
                std::printf("  RID (page=%u, slot=%u) -> gone  %s\n",
                            rids[i].page_id, rids[i].slot_id,
                            is_deleted ? "(expected: was deleted)" : "(UNEXPECTED!)");
            }
        }

        BufferPool::Stats s = pool.stats();
        std::printf("  reads needed %llu cache miss(es) to repopulate a cold pool\n",
                    (unsigned long long)s.misses);

        std::printf("\n[7] SCAN of the reopened database\n");
        int live = heap.scan_all();
        std::printf("  scan found %d live record(s), %d inserted record(s) verified byte-for-byte\n",
                    live, recovered);
    }

    std::printf("\nDemo finished.\n");
    return 0;
}
