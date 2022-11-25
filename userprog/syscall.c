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

void syscall_entry (void);
void syscall_handler (struct intr_frame *);
void check_address(void *addr);

void halt (void);
void exit (int status);
bool create (const char *file, unsigned initial_size);
bool remove (const char *file);
int write (int fd, const void *buffer, unsigned size); 
int wait (tid_t pid);
tid_t fork (const char *thread_name, struct intr_frame *f);
int exec (const char *file);
int open (const char *file);
int add_file_to_fd_table(struct file *file);
struct file *fd_to_file(int fd);
void remove_fd(int fd); 
void close (int fd);
int filesize (int fd);
int read (int fd, void *buffer, unsigned size);
void seek (int fd, unsigned position);
unsigned tell (int fd);

struct lock filesys_lock;


/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

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
void 
check_address(void *addr) {
	struct thread *cur = thread_current();
/* 1. 포인터가 가리키는 주소가 유저영역의 주소인지 확인 */
/* 2. 포인터가 가리키는 주소가 존재하는지 확인 */
/* 3. 포인터가 가리키는 주소에 해당하는 실주소가 없는 경우 NULL 반환 */
// || pml4_get_page(cur->pml4, addr) == NULL
	if(!is_user_vaddr(addr) || addr == NULL || pml4_get_page(cur->pml4, addr) == NULL){
		exit(-1);
	}
/* 잘못된 접근일 경우 프로세스 종료 */ 
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	/* 유저 스택에 저장되어 있는 시스템 콜 넘버를 이용해 시스템 콜 핸들러 구현 */
	int sys_num = f->R.rax;
	// check_address(sys_num);  /* 스택 포인터가 유저 영역인지 확인 */
	// printf("===========syscall_handler 안========%d=======\n", sys_num);

	switch (sys_num){
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
			f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_WAIT:
			f->R.rax = wait(f->R.rdi);
			break;
		case SYS_FORK:
			f->R.rax = fork(f->R.rdi, f);
			break;
		case SYS_EXEC:
			if(exec(f->R.rdi) == -1){
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
			f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_SEEK:
			seek(f->R.rdi, f->R.rsi);
			break;
		case SYS_TELL:
			f->R.rax = tell(f->R.rdi);
			break;
		default:
			// exit(-1);
			// break;
			thread_exit();
	}
}


void
halt (void) {
	power_off();
}

void
exit (int status) {
	struct thread *cur = thread_current();
	/* 프로세스 디스크립터에 exit status 저장 */ 
	cur->exit_status = status;
	printf("%s: exit(%d)\n" , cur->name , status); 
	thread_exit();
}

int
wait (tid_t pid) {
	/* 자식 프로세스가 종료 될 때까지 대기 */
	return process_wait(pid);
}

bool
create (const char *file, unsigned initial_size) {
	check_address(file);
	/* 파일 이름과 크기에 해당하는 파일 생성 */
	/* 파일 생성 성공 시 true 반환, 실패 시 false 반환 */
	return filesys_create(file, initial_size);
}

bool
remove (const char *file) {
	check_address(file);
	/* 파일 이름에 해당하는 파일을 제거 */
	/* 파일 제거 성공 시 true 반환, 실패 시 false 반환 */
	return filesys_remove(file);
}


/* 자식 프로세스를 생성하고 프로그램을 실행시키는 시스템 콜 */
int
exec (const char *file) {
	check_address(file);
	int size = strlen(file) +1 ; // 마지막 null값이라 +1
	char *fn_copy = palloc_get_page(PAL_ZERO);
	if ((fn_copy) == NULL){
		exit(-1);
	}	
	strlcpy(fn_copy, file, size);
	/* process_execute() 함수를 호출하여 자식 프로세스 생성 */ 
	if (process_exec(fn_copy) == -1){
		/* 프로그램 적재 실패 시 -1 리턴 */
		return -1;
	}
	/* 프로그램 적재 성공 시 자식 프로세스의 pid 리턴 */
	NOT_REACHED();
	return 0;
}

 /* 파일을 현재 프로세스의 fdt에 추가 */
int 
add_file_to_fdt(struct file *file){
	struct thread *cur = thread_current();
	struct file **cur_fd_table = cur->fd_table;
	for (int i = cur->fdidx; i < MAX_FD_NUM; i++){
		if (cur_fd_table[i] == NULL){
			cur_fd_table[i] = file;
			cur->fdidx = i;
			return cur->fdidx;
		}
	}
	cur->fdidx = MAX_FD_NUM;
	return -1;
}

int
open (const char *file) {
/* 성공 시 fd를 생성하고 반환, 실패 시 -1 반환 */
	check_address(file);
	struct file *open_file = filesys_open (file);
	
	if(open_file == NULL){
		return -1;
	}
	
	int fd = add_file_to_fdt(open_file);
	if (fd == -1){ // fd table 가득 찼다면
		file_close(open_file);
	}
	return fd;
}

int
write (int fd, const void *buffer, unsigned size) {
	struct file *file = fd_to_file(fd);
	check_address(buffer);
	check_address(buffer+size-1); // -1은 null 전까지만 유효하면 되서 
	if(file == NULL){
		return -1;
	}

	lock_acquire(&filesys_lock);
	if (fd == 1) {	// stdout(표준 출력) - 모니터
		putbuf(buffer, size);
		lock_release(&filesys_lock);
		return size;
	}else if(fd == 0){
		lock_release(&filesys_lock);
		return -1;
	}else{ 
		int bytes_written = file_write(file, buffer, size);
		lock_release(&filesys_lock);
		return bytes_written;
	}
}

/* 현재 프로세스의 복제본으로 자식 프로세스를 생성 */
tid_t
fork (const char *thread_name, struct intr_frame *f){
	return process_fork(thread_name, f);
}

/* 현재 프로세스의 fd에 있는 file 반환 */
struct file *
fd_to_file(int fd){
	struct thread *cur = thread_current();
	struct file **cur_fd_table = cur->fd_table;
	if(0 <= fd && fd < MAX_FD_NUM){
		return cur_fd_table[fd];
	}else{
		return NULL;
	}
}

void
remove_fd(int fd){
	struct thread *cur = thread_current();
	struct file **cur_fd_table = cur->fd_table;
	if(fd< 0 || fd > MAX_FD_NUM){
		return;
	}
	cur_fd_table[fd] = NULL;
}

void
close (int fd) {
	// fd를 file로 변경해서 file_close()인자로 넣기
	struct file *file = fd_to_file(fd);
	if(file == NULL){
		return;
	}
	// file_close(file);
	// fdt 에서 지워주기
	remove_fd(fd);
}

int
filesize (int fd) {
	struct file *file = fd_to_file(fd);
	if(file == NULL){
		return -1;
	}
	return file_length(file);
}

int
read (int fd, void *buffer, unsigned size) {
	struct file *file = fd_to_file(fd);
	// 버퍼의 처음 시작~ 끝 주소 check
	check_address(buffer);
	check_address(buffer+size-1); // -1은 null 전까지만 유효하면 되서 
	char *buf = buffer;
	int read_size;

	if(file == NULL){
		return -1;
	}
	// 정상인데 0 일 때, 키보드면 input_get
	if(fd == 0){
		char keyboard;
		for(read_size =0; read_size < size; read_size ++){
			keyboard = input_getc();
			// *buf ++ = keyboard;
			buf = keyboard;
			*buf ++;
			if(keyboard == '\0'){ // null 전까지 저장
				break;
			}
		} 
	}else if(fd == 1){
		return -1;
	}else{
	// 정상일 때 file_read
		lock_acquire(&filesys_lock);
		read_size = file_read(file, buffer, size);	// 실제 읽은 사이즈 return
		lock_release(&filesys_lock);
	}
	return read_size;
}

/* 다음 읽거나 쓸 file_pos 옮겨주기 */
void
seek (int fd, unsigned position) {
	struct file *file = fd_to_file(fd);
	// check_address(file);
	// if(file == NULL){
	// 	return -1;
	// }
	if(fd < 2){
		return;
	}
	if(fd >= 2){
		file_seek(file, position);
	}
}

unsigned
tell (int fd) {
	struct file *file = fd_to_file(fd);
	// check_address(file);
	// if(file == NULL){
	// 	return -1;
	// }
	if(fd < 2){
		return;
	}
	return file_tell(file);
}