#include "storage/disk_manager.h"
#include "storage/buffer_pool.h"
#include "storage/heap_file.h"
#include <iostream>

int main() {
    DiskManager dm("mydb.dat");
    BufferPool bp(&dm);
    HeapFile hf(&bp);

    int choice;

    while (true) {
        std::cout << "\n1. Insert\n2. Delete\n3. Scan\n4. Exit\nChoice: ";
        std::cin >> choice;

        if (choice == 1) {
            Record r;
            std::cout << "Enter ID: ";
            std::cin >> r.id;
            std::cout << "Enter Age: ";
            std::cin >> r.age;

            RID rid = hf.insert(r);
            std::cout << "Inserted at Page "
                      << rid.page_id
                      << " Slot "
                      << rid.slot_id
                      << std::endl;
        }
        else if (choice == 2) {
            uint16_t pid, sid;
            std::cout << "Enter Page ID: ";
            std::cin >> pid;
            std::cout << "Enter Slot ID: ";
            std::cin >> sid;

            hf.Delete(pid, sid);
            std::cout << "Deleted.\n";
        }
        else if (choice == 3) {
            hf.scan_all();
        }
        else if (choice == 4) {
            bp.flush_all();
            break;
        }
    }

    return 0;
}
