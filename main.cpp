#include "storage/disk_manager.h"
#include "storage/buffer_pool.h"
#include "storage/heap_file.h"
#include <iostream>
#include <limits>

// Reads one integer from stdin, recovering from non-numeric input instead
// of spinning forever on a failed stream (the classic `cin >> int` trap).
static bool read_int(const char *prompt, int &out)
{
    std::cout << prompt;
    if (!(std::cin >> out))
    {
        std::cin.clear();
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        return false;
    }
    return true;
}

int main() {
    DiskManager dm("mydb.dat");

    if (!dm.is_ok())
    {
        std::cerr << "Error: could not open database file 'mydb.dat'.\n";
        return 1;
    }

    BufferPool bp(&dm);
    HeapFile hf(&bp);

    int choice;

    while (true) {
        std::cout << "\n1. Insert\n2. Delete\n3. Scan\n4. Exit\nChoice: ";

        if (!read_int("", choice)) {
            std::cout << "Invalid input, please enter 1-4.\n";
            continue;
        }

        if (choice == 1) {
            Record r;
            if (!read_int("Enter ID: ", r.id) ||
                !read_int("Enter Age: ", r.age))
            {
                std::cout << "Invalid input, insert cancelled.\n";
                continue;
            }

            RID rid = hf.insert(r);
            if (rid.is_valid())
            {
                std::cout << "Inserted record (ID: " << r.id
                          << ", Age: " << r.age << ") at Page "
                          << rid.page_id
                          << " Slot "
                          << rid.slot_id
                          << std::endl;
            }
            else
            {
                std::cout << "Insert failed (see error above).\n";
            }
        }
        else if (choice == 2) {
            int pid, sid;
            if (!read_int("Enter Page ID: ", pid) ||
                !read_int("Enter Slot ID: ", sid) ||
                pid < 0 || pid > 0xFFFF || sid < 0 || sid > 0xFFFF)
            {
                std::cout << "Invalid input, delete cancelled.\n";
                continue;
            }

            if (hf.Delete(static_cast<uint16_t>(pid),
                          static_cast<uint16_t>(sid)))
            {
                std::cout << "Deleted record at Page " << pid
                          << " Slot " << sid << ".\n";
            }
            else
            {
                std::cout << "No live record at Page " << pid
                          << " Slot " << sid
                          << " (never existed or already deleted).\n";
            }
        }
        else if (choice == 3) {
            std::cout << "Records:\n";
            int count = hf.scan_all();
            if (count == 0)
            {
                std::cout << "  (database is empty)\n";
            }
            std::cout << "Total: " << count << " record(s)\n";
        }
        else if (choice == 4) {
            int flushed = bp.flush_all();
            std::cout << "Flushed " << flushed
                      << " dirty page(s) to disk. Goodbye.\n";
            break;
        }
        else {
            std::cout << "Unknown choice '" << choice
                      << "', please enter 1-4.\n";
        }
    }

    return 0;
}
