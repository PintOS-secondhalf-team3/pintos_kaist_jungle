/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "lib/kernel/hash.h"
#include "include/threads/thread.h"
#include "userprog/process.h"

//-------project3-memory_management-start--------------
struct list frame_table;	// frame_table을 전역으로 선언함
//-------project3-memory_management-end----------------

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
	list_init(&frame_table); // frame_table 리스트를 초기화

	// heesan 왜 여기서 start에 frame_table의 첫 요소를 할당해주었는가?????
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
		// uninit_new 6번째 인자에는 생김새가 같은 인자가 들어가야 함.
		uninit_new(page, upage, init, type, aux, initializer);
		page->writable = writable;
		// TODO: Insert the page into the spt.
		return spt_insert_page(spt, page);
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
// 인자로 받은 va(가상 주소)에 해당하는 페이지 번호를 spt에서 검색하여 적절한 page를 찾는 함수
struct page *
spt_find_page(struct supplemental_page_table *spt UNUSED, void *va UNUSED)
// va를 기준으로 hash_table에서 elem을 찾는다
{
	//-------project3-memory_management-start--------------
	struct page *page = page_lookup(va); 
	//-------project3-memory_management-end----------------
	/* TODO: Fill this function. */
	
	return page;
}

//-------project3-memory_management-start--------------
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

bool spt_insert_page(struct supplemental_page_table *spt UNUSED, struct page *page UNUSED)
{ // 인자로 주어진 page를 spt에 넣는 함수. 이미 spt에 있는 page인지도 검증해야 함.

	int succ = false;
	/* TODO: Fill this function. */

	// hash_insert: spt에 page를 새로 넣었으면 NULL 반환, 이미 spt에 page가 존재한다면 해당 page의 hash_elem을 반환
	struct hash_elem *p = hash_insert(&spt->spt_hash, &page->hash_elem);
	if (p == NULL)
		succ = true;
	
	return succ;
}
//-------project3-memory_management-end----------------


void spt_remove_page(struct supplemental_page_table *spt, struct page *page)
{
	vm_dealloc_page(page);
	return true;
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

//-------project3-memory_management-start--------------
/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
/* user pool에서 새로운 physical page를 palloc_get_page()를 통해 얻어오는 함수
   그리고 이를 물리 메모리의 frame과 연결
   아직은 swap out할 필요 없고, PANIC ("todo")를 넣으면 됨
		// 만약 가용 가능한 페이지가 없다면 페이지를 스왑하고 frame 공간을 디스크로 내린다.
*/
static struct frame *
vm_get_frame(void) 
{
	// 새로운 frame 만들기
	struct frame *frame = (struct frame *)malloc(sizeof(struct frame));

	ASSERT(frame != NULL);
	ASSERT(frame->page == NULL);
	/* TODO: Fill this function. */

	// physical memory의 user pool에서 1page를 할당하고, 이에 해당하는 kva를 반환
	frame->kva = palloc_get_page(PAL_USER);	// 새로 만든 frame과 새로 할당받은 page를 연결

	if (frame->kva == NULL) // 유저 풀 공간이 하나도 없다면
	{
		PANIC ("todo");
		// frame = vm_evict_frame(); // 새로운 프레임을 할당
		// frame->page = NULL;
		// return frame;
	}
	list_push_back(&frame_table, &frame->frame_elem);	// frame table 리스트에 frame elem을 넣음

	frame->page = NULL;	// frame의 page멤버 초기화

	return frame;
}
//-------project3-memory_management-end----------------

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
// page fault는 user program이 진행되면서 program이 물리 메모리에 있을거라고 생각하면서
// 접근 하는데 실제로는 원하는 데이터가 물리 메모리에 load 혹은 저장되어있지 않을 경우 발생함
bool vm_try_handle_fault(struct intr_frame *f UNUSED, void *addr UNUSED, bool user UNUSED, bool write UNUSED, bool not_present UNUSED)
{ // 후반부 요거 구현하면 fork제외 project2 전부 통과해야 함
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

//-------project3-memory_management-start--------------

/* Claim the page that allocate on VA. */
// 할당할 페이지를 요청함.
bool vm_claim_page(void *va UNUSED)
{ 
	struct page *page = NULL;
	struct thread *curr = thread_current();
	/* TODO: Fill this function */
	page = spt_find_page(&curr->spt, va); // 먼저 페이지를 가져온다.

	if (page == NULL)
	{
		return false;
	}
	return vm_do_claim_page(page); // 해당 페이지와 함께 vm_do_claim_page를 호출
}

/* Claim the PAGE and set up the mmu. */
/* claim : 물리 프레임을 페이지에 할당하는 것
   vm_get_frame()을 호출하여 frame을 얻고 MMU를 설정
*/
static bool
vm_do_claim_page(struct page *page)
{ // 가상 주소와 물리 주소 매핑( 성공, 실패 여부 리턴)
	struct frame *frame = vm_get_frame();
	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	// install_page: kva와 va를 연결해주는 함수
	// -> 가상 주소에서 페이지테이블의 물리적 주소로 매핑
	if (install_page(page->va, frame->kva, page->writable))
	{
		return swap_in(page, frame->kva); // 우니스 블로그 보고 함, 왜 인지 아직 이해 못 함???????
										// swap in -> disk(swap area)에서 메모리로 데이터 가져옴
										// page fault나면 lazy loading 시작
	}
	return false;
}
//-------project3-memory_management-end----------------

/* Initialize new supplemental page table */
void supplemental_page_table_init(struct supplemental_page_table *spt UNUSED)
{					
	//-------project3-memory_management-start--------------									   
	struct hash *page_table = malloc(sizeof(struct hash)); // page_table에 메모리 할당
	hash_init(page_table, page_hash, page_less, NULL);	   // 해시테이블 초기화

	spt->spt_hash = page_table; // spt에 해당 page_table 연결
	//-------project3-memory_management-end----------------
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
// hash 테이블 자료구조에서 key 값을 받아 bucket 안의 index로 변형시키는 function
unsigned
page_hash(const struct hash_elem *p_, void *aux UNUSED)
{
	const struct page *p = hash_entry(p_, struct page, hash_elem);
	return hash_bytes(&p->va, sizeof p->va);
	// hash_bytes : buf에서 시작하는 크기 바이트의 해시를 반환함
}

/* Returns true if page a precedes page b. */
// hash 자료구조에서 elem들의 값을 비교해 a, b중 a가 더 작은지 아닌지를 return하는 함수
// a가 b보다 작으면 true, 반대면 false
bool page_less(const struct hash_elem *a_,
			   const struct hash_elem *b_, void *aux UNUSED)
{
	const struct page *a = hash_entry(a_, struct page, hash_elem);
	const struct page *b = hash_entry(b_, struct page, hash_elem);

	return a->va < b->va;
}
