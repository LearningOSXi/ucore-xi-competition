#include "console.h"
#include "defs.h"
#include "loader.h"
#include "timer.h"
#include "trap.h"

void clean_bss()
{
	// 将.bss段全部置零，确保全局变量和静态变量初始值为0。
	extern char s_bss[];
	extern char e_bss[];
	memset(s_bss, 0, e_bss - s_bss);
}

void main()
{
	clean_bss();   // 清理.bss段
	proc_init();   // 初始化进程管理数据结构
	loader_init(); // 初始化应用程序加载器
	trap_init();   // 初始化中断和异常处理机制
	timer_init();  // 初始化时钟中断
	run_all_app(); // 加载并启动所有用户程序
	infof("start scheduler!");
	scheduler();   // 核心调度器
}
