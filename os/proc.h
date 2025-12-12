#ifndef PROC_H
#define PROC_H

#include "types.h"

#define NPROC (16)// 宏定义，加括号更安全
#define MAX_SYSCALL_NUM 500

// Saved registers for kernel context switches. 用于内核上下文切换的保存寄存器。
struct context {
	uint64 ra;
	uint64 sp;

	// callee-saved
	uint64 s0;
	uint64 s1;
	uint64 s2;
	uint64 s3;
	uint64 s4;
	uint64 s5;
	uint64 s6;
	uint64 s7;
	uint64 s8;
	uint64 s9;
	uint64 s10;
	uint64 s11;
};

enum procstate { UNUSED, USED, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

// Per-process state 每个进程的状态
struct proc {
	enum procstate state; // Process state 进程状态
	int pid; // Process ID 进程ID（进程标识符）
	uint64 ustack; // Virtual address of user stack   用户栈的虚拟地址
	uint64 kstack; // Virtual address of kernel stack 内核栈的虚拟地址
	struct trapframe *trapframe; // data page for trampoline.S  trampoline.S的数据页（用于保存异常/中断时的寄存器上下文）
	struct context context; // swtch() here to run process      在此处使用swtch()切换以运行进程（保存进程切换时的内核上下文）
	/*
	* LAB1: you may need to add some new fields here 你可能需要在这里添加一些新字段
	*/
	unsigned char scheduled; // C没有内置bool型，用char比较省内存，范围0-255
	uint64 first_schedule_cycle;
	unsigned int syscall_times[MAX_SYSCALL_NUM];
};

/*
* LAB1: you may need to define struct for TaskInfo here 你可能需要在这里定义TaskInfo结构体
*/
typedef enum {
	UnInit,
	Ready,
	Running,
	Exited,
} TaskStatus;

typedef struct {
	TaskStatus status;
	unsigned int syscall_times[MAX_SYSCALL_NUM];
	int time;
} TaskInfo;


struct proc *curr_proc();
void exit(int);
void proc_init();
void scheduler() __attribute__((noreturn));
void sched();
void yield();
struct proc *allocproc();
// swtch.S
void swtch(struct context *, struct context *);

#endif // PROC_H