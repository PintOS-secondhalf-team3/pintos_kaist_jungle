/* uninit.c: Implementation of uninitialized page.
 *
 * All of the pages are born as uninit page. When the first page fault occurs,
 * the handler chain calls uninit_initialize (page->operations.swap_in).
 * The uninit_initialize function transmutes the page into the specific page
 * object (anon, file, page_cache), by initializing the page object,and calls
 * initialization callback that passed from vm_alloc_page_with_initializer
 * function.
 * */

#include "vm/vm.h"
#include "vm/uninit.h"
#include "threads/malloc.h"

static bool uninit_initialize(struct page *page, void *kva);
static void uninit_destroy(struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations uninit_ops = {
	.swap_in = uninit_initialize,
	.swap_out = NULL,
	.destroy = uninit_destroy,

	// uninit type의 page는 lazy loading을 지원하기 위해 있다.
	// 모든 페이지는 우선 uninit type으로 생성
	.type = VM_UNINIT,

};

/* DO NOT MODIFY this function */
// uninit_new: uninit type의 page 1개를 만든다. 구조체에 전달받은 인자를 넘겨준다
void uninit_new(struct page *page, void *va, vm_initializer *init,
				enum vm_type type, void *aux,
				bool (*initializer)(struct page *, enum vm_type, void *))
{
	ASSERT(page != NULL);

	*page = (struct page){
		.operations = &uninit_ops,
		.va = va,
		.frame = NULL, /* no frame for now */
		.uninit = (struct uninit_page){
			.init = init,					 // lazy_load_segment
			.type = type,					 // VM_ANON or VM_FILE
			.aux = aux,						 // container
			.page_initializer = initializer, // anon_initializer or file_backed_initializer
		}};
}

/* Initalize the page on first fault */
/* 각각의 page 종류에 따라 다른 page initialize가 진행됨 */
static bool
uninit_initialize(struct page *page, void *kva)
{
	// 프로세스가 처음 만들어진(UNINIT)페이지에 처음으로 접근할 때 page fault가 발생한다.
	// 그러면 page fault handler는 해당 페이지를 디스크에서 프레임으로 swap-in하는데, UNINIT type일 때의 swap_in 함수가 바로 이 함수이다.
	// page fault handler: page_fault() -> vm_try_handle_fault()-> vm_do_claim_page()
	// -> swap_in() -> uninit_initialize()-> r각 타입에 맞는 initializer()와 vm_init() 호출

	struct uninit_page *uninit = &page->uninit; // 페이지 구조체 내 UNION 내 uninit struct

	/* Fetch first, page_initialize may overwrite the values */
	// vm_initializer 및 aux를 가져오고 함수 포인터를 통해 해당 page_initializer를 호출함
	vm_initializer *init = uninit->init;
	void *aux = uninit->aux;

	// enum vm_type type = uninit->type;

	/* TODO: You may need to fix this function. */
	// type이 anon인 경우, page_initializer: anon_initializer(), init: lazy_load_segment() 여기서 호출됨
	/*
		해당 페이지 타입에 맞도록 페이지를 초기화한다.
		만약 해당 페이지의 segment가 load되지 않은 상태면 lazy load해준다.
		init이 lazy_load_segmet일때에 해당.
	*/
	return uninit->page_initializer(page, uninit->type, kva) && (init ? init(page, aux) : true);
}

/* Free the resources hold by uninit_page. Although most of pages are transmuted
 * to other page objects, it is possible to have uninit pages when the process
 * exit, which are never referenced during the execution.
 * PAGE will be freed by the caller. */
static void
uninit_destroy(struct page *page)
{
	struct uninit_page *uninit UNUSED = &page->uninit;
	/* TODO: Fill this function.
	 * TODO: If you don't have anything to do, just return. */
	// uninit->aux = NULL;
}
