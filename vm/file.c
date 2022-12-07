/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "userprog/process.h"

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

	/* reopen하는 이유: 
	   file은 이미 open된 상태이며 우리는 그 file을 메모리에 올려주는 작업을 함.
	   만약 우리가 file에 수정을 하는 작업 도중에 file이 close 되어버렸다면, 수정사항이 disk에 반영되지 않음
	   따라서 mmap이 실행되고 munmap이 실행되기 전까지 같은 inode를 가진 새로운 file 구조체 만들어서 
	   이를 open하는 것임
	*/
	struct file *mfile = file_reopen(file);
	void * start_addr = addr; // 시작 주소

	// 파일을 읽고자 하는 크기(length)가 실제 파일의 크기보다 크다면, 실제 파일의 크기만큼만 읽는다.
	// 반대로, 파일을 읽고자 하는 크기(length)가 실제 파일의 크기보다 작다면, length만큼만 읽는다. 
	size_t read_bytes = length > file_length(file) ? file_length(file) : length;
	// PGSIZE 단위로 파일을 다루기 때문에, PGSIZE만큼 못쓰고 남는 부분이 있다면 패딩처리 해준다.
	size_t zero_bytes = PGSIZE - read_bytes%PGSIZE; 

	/* 파일을 페이지 단위로 잘라 해당 파일의 정보들을 container 구조체에 저장한다.
	   FILE-BACKED 타입의 UINIT 페이지를 만들어 lazy_load_segment()를 vm_init으로 넣는다. */
	while(read_bytes > 0 || zero_bytes > 0) {

		// page_read_bytes만큼 잘라서 읽는다. 
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;	
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		// container에 file 읽기 정보를 넣는다. - 나중에 lazy_load_segment로 넘어감
		struct container *container = (struct container *)malloc(sizeof(struct container));
		container->file = mfile;
		container->page_read_bytes = page_read_bytes;
		container->offset = offset;

		if (!vm_alloc_page_with_initializer(VM_FILE, addr,
											writable, lazy_load_segment, container)) {
			return false;
		}
		
		// 읽은 만큼 읽기정보 업데이트
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		addr += PGSIZE;
		offset += page_read_bytes;
	}

	return start_addr;	// 시작 주소를 반환
}

/* Do the munmap */
void do_munmap(void *addr)
{
}
