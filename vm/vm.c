/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "lib/kernel/hash.h"
#include "userprog/process.h"
#include "threads/vaddr.h"

//#include "lib/kernel/list.h"

struct list frame_table;

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
	list_init(&frame_table);
	struct list_elem* start = list_begin(&frame_table);
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT);

	struct supplemental_page_table *spt = &thread_current ()->spt;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */
		struct page* page = (struct page*)malloc(sizeof(struct page));
		
		typedef bool (*initializerFunc)(struct page *, enum vm_type, void *);
		initializerFunc initializer = NULL;

		switch (VM_TYPE(type))
		{
			case VM_ANON:
				initializer = anon_initializer;
				break;
			
			case VM_FILE:
				initializer = file_backed_initializer;
				break;
		}
		uninit_new(page, upage, init, type, aux, initializer);
		page->writable = writable;
		/* TODO: Insert the page into the spt. */
		return spt_insert_page(spt, page);
	}
err: 
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	// struct page *page = NULL;
	struct page* page = page_lookup (va);
	return page;
	// struct page *temp_page;
	// temp_page->va = va;
	
	// struct hash_elem* hash_elem = hash_find(&spt->spt_hash, &temp_page->hash_elem);
	// /* TODO: Fill this function. */
	// struct page* page = hash_entry(hash_elem, struct page, hash_elem);
	// return page;
}

struct page *
page_lookup (const void *address) {
  struct page* p = (struct page*)malloc(sizeof(struct page)); // Anonymous Page---------수정 12/06
  struct hash_elem *e;

  p->va = pg_round_down(address);
  e = hash_find (&thread_current()->spt.spt_hash , &p->hash_elem);
  return e != NULL ? hash_entry (e, struct page, hash_elem) : NULL;
}
/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	int succ = false;
	struct hash_elem* p = hash_insert(&spt->spt_hash, &page->hash_elem);
	if (p == NULL);
		succ = true;
	/* TODO: Fill this function. */
	return succ;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	/* TODO: The policy for eviction is up to you. */
	
	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */
	swap_out(victim->page);
	return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
	struct frame *frame = (struct frame*)malloc(sizeof(struct frame));
	/* TODO: Fill this function. */
	frame->kva = palloc_get_page(PAL_USER);
	if (frame->kva ==NULL){
		PANIC("todo");
		//frame = vm_evict_frame();
		//frame->page = NULL;
		//return frame;
	}
	list_push_back(&frame_table,&frame->frame_elem);

	frame->page = NULL;

	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	struct page *page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */

	// --------------------project3 Anonymous Page start---------
	if (is_kernel_vaddr(addr)) {
        return false;
	}

    void *rsp_stack = is_kernel_vaddr(f->rsp) ? thread_current()->rsp_stack : f->rsp;
    if (not_present){
        if (!vm_claim_page(addr)) {
            if (rsp_stack - 8 <= addr && USER_STACK - 0x100000 <= addr && addr <= USER_STACK) {
                vm_stack_growth(thread_current()->stack_bottom - PGSIZE);
                return true;
            }
            return false;
        }
        else
            return true;
    }
    return false;
	// --------------------project3 Anonymous Page end---------
	// return vm_do_claim_page (page);  //기존 코드
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
bool
vm_claim_page (void *va UNUSED) {
	struct page *page = NULL;
	struct thread* curr = thread_current();
	
	/* TODO: Fill this function */
	page = spt_find_page(&curr->spt,va);
	if (page == NULL)
		return false;
	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
// 가상 주소와 물리 주소 매핑(성공, 실패 여부 리턴)
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	if (install_page(page->va, frame->kva, page->writable))
		return swap_in (page, frame->kva);
	return false;
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	hash_init(&spt->spt_hash ,page_hash ,page_less , NULL);
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
		/**/
		//----------------------------project3 anonymous page start-----------
		//spt를 src에서 dst로 복붙한다.
		
		//해시테이블 순회하기
		struct hash_iterator i;
		hash_first(&i, &src->spt_hash);
		while (hash_next(&i)) // src의 모든 페이지를 dst로 복붙.
		{
			// 해시테이블의 elem에서 page 받아옴.
			struct page* parent_page = hash_entry(hash_cur(&i), struct page, hash_elem);// 부모페이지
			enum vm_type parent_type = page_get_type(parent_page);	// 부모페이지의 type
			void* upage = parent_page->va;  // 부모페이지의 va
			bool writable = parent_page->writable;
			vm_initializer *init = parent_page->uninit.init; // 부모의 init함수
			void* aux = parent_page->uninit.aux; 
			
			
			if (parent_page->operations->type == VM_UNINIT) {	// 부모 type이 uninit인 경우
				if(!vm_alloc_page_with_initializer(parent_type, upage, writable, init, aux)) {
					return false;
				}
			}
			else {	// 부모 type이 uninit이 아닌 경우
				if(!vm_alloc_page(parent_type, upage, writable)) {
					return false;
				}
				if(!vm_claim_page(upage)) {
					return false;
				}

				// 부모의 것을 child에 memcpy한다. 
				struct page* child_page = spt_find_page(dst, upage);
				memcpy(child_page->frame->kva, parent_page->frame->kva, PGSIZE);
			}
		}
		return true;
		
		//----------------------------project3 anonymous page end-----------
}

/* Free the resource hold by the supplemental page table */

void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	//----------------------------project3 anonymous page start-----------
	hash_destroy(&spt->spt_hash, hash_destructor);
	//----------------------------project3 anonymous page end-----------

}

/* Returns true if page a precedes page b. */
bool
page_less (const struct hash_elem *a_,
           const struct hash_elem *b_, void *aux UNUSED) {
  const struct page *a = hash_entry (a_, struct page, hash_elem);
  const struct page *b = hash_entry (b_, struct page, hash_elem);

  return a->va < b->va;
}

/* Returns a hash value for page p. */
unsigned
page_hash (const struct hash_elem *p_, void *aux UNUSED) {
  const struct page *p = hash_entry (p_, struct page, hash_elem);
  return hash_bytes (&p->va, sizeof p->va);
}