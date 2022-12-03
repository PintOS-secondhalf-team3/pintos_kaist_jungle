/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "lib/kernel/hash.h"
#include "include/threads/thread.h"
#include "userprog/process.h"

struct list frame_table;

/* Initializes the virtual memory subsystem by invoking
 * intialize codes. */
void vm_init(void)
{
	vm_anon_init();
	vm_file_init();
#ifdef EFILESYS /* For project 4 */
	pagecache_init();
#endif
	register_inspect_intr();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
	list_init(&frame_table); // frame_table 리스트로 묶어서 초기화

	// heesan 왜 여기서 start에 frame_table의 첫 요소를 할당해주었는가?
	struct list_elem *start = list_begin(&frame_table);
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type(struct page *page)
{
	int ty = VM_TYPE(page->operations->type);
	switch (ty)
	{
	case VM_UNINIT:
		return VM_TYPE(page->uninit.type);
	default:
		return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim(void);
static bool vm_do_claim_page(struct page *page);
static struct frame *vm_evict_frame(void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool vm_alloc_page_with_initializer(enum vm_type type, void *upage, bool writable, vm_initializer *init, void *aux)
{	// 전달된 vm_type에 따라 적절한 initializer를 가져와서 uninit_new를 호출하는 역할
	// vm_alloc_page_with_initializer는 무조건 uninit type의 page를 만든다.

	ASSERT(VM_TYPE(type) != VM_UNINIT);

	struct supplemental_page_table *spt = &thread_current()->spt;

	/* Check whether the upage is already occupied or not. */
	if (spt_find_page(spt, upage) == NULL) // page fault
	{	
		// 유저 페이지가 아직 없으니까 초기화를 해줘야 함

		// TODO: Create the page, fetch the initialier according to the VM type
		struct page *page = (struct page *)malloc(sizeof(struct page));

		// heesan ??? 이거 뭐지? 어떻게 해석하지?? 
		typedef bool (*initializerFunc)(struct page *, enum vm_type, void *);
		// initailizer의 타입을 맞춰줘야 uninit_new의 인자로 들어갈 수 있다.
		initializerFunc initializer = NULL;

		// vm_type에 따라 다른 initializer를 부른다.
		switch (VM_TYPE(type))
		{
		case VM_ANON:
			initializer = anon_initializer;
			break;
		case VM_FILE:
			initializer = file_backed_initializer;
			break;
		default:
			break;
		}
		// TODO: and then create "uninit" page struct by calling uninit_new.
		// TODO: should modify the field after calling the uninit_new.
		
		// uninit_new에서 받아온 type으로 이 uninit type이 어떤 type으로 변할지와 같은 정보들을 page 구조체에 채워줌
		uninit_new(page,upage,init,type,aux,initializer);
		page->writable = writable;
		// TODO: Insert the page into the spt.
		return spt_insert_page(spt, page);
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
// 인자로 받은 va(가상 주소)에 해당하는 페이지 번호를 spt에서 검색하여 페이지 번호를 추출하는 함수
struct page *
spt_find_page(struct supplemental_page_table *spt UNUSED, void *va UNUSED)
// va를 기준으로 hash_table에서 elem을 찾는다
{
	struct page *page = page_lookup(va); // heesan
	/* TODO: Fill this function. */

	return page;
}

/* Returns the page containing the given virtual address, or a null pointer if no such page exists. */
struct page *
page_lookup(const void *address)
{
	struct page p;
	struct hash_elem *e;

	// va가 가리키는 가상 페이지의 시작포인트(오프셋이 0으로 설정된 va) 반환
	p.va = pg_round_down(address);

	// hash_find : 가상 주소를 기반으로 페이지를 찾고 반환하는 함수
	// 주어진 element와 같은 element가 hash안에 있는지 탐색
	// 성공하면 해당 element를, 실패하면 null 포인터로 반환
	e = hash_find(&thread_current()->spt->spt_hash, &p.hash_elem); // 해시 테이블에서 요소 검색한다.
	return e != NULL ? hash_entry(e, struct page, hash_elem) : NULL;
}

/* Insert PAGE into spt with validation. */
bool spt_insert_page(struct supplemental_page_table *spt UNUSED,
					 struct page *page UNUSED)
{
	int succ = false;
	/* TODO: Fill this function. */
	struct hash_elem *p = hash_insert(&spt->spt_hash, &page->hash_elem);
	if (p == NULL)
		;
	succ = true;

	return insert_page(&spt->spt_hash, page);
}

// heesan
bool insert_page(struct hash *pages, struct page *p)
{
	if (!hash_insert(pages, &p->hash_elem))
		return true;
	else
		return false;
}

void spt_remove_page(struct supplemental_page_table *spt, struct page *page)
{
	vm_dealloc_page(page);
	return true;
}

bool delete_page(struct hash *pages, struct page *p)
{
	if (!hash_delete(pages, &p->hash_elem))
		return true;
	else
		return false;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim(void)
{

	struct frame *victim = NULL;
	/* TODO: The policy for eviction is up to you. */

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame(void)
{
	struct frame *victim UNUSED = vm_get_victim();
	/* TODO: swap out the victim and return the evicted frame. */
	// 비우고자 하는 해당 프레임을 victim이라 하고, 이 victim과 연결된 가상 페이지를 swap_out()에 인자로 넣어준다.
	swap_out(victim->page);

	return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/

// user pool에서 새 물리적 페이지를 가져오는 함수
// palloc을 이용해 프레임을 할당받아옴. 만약 가용 가능한 페이지가 없다면 페이지를 스왑하고 frame 공간을 디스크로 내린다.
static struct frame *
vm_get_frame(void) // heesan 구현
{
	struct frame *frame = (struct frame *)malloc(sizeof(struct frame));

	ASSERT(frame != NULL);
	ASSERT(frame->page == NULL);
	/* TODO: Fill this function. */

	// user pool에서 커널 가상 주소 공간으로 1page 할당
	frame->kva = palloc_get_page(PAL_USER);

	if (frame->kva == NULL)
	{							  // 유저 풀 공간이 하나도 없다면
		frame = vm_evict_frame(); // 새로운 프레임을 할당 받는다.
		frame->page = NULL;
		return frame;
	}
	list_push_back(&frame_table, &frame->frame_elem);

	frame->page = NULL;

	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth(void *addr UNUSED)
{
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp(struct page *page UNUSED)
{
}

/* Return true on success */
bool vm_try_handle_fault(struct intr_frame *f UNUSED, void *addr UNUSED,
						 bool user UNUSED, bool write UNUSED, bool not_present UNUSED)
{
	struct supplemental_page_table *spt UNUSED = &thread_current()->spt;
	struct page *page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */

	return vm_do_claim_page(page);
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void vm_dealloc_page(struct page *page)
{
	destroy(page);
	free(page);
}

/* Claim the page that allocate on VA. */
bool vm_claim_page(void *va UNUSED)
{

	struct page *page = NULL;
	struct thread *curr = thread_current();
	/* TODO: Fill this function */
	page = spt_find_page(&curr->spt, va);

	if (page == NULL)
	{
		return false;
	}
	return vm_do_claim_page(page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page(struct page *page)
{ // 가상 주소와 물리 주소 매핑( 성공, 실패 여부 리턴)
	struct frame *frame = vm_get_frame();
	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	// 페이지랑 프레임이랑 연결시켜주는 함수
	if (install_page(page->va, frame->kva, page->writable))
	{
		return swap_in(page, frame->kva); // heesan??
	}
	return false;
}

/* Initialize new supplemental page table */
void supplemental_page_table_init(struct supplemental_page_table *spt UNUSED)
{	// 후반부
	struct hash* page_table = malloc(sizeof(struct hash)); // page_table에 메모리 할당
	hash_init(page_table, page_hash, page_less, NULL); // 해시테이블 초기화

	// 왜 빨간줄??
	spt->spt_hash = page_table; // spt에 해당 page_table 연결
	// spt 초기화 끝
}

/* Copy supplemental page table from src to dst */
bool supplemental_page_table_copy(struct supplemental_page_table *dst UNUSED,
struct supplemental_page_table *src UNUSED)
{

}

/* Free the resource hold by the supplemental page table */
void supplemental_page_table_kill(struct supplemental_page_table *spt UNUSED)
{
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
}

/* Returns a hash value for page p. */
// 해시 테이블 초기화할 때 해시 값을 구해주는 함수의 포인터
unsigned
page_hash(const struct hash_elem *p_, void *aux UNUSED)
{
	const struct page *p = hash_entry(p_, struct page, hash_elem);
	return hash_bytes(&p->va, sizeof p->va);
	// hash_bytes : buf에서 시작하는 크기 바이트의 해시를 반환합니다.
}

/* Returns true if page a precedes page b. */
// 해시 테이블 초기화할 때 해시 값을 구해주는 함수의 포인터
// a가 b보다 작으면 true, 반대면 false
bool page_less(const struct hash_elem *a_,
			   const struct hash_elem *b_, void *aux UNUSED)
{
	const struct page *a = hash_entry(a_, struct page, hash_elem);
	const struct page *b = hash_entry(b_, struct page, hash_elem);

	return a->va < b->va;
}
