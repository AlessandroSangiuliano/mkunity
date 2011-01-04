/*
 * Copyright (c) 1991-1998 Open Software Foundation, Inc. 
 *  
 * 
 */
/*
 * MkLinux
 */

/*
 *  linux/arch/i386/mm/fault.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 */

#include <linux/autoconf.h>

#include <mach/mach_interface.h>

#include <osfmach3/mach3_debug.h>

#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/head.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>

#include <asm/system.h>
#include <asm/segment.h>
#include <asm/pgtable.h>

#ifdef	CONFIG_OSFMACH3_DEBUG
#define FAULT_DEBUG	1
#endif	/* CONFIG_OSFMACH3_DEBUG */

#if	FAULT_DEBUG
int fault_debug = 0;
#endif	/* FAULT_DEBUG */

extern void die_if_kernel(const char *,struct pt_regs *,long);

/*
 * This routine handles page faults.  It determines the address,
 * and the problem, and then passes it off to one of the appropriate
 * routines.
 *
 * error_code:
 *	bit 0 == 0 means no page found, 1 means protection fault
 *	bit 1 == 0 means read, 1 means write
 *	bit 2 == 0 means kernel, 1 means user-mode
 */
asmlinkage void do_page_fault(struct pt_regs *regs, unsigned long error_code)
{
#ifndef	CONFIG_OSFMACH3
	void (*handler)(struct task_struct *,
			struct vm_area_struct *,
			unsigned long,
			int);
#endif	/* OSFMACH3 */
	struct task_struct *tsk = current;
	struct mm_struct *mm = tsk->mm;
	struct vm_area_struct * vma;
	unsigned long address;
#ifndef	CONFIG_OSFMACH3
	unsigned long page;
#endif	/* CONFIG_OSFMACH3 */

	/* get the address */
#ifdef	CONFIG_OSFMACH3
	address = tsk->osfmach3.thread->fault_address;
#if	FAULT_DEBUG
	if (fault_debug) {
		printk("do_page_fault: fault at 0x%lx\n", address);
	}
#endif	/* FAULT_DEBUG */
#else	/* CONFIG_OSFMACH3 */
	__asm__("movl %%cr2,%0":"=r" (address));
#endif	/* CONFIG_OSFMACH3 */
	down(&mm->mmap_sem);
	vma = find_vma(mm, address);
	if (!vma)
		goto bad_area;
	if (vma->vm_start <= address)
#ifdef	CONFIG_OSFMACH3
		goto bad_area;	/* we don't do swap in the server */
#else	/* CONFIG_OSFMACH3 */
		goto good_area;
#endif	/* CONFIG_OSFMACH3 */
	if (!(vma->vm_flags & VM_GROWSDOWN))
		goto bad_area;
#ifndef	CONFIG_OSFMACH3
	if (error_code & 4)
#endif	/* CONFIG_OSFMACH3 */
		{
		/*
		 * accessing the stack below %esp is always a bug.
		 * The "+ 32" is there due to some instructions (like
		 * pusha) doing pre-decrement on the stack and that
		 * doesn't show up until later..
		 */
		if (address + 32 < regs->esp)
			goto bad_area;
	}
	if (expand_stack(vma, address))
		goto bad_area;
#if	FAULT_DEBUG
	if (fault_debug) {
		printk("do_page_fault: vma: start=0x%lx,end=0x%lx,off=0x%lx\n",
		       vma->vm_start, vma->vm_end, vma->vm_offset);
	}
#endif	/* FAULT_DEBUG */
	up(&mm->mmap_sem);
	return;

#ifndef	CONFIG_OSFMACH3
/*
 * Ok, we have a good vm_area for this memory access, so
 * we can handle it..
 */
good_area:
	write = 0;
	handler = do_no_page;
	switch (error_code & 3) {
		default:	/* 3: write, present */
			handler = do_wp_page;
#ifdef TEST_VERIFY_AREA
			if (regs->cs == KERNEL_CS)
				printk("WP fault at %08lx\n", regs->eip);
#endif
			/* fall through */
		case 2:		/* write, not present */
			if (!(vma->vm_flags & VM_WRITE))
				goto bad_area;
			write++;
			break;
		case 1:		/* read, present */
			goto bad_area;
		case 0:		/* read, not present */
			if (!(vma->vm_flags & (VM_READ | VM_EXEC)))
				goto bad_area;
	}
	handler(tsk, vma, address, write);
	up(&mm->mmap_sem);
	/*
	 * Did it hit the DOS screen memory VA from vm86 mode?
	 */
	if (regs->eflags & VM_MASK) {
		unsigned long bit = (address - 0xA0000) >> PAGE_SHIFT;
		if (bit < 32)
			tsk->tss.screen_bitmap |= 1 << bit;
	}
	return;
#endif	/* CONFIG_OSFMACH3 */

/*
 * Something tried to access memory that isn't in our memory map..
 * Fix it, but check if it's kernel or user first..
 */
bad_area:
	up(&mm->mmap_sem);
#ifdef	CONFIG_OSFMACH3
#if	FAULT_DEBUG
	if (fault_debug) {
		printk("do_page_fault: bad area, SIGSEGV\n");
	}
#endif	/* FAULT_DEBUG */
	if (tsk != &init_task) {
		tsk->osfmach3.thread->fault_address = address;
		tsk->osfmach3.thread->error_code = error_code;
		tsk->osfmach3.thread->trap_no = 14;
		force_sig(SIGSEGV, tsk);
		return;
	}
#else	/* CONFIG_OSFMACH3 */
	if (error_code & 4) {
		tsk->tss.cr2 = address;
		tsk->tss.error_code = error_code;
		tsk->tss.trap_no = 14;
		force_sig(SIGSEGV, tsk);
		return;
	}
/*
 * Oops. The kernel tried to access some bad page. We'll have to
 * terminate things with extreme prejudice.
 *
 * First we check if it was the bootup rw-test, though..
 */
	if (wp_works_ok < 0 && address == TASK_SIZE && (error_code & 1)) {
		wp_works_ok = 1;
		pg0[0] = pte_val(mk_pte(0, PAGE_SHARED));
		flush_tlb();
		printk("This processor honours the WP bit even when in supervisor mode. Good.\n");
		return;
	}
	if ((unsigned long) (address-TASK_SIZE) < PAGE_SIZE) {
		printk(KERN_ALERT "Unable to handle kernel NULL pointer dereference");
		pg0[0] = pte_val(mk_pte(0, PAGE_SHARED));
	} else
		printk(KERN_ALERT "Unable to handle kernel paging request");
	printk(" at virtual address %08lx\n",address);
	__asm__("movl %%cr3,%0" : "=r" (page));
	printk(KERN_ALERT "current->tss.cr3 = %08lx, %%cr3 = %08lx\n",
		tsk->tss.cr3, page);
	page = ((unsigned long *) page)[address >> 22];
	printk(KERN_ALERT "*pde = %08lx\n", page);
	if (page & 1) {
		page &= PAGE_MASK;
		address &= 0x003ff000;
		page = ((unsigned long *) page)[address >> PAGE_SHIFT];
		printk(KERN_ALERT "*pte = %08lx\n", page);
	}
#endif	/* CONFIG_OSFMACH3 */
	die_if_kernel("Oops", regs, error_code);
	do_exit(SIGKILL);
}