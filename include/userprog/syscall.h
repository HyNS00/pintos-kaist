#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

// * USERPROG 추가
#include <stdbool.h>
#include "threads/thread.h"

struct lock filesys_lock;

void syscall_init (void);

/* 추가 */
void check_address(void *addr);

/* Projects 2 and later. */
void halt (void);
void exit (int status);
tid_t fork (const char *thread_name);
int exec (const char *file_name);
int wait (pid_t);
bool create (const char *file, unsigned initial_size);
bool remove (const char *file);
int open (const char *file);
int filesize (int fd);
int read (int fd, void *buffer, unsigned length);
int write (int fd UNUSED, const void *buffer, unsigned length);
void seek (int fd, unsigned position);
unsigned tell (int fd);
void close (int fd);


#endif /* userprog/syscall.h */