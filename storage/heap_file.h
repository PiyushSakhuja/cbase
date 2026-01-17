#pragma once

#include "buffer_pool.h"
#include <cstdint>
// Record layout inside a page
struct Record
{
    int id;
    int age;
};

struct PageHeader
{
    uint16_t record_count;      // how many record are present means if some deleted it will not count
    uint16_t slot_count;        // total record if deleted or not
    uint16_t free_space_offset; // from where record data will be written
};

struct Slot
{
    uint16_t offset;  // where this slot storage starts
    uint16_t size;    // size of the record
    uint8_t is_used; // 1 = slot occupied , 0 = free
};

class HeapFile
{
public:
    explicit HeapFile(BufferPool *buffer_pool);

    void insert(const Record &record);
    void scan_all();
    void Delete(uint16_t page_id, uint16_t slot_id);


private:
    BufferPool *buffer_pool_;
    int next_page_id_;

    
    void initialize_page(Page *page);



    PageHeader read_page_header(char *data);
    void write_page_header(char *data, const PageHeader &header);

    
    Slot read_slot(char *data, uint16_t slot_id);
    void write_slot(char *data, uint16_t slot_id, const Slot &slot);

    
    uint16_t get_free_space_start(const PageHeader &header);
};
