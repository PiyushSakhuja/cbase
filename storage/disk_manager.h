#pragma once
#include <fstream>
#include <string>

#include "config.h"

class DiskManager {
public:
    DiskManager(const std::string& filename);

    void write_page(int page_id, const char* data);
    void read_page(int page_id, char* data);
    int get_page_count();

private:
    std::fstream file;
};
