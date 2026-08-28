// DiskManager unit tests: file lifecycle, page I/O round-trips, invalid
// page ids, persistence across close/reopen, truncated files.

#include "test_framework.h"
#include "../storage/disk_manager.h"
#include <cstdio>
#include <cstring>
#include <fstream>

namespace {
// Fills a page-sized buffer with a recognizable pattern per page id.
void fill_page(char *buf, int page_id) {
    std::memset(buf, 0, PAGE_SIZE);
    std::snprintf(buf, PAGE_SIZE, "page-%d-payload", page_id);
    // make every byte depend on the page id so cross-page reads can't pass
    for (int i = 0; i < PAGE_SIZE; i++) {
        buf[i] = static_cast<char>((buf[i] + page_id * 7 + i / 256) & 0xFF);
    }
    std::snprintf(buf, 32, "page-%d-payload", page_id);  // marker prefix
}
}  // namespace

TEST(disk_manager_creates_missing_file) {
    ctest::TmpFile f{"dm_create"};
    {
        DiskManager dm(f.path);
        CHECK(dm.is_ok());
    }
    // file must exist after construction: verify by reopening
    DiskManager dm2(f.path);
    CHECK(dm2.is_ok());
    CHECK_EQ(dm2.get_page_count(), 0);
}

TEST(disk_manager_write_and_read_round_trip) {
    ctest::TmpFile f{"dm_rw"};
    DiskManager dm(f.path);

    char out[PAGE_SIZE], in[PAGE_SIZE];
    fill_page(out, 0);
    CHECK(dm.write_page(0, out));
    CHECK(dm.read_page(0, in));
    CHECK_EQ(std::memcmp(out, in, PAGE_SIZE), 0);
}

TEST(disk_manager_multiple_pages_round_trip) {
    ctest::TmpFile f{"dm_multi"};
    DiskManager dm(f.path);

    char pages[5][PAGE_SIZE];
    for (int i = 0; i < 5; i++) {
        fill_page(pages[i], i);
        CHECK(dm.write_page(i, pages[i]));
    }
    CHECK_EQ(dm.get_page_count(), 5);

    for (int i = 0; i < 5; i++) {
        char in[PAGE_SIZE];
        CHECK(dm.read_page(i, in));
        CHECK_EQ(std::memcmp(pages[i], in, PAGE_SIZE), 0);
    }
}

TEST(disk_manager_data_survives_close_and_reopen) {
    ctest::TmpFile f{"dm_persist"};
    char out[PAGE_SIZE], in[PAGE_SIZE];
    fill_page(out, 3);

    {
        DiskManager dm(f.path);
        CHECK(dm.write_page(3, out));
        CHECK_EQ(dm.get_page_count(), 4);  // pages 0..3 exist on disk
    }  // dm destroyed -> file closed

    DiskManager dm2(f.path);
    CHECK(dm2.is_ok());
    CHECK_EQ(dm2.get_page_count(), 4);
    CHECK(dm2.read_page(3, in));
    CHECK_EQ(std::memcmp(out, in, PAGE_SIZE), 0);
}

TEST(disk_manager_rejects_negative_page_ids) {
    ctest::TmpFile f{"dm_neg"};
    DiskManager dm(f.path);

    char buf[PAGE_SIZE];
    CHECK_EQ(dm.write_page(-1, buf), false);
    CHECK_EQ(dm.read_page(-1, buf), false);
    // buffer is still fully defined after the failed read
    bool all_zero = true;
    for (int i = 0; i < PAGE_SIZE; i++) {
        if (buf[i] != 0) { all_zero = false; break; }
    }
    CHECK(all_zero);
}

TEST(disk_manager_read_beyond_eof_zero_fills) {
    ctest::TmpFile f{"dm_eof"};
    DiskManager dm(f.path);

    char buf[PAGE_SIZE];
    CHECK_EQ(dm.read_page(7, buf), false);  // no page 7 on disk yet

    bool all_zero = true;
    for (int i = 0; i < PAGE_SIZE; i++) {
        if (buf[i] != 0) { all_zero = false; break; }
    }
    CHECK(all_zero);
}

TEST(disk_manager_truncated_page_zero_fills_tail) {
    // A file with a partial trailing page (e.g. crash during write) must
    // yield the bytes that exist plus zeros, never garbage.
    ctest::TmpFile f{"dm_trunc"};

    {
        std::ofstream out(f.path, std::ios::binary);
        char head[100];
        std::memset(head, 'A', sizeof(head));
        out.write(head, sizeof(head));
    }

    DiskManager dm(f.path);
    CHECK_EQ(dm.get_page_count(), 0);  // partial page not counted

    char buf[PAGE_SIZE];
    CHECK_EQ(dm.read_page(0, buf), false);
    bool head_ok = true;
    for (int i = 0; i < 100; i++) {
        if (buf[i] != 'A') { head_ok = false; break; }
    }
    CHECK(head_ok);
    CHECK_EQ(buf[100], 0);   // missing tail is zero-filled
    CHECK_EQ(buf[PAGE_SIZE - 1], 0);
}

TEST(disk_manager_writing_extends_file) {
    ctest::TmpFile f{"dm_extend"};
    DiskManager dm(f.path);

    char buf[PAGE_SIZE];
    fill_page(buf, 10);
    CHECK(dm.write_page(10, buf));
    CHECK_EQ(dm.get_page_count(), 11);  // pages 0..10
}

RUN_TESTS()
