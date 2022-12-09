/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
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
	// 후반부
	/* TODO: Set up the swap_disk. */
	swap_disk = NULL;
}

/* Initialize the file mapping */
// 이 함수는 먼저 페이지->작업에서 익명 페이지에 대한 핸들러를 설정
bool anon_initializer(struct page *page, enum vm_type type, void *kva)
{
	/* page struct 안의 union 영역은 현재 uninit page이다.
	ANON page를 초기화해주기 위해 해당 데이터를 모두 0으로 초기화해준다.
	이렇게 하면 union영역은 모두 다 0으로 초기화가 되는 것인가?? -> 그렇다. */
	/* Set up the handler */
	// ----------------project3 start --------------
	
	// ----------------project3 end --------------
	
	page->operations = &anon_ops;

	struct anon_page *anon_page = &page->anon;
	// ----------------project3 start --------------
	
	// ----------------project3 end --------------
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in(struct page *page, void *kva)
{
	struct anon_page *anon_page = &page->anon;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out(struct page *page)
{
	struct anon_page *anon_page = &page->anon;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy(struct page *page)
{
	struct anon_page *anon_page = &page->anon;
}
