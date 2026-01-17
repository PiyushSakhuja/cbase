#include "storage/page.h"
#include "storage/disk_manager.h"
#include "storage/buffer_pool.h"
#include "storage/heap_file.h"
#include <iostream>
#include <cstring>

int main()
{
    DiskManager dm("mydb.db");
    BufferPool bp(&dm);
    HeapFile hf(&bp);

    hf.insert({1, 20});
    hf.insert({2, 25});
    hf.insert({3, 30});
    
    hf.scan_all();
    hf.Delete(3);
    hf.scan_all();

    bp.flush_all();
}
