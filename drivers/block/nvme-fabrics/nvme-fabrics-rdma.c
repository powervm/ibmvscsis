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

/* TODO: Ideally, what I really want, is for these parameter modules
 * to be defined in nvme-fabrics.c or .h file and to force
 * a programmer to define them to default values specific to
 * the transport driver in the .c file (like nvme-fabrics-rdma.c).
 * This way we minimize breaking envisioned tools like 'lsnvme',
 * which will go and collect 'stuff' based on default parameter/file
 * names, and print it out on the command-line,
 * similar to a lsscsi command-line tool.
 * Not sure how to do this yet, as simply re-assigning a value
 * to these parameter variables does not necessarily get updated
 * on /sys/module/nvme_rdma/parameters.
 */
#define FABRIC_STRING_MAX	50
static char fabric_used[FABRIC_STRING_MAX];
module_param_string(fabric_used, "rdma", FABRIC_STRING_MAX, 0444);
MODULE_PARM_DESC(fabric_used, "Read-only description of fabric being used");

static unsigned char fabric_timeout = 15;
module_param(fabric_timeout, byte, 0644);
MODULE_PARM_DESC(fabric_timeout, "Timeout for fabric-specific communication");

/*
 * TODO: Note parameter module strings do not have the ability to change on
 * command-line (like 'echo "name" > hostname') and have the driver notice...
 * at least I don't think so without something crashing.  Need to
 * rework so we always have the actual hostname being used to update on /sys
 * for tools like 'lsnvme' to pick up and use
 */
static char hostname[FABRIC_STRING_MAX];
module_param_string(hostname, "org.nvmeexpress.rdmahost",
			FABRIC_STRING_MAX, 0444);
MODULE_PARM_DESC(hostname, "Read-only default name of nvme rdma host");

static struct nvme_fabric_dev nvme_rdma_dev;

static int nvme_rdma_submit_aq_cmd(struct nvme_fabric_dev *dev,
					struct nvme_common_command *cmd,
					__u32 *result) {

	NVME_UNUSED(dev);
	NVME_UNUSED(cmd);
	NVME_UNUSED(result);

	/*
	 * TODO:
	 * guts of NVMe RDMA specific function defined here;
	 * see nvmerp_submit_aq_cmd() from xport_rdma.c from
	 * demo for idea how it should work.
	 */

	/*
	    rdma_specific_function();
	    create_cmd_capsule();
	    if (admin_cmd == identify) {
		nvme_common_identify(...);
	    }
	    send_nvme_capsule(nvme_cmd_capsule cpl);
	*/

	return -1;
}

/*
 * This is the specific discovery/probe sequence of rdma.
 * The nvme-fabric middle layer will call the generic
 * probe() function to call this.
 */
static int nvme_rdma_probe(int FINISHME)
{
	NVME_UNUSED(FINISHME);
	return -1;
}

/*
 * This is the specific shutdown and cleanup for the RDMA
 * transport of NVMe.
 */
static void nvme_rdma_cleanup(int FINISHME)
{
	NVME_UNUSED(FINISHME);
}

/*
 * Define the specific NVMe RDMA capsule function definitions
 * for this driver.
 */
static struct nvme_fabric_host_operations nvme_rdma_ops = {
	.owner			= THIS_MODULE,
	.prepsend_admin_cmd	= nvme_rdma_submit_aq_cmd,
	.prepsend_io_cmd	= TODO,
	.probe			= nvme_rdma_probe,
	.connect_create_queues  = TODO,
	.stop_destroy_queues    = nvme_rdma_cleanup
};

static void __exit nvme_rdma_exit(void)
{
	int retval;

	pr_info("\n%s: %s()\n", __FILE__, __func__);
	retval = nvme_fabric_unregister(-666);
	pr_info("%s(): retval is %d\n", __func__, retval);
}

static int __init nvme_rdma_init(void)
{
	int retval;

	pr_info("\n%s: %s() hostname: %s fabric: %s\n",
		__FILE__, __func__, hostname, fabric_used);
	retval = nvme_fabric_register(NVMF_CLASS, &nvme_rdma_ops);
	nvme_rdma_dev.fabric_address = 666;
	pr_info("%s(): retval is %d\n", __func__, retval);
	return retval;
}

module_init(nvme_rdma_init);
module_exit(nvme_rdma_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Phil Cayton, Jay Freyensee");
MODULE_DESCRIPTION("NVMe host driver implementation over RDMA fabric");
MODULE_VERSION("0.000000001");
