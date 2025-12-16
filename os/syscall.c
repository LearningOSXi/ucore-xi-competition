#include "syscall.h"
#include "defs.h"
#include "loader.h"
#include "syscall_ids.h"
#include "timer.h"
#include "trap.h"
#include "proc.h"
#include "vm.h"
#include "riscv.h"

uint64 sys_write(int fd, uint64 va, uint len)
{
	debugf("sys_write fd = %d va = %x, len = %d", fd, va, len);
	if (fd != STDOUT)
		return -1;
	struct proc *p = curr_proc();
	char str[MAX_STR_LEN];
	int size = copyinstr(p->pagetable, str, va, MIN(len, MAX_STR_LEN));
	debugf("size = %d", size);
	for (int i = 0; i < size; ++i) {
		console_putchar(str[i]);
	}
	return size;
}

__attribute__((noreturn)) void sys_exit(int code)
{
	exit(code);
	__builtin_unreachable();
}

uint64 sys_sched_yield()
{
	yield();
	return 0;
}

uint64 sys_gettimeofday(TimeVal *val, int _tz) // TODO: implement sys_gettimeofday in pagetable. (VA to PA)
{
	// YOUR CODE
	val->sec = 0;
	val->usec = 0;

	/* The code in `ch3` will leads to memory bugs*/

	// uint64 cycle = get_cycle();
	// val->sec = cycle / CPU_FREQ;
	// val->usec = (cycle % CPU_FREQ) * 1000000 / CPU_FREQ;
	return 0;
}

uint64 sys_sbrk(int n)
{
	uint64 addr;
	struct proc *p = curr_proc();
	addr = p->program_brk;
	if(growproc(n) < 0)
		return -1;
	return addr;	
}

uint64 sys_mmap(void *start, unsigned long long len, int port, int flag, int sd)
{
	if(len > (1ULL << 30)) return -1; // 最大不可超过1GB
	if(len == 0) return 0;

	uint64 va = (uint64)start;
	uint64 vend = PGROUNDUP(va+len);

	// start 没有页对齐 || 物理内存不足  || port其他位不全为0 || 不可读不可写不可执行的内存
	if(!PGALIGNED(va) || vend > MAXVA || (port & ~0x7) != 0 || (port & 0x7) == 0){ 
		return -1;
	}

	pagetable_t pagetable = curr_proc()->pagetable;

	// 权限
	int perm = (port << 1) | PTE_U;

	// 检查是否存在被映射过的虚存
	for(uint64 a = va; a < vend; a+=PAGE_SIZE){
		if(walkaddr(pagetable, a) != 0)
			return -1;
	}

	// 分配并映射
	for(;va < vend; va+=PAGE_SIZE){
		uint64 pa = (uint64)kalloc();
		if(pa == 0) return -1; // 没有空闲物理页
		memset((void*)pa, 0, PAGE_SIZE);
		if(mappages(pagetable, va, PAGE_SIZE, pa, perm) != 0 ){ 
			//已被映射或者页表物理页分配失败
			return -1;
		}
	}
	
	return 0;
}

uint64 sys_munmap(void *start, unsigned long long len){
	if(len > (1ULL << 30)) return -1; // 最大不可超过1GB
	if(len == 0) return 0;

	uint64 va = (uint64)start;
	uint64 vend = PGROUNDUP(va+len);

	// 检查start有没有页对齐
	if(!PGALIGNED(va) || vend > MAXVA) return -1; 

	pagetable_t pagetable = curr_proc()->pagetable;
	
	// 检查是否存在未被映射的虚存
	for(uint64 a = va; a < vend; a+=PAGE_SIZE){
		if(walkaddr(pagetable, a) == 0)
			return -1;
	}

	// 取消映射
	int npages = (vend-va) / PGSIZE;
	uvmunmap(pagetable, va, npages, 1);

	return 0;
}


// TODO: add support for mmap and munmap syscall.
// hint: read through docstrings in vm.c. Watching CH4 video may also help.
// Note the return value and PTE flags (especially U,X,W,R)
/*
* LAB1: you may need to define sys_task_info here
*/

extern char trap_page[];

void syscall()
{
	struct trapframe *trapframe = curr_proc()->trapframe;
	int id = trapframe->a7, ret;
	uint64 args[6] = { trapframe->a0, trapframe->a1, trapframe->a2,
			   trapframe->a3, trapframe->a4, trapframe->a5 };
	tracef("syscall %d args = [%x, %x, %x, %x, %x, %x]", id, args[0],
	       args[1], args[2], args[3], args[4], args[5]);
	/*
	* LAB1: you may need to update syscall counter for task info here
	*/
	switch (id) {
	case SYS_write:
		ret = sys_write(args[0], args[1], args[2]);
		break;
	case SYS_exit:
		sys_exit(args[0]);
		// __builtin_unreachable();
	case SYS_sched_yield:
		ret = sys_sched_yield();
		break;
	case SYS_gettimeofday:
		ret = sys_gettimeofday((TimeVal *)args[0], args[1]);
		break;
	case SYS_sbrk:
		ret = sys_sbrk(args[0]);
		break;
	/*
	* LAB1: you may need to add SYS_taskinfo case here
	*/
	case SYS_mmap:
		ret = sys_mmap((void *)args[0], args[1], args[2], args[3], args[4]);
		break;
	case SYS_munmap:
		ret = sys_munmap((void *)args[0], args[1]);
		break;
	default:
		ret = -1;
		errorf("unknown syscall %d", id);
	}
	trapframe->a0 = ret;
	tracef("syscall ret %d", ret);
}
