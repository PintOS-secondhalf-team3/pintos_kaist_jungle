#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

bool install_page(void *upage, void *kpage, bool writable);
tid_t process_create_initd (const char *file_name);
tid_t process_fork (const char *name, struct intr_frame *if_);
int process_exec (void *f_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (struct thread *next);
void argument_stack(char **argv, int argc, struct intr_frame *if_);

//--------------------project3 Anonymous Page start---------
static bool
load_segment(struct file *file, off_t ofs, uint8_t *upage,
			 uint32_t read_bytes, uint32_t zero_bytes, bool writable);

struct container{
    struct file *file;
    off_t offset;
    size_t page_read_bytes;
};
//--------------------project3 Anonymous Page end---------

#endif /* userprog/process.h */
