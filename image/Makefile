SYSIMG=OluxOS.img
IMGSIZE=1474560
IMGTEMP=ramdisk
KERNELNAME=OluxOS.krn
TEMPLATES=templates
GRUBSCRIPT=grub_script

CAT=/bin/cat
YES=/bin/yes
DD=/bin/dd
CP=/bin/cp
RM=/bin/rm
MKDIR=/bin/mkdir
SYNC=/bin/sync

SUDO=/usr/bin/sudo
MKE2FS=/sbin/mke2fs
MODPROBE=/sbin/modprobe
MOUNT=/bin/mount
UMOUNT=/bin/umount
CHOWN=/bin/chown

GRUB=/sbin/grub

MAKEFLAGS=--quiet


all:
	$(DD) if=/dev/zero of=$(SYSIMG) bs=$(IMGSIZE) count=1
	$(DD) if=../$(KERNELNAME) of=$(SYSIMG) bs=$(IMGSIZE) count=1 conv=notrunc
	#$(YES) | $(MKE2FS) $(SYSIMG)
	#$(SUDO) $(MODPROBE) loop
	#$(SUDO) $(MOUNT) -o loop ./$(SYSIMG) $(IMGTEMP)

	#$(SUDO) $(CP) -rf $(TEMPLATES)/* $(IMGTEMP)
	#$(SYNC)
	#$(SUDO) $(CP) ../$(KERNELNAME) $(IMGTEMP)/boot
	#$(SUDO) $(RM) -rf $(IMGTEMP)/lost+found

	#$(CAT) $(GRUBSCRIPT) | $(GRUB) --batch

	#$(SUDO) $(UMOUNT) $(IMGTEMP)
	#$(CHOWN) -R merck:wheel $(SYSIMG)
	$(CP) $(SYSIMG) ..


clean:
	$(RM) -f OluxOS.img


