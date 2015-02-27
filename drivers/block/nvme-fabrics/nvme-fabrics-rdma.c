/*
 * nvme-fabrics-rdma.h - NVM protocol paradigm independent of transport
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
 *
 * This is the device driver implementation of NVMe over RDMA fabric.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/nvme-fabrics/nvme-common.h>
#include <linux/nvme-fabrics/nvme-fabrics.h>
#include <linux/nvme-fabrics/nvme-fabrics-rdma.h>
#include <linux/errno.h>

#define TODO	NULL

static struct nvme_fabric_dev nvme_rdma_dev;

static int nvme_rdma_submit_aq_cmd(struct nvme_fabric_dev *dev,
					struct nvme_common_command *cmd,
					__u32 *result) {

	/*
	 * TODO:
	 * guts of NVMe RDMA specific function defined here;
	 * see nvmerp_submit_aq_cmd() from xport_rdma.c from
	 * demo for idea how it should work.
	 */

	result = TODO;
	return -1;
}

/*
 * Define the specific NVMe RDMA capsule function definitions
 * for this driver.
 */
static struct nvme_fabric_host_operations nvme_rdma_ops = {
	.owner			= THIS_MODULE,
	.new_capsule		= TODO,
	.prepsend_admin_cmd	= nvme_rdma_submit_aq_cmd,
	.prepsend_io_cmd	= TODO,
	.probe			= TODO,
	.connect_create_queues  = TODO,
	.stop_destroy_queues    = TODO,
	.send_capsule		= TODO
};

static void __exit nvme_rdma_exit(void)
{
	int retval = nvme_unregister_fabric(-666);

	pr_info("\n%s: %s, retval %d\n\n", __FILE__, __func__, retval);
}

static int __init nvme_rdma_init(void)
{
	int retval = nvme_register_fabric(-666, &nvme_rdma_ops);

	nvme_rdma_dev.fabric_address = 666;
	pr_info("\n%s: %s, retval %d\n\n", __FILE__, __func__, retval);
	return retval;
}

module_init(nvme_rdma_init);
module_exit(nvme_rdma_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Phil Cayton, Jay Freyensee");
MODULE_DESCRIPTION("NVMe host driver implementation over RDMA fabric");
MODULE_VERSION("0.000000001");
