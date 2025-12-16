#include "vm.h"
#include "defs.h"
#include "riscv.h"

pagetable_t kernel_pagetable;

extern char e_text[]; // kernel.ld sets this to end of kernel code.
extern char trampoline[];

// Make a direct-map page table for the kernel. 为内核构建一个“直接映射（恒等映射）”的页表。
// kvmmake() 分配并初始化内核页表，通过恒等映射方式映射内核代码段、内核数据段以及 trampoline 页，并返回该页表的根地址。
pagetable_t kvmmake(void)
{
	pagetable_t kpgtbl;
	kpgtbl = (pagetable_t)kalloc(); // 分配一页作为“根页表”，页表本身需要一页物理内存
	memset(kpgtbl, 0, PGSIZE);
	// map kernel text executable and read-only. 映射内核代码段（可执行、只读）。
	kvmmap(kpgtbl, KERNBASE, KERNBASE, (uint64)e_text - KERNBASE,
	       PTE_R | PTE_X);
	// map kernel data and the physical RAM we'll make use of.
	kvmmap(kpgtbl, (uint64)e_text, (uint64)e_text, PHYSTOP - (uint64)e_text,
	       PTE_R | PTE_W);
	kvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
	return kpgtbl;
}

// Initialize the one kernel_pagetable
// Switch h/w page table register to the kernel's page table,
// and enable paging.
void kvm_init(void)
{
	kernel_pagetable = kvmmake();
	w_satp(MAKE_SATP(kernel_pagetable));
	sfence_vma();
	infof("enable pageing at %p", r_satp());
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
// 返回页表 pagetable 中，与虚拟地址 va 对应的页表项（PTE）的地址；
// 如果 alloc != 0，在遍历过程中如发现缺失的页表页，则创建它们。
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// RISC-V 的 Sv39 方案使用三级页表；
// 每一级页表页包含 512 个 64 位的页表项（PTE）。
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
// 一个 64 位虚拟地址被拆分为五个部分：
// 39–63 位：必须为 0（否则非法）
// 30–38 位：二级页表索引（level-2）
// 21–29 位：一级页表索引（level-1）
// 12–20 位：零级页表索引（level-0）
// 0–11 位：页内偏移（4KB）
pte_t *walk(pagetable_t pagetable, uint64 va, int alloc)
{
	// walk() 沿着 Sv39 的三级页表结构，根据虚拟地址 va 逐级向下查找，
	// 必要时分配中间页表页，最终返回最低一级（level-0）的 PTE 地址。
	if (va >= MAXVA)
		panic("walk");

	for (int level = 2; level > 0; level--) {
		pte_t *pte = &pagetable[PX(level, va)]; // 得到level级页表项
		if (*pte & PTE_V) {// 如果页表项是否合法
			pagetable = (pagetable_t)PTE2PA(*pte); // 得到页的起始地址
		} else { // 这个地址还没分配
			if (!alloc || (pagetable = (pde_t *)kalloc()) == 0) // 不是分配，说明是是查询，但该页不存在
				return 0;
			memset(pagetable, 0, PGSIZE);
			*pte = PA2PTE(pagetable) | PTE_V;
		}
	}
	return &pagetable[PX(0, va)]; // 返回物理页对应的页表项PTE的地址
}
// 总之，存在合法页直接返会PTE地址，不存在创建并返回PTE地址，但如果alloc=0或者创建失败返回0
// 但不考虑物理页是否合法

// Look up a virtual address, return the physical address,
// 查找一个虚拟地址，并返回它对应的物理地址。
// or 0 if not mapped.
// 如果该虚拟地址没有被映射，则返回 0。
// Can only be used to look up user pages.
// 只能用于查找用户页（不能用于内核页）。
uint64 walkaddr(pagetable_t pagetable, uint64 va)
{
	pte_t *pte;
	uint64 pa;

	if (va >= MAXVA)
		return 0;

	pte = walk(pagetable, va, 0);
	if (pte == 0)
		return 0;
	if ((*pte & PTE_V) == 0)
		return 0;
	if ((*pte & PTE_U) == 0)
		return 0;
	pa = PTE2PA(*pte);
	return pa;
}

// Look up a virtual address, return the physical address,
uint64 useraddr(pagetable_t pagetable, uint64 va)
{
	uint64 page = walkaddr(pagetable, va);
	if (page == 0)
		return 0;
	return page | (va & 0xFFFULL);
}

// Add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void kvmmap(pagetable_t kpgtbl, uint64 va, uint64 pa, uint64 sz, int perm)
{
	if (mappages(kpgtbl, va, sz, pa, perm) != 0)
		panic("kvmmap");
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned. Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
// 为从虚拟地址 va 开始的一段虚拟地址空间创建页表项（PTE），
// 使其映射到从物理地址 pa 开始的物理内存。
// va 和 size 可能不是按页对齐的。
// 如果映射成功返回 0；如果在创建页表项的过程中，walk() 无法分配所需的页表页，则返回 -1。

int mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
	// mappages() 按页建立“虚拟地址 → 物理地址”的映射关系，必要时通过 walk() 动态分配中间页表页。
	uint64 a, last;
	pte_t *pte;

	a = PGROUNDDOWN(va); // 把地址 a 向下对齐到最近的 4KB 页起始地址
	last = PGROUNDDOWN(va + size - 1);
	for (;;) {
		if ((pte = walk(pagetable, a, 1)) == 0) // 页表物理页分配失败
			return -1;
		if (*pte & PTE_V) { // 已经被映射过了
			errorf("remap");
			return -1;
		}
		*pte = PA2PTE(pa) | perm | PTE_V;
		if (a == last)
			break;
		a += PGSIZE;
		pa += PGSIZE;
	}
	return 0;
}

// Remove npages of mappings starting from va. va must be
// 从虚拟地址 va 开始，移除 npages 个页的映射关系。
// page-aligned. The mappings must exist.
// va 必须是页对齐的地址（4KB 对齐），这些映射必须已经存在。
// Optionally free the physical memory.
// 可以选择是否同时释放对应的物理内存。
void uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
	uint64 a;
	pte_t *pte;

	if ((va % PGSIZE) != 0)
		panic("uvmunmap: not aligned");

	for (a = va; a < va + npages * PGSIZE; a += PGSIZE) {
		if ((pte = walk(pagetable, a, 0)) == 0)
			continue;
		if ((*pte & PTE_V) != 0) {        // 合法页表项
			if (PTE_FLAGS(*pte) == PTE_V) // 如果后10位全是0，权限位就全为0，没有这样的页
				panic("uvmunmap: not a leaf");
			if (do_free) {
				uint64 pa = PTE2PA(*pte);
				kfree((void *)pa);
			}
		}
		*pte = 0;
	}
}

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t uvmcreate()
{
	pagetable_t pagetable;
	pagetable = (pagetable_t)kalloc();
	if (pagetable == 0) {
		errorf("uvmcreate: kalloc error");
		return 0;
	}
	memset(pagetable, 0, PGSIZE);
	if (mappages(pagetable, TRAMPOLINE, PAGE_SIZE, (uint64)trampoline,
		     PTE_R | PTE_X) < 0) {
		kfree(pagetable);
		errorf("uvmcreate: mappages error");
		return 0;
	}
	return pagetable;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void freewalk(pagetable_t pagetable)
{
	// there are 2^9 = 512 PTEs in a page table.
	for (int i = 0; i < 512; i++) {
		pte_t pte = pagetable[i];
		if ((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0) {
			// this PTE points to a lower-level page table.
			uint64 child = PTE2PA(pte);
			freewalk((pagetable_t)child);
			pagetable[i] = 0;
		} else if (pte & PTE_V) {
			panic("freewalk: leaf");
		}
	}
	kfree((void *)pagetable);
}

/**
 * @brief Free user memory pages, then free page-table pages.
 *
 * @param max_page The max vaddr of user-space.
 */
void uvmfree(pagetable_t pagetable, uint64 max_page)
{
	if (max_page > 0)
		uvmunmap(pagetable, 0, max_page, 1);
	freewalk(pagetable);
}

// Copy from kernel to user.
// 从内核拷贝数据到用户空间。
// Copy len bytes from src to virtual address dstva in a given page table.
// 在给定的页表 pagetable 中，把 src 指向的内核内存中的 len 字节，
// 拷贝到用户虚拟地址 dstva 指向的位置
// Return 0 on success, -1 on error.
int copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
	uint64 n, va0, pa0;

	while (len > 0) {
		va0 = PGROUNDDOWN(dstva);
		pa0 = walkaddr(pagetable, va0);
		if (pa0 == 0)
			return -1;
		n = PGSIZE - (dstva - va0);
		if (n > len)
			n = len;
		memmove((void *)(pa0 + (dstva - va0)), src, n);

		len -= n;
		src += n;
		dstva = va0 + PGSIZE;
	}
	return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
	uint64 n, va0, pa0;

	while (len > 0) {
		va0 = PGROUNDDOWN(srcva);
		pa0 = walkaddr(pagetable, va0);
		if (pa0 == 0)
			return -1;
		n = PGSIZE - (srcva - va0);
		if (n > len)
			n = len;
		memmove(dst, (void *)(pa0 + (srcva - va0)), n);

		len -= n;
		dst += n;
		srcva = va0 + PGSIZE;
	}
	return 0;
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
	uint64 n, va0, pa0;
	int got_null = 0, len = 0;

	while (got_null == 0 && max > 0) {
		va0 = PGROUNDDOWN(srcva);
		pa0 = walkaddr(pagetable, va0);
		if (pa0 == 0)
			return -1;
		n = PGSIZE - (srcva - va0);
		if (n > max)
			n = max;

		char *p = (char *)(pa0 + (srcva - va0));
		while (n > 0) {
			if (*p == '\0') {
				*dst = '\0';
				got_null = 1;
				break;
			} else {
				*dst = *p;
			}
			--n;
			--max;
			p++;
			dst++;
			len++;
		}

		srcva = va0 + PGSIZE;
	}
	return len;
}

// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
// 为进程分配页表项（PTE）和物理内存，
// 将进程的用户地址空间从 oldsz 扩展到 newsz。
// oldsz 和 newsz 不要求是页对齐的。
// 成功返回新的大小 newsz，失败返回 0。
uint64 uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz, int xperm)
{
	char *mem;
	uint64 a;

	if(newsz < oldsz)
		return oldsz;

	oldsz = PGROUNDUP(oldsz);
	for(a = oldsz; a < newsz; a += PGSIZE){
		mem = kalloc();
		if(mem == 0){
			uvmdealloc(pagetable, a, oldsz);
			return 0;
		}
		memset(mem, 0, PGSIZE);
		if(mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_R|PTE_U|xperm) != 0){
			kfree(mem);
			uvmdealloc(pagetable, a, oldsz);
			return 0;
		}
	}
	return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
// 释放用户页，将进程的用户内存大小从 oldsz 缩减到 newsz。
// oldsz 和 newsz 不要求是页对齐的，
// 并且 newsz 不一定小于 oldsz。
// oldsz 甚至可以 大于进程当前实际使用的大小。
// 函数返回新的进程大小。
uint64 uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
	if(newsz >= oldsz)
		return oldsz;

	if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){
		int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
		uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
	}

	return newsz;
}

