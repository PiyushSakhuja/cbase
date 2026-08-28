// Page unit tests.
//
// Page is a raw fixed-size byte container (the slotted-page structure on
// top of these bytes is owned by HeapFile and tested in
// heap_file_test.cpp).

#include "test_framework.h"
#include "../storage/page.h"
#include <cstring>

TEST(page_initializes_empty_and_clean) {
    Page p;
    CHECK_EQ(p.page_id, -1);   // -1 = frame not holding any page
    CHECK_EQ(p.is_dirty(), false);
}

TEST(page_data_starts_zeroed) {
    Page p;
    const char *data = p.get_data();
    bool all_zero = true;
    for (int i = 0; i < PAGE_SIZE; i++) {
        if (data[i] != 0) { all_zero = false; break; }
    }
    CHECK(all_zero);
}

TEST(page_data_is_fully_writable_and_readable) {
    Page p;
    char *data = p.get_data();
    // touch the first and last byte: proves the buffer is exactly PAGE_SIZE
    std::memset(data, 0xAB, PAGE_SIZE);
    CHECK_EQ((unsigned char)data[0], 0xAB);
    CHECK_EQ((unsigned char)data[PAGE_SIZE - 1], 0xAB);
}

TEST(page_dirty_flag_round_trip) {
    Page p;
    p.set_dirty(true);
    CHECK_EQ(p.is_dirty(), true);
    p.set_dirty(false);
    CHECK_EQ(p.is_dirty(), false);
}

TEST(page_reset_clears_content_and_state) {
    Page p;
    char *data = p.get_data();
    std::memset(data, 0x7F, PAGE_SIZE);
    p.set_dirty(true);

    p.reset(5);
    CHECK_EQ(p.page_id, 5);
    CHECK_EQ(p.is_dirty(), false);

    bool all_zero = true;
    for (int i = 0; i < PAGE_SIZE; i++) {
        if (data[i] != 0) { all_zero = false; break; }
    }
    CHECK(all_zero);
}

RUN_TESTS()
