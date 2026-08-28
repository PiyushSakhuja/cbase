// BufferPool unit tests: hits/misses, dirty tracking, flushing, eviction
// (round-robin with write-back), invalid page ids, consistency.

#include "test_framework.h"
#include "../storage/buffer_pool.h"
#include <cstring>

namespace {
void fill(char *buf, const char *tag) {
    std::memset(buf, 0, PAGE_SIZE);
    std::memcpy(buf, tag, std::strlen(tag));
}
}  // namespace

TEST(buffer_pool_miss_then_hit) {
    ctest::TmpFile f{"bp_hit"};
    DiskManager dm(f.path);
    BufferPool pool(&dm);

    Page *p1 = pool.fetch_page(0);   // miss: loads from disk
    CHECK(p1 != nullptr);
    CHECK_EQ(p1->page_id, 0);

    BufferPool::Stats s1 = pool.stats();
    CHECK_EQ(s1.misses, 1u);

    Page *p2 = pool.fetch_page(0);   // hit: same frame
    CHECK_EQ(p2, p1);
    BufferPool::Stats s2 = pool.stats();
    CHECK_EQ(s2.hits, 1u);
    CHECK_EQ(s2.misses, 1u);
}

TEST(buffer_pool_empty_frames_start_unused) {
    ctest::TmpFile f{"bp_frames"};
    DiskManager dm(f.path);
    BufferPool pool(&dm);

    for (int i = 0; i < pool.frame_count(); i++) {
        CHECK(pool.frame_at(i) != nullptr);
        CHECK_EQ(pool.frame_at(i)->page_id, -1);
    }
    CHECK_EQ(pool.frame_at(pool.frame_count()), nullptr);  // out of range
}

TEST(buffer_pool_dirty_page_flush_persists) {
    ctest::TmpFile f{"bp_flush"};
    {
        DiskManager dm(f.path);
        BufferPool pool(&dm);

        Page *p = pool.fetch_page(0);
        fill(p->get_data(), "hello-dirty-page");
        p->set_dirty(true);

        int flushed = pool.flush_all();
        CHECK_EQ(flushed, 1);
        CHECK_EQ(p->is_dirty(), false);  // clean after flush
    }
    // verify the bytes really reached the file
    DiskManager dm2(f.path);
    char buf[PAGE_SIZE];
    CHECK(dm2.read_page(0, buf));
    CHECK(std::memcmp(buf, "hello-dirty-page", 16) == 0);
}

TEST(buffer_pool_clean_pages_are_not_flushed) {
    ctest::TmpFile f{"bp_clean"};
    DiskManager dm(f.path);
    BufferPool pool(&dm);

    pool.fetch_page(0);            // loaded clean
    CHECK_EQ(pool.flush_all(), 0); // nothing dirty -> nothing written
}

TEST(buffer_pool_cached_page_stays_consistent) {
    ctest::TmpFile f{"bp_consist"};
    DiskManager dm(f.path);
    BufferPool pool(&dm);

    Page *p = pool.fetch_page(0);
    fill(p->get_data(), "consistent");
    p->set_dirty(true);
    pool.flush_all();

    // still cached: second fetch must see the same bytes
    Page *again = pool.fetch_page(0);
    CHECK(std::memcmp(again->get_data(), "consistent", 11) == 0);
}

TEST(buffer_pool_evicts_when_full_and_keeps_data_correct) {
    ctest::TmpFile f{"bp_evict"};
    DiskManager dm(f.path);
    BufferPool pool(&dm);

    // write distinct content for pages 0..3 through the pool
    for (int pid = 0; pid < 4; pid++) {
        Page *p = pool.fetch_page(pid);
        CHECK(p != nullptr);
        char tag[32];
        std::snprintf(tag, sizeof(tag), "content-page-%d", pid);
        fill(p->get_data(), tag);
        p->set_dirty(true);
    }

    BufferPool::Stats s = pool.stats();
    CHECK_EQ(s.evictions, 1u);  // the 4th fetch had to evict one frame

    // every page's content must still be intact (evicted ones come back
    // from disk - dirty victims were written back on eviction)
    for (int pid = 0; pid < 4; pid++) {
        Page *p = pool.fetch_page(pid);
        CHECK(p != nullptr);
        char tag[32];
        std::snprintf(tag, sizeof(tag), "content-page-%d", pid);
        CHECK(std::memcmp(p->get_data(), tag, std::strlen(tag)) == 0);
    }
}

TEST(buffer_pool_dirty_eviction_writes_page_back) {
    ctest::TmpFile f{"bp_dirt_evict"};
    {
        DiskManager dm(f.path);
        BufferPool pool(&dm);

        // dirty page 0, then push 3 more pages through the 3-frame pool
        Page *p0 = pool.fetch_page(0);
        fill(p0->get_data(), "dirty-victim");
        p0->set_dirty(true);

        pool.fetch_page(1);
        pool.fetch_page(2);
        pool.fetch_page(3);   // forces eviction of the dirty page 0

        BufferPool::Stats s = pool.stats();
        CHECK(s.dirty_writebacks >= 1u);
    }
    // page 0's modification must have reached the file even though
    // flush_all() was never called
    DiskManager dm2(f.path);
    char buf[PAGE_SIZE];
    CHECK(dm2.read_page(0, buf));
    CHECK(std::memcmp(buf, "dirty-victim", 12) == 0);
}

TEST(buffer_pool_rejects_invalid_page_id) {
    ctest::TmpFile f{"bp_invalid"};
    DiskManager dm(f.path);
    BufferPool pool(&dm);

    CHECK_EQ(pool.fetch_page(-1), nullptr);
    CHECK_EQ(pool.fetch_page(-42), nullptr);
}

TEST(buffer_pool_flush_all_reports_count) {
    ctest::TmpFile f{"bp_count"};
    DiskManager dm(f.path);
    BufferPool pool(&dm);

    for (int pid = 0; pid < 2; pid++) {
        Page *p = pool.fetch_page(pid);
        p->set_dirty(true);
    }
    CHECK_EQ(pool.flush_all(), 2);
    CHECK_EQ(pool.flush_all(), 0);  // already clean
}

RUN_TESTS()
