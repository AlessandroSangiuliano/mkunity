/*
 * linux/fs/bext2/acl.c
 *
 * Copyright (C) 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 */

/*
 * This file will contain the Access Control Lists management for the
 * second extended file system.
 */

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/bext2_fs.h>
#include <linux/sched.h>
#include <linux/stat.h>

/*
 * bext2_permission ()
 *
 * Check for access rights
 */
int bext2_permission (struct inode * inode, int mask)
{
	unsigned short mode = inode->i_mode;

	/*
	 * Nobody gets write access to a file on a readonly-fs
	 */
	if ((mask & S_IWOTH) && 
            (S_ISREG(mode) || S_ISDIR(mode) || S_ISLNK(mode)) &&
            IS_RDONLY(inode))
		return -EROFS;
	/*
	 * Nobody gets write access to an immutable file
	 */
	if ((mask & S_IWOTH) && IS_IMMUTABLE(inode))
		return -EACCES;
	/*
	 * Special case, access is always granted for root
	 */
	if (fsuser())
		return 0;
	/*
	 * If no ACL, checks using the file mode
	 */
	else if (current->fsuid == inode->i_uid)
		mode >>= 6;
	else if (in_group_p (inode->i_gid))
		mode >>= 3;
	if (((mode & mask & S_IRWXO) == mask))
		return 0;
	else
		return -EACCES;
}
