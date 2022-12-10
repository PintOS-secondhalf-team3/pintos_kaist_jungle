#ifndef VM_ANON_H
#define VM_ANON_H
#include "vm/vm.h"
#include "threads/vaddr.h"  //  추가
struct page;
enum vm_type;

struct anon_page {
    // struct page anon_p; // heesan 주의☠️ ??

    // swap된 데이터들이 저장된 섹터 구역
    size_t swap_index;   // swap disk 위치
};

void vm_anon_init (void);
bool anon_initializer (struct page *page, enum vm_type type, void *kva);

#endif
