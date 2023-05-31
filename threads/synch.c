/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
   */

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
   decrement it.

   - up or "V": increment the value (and wake up one waiting
   thread, if any). */

/*
세마포어 SEMA를 VALUE로 초기화한다, 세마포어는 음수가 아닌 정수와 이를 조작하는 두 가지 연산자로 구성된다.
	- down 또는 P: 값이 양수가 될 때까지 기다린 후, 값을 감소시킨다.
	- up 또는 V : 값을 증가시킨다(대기 중인 스레드가 있다면 하나를 깨운다)
*/
void
sema_init (struct semaphore *sema, unsigned value) {
	ASSERT (sema != NULL);

	sema->value = value;
	list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. This is
   sema_down function. */

/* 
세마포어에 대한 Down / P연산이다. SEMA의 값이 양수가 될 때까지 대기하고, 그런 다음 값을 원자적으로 감소시킨다.

함수는 슬립할 수 없으므로, 인터럽트 핸들러 내에서 호출해서는 안된다.
인터럽트가 비활성화된 상태에서 호출될 수 있지만, 슬립한 경우 다음 예약된 스레드는 인터럽트를 다시 활성화할 가능성이 있다.
*/
// 'sema down'은 세마포어의 값을 하나 감소시키고, 세마포어의 값이 0이면 현재 실행 중인 스레드를 블록시키고 대기 리스트에 추가하는 역할을 수행한다.
// 세마포어를 사용하여 공유 자원에 대한 동기화를 달성할 수 있께 한다.세마포어 값이 0인 경우에는 스레드를 대기 상태로 전환하고 , 다른 스레드가 세마포어를 해제하여 값이 증가하면 대기중인 슬레드가
// 실행을 재개할 수 있다.
void
sema_down (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);
	ASSERT (!intr_context ());

	old_level = intr_disable ();
	while (sema->value == 0) {
		list_insert_ordered(&sema ->waiters,&thread_current() ->elem,thread_compare_priority,0);
		
		// list_push_back (&sema->waiters, &thread_current ()->elem);
		thread_block ();
	}
	sema->value--;
	intr_set_level (old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
/*
세마포어에 대한 Down/P연산이다. 단 세마포어가 0이 아닌 경우에만 수행된다.
세마포어가 감소된 경우 true를 반환하고, 그렇지 않은 경우 false를 반환한다.
*/
bool
sema_try_down (struct semaphore *sema) {
	enum intr_level old_level;
	bool success;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (sema->value > 0)
	{
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level (old_level);

	return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
/*
세마포어에 대한 UP/V연산이다. SEMA의 값을 증가시키고, SEMA를 기다리는 스레드 중 하나를 깨운다(있을 경우)
*/
// 세마포어의 값을 증가시키고, 대기 중인 스레드 중 하나를 깨운다.
void
sema_up (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (!list_empty (&sema->waiters)) {
		// waiters 내부에서 우선순위 순으로 정렬
		list_sort(&sema->waiters,thread_compare_priority,0);
		thread_unblock (list_entry (list_pop_front (&sema->waiters),
					struct thread, elem));
		// 앞에서부터 unblock
	}
	sema->value++;
	// ready_list로 보냈으니 ready_list의 첫 번째 스레드와 현재 cpu를 차지하고 있는 스레드를 비교하는 함수
	max_priority();
	intr_set_level (old_level);
}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */

/*
세마포어의 자체 테스트다. 두 개의 스레드 간에 핑퐁제어를 수행한다. 상황을 확인하기 위해 printf()호출을 삽입가능
*/
void
sema_self_test (void) {
	struct semaphore sema[2];
	int i;

	printf ("Testing semaphores...");
	sema_init (&sema[0], 0);
	sema_init (&sema[1], 0);
	thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up (&sema[0]);
		sema_down (&sema[1]);
	}
	printf ("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper (void *sema_) {
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down (&sema[0]);
		sema_up (&sema[1]);
	}
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */

/*
LOCK을 초기화한다. 잠금은 한 번에 최대 하나의 스레드에 의해 소유될 수 있다.
잠금은 재귀적이 아니다. 즉 현재 잠금을 소유하고 있는 스레드가 해당 잠금을 얻으려고 시도하는 것은 오류다.

잠금은 초기값이 1인 세마포어의 특수 형태다. 잠금과 세마포어의 차이점은 두 가지다.
1. 세마포어는 1보다 큰 값을 가질 수 있지만, 잠금은 한 번에 하나의 스레드만 소유할 수 있다.
2. 세마포어는 소유자가 없으므로 한 스레드가 세마포어를 down하고 다른 스레드가 up할 수 있지만 잠금은 동일한 스레드가 동일한 스레드가 잠금을
획득하고 해제해야한다. 제한이 불편하게 느껴질 때는 잠금 대신 세마포어를 사용해야 하는 좋은 신호다.
*/
void
lock_init (struct lock *lock) {
	ASSERT (lock != NULL);

	lock->holder = NULL;
	sema_init (&lock->semaphore, 1);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */

/*
LOCK을 획득하며, 필요한 경우 사용 가능할 때까지 슬립한다.
잠금은 현재 스레드에 의해 이미 소유되어서는 안된다

이 함수는 슬립할 수 있으므로, 인터럽트 핸들러 내에서 호출해서는 안된다
이 함수는 인터럽트가 비활성화된 상태에서 호출될 수 있지만, 슬립이 필요한 경우
인터럽트가 다시 활성화될 수 있다.
*/
void
lock_acquire (struct lock *lock) {
	ASSERT (lock != NULL); //lock이 NULL이 아닌지 검사
	ASSERT (!intr_context ()); // 현재 코드가 인터럽트 컨텍스트에서 실행되지 않아야함을 검사한다. 인터럽트 컨텍스트에서는 일부 동작이 제한되거나 다르게 동작할 수 있기 때문
	ASSERT (!lock_held_by_current_thread (lock)); //현재 스레드가 lock을 이미 소유하고 있지 않아야함을 검사한다.
	struct thread *cur = thread_current();
	if(lock ->holder){
		cur->wait_on_lock = lock;
		list_insert_ordered(&lock->holder->donations, &cur->donation_element, compare_donate_priority,0);
		donate_priority();
	}
	// lock을 얻기 전에 lock을 가지고 있는 스레드에게 priority를 양도해야한다.

	sema_down (&lock->semaphore);
	cur -> wait_on_lock = NULL;
	lock->holder = thread_current ();
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
/* LOCK을 획득하려고 시도하고, 성공하면 true, 실패하면 false를 반환한다. 
잠금은 현재 스레드에 의해 소유되어서는 안된다.
이 함수는 슬립하지 않으므로, 인터럽트 핸들러 내에서 호출될 수 있다.*/
bool
lock_try_acquire (struct lock *lock) {
	bool success;

	ASSERT (lock != NULL);
	ASSERT (!lock_held_by_current_thread (lock));

	success = sema_try_down (&lock->semaphore);
	if (success)
		lock->holder = thread_current ();
	return success;
}

/* Releases LOCK, which must be owned by the current thread.
   This is lock_release function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
/*
현재 스레드가 소유한 LOCK을 해제한다. 이것은 lock_release함수다.
인터럽트 핸들러는 잠금을 획득할 수 없으므로, 인터럽트 핸들러 내에서 잠금을 해제하려는 것은 의미가 없다.
*/

/*
5/31 priority를 양도받아 lock을 반환할 때의 경우를 만들어줘야한다.
지금은 그냥 lock을 해제하고 sema up을 해주는 것이 전부

*/
void
lock_release (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (lock_held_by_current_thread (lock));

	remove_with_lock(lock);
	refresh_priority();

	lock->holder = NULL;
	sema_up (&lock->semaphore);
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
/*
현재 스레드가 LOCK을 소유하고 있는지 여부에 따라 true를 반환하고, 그렇지 않으면 false를 반환한다.
*/
bool
lock_held_by_current_thread (const struct lock *lock) {
	ASSERT (lock != NULL);

	return lock->holder == thread_current ();
}

/* One semaphore in a list. */
struct semaphore_elem {
	struct list_elem elem;              /* List element. */
	struct semaphore semaphore;         /* This semaphore. */
};

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */

/* 조건 변수 COND를 초기화한다. 조건 변수는 한 코드가 조건을 신호화하고,
협력하는 코드가 신호를 수신하고, 이에 대응하여 동작할 수 있도록 한다.*/
void
cond_init (struct condition *cond) {
	ASSERT (cond != NULL);

	list_init (&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */

/* LOCK을 원자적으로 해제하고, 다른 코드에서 COND가 신호를 받을 때까지 기다린다.
COND가 신호를 받은 후에는 반환하기 전에 LOCK을 다시 획득한다. 이 함수를 호출하기 전에 LOCK을 보유

이 함수로 구현된 모니터는 Mesa스타일이며 Hoare스타일이 아니다.
즉 신호로 보내고 받는 것은 원자적인 연산이 아니다. 따라서 일반적으로 대기가 완료된 후에 조건을 다시 확인하고
필요한 경우 다시 대기 해야한다.

특정 조건 변수는 단 하나의 잠금과 연관되지만, 하나의 잠금은 여러 조건 변수와 연관될 수 있다.
즉 잠금에서 조건 변수로의 일대다 매핑이 있다.*/
// 이 함수는 스레드를 조건 변수를 사용하여 스레드를 대기 상태로 전환하는 cond_wait함수의 구현이다.
void
cond_wait (struct condition *cond, struct lock *lock) {
	struct semaphore_elem waiter;




	ASSERT (cond != NULL); // 호출조건의 유효성을 검사
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	sema_init (&waiter.semaphore, 0); //waiter 변수에 세마포어를 초기화한다. 이 세마포어는 대기 상태를 나타내며 ,초기값으로 0을 설정
	list_push_back (&cond->waiters, &waiter.elem); // waiter를 조건 변수의 대기 리스트에 추가한다. 현재 스레드가 조건 변수를 기다리도록 한다.
	lock_release (lock); // 현재 스레드가 조건 변수를 기다리게 한다.
	sema_down (&waiter.semaphore); // 세마포어를 내린다. 스레드가 대기 상태로가고, 세마포어 값이 0인 경우 블록
	lock_acquire (lock); // 다시 락을 획득 
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */

/* 
COND에 대기 중인 스레드가 있다면 (LOCK으로 보호), 이 함수는 대기 중인 스레드 중 하나에게 신호를 보내
깨어나도록 한다. 이 함수를 호출하기 전에 Lock을 보유해야한다.

인터럽트 핸들러는 잠금을 획득할 수 없으므로, 인터럽트 핸들러 내에서 조건 변수에 신호를 보내려는 것은 의미가 없다.
*/
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	if (!list_empty (&cond->waiters))
		sema_up (&list_entry (list_pop_front (&cond->waiters),
					struct semaphore_elem, elem)->semaphore);
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
/* COND에 대기 중인 모든 스레드를 꺠운다. 이 함수를 호출하기 전에 LOCK을 보유해야한다

인터럽트 핸들러는 잠금을 획득할 수 없으므로, 인터럽트 핸들러 내에서 조건 변수에 신호를 보내려는 것은
의미가 없다.*/
void
cond_broadcast (struct condition *cond, struct lock *lock) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);

	while (!list_empty (&cond->waiters))
		cond_signal (cond, lock);
}
