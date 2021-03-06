# Copyright 1991-1998 by Open Software Foundation, Inc. 
#              All Rights Reserved 
#  
# Permission to use, copy, modify, and distribute this software and 
# its documentation for any purpose and without fee is hereby granted, 
# provided that the above copyright notice appears in all copies and 
# that both the copyright notice and this permission notice appear in 
# supporting documentation. 
#  
# OSF DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE 
# INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS 
# FOR A PARTICULAR PURPOSE. 
#  
# IN NO EVENT SHALL OSF BE LIABLE FOR ANY SPECIAL, INDIRECT, OR 
# CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM 
# LOSS OF USE, DATA OR PROFITS, WHETHER IN ACTION OF CONTRACT, 
# NEGLIGENCE, OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION 
# WITH THE USE OR PERFORMANCE OF THIS SOFTWARE. 
# 
# 
# MkLinux
#

#
# arch/osfmach3-i386/boot/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1994 by Linus Torvalds
#

ifdef CONFIG_KERNEL_ELF
HOSTCFLAGS := $(HOSTCFLAGS) -D__BFD__
endif

ifdef SMP
HOSTCFLAGS := $(HOSTCFLAGS) -D__SMP__
endif

zImage: $(CONFIGURE) bootsect setup compressed/vmlinux tools/build
ifdef CONFIG_KERNEL_ELF
	if hash $(ENCAPS) 2> /dev/null; then \
	  $(OBJDUMP) $(OBJDUMP_FLAGS) -o $(ZIMAGE_OFFSET) compressed/vmlinux > compressed/vmlinux.out; \
	else \
	  $(OBJCOPY) compressed/vmlinux compressed/vmlinux.out; \
	fi
	tools/build bootsect setup compressed/vmlinux.out $(ROOT_DEV) > zImage
else
	tools/build bootsect setup compressed/vmlinux $(ROOT_DEV) > zImage
endif
	sync

bzImage: $(CONFIGURE) bbootsect setup compressed/bvmlinux tools/bbuild
ifdef CONFIG_KERNEL_ELF
	if hash $(ENCAPS) 2> /dev/null; then \
	  $(OBJDUMP) $(OBJDUMP_FLAGS) -o $(IMAGE_OFFSET) compressed/bvmlinux > compressed/bvmlinux.out; \
	else \
	  $(OBJCOPY) compressed/bvmlinux compressed/bvmlinux.out; \
	fi
	tools/bbuild bbootsect setup compressed/bvmlinux.out $(ROOT_DEV) > bzImage
else
	tools/bbuild bbootsect setup compressed/bvmlinux $(ROOT_DEV) > bzImage
endif
	sync

compressed/vmlinux: $(TOPDIR)/vmlinux
	@$(MAKE) -C compressed vmlinux

compressed/bvmlinux: $(TOPDIR)/vmlinux
	@$(MAKE) -C compressed bvmlinux

zdisk: $(BOOTIMAGE)
	dd bs=8192 if=$(BOOTIMAGE) of=/dev/fd0

zlilo: $(CONFIGURE) $(BOOTIMAGE)
	if [ -f $(INSTALL_PATH)/vmlinuz ]; then mv $(INSTALL_PATH)/vmlinuz $(INSTALL_PATH)/vmlinuz.old; fi
	if [ -f $(INSTALL_PATH)/System.map ]; then mv $(INSTALL_PATH)/System.map $(INSTALL_PATH)/System.old; fi
	cat $(BOOTIMAGE) > $(INSTALL_PATH)/vmlinuz
	cp $(TOPDIR)/System.map $(INSTALL_PATH)/
	if [ -x /sbin/lilo ]; then /sbin/lilo; else /etc/lilo/install; fi

install: $(CONFIGURE) zImage
	sh ./install.sh $(VERSION).$(PATCHLEVEL).$(SUBLEVEL) zImage $(TOPDIR)/System.map "$(INSTALL_PATH)"

tools/build: tools/build.c
	$(HOSTCC) $(HOSTCFLAGS) -o $@ $< -I$(TOPDIR)/include

tools/bbuild: tools/build.c
	$(HOSTCC) $(HOSTCFLAGS) -D__BIG_KERNEL__ -o $@ $< -I$(TOPDIR)/include

setup: setup.o
	$(LD86) -s -o $@ $<

setup.o: setup.s
	$(AS86) -o $@ $<

setup.s: setup.S video.S $(CONFIGURE) $(TOPDIR)/include/linux/config.h Makefile
	$(CPP) -traditional $(SVGA_MODE) $(RAMDISK) $< -o $@

bootsect: bootsect.o
	$(LD86) -s -o $@ $<

bootsect.o: bootsect.s
	$(AS86) -o $@ $<

bootsect.s: bootsect.S $(CONFIGURE) $(TOPDIR)/include/linux/config.h Makefile
	$(CPP) -traditional $(SVGA_MODE) $(RAMDISK) $< -o $@

bbootsect: bbootsect.o
	$(LD86) -s -o $@ $<

bbootsect.o: bbootsect.s
	$(AS86) -o $@ $<

bbootsect.s: bootsect.S $(CONFIGURE) $(TOPDIR)/include/linux/config.h Makefile
	$(CPP) -D__BIG_KERNEL__ -traditional $(SVGA_MODE) $(RAMDISK) $< -o $@

dep:

clean:
	rm -f bootsect setup
	rm -f bbootsect
	rm -f zImage tools/build compressed/vmlinux.out
	rm -f bzImage tools/bbuild compressed/bvmlinux.out
	@$(MAKE) -C compressed clean
