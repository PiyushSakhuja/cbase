#pragma once
#include <fstream>
#include <string>

#include "config.h"

class DiskManager {
public:
    DiskManager(const std::string& filename);

    void write_page(int page_id, const char* data);
    void read_page(int page_id, char* data);

private:
    std::fstream file;
};
