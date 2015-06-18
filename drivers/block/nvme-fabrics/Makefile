nvme-rdma-y	:= nvme-fabrics.o nvme-fabrics-rdma.o nvme-common.o nvme-sysfs.o
obj-m		+= nvme-rdma.o

ifeq ("$(KDIR)","")
# build out-of-tree kernel module

ifneq ("","$(wildcard /usr/src/compat-rdma)")
  #for OFED-3.12
  OFED_DIR:=/usr/src/compat-rdma
else ifneq ("","$(wildcard /usr/src/ofa_kernel)")
  #for OFED 1.5.4.1
  OFED_DIR:=/usr/src/ofa_kernel
else
  OFED_DIR:=
endif

#KDIR ?= /lib/modules/`uname -r`/build
KDIR := /home/cayton/master
autoconf_h=$(shell /bin/ls -1 $(KDIR)/include/*/autoconf.h | head -1)
kconfig_h=$(shell /bin/ls -1 $(KDIR)/include/*/kconfig.h 2>/dev/null | head -1)

MODULE_DIR:=$(PWD)
#BASE_DIR:=$(MODULE_DIR)/..

ifneq ("$(OFED_DIR)","")
 KSYM += $(OFED_DIR)/Module.symvers
 INCLUDES:=-I$(OFED_DIR)/include
endif

INCLUDES+=	\
	   -Iarch/x86/include \
	   -Iarch/x86/include/generated/uapi \
	   -Iarch/x86/include/generated  \
	   -Iinclude \
	   -Iarch/x86/include/uapi \
	   -Iarch/x86/include/generated/uapi \
	   -Iinclude/uapi \
	   -Iinclude/generated/uapi

ifneq ($(kconfig_h),)
 INCLUDES+= -include $(kconfig_h)
endif

INCLUDES+=-g

KBUILD_SRC=$(KDIR)

all: $(KSYM)
	@make -C $(KDIR) M=$(MODULE_DIR) modules \
		LINUXINCLUDE='$(INCLUDES)' KBUILD_EXTRA_SYMBOLS+='$(KSYM)'

clean:
	@make -C $(KDIR) M=$(MODULE_DIR) clean 2>&1 >/dev/null
	@echo Done.

info:
	@#modinfo nvme-fabrics-common.ko || echo > /dev/null
	@#modinfo nvme-fabrics-pci.ko  || echo > /dev/null
	@modinfo nvme-fabrics-agnostic.ko  || echo > /dev/null
	@modinfo nvme-fabrics-rdma.ko  || echo > /dev/null

load-rdma:
	@sudo modprobe ib_core
	@sudo modprobe ib_addr
	@sudo modprobe ib_mad
	@sudo modprobe ib_sa
	@sudo modprobe rdma_cm

load:
	@#sudo insmod nvme-fabrics-common.ko
	@#sudo insmod nvme-fabrics-pci.ko
	@sudo insmod nvme-fabrics-agnostic.ko
	@sudo insmod nvme-fabrics-rdma.ko

unload:
	@sudo rmmod nvme-fabrics-rdma  || echo > /dev/null
	@sudo rmmod nvme-fabrics-agnostic  || echo > /dev/null
	@#sudo rmmod nvme-fabrics-pci  || echo > /dev/null
	@#sudo rmmod nvme-fabrics-common || echo > /dev/null

sandbox-test:
	@sudo rm /tmp/crash.log.x || echo > /dev/null
	@sudo dd if=/home/cayton/sandbox/block/crash.log of=/dev/nvme0q1
	@sudo dd of=/tmp/crash.log.x if=/dev/nvme0q1
	@sudo diff -s /home/cayton/sandbox/block/crash.log /tmp/crash.log.x
	@sudo rm /tmp/crash.log.x

TMP_DIR=$(shell date +%y%m%d_%H%M)

backup:
	@mkdir archive/$(TMP_DIR)
	@cp Makefile *.c archive/$(TMP_DIR)
	@mkdir ../include/linux/archive/$(TMP_DIR)
	@cp ../include/linux/*.h ../include/linux/archive/$(TMP_DIR)
	@echo Done.

help:
	@echo "  make"
	@echo "  make clean"
	@echo "  make load	# load module"
	@echo "  make unload	# unload module"
	@echo "  make info	# get info on module"
endif
