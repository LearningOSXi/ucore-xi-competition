#include "trap.h"
#include "defs.h"
#include "loader.h"
#include "syscall.h"
#include "timer.h"

extern char trampoline[], uservec[];
extern void *userret(uint64);

void kerneltrap()
{
	if ((r_sstatus() & SSTATUS_SPP) == 0)
		panic("kerneltrap: not from supervisor mode");
	panic("trap from kernel\n");
}

// set up to take exceptions and traps while in the kernel.
void set_usertrap(void)
{
	w_stvec((uint64)uservec & ~0x3); // DIRECT
}

void set_kerneltrap(void)
{
	// 将中断处理程序地址写入 stvec 寄存器
	w_stvec((uint64)kerneltrap & ~0x3); // DIRECT
}

// set up to take exceptions and traps while in the kernel.
void trap_init(void)
{
	set_kerneltrap();
}

void unknown_trap()
{
	errorf("unknown trap: %p, stval = %p\n", r_scause(), r_stval());
	exit(-1);
}

//
// handle an interrupt, exception, or system call from user space.
// 处理来自用户空间的中断、异常或系统调用。
// called from trampoline.S
// 从 trampoline.S 中调用
//
void usertrap()
{
	// 设置 stvec = kerneltrap
	set_kerneltrap();
	struct trapframe *trapframe = curr_proc()->trapframe;

	// 检查陷入发生前的特权模式，确保 usertrap() 只处理来自用户空间的陷入
	if ((r_sstatus() & SSTATUS_SPP) != 0)
		panic("usertrap: not from user mode");

	// 读取陷入原因寄存器scause
	// scause[63] = 1表示中断，0表示异常
	// scause[62:0] = 具体原因编号
	uint64 cause = r_scause();

	// 检查最高位是否为1（表示是中断）
	if (cause & (1ULL << 63)) {
		// 清除中断标志，得到纯中断编号
		cause &= ~(1ULL << 63);

		// 根据具体中断类型处理
		switch (cause) {
		case SupervisorTimer: // 监管者定时器中断（来自时钟）
			tracef("time interrupt!\n"); // 调试信息
			set_next_timer(); // 设置下一个定时器中断（通常是1个tick后）
			yield();          // 主动让出CPU，触发进程调度，即继续执行scheduler
			break;
		default:
			unknown_trap();   // 未知中断类型，错误处理
			break;
		}
	} else {
		// 根据具体异常类型处理
		switch (cause) {
		case UserEnvCall: // 用户环境调用（即系统调用）
			// ecall指令让用户程序主动陷入内核，需要跳过这条指令
			// 否则返回用户空间后会重复执行ecall，死循环！
			trapframe->epc += 4; // RISC-V指令都是4字节，所以+4

			// 处理系统调用，根据系统调用号分派到具体函数
			syscall();
			break;
		// 这些异常通常表示用户程序有bug或恶意行为
		case StoreMisaligned:       // 存储地址不对齐（如向地址1存8字节）
		case StorePageFault:        // 存储页错误（写不存在的页或无权限页）
		case InstructionMisaligned: // 指令地址不对齐（PC不是4的倍数）
		case InstructionPageFault:  // 指令页错误（执行不存在的代码页）
		case LoadMisaligned:        // 加载地址不对齐
		case LoadPageFault:         // 加载页错误（读不存在的页或无权限页）
			// 打印详细的错误信息，帮助调试
			// cause: 异常编号
			// r_stval(): 出错的虚拟地址（保存在stval寄存器）
			// trapframe->epc: 引发异常的指令地址
			printf("%d in application, bad addr = %p, bad instruction = %p, "
			       "core dumped.\n",
			       cause, r_stval(), trapframe->epc);
			exit(-2);
			break;
		case IllegalInstruction: // 非法指令异常
			// 用户程序执行了CPU不认识的指令码
			printf("IllegalInstruction in application, core dumped.\n");
			exit(-3); // 终止进程，返回错误码-3
			break;
		default: // 其他未知异常
			unknown_trap();
			break;
		}
	}
	// 所有处理完成后，准备返回用户空间
	// usertrapret会设置返回所需的所有状态，然后调用userret
	usertrapret();
}

//
// return to user space 返回用户空间
//
void usertrapret()
{
	// 设置用户空间陷入处理程序
	set_usertrap();
	// 获取当前进程的陷入帧
	struct trapframe *trapframe = curr_proc()->trapframe;
	trapframe->kernel_satp = r_satp(); // kernel page table 内核页表
	trapframe->kernel_sp =
		curr_proc()->kstack + PGSIZE; // process's kernel stack 进程的内核栈
	trapframe->kernel_trap = (uint64)usertrap; // 用户陷入处理函数地址
	trapframe->kernel_hartid = r_tp(); // hartid for cpuid() CPU核心ID（用于CPU识别）

	w_sepc(trapframe->epc);
	// set up the registers that trampoline.S's sret will use 设置 trampoline.S 中 sret 指令将使用的寄存器
	// to get to user space. 以返回到用户空间

	// set S Previous Privilege mode to User. 设置"之前特权模式"为用户模式
	uint64 x = r_sstatus();
	x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode 清除SPP位，设置为0表示用户模式
	x |= SSTATUS_SPIE; // enable interrupts in user mode 在用户模式下启用中断
	w_sstatus(x);

	// tell trampoline.S the user page table to switch to. 告诉 trampoline.S 要切换到哪个用户页表
	// uint64 satp = MAKE_SATP(p->pagetable);
	userret((uint64)trapframe);
}