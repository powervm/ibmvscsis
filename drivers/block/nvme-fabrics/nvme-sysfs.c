
/*
 * sysfs.c - NVM protocol paradigm independent of transport
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

#include <linux/sysfs.h>
#include <linux/string.h>
#include <linux/nvme-fabrics/nvme-fabrics.h>

static struct class *nvme_class;

#define SYSFS_MAX_INPUT			128

/*
 * This function is called when someone writes to the file
 * /sys/class/<nvme-fabric>/add_target.
 *
 * @class: the nvme-fabric 'class' being registered to the Linux filesystem.
 * @attr:  the 'files' under the class being created.
 * @buf: fabric dependent.  For example,
 *       in the case of rdma this in the form of
 *       ip_address port e.g. "x.x.x.x port fabric"
 * @count: the return value of this function.  It's the size of buf??
 *
 * Note: This is just for 'add_target' file.
 */
ssize_t nvme_sysfs_do_add_target(struct class *class,
				 struct class_attribute *attr,
				 const char *buf, size_t count)
{
	char address[SYSFS_MAX_INPUT];
	char *p;
	int fabric;
	int port;
	int ret = -EINVAL;
	int drain;

	pr_info("%s %s()\n", __FILE__, __func__);

	/*
	 * Reworked following code parameter block wo/the macros.
	 * Macros with if() conditionals
	 * don't pass Linux kernel coding standards (i.e., checkpatch.pl)
	 */
	/* FIRST PARAMETER */
	p = strchr(buf, ' ');
	if (!p)
		goto out;
	*p++ = 0;
	/*
	 * FIXME, TODO  we should do something w/the returned
	 * value of sscanf().
	 */
	drain = sscanf(buf, "%s", address);

	/* NEXT PARAMETER */
	buf = p;
	p = strchr(buf, ' ');
	if (!p)
		goto out;
	*p++ = 0;
	/*
	 * FIXME, TODO  we should do something w/the returned
	 * value of sscanf().
	 */
	drain = kstrtoint(buf, 0, &port);

	/* LAST PARAMETER */
	buf = p;
	p = strchr(buf, ' ');
	if (p)
		goto out;
	/*
	 * FIXME, TODO  we should do something w/the returned
	 * value of sscanf().
	 */
	drain = kstrtoint(buf, 0, &fabric);

	nvme_fabric_discovery(address, port, fabric);
	ret = count;

out:
	return ret;
}

/*
 * Called when add_target file under /sys/class/<nvme-fabric> is read.
 */
ssize_t nvme_sysfs_show_add_target(struct class *class,
			struct class_attribute *attr,
			char *buf)
{
	/* FIXME: Use snprintf() instead */
	return sprintf(buf, "{ip_addr} {port} {fabric}\n");
}

/*
 * Note, 'nvme_sysfs_show_add_target' is for reading,
 * 'nvme_sysfs_do_add_target' is for writing.
 *
 * Also Note: Must use 'sudo' or have a privileged access to
 * manipulate these targets.  In particular, using:
 * S_IRUGO | S_IWUGO instead of 0600 causes a wrap-around error
 * at least in the 3.19 kernel (unknown about earlier kernels).
 */
static CLASS_ATTR(add_target, 0600, nvme_sysfs_show_add_target,
		nvme_sysfs_do_add_target);

/*
 * This function is called when someone writes to the file
 * /sys/class/<nvme-fabric>/remove_target.
 *
 * @class: the nvme-fabric 'class' being registered to the Linux filesystem.
 * @attr:  the 'files' under the class being created.
 * @buf: fabric dependent.  For example,
 *       in the case of rdma this in the form of
 *       ip_address port e.g. "x.x.x.x port fabric"
 * @count: the return value of this function.  It's the size of buf??
 *
 * Note: This is just for 'remove_target' file.
 */
ssize_t nvme_sysfs_do_remove_target(struct class *class,
			struct class_attribute *attr,
			const char *buf, size_t count)
{
	char address[SYSFS_MAX_INPUT];
	int port;
	int fabric;
	int ret;
	int drain;

	/* FIXME, TODO: check drain for error condition */
	drain = sscanf(buf, "%s %d %d", address, &port, &fabric);
	ret = nvme_fabric_remove_target(address, port, fabric);
	if (ret)
		count = ret;
	return count;
}

/*
 * Note: Same issue here as CLASS_ATTR(add_target, ...)
 */
static CLASS_ATTR(remove_target, 0600, NULL, nvme_sysfs_do_remove_target);

/*
 * Creates files in /sys/class/<nvme fabric> which are used to
 * cause a fabric driver connect to a remote nvme target.  This
 * is only used for remote fabrics (not local nvme connections like
 * PCIe).
 *
 * Files it creates (WIP):
 *	add_target: this is written to cause the host to start discovery,
 *		target login, and setup the remote drives.
 *	remove_target: This is written to cause the host to remove
 *		the indicated remote (NVMe) target.
 *
 * TODO: The generic fops->probe() call needs to be called
 * in here somewhow, which will point to the specific fabric
 * transport code for discovery
 */
int nvme_sysfs_init(char *nvme_class_name)
{
	int ret = 0;

	pr_info("%s %s()\n", __FILE__, __func__);

	nvme_class = class_create(THIS_MODULE, nvme_class_name);
	if (IS_ERR(nvme_class)) {
		pr_err("%s %s: Failed to register dev class '%s'\n",
		       __FILE__, __func__, nvme_class_name);
		ret = PTR_ERR(nvme_class);
		goto failed_class_create;
	}

	ret = class_create_file(nvme_class, &class_attr_add_target);
	if (ret) {
		pr_err("%s %s: Failed to create add_target attribute\n",
			__FILE__, __func__);
		goto failed_create_add_target;
	}

	ret = class_create_file(nvme_class, &class_attr_remove_target);
	if (ret) {
		pr_err("%s %s: Failed to create remove_target attribute\n",
		       __FILE__, __func__);
		goto failed_create_remove_target;
	}
	return 0;
failed_create_remove_target:
	class_remove_file(nvme_class, &class_attr_add_target);
failed_create_add_target:
	class_destroy(nvme_class);
failed_class_create:
	return ret;
}

/*
 * Unregisters an nvme sysfs class and associated files with
 * the Linux filesystem. Once it's removed, there is no 'initiator'
 * to the remote targets.
 */
void nvme_sysfs_exit(void)
{
	pr_info("%s %s()\n", __FILE__, __func__);
	class_remove_file(nvme_class, &class_attr_add_target);
	class_remove_file(nvme_class, &class_attr_remove_target);
	class_destroy(nvme_class);
}
