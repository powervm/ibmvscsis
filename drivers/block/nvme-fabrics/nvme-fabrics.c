/*
 * nvme-fabrics.c - NVM protocol paradigm independent of transport
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

#include <linux/bio.h>
#include <linux/nvme-fabrics/nvme-common.h>
#include <linux/nvme-fabrics/nvme-fabrics.h>
#include <linux/errno.h>
#include <linux/in.h>
#include <linux/inet.h>

#define NVME_UNUSED(x) ((void)x)

/*
 * WIP: The vision here is on a nvme_fabric_register(), at
 * minimum we register the nvme_fabric_host_operations defined
 * for the specific driver into this nvme-fabric middle layer.
 * The middle layer will call the generic function pointer
 * ops API in the appropriate order so specific drivers don't
 * have to worry about stuff that this and to hopefully make
 * it easier to write new host fabric drivers.
 *
 * TODO: A single static variable may not be right (may
 * want an array of variables for example), but for
 * now, practical purposes we can assume a single machine will
 * be a single fabric host. We may also need to register
 * more than the nvme_fabric_host_operations.
 */
static struct nvme_fabric_host_operations *host_fabric_ops;

/*
 * Check we didin't inadvertently grow the command struct
 */
static inline void _nvme_check_size(void)
{
	BUILD_BUG_ON(sizeof(struct nvme_rw_command) != 64);
	BUILD_BUG_ON(sizeof(struct nvme_create_cq) != 64);
	BUILD_BUG_ON(sizeof(struct nvme_create_sq) != 64);
	BUILD_BUG_ON(sizeof(struct nvme_delete_queue) != 64);
	BUILD_BUG_ON(sizeof(struct nvme_features) != 64);
	BUILD_BUG_ON(sizeof(struct nvme_format_cmd) != 64);
	BUILD_BUG_ON(sizeof(struct nvme_abort_cmd) != 64);
	BUILD_BUG_ON(sizeof(struct nvme_command) != 64);
	BUILD_BUG_ON(sizeof(struct nvme_id_ctrl) != 4096);
	BUILD_BUG_ON(sizeof(struct nvme_id_ns) != 4096);
	BUILD_BUG_ON(sizeof(struct nvme_lba_range_type) != 64);
	BUILD_BUG_ON(sizeof(struct nvme_smart_log) != 512);
	BUILD_BUG_ON(sizeof(struct nvme_completion) != 16);
}

/*
 * Public function that starts a target discovery
 *
 * THIS NEEDS TO BE A CAPSULE FUNCTION
 *
 * TODO, WIP, FIXME: The current guts of this function
 *		is actually the start of the
 *		rdma-specific probe() call, so this code will get
 *		moved to nvme-fabric-rdma.c.
 *
 * So:
 *      -nvme_sysfs_init() registers files
 *      -do_add_target() gets called
 *      -do_add_target() calls this function (generic code).
 *       then this function, nvme_fabric_discovery,
 *       calls fops->probe() which will
 *       call the specific-probe/discovery code for the
 *       specific fabric (in this case, nvme-fabric-rdma.c
 *       and the function nvme_rdma_probe())
 *
 * @address: The fabric address used for the discovery connection.
 * @port:    The port on the machine used for the discovery connection.
 * @fabric:  The type of fabric used for the connection.
 * Return Value:
 *      O for success,
 *	Any other value, error
 */
int nvme_fabric_discovery(char *address, int port, int fabric)
{
	struct sockaddr		dstaddr;
	struct sockaddr_in	*dstaddr_in;

	pr_info("%s: %s()\n", __FILE__, __func__);

	dstaddr_in = (struct sockaddr_in *) &dstaddr;
	memset(dstaddr_in, 0, sizeof(*dstaddr_in));
	dstaddr_in->sin_family = AF_INET;

	dstaddr_in->sin_addr.s_addr = in_aton(address);

	dstaddr_in->sin_port = cpu_to_be16(port);
	NVME_UNUSED(fabric);

	return 0;
}
EXPORT_SYMBOL_GPL(nvme_fabric_discovery);

/*
 * Public function that starts the target removal process
 *
 * THIS NEEDS TO BE A CAPSULE FUNCTION
 *
 * TODO, WIP, FIXME
 *
 * Return Value:
 *      O for success,
 *	Any other value, error
 */
int nvme_fabric_remove_target(char *address,
			      int port, int fabric)
{
	struct sockaddr dstaddr;
	struct sockaddr_in *dstaddr_in;

	pr_info("%s: %s()\n", __FILE__, __func__);

	dstaddr_in = (struct sockaddr_in *) &dstaddr;
	memset(dstaddr_in, 0, sizeof(*dstaddr_in));
	dstaddr_in->sin_family = AF_INET;
	dstaddr_in->sin_port = cpu_to_be16(port);
	dstaddr_in->sin_addr.s_addr  = in_aton(address);

	return 0;
}
EXPORT_SYMBOL_GPL(nvme_fabric_remove_target);

/*
 * Public function that registers this module as a new NVMe fabric driver.
 *
 * @new_fabric: New fabric with specific operations defined for
 *            that NVMe fabric driver.
 *
 * Return Value:
 *      O for success,
 *	Any other value, error
 */
int nvme_fabric_register(char *nvme_class_name,
			 struct nvme_fabric_host_operations *new_fabric)
{
	int ret = -EINVAL;

	pr_info("%s: %s()\n", __FILE__, __func__);

	/*
	 * TODO: Basic check, may change if host_fabric_ops changes
	 * as we further implement this stuff.
	 */
	if (host_fabric_ops == NULL)
		host_fabric_ops = new_fabric;
	else {
		pr_err("%s %s(): host_fabric_ops/API already registered!\n",
		       __FILE__, __func__);
		ret = -EBUSY;
	    goto err2;
	}

	ret = nvme_common_init();
	if (ret) {
		pr_err("%s %s(): Error- nvme_common_init() failed\n",
		       __FILE__, __func__);
		goto err2;
	}

	ret = nvme_sysfs_init(nvme_class_name);
	if (ret) {
		pr_err("%s %s(): Error- nvme_sysfs_init() failed\n",
		       __FILE__, __func__);
		goto err3;
	}

	pr_info("%s %s() exited with %d\n",
		__FILE__, __func__, ret);
	return ret;
err3:
	nvme_common_exit();
err2:
	return ret;
}
EXPORT_SYMBOL_GPL(nvme_fabric_register);

/*
 * Public function that unregisters this module as a new NVMe fabric driver.
 *
 * Return Value:
 *      O for success,
 *      Any other value, error
 *
 * Notes: Goal here is the function is keeping everything in order on what
 * to shutdown and cleanup: first fabric specific stuff with the specific
 * driver through host_fabric_ops file-visible variable, then sysfs stuff
 * that is related to nvme-fabric generic stuff, then generic nvme
 * standard stuff.
 */
int nvme_fabric_unregister(int TODO)
{
	NVME_UNUSED(TODO);
	pr_info("%s: %s()\n", __FILE__, __func__);

	/*
	 * TODO, FIXME: The parameter in stop_destroy_queues is WIP
	 */
	host_fabric_ops->stop_destroy_queues(0);
	host_fabric_ops = NULL;
	nvme_sysfs_exit();
	nvme_common_exit();
	return 0;
}
EXPORT_SYMBOL_GPL(nvme_fabric_unregister);
