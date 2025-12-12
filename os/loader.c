#include "loader.h"
#include "defs.h"
#include "trap.h"

static uint64 app_num;
static uint64 *app_info_ptr;
extern char _app_num[], ekernel[];

// Count finished programs. If all apps exited, shutdown.
int finished()
{
	static int fin = 0;
	if (++fin >= app_num)
		panic("all apps over");
	return 0;
}

// Get user progs' infomation through pre-defined symbol in `link_app.S`
// 通过 `link_app.S` 中预定义的符号获取用户程序的信息
void loader_init()
{
	if ((uint64)ekernel >= BASE_ADDRESS) {
		panic("kernel too large...\n");
	}
	app_info_ptr = (uint64 *)_app_num;
	app_num = *app_info_ptr;
	app_info_ptr++;
}

// Load nth user app at
// [BASE_ADDRESS + n * MAX_APP_SIZE, BASE_ADDRESS + (n+1) * MAX_APP_SIZE)
int load_app(int n, uint64 *info)
{
	uint64 start = info[n], end = info[n + 1], length = end - start;
	memset((void *)BASE_ADDRESS + n * MAX_APP_SIZE, 0, MAX_APP_SIZE);
	memmove((void *)BASE_ADDRESS + n * MAX_APP_SIZE, (void *)start, length);
	return length;
}

// load all apps and init the corresponding `proc` structure. 加载所有应用程序并初始化对应的 `proc` 结构
int run_all_app()
{
	for (int i = 0; i < app_num; ++i) {
		struct proc *p = allocproc();
		struct trapframe *trapframe = p->trapframe;
		//? trapframe是干什么用的？
		load_app(i, app_info_ptr); 
		//? 用户程序不是已经在内核执行前，和内核一起加载到QEMU的内存中了吗？为什么这里还要加载，这是从哪里加载到哪里？
		// AI说是从数据段复制到执行的位置，数据段是不能直接执行的，要加载到执行位置，进行重定位等准备过程，然后执行。
		uint64 entry = BASE_ADDRESS + i * MAX_APP_SIZE;
		tracef("load app %d at %p", i, entry);
		trapframe->epc = entry;
		trapframe->sp = (uint64)p->ustack + USER_STACK_SIZE;
		p->state = RUNNABLE;
		/*
		* LAB1: you may need to initialize your new fields of proc here 你可能需要在这里初始化proc的新字段
		*/
		p->scheduled = 0;
		for(int i = 0; i < MAX_SYSCALL_NUM; i++)
			p->syscall_times[i] = 0;
	}
	return 0;
}