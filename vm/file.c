/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "userprog/process.h"
#include "threads/mmu.h"
//-------project3-swap in out start----------------

//-------project3-swap in out end----------------

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
	// mmaped file을 백업 공간으로 사용하기 때문에 swapdisk가 필요하지 않다????
}


/* Initialize the file backed page */
/*  - file-backed page를 초기화한다.
    - file-backed page를 위해 page->operation 안의 handler를 setup한다.
    - page struc의 정보를 업데이트 할 수 있다.  */
bool file_backed_initializer(struct page *page, enum vm_type type, void *kva)
{
	
	/* Set up the handler */
	// printf("file initializer 들어옴\n");
	page->operations = &file_ops;
	// printf("file init 중간\n");
	struct file_page *file_page = &page->file;

	return true;
	
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in(struct page *page, void *kva)
{
	printf("file swap in 들어옴\n");
	struct file_page *file_page UNUSED = &page->file;

	if (page==NULL) {	// page가 NULL이면 종료 // 
		return NULL;
	}

	struct container *container = (struct container *)page->uninit.aux;	// page에서 container에서 가져옴
	struct file *file = container->file;
	off_t offsetof =container->offset;
	size_t page_read_bytes = container->page_read_bytes;
	size_t page_zero_bytes = PGSIZE - page_read_bytes;
	
	// file에서 frame으로(kva통해서) read하기
	if(file_read_at(file, kva, page_read_bytes, offsetof) != page_read_bytes) {
		return false;
	}
	memset(kva + page_read_bytes, 0, page_zero_bytes);

	return true;

	/*
	struct file *file = container->file;
	off_t offsetof =container->offset;
	size_t page_read_bytes = container->page_read_bytes;
	size_t page_zero_bytes = PGSIZE - page_read_bytes;
	
	file_seek(file, offsetof);	// 파일 읽을 위치 세팅
	if (file_read(file,kva, page_read_bytes) != (int)page_read_bytes)
	{
		return false;
	}
	// frame->kva + page_read_bytes부터 page_zero_bytes만큼 값을 0으로 초기화
	memset(kva + page_read_bytes, 0, page_zero_bytes);
	*/
	
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out(struct page *page)
{
	printf("file swap out 들어옴\n");
	struct file_page *file_page UNUSED = &page->file;

	if (page==NULL) {	// page가 NULL이면 종료
		return NULL;
	}

	struct container *container = (struct container *)page->uninit.aux;	// page에서 container에서 가져옴
	// dirtybit가 1인 경우 수정사항을 file에 업데이트(swapout)해준다. 
	if(pml4_is_dirty(thread_current()->pml4, page->va)) {
		file_write_at(container->file,page->va, container->page_read_bytes, container->offset);
		pml4_set_dirty(thread_current()->pml4, page->va, 0);
	}
	// page-frame 연결 해제
	pml4_clear_page(thread_current()->pml4, page->va);

	return true;
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
/* 가상주소 addr부터 file의 크기만큼 다수의 page를 생성한 뒤, 
   각 page에 file의 정보를 저장 (file의 offset부터 lenght 크기만큼)
*/
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
			return NULL;
		}
		
		// 읽은 만큼 읽기정보 업데이트
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		addr += PGSIZE;
		offset += page_read_bytes;
	}
	thread_current()->mmap_addr = start_addr;	// munmap 확인용 -> 추후 list로 수정 요망
	return start_addr;	// 시작 주소를 반환
}


/* Do the munmap */
/* 주어진 가상주소 addr에 해당하는 page에 대해서 frame과의 매핑을 해제함
   만약 page를 수정했다면, 이를 file에 다시 기록함
*/
void do_munmap(void *addr)
{
	// 페이지에 연결되어 있는 물리 프레임과의 연결을 끊어준다. 
	// 유저 가상 메모리의 시작 주소 addr부터 연속으로 나열된 페이지 모두를 매핑 해제한다.
	// 1. addr 범위의 정해진 주소에 대한 메모리 매핑을 해제한다. (페이지를 지우는 게 아니라 present bit을 0으로 만들어준다)
	// 2. 이 addr은 반드시 아직 매핑되지 않은 동일한 프로세스에 의한 mmap 호출로부터 반환된 가상주소여야만 한다.
	// 3. 매핑이 unmapped될 때, 해당 프로세스에 의해 기록된 모든 페이지는 파일에 다시 기록된다.

	// 4. 둘 이상의 프로세스가 동일한 파일을 매핑하는 경우 두 매핑이 동일한 물리 프레임을 공유하는 방식으로 만들어 다룬다
	// 5. (4)와 연관 그리고 mmap 시스템 호출에는 클라이언트가 페이지를 공유할 것

	// addr가 아직 매핑되지 않은 동일한 프로세스에 의한 mmap 호출로부터 반환된 가상주소인지 체크해주기 
	if (thread_current()->mmap_addr != addr) {	// 추후 list에서 찾는 것으로 바꿔야 함
		return NULL;
	}
	// while문 돌면서 file을 page단위로 page-frame 연결을 해제함
	while(1) {
		// addr로 page 찾기
		struct page* page = spt_find_page(&thread_current()->spt, addr);
		if (page==NULL) {	// page가 NULL이면 종료
			return NULL;
		}
		struct container *container = page->uninit.aux;	// page에서 container 가져옴
		
		// dirty bit가 1이라면(수정했다면) if문 진입 + writable이라면
		if (pml4_is_dirty(thread_current()->pml4, page->va) && (page->writable == 1)) {	
			// addr(메모리)에 적힌 내용을 file에 덮어쓰기
			file_write_at(container->file, addr, container->page_read_bytes, container->offset);	
			// dirty bit를 다시 0으로 변경
			pml4_set_dirty(thread_current()->pml4, page->va, 0);
			
		}
		// page-frame 연결 해제
		pml4_clear_page(thread_current()->pml4, page->va);
		addr += PGSIZE;	// 다음 페이지로 
	} 
}
