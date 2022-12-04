#ifndef VM_VM_H
#define VM_VM_H
#include <stdbool.h>
#include "threads/palloc.h"
#include "lib/kernel/hash.h"


enum vm_type {
	/* page not initialized */
	VM_UNINIT = 0,
	/* page not related to the file, aka anonymous page */
	VM_ANON = 1,
	/* page that realated to the file */
	VM_FILE = 2,
	/* page that hold the page cache, for project 4 */
	VM_PAGE_CACHE = 3,

	/* Bit flags to store state */

	/* Auxillary bit flag marker for store information. You can add more
	 * markers, until the value is fit in the int. */
	VM_MARKER_0 = (1 << 3),
	VM_MARKER_1 = (1 << 4),

	/* DO NOT EXCEED THIS VALUE. */
	VM_MARKER_END = (1 << 31),
};

#include "vm/uninit.h"
#include "vm/anon.h"
#include "vm/file.h"
#ifdef EFILESYS
#include "filesys/page_cache.h"
#endif

struct page_operations;
struct thread;

#define VM_TYPE(type) ((type) & 7)

/* The representation of "page".
 * This is kind of "parent class", which has four "child class"es, which are
 * uninit_page, file_page, anon_page, and page cache (project4).
 * DO NOT REMOVE/MODIFY PREDEFINED MEMBER OF THIS STRUCTURE. */
struct page {
	
	// 해당 operations는 page 구조체를 통해 언제든 요청 될 수 있게 되어있음.
	const struct page_operations *operations;

	// 키가 되는 가상 주소
	void *va;              /* Address in terms of user space */
	struct frame *frame;   /* Back reference for frame */

	
	/* Your implementation */
	/* --- Project 3: VM-SPT ---*/
	struct hash_elem hash_elem; /*spt테이블에서 페이지를 찾기 위해서 hash_elem 필요함이 hash_elem을 타고 struct page 로 가서 메타데이터를 알 수가 있다.*/

	bool writable; // wrtie 가능한지 여부


	
	/* Per-type data are binded into the union.
	 * Each function automatically detects the current union */
	union { 
		// page initialization
		//3가지 종류의 page가 있는 만큼 각각의 page 종류에 따라 다른 초기화가 필요

		// page의 세 가지 종류
		struct uninit_page uninit;

		// 파일에 기반하고 있지 않은(파일로부터 매핑되지 않은) 페이지
		// 커널로부터 프로세스에게 할당된 일반적인 메모리 페이지
		struct anon_page anon;

		// 파일으로부터 매핑된 페이지
		struct file_page file;
#ifdef EFILESYS
		struct page_cache page_cache;
#endif
	};
};

/* The representation of "frame" */
// 물리적 메모리를 나타냄
struct frame {
	void *kva; // 커널 가상 주소 << 물리메모리 프레임이랑 일대일로 매핑되어 있는 가상 주소
	struct page *page; // 페이지 구조
	struct list_elem frame_elem; // 
};

/* The function table for page operations.
 * This is one way of implementing "interface" in C.
 * Put the table of "method" into the struct's member, and
 * call it whenever you needed. */
struct page_operations {
	// 각 페이지가 수행해야 할 수 있는 작업들이 function pointer 형태로 저장
	bool (*swap_in) (struct page *, void *);
	bool (*swap_out) (struct page *);
	void (*destroy) (struct page *);
	enum vm_type type;
};

// 
#define swap_in(page, v) (page)->operations->swap_in ((page), v)
#define swap_out(page) (page)->operations->swap_out (page)
#define destroy(page) \
	if ((page)->operations->destroy) (page)->operations->destroy (page)

/* Representation of current process's memory space.
 * We don't want to force you to obey any specific design for this struct.
 * All designs up to you for this. */
struct supplemental_page_table { // page fault가 발생 -> 이런 상황 해결 -> page table보다 더 많은 정보를 담은 page table인 spt.
	struct hash* spt_hash; // hash 자료구조 방식의 spt임
};

#include "threads/thread.h"
void supplemental_page_table_init (struct supplemental_page_table *spt);
bool supplemental_page_table_copy (struct supplemental_page_table *dst,
		struct supplemental_page_table *src);
void supplemental_page_table_kill (struct supplemental_page_table *spt);
struct page *spt_find_page (struct supplemental_page_table *spt,
		void *va);
bool spt_insert_page (struct supplemental_page_table *spt, struct page *page);
void spt_remove_page (struct supplemental_page_table *spt, struct page *page);

void vm_init (void);
bool vm_try_handle_fault (struct intr_frame *f, void *addr, bool user,
		bool write, bool not_present);

#define vm_alloc_page(type, upage, writable) \
	vm_alloc_page_with_initializer ((type), (upage), (writable), NULL, NULL)
bool vm_alloc_page_with_initializer (enum vm_type type, void *upage,
		bool writable, vm_initializer *init, void *aux);
void vm_dealloc_page (struct page *page);
bool vm_claim_page (void *va);
enum vm_type page_get_type (struct page *page);

#endif  /* VM_VM_H */
