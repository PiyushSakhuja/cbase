#include "heap_file.h"
#include <cstring>
#include <iostream>

HeapFile::HeapFile(BufferPool *bp)
    : buffer_pool_(bp)
{
    int page_count = buffer_pool_->get_disk_manager()->get_page_count();

    if (page_count == 0)
    {
        next_page_id_ = 0;
    }
    else
    {
        next_page_id_ = page_count - 1;
    }
}

void HeapFile::initialize_page(Page *page)
{
    char *data = page->get_data();

    PageHeader header;
    header.slot_count = 0;
    header.record_count = 0;
    header.free_space_offset = PAGE_SIZE;
    write_page_header(data, header);

    page->set_dirty(true);
}

RID HeapFile::insert(const Record &record)
{
    std::cout << "Inserting into page " << next_page_id_ << std::endl;
    int page_count = buffer_pool_->get_disk_manager()->get_page_count();

    bool new_page = (next_page_id_ >= page_count);

    Page *page = buffer_pool_->fetch_page(next_page_id_);

    if (page == nullptr)
    {
        std::cerr << "Failed to fetch page\n";
        exit(1);
    }

 

    if (new_page) {
    initialize_page(page);
}

    char *data = page->get_data();
    PageHeader header = read_page_header(data);

    uint16_t record_size = sizeof(Record);

    uint16_t free_space =
        header.free_space_offset - get_free_space_start(header);

    uint16_t needed_space = record_size + sizeof(Slot);

    if (free_space < needed_space)
    {
        next_page_id_++;
        return insert(record);
    }

    int slot_id = -1;
    for (uint16_t i = 0; i < header.slot_count; i++)
    {
        Slot s = read_slot(data, i);
        if (s.is_used == 0)
        {
            slot_id = i;
            break;
        }
    }

    bool new_slot = false;
    if (slot_id == -1)
    {
        slot_id = header.slot_count;
        new_slot = true;
    }

    header.free_space_offset -= record_size;
    uint16_t record_offset = header.free_space_offset;

    std::memcpy(data + record_offset, &record, record_size);

    Slot slot;
    slot.offset = record_offset;
    slot.size = record_size;
    slot.is_used = 1;

    write_slot(data, slot_id, slot);

    if (new_slot)
    {
        header.slot_count++;
    }

    header.record_count++;
    write_page_header(data, header);

    page->set_dirty(true);

    return RID{
        static_cast<uint16_t>(next_page_id_),
        static_cast<uint16_t>(slot_id)};
}

void HeapFile::scan_all()
{
    int record_size = sizeof(Record);

    for (int pid = 0; pid <= next_page_id_; pid++)
    {
        Page *page = buffer_pool_->fetch_page(pid);
        char *data = page->get_data();
        PageHeader header = read_page_header(data);

        for (uint16_t i = 0; i < header.slot_count; i++)
        {
            Slot slot = read_slot(data, i);
            if (slot.is_used == 0)
            {
                continue;
            }
            Record temp;
            std::memcpy(&temp, data + slot.offset, slot.size);
            std::cout
                << "Page " << pid
                << " Slot " << i
                << " | ID: " << temp.id
                << " Age: " << temp.age
                << std::endl;
        }
    }
}

void HeapFile::Delete(uint16_t page_id, uint16_t slot_id)
{
    Page *page = buffer_pool_->fetch_page(page_id);
    char *data = page->get_data();

    PageHeader header = read_page_header(data);

    // sanity check
    if (slot_id >= header.slot_count)
    {
        return;
    }

    Slot slot = read_slot(data, slot_id);

    // already deleted
    if (slot.is_used == 0)
    {
        return;
    }

    // mark slot free
    slot.is_used = 0;
    write_slot(data, slot_id, slot);

    // update header
    header.record_count--;
    write_page_header(data, header);

    page->set_dirty(true);
}

PageHeader HeapFile::read_page_header(char *data)
{
    PageHeader header;
    std::memcpy(&header, data, sizeof(PageHeader));
    return header;
}

void HeapFile::write_page_header(char *data, const PageHeader &header)
{
    std::memcpy(data, &header, sizeof(PageHeader));
}

void HeapFile::write_slot(char *data, uint16_t slot_id, const Slot &slot)
{
    char *slot_addr =
        data + sizeof(PageHeader) + slot_id * sizeof(Slot);

    std::memcpy(slot_addr, &slot, sizeof(Slot));
}

Slot HeapFile::read_slot(char *data, uint16_t slot_id)
{
    Slot slot;
    char *slot_addr =
        data + sizeof(PageHeader) + slot_id * sizeof(Slot);

    std::memcpy(&slot, slot_addr, sizeof(Slot));
    return slot;
}

uint16_t HeapFile::get_free_space_start(const PageHeader &header)
{
    return sizeof(PageHeader) +
           header.slot_count * sizeof(Slot);
}
