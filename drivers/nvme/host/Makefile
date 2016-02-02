
obj-$(CONFIG_BLK_DEV_NVME)	+= nvme.o
obj-$(CONFIG_NVME_FABRICS)	+= nvme-fabrics.o

lightnvm-$(CONFIG_NVM)			:= lightnvm.o
nvme-y					+= core.o pci.o $(lightnvm-y)
nvme-$(CONFIG_BLK_DEV_NVME_SCSI)        += scsi.o
nvme-fabrics-y				+= fabrics.o
