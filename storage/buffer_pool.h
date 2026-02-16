#pragma once
#include "page.h"
#include "disk_manager.h"
#include<iostream>
#include "config.h"

class BufferPool{
 public:
    BufferPool(DiskManager * disk_manager);
    Page * fetch_page(int page_id);
    void flush_all();
    DiskManager* get_disk_manager();
    
    private:
    DiskManager* disk_manager_;
    Page pages[BUFFER_POOL_SIZE];

};