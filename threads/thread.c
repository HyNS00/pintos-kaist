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
   /*magic 멤버를 위한 랜덤한 값이다.
   스택 오버플로우를 감지하는데 사용된다. 
   struct thread는 스레드의 속성을 나타내는 구조체다*/
#define THREAD_MAGIC 0xcd6abf4b

/* Random value for basic thread
   Do not modify this value. */
   /* basic thread를 위한 랜덤 값
   */
#define THREAD_BASIC 0xd42df210
static struct list sleep_list;

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
/* THREAD_READY상태에 있는 프로세스들의 목록이다. == 실행 준비가 된 프로세스들이지만 실제로 실행되지는 않았다.*/
static struct list ready_list;

/* Idle thread. */
/*컴퓨터 시스템에서 다른 작업이나 프로세스가 실행되지 않을 때 실행되는 특별한 시스템 스레드를 가리키는 용어
최소한의 유용한 작업을 수행하거나 전혀 유용한 작업을 수행하지 않으며, 주로 다른활성 작업이 없을 때 cpu를 사용 중인 상태로 유지하기
위한 자리채우기 역할을 합니다.*/
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
/* 초기 스레드 . 함수를 실행중이 스레드*/
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
/* alloocate.tid() 함수에서 사용되는 lock()*/
static struct lock tid_lock;

/* Thread destruction requests */
/* 스레드 파괴 요청*/
static struct list destruction_req;

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
/* 스케줄링*/
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */

/* 만약 false라면 라운드로빈 스케줄러를 사용합니다.
만약 true라면 ,멀티레벨 피드백 큐 스케쥴러를 사용한다.
이는 커널 명령 줄 옵션 -o mlfqs'에 의해 제어된다.*/
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule (void);
static tid_t allocate_tid (void);



/* Returns true if T appears to point to a valid thread. */
/*T가 유효한 스레드를 가리키는 것을 보인다면 true 반환한다.*/
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* Returns the running thread.
 * Read the CPU's stack pointer `rsp', and then round that
 * down to the start of a page.  Since `struct thread' is
 * always at the beginning of a page and the stack pointer is
 * somewhere in the middle, this locates the curent thread. */
 /* 실행 중인 스레드를 반환한다.
 	cpu의 스택 포인터 rsp를 읽은 다음
	해당 페이지의 시작으로 내림한다.
	struct thread가 항상 페이지의 시작에 있고,
	스택 포인터는 중간 어딘가에 있으므로,
	현재 스레드를 찾을 수 있다. */
#define running_thread() ((struct thread *) (pg_round_down (rrsp ())))


// Global descriptor table for the thread_start.
// Because the gdt will be setup after the thread_init, we should
// setup temporal gdt first.

// 임시 GDT를 설정해야한다.
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
   /* 현재 실행 중인 코드를 스레드로 변환하여 스레딩 시스템을 초기화한다.
   이는 일반적으로 작동할 수 없으며 , 이 경우에만 실행한다.
   
   실행 대기열 과 tid락을 초기화한다.
   이 함수를 호출한 후에는 thread_create() 사용하여 스레드를 생성하기 전에
   페이지 할당자를 반드시 초기화해야 한다.

   이 함수가 완료될 때까지 thread_current()를 호출하는 것은 안전하지 않는다.
   */
void
thread_init (void) {
	ASSERT (intr_get_level () == INTR_OFF);

	/* Reload the temporal gdt for the kernel
	 * This gdt does not include the user context.
	 * The kernel will rebuild the gdt with user context, in gdt_init (). */
	 /*
	 커널을 위해 임시 GDT를 다시 로드한다. 이 GDT에는 사용자 컨텍스트가 포함되지 않는다.
	 커널은 gdt_init()에서 사용자 컨텍스트와 함꼐 GDT를 다시 구축할 것이다.
	 */
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
// 재우는 함수
void 
thread_sleep (int64_t ticks){
	enum intr_level old_level;
	old_level = intr_disable();
	/*
	현재 스레드가 idle 스레드가 아니라면 
	스레드를 sleep_list 에 스레드를 넣고
	재운다.
	*/ 
	ASSERT (!intr_context ());
	if (thread_current() != idle_thread){
		thread_current()->wakeup_tick = ticks;
		list_insert_ordered(&sleep_list, &thread_current()->elem,thread_compare_time,0);
		thread_block();
	}
	intr_set_level(old_level);
}
/* 타이머 인터럽트에서 글로벌 틱수를 받아와서 
슬립 리스트의 가장 짧게 잠드는 스레드와 글로벌 틱스를 비교해
시간이 되면 thread_unblock()호출 */
void thread_wakeup (int64_t ticks){
	enum intr_level old_level;
	old_level = intr_disable(); // 인터럽트 비활성화
	bool flag = true;
	while(flag){
		flag = false;
		if(!list_empty(&sleep_list)){
			if(list_entry(list_front(&sleep_list), struct thread, elem)-> wakeup_tick <= ticks){
				flag = true;
				thread_unblock(list_entry(list_pop_front(&sleep_list), struct thread, elem));
			}
		}
	}
	intr_set_level(old_level);
}

// 스레드의 로컬 타임 비교 전자가 더 높으면 false 후자가 높으면 true
/* 스레드의 꺠움 시간을 비교, */
bool thread_compare_time(const struct list_elem *a,
							 const struct list_elem *b,
							 void *aux)
{
	return list_entry(a, struct thread, elem)->wakeup_tick < list_entry(b, struct thread, elem)->wakeup_tick;
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
/* 인터럽트를 활성화하여 선점형 스레드 스케줄링을 시작합니다.
또한 아이들 스레드를 생성합니다. */
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
   /* 타이머 인터럼트 핸들러에서 각 타이머 틱을 호출한다.
   따라서 이 함수는 외부 인터럼트 컨텍스트에서 실행된다.*/
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

	/* Enforce preemption. */
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
/* 주어진 초기 우선 순위로 FUNCTION을 실행하는 새로운 이름이 NAME인 커널 스레들 생성하고, 인자로
AUX를 전달한 뒤 ,준비 큐에 추가한다.
새로운 스레드의 식별자를 반환하여 생성에 실패한 경우 TID_ERROR를 반환한다.

thread_start()가 호출된 경우, 새로운 스레드는 thread_create()가 반환되기 전에도 스케줄될 수 있습니다.
심지어 thread_create()가 반환되기 전에 종료될 수도 있습니다.
반대로, 새로운 스레드가 스케줄되기 전에 원래 스레드는 어떤 시간 동안 실행될 수 있습니다.
순서를 보장해야 하는 경우, 세마포어나 다른 형태의 동기화를 사용하세요.

제공된 코드는 새로운 스레드의 `priority' 멤버를 PRIORITY로 설정하지만,
실제로 우선순위 스케줄링은 구현되어 있지 않습니다.
우선순위 스케줄링은 1-3 문제의 목표입니다.
*/
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
	
	/* 스케쥴링이 될 경우 kernel_thread를 호출한다.*/
	t->tf.rip = (uintptr_t) kernel_thread;
	t->tf.R.rdi = (uint64_t) function;
	t->tf.R.rsi = (uint64_t) aux;
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;

	// 	struct semaphore fork_sema;
	// struct semaphore wait_sema;
	// struct list_elem child_elem;
	// struct list child_list;
	// struct semaphore free_sema;

	sema_init (&t->free_sema, 0);
	sema_init (&t->wait_sema, 0);
	sema_init (&t->fork_sema, 0);

	list_push_back (&thread_current ()->child_list, &t->child_elem);


	/* Add to run queue. */
	thread_unblock (t);
	max_priority();
	return tid;
} // 스레드를 생성하는 함수 'thread_create'의 구현이다. 함수는 새로운 스레드를 생성하고 초기화한 후, 스레드를 실행 대기열에 추가하여 스케줄링
// 매개변수로는 이름,우선순위, 실행할 함수, 보조인자 aux

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */

/* 현재 스레드를 슬립 상태로 만든다. thread_unblock에 의해 꺠어날 때까지 다시 스케줄 되지 않는다

 이 함수는 인터럽트가 꺼진 상태에서 호출되어야한다. 일반적으로 synch.h에 있는 동기화 기법 중 하나를 사용하는 것이 
 더 좋은 방법이다.*/
void
thread_block (void) {
	ASSERT (!intr_context ());
	ASSERT (intr_get_level () == INTR_OFF);
	thread_current ()->status = THREAD_BLOCKED; // 현재 스레드의 상태를 BLOCKED로 설정
	schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */

/* 블록된 스레드 T를 실행 대기 상태로 전환한다.
만약 T가 블록되지 않은 상태라면 에러다. (실행 중인 스레드를 실행 대기 상태로 만들기 위해 thread_yield()를 사용한다) 

이 함수는 실행 중인 스레드를 선점하지 않는다. 이는 중요할 수 있다. 호출자가 인터럼트를 비활성화한 상태에서 스레드를 원자적으로 언블럭하고 
다른 데이터를 업데이트 할 수 있다고 기대할 수 있기 떄문이다.

*/
void
thread_unblock (struct thread *t) {
	enum intr_level old_level;

	ASSERT (is_thread (t)); // t가 유효한 스레드인지 확인한다.

	old_level = intr_disable (); // 인터럽트를 비활성화하고 이전 인터럽트 레벨을 저장
	ASSERT (t->status == THREAD_BLOCKED); // t의 상태가 blocked인지 확인한다.
	// list_push_back (&ready_list, &t->elem); // 준비 리스트의 뒤에 t를 추가한다.
	list_insert_ordered(&ready_list, &t->elem, thread_compare_priority ,0);
	sort_ready_list();
	t->status = THREAD_READY; // t의 상태를 READY로 변경한다
	intr_set_level (old_level); // 이전 인터럽트 레벨로 복원한다.
}

/* Returns the name of the running thread. */
const char *
thread_name (void) {
	return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */

/* 현재 실행 중인 스레드를 반환한다.
running_thread()함수와 몇 가지 안전성 체크를 포함한다.*/
struct thread *
thread_current (void) {
	struct thread *t = running_thread ();

	/* Make sure T is really a thread.
	   If either of these assertions fire, then your thread may
	   have overflowed its stack.  Each thread has less than 4 kB
	   of stack, so a few big automatic arrays or moderate
	   recursion can cause stack overflow. */

	/* 
	T가 진짜로 스레드인지 확인합니다.
   만약 이 중 어느 하나라도 어설션(단언문)이 실패한다면, 스레드가 스택 오버플로우가 발생한 것일 수 있습니다.
   각 스레드는 4kB 미만의 스택을 가지고 있으므로, 큰 자동 배열이나 중간 정도의 재귀 호출은 스택 오버플로우를 발생시킬 수 있습니다.
	*/
	ASSERT (is_thread (t)); // t가 진짜 유효한 스레드인지 확인한다.
	ASSERT (t->status == THREAD_RUNNING); // t의 상태가 THREAD_RUNNING인지 확인합니다.

	return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) {
	return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */

/*  현재 스레드를 비활성화하고 파괴한다.
*/
void
thread_exit (void) {
	ASSERT (!intr_context ());

#ifdef USERPROG
	process_exit ();
#endif

	/* Just set our status to dying and schedule another process.
	   We will be destroyed during the call to schedule_tail(). */

	/* 우리의 상태를 dying으로 설정하고 다른 프로세스를 스케줄링한다.
	schedule_tail()호출 중에 파괴 */
	intr_disable ();
	do_schedule (THREAD_DYING);
	NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
/* cpu를 양보한다. 현재 스레드는 슬립 상태로 전환되지 않으며, 스케줄러의 임의대로 즉시 다시
스케줄링 될 수 있다.*/
/* cpu를 양보한다는 것은 현재 실행 중인 스레드가 다른 스레드에게 cpu실행 시간을 양보하는 것을 의미한다. cpu는 하나의 스레드에 의해
독점적으로 사용되는 것이 아니라 여러 스레드 간에 시분할 방식으로 공유된다.

thread_yield() 이 함수는 현재 스레드의 실행을 일시 중단하고, 스케줄러에게 다른 스레드를 실행하도록 요청한다. 스케줄러는 그에 따라 실행할 스레드를
선택하여 cpu를 할당한다.

*/
void
thread_yield (void) {
	struct thread *curr = thread_current ();
	enum intr_level old_level;

	ASSERT (!intr_context ());

	old_level = intr_disable ();
	if (curr != idle_thread)
		// list_push_back (&ready_list, &curr->elem);
		list_insert_ordered(&ready_list, &curr->elem, thread_compare_priority ,0);
	sort_ready_list();
	do_schedule (THREAD_READY);
	intr_set_level (old_level);
}
void sort_ready_list() {
	list_sort(&ready_list, thread_compare_priority,NULL);
}
bool thread_compare_priority(const struct list_elem *a,
							 const struct list_elem *b,
							 void *aux)
{
	return list_entry(a, struct thread, elem)->priority > list_entry(b, struct thread, elem)->priority;
}

void
max_priority (void) {
	if(!list_empty(&ready_list) && thread_current() -> priority <
	list_entry(list_front(&ready_list),struct thread,elem)->priority){
		thread_yield ();
	}
}
/* Sets the current thread's priority to NEW_PRIORITY. */
/* 현재 스레드의 우선순위를 new_priority로 설정한다.*/
// 현재 스레드가 더 이상 높은 우선 순위를 가지지 않으면 양보한다.
void
thread_set_priority (int new_priority) {
	thread_current ()->priority = new_priority;
	thread_current ()->init_priority = new_priority;
	refresh_priority();
	max_priority();
	
}
bool compare_donate_priority(const struct list_elem *c,
							 const struct list_elem *d,
							 void *aux)
{
	return list_entry(c, struct thread, donation_element)->priority > list_entry(d, struct thread, donation_element)->priority;
}

// 자신의 priority를 필요한 lock을 점유하고 있는 스레드에게 빌려주는 함수
// wait_on_lock = 현재 스레드가 대기 중인 락이 없으면 반복문 종료
// 대기 중인 락이 존재하는 경우, 소유자를 가져옴(holder)
// holder 스레드의 우선순위를 현재 스레드의 우선순위로 변경
// holder는 락을 소유하고 있는 스레드. 즉 lock의 holder 멤버는 현재 해당 락을 소유하고 있는 스레드를 가리키게 된다.
void
donate_priority (void)
{
	int depth; // 깊이
	struct thread *cur = thread_current(); // 현재의 스레드
	for (depth =0; depth <8; depth ++){
		if (!cur->wait_on_lock)
			break;
		// cur 현재 실행 중인 스레드를 가리킨다. 함수 내에서 현재 스레드를 의미하고 우선순위 기부를 위해 현재 스레드의 우선순위를 다른 스레드에게 
		// ㄱ기부하기 위해 업데이트
		struct thread *holder = cur->wait_on_lock->holder;
		// cur ->wait_on_lock :현재 스레드가 대기 중인 락을 가리킴
		// cur->wait_on_lock->holder : 대기 중인 락의 소유
		holder->priority = cur ->priority;
		cur = holder;
	}
}

void 
remove_with_lock (struct lock *lock){
	// lock을 해지 했을 때, waiters 리스트에서 해당 엔트리를 삭제 하기 위한 함수를 구현
	// 현재 쓰레드의 waiters리스트를 확인하여 해지하여 lock을 보유하고 있는 엔트리 삭제
	struct list_elem *e;
	struct thread *cur = thread_current();
	// &cur ->donations는 cur의 donations 리스트의 시작 부분의 주소를 나타낸다.
	// cur현재 실행 중인 스레드를 가리키는 포인터, donations은 해당 스레드가 받은 우선순위 기부를 관리하는 리스트.
	for (e =list_begin(&cur ->donations); e != list_end(&cur ->donations); e= list_next(e)){
		// 끝요소가 아닌 동안 반복한다.
		struct thread *t = list_entry(e, struct thread, donation_element);
		if(t->wait_on_lock == lock){
			list_remove(&t->donation_element);
		}
	}
}
// 우선순위를 돌려주는 작업
void
refresh_priority (void){
	struct thread *cur = thread_current();
	cur -> priority = cur ->init_priority;
	if(!list_empty(&cur->donations)){
		list_sort(&cur->donations,donate_priority,0);	
		struct thread *front = list_entry(list_front(&cur -> donations),struct thread, donation_element);
		if(front->priority > cur->priority){
			cur->priority = front->priority;
			//가장 앞에 있는 스레드의 우선 순위와 현재 스레드의 우선순위를 비교하여
			// 앞에 있는 것이 더 높다면 현재 스레드의 우선순위를 업데이트한다
		}
	}
}


/* Returns the current thread's priority. */
/* 현재 스레드의 우선순위를 돌려준다.*/
// 우선 순위 기부가 있는 경우에는 더 높은 우선순위를 반환
int
thread_get_priority (void) {
	return thread_current ()->priority;
}

/* Sets the current thread's nice value to NICE. */
/* 현재 스레드의 nice값을 NICE로 설정한다.*/
void
thread_set_nice (int nice UNUSED) {
	/* TODO: Your implementation goes here */
}

/* Returns the current thread's nice value. */
/* 현재 스레드의 nice값을 반환한다.*/
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

 /* idle 스레드다. 다른 실행 가능한 스레드가 없을 때 샐행된다.
	idle 스레드는 thread_start()에 의해 초기에 준비 목록에 추가된다.
	초기에 스케줄링 되며, 이 때  idle_thread를 초기화하고 전달된 세마포를 up하여
	thread_start()가 계속 실행되도록 하고 즉시 블록된다.
	그 후로 idle 스레드는 준비 목록에 나타나지 않는다.
	준비 목록이 비어있을 떄, 빈 특별한 경우로서 next_thread_to_run()에 의해 반환된다.
 */
static void
idle (void *idle_started_ UNUSED) {
	struct semaphore *idle_started = idle_started_;
	// idle_started_를 idle_started 세마포어 포인터로 캐스팅, 세마포어를 idle_started로 사용하기 위한 작업
	idle_thread = thread_current ();
	// 현재 실행중인 스레드를 할당한다. 이는 현재 스레드를 idle_started로 지정하여 현재 스레드가 비활성 상태임을 나타낸다.
	sema_up (idle_started);
	// 시그널(up)상태로 변경, 이는 idle함수가 시작되었음을 알리는 역할을 한다. (up은  세마포어의 내부 값이 양수인 상태를 나타냄)
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

		/*
		 인터럽트를 다시 활성화하고 다음 인터럽트를 기다리는 부분이다.
		 'sti' 명령은 다음 명령이 완료될 때까지 인터럽트를 비활성화합니다. 따라서 이 두 개의 명려은 원자적으로 실행된다.
		 이러한 원자성은 다시 활성화한 후 다음 인터럽트가 발생하기 전에 처리 될 수 있어서 시간의 약 1클록 틱만큼 낭비될 수 있다.
		 
		 이 코드는 인터럽트를 허용하고 다음 인터럽트가 발생할 떄까지 기다리는 방법을 구현하기 위해 사용된다. 이는 인터럽트 기반의 이벤트 처리나 동기화 작업에
		 유용하다. 
		*/
		asm volatile ("sti; hlt" : : : "memory");
	}
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) {
	ASSERT (function != NULL);

	intr_enable ();       /* The scheduler runs with interrupts off. */ // 인터럽트를 활성화, -> 스케줄러가 인터럽트가 비활성화된 상태에서 실행되어야함
	function (aux);       /* Execute the thread function. */ // 함수 실행을 통해 스레드는 원하는 작업을 수행
	thread_exit ();       /* If function() returns, kill the thread. */ // 현재 스레드를 종료, 스레드가 실행을 마치고, 스레드 자체를 종료시키는 역할
}


/* Does basic initialization of T as a blocked thread named
   NAME. */ 
// kernel thread
static void
init_thread (struct thread *t, const char *name, int priority) {
	ASSERT (t != NULL); // null인 경우에는 assert 문에 의해 프로그램이 중단된다.
	ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT (name != NULL);

	memset (t, 0, sizeof *t);
	t->status = THREAD_BLOCKED;
	strlcpy (t->name, name, sizeof t->name);
	t->tf.rsp = (uint64_t) t + PGSIZE - sizeof (void *);
	t->priority = priority;
	t->magic = THREAD_MAGIC;
	// 초기화
	t->wait_on_lock = NULL;
	list_init(&t->donations);
	t->init_priority = priority;


	list_init (&t->child_list);
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
/* 다음 스케줄링 될 스레드를 선택하고 반환한다. 런 큐에 스레드가 있는 경우, 런 큐에서 스레드를 반환해야한다. 런큐가 비어있는 경우
idle_thread를 반환한다.*/
static struct thread *
next_thread_to_run (void) {
	if (list_empty (&ready_list)) // 준비 큐가 비어있는지 확인한다. 준비 큐는 실행 가능한 스레드들의 리스트로, 비어있는 것은 현재 실행 가능한 스레드가 없음을 
		return idle_thread;        // 의미한다. 만약 비어있다면 idle_thread를 반환, 이는 대기 상태에 있는 스레드로, 아무런 작업도 수행하지 않고 cpu를 놀게둔다.
	else
		return list_entry (list_pop_front (&ready_list), struct thread, elem); // 만약 비어있지 않으면, 준비큐에서 첫 번째 스레드를 꺼내온다
}

/* Use iretq to launch the thread */ // iretq는 언터럽트를 반환하고 스레드의 실행을 시작하기 위해 사용되는 어셈블리 명령어 
void 								// 인터럽트 핸들러나 예외처리가 끝나고 해당 스레드의 실행을 재개하는데 사용된다.
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

   /*
   새로운 스레드의 페이지 테이블을 활성화하고, 이전 스레드가 종료 중인 경우, 없애는 방식으로 스레드를 전환한다.

   이 함수가 호출 될 때, 우리는 이미 이전 스레드 PREV로부터 스레드 전환을 수행했으며, 새로운 스레드는 이미 실행중이며 인터럽트는
   아직 비활성화된 상태다.
   
   */
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
	 /*
	 주요한 스위칭 로직
	 먼저 전체 실행 컨텍스트를 intr_frame으로 복원한 다음,
	 do_iret을 호출하여 다음 스레드로 전환한다.
	 주의할 점은 스위칭이 완료될 때까지 어떠한 스택도 사용해서는 안된다는 것이다.

		1. 전체 실행 컨텍스트를 'intr_frame'에 복원합니다. 이는 이전 스레드의 실행 상태를 'intr_frame'에 저장하여 다음으로 전환될 스레드가 이전 스레드의 
		상태를 그대로 복원할 수 있도록 하는 작업
		2. 'do_iret'를 호출하여 다음 스레드로 전환합니다. 'do_iret'는 스레드 전환을 수행하는 함수로, 이전 스레드의 실행을 중단하고 다음 스레드의 실행을 시작한다.
		3. 현재 스레드의 실행이 중단 되고 다음 스레드로 전환되는 동안에는 이 코드에서 새로운 스택을 사용하지 않아야한다.
	 */
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

 /*새로운 프로세스를 스케줄한다. 진입 시에는 인터럽트가 비활성화되어야 한다.
 현재 스레드의 상태를 status로 변경한 다음, 실행할 다른 스레드를 찾아 전환한다.
 schedule()함수 내에서 printf()를 호출하는 것은 안전하지 않다.*/

	/*1. 함수가 호출되면 인터럽트가 비활성화 되어야한다. 이는 다른 스레드를 스케줄하는 동안 인터럽트가 발생하지 않도록
	하기 위함 
	2. 함수는 현재 스레드의 상태를 status로 변경한다. 이는 현재 스레드의 실행사태를 지정된 status로 변경하는 작업이다.
	3. 그런 다음 , 실행할 다른 스레드를 찾아 전환한다. 이는 다음으로 실행될 스레드를 선택하고, 해당 스레드의 실행을 시작한는 작업을 수행한다.
	4. 주의해야할 점은 schedule()함수 내에서 printf()를 호출하는 것은 안전하지 않다. */
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
/* 현재 실행중인 스레드(curr)와 다음에 실행될 스레드(next)를 선택하고, 스레드의 상태를 변경하여 스케줄링을 진행한다
Assert문을 통해 인터럽트가 비활성화된 상태고, 현재 스레드의 상태가 threa_running이 아닌 것을 확인한다.& 다음 실행될 스레드가 유효한 스레드인지 검사
*/
static void
schedule (void) {
	struct thread *curr = running_thread ();
	struct thread *next = next_thread_to_run ();

	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (curr->status != THREAD_RUNNING);
	ASSERT (is_thread (next));
	/* Mark us as running. */
	next->status = THREAD_RUNNING;

	/* Start new time slice. */
	thread_ticks = 0;
	// thread_thicks변수를 0으로 초기화하여 새로운 타임 슬라이스가 시작되었음을 표시한다.
#ifdef USERPROG
	/* Activate the new address space. */
	process_activate (next);
#endif
	// curr과 next가 다른 경우, 현재 스레드가 종료 중인 상태인 경우, 해당 스레드의 메모리 해제 작업을 예약한다.
	// 실제 메모리 해제 작업은 schedule()함수의 시작 부분에서 호출
	if (curr != next) {
		/* If the thread we switched from is dying, destroy its struct
		   thread. This must happen late so that thread_exit() doesn't
		   pull out the rug under itself.
		   We just queuing the page free reqeust here because the page is
		   currently used by the stack.
		   The real destruction logic will be called at the beginning of the
		   schedule(). */
		if (curr && curr->status == THREAD_DYING && curr != initial_thread) {
			ASSERT (curr != next);
			list_push_back (&destruction_req, &curr->elem);
		}

		/* Before switching the thread, we first save the information
		 * of current running. */
		// 다음 스레드로 전환하기 전에 현재 실행 중인 스레드의 정보를 저장
		thread_launch (next);
	}
}

/* Returns a tid to use for a new thread. */
// 새로운 스레드 생성할 떄 사용할 TID를 할당, 현재까지 사용된 TID에서 1을 증가시킨 값을 새로운 TID로 사용하고 동시성 문제를 피하기 위해 lock사용
static tid_t
allocate_tid (void) {
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire (&tid_lock);
	tid = next_tid++;
	lock_release (&tid_lock);

	return tid;
}
