# Kbuild Makefile for openclaw kernel module

obj-m += openclaw.o
openclaw-objs := openclaw_mod.o openclaw_chardev.o openclaw_netfilter.o openclaw_procfs.o

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

install:
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install
	depmod -a

load:
	insmod openclaw.ko

unload:
	rmmod openclaw

.PHONY: all clean install load unload
