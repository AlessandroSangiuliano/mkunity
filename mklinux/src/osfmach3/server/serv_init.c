/*
 * Copyright (c) 1991-1998 Open Software Foundation, Inc. 
 *  
 * 
 */
/*
 * MkLinux
 */

#include <linux/autoconf.h>

#include <mach/exc_server.h>
#include <mach/std_types.h>
#include <mach/port.h>
#include <mach/mach_traps.h>
#include <mach/host_info.h>
#include <mach/host_reboot.h>
#include <mach/mach_host.h>
#include <mach/mach_interface.h>
#include <mach/bootstrap.h>

#include <osfmach3/mach_init.h>
#include <osfmach3/mach3_debug.h>
#include <osfmach3/uniproc.h>
#include <osfmach3/console.h>
#include <osfmach3/parent_server.h>
#include <osfmach3/serv_port.h>
#include <osfmach3/server_thread.h>
#include <osfmach3/block_dev.h>

#include <asm/segment.h>
#include <asm/page.h>
#include <asm/param.h>
#include <asm/processor.h>
#include <asm/unistd.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/major.h>
#include <linux/malloc.h>
#include <linux/mm.h>
#include <linux/blk.h>

#include <asm/pgtable.h>

extern void	uniproc_preemption_init(void);
extern char	*getenv(char *var);
extern void	start_kernel(void);
extern void	subsystem_init(void);
extern void	mach_trap_init(void);
extern void	server_thread_init(void);
extern void	osfmach3_notify_init(void);
extern void	ux_server_init(void);
extern void	dev_to_object_init(void);
extern void	device_reply_hdlr(void);
extern void	init_mapped_time(void);
extern void	jiffies_thread_init(void);
extern void	user_memory_init(void);
extern void	inode_pager_init(void);
extern void	fake_interrupt_init(void);
extern void	osfmach3_console_init(void);

extern int	bdflush(void *);
extern int	kswapd(void *);
extern int	root_mountflags;

void		setup_server(int argc, char **argv);
void		ports_init(void);
void		print_versions(void);
void		gen_init(int argc, char **argv);

boolean_t use_activations = FALSE;
extern boolean_t in_kernel;		/* exported by libcthreads */
extern boolean_t map_user_mem_to_copy;

mach_port_t	privileged_host_port;
mach_port_t	host_port;
mach_port_t	security_port;
mach_port_t	root_ledger_wired;
mach_port_t	root_ledger_paged;
mach_port_t	device_server_port;
mach_port_t	default_pager_port;
mach_port_t	default_processor_set;
mach_port_t	default_processor_set_name;
mach_port_t	exc_subsystem_port;

security_token_t	server_security_token;

char		*rootdev_name = "boot_device";

vm_size_t	__mem_size;		/* emulated memory size */
vm_size_t	osfmach3_mem_size;	/* real memory size */

vm_size_t	page_size;
vm_size_t	page_mask;
int		page_shift;

int	parent_server = 0;		/* Running under another server ? */

char	osfmach3_kernel_version[KERNEL_VERSION_MAX];

/*
 * Size of the server threads' stacks size.
 * This variable is imported and used by the cthreads library.
 *
 * IMPORTANT NOTE: one needs to use the -maxonstack MIG option when
 * building libmach. Otherwise, stacks of 64K or over are necessary !
 */
#ifdef	i386
vm_size_t	cthread_stack_size = 16 * 1024;
#endif	/* i386 */
#ifdef	PPC
vm_size_t	cthread_stack_size = 16 * 1024;
#endif	/* PPC */
#ifdef	hp_pa
vm_size_t	cthread_stack_size = 32 * 1024;
#endif	/* hp_pa */

#ifdef	CONFIG_OSFMACH3_DEBUG
vm_size_t	cthread_red_zone_size = PAGE_SIZE;
#endif	/* CONFIG_OSFMACH3_DEBUG */

host_priority_info_data_t host_pri_info;

int
main(
	int	argc,
	char	**argv)
{
	struct server_thread_priv_data	priv_data;

	server_thread_set_priv_data(cthread_self(), &priv_data);
	set_current_task(&init_task);
	uniproc_enter();
	uniproc_preemption_init();

	/*
	 * Initialize the server.
	 */
	setup_server(argc, argv);

	/*
	 * Linux-specific code starts here.
	 */
	start_kernel();

	uniproc_exit();
	cthread_detach(cthread_self());
	cthread_exit((void *) 0);
	return(0); /* Not reached */
}

void
setup_server(
	int	argc,
	char	**argv)
{
	kern_return_t		kr;
	mach_msg_type_number_t	count;

	ports_init();

	count = HOST_PRIORITY_INFO_COUNT;
	kr= host_info(mach_host_self(), HOST_PRIORITY_INFO,
		      (host_info_t) &host_pri_info, &count);
	if (kr != KERN_SUCCESS)
		panic("host_info HOST_PRIORITY_INFO");

	gen_init(argc, argv);  /* generic "machine-dependent" initialization */
	print_versions();
	if (use_activations)
		subsystem_init();

	/*
	 * Make the server unswappable (but it's still pageable).
	 */
	kr = task_swappable(privileged_host_port, mach_task_self(), FALSE);
	if (kr != KERN_SUCCESS) {
		MACH3_DEBUG(1, kr, ("setup_server: task_swappable"));
	}

	/*
	 * Initialize module for handling server threads.  Must be
	 * done very early, because even our wired threads are managed
	 * by this module.
	 */
	server_thread_init();
	ux_server_init();
	mach_trap_init();
	osfmach3_notify_init();

	if (parent_server) {
		/*
		 * Catch signals if we're a regular application
		 * of a parent server.
		 */
		parent_server_catchall_signals();
	}

	fake_interrupt_init();
	init_mapped_time();
	jiffies_thread_init();
	dev_to_object_init();
	device_reply_hdlr();
	user_memory_init();
	inode_pager_init();
}


/*
 * Print Mach kernel version and server version.
 */
void
print_versions(void)
{
	if (host_kernel_version(mach_host_self(),&osfmach3_kernel_version[0])
	    != KERN_SUCCESS) {
		printk("Warning: print_versions: can't get micro-kernel version.\n");
	} else {
		printk("%s", osfmach3_kernel_version);	/* MK version */
	}
}

void
ports_init(void)
{
	mach_port_t	bootstrap_port;
	kern_return_t	result;
	u_int		security_token_size;

	/*
	 * Get our bootstrap port
	 */
	result = task_get_special_port(mach_task_self(),
				       TASK_BOOTSTRAP_PORT,
				       &bootstrap_port);
	if (result != KERN_SUCCESS)
		panic("get bootstrap port %d", result);

	/*
	 * Ask for the privileged ports
	 */
	result = bootstrap_ports(bootstrap_port,
				 &privileged_host_port,
				 &device_server_port,
				 &root_ledger_wired,
				 &root_ledger_paged,
				 &security_port);
	if (result != KERN_SUCCESS)
		panic("bootstrap privileged ports %d", result);

	/*
	 * Get default_pager port
	 */
	result = vm_set_default_memory_manager(privileged_host_port,
					       &default_pager_port);
	if (result != KERN_SUCCESS)
                panic("getting default pager port");

	host_port = mach_host_self();

	/*
	 * Lookup our default processor-set name/control ports.
	 */
	(void) processor_set_default(mach_host_self(),
				     &default_processor_set_name);
	(void) host_processor_set_priv(privileged_host_port,
				       default_processor_set_name,
				       &default_processor_set);


	/*
	 * Get the security token for device operations
	 */
	security_token_size = TASK_SECURITY_TOKEN_COUNT;
	result = task_info(mach_task_self(), TASK_SECURITY_TOKEN,
			   (task_info_t) &server_security_token,
			   &security_token_size);
	if (result != KERN_SUCCESS)
		panic("server security token %d", result);
}

void
subsystem_init(void)
{
	kern_return_t		result;
	rpc_subsystem_t		serv_subsystem;
	mach_msg_type_number_t	serv_subsystem_size;
	extern struct rpc_subsystem serv_callback_subsystem;

	/*
	 * Combine the exception_raise subsystem with the callback
	 * subsystem, so that a single port can be used for RPC to
	 * both of them.
	 */
	serv_subsystem =
		mach_subsystem_join((rpc_subsystem_t) &catch_exc_subsystem,
				    (rpc_subsystem_t) &serv_callback_subsystem,
				    &serv_subsystem_size,
				    (void *(*)(int)) cthread_malloc);

	/*
	 * Register subsystems.
	 */
	result = mach_subsystem_create(mach_task_self(),
				       (user_subsystem_t) serv_subsystem,
				       serv_subsystem_size,
				       &exc_subsystem_port);
	if (result != KERN_SUCCESS) {
		MACH3_DEBUG(0, result, ("ports_init: mach_subsystem_create"));
		printk("subsystem_init: continuing without activations\n");
		use_activations = FALSE;
	}
}

void
insert_args(
	char	*args,
	int	*argcp,
	char	***argvp)
{
	int pass, nargs, argc = 0, i;
	char *p, **argv;
	kern_return_t kr;

	/* Two passes over the argument string.  The first counts the
	   arguments so we know how big an argv[] to allocate; the second
	   fills in the pointers in this argv[].  */
	for (pass = 1; pass <= 2; pass++) {
		nargs = 0;
		p = args;
		while (1) {
			while (*p == ' ') p++;
			if (*p == '\0')
				break;
			if (pass == 2)
				argv[nargs] = p;
			nargs++;
			while (*p != ' ' && *p != '\0') p++;
			if (pass == 2 && *p == ' ')
				*p++ = '\0';
		}
		if (pass == 1) {
			/* Allocate a new argv[] that will contain the
			   inserted arguments followed by the remaining
			   original arguments.  After this argv[], we will
			   put a copy of `args' so that we can replace
			   spaces by '\0's in this copy.  We can't use
			   MALLOC yet so vm_allocate directly.  Since
			   pointers to these args can be stashed away
			   (e.g. in bootparam), we never deallocate this
			   memory.  */
			argc = *argcp + nargs;
			i = (argc + 1) * sizeof *argv + p - args + sizeof '\0';
			kr = vm_allocate(mach_task_self(),
					 (vm_address_t *) &argv, i, TRUE);
			if (kr != KERN_SUCCESS) {
				MACH3_DEBUG(1, kr, ("insert_args: allocate"));
				panic("insert_args");
			}
			for (i = 0; i < *argcp; i++)
				argv[i + nargs] = (*argvp)[i];
			argv[argc] = NULL;
			p = (char *) &argv[argc + 1];
			args = strcpy(p, args);
		}
	}
	*argcp = argc;
	*argvp = argv;
}

extern char **server_command_line_p;
boolean_t single_user = FALSE;

void
old_get_config_info(
	int	argc,
	char	**argv)
{
	char		*args;

	if (in_kernel) {
		use_activations = TRUE;
	}

	/*
	 * Parse the arguments.
	 */

	/*
	 * Arg 0 is program name
	 */
	argv++, argc--;

	/*
	 * There may be additional args in our environment
	 */
	args = getenv("startup_flags");

	/*
	 * Process flag arguments:
	 */
	while (1) {
		register char *cp;
		register char c;

		if (argc == 0 || argv[0][0] != '-') {
			if (args != NULL) {
				insert_args(args, &argc, &argv);
				args = NULL;
				continue;
			}
			break;
		}
		cp = argv[0];
		while ((c = *++cp) != '\0') {
			switch (c) {
			    case 'h':
				osfmach3_console_init();	/* XXX */
				Debugger("inline");
				break;
			    case 'l':
				parent_server_set_type(PARENT_SERVER_LINUX);
				goto flag_2;
			    case 'o':
				parent_server_set_type(PARENT_SERVER_OSF1);
				goto flag_2;
			    case '2':	 
flag_2:
				parent_server_get_mach_privilege();
				if (parent_server != 1) {
					parent_server = 1;
					printk("The server is not running standalone. PID = %d.\n",
					       parent_server_getpid());
				}
				break;
			    case 'A':
				/* Set option for using short-circuited
				 * RPC in syscalls.  Check for {0,1}
				 * argument (use 1, if not specified):
				 */
				if (argv[1] && argv[1][0] != '-') {
					use_activations =
						(argv[1][0] != '0');
					argv++; argc--;
				} else
					use_activations = TRUE;

				if (use_activations) {
					if (! in_kernel) {
						printk("\nWARNING: user mode: "
						    "-A ignored\n");
						use_activations = FALSE;
					}
				}
				break;

			    case 'k':
			    case 'S':
				if (!in_kernel)
					printk("WARNING: Server not "
					       "kernel-loaded but -%c found\n",
					       c);
				if (argv[1] && argv[1][0] != '-')
					argv++, argc--;
					/* Skip over the submap base/size arg */
				break;

			    case 's':
				single_user = TRUE;
				break;

			    case 'c':	/* Linux-like command line */
				if (argv[1]) {
					server_command_line_p = &argv[1];
					argv++; argc--;
				}
				break;
			    default:
				printk("Warning: invalid flag `-%c'\n", c);
				break;
			}
		}
		argv++, argc--;
	}

	/*
	 * First non-flag arg is root device name.  If this is a first
	 * server the microkernel will pass us this even if we say -a,
	 * so we skip it even in that case.
	 */
	if (argc > 0) {
		rootdev_name = argv[0];
		argv++, argc--;
	}

	if (in_kernel) {
		printk("*** SERVER IS KERNEL-LOADED ***\n");
	}
	if (use_activations) {
		printk("*** USING ACTIVATIONS ***\n");
	}
}

kernel_boot_info_t kernel_boot_info;

void
get_config_info(
	int	argc,
	char	**argv)
{
	boolean_t	found_name_arg;
	char		*bp;
	int		len;
	kern_return_t	kr;
	char 		*(local_argv[50]);

	if (argc > 1) {
		old_get_config_info(argc, argv);
		return;
	}

	kr = host_get_boot_info(privileged_host_port,
				kernel_boot_info);
	if (kr != KERN_SUCCESS || *kernel_boot_info == 0) {
		old_get_config_info(argc, argv);
		return;
	}
	printk("Boot info: %s\n", kernel_boot_info);
	/*
	 * Process the arguments: the format of the boot info is
	 * 	rootdev [-howto] [kernel_name[:server_name]] [server args]
	 *
	 * We translate this into the syntax expected by old_get_config_info,
	 * i.e. the syntax that is used to invoke a child server as a
	 * process under the main server:
	 *
	 * 	server_name [-howto] [server args] rootdev
	 */

	/* We will construct argc and argv as for a child server: */
	argc = 1;	/* Leave room for argv[0], to be filled in later */
	argv = local_argv;
	bp = kernel_boot_info;

	/* Remove any trailing new-line from kernel_boot_info: */
	len = strlen(bp);
	if (bp[len - 1] == '\n')
		bp[len - 1] = '\0';

	rootdev_name = bp;	/* This will be last entry in argv */
	found_name_arg = FALSE;

	/* Process the blank-separated words of the string into null-terminated
	 * argv entries.  At top of each iteration, we are positioned at
	 * start of the previously processed word (or at the end of string).
	 */
	do {
		/* Find the end of the previous word and null terminate it: */
		while (*bp && *bp != ' ' && *bp != '\n')
			bp++;

		if (*bp == '\n') {
			*bp++ = 0;
			break;
		}
		else if (*bp)
			*bp++ = 0;
		else
			break;

		/* Find the start of the next word, or end-of-line: */
		while (*bp == ' ')
			bp++;

		if (*bp == '\n') {
			*bp++ = 0;
			break;
		}
		else if (!*bp)
			break;

		/* Is it the kernel:server argument? */
		if (*bp != '-' && !found_name_arg) {
			found_name_arg = TRUE;
			/* Skip over kernel name to server name: */
			while (*bp && *bp != ' ' && *bp != ':')
				bp++;
			if (*bp == ':')
				argv[0] = ++bp;
			else
				argv[0] = "[No Server Name Found]";
		} else {
			argv[argc++] = bp;
		}
	} while (1);

	argv[argc++] = rootdev_name;
	argv[argc] = 0;

	old_get_config_info(argc, argv);
}

void
set_rootdev(void)
{
}

void
parse_root_device(const char *rootdev_name)
{
	static char		name[GETS_MAX];
	int kr;

	if (ROOT_DEV != 0) {
		/*
		 * root device has already been specified via
		 * a "root=..." argument in the command line.
		 */
		return;
	}

	while (1) {
		if (rootdev_name == NULL) {
			printk("root device? ");
			rootdev_name = gets(name);
			if (rootdev_name == NULL)
				panic("set_rootdev: gets");
		}
		kr = gen_disk_name_to_dev(rootdev_name, &ROOT_DEV);
		if (kr == 0 && ROOT_DEV != NODEV)
			break;
		printk("Bad Mach root device name: %s\n", rootdev_name);
		rootdev_name = NULL;
	}
	printk("Mach root device %s: major=%d minor=%d\n", rootdev_name,
	       MAJOR(ROOT_DEV), MINOR(ROOT_DEV));
}

void
size_memory(void)
{
	kern_return_t rc;
	int host_buff[HOST_BASIC_INFO_COUNT];
	mach_msg_type_number_t host_buff_size = HOST_BASIC_INFO_COUNT;
	vm_statistics_data_t vm_stat;

	rc = host_info(mach_host_self(), HOST_BASIC_INFO,
		       host_buff, &host_buff_size);
	if (rc != KERN_SUCCESS) {
		printk("host_info() returned 0x%x.\n", rc);
		panic("size_memory: host_info failed");
	}

	osfmach3_mem_size = ((struct host_basic_info *) host_buff)->memory_size;

        host_buff_size = HOST_VM_INFO_COUNT;
	rc = host_statistics(privileged_host_port, HOST_VM_INFO,
			     (host_info_t) &vm_stat, &host_buff_size);
	if (rc != KERN_SUCCESS) {
		printk("vm_statistics() returned 0x%x.\n", rc);
		panic("size_memory: vm_statistics failed");
	}

        (void) host_page_size(mach_host_self(), &page_size);

	page_mask = page_size - 1;

	if ((page_mask & page_size) != 0)
		panic("size_memory: page size not a power of two");

	for (page_shift = 0; ; page_shift++)
		if ((1 << page_shift) == page_size)
			break;
}

void
gen_init(
	int	argc,
	char	**argv)
{
	extern boolean_t	in_kernel;
	boolean_t		page_0_was_allocated;
	kern_return_t		rc;

	size_memory();

	serv_port_register(mach_task_self(), &init_task);
	init_task.osfmach3.task->mach_task_port = mach_task_self();

	/*
	 * Protect our page 0, so that all null pointers references
	 * will fail. We may have to undo the existing protection...
	 */
	if (!in_kernel &&
	    ((rc = vm_protect(mach_task_self(), 0, PAGE_SIZE, TRUE,
			VM_PROT_READ)) != KERN_SUCCESS ||
	     (rc = vm_protect(mach_task_self(), 0, PAGE_SIZE, FALSE,
			VM_PROT_NONE)) != KERN_SUCCESS)) {
		vm_offset_t page_0 = (vm_offset_t)0;	/* page 0 in the map */
		/*
		 * vm_deallocate returns success even for non-mapped storage.
		 * Trust the vm_protect rc instead.
		 */
		(void)vm_deallocate(mach_task_self(), 0, PAGE_SIZE);
                page_0_was_allocated = (rc != KERN_INVALID_ADDRESS); /* (known non-SUCCESS) */
		rc = vm_map(mach_task_self(), &page_0, PAGE_SIZE,
			0, FALSE, MEMORY_OBJECT_NULL, 0, FALSE,
			VM_PROT_NONE, VM_PROT_READ, VM_INHERIT_NONE);
	} else {
		rc = KERN_SUCCESS;
		page_0_was_allocated = FALSE;
        }

	/*
	 * Get arguments and config info
	 */
	get_config_info(argc, argv);
	if (rc != KERN_SUCCESS)
		printk("\aWARNING: failed to reserve page 0. vm_map() returned 0x%x\n", rc);

	if (page_0_was_allocated) {
		printk("\aWARNING: existing page 0 deallocated.\n");
	}

	osfmach3_console_init();
}

void
osfmach3_start_init(
	char	**argv_init,
	char	**envp_init)
{
	extern int	sys_setup(void);
	int		ret;
	kern_return_t	kr;
	mach_port_t	child_thread;
	struct vm_area_struct *mpnt;
	unsigned long	p1, p2, argv, envp;
	int 		argc, envc, i;
	char		*cp;
	struct pt_regs	*user_regs;
	unsigned long	old_fs;
#ifdef	CONFIG_BLK_DEV_INITRD
	int		real_root_mountflags;
#endif	/* CONFIG_BLK_DEV_INITRD */

	/*
	 * Create a new empty task.
	 */
	ret = do_fork(CLONING_INIT, 0, NULL);
	if (ret != 1) {
		if (ret > 0)
			panic("fork init: got pid %d instead of 1.\n", ret);
		panic("can't fork init: error %d", -ret);
	}

	/*
	 * Act as "init" now.
	 */
	uniproc_switch_to(current, task[smp_num_cpus]);

	/* Launch bdflush from here, instead of the old syscall way. */
	kernel_thread(bdflush, NULL, 0);
	/* Start the background pageout daemon. */
	kernel_thread(kswapd, NULL, 0);

#ifdef CONFIG_BLK_DEV_INITRD
	real_root_dev = ROOT_DEV;
	real_root_mountflags = root_mountflags;
	if (initrd_start && mount_initrd) root_mountflags &= ~MS_RDONLY;
	else mount_initrd =0;
#endif
#ifdef	CONFIG_OSFMACH3
	/*
	 * This will mount the root file system, amongst other things.
	 */
	old_fs = get_fs();
	set_fs(get_ds());
	sys_setup();
	set_fs(old_fs);
#else	/* CONFIG_OSFMACH3 */
	setup();
#endif	/* CONFIG_OSFMACH3 */

#ifdef __SMP__
	/*
	 *	With the devices probed and setup we can
	 *	now enter SMP mode.
	 */
	
	smp_begin();
#endif	

	#ifdef CONFIG_UMSDOS_FS
	{
		/*
			When mounting a umsdos fs as root, we detect
			the pseudo_root (/linux) and initialise it here.
			pseudo_root is defined in fs/umsdos/inode.c
		*/
		extern struct inode *pseudo_root;
		if (pseudo_root != NULL){
			current->fs->root = pseudo_root;
			current->fs->pwd  = pseudo_root;
		}
	}
	#endif

	/*
	 * Load /mach_servers/mach_init into the new task.
	 */
	child_thread = current->osfmach3.thread->mach_thread_port;

	/*
	 * Put the arguments in the empty user task.
	 */
	mpnt = (struct vm_area_struct *)kmalloc(sizeof (*mpnt), GFP_KERNEL);
	if (mpnt == NULL) {
		panic("can't allocate vm_area for init args");
	}
	mpnt->vm_mm = current->mm;
	mpnt->vm_start = PAGE_SIZE;
	mpnt->vm_end = PAGE_SIZE + ARG_MAX;
	mpnt->vm_page_prot = PAGE_COPY;
	mpnt->vm_flags = VM_READ|VM_WRITE;
	mpnt->vm_inode = NULL;
	mpnt->vm_offset = 0;
	mpnt->vm_ops = NULL;
	insert_vm_struct(current->mm, mpnt);

	p1 = mpnt->vm_start;
	for (argc = 0; argv_init[argc] != NULL; argc++);
	for (envc = 0; envp_init[envc] != NULL; envc++);
	p2 = p1 + ((argc+1 + envc+1) * sizeof (char *));

	argv = p1;
	for (i = 0; i < argc; i++) {
		put_fs_long(p2, (long *) p1);
		for (cp = argv_init[i]; *cp; cp++) {
			put_fs_byte(*cp, (char *) p2);
			p2 += sizeof (char);
		}
		put_fs_byte('\0', (char *) p2);
		p2 += sizeof (char);
		p1 += sizeof (char *);
	}
	put_fs_long(0, (long *) p1);
	p1 += sizeof (char *);

	envp = p1;
	for (i = 0; i < envc; i++) {
		put_fs_long(p2, (long *) p1);
		for (cp = envp_init[i]; *cp; cp++) {
			put_fs_byte(*cp, (char *) p2);
			p2 += sizeof (char);
		}
		put_fs_byte('\0', (char *) p2);
		p2 += sizeof (char);
		p1 += sizeof (char *);
	}
	put_fs_long(0, (long *) p1);
	p1 += sizeof (char *);

	/*
	 * The fake exec.
	 */
	ret = do_execve("/mach_servers/mach_init",
			(char **) argv, (char **) envp,
			current->osfmach3.thread->regs_ptr);
	if (ret) {
		panic("can't exec /mach_servers/mach_init: error %d", -ret);
	}
	user_regs = current->osfmach3.thread->regs_ptr;

	uniproc_switch_to(current, task[0]);

	/*
	 * Launch /mach_servers/mach_init.
	 */
	kr = osfmach3_thread_set_state(child_thread,
				       user_regs);
	if (kr != KERN_SUCCESS) {
		MACH3_DEBUG(0, kr, ("osfmach3_start_init: "
				    "osfmach3_thread_set_state(init=0x%x)",
				    child_thread));
		panic("can't set init threads's state");
	}
	kr = thread_resume(child_thread);
	if (kr != KERN_SUCCESS) {
		MACH3_DEBUG(0, kr, ("osfmach3_start_init: "
				    "thread_resume(init=0x%x)",
				    child_thread));
		panic("can't resume init's thread");
	}
}

boolean_t server_halted = FALSE;

void
osfmach3_halt_system(
	boolean_t reset)
{
	kern_return_t kr;

	server_halted = TRUE;	/* for the jiffies thread */
	if (parent_server) {
		parent_server_release_console();
		parent_server_exit(0);
	} else {
		kr = host_reboot(privileged_host_port,
				 reset ? 0 : HOST_REBOOT_HALT);
		if (kr != KERN_SUCCESS) {
			MACH3_DEBUG(0, kr,
				    ("osfmach3_halt_system(%d): host_reboot",
				     reset));
		}
	}
	/* in case it didn't work ... */
	printk("osfmach3_halt_system: entering endless loop...\n");
	for (;;);
}
