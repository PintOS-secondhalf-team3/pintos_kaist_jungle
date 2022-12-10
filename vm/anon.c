/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include "lib/kernel/bitmap.h"
#include "threads/mmu.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;

//-------project3-swap in out start----------------
// 여기 스왑 테이블의 경우 각각의 비트는 스왑 슬롯 각각과 매칭된다.
// 스왑 테이블에서 해당 스왑 슬롯에 해당하는 비트가 1이라는 말은 그에
// 대응되는 페이지가 swap out되어 디스크의 스왑 공간에 임시적으로 저장되었다는 뜻이다.
struct bitmap *swap_table;
//-------project3-swap in out end----------------
static bool anon_swap_in(struct page *page, void *kva);
static bool anon_swap_out(struct page *page);
static void anon_destroy(struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

/* Initialize the data for anonymous pages */
void vm_anon_init(void)
{ // 익명 페이지 하위 시스템을 초기화
	/* TODO: Set up the swap_disk. */
	//-------project3-swap in out start----------------
	// 1. swap disk를 셋업.
	swap_disk = disk_get(1, 1);
	// swap_size: page의 개수 = slot의 개수, disk_size(swap_disk): sector의 개수
	size_t swap_size = disk_size(swap_disk) / SECTORS_PER_PAGE; // 1page = 1slot = 8sector
	swap_table = bitmap_create(swap_size);						// swap_table을 bitmap자료구조로 만듬.
																//-------project3-swap in out end----------------
}

/* Initialize the file mapping */
bool anon_initializer(struct page *page, enum vm_type type, void *kva)
{
	// 이 함수는 먼저 page->operation에서 익명 페이지에 대한 핸들러를 설정
	// 현재 빈 구조체인 anon_page에서 일부 정보를 업데이트해야 할 수도 있음
	// 익명 페이지(예: VM_ANON)의 초기화로 사용

	/* Set up the handler */

	//-------project3-swap in/out start----------------
	/* page struct 안의 Union 영역은 현재 uninit page이다.
	ANON page를 초기화해주기 위해 해당 데이터를 모두 0으로 초기화해준다.
Q. 이렇게 하면 Union 영역은 모두 다 0으로 초기화되나? -> 그렇다. */
	struct uninit_page *uninit = &page->uninit;
	memset(uninit, 0, sizeof(struct uninit_page)); // // 12/10 15:42 수정
	//-------project3-swap in/out end----------------
	page->operations = &anon_ops;

	// 해당 페이지는 아직 물리 메모리 위에 있으므로 swap_index 값을 -1로 설정해준다.
	struct anon_page *anon_page = &page->anon;
	// 페이지 폴트가 떠서 해당 페이지를 물리 메모리에 올리게 되므로, 해당 페이지는 디스크가 아닌 물리 메모리에 있기 때문!!
	anon_page->swap_index = -1; // 12/10 15:42 수정
	return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in(struct page *page, void *kva)
{
	// printf("anon swap in\n");
	struct anon_page *anon_page = &page->anon;
	//-------project3-swap in out start----------------
	/* swap out된 페이지가 디스크 스왑 영역 어디에 저장되었는지는
   anon_page 구조체 안에 저장되어 있다. */
	int bitmap_idx = anon_page->swap_index;

	// 스왑 테이블에서 해당 스왑 슬롯이 진짜 사용 중인지 체크한다.
	if (bitmap_test(swap_table, bitmap_idx) == false)
	{
		return false; // bitmap에 false로 표시되었다면, 읽을 수 없으므로 종료
	}

	// swap area(disk)에서 frame으로(kva통해서) read하기
	// 해당 스왑 영역의 데이터를 가상 주소 공간 kva에 써 준다.
	for (int i = 0; i < SECTORS_PER_PAGE; i++)
	{
		disk_read(swap_disk, bitmap_idx * SECTORS_PER_PAGE + i, kva + DISK_SECTOR_SIZE * i);
	}

	// swap table 업데이트
	// 다시 해당 스왑 슬롯을 false로 만들어준다.
	bitmap_set(swap_table, bitmap_idx, false);
	// bitmap_flip(swap_table, bitmap_idx);

	return true;
	//-------project3-swap in out end----------------
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out(struct page *page)
{
	// printf("anon swap out\n");
	struct anon_page *anon_page = &page->anon;
	//-------project3-swap in out start----------------
	/* 비트맵을 처음부터 순회해 false 값을 가진 비트를 하나 찾는다.
	   즉, 페이지를 할당받을 수 있는 swap slot을 하나 찾는다. */
	int bitmap_idx = bitmap_scan(swap_table, 0, 1, false); // bitmap_idx = slot_no
	if (bitmap_idx == BITMAP_ERROR)
	{
		return false; // 찾지 못한 경우
	}

	// disk에 변경사항 write해줌, 1page = 8sector = 8slot
	// 한 페이지를 디스크에 써 주기 위해 SECTORS_PER_PAGE개의 섹터에 저장해야 한다.
	// 이 때 디스크에 각 섹터의 크기 DISK_SECTOR_SIZE만큼 써 준다.
	for (int i = 0; i < SECTORS_PER_PAGE; i++)
	{
		// DISK_SECTOR_SIZE = 512 = 1섹터의 크기가 512bytes이기 때문
		disk_write(swap_disk, bitmap_idx * SECTORS_PER_PAGE + i, page->va + DISK_SECTOR_SIZE * i);
	}

	// swap table의 해당 페이지에 대한 swap slot의 비트를 true로 바꿔주고
	// 해당 페이지의 PTE에서 Present bit을 0으로 바꿔준다.
	// 이제 프로세스가 이 페이지에 접근하면 Page fault가 뜬다.
	bitmap_set(swap_table, bitmap_idx, true); // bitmap을 다시 true로 세팅
	// bitmap_flip(swap_table, bitmap_idx);

	pml4_clear_page(thread_current()->pml4, page->va); // pml4에서 삭제

	// anon_page구조체에 page위치 저장
	// 페이지의 swap_index 값을 이 페이지가 저장된 swap slot의 번호로 써 준다.
	// why ? ->이 페이지가 디스크의 스왑 영역 중 어디에 swap 되었는지를 확인할 수 있도록 한다.
	anon_page->swap_index = bitmap_idx;
	// SWAP OUT된 페이지에 저장된 swap_index 값으로 스왑 슬롯을 찾아 해당 슬롯에 저장된 데이터를 다시 페이지로 원복시킨다.

	return true;
	//-------project3-swap in out end----------------
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy(struct page *page)
{
	struct anon_page *anon_page = &page->anon;
}
