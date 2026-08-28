#include "heap_file.h"
#include <cstring>
#include <iostream>

// The on-disk format is a raw memcpy of the structs above. These asserts
// document the exact layout this build writes to disk (Slot carries one tail
// padding byte). Reading a file written on a machine with different
// endianness or struct padding is not supported - see README ("On-disk
// format").
static_assert(sizeof(PageHeader) == 6, "unexpected PageHeader padding");
static_assert(sizeof(Slot) == 6, "unexpected Slot padding");
static_assert(sizeof(Record) == 8, "unexpected Record padding");

HeapFile::HeapFile(BufferPool *bp)
    : buffer_pool_(bp)
{
    int page_count = 0;
    if (bp != nullptr)
    {
        page_count = bp->get_disk_manager()->get_page_count();
    }

    if (page_count == 0)
    {
        // Fresh database: the first insert will initialize page 0.
        next_page_id_ = 0;
        initialized_pages_ = 0;
    }
    else
    {
        // Existing database: pages 0..page_count-1 are on disk with valid
        // headers; keep appending to the last one.
        next_page_id_ = page_count - 1;
        initialized_pages_ = page_count;
    }
}

void HeapFile::initialize_page(Page *page)
{
    // Empty slotted page: no slots, free space starts at the page end.
    // Records grow upward from PAGE_SIZE; the directory grows downward
    // right after the header.
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
    const uint16_t record_size = sizeof(Record);

    // A record must fit in a brand-new page together with its slot entry,
    // otherwise no page could ever hold it.
    if (static_cast<int>(record_size) + static_cast<int>(sizeof(Slot)) +
            static_cast<int>(sizeof(PageHeader)) >
        PAGE_SIZE)
    {
        std::cerr << "HeapFile: record of " << record_size
                  << " bytes can never fit in a page\n";
        return RID::invalid();
    }

    // Loop (instead of recursing) while the current page cannot take the
    // record: either it is full or its header is unusable. next_page_id_
    // strictly increases each round, so this terminates - worst case at a
    // fresh page, which always has room (checked above).
    while (true)
    {
        // RID page ids are uint16_t: 65,535 pages (~19M records) is the
        // addressing limit of this format.
        if (next_page_id_ >= 0xFFFF)
        {
            std::cerr << "HeapFile: page id limit reached ("
                      << next_page_id_ << ")\n";
            return RID::invalid();
        }

        // A page is "new" when it is beyond every initialized page. This is
        // tracked in memory, NOT via the file size: dirty pages only reach
        // the file on flush/eviction, so the file size would disagree with
        // reality mid-session (this was the source of the v1 overwrite bug).
        bool new_page = (next_page_id_ >= initialized_pages_);

        Page *page = buffer_pool_->fetch_page(next_page_id_);
        if (page == nullptr)
        {
            std::cerr << "HeapFile: could not fetch page "
                      << next_page_id_ << " for insert\n";
            return RID::invalid();
        }

        if (new_page)
        {
            initialize_page(page);
            initialized_pages_ = next_page_id_ + 1;
        }

        char *data = page->get_data();
        PageHeader header = read_page_header(data);

        if (!header_is_sane(header))
        {
            std::cerr << "HeapFile: page " << next_page_id_
                      << " has a corrupt header, skipping to next page\n";
            next_page_id_++;
            continue;
        }

        // Prefer reusing a slot freed by Delete(); only append a new slot
        // to the directory when none is free.
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

        // Exact space check: a reused slot costs only the record bytes; a
        // new slot also costs one directory entry.
        uint16_t dir_end = get_free_space_start(header);
        uint16_t dir_end_after = dir_end + (new_slot ? sizeof(Slot) : 0);

        if (header.free_space_offset < dir_end_after + record_size)
        {
            // Page is full for this record: move on to the next page.
            next_page_id_++;
            continue;
        }

        // Reserve the record space at the top of the used area (records
        // grow upward, i.e. free_space_offset moves down).
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
}

bool HeapFile::get_record(uint16_t page_id, uint16_t slot_id, Record &out)
{
    Page *page = buffer_pool_->fetch_page(page_id);
    if (page == nullptr)
    {
        return false;
    }

    char *data = page->get_data();
    PageHeader header = read_page_header(data);

    if (!header_is_sane(header))
    {
        std::cerr << "HeapFile: page " << page_id
                  << " has a corrupt header\n";
        return false;
    }

    if (slot_id >= header.slot_count)
    {
        return false; // slot does not exist in this page
    }

    Slot slot = read_slot(data, slot_id);
    if (slot.is_used == 0)
    {
        return false; // logically deleted
    }
    if (!slot_points_inside_page(slot))
    {
        std::cerr << "HeapFile: slot " << slot_id << " of page " << page_id
                  << " points outside the page\n";
        return false;
    }

    std::memcpy(&out, data + slot.offset, sizeof(Record));
    return true;
}

int HeapFile::scan_all()
{
    int live = 0;

    // Only scan pages known to be initialized (on disk or this session).
    for (int pid = 0; pid < initialized_pages_; pid++)
    {
        Page *page = buffer_pool_->fetch_page(pid);
        if (page == nullptr)
        {
            std::cerr << "HeapFile: scan could not fetch page " << pid << "\n";
            continue;
        }

        char *data = page->get_data();
        PageHeader header = read_page_header(data);

        if (!header_is_sane(header))
        {
            std::cerr << "HeapFile: page " << pid
                      << " has a corrupt header, skipping it\n";
            continue;
        }

        for (uint16_t i = 0; i < header.slot_count; i++)
        {
            Slot slot = read_slot(data, i);
            if (slot.is_used == 0)
            {
                continue; // deleted slot
            }
            if (!slot_points_inside_page(slot))
            {
                std::cerr << "HeapFile: slot " << i << " of page " << pid
                          << " points outside the page, skipping it\n";
                continue;
            }

            Record temp;
            std::memcpy(&temp, data + slot.offset, sizeof(Record));
            std::cout
                << "Page " << pid
                << " Slot " << i
                << " | ID: " << temp.id
                << " Age: " << temp.age
                << std::endl;
            live++;
        }
    }
    return live;
}

int HeapFile::count_records()
{
    // Same walk as scan_all(), minus the printing.
    int live = 0;
    for (int pid = 0; pid < initialized_pages_; pid++)
    {
        Page *page = buffer_pool_->fetch_page(pid);
        if (page == nullptr)
        {
            continue;
        }

        char *data = page->get_data();
        PageHeader header = read_page_header(data);

        if (!header_is_sane(header))
        {
            continue;
        }

        for (uint16_t i = 0; i < header.slot_count; i++)
        {
            Slot slot = read_slot(data, i);
            if (slot.is_used != 0 && slot_points_inside_page(slot))
            {
                live++;
            }
        }
    }
    return live;
}

bool HeapFile::Delete(uint16_t page_id, uint16_t slot_id)
{
    Page *page = buffer_pool_->fetch_page(page_id);
    if (page == nullptr)
    {
        return false;
    }

    char *data = page->get_data();
    PageHeader header = read_page_header(data);

    if (!header_is_sane(header))
    {
        std::cerr << "HeapFile: page " << page_id
                  << " has a corrupt header\n";
        return false;
    }

    if (slot_id >= header.slot_count)
    {
        return false; // slot does not exist
    }

    Slot slot = read_slot(data, slot_id);
    if (slot.is_used == 0)
    {
        return false; // already deleted
    }

    // Logical deletion: free the slot, leave the record bytes in place.
    // The RID stops resolving; the slot (and, on later inserts, its space)
    // becomes reusable.
    slot.is_used = 0;
    write_slot(data, slot_id, slot);

    // Guard against a header whose record_count is already 0 (would wrap
    // around 65535 and corrupt the header further).
    if (header.record_count > 0)
    {
        header.record_count--;
        write_page_header(data, header);
    }

    page->set_dirty(true);
    return true;
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
    // Bounds check: with a sane header this can never trigger, but a
    // corrupted slot_count read from disk must not let us write outside
    // the page.
    if (slot_id >= MAX_SLOT_COUNT)
    {
        std::cerr << "HeapFile: refusing to write slot " << slot_id
                  << " outside the page\n";
        return;
    }

    char *slot_addr =
        data + sizeof(PageHeader) + static_cast<size_t>(slot_id) * sizeof(Slot);

    std::memcpy(slot_addr, &slot, sizeof(Slot));
}

Slot HeapFile::read_slot(char *data, uint16_t slot_id)
{
    Slot slot;

    if (slot_id >= MAX_SLOT_COUNT)
    {
        // Out-of-range slot: return a safe, unused slot instead of reading
        // outside the page.
        slot.offset = 0;
        slot.size = 0;
        slot.is_used = 0;
        return slot;
    }

    char *slot_addr =
        data + sizeof(PageHeader) + static_cast<size_t>(slot_id) * sizeof(Slot);

    std::memcpy(&slot, slot_addr, sizeof(Slot));
    return slot;
}

uint16_t HeapFile::get_free_space_start(const PageHeader &header)
{
    return sizeof(PageHeader) +
           static_cast<uint16_t>(header.slot_count) * sizeof(Slot);
}

bool HeapFile::header_is_sane(const PageHeader &header)
{
    return header.slot_count <= MAX_SLOT_COUNT
        && header.record_count <= header.slot_count
        && header.free_space_offset >= get_free_space_start(header)
        && header.free_space_offset <= PAGE_SIZE;
}

bool HeapFile::slot_points_inside_page(const Slot &slot)
{
    // Records have one fixed size in this engine; a slot claiming anything
    // else (or an offset that leaves the page) means corruption.
    return slot.size == sizeof(Record)
        && slot.offset >= sizeof(PageHeader)
        && slot.offset + slot.size <= PAGE_SIZE;
}
