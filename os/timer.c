#include "timer.h"
#include "riscv.h"
#include "sbi.h"

/// read the `mtime` regiser
uint64 get_cycle()
{
	return r_time();
}

/// Enable timer interrupt 启用定时器中断
void timer_init()
{
	// Enable supervisor timer interrupt 启用监管者模式定时器中断
	w_sie(r_sie() | SIE_STIE); // 设置SIE寄存器的STIE位
	set_next_timer();          // 设置下一个定时器中断时间
}

/// Set the next timer interrupt 设置下一个定时器中断
void set_next_timer()
{
	const uint64 timebase = CPU_FREQ / TICKS_PER_SEC; // 计算每个tick的时钟周期数
	set_timer(get_cycle() + timebase);                // 设置下次中断的时间
	// set_timer是设置timecmp寄存器（属于MMIO）的值，而S-mode权限不够，所以最终会用ecall切换到M-mode
	// get_cycle()会读取time寄存器（属于CSR）的值，S-mode有读取权限，会使用内联汇编读取
	// 时钟中断是由硬件自动触发，RISC-V 硬件的中断逻辑（简化）：
	// 1. 每个时钟周期：time = time + 1
	// 2. 每个时钟周期：比较 time 和 timecmp
	// 3. 如果 time >= timecmp：设置 mip.MTIP = 1
	// 4. 如果同时满足：
	//    - mip.MTIP == 1       (中断挂起)
	//    - mie.MTIE == 1       (中断使能)
	//    - mstatus.MIE == 1    (全局中断使能)
	//    → 硬件自动跳转到 mtvec/stvec（中断处理程序入口）
	//		（M-mode定时器中段：使用 mtvec，S-mode使用stvec）
	//		（ucore是内核，特权级为S-mode，使用stvec，在trap_init()里设置）
	// 中断触发后，由操作系统处理终端，然后清除中断标志，设置下一个timecmp
}