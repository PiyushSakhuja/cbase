#include "disk_manager.h"
#include <cstring>
#include <iostream>

DiskManager::DiskManager(const std::string &filename)
    : ok(false)
{
    // Open existing file for read+write. If it does not exist yet, create it
    // first, then reopen in read+write mode.
    file.open(filename, std::ios::in | std::ios::out | std::ios::binary);

    if (!file)
    {
        std::ofstream temp(filename, std::ios::binary);
        temp.close();
        file.open(filename, std::ios::in | std::ios::out | std::ios::binary);
    }

    if (!file)
    {
        std::cerr << "DiskManager: cannot open or create database file '"
                  << filename << "'\n";
        return;
    }

    ok = true;
}

bool DiskManager::is_ok() const
{
    return ok;
}

bool DiskManager::write_page(int page_id, const char *data)
{
    if (!ok || page_id < 0)
    {
        std::cerr << "DiskManager: write_page rejected (file open: "
                  << std::boolalpha << ok << ", page_id: " << page_id << ")\n";
        return false;
    }

    // streamoff is 64-bit: avoids the int overflow that page_id * PAGE_SIZE
    // would have for files larger than ~2 GB.
    file.seekp(static_cast<std::streamoff>(page_id) * PAGE_SIZE);
    file.write(data, PAGE_SIZE);
    file.flush();

    if (!file)
    {
        std::cerr << "DiskManager: failed to write page " << page_id << "\n";
        file.clear();
        return false;
    }
    return true;
}

bool DiskManager::read_page(int page_id, char *data)
{
    if (!ok || page_id < 0)
    {
        std::cerr << "DiskManager: read_page rejected (file open: "
                  << std::boolalpha << ok << ", page_id: " << page_id << ")\n";
        std::memset(data, 0, PAGE_SIZE);
        return false;
    }

    file.seekg(static_cast<std::streamoff>(page_id) * PAGE_SIZE);

    if (!file)
    {
        std::cerr << "DiskManager: seek failed while reading page "
                  << page_id << "\n";
        file.clear();
        std::memset(data, 0, PAGE_SIZE);
        return false;
    }

    file.read(data, PAGE_SIZE);
    std::streamsize got = file.gcount();

    if (got < PAGE_SIZE)
    {
        // Past EOF or truncated tail page: zero only the missing tail so the
        // buffer is always fully defined, then recover the stream state.
        std::memset(data + got, 0, PAGE_SIZE - got);
        file.clear();
        return false;
    }
    return true;
}

int DiskManager::get_page_count() {
    if (!ok)
    {
        return 0;
    }

    file.clear();
    file.seekg(0, std::ios::end);
    std::streampos size = file.tellg();

    if (size < 0)
    {
        std::cerr << "DiskManager: could not determine database file size\n";
        file.clear();
        return 0;
    }

    return static_cast<int>(size / PAGE_SIZE);
}
