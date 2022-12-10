#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#include "include/vm/file.h"
#ifdef VM
#include "vm/vm.h"
// --------------------project3 Anonymous Page start---------
#include "vm/file.h"
// --------------------project3 Anonymous Page end---------
#endif

static void process_cleanup(void);
static bool load(const char *file_name, struct intr_frame *if_);
static void initd(void *f_name);
static void __do_fork(void *);
struct thread *get_child(int pid);

struct lock file_lock;

/* General process initializer for initd and other process. */
static void
process_init(void)
{
	struct thread *current = thread_current();
}

/* Starts the first userland program, called "initd", loaded from FILE_NAME.
 * The new thread may be scheduled (and may even exit)
 * before process_create_initd() returns. Returns the initd's
 * thread id, or TID_ERROR if the thread cannot be created.
 * Notice that THIS SHOULD BE CALLED ONCE. */
tid_t process_create_initd(const char *file_name)
{
	char *fn_copy;
	tid_t tid;

	/* Make a copy of FILE_NAME.
	 * Otherwise there's a race between the caller and load(). */
	fn_copy = palloc_get_page(0);
	if (fn_copy == NULL)
		return TID_ERROR;
	strlcpy(fn_copy, file_name, PGSIZE);

	char *token, *ptr;
	for (token = strtok_r(file_name, " ", &ptr); token != NULL; token = strtok_r(NULL, " ", &ptr))
		;

	/* Create a new thread to execute FILE_NAME. */
	tid = thread_create(file_name, PRI_DEFAULT, initd, fn_copy);
	if (tid == TID_ERROR)
		palloc_free_page(fn_copy);
	return tid;
}

/* A thread function that launches first user process. */
static void
initd(void *f_name)
{
#ifdef VM
	supplemental_page_table_init(&thread_current()->spt);
#endif

	process_init();

	if (process_exec(f_name) < 0)
		PANIC("Fail to launch initd\n");
	NOT_REACHED();
}

/* 자식 리스트를 pid로 검색하여 해당 프로세스 디스크립터를 반환 & pid가 없을 경우 NULL 반환 */
struct thread *get_child(int pid)
{
	/* 자식 리스트에 접근하여 프로세스 디스크립터 검색 */
	struct thread *cur = thread_current();
	// 자식 리스트에서 pid에 맞는 list_elem 찾기
	struct list_elem *child = list_begin(&cur->childs);
	while (child != list_end(&cur->childs))
	{
		struct thread *target = list_entry(child, struct thread, child_elem);
		if (target->tid == pid)
		{ /* 해당 pid가 존재하면 프로세스 디스크립터 반환 */
			return target;
		}
		else
		{
			child = list_next(child);
		}
	}
	return NULL; /* 리스트에 존재하지 않으면 NULL 리턴 */
}

/* 부모 프로세스의 자식 리스트에서 프로세스 디스크립터 제거 & 프로세스 디스크립터 메모리 해제
 */
// void remove_child_process(struct thread *cp)
// {
// 	/* 자식 리스트에서 제거*/
// 	list_remove(&cp->child_elem);
// 	/* 프로세스 디스크립터 메모리 해제 */
// }

/* Clones the current process as `name`. Returns the new process's thread id, or
 * TID_ERROR if the thread cannot be created. */
/* 인터럽트 프레임 : 인터럽트가 호출됐을 때 이전에 레지스터에 작업하던 context 정보를 스택에 담는 구조체
				 부모 프로세스가 갖고 있던 레지스터 정보를 담아 고대로 복사해야 해서 */
tid_t process_fork(const char *name, struct intr_frame *if_ UNUSED)
{
	/* Clone current thread to new thread.*/
	/* project 2 fork */
	struct thread *cur = thread_current();
	memcpy(&cur->parent_if, if_, sizeof(struct intr_frame)); //  parent_if에는 유저 스택 정보 담기
	/* 자식 프로세스 생성 */
	tid_t pid = thread_create(name, PRI_DEFAULT, __do_fork, cur); // 마지막에 thread_current를 줘서, 같은 rsi를 공유하게 함
	if (pid == TID_ERROR)
	{
		return TID_ERROR;
	}
	struct thread *child = get_child(pid);

	sema_down(&child->fork_sema); // fork_sema가 1이 될 때까지(=자식 스레드 load 완료될 때까지) 기다렸다가 // 부모 얼음
	if (child->exit_status == -1)
	{
		return TID_ERROR;
	}
	return pid; // 끝나면 pid 반환
}

#ifndef VM
/* Duplicate the parent's address space by passing this function to the
 * pml4_for_each. This is only for the project 2. */
/* 이 함수를 pml4_for_each에 전달하여 부모의 주소 공간을 복제 */
static bool
duplicate_pte(uint64_t *pte, void *va, void *aux)
{
	struct thread *current = thread_current();
	struct thread *parent = (struct thread *)aux;
	void *parent_page;
	void *newpage;
	bool writable;

	/* 1. TODO: If the parent_page is kernel page, then return immediately.
	부모의 page가 kernel page인 경우 즉시 리턴 */
	if (is_kernel_vaddr(va))
	{
		return true;
	}
	/* 2. Resolve VA from the parent's page map level 4.
	부모 스레드 내 멤버인 pml4를 이용해 부모 페이지를 불러온다. 이때, pml4_get_page() 함수를 이용한다.*/
	parent_page = pml4_get_page(parent->pml4, va);
	if (parent_page == NULL)
	{
		return false;
	}

	/* 3. TODO: Allocate new PAL_USER page for the child and set result to
	 *    TODO: NEWPAGE.
		자식에 새로운 PAL_USER 페이지를 할당하고 결과를 newpage에 저장한다.*/
	newpage = palloc_get_page(PAL_USER | PAL_ZERO);
	if (newpage == NULL)
	{
		return false;
	}

	/* 4. TODO: Duplicate parent's page to the new page and
	 *    TODO: check whether parent's page is writable or not (set WRITABLE
	 *    TODO: according to the result).
	 * 부모 페이지를 복사해 3에서 새로 할당받은 페이지에 넣어준다.
	 * 이때 부모 페이지가 writable인지 아닌지 확인하기 위해 is_writable() 함수를 이용한다.*/
	memcpy(newpage, parent_page, PGSIZE);
	writable = is_writable(pte);

	/* 5. Add new page to child's page table at address VA with WRITABLE
	 *    permission.
	 페이지 생성에 실패하면 에러 핸들링이 동작하도록 false를 리턴한다. */
	if (!pml4_set_page(current->pml4, va, newpage, writable))
	{
		/* 6. TODO: if fail to insert page, do error handling. */
		return false;
	}
	return true;
}
#endif

/* A thread function that copies parent's execution context.
 * Hint) parent->tf does not hold the userland context of the process.
 *       That is, you are required to pass second argument of process_fork to
 *       this function. */
/* 부모 프로세스의 실행 컨텍스트를 복사하는 스레드 함수.
   parent->tf는 프로세스의 사용자 및 컨텍스트를 보유하지 않음.
   즉, process_fork의 두 번째 인수를 이 함수에 전달해야함. */
static void
__do_fork(void *aux)
{
	struct intr_frame if_;
	struct thread *parent = (struct thread *)aux;
	struct thread *current = thread_current();
	/* TODO: somehow pass the parent_if. (i.e. process_fork()'s if_) */
	struct intr_frame *parent_if;
	parent_if = &parent->parent_if;
	bool succ = true;

	/* 1. Read the cpu context to local stack. */
	memcpy(&if_, parent_if, sizeof(struct intr_frame));

	/* 2. Duplicate PT */
	current->pml4 = pml4_create();
	if (current->pml4 == NULL)
		goto error;

	process_activate(current);
#ifdef VM
	supplemental_page_table_init(&current->spt);
	if (!supplemental_page_table_copy(&current->spt, &parent->spt))
		goto error;
#else
	// 커널을 포함하여 사용 가능한 각 pte에 부모의 주소 공간을 복제(duplicate_pte)
	if (!pml4_for_each(parent->pml4, duplicate_pte, parent))
		goto error;
#endif

	/* TODO: Your code goes here.
	 * TODO: Hint) To duplicate the file object, use `file_duplicate`
	 * TODO:       in include/filesys/file.h. Note that parent should not return
	 * TODO:       from the fork() until this function successfully duplicates
	 * TODO:       the resources of parent.*/
	/* 파일 개체를 복제하려면 include/filesys/file.h에서 'file_duplicate'를 사용
	  이 함수가 부모의 리소스를 성공적으로 복제할 때까지 부모는 포크()에서 돌아오지 않아야함 */

	if (parent->fdidx == MAX_FD_NUM)
	{
		goto error;
	}

	current->fd_table[0] = parent->fd_table[0];
	current->fd_table[1] = parent->fd_table[1];
	for (int i = 2; i < MAX_FD_NUM; i++)
	{
		struct file *f = parent->fd_table[i];
		if (f == NULL)
		{
			continue;
		}

		current->fd_table[i] = file_duplicate(f);
	}

	current->fdidx = parent->fdidx;
	sema_up(&current->fork_sema);
	if_.R.rax = 0; // 반환값 (자식프로세스가 0을 반환해야 함.)
	process_init();

	/* Finally, switch to the newly created process. */
	if (succ)
		do_iret(&if_);
error:
	current->exit_status = TID_ERROR;
	sema_up(&current->fork_sema);
	exit(TID_ERROR);
	// thread_exit();
}

/* Switch the current execution context to the f_name.
 * Returns -1 on fail. */
int process_exec(void *f_name)
{
	char *file_name = f_name;
	struct intr_frame _if;
	bool success;

	/* We cannot use the intr_frame in the thread structure.
	 * This is because when current thread rescheduled,
	 * it stores the execution information to the member. */
	_if.ds = _if.es = _if.ss = SEL_UDSEG;
	_if.cs = SEL_UCSEG;
	_if.eflags = FLAG_IF | FLAG_MBS;

	/* We first kill the current context */
	process_cleanup(); // 새로운 실행 파일을 현재 스레드에 담기 전에 현재 process에 담긴 context 삭제
	
	#ifdef VM
	// process_cleanup()에서 hash table까지 다 없애주기 때문에 다시 hash table init을 실행해야 함
	supplemental_page_table_init(&thread_current()->spt);
	#endif
	// 필요하지 않은 레지스터까지 0으로 바꿔 "#GP General Protection Exception"; 오류 발생
	// memset(&_if, 0, sizeof(_if)); 

	/* And then load the binary */
	success = load(file_name, &_if); // f_name, if_.rip (function entry point), rsp(stack top : user stack)

	/* If load failed, quit. */
	palloc_free_page(file_name);
	if (!success)
		return -1;
	// hex_dump(_if.rsp, _if.rsp, USER_STACK - _if.rsp, true); // for debugging
	/* Start switched process. */
	do_iret(&_if);
	NOT_REACHED();
}

// /* 자식 리스트를 pid로 검색하여 해당 프로세스 디스크립터를 반환 & pid가 없을 경우 NULL 반환 */
// struct thread *get_child_process (int pid) {
// 	/* 자식 리스트에 접근하여 프로세스 디스크립터 검색 */
// 	struct thread *cur = thread_current();
// 	// 자식 리스트에서 pid에 맞는 list_elem 찾기
// 	struct list_elem *child = list_begin(&cur->childs);
// 	while(child != list_end(&cur->childs)){
// 		struct thread *target = list_entry(child, struct thread, child_elem);
// 		if(target->tid == pid){	/* 해당 pid가 존재하면 프로세스 디스크립터 반환 */
// 			return target;
// 		}else{
// 			child = list_next(child);
// 		}
// 	}
// 	return NULL;	/* 리스트에 존재하지 않으면 NULL 리턴 */
// }

/* 부모 프로세스의 자식 리스트에서 프로세스 디스크립터 제거 & 프로세스 디스크립터 메모리 해제
 */
void remove_child_process(struct thread *cp)
{
	/* 자식 리스트에서 제거*/
	list_remove(&cp->child_elem);
	/* 프로세스 디스크립터 메모리 해제 */
}

/* Waits for thread TID to die and returns its exit status.  If
 * it was terminated by the kernel (i.e. killed due to an
 * exception), returns -1.  If TID is invalid or if it was not a
 * child of the calling process, or if process_wait() has already
 * been successfully called for the given TID, returns -1
 * immediately, without waiting.
 *
 * This function will be implemented in problem 2-2.  For now, it
 * does nothing. */
int process_wait(tid_t child_tid UNUSED)
{
	/* 자식프로세스가 모두 종료될 때까지 대기(sleep state)
	자식 프로세스가 올바르게 종료 됐는지 확인 */
	/* XXX: Hint) The pintos exit if process_wait (initd), we recommend you
	 * XXX:       to add infinite loop here before
	 * XXX:       implementing the process_wait. */

	/* 자식 프로세스의 프로세스 디스크립터 검색 */
	struct thread *child = get_child(child_tid); /* 자식 리스트를 검색하여 프로세스 디스크립터의 주소 리턴 */
	if (child == NULL)
	{ /* 예외 처리 발생시 -1 리턴 */
		return -1;
	}
	/* 자식프로세스가 종료될 때까지 현재(부모) 프로세스 대기(세마포어 이용) */
	// struct thread * cur = thread_current();
	sema_down(&child->wait_sema); // 자식을 sema를 다운시키지만(실제 다운되진 않고), sema_down 속 thread_block보면 현재 (부모)를 block
	/* 자식 프로세스 디스크립터 삭제 */
	/* --------------------누군가가 sema up을 해줘서 부모(자신)가 깸 ---------------------- */
	int exit_status = child->exit_status;
	remove_child_process(child); /* 프로세스 디스크립터를 자식 리스트에서 제거 후 메모리 해제 */
	/* 자식 프로세스의 exit status 리턴 */
	sema_up(&child->free_sema);

	return exit_status;
	// thread_set_priority(thread_get_priority() - 1);
	// return -1;
}

/* Exit the process. This function is called by thread_exit (). */
void process_exit(void)
{
	struct thread *cur = thread_current();
	/* TODO: Your code goes here.
	 * TODO: Implement process termination message (see
	 * TODO: project2/process_termination.html).
	 * TODO: We recommend you to implement process resource cleanup here. */
	for (int i = 0; i < MAX_FD_NUM; i++)
	{
		close(i);
	}

	/* 실행 중인 파일 close */
	file_close(cur->run_file);
	palloc_free_multiple(cur->fd_table, FDT_PAGES); // multi-oom

	// process_cleanup ();  // 밑으로 위치 이동
	/* 프로세스 디스크립터에 프로세스 종료를 알림 */
	sema_up(&cur->wait_sema); // 현재가 자식 wait_sema up
	process_cleanup();
	sema_down(&cur->free_sema);
}

/* Free the current process's resources. */
static void
process_cleanup(void)
{
	struct thread *curr = thread_current();

#ifdef VM
	// supplemental_page_table_kill(&curr->spt);
	if(!hash_empty(&curr->spt.spt_hash)) {
		supplemental_page_table_kill(&curr->spt);
	}
#endif

	uint64_t *pml4;
	/* Destroy the current process's page directory and switch back
	 * to the kernel-only page directory. */
	pml4 = curr->pml4;
	if (pml4 != NULL)
	{
		/* Correct ordering here is crucial.  We must set
		 * cur->pagedir to NULL before switching page directories,
		 * so that a timer interrupt can't switch back to the
		 * process page directory.  We must activate the base page
		 * directory before destroying the process's page
		 * directory, or our active page directory will be one
		 * that's been freed (and cleared). */
		curr->pml4 = NULL;
		pml4_activate(NULL);
		pml4_destroy(pml4);
	}
}

/* Sets up the CPU for running user code in the nest thread.
 * This function is called on every context switch. */
void process_activate(struct thread *next)
{
	/* Activate thread's page tables. */
	pml4_activate(next->pml4);

	/* Set thread's kernel stack for use in processing interrupts. */
	tss_update(next);
}

/* We load ELF binaries.  The following definitions are taken
 * from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
#define EI_NIDENT 16

#define PT_NULL 0			/* Ignore. */
#define PT_LOAD 1			/* Loadable segment. */
#define PT_DYNAMIC 2		/* Dynamic linking info. */
#define PT_INTERP 3			/* Name of dynamic loader. */
#define PT_NOTE 4			/* Auxiliary info. */
#define PT_SHLIB 5			/* Reserved. */
#define PT_PHDR 6			/* Program header table. */
#define PT_STACK 0x6474e551 /* Stack segment. */

#define PF_X 1 /* Executable. */
#define PF_W 2 /* Writable. */
#define PF_R 4 /* Readable. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
 * This appears at the very beginning of an ELF binary. */
struct ELF64_hdr
{
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

struct ELF64_PHDR
{
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
};

/* Abbreviations */
#define ELF ELF64_hdr
#define Phdr ELF64_PHDR

static bool setup_stack(struct intr_frame *if_);
static bool validate_segment(const struct Phdr *, struct file *);

// (arg_list, token_count, if_)
void argument_stack(char **argv, int argc, struct intr_frame *if_)
{
	char *arg_address[128];

	/* 맨 끝 NULL 값(arg[4]) 제외하고 스택에 저장(arg[3]~arg[0]) */
	for (int i = argc - 1; i >= 0; i--)
	{
		int argv_len = strlen(argv[i]); // foo 면 3
		/*
		if_->rsp: 현재 user stack에서 현재 위치를 가리키는 스택 포인터.
		각 인자에서 인자 크기(argv_len)를 읽고 (이때 각 인자에 sentinel이 포함되어 있으니 +1 - strlen에서는 sentinel 빼고 읽음)
		그 크기만큼 rsp를 내려준다. 그 다음 빈 공간만큼 memcpy를 해준다.
		 */
		if_->rsp = if_->rsp - (argv_len + 1);
		memcpy(if_->rsp, argv[i], argv_len + 1);
		arg_address[i] = if_->rsp; // arg_address 배열에 현재 문자열 시작 주소 위치를 저장한다.
	}

	/* word-align: 8의 배수 맞추기 위해 padding 삽입*/
	while (if_->rsp % 8 != 0)
	{
		if_->rsp--;				  // 주소값을 1 내리고
		*(uint8_t *)if_->rsp = 0; //데이터에 0 삽입 => 8바이트 저장
	}

	/* 이제는 주소값 자체를 삽입! 이때 센티넬 포함해서 넣기*/
	for (int i = argc; i >= 0; i--)
	{							 // 여기서는 NULL 값 포인터도 같이 넣는다.
		if_->rsp = if_->rsp - 8; // 8바이트만큼 내리고
		if (i == argc)
		{ // 가장 위에는 NULL이 아닌 0을 넣어야지
			memset(if_->rsp, 0, sizeof(char **));
		}
		else
		{														// 나머지에는 arg_address 안에 들어있는 값 가져오기
			memcpy(if_->rsp, &arg_address[i], sizeof(char **)); // char 포인터 크기: 8바이트
		}
	}
	if_->R.rdi = argc;
	if_->R.rsi = if_->rsp; // arg_address 맨 앞 가리키는 주소값

	/* fake return address */
	if_->rsp = if_->rsp - 8; // void 포인터도 8바이트 크기
	memset(if_->rsp, 0, sizeof(void *));
}

/* Loads an ELF executable from FILE_NAME into the current thread.
 * Stores the executable's entry point into *RIP
 * and its initial stack pointer into *RSP.
 * Returns true if successful, false otherwise. */
static bool
load(const char *file_name, struct intr_frame *if_)
{
	struct thread *t = thread_current();
	struct ELF ehdr;
	struct file *file = NULL;
	off_t file_ofs;
	bool success = false;
	int i;

	char *argv[128]; // 커맨드 라인 길이 제한 128
	char *token, *save_ptr;
	int argc = 0;
	lock_init(&file_lock);

	token = strtok_r(file_name, " ", &save_ptr);
	argv[argc] = token;

	while (token != NULL)
	{
		token = strtok_r(NULL, " ", &save_ptr);
		argc++;
		argv[argc] = token;
	}

	/* Allocate and activate page directory. */
	t->pml4 = pml4_create();
	if (t->pml4 == NULL)
		goto done;
	process_activate(t);

	/* 락 획득 */
	// lock_acquire(&file_lock);
	/* Open executable file. */
	file = filesys_open(file_name);
	if (file == NULL)
	{
		/* 락 해제 */
		// lock_release(&file_lock);
		printf("load: %s: open failed\n", file_name);
		goto done;
	}

	/* thread 구조체의 run_file을 현재 실행할 파일로 초기화 */
	t->run_file = file;
	/* file_deny_write()를 이용하여 파일에 대한 write를 거부 */
	file_deny_write(file);
	/* 락 해제 */
	// lock_release(&file_lock);

	/* Read and verify executable header. */
	if (file_read(file, &ehdr, sizeof ehdr) != sizeof ehdr || memcmp(ehdr.e_ident, "\177ELF\2\1\1", 7) || ehdr.e_type != 2 || ehdr.e_machine != 0x3E // amd64
		|| ehdr.e_version != 1 || ehdr.e_phentsize != sizeof(struct Phdr) || ehdr.e_phnum > 1024)
	{
		printf("load: %s: error loading executable\n", file_name);
		goto done;
	}

	/* Read program headers. */
	file_ofs = ehdr.e_phoff;
	for (i = 0; i < ehdr.e_phnum; i++)
	{
		struct Phdr phdr;

		if (file_ofs < 0 || file_ofs > file_length(file))
			goto done;
		file_seek(file, file_ofs);

		if (file_read(file, &phdr, sizeof phdr) != sizeof phdr)
			goto done;
		file_ofs += sizeof phdr;
		switch (phdr.p_type)
		{
		case PT_NULL:
		case PT_NOTE:
		case PT_PHDR:
		case PT_STACK:
		default:
			/* Ignore this segment. */
			break;
		case PT_DYNAMIC:
		case PT_INTERP:
		case PT_SHLIB:
			goto done;
		case PT_LOAD:
			if (validate_segment(&phdr, file))
			{
				bool writable = (phdr.p_flags & PF_W) != 0;
				uint64_t file_page = phdr.p_offset & ~PGMASK;
				uint64_t mem_page = phdr.p_vaddr & ~PGMASK;
				uint64_t page_offset = phdr.p_vaddr & PGMASK;
				uint32_t read_bytes, zero_bytes;
				if (phdr.p_filesz > 0)
				{
					/* Normal segment.
					 * Read initial part from disk and zero the rest. */
					read_bytes = page_offset + phdr.p_filesz;
					zero_bytes = (ROUND_UP(page_offset + phdr.p_memsz, PGSIZE) - read_bytes);
				}
				else
				{
					/* Entirely zero.
					 * Don't read anything from disk. */
					read_bytes = 0;
					zero_bytes = ROUND_UP(page_offset + phdr.p_memsz, PGSIZE);
				}
				if (!load_segment(file, file_page, (void *)mem_page,
								  read_bytes, zero_bytes, writable))
					goto done;
			}
			else
				goto done;
			break;
		}
	}

	/* Set up stack. */
	if (!setup_stack(if_))
		goto done;

	/* Start address. */
	if_->rip = ehdr.e_entry;

	/* TODO: Your code goes here.
	 * TODO: Implement argument passing (see project2/argument_passing.html). */
	argument_stack(argv, argc, if_);

	success = true;

done:
	/* We arrive here whether the load is successful or not. */
	// file_close(file);
	return success;
}

/* Checks whether PHDR describes a valid, loadable segment in
 * FILE and returns true if so, false otherwise. */
static bool
validate_segment(const struct Phdr *phdr, struct file *file)
{
	/* p_offset and p_vaddr must have the same page offset. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset must point within FILE. */
	if (phdr->p_offset > (uint64_t)file_length(file))
		return false;

	/* p_memsz must be at least as big as p_filesz. */
	if (phdr->p_memsz < phdr->p_filesz)
		return false;

	/* The segment must not be empty. */
	if (phdr->p_memsz == 0)
		return false;

	/* The virtual memory region must both start and end within the
	   user address space range. */
	if (!is_user_vaddr((void *)phdr->p_vaddr))
		return false;
	if (!is_user_vaddr((void *)(phdr->p_vaddr + phdr->p_memsz)))
		return false;

	/* The region cannot "wrap around" across the kernel virtual
	   address space. */
	if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
		return false;

	/* Disallow mapping page 0.
	   Not only is it a bad idea to map page 0, but if we allowed
	   it then user code that passed a null pointer to system calls
	   could quite likely panic the kernel by way of null pointer
	   assertions in memcpy(), etc. */
	if (phdr->p_vaddr < PGSIZE)
		return false;

	/* It's okay. */
	return true;
}

#ifndef VM
/* Codes of this block will be ONLY USED DURING project 2.
 * If you want to implement the function for whole project 2, implement it
 * outside of #ifndef macro. */

/* load() helpers. */
// static bool install_page(void *upage, void *kpage, bool writable);

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment(struct file *file, off_t ofs, uint8_t *upage,
			 uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
	ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT(pg_ofs(upage) == 0);
	ASSERT(ofs % PGSIZE == 0);

	file_seek(file, ofs);
	while (read_bytes > 0 || zero_bytes > 0)
	{
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* Get a page of memory. */
		uint8_t *kpage = palloc_get_page(PAL_USER);
		if (kpage == NULL)
			return false;

		/* Load this page. */
		if (file_read(file, kpage, page_read_bytes) != (int)page_read_bytes)
		{
			palloc_free_page(kpage);
			return false;
		}
		// kpage + page_read_bytes부터 page_zero_bytes만큼 값을 0으로 초기화
		memset(kpage + page_read_bytes, 0, page_zero_bytes);	

		/* Add the page to the process's address space. */
		if (!install_page(upage, kpage, writable))
		{
			printf("fail\n");
			palloc_free_page(kpage);
			return false;
		}

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a minimal stack by mapping a zeroed page at the USER_STACK */
static bool
setup_stack(struct intr_frame *if_)
{	
	uint8_t *kpage;
	bool success = false;

	kpage = palloc_get_page(PAL_USER | PAL_ZERO);
	if (kpage != NULL)
	{
		success = install_page(((uint8_t *)USER_STACK) - PGSIZE, kpage, true);
		if (success)
			if_->rsp = USER_STACK;
		else
			palloc_free_page(kpage);
	}
	return success;
}


#else
/* From here, codes will be used after project 3.
 * If you want to implement the function for only project 2, implement it on the
 * upper block. */

bool
lazy_load_segment(struct page *page, void *aux)
{	
	//-------project3-memory_management-start--------------
	struct frame *frame = page->frame;
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
	// aux에서 필요한 정보 빼내기
	struct file *file = ((struct container *)aux)->file;
	off_t offsetof = ((struct container *)aux)->offset;
	size_t page_read_bytes = ((struct container *)aux)->page_read_bytes;
	size_t page_zero_bytes = PGSIZE - page_read_bytes;
	// printf("=====================lazy_load_segment진입\n");
	file_seek(file, offsetof);	// 파일 읽을 위치 세팅
	if (file_read(file, frame->kva, page_read_bytes) != (int)page_read_bytes)
	{
		// printf("=====================lazy_load_segment에서 파일 읽기 실패\n");
		palloc_free_page(frame->kva);	// ?????????????
		return false;
	}
	// frame->kva + page_read_bytes부터 page_zero_bytes만큼 값을 0으로 초기화
	memset(frame->kva + page_read_bytes, 0, page_zero_bytes);
	//printf("=====================lazy_load_segment성공\n");
	return true;
	//-------project3-memory_management-end----------------
}

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment(struct file *file, off_t ofs, uint8_t *upage,
			 uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{ 	
	//-------project3-memory_management-start--------------
	// vm_alloc_page_with_initializer()를 호출하여 보류 중인 페이지 개체를 생성
	// 페이지 폴트가 발생하면 세그먼트가 실제로 파일에서 로드되는 때임.
	ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT(pg_ofs(upage) == 0);
	ASSERT(ofs % PGSIZE == 0);

	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* TODO: Set up aux to pass information to the lazy_load_segment. */
		struct container *container = (struct container *)malloc(sizeof(struct container));
		container->file = file;
		container->page_read_bytes = page_read_bytes;
		container->offset = ofs;
		// void *aux = NULL:
		if (!vm_alloc_page_with_initializer(VM_ANON, upage,
											writable, lazy_load_segment, container)) {
			// vm_alloc_page_with_initializer: spt에 앞으로 사용할 page들(aux에 있음)을 추가해준다.
			// vm_alloc_page_with_initializer의 5번째 인자인 aux는 load_segment에 설정한 정보
			// 이 정보를 사용하여 세그먼트를 읽을 파일을 찾고 결국 세그먼트를 메모리로 읽어야 함
			return false;
		}

		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
		ofs += page_read_bytes;	// 추가
	}
	return true;
}

/* Create a PAGE of stack at the USER_STACK. Return true on success. */
bool  // 기존 앞에 static 붙어있었음.
setup_stack(struct intr_frame *if_)
{	
	// 스택을 식별하는 방법을 제공해야 할 수도 있음
	// vm/vm.h의 vm_type에 있는 보조 마커(예: VM_MARKER_0)를 사용하여 페이지를 표시할 수 있음
	bool success = false;
	void *stack_bottom = (void *)(((uint8_t *)USER_STACK) - PGSIZE);

	/* TODO: Map the stack on stack_bottom and claim the page immediately.
	 * TODO: If success, set the rsp accordingly.
	 * TODO: You should mark the page is stack. */
	/* TODO: Your code goes here */

	// --------------------project3 Anonymous Page start---------
	//????????????
	//vm_alloc_page를 통한 페이지 할당
	printf("=====================setup_stack진입\n");
	if (vm_alloc_page(VM_ANON | VM_MARKER_0, stack_bottom, 1)) {    // type, upage, writable
		success = vm_claim_page(stack_bottom);
		printf("=====================vm_alloc_page성공\n");
		if (success) {
			if_->rsp = USER_STACK;
            thread_current()->stack_bottom = stack_bottom;
		}
    }
	// 마지막으로 spt_find_page를 통해 추가 페이지 테이블을 참조하여
	//  오류가 발생한 주소에 해당하는 페이지 구조를 해결하도록
	// vm_try_handle_fault 함수를 수정합니다.
	return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
 * virtual address KPAGE to the page table.
 * If WRITABLE is true, the user process may modify the page;
 * otherwise, it is read-only.
 * UPAGE must not already be mapped.
 * KPAGE should probably be a page obtained from the user pool
 * with palloc_get_page().
 * Returns true on success, false if UPAGE is already mapped or
 * if memory allocation fails. */
/* upage와 kpage를 연결한 정보를 페이지테이블(pml4)에 세팅함
*/
bool
install_page(void *upage, void *kpage, bool writable)
{
	struct thread *t = thread_current();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	return (pml4_get_page(t->pml4, upage) == NULL && pml4_set_page(t->pml4, upage, kpage, writable));
}

#endif /* VM */
