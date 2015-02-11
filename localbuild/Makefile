FABRIC_DIR = ../nvm/linux-mainline/drivers/block/nvme-fabrics 
PWD = $(shell pwd)/$(FABRIC_DIR)
INC = $(shell pwd)/../nvm/linux-mainline/include/linux
INCFAB = $(shell pwd)/../nvm/linux-mainline/include/linux/nvme-fabrics
EXTRA_CFLAGS += -I$(INCFAB)
ccflags-y += -I$(INCFAB)

ifneq ($(KERNELRELEASE),)
	obj-m := nvme-rdma.o 
	nvme-rdma-objs := $(PWD)/nvme-fabrics.o $(PWD)/nvme-fabrics-rdma.o

else
	KERNELDIR ?= /lib/modules/$(shell uname -r)/build

default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules
	cd $(PWD) && mv nvme-rdma.ko $(shell pwd)

clean:
	-rm -f nvme-rdma.ko
	cd $(PWD) && rm -f *.o *.mod.c
endif

 

#clean:
#	@rm -rf ~/.ccache/* 2>&1 >/dev/null
#	@make -C $(KDIR) M=$(MODULE_DIR) clean 2>&1 >/dev/null
#	@echo Done.


