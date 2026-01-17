#include "page.h"
#include <cstring>

Page::Page(){
    page_id = -1;
    dirty = false;
    std::memset(data,0,PAGE_SIZE);
}

char* Page::get_data(){
    return data;
}

void Page::set_dirty(bool d){
    dirty = d;
}

bool Page::is_dirty() const{
    return dirty;
}