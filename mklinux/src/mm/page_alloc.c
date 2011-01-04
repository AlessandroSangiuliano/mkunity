/*
 *  linux/mm/page_alloc.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *  Swap reorganised 29.12.95, Stephen Tweedie
 */

#include <linux/autoconf.h>

#ifdef	CONFIG_OSFMACH3
#include <mach/mach_interface.h>
#include <mach/port.h>

#include <osfmach3/mach_init.h>
#include <osfmach3/mach3_debug.h>
#include <osfmach3/free_area_pager.h>
#endif	/* CONFIG_OSFMACH3 */

#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/head.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/swap.h>
#include <linux/fs.h>
#include <linux/swapctl.h>
#include <linux/interrupt.h>

#include <asm/dma.h>
#include <asm/system.h> /* for cli()/sti() */
#include <asm/segment.h> /* for memcpy_to/fromfs */
#include <asm/bitops.h>
#include <asm/pgtable.h>

#ifdef	CONFIG_OSFMACH3
unsigned long		__mem_base = 0;
extern int		initial_min_free_pages;
/*
 * Don't insist on shrinking the "free_area" below "max_shrink_ratio" % of
 * the physical memory.
 */
int			max_shrink_ratio = 30;
#else	/* CONFIG_OSFMACH3 */
#define free_area_mark_used(address, size)
#endif	/* CONFIG_OSFMACH3 */

int nr_swap_pages = 0;
int nr_free_pages = 0;

/*
 * Free area management
 *
 * The free_area_list arrays point to the queue heads of the free areas
 * of different sizes
 */

#define NR_MEM_LISTS 6

/* The start of this MUST match the start of "struct page" */
struct free_area_struct {
	struct page *next;
	struct page *prev;
	unsigned int * map;
};

#define memory_head(x) ((struct page *)(x))

static struct free_area_struct free_area[NR_MEM_LISTS];

static inline void init_mem_queue(struct free_area_struct * head)
{
	head->next = memory_head(head);
	head->prev = memory_head(head);
}

static inline void add_mem_queue(struct free_area_struct * head, struct page * entry)
{
	struct page * next = head->next;

	entry->prev = memory_head(head);
	entry->next = next;
	next->prev = entry;
	head->next = entry;
}

static inline void remove_mem_queue(struct page * entry)
{
	struct page * next = entry->next;
	struct page * prev = entry->prev;
	next->prev = prev;
	prev->next = next;
}

/*
 * Free_page() adds the page to the free lists. This is optimized for
 * fast normal cases (no error jumps taken normally).
 *
 * The way to optimize jumps for gcc-2.2.2 is to:
 *  - select the "normal" case and put it inside the if () { XXX }
 *  - no else-statements if you can avoid them
 *
 * With the above two rules, you get a straight-line execution path
 * for the normal case, giving better asm-code.
 *
 * free_page() may sleep since the page being freed may be a buffer
 * page or present in the swap cache. It will not sleep, however,
 * for a freshly allocated page (get_free_page()).
 */

#ifdef	CONFIG_OSFMACH3
/* we need ADDRESS earlier than linux... */
#define ADDRESS(x) (PAGE_OFFSET + ((x) << PAGE_SHIFT))
#endif	/* CONFIG_OSFMACH3 */

/*
 * Buddy system. Hairy. You really aren't expected to understand this
 *
 * Hint: -mask = 1+~mask
 */
static inline void free_pages_ok(unsigned long map_nr, unsigned long order)
{
	struct free_area_struct *area = free_area + order;
	unsigned long index = map_nr >> (1 + order);
	unsigned long mask = (~0UL) << order;
	unsigned long flags;

	save_flags(flags);
	cli();

#define list(x) (mem_map+(x))

	map_nr &= mask;
	nr_free_pages -= mask;
#ifdef	CONFIG_OSFMACH3
	free_area_discard_pages(ADDRESS(map_nr), PAGE_SIZE << order);
#endif	/* CONFIG_OSFMACH3 */
	while (mask + (1 << (NR_MEM_LISTS-1))) {
		if (!change_bit(index, area->map))
			break;
		remove_mem_queue(list(map_nr ^ -mask));
		mask <<= 1;
		area++;
		index >>= 1;
		map_nr &= mask;
	}
	add_mem_queue(area, list(map_nr));

#undef list

	restore_flags(flags);
}

void __free_page(struct page *page)
{
	if (!PageReserved(page) && atomic_dec_and_test(&page->count)) {
		unsigned long map_nr = page->map_nr;
#ifndef	CONFIG_OSFMACH3
		delete_from_swap_cache(map_nr);
#endif	/* CONFIG_OSFMACH3 */
		free_pages_ok(map_nr, 0);
	}
}

void free_pages(unsigned long addr, unsigned long order)
{
	unsigned long map_nr = MAP_NR(addr);

	if (map_nr < MAP_NR(high_memory)) {
		mem_map_t * map = mem_map + map_nr;
		if (PageReserved(map))
			return;
		if (atomic_dec_and_test(&map->count)) {
#ifndef	CONFIG_OSFMACH3
			delete_from_swap_cache(map_nr);
#endif	/* CONFIG_OSFMACH3 */
			free_pages_ok(map_nr, order);
			return;
		}
	}
}

/*
 * Some ugly macros to speed up __get_free_pages()..
 */
#define MARK_USED(index, order, area) \
	change_bit((index) >> (1+(order)), (area)->map)
#define CAN_DMA(x) (PageDMA(x))
#ifndef	CONFIG_OSFMACH3
#define ADDRESS(x) (PAGE_OFFSET + ((x) << PAGE_SHIFT))
#endif	/* CONFIG_OSFMACH3 */
#define RMQUEUE(order, dma) \
do { struct free_area_struct * area = free_area+order; \
     unsigned long new_order = order; \
	do { struct page *prev = memory_head(area), *ret; \
		while (memory_head(area) != (ret = prev->next)) { \
			if (!dma || CAN_DMA(ret)) { \
				unsigned long map_nr = ret->map_nr; \
				(prev->next = ret->next)->prev = prev; \
				MARK_USED(map_nr, new_order, area); \
				nr_free_pages -= 1 << order; \
				EXPAND(ret, map_nr, order, new_order, area); \
				free_area_mark_used(ADDRESS(map_nr), \
						    PAGE_SIZE << order); \
				restore_flags(flags); \
				return ADDRESS(map_nr); \
			} \
			prev = ret; \
		} \
		new_order++; area++; \
	} while (new_order < NR_MEM_LISTS); \
} while (0)

#define EXPAND(map,index,low,high,area) \
do { unsigned long size = 1 << high; \
	while (high > low) { \
		area--; high--; size >>= 1; \
		add_mem_queue(area, map); \
		MARK_USED(index, high, area); \
		index += size; \
		map += size; \
	} \
	map->count = 1; \
	map->age = PAGE_INITIAL_AGE; \
} while (0)

unsigned long __get_free_pages(int priority, unsigned long order, int dma)
{
	unsigned long flags;
	int reserved_pages;

	if (order >= NR_MEM_LISTS)
		return 0;
	if (intr_count && priority != GFP_ATOMIC) {
		static int count = 0;
		if (++count < 5) {
			printk("gfp called nonatomically from interrupt %p\n",
				__builtin_return_address(0));
			priority = GFP_ATOMIC;
		}
	}
	reserved_pages = 5;
	if (priority != GFP_NFS)
		reserved_pages = min_free_pages;
	if ((priority == GFP_BUFFER || priority == GFP_IO) && reserved_pages >= 48)
		reserved_pages -= (12 + (reserved_pages>>3));
	save_flags(flags);
repeat:
#ifdef	CONFIG_OSFMACH3
	if (min_free_pages < reserved_pages) {
		/*
		 * More pages are available now. Try again.
		 */
		reserved_pages = min_free_pages;
		goto repeat;
	}
#endif	/* CONFIG_OSFMACH3 */
	cli();
	if ((priority==GFP_ATOMIC) || nr_free_pages > reserved_pages) {
		RMQUEUE(order, dma);
		restore_flags(flags);
#ifdef	CONFIG_OSFMACH3
		/* don't give up so easily... */
		goto try_harder;
#endif	/* CONFIG_OSFMACH3 */
		return 0;
	}
	restore_flags(flags);
	if (priority != GFP_BUFFER && try_to_free_page(priority, dma, 1))
		goto repeat;
#ifdef	CONFIG_OSFMACH3
    try_harder:
	/*
	 * Don't fail too often: better cause more paging activity
	 * than various system call failures...
	 */
	if (min_free_pages > initial_min_free_pages) {
		/*
		 * We have reserved more pages than necessary.
		 */
		if (priority != GFP_BUFFER ||
		    min_free_pages > ((__mem_size >> PAGE_SHIFT)
				      * 100) / (100 - max_shrink_ratio)) {
			/*
			 * We're not trying to grow the buffer cache.
			 * Or we have already shrinked the "free_area"
			 * of more than "max_shrink_ratio"% of the physical
			 * memory (in which case we allow the buffer cache
			 * to grow anyway).
			 */
			min_free_pages--;
			if (reserved_pages > 0) 
				reserved_pages--;
			goto repeat;
		}
	}
#endif	/* CONFIG_OSFMACH3 */
	return 0;
}

/*
 * Show free area list (used inside shift_scroll-lock stuff)
 * We also calculate the percentage fragmentation. We do this by counting the
 * memory on each free list with the exception of the first item on the list.
 */
void show_free_areas(void)
{
 	unsigned long order, flags;
 	unsigned long total = 0;

	printk("Free pages:      %6dkB\n ( ",nr_free_pages<<(PAGE_SHIFT-10));
	save_flags(flags);
	cli();
 	for (order=0 ; order < NR_MEM_LISTS; order++) {
		struct page * tmp;
		unsigned long nr = 0;
		for (tmp = free_area[order].next ; tmp != memory_head(free_area+order) ; tmp = tmp->next) {
			nr ++;
		}
		total += nr * ((PAGE_SIZE>>10) << order);
		printk("%lu*%lukB ", nr, (PAGE_SIZE>>10) << order);
	}
	restore_flags(flags);
	printk("= %lukB)\n", total);
#ifdef SWAP_CACHE_INFO
	show_swap_cache_info();
#endif	
}

#define LONG_ALIGN(x) (((x)+(sizeof(long))-1)&~((sizeof(long))-1))

/*
 * set up the free-area data structures:
 *   - mark all pages reserved
 *   - mark all memory queues empty
 *   - clear the memory bitmaps
 */
unsigned long free_area_init(unsigned long start_mem, unsigned long end_mem)
{
	mem_map_t * p;
	unsigned long mask = PAGE_MASK;
	int i;
#ifdef	CONFIG_OSFMACH3
	unsigned long mem_size;
#endif	/* CONFIG_OSFMACH3 */

#ifdef	CONFIG_OSFMACH3
	high_memory = PAGE_OFFSET + __mem_size;
	end_mem = high_memory;
	mem_size = __mem_size;
#endif	/* CONFIG_OSFMACH3 */

	/*
	 * select nr of pages we try to keep free for important stuff
	 * with a minimum of 48 pages. This is totally arbitrary
	 */
#ifdef	CONFIG_OSFMACH3
	i = (mem_size - PAGE_OFFSET) >> (PAGE_SHIFT+7);
#else	/* CONFIG_OSFMACH3 */
	i = (end_mem - PAGE_OFFSET) >> (PAGE_SHIFT+7);
#endif	/* CONFIG_OSFMACH3 */
	if (i < 24)
		i = 24;
	i += 24;   /* The limit for buffer pages in __get_free_pages is
	   	    * decreased by 12+(i>>3) */
	min_free_pages = i;
	free_pages_low = i + (i>>1);
	free_pages_high = i + i;
#ifndef	CONFIG_OSFMACH3
	start_mem = init_swap_cache(start_mem, end_mem);
#endif	/* CONFIG_OSFMACH3 */
	mem_map = (mem_map_t *) start_mem;
	p = mem_map + MAP_NR(end_mem);
	start_mem = LONG_ALIGN((unsigned long) p);
	memset(mem_map, 0, start_mem - (unsigned long) mem_map);
	do {
		--p;
		p->flags = (1 << PG_DMA) | (1 << PG_reserved);
		p->map_nr = p - mem_map;
	} while (p > mem_map);

	for (i = 0 ; i < NR_MEM_LISTS ; i++) {
		unsigned long bitmap_size;
		init_mem_queue(free_area+i);
		mask += mask;
#ifdef	CONFIG_OSFMACH3
		mem_size = (mem_size + ~mask) & mask;
		bitmap_size = (mem_size - PAGE_OFFSET) >> (PAGE_SHIFT + i);
#else	/* CONFIG_OSFMACH3 */
		end_mem = (end_mem + ~mask) & mask;
		bitmap_size = (end_mem - PAGE_OFFSET) >> (PAGE_SHIFT + i);
#endif	/* CONFIG_OSFMACH3 */
		bitmap_size = (bitmap_size + 7) >> 3;
		bitmap_size = LONG_ALIGN(bitmap_size);
		free_area[i].map = (unsigned int *) start_mem;
		memset((void *) start_mem, 0, bitmap_size);
		start_mem += bitmap_size;
	}

#ifdef	CONFIG_OSFMACH3
	/*
	 * Allocate the free_area as a memory object the size
	 * of the physical memory.
	 */

	PAGE_OFFSET = free_area_pager_init(__mem_size, mask);
	printk("Emulating %d MB of physical memory from 0x%lx to 0x%lx\n",
	       (__mem_size / 1024) / 1024,
	       PAGE_OFFSET,
	       PAGE_OFFSET + __mem_size);

	/* recompute high_memory and end_mem now that we know the PAGE_OFFSET */
	high_memory = PAGE_OFFSET + __mem_size;
	end_mem = high_memory;
#endif	/* CONFIG_OSFMACH3 */

	return start_mem;
}

#ifndef	CONFIG_OSFMACH3
/*
 * The tests may look silly, but it essentially makes sure that
 * no other process did a swap-in on us just as we were waiting.
 *
 * Also, don't bother to add to the swap cache if this page-in
 * was due to a write access.
 */
void swap_in(struct task_struct * tsk, struct vm_area_struct * vma,
	pte_t * page_table, unsigned long entry, int write_access)
{
	unsigned long page = __get_free_page(GFP_KERNEL);

	if (pte_val(*page_table) != entry) {
		if (page)
			free_page(page);
		return;
	}
	if (!page) {
		printk("swap_in:");
		set_pte(page_table, BAD_PAGE);
		swap_free(entry);
		oom(tsk);
		return;
	}
	read_swap_page(entry, (char *) page);
	if (pte_val(*page_table) != entry) {
		free_page(page);
		return;
	}
	vma->vm_mm->rss++;
	tsk->maj_flt++;

	/* Give the physical reallocated page a bigger start */
	if (vma->vm_mm->rss < (MAP_NR(high_memory) >> 2))
		mem_map[MAP_NR(page)].age = (PAGE_INITIAL_AGE + PAGE_ADVANCE);

	if (!write_access && add_to_swap_cache(MAP_NR(page), entry)) {
		/* keep swap page allocated for the moment (swap cache) */
		set_pte(page_table, mk_pte(page, vma->vm_page_prot));
		return;
	}
	set_pte(page_table, pte_mkwrite(pte_mkdirty(mk_pte(page, vma->vm_page_prot))));
  	swap_free(entry);
  	return;
}
#endif	/* CONFIG_OSFMACH3 */