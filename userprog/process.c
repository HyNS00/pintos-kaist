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

#ifdef VM
#include "vm/vm.h"
#endif

static void process_cleanup (void);
static bool load (const char *file_name, struct intr_frame *if_);
static void initd (void *f_name);
static void __do_fork (void *);

/* General process initializer for initd and other process. */
/* initd 및 기타 프로세스를 위한 일반적인 프로세스 초기화하기*/
static void
process_init (void) {
	struct thread *current = thread_current ();
}

/* Starts the first userland program, called "initd", loaded from FILE_NAME.
 * The new thread may be scheduled (and may even exit)
 * before process_create_initd() returns. Returns the initd's
 * thread id, or TID_ERROR if the thread cannot be created.
 * Notice that THIS SHOULD BE CALLED ONCE. */
/*
inid를 file_name에서 로드하고 시작한다.
새로운 스레드는 process_create_initd()가 반환되기 전에
스케줄 될 수 있으며
스레드 ID를 반환하거나 스레드를 생성할 수 없는 경우 TID_ERROR를 반환한다.
한번만 호출되어야 한다.
*/
tid_t
process_create_initd (const char *file_name) {
	char *fn_copy;
	tid_t tid;

	/* Make a copy of FILE_NAME.
	 * Otherwise there's a race between the caller and load(). */
	/*file_name의 사본을 만든다. 호출자와 load()함수 간에 경합이 발생*/
	fn_copy = palloc_get_page (0);
	if (fn_copy == NULL)
		return TID_ERROR;
	strlcpy (fn_copy, file_name, PGSIZE);
	char *temp_ptr;
	strtok_r(file_name," ",&temp_ptr);
	/* Create a new thread to execute FILE_NAME. */
	/*file_name을 실행하기 위해 새로운 스레드 생성*/
	tid = thread_create (file_name, PRI_DEFAULT, initd, fn_copy); // 우선순위는 pri_default로 설정되고 initd함수를 실행하도록 지정한다.
	if (tid == TID_ERROR)
		palloc_free_page (fn_copy); // 할당 페이지 해제
	return tid;
}

/* A thread function that launches first user process. */
/* 첫 번째 사용자 프로세스를 실행하는 스레드함수*/
static void
initd (void *f_name) { // 스레드 함수의 이름은 initd
#ifdef VM
	supplemental_page_table_init (&thread_current ()->spt); // 컴파일 타임에 VM이 정의되어 있는지 확인하는 전처러기 지시문
#endif

	process_init (); // 프로세스 관리를 초기화하는 함수를 호출

	if (process_exec (f_name) < 0) // f_name을 실행하는  함수를 호출., 0보다 작으면 비정상적으로 종료
		PANIC("Fail to launch initd\n");
	NOT_REACHED (); // 비정상적인 상황이 발생했을 때 코드
}

/* Clones the current process as `name`. Returns the new process's thread id, or
 * TID_ERROR if the thread cannot be created. */

/* 현재 프로세스를 name으로 복제한다. 새로운 프로세스의 ID를 반환하고 스레드를 생성할 수 없는 경우 TID_ERROR을 반환*/
// UNUSED는 해당 변수가 사용되지 않을 때 컴파일러에게 경고하지 않도록 하는 매크로다.
// 변수가 선언되었지만, 사용되지 않는 경우에 UNUSED를 사용하여 경고 메시지를 방지할 수 있다.
tid_t
process_fork (const char *name, struct intr_frame *if_ UNUSED) { // 현재 스레드를 보겢하여 새로운 스레드를 생성하는 역할을 한다. 반환값은 새로운 스레드의 스레드ID
	/* Clone current thread to new thread.*/
	struct thread *cur = thread_current(); // 현재 스레드의 정보를 저장
	memcpy(&cur->parent_if, if_, sizeof(struct intr_frame));
	
	tid_t tid = thread_create(name,PRI_DEFAULT,__do_fork,cur);
	if(tid == TID_ERROR){
		return TID_ERROR;
	}
	struct thread *child = get_child_with_pid(tid);
	sema_down(&child ->fork_sema);
	if(child->exit_status == -1){
		return TID_ERROR;
	}
	return tid;
}

#ifndef VM
/* Duplicate the parent's address space by passing this function to the
 * pml4_for_each. This is only for the project 2. */
/*
이 함수를 pml4_for_each에 전달하여 부모의 주소 공간을 복제한다.
*/
static bool
duplicate_pte (uint64_t *pte, void *va, void *aux) { // pte:페이지 테이블 엔트리의 주소 ,va는 가상주소 ,즉 가상 주소를 기준으로 부모의 주소 공간을 복제하는 역할
	struct thread *current = thread_current ();
	struct thread *parent = (struct thread *) aux;
	void *parent_page;
	void *newpage;
	bool writable;

	/* 1. TODO: If the parent_page is kernel page, then return immediately. */
	// 부모 페이지가 커널 페이지라면 즉시 반환
	if(is_kernel_vaddr(va)){
		return true;
	}
	/* 2. Resolve VA from the parent's page map level 4. */
	/*부모의 페이지 맵 레벨 4에서 va에 해당하는 페이지를 resolve하고 parent_page에 저장

	parent -> pml4, va를 인자로 전달하여 부모의 페이지 맵 레벨4 테이블에서 va에 해당하는 페이지 엔트리를 검색한다. 
	
	즉 parent_page 변수에는 부모 프로세스의 페이지 테이블에서 va에 해당하는 페이지의 정보가 전달되어 있다.*/
	parent_page = pml4_get_page (parent->pml4, va);
	if(parent == NULL){
		return false;
	}
	/* 3. TODO: Allocate new PAL_USER page for the child and set result to
	 *    TODO: NEWPAGE. */
	/* 
	자식을 위한 새로운 PAL_USER페이를 할당하고, 그 결과를 newpage에 설정한다.
	*/
	newpage = palloc_get_page(PAL_USER);
	if (newpage == NULL){
		printf("[fork-duplicate] failed to palloc new page\n");
		return false;
	}
	/* 4. TODO: Duplicate parent's page to the new page and
	 *    TODO: check whether parent's page is writable or not (set WRITABLE
	 *    TODO: according to the result). */
	/*
	부모의 페이지를 새로운 페이지로 복제하고, 부모 페이지가 쓰기 가능한지 확인함
	*/
	memcpy(newpage,parent_page,PGSIZE);
	writable = is_writable(pte); //pte는 parent_page의 포인터 변수다
	/* 5. Add new page to child's page table at address VA with WRITABLE
	 *    permission. */

	/* 자식 페이지의 테이블에 새로운 페이즐 VA주소에 WRITEABLE권한으로 추가한다.*/
	if (!pml4_set_page (current->pml4, va, newpage, writable)) {
		/* 6. TODO: if fail to insert page, do error handling. */
		printf("Failed to map user virtual page to given physical frame\n");
		return false;
	}		// 함수 반환값이 false인 경우 페이지를 삽입하는데 실패한 것이므로 에러처리를 수행해야 한다.
	return true;
}
#endif

/* A thread function that copies parent's execution context.
 * Hint) parent->tf does not hold the userland context of the process.
 *       That is, you are required to pass second argument of process_fork to
 *       this function. */

/*
부모의 실행 컨텍스트를 복사하는 스레드함수, parent -> tf는 사용자 영역 컨테스트를 유지하지 않는다.
process_fork의 두 번째 인자를 이 함수에 전달해야한다.
*/
static void
__do_fork (void *aux) {
	struct intr_frame if_; // if라는 인터럽트 프레임 변수를 선언
	struct thread *parent = (struct thread *) aux; // aux를 parent변수에 할당
	struct thread *current = thread_current (); 
	/* TODO: somehow pass the parent_if. (i.e. process_fork()'s if_) */ // 부모의 언터럽트 변수를 어떻게 전달할지 결정해야한다. 
	struct intr_frame *parent_if; // 부모의 실행 컨텍스트를 로컬 스택으로 복사하는 용도 . 어떻게 전달해야할지 결정
	bool succ = true;
	parent_if = &parent->parent_if;
	/* 1. Read the cpu context to local stack. */
	/* CPU 컨텍스트를 로컬 스택으로 복제한다.*/
	memcpy (&if_, parent_if, sizeof (struct intr_frame));
	// parent_if 가 가리키는 struct intr_frame의 데이터를 &if 가 가리키는 메모리 영역으로 복사하는 영역
	// &if -> 복사된 데이터를 저장할 대상 메모리 포인터, parent_if복사할 데이터가 있는 원본 메모리 영역의 포인터
	/* 2. Duplicate PT */
	/* */
	current->pml4 = pml4_create(); // 현재 스레드의 페이지 디렉터리 생성
	if (current->pml4 == NULL) // 유효성을 확인
		goto error;

	process_activate (current); // 현재 프로세스를 활성화
#ifdef VM // 가상메모리 기능이 활성화된 경우
	supplemental_page_table_init (&current->spt);
	if (!supplemental_page_table_copy (&current->spt, &parent->spt))
		goto error;
#else // 가상메모리 기능이 비활성화 된 경우
	if (!pml4_for_each (parent->pml4, duplicate_pte, parent))
		goto error;
#endif

	/* TODO: Your code goes here.
	 * TODO: Hint) To duplicate the file object, use `file_duplicate`
	 * TODO:       in include/filesys/file.h. Note that parent should not return
	 * TODO:       from the fork() until this function successfully duplicates
	 * TODO:       the resources of parent.*/

	if(parent->fd_idx == FDCOUNT_LIMIT)
		goto error;
	for(int i =0;i<FDCOUNT_LIMIT;i++) {
		struct file *file = parent->fd_table[i];
		if(file == NULL)
			continue;
		
		bool found = false;
		if(!found) {
			struct file *new_file;
			if(file > 2)
				new_file = file_duplicate(file);
			else
				new_file = file;

			current->fd_table[i] = new_file;
		}
	}
	current->fd_idx = parent->fd_idx;
	// 자식스레드가 성공적으로 생성된다면 부모 스레드를 깨운다.
	sema_up(&current->fork_sema);
	/* Finally, switch to the newly created process. */
	if (succ)
		do_iret (&if_);
		// 새로 생성된 프로세스로 전환한다.
error:
	current->exit_status = TID_ERROR;
	sema_up(&current->fork_sema);
	exit(TID_ERROR);
}

/* Switch the current execution context to the f_name.
 * Returns -1 on fail. */
int
process_exec (void *f_name) {
	char *file_name = f_name;
	bool success;
	int argc = 0;
	char *token;
	char *save_ptr;
	char file_name_copy[128];	
	/* filen_name을 공백을 기준으로 토큰으로 분리하여 argv배열에 저장
	argc는 토큰의 개수를 나타내는 변수다.

	*/
	/* We cannot use the intr_frame in the thread structure.
	 * This is because when current thread rescheduled,
	 * it stores the execution information to the member. */
	struct intr_frame _if;
	_if.ds = _if.es = _if.ss = SEL_UDSEG;
	_if.cs = SEL_UCSEG;
	_if.eflags = FLAG_IF | FLAG_MBS;
	memcpy(file_name_copy,file_name,strlen(file_name)+1);
	/* We first kill the current context */
	process_cleanup ();

	/* And then load the binary */
	success = load (file_name_copy, &_if);
	// file_name은 사용자가 커맨드 라인에 적은 f_name을 받은 변수다.
	/* If load failed, quit. */


	
	palloc_free_page (file_name); // 메모리반환
	if (!success){
		return -1;
	}
	hex_dump(_if.rsp,_if.rsp,KERN_BASE - _if.rsp,true);
	/* Start switched process. */
	do_iret (&_if);
	NOT_REACHED ();
}
struct thread *get_child_with_pid(int pid)
{
	struct thread *cur = thread_current();
	struct list *child_list = &cur->child_list;
	for (struct list_elem *e = list_begin(child_list); e != list_end(child_list); e= list_next(e))
	{
		struct thread *t = list_entry(e,struct thread,child_elem);
		if(t->tid == pid)
			return t;
	}
	return NULL;
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
/*
TID가 유효하고 호출자의 자식 스레드인 경우, 해당 스레드가 종료될 때까지 대기한다.
스레드가 예외로 인해 종료된 경우(즉, 커널에 의해 종료된 경우), 종료상태 -1을 반환한다.
TID가 유효하지 않거나 호출자의 자식 스레드가 아니거나 이미 process_wait()가 TID에 대해
호출되어 성공적으로 반환된 경우 즉시 -1을 반환
*/
int
process_wait (tid_t child_tid UNUSED) {
	/* XXX: Hint) The pintos exit if process_wait (initd), we recommend you
	 * XXX:       to add infinite loop here before
	 * XXX:       implementing the process_wait. */
	struct thread *child = get_child_with_pid(child_tid);
	if (child == NULL){
		return -1;
	}
	sema_down(&child->wait_sema);
	int exit_status = child->exit_status;
	list_remove(&child->child_elem);
	sema_up(&child->free_sema);
	return exit_status;
}

/* Exit the process. This function is called by thread_exit (). */
void
process_exit (void) {
	struct thread *curr = thread_current ();
	/* TODO: Your code goes here.
	 * TODO: Implement process termination message (see
	 * TODO: project2/process_termination.html).
	 * TODO: We recommend you to implement process resource cleanup here. */

	process_cleanup ();
}

/* Free the current process's resources. */
static void
process_cleanup (void) {
	struct thread *curr = thread_current ();

#ifdef VM
	supplemental_page_table_kill (&curr->spt);
#endif

	uint64_t *pml4;
	/* Destroy the current process's page directory and switch back
	 * to the kernel-only page directory. */
	pml4 = curr->pml4;
	if (pml4 != NULL) {
		/* Correct ordering here is crucial.  We must set
		 * cur->pagedir to NULL before switching page directories,
		 * so that a timer interrupt can't switch back to the
		 * process page directory.  We must activate the base page
		 * directory before destroying the process's page
		 * directory, or our active page directory will be one
		 * that's been freed (and cleared). */
		curr->pml4 = NULL;
		pml4_activate (NULL);
		pml4_destroy (pml4);
	}
}
// 스택에 인자를 넣는 과정
/*
1. 스택 포인터를 이용하여 인자를 저장할 스택의 위치를 결정한다.
2. 인자를 스택에 저장한다.
3. 스택 포인터를 갱신하여 다음 인자를 저장할 위치로 이동한다.
4. 필요한 경우 정렬 패딩을 추가한다.
5. 필요한 경우 인자를 가리키는 포인터를 스택에 저장한다.
후입선출에 따라 역순으로 저장 및 할당

스택은 메모리의 높은 주소에서 낮은 주소로 자라나는 방향으로 할당되기 때문에, 인자들을 거꾸로 스택에 저장하기 위해서
스택 포인터를 아래쪽으로 이동시켜야 한다. rsp 를 감소시켜서 아래로 이동한다.

rsp는 스택의 가장 위쪽을 가리키는 포인터로 사용되는데, 이 포인터를 감소시키면 스택에 새로운 데이터를 저장할 공간을 확보 할 수 있다. 그리고 rsp가 가리키는 위치에 실제로 데이터를 저장하게 된다.
아래로 이동하는 것은 스택에 데이터를 저장하기 위한 위치를 지정하는 것이며, 이를 통해 인자들을 거꾸로 스택에 저장하기 때문
*/

/* Sets up the CPU for running user code in the nest thread.
 * This function is called on every context switch. */
void
process_activate (struct thread *next) {
	/* Activate thread's page tables. */
	pml4_activate (next->pml4);

	/* Set thread's kernel stack for use in processing interrupts. */
	tss_update (next);
}

/* We load ELF binaries.  The following definitions are taken
 * from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
#define EI_NIDENT 16

#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
 * This appears at the very beginning of an ELF binary. */
struct ELF64_hdr {
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

struct ELF64_PHDR {
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

static bool setup_stack (struct intr_frame *if_);
static bool validate_segment (const struct Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes,
		bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
 * Stores the executable's entry point into *RIP
 * and its initial stack pointer into *RSP.
 * Returns true if successful, false otherwise. */
static bool
load (const char *file_name, struct intr_frame *if_) {
	struct thread *t = thread_current (); // 현재 실행 중인 스레드에 대한 포인터
	struct ELF ehdr; // ELF파일의 헤더 정보를 저장하기 위한 ehdr구조체
	struct file *file = NULL;
	off_t file_ofs;
	bool success = false;
	int i;

	char *arg_list[128];
	char *token,*save_ptr;
	int token_count = 0;
	token = strtok_r(file_name," ",&save_ptr);
	arg_list[token_count] = token;
	while(token != NULL){
		token = strtok_r(NULL, " ",&save_ptr);
		token_count ++;
		arg_list[token_count] = token;
	}
	
	/* Allocate and activate page directory. */
	t->pml4 = pml4_create ();
	if (t->pml4 == NULL)
		goto done;
	process_activate (thread_current ());

	/* Open executable file. */
	file = filesys_open (file_name);
	if (file == NULL) {
		printf ("load: %s: open failed\n", file_name);
		goto done;
	}

	/* Read and verify executable header. */
	if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
			|| memcmp (ehdr.e_ident, "\177ELF\2\1\1", 7)
			|| ehdr.e_type != 2
			|| ehdr.e_machine != 0x3E // amd64
			|| ehdr.e_version != 1
			|| ehdr.e_phentsize != sizeof (struct Phdr)
			|| ehdr.e_phnum > 1024) {
		printf ("load: %s: error loading executable\n", file_name);
		goto done;
	}

	/* Read program headers. */
	file_ofs = ehdr.e_phoff;
	for (i = 0; i < ehdr.e_phnum; i++) {
		struct Phdr phdr;

		if (file_ofs < 0 || file_ofs > file_length (file))
			goto done;
		file_seek (file, file_ofs);

		if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
			goto done;
		file_ofs += sizeof phdr;
		switch (phdr.p_type) {
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
				if (validate_segment (&phdr, file)) {
					bool writable = (phdr.p_flags & PF_W) != 0;
					uint64_t file_page = phdr.p_offset & ~PGMASK;
					uint64_t mem_page = phdr.p_vaddr & ~PGMASK;
					uint64_t page_offset = phdr.p_vaddr & PGMASK;
					uint32_t read_bytes, zero_bytes;
					if (phdr.p_filesz > 0) {
						/* Normal segment.
						 * Read initial part from disk and zero the rest. */
						read_bytes = page_offset + phdr.p_filesz;
						zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
								- read_bytes);
					} else {
						/* Entirely zero.
						 * Don't read anything from disk. */
						read_bytes = 0;
						zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
					}
					if (!load_segment (file, file_page, (void *) mem_page,
								read_bytes, zero_bytes, writable))
						goto done;
				}
				else
					goto done;
				break;
		}
	}

	/* Set up stack. */
	if (!setup_stack (if_))
		goto done;

	argument_stack(arg_list,token_count, if_);
	/* Start address. */
	if_->rip = ehdr.e_entry;

	/* TODO: Your code goes here.
	 * TODO: Implement argument passing (see project2/argument_passing.html). */

	success = true;

done:
	/* We arrive here whether the load is successful or not. */
	file_close (file);
	return success;
}

void argument_stack(char **argv, int argc, struct intr_frame *if_){
	char * arg_address[128];
	for (int i = argc -1; i>=0; i--){
		int argv_len = strlen(argv[i]);
		if_ ->rsp = if_->rsp - (argv_len +1);
		memcpy(if_ ->rsp, argv[i], argv_len+1);
		arg_address[i] = if_->rsp;
	}
	while (if_->rsp % 8 != 0)
	{
		if_-> rsp--;
		*(uint8_t *) if_->rsp =0;
	}
	for (int i = argc; i >= 0; i--){
		if_->rsp = if_->rsp -8;
		if(i == argc){
			memset(if_->rsp,0,sizeof(char**));
		} else{
			memcpy(if_->rsp, &arg_address[i],sizeof(char **));
		}
	}
	if_->rsp = if_->rsp-8;
	memset(if_->rsp,0,sizeof(void *));
	if_->R.rdi = argc;
	if_->R.rsi = if_->rsp + 8;
}
/* Checks whether PHDR describes a valid, loadable segment in
 * FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Phdr *phdr, struct file *file) {
	/* p_offset and p_vaddr must have the same page offset. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset must point within FILE. */
	if (phdr->p_offset > (uint64_t) file_length (file))
		return false;

	/* p_memsz must be at least as big as p_filesz. */
	if (phdr->p_memsz < phdr->p_filesz)
		return false;

	/* The segment must not be empty. */
	if (phdr->p_memsz == 0)
		return false;

	/* The virtual memory region must both start and end within the
	   user address space range. */
	if (!is_user_vaddr ((void *) phdr->p_vaddr))
		return false;
	if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
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
static bool install_page (void *upage, void *kpage, bool writable);

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
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	file_seek (file, ofs);
	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* Get a page of memory. */
		uint8_t *kpage = palloc_get_page (PAL_USER);
		if (kpage == NULL)
			return false;

		/* Load this page. */
		if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes) {
			palloc_free_page (kpage);
			return false;
		}
		memset (kpage + page_read_bytes, 0, page_zero_bytes);

		/* Add the page to the process's address space. */
		if (!install_page (upage, kpage, writable)) {
			printf("fail\n");
			palloc_free_page (kpage);
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
setup_stack (struct intr_frame *if_) {
	uint8_t *kpage;
	bool success = false;

	kpage = palloc_get_page (PAL_USER | PAL_ZERO);
	if (kpage != NULL) {
		success = install_page (((uint8_t *) USER_STACK) - PGSIZE, kpage, true);
		if (success)
			if_->rsp = USER_STACK;
		else
			palloc_free_page (kpage);
	}
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
static bool
install_page (void *upage, void *kpage, bool writable) {
	struct thread *t = thread_current ();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	return (pml4_get_page (t->pml4, upage) == NULL
			&& pml4_set_page (t->pml4, upage, kpage, writable));
}
#else
/* From here, codes will be used after project 3.
 * If you want to implement the function for only project 2, implement it on the
 * upper block. */

static bool
lazy_load_segment (struct page *page, void *aux) {
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
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
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* TODO: Set up aux to pass information to the lazy_load_segment. */
		void *aux = NULL;
		if (!vm_alloc_page_with_initializer (VM_ANON, upage,
					writable, lazy_load_segment, aux))
			return false;

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a PAGE of stack at the USER_STACK. Return true on success. */
static bool
setup_stack (struct intr_frame *if_) {
	bool success = false;
	void *stack_bottom = (void *) (((uint8_t *) USER_STACK) - PGSIZE);

	/* TODO: Map the stack on stack_bottom and claim the page immediately.
	 * TODO: If success, set the rsp accordingly.
	 * TODO: You should mark the page is stack. */
	/* TODO: Your code goes here */

	return success;
}
#endif /* VM */
