#include "buffer_pool.h"
#include <iostream>

BufferPool::BufferPool(DiskManager *dm)
    : disk_manager_(dm)
    , next_victim_(0)
    , stats_{0, 0, 0, 0}
{
}

Page *BufferPool::fetch_page(int page_id)
{
    if (page_id < 0)
    {
        std::cerr << "BufferPool: fetch_page rejected invalid page_id "
                  << page_id << "\n";
        return nullptr;
    }

    // 1. Cache hit: the page is already in a frame.
    for (int i = 0; i < BUFFER_POOL_SIZE; i++)
    {
        if (pages[i].page_id == page_id)
        {
            stats_.hits++;
            return &pages[i];
        }
    }

    // 2. There is still an empty frame: load the page into it.
    for (int i = 0; i < BUFFER_POOL_SIZE; i++)
    {
        if (pages[i].page_id == -1)
        {
            pages[i].reset(page_id);
            disk_manager_->read_page(page_id, pages[i].get_data());
            stats_.misses++;
            return &pages[i];
        }
    }

    // 3. All frames occupied: evict one (round-robin). A dirty victim must
    // be written back to disk BEFORE its frame is reused, otherwise its
    // changes would be lost.
    Page &victim = pages[next_victim_];
    if (victim.is_dirty())
    {
        disk_manager_->write_page(victim.page_id, victim.get_data());
        victim.set_dirty(false);
        stats_.dirty_writebacks++;
    }
    stats_.evictions++;

    int frame = next_victim_;
    next_victim_ = (next_victim_ + 1) % BUFFER_POOL_SIZE;

    victim.reset(page_id);
    disk_manager_->read_page(page_id, victim.get_data());
    stats_.misses++;
    return &pages[frame];
}

int BufferPool::flush_all()
{
    int flushed = 0;
    for (int i = 0; i < BUFFER_POOL_SIZE; i++)
    {
        if (pages[i].page_id != -1 && pages[i].is_dirty())
        {
            if (disk_manager_->write_page(
                    pages[i].page_id,
                    pages[i].get_data()))
            {
                flushed++;
            }
            pages[i].set_dirty(false);
        }
    }
    stats_.dirty_writebacks += flushed;
    return flushed;
}

DiskManager* BufferPool::get_disk_manager() {
    return disk_manager_;
}

BufferPool::Stats BufferPool::stats() const {
    return stats_;
}

const Page* BufferPool::frame_at(int index) const {
    if (index < 0 || index >= BUFFER_POOL_SIZE)
        return nullptr;
    return &pages[index];
}
