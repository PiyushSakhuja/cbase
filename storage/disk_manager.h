#pragma once
#include <fstream>
#include <string>

#include "config.h"

// DiskManager: lowest layer of the engine. Maps page ids to file offsets
// using `offset = page_id * PAGE_SIZE` and transfers exactly PAGE_SIZE bytes
// per operation. It knows nothing about page contents (headers, slots,
// records) - that is HeapFile's job.
//
// Error handling: I/O failures are reported on stderr and via a false return
// value; they are never silently ignored. A DiskManager that failed to open
// its file is "not ok" (see is_ok()); all operations on it fail safely.
class DiskManager {
public:
    DiskManager(const std::string& filename);

    // False if the database file could not be opened or created.
    bool is_ok() const;

    // Writes one full page at `page_id * PAGE_SIZE`. Extends the file if the
    // page is beyond the current end. Returns false on I/O failure.
    bool write_page(int page_id, const char* data);

    // Reads one full page. If the page is past EOF or the file is truncated,
    // the missing bytes are zero-filled and false is returned (the buffer is
    // still fully defined). Returns false on any I/O failure.
    bool read_page(int page_id, char* data);

    // Number of complete PAGE_SIZE pages currently in the file.
    // A partial trailing page is not counted. Returns 0 if the file is
    // missing/empty or could not be opened.
    int get_page_count();

private:
    std::fstream file;
    bool ok;
};
