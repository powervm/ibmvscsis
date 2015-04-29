/*
 * nvme-common.c - NVM protocol paradigm independent of transport
 *
 *
 * Copyright (C) 2015 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * The NVMe Fabrics project separates the NVMe (Non-Volatile Memory express)
 * SSD protocol from the physical technology or 'fabric' (RDMA, ethernet,
 * PCIe, etc) used as the bus communication mechanism between the storage
 * device and the rest of the system. Thus, this initial NVMe framework
 * makes no assumption that a technology like PCIe or RDMA is being
 * used to carry out the protocol.
 */

#define NVME_UNUSED(x) ((void)x)
#include <linux/nvme-fabrics/nvme-common.h>

#include <linux/hdreg.h>
#include <linux/interrupt.h>
#include <linux/moduleparam.h>
#include <linux/ptrace.h>
#include <scsi/sg.h>

/* TODO: Check to make sure ioctl values are good values to use and take */
#define NVME_COMMON_IOCTL_ID		_IO('N', 0x66)
#define NVME_COMMON_IOCTL_ADMIN_CMD	_IOWR('N', 0x67, struct nvme_admin_cmd)
#define NVME_COMMON_IOCTL_SUBMIT_IO	_IOW('N', 0x68, struct nvme_user_io)

/* NVMe protocol specific timeout/delay values */
#define NVME_ADMIN_TIMEOUT	(admin_timeout * HZ)
#define NVME_IO_TIMEOUT		(io_timeout * HZ)
#define NVME_CLTR_HALT_DELAY	(cltr_halt_delay * HZ)

/* It's ok for this module parameter to stay here nvme-common for
 * an nvme fabric driver, because every nvme fabric driver should
 * have this parameter and the programmer should not ever need
 * to set this value to a specific value.
 */
static int nvme_major;
module_param(nvme_major, int, 0);

/* The folloing module parameters are defined here in nvme-common
 * because their functionality is related to the base NVMe protocol,
 * minus any fabric transport knowledge (plus it looks like this
 * can work from checking out the initial stages of the working
 * but brain-dead nvme-fabric-rdma driver.
 */
static unsigned char admin_timeout = 15;
module_param(admin_timeout, byte, 0644);
MODULE_PARM_DESC(admin_timeout, "timeout in seconds for NVMe admin commands");

static unsigned char io_timeout = 15;
module_param(io_timeout, byte, 0644);
MODULE_PARM_DESC(io_timeout, "timeout in seconds for NVMe I/O");

static unsigned char io_retry_time = 15;
module_param(io_retry_time, byte, 0644);
MODULE_PARM_DESC(io_retry_time, "time in seconds to retry failed I/O");

static unsigned char ctrl_halt_delay = 5;
module_param(ctrl_halt_delay, byte, 0644);
MODULE_PARM_DESC(cltr_halt_delay, "timeout in seconds for ctlr shutdown");

/* TODO: Not sure if we need this module parameter for nvme-common?? */
static int use_threaded_interrupts;
module_param(use_threaded_interrupts, int, 0);

static struct task_struct *nvme_thread;
static wait_queue_head_t nvme_kthread_wait;
static struct workqueue_struct *nvme_workq;

int nvme_common_ioctl(struct block_device *bdev, fmode_t mode,
			unsigned int cmd,
			unsigned long arg)
{
	NVME_UNUSED(bdev);
	NVME_UNUSED(mode);
	NVME_UNUSED(cmd);
	NVME_UNUSED(arg);
	return -69;
}

void nvme_common_free_iod(struct nvme_common_dev *dev,
			  struct nvme_common_iod *iod)
{
	NVME_UNUSED(dev);
	NVME_UNUSED(iod);
}

int nvme_common_setup_prps(struct nvme_common_dev *dev,
			   struct nvme_common_iod *iod,
			   int total_len, gfp_t gfp)
{
	NVME_UNUSED(dev);
	NVME_UNUSED(iod);
	NVME_UNUSED(total_len);
	NVME_UNUSED(gfp);
	return -1;
}

void nvme_common_submit_discard(struct nvme_common_queue *nvmeq,
				       struct nvme_common_ns *ns,
				       struct request *req,
				       struct nvme_common_iod *iod)
{
	NVME_UNUSED(nvmeq);
	NVME_UNUSED(ns);
	NVME_UNUSED(req);
	NVME_UNUSED(iod);
}

void nvme_common_submit_flush(struct nvme_common_queue *nvmeq,
			      struct nvme_common_ns *ns,
			      int cmdid)
{
	NVME_UNUSED(nvmeq);
	NVME_UNUSED(ns);
	NVME_UNUSED(cmdid);
}

int nvme_common_queue_rq(struct blk_mq_hw_ctx *hctx,
			 const struct blk_mq_queue_data *bd)
{
	NVME_UNUSED(hctx);
	NVME_UNUSED(bd);
	return -69;
}

int nvme_common_process_cq(struct nvme_common_queue *nvmeq)
{
	NVME_UNUSED(nvmeq);
	return -69;
}


int nvme_common_submit_admin_cmd(struct nvme_common_dev *dev,
				 struct nvme_command *cmd,
				 u32 *result)
{
	NVME_UNUSED(dev);
	NVME_UNUSED(cmd);
	NVME_UNUSED(result);
	return -69;
}

int nvme_common_identify(struct nvme_common_dev *dev,
			 unsigned nsid, unsigned cns,
			 dma_addr_t dma_addr)
{
	NVME_UNUSED(dev);
	NVME_UNUSED(nsid);
	NVME_UNUSED(cns);
	NVME_UNUSED(dma_addr);
	return -69;
}

int nvme_common_get_features(struct nvme_common_dev *dev, unsigned fid,
			     unsigned nsid,
			     dma_addr_t dma_addr, u32 *result)
{
	NVME_UNUSED(dev);
	NVME_UNUSED(fid);
	NVME_UNUSED(nsid);
	NVME_UNUSED(dma_addr);
	NVME_UNUSED(result);
	return -69;
}

int nvme_common_set_features(struct nvme_common_dev *dev,
			     unsigned fid, unsigned dword11,
			     dma_addr_t dma_addr, u32 *result)
{
	NVME_UNUSED(dev);
	NVME_UNUSED(fid);
	NVME_UNUSED(dword11);
	NVME_UNUSED(dma_addr);
	NVME_UNUSED(result);
	return -69;
}

struct nvme_common_queue *nvme_common_alloc_queue(
				struct nvme_common_dev *dev,
				int qid, int depth, int vector)
{

	NVME_UNUSED(dev);
	NVME_UNUSED(qid);
	NVME_UNUSED(depth);
	NVME_UNUSED(vector);
	return NULL;
}

int nvme_common_create_queue(struct nvme_common_queue *nvmeq, int qid)
{
	NVME_UNUSED(nvmeq);
	NVME_UNUSED(qid);
	return -69;
}

struct nvme_common_ns *nvme_common_alloc_ns(struct nvme_common_dev *dev,
					    unsigned nsid,
					    struct nvme_id_ns *id,
					    struct nvme_lba_range_type *rt)
{
	NVME_UNUSED(dev);
	NVME_UNUSED(nsid);
	NVME_UNUSED(id);
	NVME_UNUSED(rt);
	return NULL;
}

int nvme_common_setup_io_queues(struct nvme_common_dev *dev)
{
	NVME_UNUSED(dev);
	return -69;
}

/*
 * Return: error value if an error occurred setting up the queues or calling
 * Identify Device.  0 if these succeeded, even if adding some of the
 * namespaces failed.  At the moment, these failures are silent.  TBD which
 * failures should be reported.
 */
 /* Revisit / rewrite  - figure out how to do it right */
 /*Seriously FIXME - This is a major function`*/
int nvme_common_dev_add(struct nvme_common_dev *dev)
{
	NVME_UNUSED(dev);
	return -69;
}

/*
 * Initialization function to startup the generic,
 * nvme pcie-free protocol.
 */
int nvme_common_init(void)
{
	int result = -ENOMEM;

	init_waitqueue_head(&nvme_kthread_wait);

	/*
	 * TODO/INVESTIGATE: alloc_workqueue(), instead
	 * of create_singlethread_workqueue()
	 * Ask Matthew/Keith
	 */
	nvme_workq = create_singlethread_workqueue("nvme");
	if (!nvme_workq)
		goto out;

	result = register_blkdev(nvme_major, "nvme");
	if (result < 0)
		goto kill_workq;
	else if (result > 0)
		nvme_major = result;

	pr_info("%s %s(): Exit w/nvme_major %d\n",
		__FILE__, __func__, nvme_major);
	return 0;

 kill_workq:
	pr_err("%s(): Error: register_blkdev() failed, %d\n",
	       __func__, result);
	destroy_workqueue(nvme_workq);
 out:
	pr_err("%s(): Error: creating workqueue failed, %d\n",
	       __func__, result);
	return result;

}

/*
 * Exit function to unregister the generic,
 * nvme, pcie-free protocol stuff.
 */
void nvme_common_exit(void)
{
	unregister_blkdev(nvme_major, "nvme");
	destroy_workqueue(nvme_workq);
	BUG_ON(nvme_thread && !IS_ERR(nvme_thread));
	pr_info("%s %s(): Exit w/nvme_major %d\n", __FILE__, __func__,
		nvme_major);
}
