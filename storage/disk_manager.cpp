#include "disk_manager.h"
#include <cstring>

DiskManager::DiskManager(const std::string &filename)
{
    file.open(filename, std::ios::in | std::ios::out | std::ios::binary);

    if (!file)
    {
        std::ofstream temp(filename, std::ios::binary);
        temp.close();
        file.open(filename, std::ios::in | std::ios::out | std::ios::binary);
    }
}
void DiskManager::write_page(int page_id, const char *data)
{
    file.seekp(page_id * PAGE_SIZE);
    file.write(data,PAGE_SIZE);
    file.flush();
}
void DiskManager::read_page(int page_id, char *data)
{
    file.seekg(page_id * PAGE_SIZE);

    if (!file.read(data, PAGE_SIZE)) {
        std::memset(data, 0, PAGE_SIZE);
        file.clear();
    }
}
