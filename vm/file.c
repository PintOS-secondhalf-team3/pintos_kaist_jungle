/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"

static bool file_backed_swap_in(struct page *page, void *kva);
static bool file_backed_swap_out(struct page *page);
static void file_backed_destroy(struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
 // - file-backed page subsystem을 초기화한다.
 // - file-backed page와 관련된 것을 setup할 수 있다.
void vm_file_init(void)
{

}


/* Initialize the file backed page */
/*  - file-backed page를 초기화한다.
    - file-backed page를 위해 page->operation 안의 handler를 setup한다.
    - page struc의 정보를 업데이트 할 수 있다.  */
bool file_backed_initializer(struct page *page, enum vm_type type, void *kva)
{
	/* Set up the handler */
	page->operations = &file_ops;

	struct file_page *file_page = &page->file;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in(struct page *page, void *kva)
{
	struct file_page *file_page UNUSED = &page->file;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out(struct page *page)
{
	struct file_page *file_page UNUSED = &page->file;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
 // - 연관 file을 닫음으로 file backed page를 지울 수 있다.
 // - 만약 content가 변질되었다면, file로 write-back을 해야한다.
 // - 이 함수에서 page를 free할 필요는 없다. (file_backed_destroy의 caller가 이를 수행한다.)
static void
file_backed_destroy(struct page *page)
{
	struct file_page *file_page UNUSED = &page->file;
}

/* Do the mmap */
void *
do_mmap(void *addr, size_t length, int writable,
		struct file *file, off_t offset)
{
}

/* Do the munmap */
void do_munmap(void *addr)
{
}
