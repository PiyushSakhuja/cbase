#pragma once

#include "config.h"

class Page {
public:
    Page();

    char* get_data();
    void set_dirty(bool dirty);
    bool is_dirty() const;

    int page_id;

private:
    char data[PAGE_SIZE];
    bool dirty;
};
