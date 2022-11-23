#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* Random value for basic thread
   Do not modify this value. */
#define THREAD_BASIC 0xd42df210

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

static struct list sleep_list;

int64_t next_tick_to_awake = INT64_MAX;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Thread destruction requests */
static struct list destruction_req;

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* 각 스레드를 제공하는 시간 눈금 */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule (void);
static tid_t allocate_tid (void);

/* Returns true if T appears to point to a valid thread. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* Returns the running thread.
 * Read the CPU's stack pointer `rsp', and then round that
 * down to the start of a page.  Since `struct thread' is
 * always at the beginning of a page and the stack pointer is
 * somewhere in the middle, this locates the curent thread. */
#define running_thread() ((struct thread *) (pg_round_down (rrsp ())))


// Global descriptor table for the thread_start.
// Because the gdt will be setup after the thread_init, we should
// setup temporal gdt first.
static uint64_t gdt[3] = { 0, 0x00af9a000000ffff, 0x00cf92000000ffff };

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) {
	ASSERT (intr_get_level () == INTR_OFF);

	/* Reload the temporal gdt for the kernel
	 * This gdt does not include the user context.
	 * The kernel will rebuild the gdt with user context, in gdt_init (). */
	struct desc_ptr gdt_ds = {
		.size = sizeof (gdt) - 1,
		.address = (uint64_t) gdt
	};
	lgdt (&gdt_ds);

	/* Init the globla thread context */
	lock_init (&tid_lock);
	list_init (&ready_list);
	list_init (&destruction_req);
	list_init (&sleep_list);

	/* Set up a thread structure for the running thread. */
	initial_thread = running_thread ();
	init_thread (initial_thread, "main", PRI_DEFAULT);
	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) {
	/* Create the idle thread. */
	struct semaphore idle_started;
	sema_init (&idle_started, 0);
	thread_create ("idle", PRI_MIN, idle, &idle_started);

	/* Start preemptive thread scheduling. */
	intr_enable ();

	/* Wait for the idle thread to initialize idle_thread. */
	sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) {
	struct thread *t = thread_current ();

	/* Update statistics. */
	if (t == idle_thread)
		idle_ticks++;
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else
		kernel_ticks++;

	/* 선점 시행 */
	if (++thread_ticks >= TIME_SLICE)
		intr_yield_on_return ();
}

/* Prints thread statistics. */
void
thread_print_stats (void) {
	printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
			idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
		thread_func *function, void *aux) {
	struct thread *t;
	tid_t tid;

	ASSERT (function != NULL);

	/* Allocate thread. */
	t = palloc_get_page (PAL_ZERO);
	if (t == NULL)
		return TID_ERROR;

	/* Initialize thread. */
	init_thread (t, name, priority);
	tid = t->tid = allocate_tid ();

	/* Call the kernel_thread if it scheduled.
	 * Note) rdi is 1st argument, and rsi is 2nd argument. */
	t->tf.rip = (uintptr_t) kernel_thread;
	t->tf.R.rdi = (uint64_t) function;
	t->tf.R.rsi = (uint64_t) aux;
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;

	// t->parent_fd = thread_current()->tid; 	/* 부모 프로세스 저장 */
	// t->is_mem_load = false;	/* 프로그램이 로드되지 않음 */
	// t->is_proc_off = false;	/* 프로세스가 종료되지 않음 */
	// // sema_init(&t->sema_exit, 0); /* exit 세마포어 0으로 초기화 */ 
	// // sema_init(&t->sema_load, 0); /* load 세마포어 0으로 초기화 */ 
	// t->child_elem;
	// t->childs ;/* 자식 리스트에 추가 */

	/* 실행 대기열에 추가 */
	thread_unblock (t);
	/* 현재 수행중인 스레드와 가장 높은 우선순위의 스레드의 우선순위를 비교하여 스케줄링 */
	test_max_priority();	
	return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) {
	ASSERT (!intr_context ());
	ASSERT (intr_get_level () == INTR_OFF);
	thread_current ()->status = THREAD_BLOCKED;
	schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
/* 해당 thread를 우선순위 정렬하여 ready list에 넣고 status도 ready로 옮겨줌 */
void
thread_unblock (struct thread *t) {
	enum intr_level old_level;

	ASSERT (is_thread (t));

	old_level = intr_disable ();
	ASSERT (t->status == THREAD_BLOCKED);
	list_insert_ordered(&ready_list, &t->elem, cmp_priority, NULL);
	t->status = THREAD_READY;
	intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void) {
	return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) {
	struct thread *t = running_thread ();

	/* Make sure T is really a thread.
	   If either of these assertions fire, then your thread may
	   have overflowed its stack.  Each thread has less than 4 kB
	   of stack, so a few big automatic arrays or moderate
	   recursion can cause stack overflow. */
	ASSERT (is_thread (t));
	ASSERT (t->status == THREAD_RUNNING);

	return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) {
	return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) {
	ASSERT (!intr_context ());

#ifdef USERPROG
	process_exit ();
#endif

	/* Just set our status to dying and schedule another process.
	   We will be destroyed during the call to schedule_tail(). */
	intr_disable ();
	// list_remove(&thread_current()->allelem);
	do_schedule (THREAD_DYING);
	NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
/* 무조건 run thread 재우고 ready list 우선순위 높은 thread 실행 */
void
thread_yield (void) {
	struct thread *curr = thread_current ();
	enum intr_level old_level;

	ASSERT (!intr_context ());

	old_level = intr_disable ();		 /* interrupt 비활성화 */
	if (curr != idle_thread)
		list_insert_ordered(&ready_list, &curr->elem, cmp_priority, NULL); /* 현재 thread가 CPU를 양보하여 ready_list에 삽입 될 때 
	우선순위 순서로 정렬되어 삽입 되도록 수정 */
	do_schedule (THREAD_READY);			/* running thread 를 ready로 바꾸고 다음 thread를 running으로 바꿈 : 컨텍스트 스위치 작업을 수행 */
	intr_set_level (old_level);			/* interrupt 못받는 상태로 설정하고, 이전 인터럽트 상태 반환 */
}

/* next_tick_to_awake가 깨워야 할 스레드 중 가장 작은 tick을 갖도록 업데이트 */
 void update_next_tick_to_awake(int64_t ticks) { // ticks = 최소값 
	if (next_tick_to_awake > ticks){
		next_tick_to_awake = ticks;
	}
}


/* next_tick_to_awake 을 반환. */ 
int64_t get_next_tick_to_awake(void) {
	return next_tick_to_awake;
}


/* Thread를 blocked 상태로 만들고 sleep queue에 삽입하여 대기 */
void
thread_sleep(int64_t ticks){ 				/* ticks = 현재 시간 + 재울 시간 = 깨어날 시간 */ 
	struct thread *curr = thread_current(); /* 현재 쓰레드 */
	enum intr_level old_level;				/* 인터럽트 상태 저장할 변수 */

	ASSERT (!intr_context ());

	old_level = intr_disable ();			/* 인터럽트를 사용하지 않도록 설정하고 이전 인터럽트 상태를 반환  */
	
	curr->wakeup_tick = ticks;				/* 현재 쓰레드의 wakeup_tick에 ticks 저장*/
	if (curr != idle_thread){				/* idle_thread는 sleep list에 넣지 않음 */
		list_push_back (&sleep_list, &curr->elem);
		update_next_tick_to_awake(ticks);	/* awake함수가 실행되어야 할 tick값을 update */
		do_schedule (THREAD_BLOCKED);		/* running thread 를 block으로 바꾸고 다음 thread를 running으로 바꿈 : 컨텍스트 스위치 작업을 수행 */
	}
	intr_set_level (old_level);				/* 인터럽트를 다시 받아들이도록 수정 */
}


/* Sleep queue에서 깨워야 할 thread를 찾아서 wake */
void thread_awake(int64_t ticks){ 			/* ticks = 현재 시간 */
	int64_t next_tick_to_awake = INT64_MAX; /* 쓰레드가 일어나면 sleep list 변할 거라 최소 시간 갱신 */
	/* sleep list의 모든 entry 를 순회 */
	struct list_elem *cur = list_begin(&sleep_list);
	while(cur != list_end(&sleep_list)){
		struct thread *t = list_entry(cur, struct thread, elem); /* cur의 structure 포인터 반환 */
		if (ticks >= t->wakeup_tick){	/* 현재 시간이 t의 wakeup_tick보다 크거나 같으면 */
			/* 슬립 큐에서 제거하고 unblock */
			cur = list_remove(&t->elem);
			thread_unblock(t);
		}else{	/* 현재 시간이 t의 wakeup_tick보다 작으면 update_next_tick_to_awake()를 호출 */ 
			update_next_tick_to_awake(t->wakeup_tick);	/* 최소 틱을 next_tick_to_awak에 update */
			cur = list_next(cur);						/* cur를 다음 cur로 변경 */
		}
	}
}


/* ready_list에서 우선순위가 가장 높은 스레드와 현재 스레드의 우선순위를 비교하여 스케줄링 */
void test_max_priority (void){
	struct thread *curr = thread_current ();
	if(!list_empty(&ready_list)){	/* ready_list 가 비어있지 않은지 확인 : 비어있으면 error */
		struct list_elem *high = list_begin(&ready_list);	/* ready list에서 우선순위 제일 높은 것 */
		struct thread *t = list_entry(high, struct thread, elem); /* high의 structure 포인터 반환 */
		if(t->priority > curr->priority){	/* ready list에서 제일 높은 우선순위가 현재 스레드보다 높다면 */
			thread_yield();					/*무조건 run thread 재우고 ready list 우선순위 높은 thread 실행 */
		}
	}else{
		return;
	} 
}


/* 인자로 주어진 스레드들의 우선순위를 비교 */
bool cmp_priority (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED){
	/* list_insert_ordered() 함수에서 사용 하기 위해 정열 방법을 결정하기 위한 함수 작성 */
	struct thread *cmp_a = list_entry(a, struct thread, elem);
	struct thread *cmp_b = list_entry(b, struct thread, elem);     

	if(cmp_a->priority > cmp_b->priority){	// a thread가 우선순위가 높으면 1 반환 
		return true;
	}else{
		return false;
	}
}

/* 인자로 주어진 스레드의 donation_elem의 우선순위를 비교 */
bool cmp_don_priority (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED){
	return list_entry(a, struct thread, donation_elem)->priority > list_entry(b,struct thread, donation_elem)->priority;
}

/* Sets the current thread's priority to NEW_PRIORITY. */
/* 현재 스레드의 우선 순위를 인자로 받은 NEW_PRIORITY로 설정 */
void
thread_set_priority (int new_priority) {
	thread_current()->init_priority = new_priority;
	refresh_priority();		/* 우선순위를 변경으로 인한 donation 관련 정보를 갱신*/
	test_max_priority();	/* 우선순위에 따라 선점이 발생하도록 */
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) {
	return thread_current ()->priority;
}


/* priority donation 을 수행하는 함수.*/
void donate_priority(void){
	/* 현재 스레드가 기다리고 있는 lock 과 연결 된 모든 스레드들을 순회하며 
	   현재 스레드의 우선순위를 lock 을 보유하고 있는 스레드에게 기부. */
	struct thread *cur = thread_current();
	struct thread *donated_elem = cur;
	int nested_depth = 0 ;

	while(donated_elem->wait_on_lock != NULL && nested_depth < 8 ){	/* (Nested donation 그림 참고, nested depth 는 8로 제한한다. ) */
		donated_elem = donated_elem->wait_on_lock->holder;
		if (donated_elem->priority < cur->priority){
			donated_elem->priority = cur->priority;
			nested_depth ++;
		}
	} 
}


/* lock 을 해지 했을때 donations 리스트에서 해당 엔트리를 삭제 하기 위한 함수 */
void remove_with_lock(struct lock *lock){
	struct thread *cur = thread_current();
	struct list_elem *e;

	for (e=list_begin(&cur->donations); e!=list_end(&cur->donations); e=list_next(e)){
		struct thread *t = list_entry(e, struct thread, donation_elem); /* elme -> donation_elem 으로 체인지 후 pass tests/threads/priority-donate-one*/
		if(t->wait_on_lock == lock){		/* 해지할 lock을 보유하고 있으면 */
			list_remove(&t->donation_elem);		/* 보유하고 있는 엔트리를 삭제 - list_remove dont do that 해결 */ 
		}
	}
}


/* 스레드의 우선순위가 변경 되었을때 donation 을 고려하여 우선순위를 다시 결정 하는 함수 */
void refresh_priority(void){
	/* 현재 스레드의 우선순위를 기부받기 전의 우선순위로 변경 */
	struct thread *cur = thread_current();
	cur->priority = cur->init_priority;

	/* 가장 우선순위가 높은 donations 리스트의 스레드와
	현재 스레드의 우선순위를 비교하여 높은 값을 현재 스레드의 우선순위로 설정 */
	if(list_empty(&cur->donations)== false){
		list_sort(&cur->donations, cmp_don_priority, NULL);
		struct thread *t = list_entry(list_front(&cur->donations), struct thread, donation_elem);
		if(t->priority > cur->priority){	/* 가장 우선순위가 높은 donations 리스트의 스레드가 현재 스레드의 우선순위보다 높으면*/
			cur->priority = t->priority;
		}
	} 
}


/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED) {
	/* TODO: Your implementation goes here */
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) {
	/* TODO: Your implementation goes here */
	return 0;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) {
	/* TODO: Your implementation goes here */
	return 0;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) {
	/* TODO: Your implementation goes here */
	return 0;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) {
	struct semaphore *idle_started = idle_started_;

	idle_thread = thread_current ();
	sema_up (idle_started);

	for (;;) {
		/* Let someone else run. */
		intr_disable ();
		thread_block ();

		/* Re-enable interrupts and wait for the next one.

		   The `sti' instruction disables interrupts until the
		   completion of the next instruction, so these two
		   instructions are executed atomically.  This atomicity is
		   important; otherwise, an interrupt could be handled
		   between re-enabling interrupts and waiting for the next
		   one to occur, wasting as much as one clock tick worth of
		   time.

		   See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
		   7.11.1 "HLT Instruction". */
		asm volatile ("sti; hlt" : : : "memory");
	}
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) {
	ASSERT (function != NULL);

	intr_enable ();       /* The scheduler runs with interrupts off. */
	function (aux);       /* Execute the thread function. */
	thread_exit ();       /* If function() returns, kill the thread. */
}


/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority) {
	ASSERT (t != NULL);
	ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT (name != NULL);

	memset (t, 0, sizeof *t);
	t->status = THREAD_BLOCKED;
	strlcpy (t->name, name, sizeof t->name);
	t->tf.rsp = (uint64_t) t + PGSIZE - sizeof (void *);
	t->priority = priority;
	t->magic = THREAD_MAGIC;
	// list_push_back (&all_list, &t->allelem);			/*악깡버*/

	/* Priority donation 관련 자료구조 초기화 */
	t->init_priority = priority;
	t->wait_on_lock = NULL;
	list_init (&t->donations);
	// list_init (&t->childs);				/* 자식 리스트 초기화 */
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) {
	if (list_empty (&ready_list))
		return idle_thread;
	else
		return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Use iretq to launch the thread */
void
do_iret (struct intr_frame *tf) {
	__asm __volatile(
			"movq %0, %%rsp\n"
			"movq 0(%%rsp),%%r15\n"
			"movq 8(%%rsp),%%r14\n"
			"movq 16(%%rsp),%%r13\n"
			"movq 24(%%rsp),%%r12\n"
			"movq 32(%%rsp),%%r11\n"
			"movq 40(%%rsp),%%r10\n"
			"movq 48(%%rsp),%%r9\n"
			"movq 56(%%rsp),%%r8\n"
			"movq 64(%%rsp),%%rsi\n"
			"movq 72(%%rsp),%%rdi\n"
			"movq 80(%%rsp),%%rbp\n"
			"movq 88(%%rsp),%%rdx\n"
			"movq 96(%%rsp),%%rcx\n"
			"movq 104(%%rsp),%%rbx\n"
			"movq 112(%%rsp),%%rax\n"
			"addq $120,%%rsp\n"
			"movw 8(%%rsp),%%ds\n"
			"movw (%%rsp),%%es\n"
			"addq $32, %%rsp\n"
			"iretq"
			: : "g" ((uint64_t) tf) : "memory");
}

/* Switching the thread by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function. */
static void
thread_launch (struct thread *th) {
	uint64_t tf_cur = (uint64_t) &running_thread ()->tf;
	uint64_t tf = (uint64_t) &th->tf;
	ASSERT (intr_get_level () == INTR_OFF);

	/* The main switching logic.
	 * We first restore the whole execution context into the intr_frame
	 * and then switching to the next thread by calling do_iret.
	 * Note that, we SHOULD NOT use any stack from here
	 * until switching is done. */
	__asm __volatile (
			/* Store registers that will be used. */
			"push %%rax\n"
			"push %%rbx\n"
			"push %%rcx\n"
			/* Fetch input once */
			"movq %0, %%rax\n"
			"movq %1, %%rcx\n"
			"movq %%r15, 0(%%rax)\n"
			"movq %%r14, 8(%%rax)\n"
			"movq %%r13, 16(%%rax)\n"
			"movq %%r12, 24(%%rax)\n"
			"movq %%r11, 32(%%rax)\n"
			"movq %%r10, 40(%%rax)\n"
			"movq %%r9, 48(%%rax)\n"
			"movq %%r8, 56(%%rax)\n"
			"movq %%rsi, 64(%%rax)\n"
			"movq %%rdi, 72(%%rax)\n"
			"movq %%rbp, 80(%%rax)\n"
			"movq %%rdx, 88(%%rax)\n"
			"pop %%rbx\n"              // Saved rcx
			"movq %%rbx, 96(%%rax)\n"
			"pop %%rbx\n"              // Saved rbx
			"movq %%rbx, 104(%%rax)\n"
			"pop %%rbx\n"              // Saved rax
			"movq %%rbx, 112(%%rax)\n"
			"addq $120, %%rax\n"
			"movw %%es, (%%rax)\n"
			"movw %%ds, 8(%%rax)\n"
			"addq $32, %%rax\n"
			"call __next\n"         // read the current rip.
			"__next:\n"
			"pop %%rbx\n"
			"addq $(out_iret -  __next), %%rbx\n"
			"movq %%rbx, 0(%%rax)\n" // rip
			"movw %%cs, 8(%%rax)\n"  // cs
			"pushfq\n"
			"popq %%rbx\n"
			"mov %%rbx, 16(%%rax)\n" // eflags
			"mov %%rsp, 24(%%rax)\n" // rsp
			"movw %%ss, 32(%%rax)\n"
			"mov %%rcx, %%rdi\n"
			"call do_iret\n"
			"out_iret:\n"
			: : "g"(tf_cur), "g" (tf) : "memory"
			);
}

/* Schedules a new process. At entry, interrupts must be off.
 * This function modify current thread's status to status and then
 * finds another thread to run and switches to it.
 * It's not safe to call printf() in the schedule(). */
static void
do_schedule(int status) {
	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (thread_current()->status == THREAD_RUNNING);
	while (!list_empty (&destruction_req)) {
		struct thread *victim =
			list_entry (list_pop_front (&destruction_req), struct thread, elem);
		palloc_free_page(victim);
	}
	thread_current ()->status = status;
	schedule ();
}

static void
schedule (void) {
	struct thread *curr = running_thread ();
	struct thread *next = next_thread_to_run (); // ready list가 비어있으면 idle_thread 반환

	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (curr->status != THREAD_RUNNING);
	ASSERT (is_thread (next));
	/* Mark us as running. */
	next->status = THREAD_RUNNING;

	/* Start new time slice. */
	thread_ticks = 0;

#ifdef USERPROG
	/* Activate the new address space. */
	process_activate (next);
#endif

	if (curr != next) {
		/* If the thread we switched from is dying, destroy its struct
		   thread. This must happen late so that thread_exit() doesn't
		   pull out the rug under itself.
		   We just queuing the page free reqeust here because the page is
		   currently used bye the stack.
		   The real destruction logic will be called at the beginning of the
		   schedule(). */
		if (curr && curr->status == THREAD_DYING && curr != initial_thread) {
			ASSERT (curr != next);
			list_push_back (&destruction_req, &curr->elem);
		}

		/* Before switching the thread, we first save the information
		 * of current running. */
		thread_launch (next);
	}
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) {
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire (&tid_lock);
	tid = next_tid++;
	lock_release (&tid_lock);

	return tid;
}
