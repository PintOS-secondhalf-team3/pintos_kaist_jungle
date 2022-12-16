/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */
#include "vm/vm.h"
#include "devices/disk.h"
#include "lib/kernel/bitmap.h"
#include "threads/mmu.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
//-------project3-swap in out start----------------
struct bitmap* swap_table;
size_t swap_size;
//-------project3-swap in out end----------------
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

/* Initialize the data for anonymous pages */
void
vm_anon_init (void) { // 익명 페이지 하위 시스템을 초기화
	/* TODO: Set up the swap_disk. */
	//-------project3-swap in out start----------------
	// 1. swap disk를 셋업.
	swap_disk = disk_get(1,1);
	// swap_size: page의 개수 = slot의 개수, disk_size(swap_disk): sector의 개수	
	swap_size = disk_size(swap_disk)/8; // SECTORS_PER_PAGE;	// 1page = 1slot = 8sector
	swap_table = bitmap_create(swap_size);  // swap_table을 bitmap자료구조로 만듬.
	//-------project3-swap in out end----------------
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) { 
		// 이 함수는 먼저 page->operation에서 익명 페이지에 대한 핸들러를 설정
		// 현재 빈 구조체인 anon_page에서 일부 정보를 업데이트해야 할 수도 있음
		// 익명 페이지(예: VM_ANON)의 초기화로 사용
	/* Set up the handler */
	//-------project3-swap in out start----------------
	struct uninit_page* uninit_page = &page->uninit;
	memset(uninit_page, 0, sizeof(struct uninit_page));
	//-------project3-swap in out end----------------
	page->operations = &anon_ops;
	struct anon_page *anon_page = &page->anon;

	return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;
	//-------project3-swap in out start----------------
	int bitmap_idx = anon_page->swap_location;

	if(bitmap_test(swap_table, bitmap_idx) == false) {
		return false;	// bitmap에 false로 표시되었다면, 읽을 수 없으므로 종료
	}
	// swap area(disk)에서 frame으로(kva통해서) read하기 
	for (int i=0; i<SECTORS_PER_PAGE; i++) {
		disk_read(swap_disk, bitmap_idx*SECTORS_PER_PAGE + i, kva + DISK_SECTOR_SIZE*i);
	}

	// swap table 업데이트
	bitmap_set(swap_table, bitmap_idx, false);	// bitmap을 다시 false로 세팅
	// bitmap_flip(swap_table, bitmap_idx);

	return true;
	//-------project3-swap in out end----------------
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	//-------project3-swap in out start----------------
	// bitmap값이 0인 page를 찾는다.
	int bitmap_idx = bitmap_scan(swap_table, 0, 1, false);	// bitmap_idx = slot_no
	if (bitmap_idx == BITMAP_ERROR) {	
		return false;	// 찾지 못한 경우 
	}
	
	// disk에 변경사항 write해줌, 1page = 8sector = 8slot
	for (int i=0; i < SECTORS_PER_PAGE; i++) {
		// DISK_SECTOR_SIZE = 512 = 1섹터의 크기가 512bytes이기 때문
		disk_write(swap_disk, bitmap_idx*SECTORS_PER_PAGE + i, page->va + DISK_SECTOR_SIZE*i);
	}

	bitmap_set(swap_table, bitmap_idx, true);	// bitmap을 다시 true로 세팅
	// bitmap_flip(swap_table, bitmap_idx);
	
	pml4_clear_page(thread_current()->pml4, page->va);	// pml4에서 삭제

	// anon_page구조체에 page위치 저장
	anon_page->swap_location = bitmap_idx;
	return true;
	//-------project3-swap in out end----------------
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
}
