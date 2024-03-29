#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "threads/palloc.h"
#include "intrinsic.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "userprog/process.h"
#include "vm/vm.h"
#include "vm/file.h"
#include "filesys/file.h"
#include "filesys/inode.h"
#include "filesys/directory.h"

void syscall_entry(void);
void syscall_handler(struct intr_frame *);
struct page *check_address(void *addr);

void halt(void);
void exit(int status);
bool create(const char *file, unsigned initial_size);
bool remove(const char *file);
int write(int fd, const void *buffer, unsigned size);
int wait(tid_t pid);
tid_t fork(const char *thread_name, struct intr_frame *f);
int exec(const char *file);
int open(const char *file);
int add_file_to_fdt(struct file *file);
struct file *fd_to_file(int fd);
void remove_fd(int fd);
void close(int fd);
int filesize(int fd);
int read(int fd, void *buffer, unsigned size);
void seek(int fd, unsigned position);
unsigned tell(int fd);
void *mmap(void *addr, size_t length, int writable, int fd, off_t offset);
void munmap(void *addr);
void check_valid_buffer(void *buffer, unsigned size, void *rsp, bool to_write);

// ------------project4 - Subdirectories and Soft Links start------------
bool isdir(int fd);
bool chdir(const char *dir);
bool mkdir(const char *dir);
bool readdir(int fd, char *name);
int inumber(int fd);
int symlink(const char *target, const char *linkpath);
// ------------project4 - Subdirectories and Soft Links end------------
struct lock filesys_lock;


/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081			/* Segment selector msr */
#define MSR_LSTAR 0xc0000082		/* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void syscall_init(void)
{
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48 |
							((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t)syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			  FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
	lock_init(&filesys_lock);
}

/* 주소 값이 유저 영역에서 사용하는 주소 값인지 확인 하는 함수
Pintos에서는 시스템 콜이 접근할 수 있는 주소를 0x8048000~0xc0000000으로 제한함
유저 영역을 벗어난 영역일 경우 프로세스 종료(exit(-1)) */
struct page *check_address(void *addr)
{
	struct thread *cur = thread_current();
	/* 1. 포인터가 가리키는 주소가 유저영역의 주소인지 확인 */
	/* 2. 포인터가 가리키는 주소가 존재하는지 확인 */
	/* 3. 포인터가 가리키는 주소에 해당하는 실주소가 없는 경우 NULL 반환 */
	/*
	if (!is_user_vaddr(addr) || addr == NULL || pml4_get_page(cur->pml4, addr) == NULL)
	{
		exit(-1);
	}
	*/
	if (!is_user_vaddr(addr) || addr == NULL)
	{
		exit(-1); /* 잘못된 접근일 경우 프로세스 종료 */
	}
	return spt_find_page(&cur->spt, addr);
}

/* The main system call interface */
void syscall_handler(struct intr_frame *f UNUSED)
{
	/* 유저 스택에 저장되어 있는 시스템 콜 넘버를 이용해 시스템 콜 핸들러 구현 */
	int sys_num = f->R.rax;

	// 스레드 구조체에 유저모드(interrupt frame에 있음)의 rsp를 저장함
	thread_current()->rsp_stack = f->rsp;

	// check_address(sys_num);  /* 스택 포인터가 유저 영역인지 확인 */

	switch (sys_num)
	{
	case SYS_HALT:
		halt();
		break;
	case SYS_EXIT:
		exit(f->R.rdi);
		break;
	case SYS_CREATE:
		f->R.rax = create(f->R.rdi, f->R.rsi);
		break;
	case SYS_REMOVE:
		f->R.rax = remove(f->R.rdi);
		break;
	case SYS_WRITE:
		check_valid_buffer(f->R.rsi, f->R.rdx, f->rsp, 0);
		f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_WAIT:
		f->R.rax = wait(f->R.rdi);
		break;
	case SYS_FORK:
		f->R.rax = fork(f->R.rdi, f);
		break;
	case SYS_EXEC:
		if (exec(f->R.rdi) == -1)
		{
			exit(-1);
		}
		break;
	case SYS_OPEN:
		f->R.rax = open(f->R.rdi);
		break;
	case SYS_CLOSE:
		close(f->R.rdi);
		break;
	case SYS_FILESIZE:
		f->R.rax = filesize(f->R.rdi);
		break;
	case SYS_READ:
		check_valid_buffer(f->R.rsi, f->R.rdx, f->rsp, 1);
		f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_SEEK:
		seek(f->R.rdi, f->R.rsi);
		break;
	case SYS_TELL:
		f->R.rax = tell(f->R.rdi);
		break;
	// --------------------project3 Memory Mapped Files start---------
	case SYS_MMAP:
		f->R.rax = mmap(f->R.rdi, f->R.rsi, f->R.rdx, f->R.r10, f->R.r8);
		break;
	case SYS_MUNMAP:
		munmap(f->R.rdi);
		break;
	// --------------------project3 Memory Mapped Files end-----------

	//------project4-subdirectory start-----------------------
	case SYS_ISDIR:
		f->R.rax = isdir(f->R.rdi);
		break;
	case SYS_CHDIR:
		f->R.rax = chdir(f->R.rdi);
		break;
	case SYS_MKDIR:
		f->R.rax = mkdir(f->R.rdi);
		break;
	case SYS_READDIR:
		f->R.rax = readdir(f->R.rdi, f->R.rsi);
		break;
	case SYS_INUMBER:
		f->R.rax = inumber(f->R.rdi);
		break;
	case SYS_SYMLINK:
		f->R.rax = symlink(f->R.rdi, f->R.rsi);
		break;
	//------project4-subdirectory end--------------------------
	default:
		exit(-1);

		// /* Project 4 only.
		// 기존 시스템 콜들 중에서, close만 디렉토리에 대한 file descriptor를 받을 수 있도록 해야합니다.
		// SYS_CHDIR,                  /* Change the current directory. */
		// SYS_MKDIR,                  /* Create a directory. */
		// SYS_READDIR,                /* Reads a directory entry. */
		// SYS_ISDIR,                  /* Tests if a fd represents a directory. */
		// SYS_INUMBER,                /* Returns the inode number for a fd. */
		// SYS_SYMLINK,                /* Returns the inode number for a fd. */
	}
}

void halt(void)
{
	power_off();
}

void exit(int status)
{
	struct thread *cur = thread_current();
	/* 프로세스 디스크립터에 exit status 저장 */
	cur->exit_status = status;
	printf("%s: exit(%d)\n", cur->name, status);
	thread_exit();
}

int wait(tid_t pid)
{
	/* 자식 프로세스가 종료 될 때까지 대기 */
	return process_wait(pid);
}

bool create(const char *file, unsigned initial_size)
{
	check_address(file);
	/* 파일 이름과 크기에 해당하는 파일 생성 */
	/* 파일 생성 성공 시 true 반환, 실패 시 false 반환 */
	return filesys_create(file, initial_size);
}

bool remove(const char *file)
{
	// project4 - Subdirectories and Soft Links- 에 맞게 수정해야함
	check_address(file);
	/* 파일 이름에 해당하는 파일을 제거 */
	/* 파일 제거 성공 시 true 반환, 실패 시 false 반환 */
	return filesys_remove(file);
}

/* 자식 프로세스를 생성하고 프로그램을 실행시키는 시스템 콜 */
int exec(const char *file)
{
	check_address(file);
	int size = strlen(file) + 1; // 마지막 null값이라 +1
	char *fn_copy = palloc_get_page(PAL_ZERO);
	if ((fn_copy) == NULL)
	{
		exit(-1);
	}
	strlcpy(fn_copy, file, size);
	/* process_execute() 함수를 호출하여 자식 프로세스 생성 */
	if (process_exec(fn_copy) == -1)
	{
		/* 프로그램 적재 실패 시 -1 리턴 */
		return -1;
	}
	/* 프로그램 적재 성공 시 자식 프로세스의 pid 리턴 */
	NOT_REACHED();
	return 0;
}

/* 파일을 현재 프로세스의 fdt에 추가 */
int add_file_to_fdt(struct file *file)
{
	struct thread *cur = thread_current();
	struct file **cur_fd_table = cur->fd_table;
	for (int i = cur->fdidx; i < MAX_FD_NUM; i++)
	{
		if (cur_fd_table[i] == NULL)
		{
			cur_fd_table[i] = file;
			cur->fdidx = i;

			return cur->fdidx;
		}
	}
	cur->fdidx = MAX_FD_NUM;
	return -1;
}

int open(const char *file)
{
	/* 성공 시 fd를 생성하고 반환, 실패 시 -1 반환 */
	check_address(file);
	lock_acquire(&filesys_lock);
	struct file *open_file = filesys_open(file);
	lock_release(&filesys_lock);
	if (open_file == NULL)
	{
		return -1;
	}

	int fd = add_file_to_fdt(open_file);
	if (fd == -1)
	{ // fd table 가득 찼다면
		file_close(open_file);
	}
	return fd;
}

int write(int fd, const void *buffer, unsigned size)
{
	struct file *file = fd_to_file(fd);
	check_address(buffer);
	if (file == NULL)
	{
		return -1;
	}

	if (fd == 1)
	{ // stdout(표준 출력) - 모니터
		putbuf(buffer, size);
		return size;
	}
	else if (fd == 0)
	{
		return -1;
	}
	else
	{
		lock_acquire(&filesys_lock);
		int bytes_written = file_write(file, buffer, size);
		lock_release(&filesys_lock);
		return bytes_written;
	}
}

/* 현재 프로세스의 복제본으로 자식 프로세스를 생성 */
tid_t fork(const char *thread_name, struct intr_frame *f)
{
	return process_fork(thread_name, f);
}

/* 현재 프로세스의 fd에 있는 file 반환 */
struct file *
fd_to_file(int fd)
{
	struct thread *cur = thread_current();
	struct file **cur_fd_table = cur->fd_table;
	if (0 <= fd && fd < MAX_FD_NUM)
	{
		return cur_fd_table[fd];
	}
	else
	{
		return NULL;
	}
}

void remove_fd(int fd)
{
	struct thread *cur = thread_current();
	struct file **cur_fd_table = cur->fd_table;
	if (fd < 0 || fd > MAX_FD_NUM)
	{
		return;
	}
	cur_fd_table[fd] = NULL;
}

void close(int fd)
{
	// fd를 file로 변경해서 file_close()인자로 넣기
	struct file *file = fd_to_file(fd);
	if (file == NULL)
	{
		return;
	}
	// file_close(file);
	// fdt 에서 지워주기
	remove_fd(fd);
}

int filesize(int fd)
{
	struct file *file = fd_to_file(fd);
	if (file == NULL)
	{
		return -1;
	}
	return file_length(file);
}

int read(int fd, void *buffer, unsigned size)
{
	struct file *file = fd_to_file(fd);
	// 버퍼의 처음 시작~ 끝 주소 check
	check_address(buffer);
	check_address(buffer + size - 1); // -1은 null 전까지만 유효하면 돼서
	char *buf = buffer;
	int read_size;

	if (file == NULL)
	{
		return -1;
	}
	// 정상인데 0 일 때, 키보드면 input_get
	if (fd == 0)
	{
		char keyboard;
		for (read_size = 0; read_size < size; read_size++)
		{
			keyboard = input_getc();
			// *buf ++ = keyboard;
			buf = keyboard;
			*buf++;
			if (keyboard == '\0')
			{ // null 전까지 저장
				break;
			}
		}
	}
	else if (fd == 1)
	{
		return -1;
	}
	else
	{
		// 정상일 때 file_read
		lock_acquire(&filesys_lock);
		read_size = file_read(file, buffer, size); // 실제 읽은 사이즈 return
		lock_release(&filesys_lock);
	}
	return read_size;
}

/* 다음 읽거나 쓸 file_pos 옮겨주기 */
void seek(int fd, unsigned position)
{
	struct file *file = fd_to_file(fd);
	// check_address(file);
	// if(file == NULL){
	// 	return -1;
	// }
	if (fd < 2)
	{
		return;
	}
	if (fd >= 2)
	{
		file_seek(file, position);
	}
}

unsigned
tell(int fd)
{
	struct file *file = fd_to_file(fd);
	// check_address(file);
	// if(file == NULL){
	// 	return -1;
	// }
	if (fd < 2)
	{
		return;
	}
	return file_tell(file);
}

// --------------------project3 start----------------------
void *mmap(void *addr, size_t length, int writable, int fd, off_t offset)
{

	// 1. 파일 내용을 읽는 위치(커서)(offset)가 page-align되어야 함 -> struct file의 pos멤버
	if (offset % PGSIZE != 0)
	{
		return NULL;
	}

	// 2. 가상 유저 page 시작 주소(addr)가 page-align되어야 함, addr이 유저영역이어야 함, addr이 NULL이 아니어야 함, length가 0보다 커야 함
	if ((pg_round_down(addr) != addr) || is_kernel_vaddr(addr) || addr == NULL || (long long)length <= 0)
	{
		return NULL;
	}

	// 3. fd가 콘솔 입출력(STDIN/STDOUT)이 아니어야 함
	if (fd == 0 || fd == 1)
	{
		exit(-1);
	}

	// 4. 매핑하려는 페이지가 이미 spt에 존재하는 페이지이면 안됨
	if (spt_find_page(&thread_current()->spt, addr))
	{
		return NULL;
	}

	struct file *target = fd_to_file(fd);

	if (target == NULL)
	{
		return NULL;
	}

	return do_mmap(addr, length, writable, target, offset);
}

void munmap(void *addr)
{
	do_munmap(addr);
}

void check_valid_buffer(void *buffer, unsigned size, void *rsp, bool to_write)
{

	if (buffer <= USER_STACK && buffer >= rsp)
	{ //
		return;
	}

	for (int i = 0; i < size; i++) {
		// 인자로 받은 buffer부터 buffer + size까지의 크기가 한 페이지의 크기를 넘을수도 있음
		struct page *page = check_address(buffer + i);
		if (page == NULL)
			exit(-1);
		// to_write인자는 SYS_READ이면 true로, SYS_WRITE이면 false로 들어옴
		// SYS_READ일 때는 file(DISK)에서 buffer(MEM)로 write를 해야하기 때문에, page의 writable이 항상 true여야 함
		if (to_write == true && page->writable == false)
			exit(-1);
	}
}
// --------------------project3 end-------------------------

//------project4-start-----------------------
bool isdir(int fd)
{
	// struct file *target = fd_to_file(fd);
	struct file *target = thread_current()->fd_table[fd];

	return inode_is_dir(file_get_inode(target));
}

// 프로세스의 현재 작업 디렉토리를 상대 혹은 절대 경로 dir 로 변환 - change directory
bool chdir(const char *dir)
{
	if (dir == NULL) {
		return false;
	}

	// name의 파일 경로 를 cp_name에 복사, 마지막에 '\0' 넣음
    char *cp_name = (char *)malloc(strlen(dir) + 1);
    strlcpy(cp_name, dir, strlen(dir) + 1);

	struct dir *chdir = NULL;
    if (cp_name[0] == '/') {	// dir이 절대 경로인 경우
        chdir = dir_open_root();
    }
    else {						// dir이 상대 경로인 경우
        chdir = dir_reopen(thread_current()->cur_dir);
	}

	// dir경로를 분석하여 디렉터리를 반환
    char *token, *savePtr;
    token = strtok_r(cp_name, "/", &savePtr);

    struct inode *inode = NULL;
	while (token != NULL) {
        // dir에서 token이름의 파일을 검색하여 inode의 정보를 저장
        if (!dir_lookup(chdir, token, &inode)) {
            dir_close(chdir);
            return false;
        }

        // inode가 파일일 경우 NULL 반환
        if (!inode_is_dir(inode)) {
            dir_close(chdir);
            return false;
        }

        // dir의 디렉터리 정보를 메모리에서 해지
        dir_close(chdir);
        
        // inode의 디렉터리 정보를 dir에저장
        chdir = dir_open(inode);

        // token에 다음 경로 저장
        token = strtok_r(NULL, "/", &savePtr);
    }
	// 스레드의현재작업디렉터리를변경
    dir_close(thread_current()->cur_dir);
    thread_current()->cur_dir = chdir;
    free(cp_name);
    return true;

}

// 상대 혹은 절대 디렉토리 이름이 dir인 디렉토리를 생성
bool mkdir(const char *dir)
{
	lock_acquire(&filesys_lock);
    bool new_dir = filesys_create_dir(dir);
    lock_release(&filesys_lock);
    return new_dir;
}

// 디렉토리를 나타내는 file descriptor fd로부터 디렉토리 엔트리를 읽습니다.
// 성공하면 null로 끝나는 파일명을 name에 저장하고 true를 반환합니다.
// name에는 READDIR_MAX_LEN + 1 bytes만큼의 공간이 있어야 합니다.
// 디렉토리에 엔트리가 하나도 없으면 false를 반환합니다. 
bool readdir(int fd, char *name)
{
	if (name == NULL)
		return false;

	struct file *f = fd_to_file(fd);
	if (f == NULL)
		return false;

	if (!inode_is_dir(f->inode))
		return false;

	struct dir *dir = f;

	bool succ = dir_readdir(dir, name);

	return succ;

    // // fd리스트에서 fd에 대한 file정보 얻어옴
	// struct file *target = find_file_by_fd(fd);
    // if (target == NULL) {
    //     return false;
	// }

    // // fd의 file->inode가 디렉터리인지 검사
    // if (!inode_is_dir(file_get_inode(target))) {
    //     return false;
	// }

    // // p_file을 dir자료구조로 포인팅
    // struct dir *p_file = target;
    // if (p_file->pos == 0) {
    //     dir_seek(p_file, 2 * sizeof(struct dir_entry));		// ".", ".." 제외
	// }

    // // 디렉터리의 엔트리에서 ".", ".." 이름을 제외한 파일이름을 name에 저장
    // bool result = dir_readdir(p_file, name);

    // return result;
}

int inumber(int fd)
{
	struct file *f = fd_to_file(fd);
	if (f == NULL)
		return false;

	return inode_get_inumber(f->inode);
}
int symlink(const char *target, const char *linkpath)
{

}
//------project4-end--------------------------

