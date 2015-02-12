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

#define NVME_UNUSED(x) ((void)x)

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
 * Public function that...
 *
 * @fabric
 * @capsule
 * @pbio
 * @cmd
 * @dbell_value
 */
void nvme_new_capsule(struct nvme_fabric *fabric,
		      struct nvme_cmd_capsule *capsule,
		      struct bio *pbio,
		      struct nvme_common_command cmd,
		      __u32 dbell_value)
{

	const int UNINIT = -666;
	int ret = UNINIT;  /* to catch uninitialized return value */

	if (fabric->fops) {
		if (fabric->fops->new_capsule) {
			ret = fabric->fops->new_capsule(
				capsule, pbio, cmd, dbell_value);
			if (ret != 0)
				pr_err("%s: Error: new_capsule() =  %d.\n",
				       __func__, ret);
		}
	}
	if (ret == UNINIT)
		pr_err("%s: Error: fops/new_capsule() uninitialized, %d.\n",
		       __func__, ret);
}
EXPORT_SYMBOL_GPL(nvme_new_capsule);

/*
 * Public function that registers this module as a new NVMe fabric
 * driver.
 *
 * @TODO: TODO DUH!!
 * @new_fabric: New fabric with specific operations defined for
 *            that NVMe fabric driver.
 *
 * Return Value:
 *      O for success,
 *	Any other value, error
 */
int nvme_register_fabric(int TODO,
			 struct nvme_fabric_host_operations *new_fabric)
{
	NVME_UNUSED(new_fabric);

	/*
	 * At some point, all this new framework code has to get tied
	 * into the Linux kernel itself into a function like
	 * this one.  Possibilities include:
	 *
	 * driver_register(struct device_driver *drv)
	 *
	 * found in drivers/base, but the problem is this
	 * device_driver struct has a:
	 *
	 * struct bus_type bus
	 *
	 * member in it that I don't think it makes much sense for
	 * us as we may be using ethernet as our 'bus' for RDMA
	 * connections...
	 *
	 * From talking to Phil, looks like we will use a sysfs
	 * call (class_create(), class_create_file(), etc), similiar
	 * to what iscsi does.
	 */
	return 0;
}
EXPORT_SYMBOL_GPL(nvme_register_fabric);

/*
 * Public function that unregisters this module as a new NVMe fabric
 * driver.
 *
 * @TODO: TODO DUH!!
 *
 * Return Value:
 *      O for success,
 *      Any other value, error
 *
 * Big Caveats: This function could go away in favor of a
 * "nvme_fabric_dev_dealloc()" function that will initialize
 * a new nvme_fabric device, or this could go away all together
 * and stuff like nvme_fabric_operations gets initialized in the driver.
 */
int nvme_unregister_fabric(int TODO)
{
	NVME_UNUSED(TODO);
	return 0;
}
EXPORT_SYMBOL_GPL(nvme_unregister_fabric);
