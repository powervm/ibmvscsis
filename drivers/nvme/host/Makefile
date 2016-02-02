
obj-$(CONFIG_BLK_DEV_NVME)	+= nvme.o
obj-$(CONFIG_NVME_FABRICS)	+= nvme-fabrics.o
obj-$(CONFIG_NVME_RDMA)		+= nvme-rdma.o

lightnvm-$(CONFIG_NVM)			:= lightnvm.o
nvme-y					+= core.o pci.o $(lightnvm-y)
nvme-$(CONFIG_BLK_DEV_NVME_SCSI)        += scsi.o

nvme-fabrics-y				+= fabrics.o

nvme-rdma-y				+= rdma.o
