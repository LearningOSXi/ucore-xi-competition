#include "kalloc.h"
#include "defs.h"
#include "riscv.h"

extern char ekernel[];

struct linklist {
	struct linklist *next;
};

struct {
	struct linklist *freelist;
} kmem;

void freerange(void *pa_start, void *pa_end)
{
	char *p;
	p = (char *)PGROUNDUP((uint64)pa_start);
	for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE)
		kfree(p);
}

void kinit()
{
	freerange(ekernel, (void *)PHYSTOP);
}

// Free the page of physical memory pointed at by v, 释放由参数 pa 指向的一整页物理内存。
// which normally should have been returned by a     通常来说，这个指针应该是之前由 kalloc() 分配得到的。
// call to kalloc().  (The exception is when         唯一的例外是：在初始化内存分配器时（kinit）
// initializing the allocator; see kinit above.)     那时我们会“假装这些页是被分配过的”，然后再用 kfree 回收它们。
void kfree(void *pa)
{
	struct linklist *l;
	if (((uint64)pa % PGSIZE) != 0 || (char *)pa < ekernel ||
	    (uint64)pa >= PHYSTOP)
		panic("kfree");
	// Fill with junk to catch dangling refs.
	memset(pa, 1, PGSIZE); // 填入垃圾数据（全1）
	// 这样做是为了防止释放后还是用这个数据，只要释放后使用很快会触发异常。
	l = (struct linklist *)pa;
	l->next = kmem.freelist;
	kmem.freelist = l;
}

// Allocate one 4096-byte page of physical memory. 分配一页大小为 4096 字节 的物理内存。
// Returns a pointer that the kernel can use.      返回一个内核可以直接使用的指针（指向该物理页起始地址）。
// Returns 0 if the memory cannot be allocated.    如果当前没有空闲物理页可用，则返回 0（NULL）。
void *kalloc(void)
{
	// 物理页分配
	struct linklist *l;
	l = kmem.freelist;
	if (l) {
		kmem.freelist = l->next;
		memset((char *)l, 5, PGSIZE); // fill with junk，防止使用历史残留数据
	}
	return (void *)l;
}