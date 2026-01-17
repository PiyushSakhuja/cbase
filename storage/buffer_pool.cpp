#include "buffer_pool.h"

BufferPool::BufferPool(DiskManager *dm) : disk_manager_(dm)
{
}

Page *BufferPool::fetch_page(int page_id)
{
    for (int i = 0; i < BUFFER_POOL_SIZE; i++)
    {
        if (pages[i].page_id == page_id)
        {
            return &pages[i];
        }
    }
    for (int i = 0; i < BUFFER_POOL_SIZE; i++)
    {
        if(pages[i].page_id ==- 1){
            disk_manager_->read_page(page_id,pages[i].get_data());
            pages[i].page_id=page_id;
            return &pages[i];
        }
    }
    
    return nullptr;
}

void BufferPool::flush_all() {
    for(int i =  0 ; i < BUFFER_POOL_SIZE;i++){
        if(pages[i].page_id != -1 && pages[i].is_dirty()){
            disk_manager_->write_page(
                pages[i].page_id,
                pages[i].get_data()
            );
            pages[i].set_dirty(false);
        }
    }
}