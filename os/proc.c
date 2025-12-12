#include "proc.h"
#include "defs.h"
#include "loader.h"
#include "trap.h"
#include "timer.h"

struct proc pool[NPROC];
char kstack[NPROC][PAGE_SIZE];
__attribute__((aligned(4096))) char ustack[NPROC][PAGE_SIZE];
__attribute__((aligned(4096))) char trapframe[NPROC][PAGE_SIZE];
// attribute是GCC/Clang编译器扩展，有许多用途，这里是对齐，可以优化性能，而且有些架构要求特定类型必须对齐。
//? 为什么前两个不让它对齐

extern char boot_stack_top[];
struct proc *current_proc;
struct proc idle;

int threadid()
{
	return curr_proc()->pid;
}

struct proc *curr_proc()
{
	return current_proc;
}

// initialize the proc table at boot time. 在启动时初始化进程表
void proc_init(void)
{
	struct proc *p;
	for (p = pool; p < &pool[NPROC]; p++) {
		p->state = UNUSED;
		p->kstack = (uint64)kstack[p - pool];
		p->ustack = (uint64)ustack[p - pool];
		p->trapframe = (struct trapframe *)trapframe[p - pool];
		// char是C语言中最"通用"的类型，可以安全转换为任何类型
		// 而且如果直接声明结构体数组，会调用构造函数（如果有）

		/*
		* LAB1: you may need to initialize your new fields of proc here 你可能需要在这里初始化proc结构体的新字段
		*/
		// 感觉都应该在loader.c的run_all_app()里初始化
	}
	idle.kstack = (uint64)boot_stack_top;
	idle.pid = 0;
	current_proc = &idle;
}

int allocpid()
{
	static int PID = 1;
	return PID++;
}

// Look in the process table for an UNUSED proc. 在进程表中查找 UNUSED（未使用）的进程。
// If found, initialize state required to run in the kernel. 如果找到，初始化在内核中运行所需的状态。
// If there are no free procs, or a memory allocation fails, return 0. 如果没有空闲进程，或内存分配失败，返回 0。
struct proc *allocproc(void)
{
	struct proc *p;
	// 1. 遍历进程池寻找空闲进程
	for (p = pool; p < &pool[NPROC]; p++) {
		if (p->state == UNUSED) {
			goto found; // 找到空闲进程，跳转到初始化部分
		}
	}
	return 0; // 2. 没有空闲进程，返回空指针

found:
	// 3. 找到空闲进程后的初始化工作
	p->pid = allocpid(); // 分配唯一的进程ID
	p->state = USED;     // 标记为已使用
	// 4. 清零关键数据结构
	memset(&p->context, 0, sizeof(p->context)); // 清空进程上下文
	memset(p->trapframe, 0, PAGE_SIZE);         // 清空陷入帧（一页大小）
	memset((void *)p->kstack, 0, PAGE_SIZE);    // 清空内核栈（一页大小）
	// 5. 设置上下文的关键寄存器
	p->context.ra = (uint64)usertrapret;   // 返回地址：用户陷入返回函数，swtch函数中的ret语句会切换到ra寄存器所指向的函数
	p->context.sp = p->kstack + PAGE_SIZE; // 栈指针：指向内核栈顶部
	return p; // 返回初始化好的进程指针
}

// Scheduler never returns.  It loops, doing: 
// 调度器永远不会返回。它循环执行：
//  - choose a process to run. 
//	  选择一个进程运行
//  - swtch to start running that process. 
//	  切换到该进程开始运行
//  - eventually that process transfers control
//    via swtch back to the scheduler. 
//	  最终该进程通过切换将控制权交还给调度器
void scheduler(void)
{
	struct proc *p;
	for (;;) { // 无限循环
		for (p = pool; p < &pool[NPROC]; p++) { // 遍历进程池
			if (p->state == RUNNABLE) { // 找到就绪态的进程
				/*
				* LAB1: you may need to init proc start time here
				*/
				if(p->scheduled == 0) {
					p->first_schedule_cycle = get_cycle();
					p->scheduled = 1;
				}
				p->state = RUNNING; // 标记为运行态
				current_proc = p;   // 更新全局当前进程指针
				swtch(&idle.context, &p->context);// 上下文切换
                // ↑ 这里不会立即返回，只有当进程主动让出CPU或被中断时，
                // 才会返回到这里继续调度循环
			}
		}
	}
}

// Switch to scheduler.  Must hold only p->lock     切换到调度器。必须只持有 p->lock，
// and have changed proc->state. Saves and restores 并且已经改变了 proc->state。保存和恢复
// intena because intena is a property of this      intena 是因为 intena 是这个内核线程的属性，
// kernel thread, not this CPU. It should           而不是这个 CPU 的属性。它本应该是 
// be proc->intena and proc->noff, but that would   proc->intena 和 proc->noff，但那样会在
// break in the few places where a lock is held but 少数持有锁但没有进程的情况下出现问题。
// there's no process.
void sched(void)
{
	struct proc *p = curr_proc();
	if (p->state == RUNNING)
		panic("sched running");
	swtch(&p->context, &idle.context);
}

// Give up the CPU for one scheduling round.
void yield(void)
{
	current_proc->state = RUNNABLE;
	sched();
}

// Exit the current process.
void exit(int code)
{
	struct proc *p = curr_proc();
	infof("proc %d exit with %d", p->pid, code);
	p->state = UNUSED;
	finished();
	sched();
}
