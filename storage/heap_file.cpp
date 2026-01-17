#include "heap_file.h"
#include <cstring>
#include <iostream>

HeapFile::HeapFile(BufferPool *bp)
    : buffer_pool_(bp), next_page_id_(0) {}

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

void HeapFile::insert(const Record &record)
{
    Page *page = buffer_pool_->fetch_page(next_page_id_);

    // initialize page if first use
    if (page->page_id == -1)
    {
        page->page_id = next_page_id_;
        initialize_page(page);
    }

    char *data = page->get_data();
    PageHeader header = read_page_header(data);

    uint16_t record_size = sizeof(Record);

    uint16_t free_space = header.free_space_offset - get_free_space_start(header);
    uint16_t needed_space = record_size + sizeof(Slot);

    if (free_space < needed_space)
    {
        next_page_id_++;
        insert(record);
        return;
    }

    int slot_id = -1;
    for (uint16_t i = 0; i < header.slot_count; i++)
    {
        Slot slot = read_slot(data, i);
        if (slot.is_used == 0)
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

    std::memcpy(data + record_offset, &record, record_size); //(data is raw bytes of page which starts from 0 of the page usme add krenge + record_offset mtlb last se record write karenge,kya write karenge,kitna write karenge)

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
