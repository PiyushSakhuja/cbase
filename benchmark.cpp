// CBase benchmark: measures real insert / scan / reopen-read throughput.
//
// Only values produced by actual execution are printed. Durations are shown
// with an adaptive unit: milliseconds at >= 1 ms, microseconds below that
// (per-record rates fall back to nanoseconds), so fast phases never round
// away to a useless "0.0 ms". Usage:
//
//   ./build/cbase_bench [num_records] [db_file]
//
// Defaults: 10,000 records into cbase_bench.dat (removed at start, left
// behind afterwards so the file can be inspected).
//
// Build: make bench

#include "storage/disk_manager.h"
#include "storage/buffer_pool.h"
#include "storage/heap_file.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using Clock = std::chrono::steady_clock;

static double ms_since(Clock::time_point start)
{
    auto end = Clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

// Duration given in milliseconds: shown as "17.7 ms" at >= 1 ms and as
// "184.2 us" below 1 ms. One decimal of real precision in both units.
static std::string format_ms(double ms)
{
    char buf[48];
    if (ms >= 1.0)
    {
        std::snprintf(buf, sizeof(buf), "%.1f ms", ms);
    }
    else
    {
        std::snprintf(buf, sizeof(buf), "%.1f us", ms * 1000.0);
    }
    return std::string(buf);
}

// Per-record rate given in microseconds: switches to nanoseconds when the
// value would otherwise print as "0.00 us/record".
static std::string format_per_record(double us)
{
    char buf[48];
    if (us >= 0.01)
    {
        std::snprintf(buf, sizeof(buf), "%.2f us/record", us);
    }
    else
    {
        std::snprintf(buf, sizeof(buf), "%.1f ns/record", us * 1000.0);
    }
    return std::string(buf);
}

int main(int argc, char **argv)
{
    int n = 10000;
    const char *path = "cbase_bench.dat";

    if (argc > 1)
    {
        n = std::atoi(argv[1]);
        if (n <= 0)
        {
            std::fprintf(stderr, "record count must be positive\n");
            return 1;
        }
    }
    if (argc > 2)
    {
        path = argv[2];
    }

    std::remove(path);

    std::printf("CBase benchmark\n");
    std::printf("records:      %d\n", n);
    std::printf("page size:    %d bytes\n", PAGE_SIZE);
    std::printf("pool frames:  %d\n\n", BUFFER_POOL_SIZE);

    std::vector<RID> rids;
    rids.reserve(n);

    // ---------------- Phase 1: insert ----------------
    double insert_ms;
    int pages_used;
    {
        DiskManager dm(path);
        BufferPool pool(&dm);
        HeapFile heap(&pool);

        auto t0 = Clock::now();
        for (int i = 0; i < n; i++)
        {
            Record r{i, 20 + (i % 50)};
            RID rid = heap.insert(r);
            if (!rid.is_valid())
            {
                std::fprintf(stderr, "insert failed at record %d\n", i);
                return 1;
            }
            rids.push_back(rid);
        }
        insert_ms = ms_since(t0);

        int flushed = pool.flush_all(); // simulate a clean shutdown
        pages_used = dm.get_page_count();

        BufferPool::Stats s = pool.stats();
        std::printf("insert:       %s  (%s, %d page(s) flushed, "
                    "%llu eviction(s) during load)\n",
                    format_ms(insert_ms).c_str(),
                    format_per_record(insert_ms * 1000.0 / n).c_str(), flushed,
                    (unsigned long long)s.evictions);
    }

    // ---------------- Phase 2: reopen + sequential scan ----------------
    double reopen_scan_ms;
    int scanned;
    {
        DiskManager dm(path);
        BufferPool pool(&dm);
        HeapFile heap(&pool);

        auto t1 = Clock::now();
        scanned = heap.count_records();   // silent scan: measures the engine
        reopen_scan_ms = ms_since(t1);

        if (scanned != n)
        {
            std::fprintf(stderr, "scan found %d records, expected %d\n",
                         scanned, n);
            return 1;
        }
        std::printf("scan:         %s  (%s, %d record(s) "
                    "recovered after reopen)\n",
                    format_ms(reopen_scan_ms).c_str(),
                    format_per_record(reopen_scan_ms * 1000.0 / n).c_str(),
                    scanned);
    }

    // ---------------- Phase 3: reopen + random point reads ----------------
    double point_read_ms;
    int verified;
    {
        DiskManager dm(path);
        BufferPool pool(&dm);
        HeapFile heap(&pool);

        // deterministic pseudo-random indices (no <random> needed)
        unsigned seed = 12345;
        int reads = n < 1000 ? n : 1000;

        auto t2 = Clock::now();
        verified = 0;
        for (int i = 0; i < reads; i++)
        {
            seed = seed * 1103515245u + 12345u;
            int idx = (seed >> 16) % rids.size();
            Record got;
            if (heap.get_record(rids[idx].page_id, rids[idx].slot_id, got) &&
                got.id == idx)
            {
                verified++;
            }
        }
        point_read_ms = ms_since(t2);

        BufferPool::Stats s = pool.stats();
        std::printf("point reads:  %s  (%d/%d RID lookups verified, "
                    "%llu hit(s) / %llu miss(es))\n",
                    format_ms(point_read_ms).c_str(), verified, reads,
                    (unsigned long long)s.hits, (unsigned long long)s.misses);
    }

    // ---------------- Summary ----------------
    std::printf("\npages used:   %d  (~%d record(s)/page)\n",
                pages_used, pages_used > 0 ? n / pages_used : 0);
    std::printf("total time:   %s (insert + scan + point reads)\n",
                format_ms(insert_ms + reopen_scan_ms + point_read_ms).c_str());
    std::printf("database:     %s\n", path);

    if (scanned != n || verified != (n < 1000 ? n : 1000))
    {
        std::fprintf(stderr, "BENCHMARK FAILED A CORRECTNESS CHECK\n");
        return 1;
    }
    return 0;
}
