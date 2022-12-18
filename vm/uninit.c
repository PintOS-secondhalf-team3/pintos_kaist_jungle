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

static bool uninit_initialize (struct page *page, void *kva);
static void uninit_destroy (struct page *page);

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
void
uninit_new (struct page *page, void *va, vm_initializer *init,
		enum vm_type type, void *aux,
		bool (*initializer)(struct page *, enum vm_type, void *)) {
	ASSERT (page != NULL);

	*page = (struct page) {
		.operations = &uninit_ops,
		.va = va,
		.frame = NULL, /* no frame for now */
		.uninit = (struct uninit_page) {
			.init = init,					// lazy_load_segment
			.type = type,					// VM_ANON or VM_FILE
			.aux = aux,						// container
			.page_initializer = initializer,// anon_initializer or file_backed_initializer
		}
	};
}

/* Initalize the page on first fault */
/* 각각의 page 종류에 따라 다른 page initialize가 진행됨 */
static bool
uninit_initialize (struct page *page, void *kva) {
	struct uninit_page *uninit = &page->uninit;
	// vm_initializer 및 aux를 가져오고 함수 포인터를 통해 해당 page_initializer를 호출합니다.

	/* Fetch first, page_initialize may overwrite the values */
	vm_initializer *init = uninit->init;
	void *aux = uninit->aux;

	// enum vm_type type = uninit->type;

	/* TODO: You may need to fix this function. */
	// type이 anon인 경우, page_initializer: anon_initializer(), init: lazy_load_segment() 여기서 호출됨
	
	return uninit->page_initializer (page, uninit->type, kva) &&
		(init ? init (page, aux) : true);
}

/* Free the resources hold by uninit_page. Although most of pages are transmuted
 * to other page objects, it is possible to have uninit pages when the process
 * exit, which are never referenced during the execution.
 * PAGE will be freed by the caller. */
static void
uninit_destroy (struct page *page) {
	struct uninit_page *uninit UNUSED = &page->uninit;
	/* TODO: Fill this function.
	 * TODO: If you don't have anything to do, just return. */
	// uninit->aux = NULL;
}
