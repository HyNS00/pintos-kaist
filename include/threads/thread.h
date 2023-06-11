#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H
#include "synch.h"
#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#ifdef VM
#include "vm/vm.h"
#endif


/* States in a thread's life cycle. */
enum thread_status {
	THREAD_RUNNING,     /* Running thread. */
	THREAD_READY,       /* Not running but ready to run. */
	THREAD_BLOCKED,     /* Waiting for an event to trigger. */
	THREAD_DYING        /* About to be destroyed. */
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

/* A kernel thread or user process.
 *
 * Each thread structure is stored in its own 4 kB page.  The
 * thread structure itself sits at the very bottom of the page
 * (at offset 0).  The rest of the page is reserved for the
 * thread's kernel stack, which grows downward from the top of
 * the page (at offset 4 kB).  Here's an illustration:
 *
 *      4 kB +---------------------------------+
 *           |          kernel stack           |
 *           |                |                |
 *           |                |                |
 *           |                V                |
 *           |         grows downward          |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           +---------------------------------+
 *           |              magic              |
 *           |            intr_frame           |
 *           |                :                |
 *           |                :                |
 *           |               name              |
 *           |              status             |
 *      0 kB +---------------------------------+
 *
 * The upshot of this is twofold:
 *
 *    1. First, `struct thread' must not be allowed to grow too
 *       big.  If it does, then there will not be enough room for
 *       the kernel stack.  Our base `struct thread' is only a
 *       few bytes in size.  It probably should stay well under 1
 *       kB.
 *
 *    2. Second, kernel stacks must not be allowed to grow too
 *       large.  If a stack overflows, it will corrupt the thread
 *       state.  Thus, kernel functions should not allocate large
 *       structures or arrays as non-static local variables.  Use
 *       dynamic allocation with malloc() or palloc_get_page()
 *       instead.
 *
 * The first symptom of either of these problems will probably be
 * an assertion failure in thread_current(), which checks that
 * the `magic' member of the running thread's `struct thread' is
 * set to THREAD_MAGIC.  Stack overflow will normally change this
 * value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
 * the run queue (thread.c), or it can be an element in a
 * semaphore wait list (synch.c).  It can be used these two ways
 * only because they are mutually exclusive: only a thread in the
 * ready state is on the run queue, whereas only a thread in the
 * blocked state is on a semaphore wait list. */
struct thread {
	/* Owned by thread.c. */
	tid_t tid;                          /* Thread identifier. */
	enum thread_status status;          /* Thread state. */ // 스레드의 상태,
	char name[16];                      /* Name (for debugging purposes). */
	int priority;                       /* Priority. */
	int64_t wakeup_tick;                    // 각자 가지고 있을 일어날 시간
	// donation 선언
	struct list donations; // priority가 변경되었다면 A thread의 list donations에 b스레드를 기억한다
	struct list_elem donation_element; // list donation에 넣어줄 리스트 성분
	struct lock *wait_on_lock; // 해당 스레드가 대기하고 있는 lock자료구조 주소 저장:thread가 원하는 lock을 이미 다른 thread가 점유하고 있으면 lock의 주소를 저장
	int init_priority; // priority를 양도받았다가 다시 반납할 때 원래 복원할 때 고유 priority
	int exit_status;
	/* Shared between thread.c and synch.c. */
	struct list_elem elem;              /* List element. */
	struct file **fd_table;
	int fd_idx;
	struct intr_frame parent_if;
	// for system call
	#define FDT_PAGES 3 // 파일 디스크립터 테이블에 할당할 페이지의 양을 말하는 것
	// 어람나 많은 페이지를 할당해야하는지를 다루고 있다.
	#define FDCOUNT_LIMIT FDT_PAGES*(1<<9) // limit fdldx
	struct semaphore fork_sema;
	struct semaphore wait_sema;
	struct list_elem child_elem;
	struct list child_list;
	struct semaphore free_sema;
	
#ifdef USERPROG
	/* Owned by userprog/process.c. */
	uint64_t *pml4;                     /* Page map level 4 */
#endif
#ifdef VM
	/* Table for whole virtual memory owned by thread. */
	struct supplemental_page_table spt;
#endif

	/* Owned by thread.c. */
	struct intr_frame tf;               /* Information for switching */
	struct intr_frame ptf;
	unsigned magic;                     /* Detects stack overflow. */
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

void thread_sleep(int64_t); // 재우는 함수
void thread_wakeup(int64_t); // 깨우는 함수
bool thread_compare_time(const struct list_elem *a, const struct list_elem *b, void *aux);
//void timer_interrupt(struct intr_frame *tf);

bool compare_donate_priority(const struct list_elem *c, const struct list_elem *d, void *aux);
void donate_priority (void);
typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);
void sort_ready_list(void);
void thread_block (void);
void thread_unblock (struct thread *);
 
void remove_with_lock (struct lock *lock);
void refresh_priority (void);
void lock_release (struct lock *lock);

bool thread_compare_priority(const struct list_elem *a, const struct list_elem *b, void *aux);
void max_priority (void);
struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);


void thread_exit (void) NO_RETURN;
void thread_yield (void);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

void do_iret (struct intr_frame *tf);

#endif /* threads/thread.h */
