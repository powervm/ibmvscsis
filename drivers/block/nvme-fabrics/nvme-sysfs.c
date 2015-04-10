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

/*
 * This function is called when someone writes to the file
 * /sys/class/<nvme-fabric>/add_endpoint.
 *
 * @class: the nvme-fabric 'class' being registered to the Linux filesystem.
 * @attr:  the 'files' under the class being created.
 * @buf: PAGE_SIZE buffer containing one or more fabric dependent remote
 *	 nodes assigned to this Host.   For example,
 *       in the case of rdma this in the form of
 *       ip_address port e.g. "x.x.x.x port fabric"
 * @count: the return value of this function is the # bytes used from the
 *	   buffer.
 *	   -In practice this will be the "entire buffer"
 *
 * Note: This is just for 'add_endpoint' file.
 */
ssize_t nvme_sysfs_do_add_endpoint(struct class *class,
				   struct class_attribute *attr,
				   const char *buf, size_t count)
{
	char address[ADDRSIZE];
	char *p;
	int fabric;
	int port;
	int ret = -EINVAL;

	pr_info("%s %s %s()\n", hostname, __FILE__, __func__);

	pr_info("%s\n", buf);
	do {
		/* EP Address */
		p = strchr(buf, ' ');
		if (!p)
			goto out;
		for (; *p == ' '; p++)
			*p = 0;
		strcpy(address, buf);
		buf = p;

		/* EP Port */
		p = strchr(buf, ' ');
		if (!p)
			goto out;
		for (; *p == ' '; p++)
			*p = 0;
		if (*p == '\0' || *p == '\n')
			goto out;
		ret = kstrtoint(buf, 0, &port);
		if (ret)
			goto out;
		buf = p;

		/* EP Fabric Type*/
		p = strchr(buf, ' ');
		if (p)
			for (; *p == ' '; p++)
				*p = 0;
		ret = kstrtoint(buf, 0, &fabric);
		if (ret)
			goto out;
		buf = p;

		pr_info("NVMe Add Remote Endpoint: %s, %d, %d\n",
			address, port, fabric);

		ret = nvme_fabric_discovery(address, port, fabric);

		if (ret < 0) {
			pr_err("%s %s(): Error adding remote endpoint\n",
			       __FILE__, __func__);
			goto err;
		}

	} while (p && *p != '\n');
	return count;

out:
	pr_err("%s %s(): add_endpoint: incorrectly formed string\n",
	       __FILE__, __func__);
err:
	return ret;
}

/*
 * Called when add_endpoint file under /sys/class/<nvme-fabric> is read.
 */
ssize_t nvme_sysfs_show_add_endpoint(struct class *class,
				     struct class_attribute *attr,
				     char *buf)
{
	return snprintf(buf, PAGE_SIZE, "{ip_addr} {port} {fabric}\n");
}

/*
 * Note, 'nvme_sysfs_show_add_endpoint' is for reading,
 * 'nvme_sysfs_do_add_endpoint' is for writing.
 *
 * Also Note: Must use 'sudo' or have a privileged access to
 * manipulate these endpoints.  In particular, using:
 * S_IRUGO | S_IWUGO instead of 0600 causes a wrap-around error
 * at least in the 3.19 kernel (unknown about earlier kernels).
 */
static CLASS_ATTR(add_endpoint, 0600, nvme_sysfs_show_add_endpoint,
		  nvme_sysfs_do_add_endpoint);

/*
 * This function is called when someone writes to the file
 * /sys/class/<nvme-fabric>/set_hostname.
 *
 * @class: the nvme-fabric 'class' being registered to the Linux filesystem.
 * @attr:  the 'files' under the class being created.
 * @buf: hostname this driver should use when connecting to a remote EP
 * @count: the return value of this function is the # bytes used from the
 *	   buffer.
 *	   - In practice this will be the "entire buffer"
 *
 * Note: This is just for 'set_hostname' file.
 */
ssize_t nvme_sysfs_do_set_hostname(struct class *class,
				   struct class_attribute *attr,
				   const char *buf, size_t count)
{
	int ret;

	ret = sscanf(buf, "%s", hostname);
	if (ret <= 0) {
		pr_err("%s %s(): set_hostname: incorrectly formed string\n",
		       __FILE__, __func__);
		return ret;
	}

	return count;
}

/*
 * Called when add_endpoint file under /sys/class/<nvme-fabric> is read.
 */
ssize_t nvme_sysfs_show_set_hostname(struct class *class,
				     struct class_attribute *attr,
				     char *buf)
{
	return snprintf(buf, PAGE_SIZE, "hostname = %s\n", hostname);
}

/*
 * Note: Same issue here as CLASS_ATTR(add_endpoint, ...)
 */
static CLASS_ATTR(set_hostname, 0600, nvme_sysfs_show_set_hostname,
		  nvme_sysfs_do_set_hostname);

/*
 * This function is called when someone writes to the file
 * /sys/class/<nvme-fabric>/remove_endpoint.
 *
 * @class: the nvme-fabric 'class' being registered to the Linux filesystem.
 * @attr:  the 'files' under the class being created.
 * @buf: fabric dependent.  For example,
 *       in the case of rdma this in the form of
 *       ip_address port e.g. "x.x.x.x port fabric"
 * @count: the return value of this function is the # bytes used from the
 *	   buffer.
 *	   - In practice this will be the "entire buffer"
 *
 * Note: This is just for 'remove_endpoint' file.
 */
ssize_t nvme_sysfs_do_remove_endpoint(struct class *class,
				      struct class_attribute *attr,
				      const char *buf, size_t count)
{
	char address[ADDRSIZE];
	int port;
	int fabric;
	int ret;

	ret = sscanf(buf, "%s %d %d", address, &port, &fabric);
	if (ret <= 0)
		goto out;

	ret = nvme_fabric_remove_endpoint(address, port, fabric);
	if (ret)
		goto out;

	ret = count;
out:
	return ret;
}

/*
 * Note: Same issue here as CLASS_ATTR(add_endpoint, ...)
 */
static CLASS_ATTR(remove_endpoint, 0600, NULL, nvme_sysfs_do_remove_endpoint);

/*
 * This function is called when someone writes to the file
 * /sys/class/<nvme-fabric>/add_subsystem.
 *
 * @class: the nvme-fabric 'class' being registered to the Linux filesystem.
 * @attr:  the 'files' under the class being created.
 * @buf: PAGE_SIZE buffer containing one or more fabric dependent remote
 *	 nodes assigned to this Host.   For example,
 *       in the case of rdma this in the form of
 *       ip_address port e.g. "x.x.x.x port fabric"
 * @count: the return value of this function is the # bytes used from the
 *         buffer.
 *	   -In practice this will be the "entire buffer"
 *
 * Note: This is just for 'add_subsystem' file.
 */
ssize_t nvme_sysfs_do_add_subsystem(struct class *class,
				    struct class_attribute *attr,
				    const char *buf, size_t count)
{
	char	*p;
	char	subsystem_name[FABRIC_STRING_MAX];
	char	controller_name[FABRIC_STRING_MAX];
	char	controller_address[ADDRSIZE];
	int	controller_port;
	int	fabric_type;
	int	ret = -EINVAL;

	pr_info("%s %s()\n", __FILE__, __func__);

	pr_info("%s\n", buf);

	/* Subsystem Name */
	p = strchr(buf, ' ');
	if (!p)
		goto out;
	for (; *p == ' '; p++)
		*p = 0;
	strcpy(subsystem_name, buf);
	buf = p;
	do {
		/* Controller Name */
		p = strchr(buf, ' ');
		if (!p)
			goto out;
		for (; *p == ' '; p++)
			*p = 0;
		strcpy(controller_name, buf);
		buf = p;

		/* Address */
		p = strchr(buf, ' ');
		if (!p)
			goto out;
		for (; *p == ' '; p++)
			*p = 0;
		strcpy(controller_address, buf);
		buf = p;

		/* Port */
		p = strchr(buf, ' ');
		if (!p)
			goto out;
		for (; *p == ' '; p++)
			*p = 0;
		if (*p == '\0' || *p == '\n')
			goto out;
		ret = kstrtoint(buf, 0, &controller_port);
		if (ret)
			goto out;
		buf = p;

		/* Fabric Type */
		p = strchr(buf, ' ');
		if (p)
			for (; *p == ' '; p++)
				*p = 0;
		ret = kstrtoint(buf, 0, &fabric_type);
		if (ret)
			goto out;
		buf = p;

		pr_info("NVMe Add Remote Subsytem: %s, %s, %s, %d, %d\n",
			subsystem_name, controller_name,
			controller_address, controller_port,
			fabric_type);

		ret = nvme_fabric_add_subsystem(subsystem_name,
						controller_name,
						fabric_type,
						controller_address,
						controller_port);

		if (ret) {
			pr_err("%s %s(): Error adding subsystem\n",
			       __FILE__, __func__);
			goto err;
		}

	} while (p && *p != '\n');
	ret = count;

	return ret;

out:
	pr_err("%s %s(): add_endpoint: incorrectly formed string\n",
	       __FILE__, __func__);
err:
	return ret;
}

/*
 * Called when add_endpoint file under /sys/class/<nvme-fabric> is read.
 */
ssize_t nvme_sysfs_show_add_subsystem(struct class *class,
				      struct class_attribute *attr,
				      char *buf)
{
	return snprintf(buf, PAGE_SIZE, "{ip_addr} {port} {fabric}\n");
}

/*
 * Note, 'nvme_sysfs_show_add_subsystem' is for reading,
 * 'nvme_sysfs_do_add_subsystem' is for writing.
 *
 * Also Note: Must use 'sudo' or have a privileged access to
 * manipulate these endpoints.  In particular, using:
 * S_IRUGO | S_IWUGO instead of 0600 causes a wrap-around error
 * at least in the 3.19 kernel (unknown about earlier kernels).
 */
static CLASS_ATTR(add_subsystem, 0600, nvme_sysfs_show_add_subsystem,
		  nvme_sysfs_do_add_subsystem);

/*
 * Creates files in /sys/class/<nvme fabric> which are used to
 * cause a fabric driver connect to a remote nvme endpoint.  This
 * is only used for remote fabrics (not local nvme connections like
 * PCIe).
 *
 * Files it creates (WIP):
 *	add_endpoint: this is written to cause the host to start discovery,
 *		endpoint login, and setup the remote drives.
 *	remove_endpoint: This is written to cause the host to remove
 *		the indicated remote (NVMe) endpoint.
 *	set_hosntame: This is written to cause the host to set the
 *		hostname to the the indicated value.
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

	ret = class_create_file(nvme_class, &class_attr_add_endpoint);
	if (ret) {
		pr_err("%s %s: Failed to create add_endpoint attribute\n",
		       __FILE__, __func__);
		goto failed_create_add_endpoint;
	}

	ret = class_create_file(nvme_class, &class_attr_remove_endpoint);
	if (ret) {
		pr_err("%s %s: Failed to create remove_endpoint attribute\n",
		       __FILE__, __func__);
		goto failed_create_remove_endpoint;
	}

	ret = class_create_file(nvme_class, &class_attr_add_subsystem);
	if (ret) {
		pr_err("%s %s: Failed to create add_subsystem attribute\n",
		       __FILE__, __func__);
		goto failed_create_add_subsystem;
	}

	ret = class_create_file(nvme_class, &class_attr_set_hostname);
	if (ret) {
		pr_err("%s %s: Failed to create set_hostname attribute\n",
		       __FILE__, __func__);
		goto failed_create_set_hostname;
	}
	return 0;
failed_create_set_hostname:
	class_remove_file(nvme_class, &class_attr_add_subsystem);
failed_create_add_subsystem:
	class_remove_file(nvme_class, &class_attr_remove_endpoint);
failed_create_remove_endpoint:
	class_remove_file(nvme_class, &class_attr_add_endpoint);
failed_create_add_endpoint:
	class_destroy(nvme_class);
failed_class_create:
	return ret;
}

/*
 * Unregisters an nvme sysfs class and associated files with
 * the Linux filesystem. Once it's removed, there is no 'initiator'
 * to the remote endpoint.
 */
void nvme_sysfs_exit(void)
{
	pr_info("%s %s()\n", __FILE__, __func__);
	class_remove_file(nvme_class, &class_attr_set_hostname);
	class_remove_file(nvme_class, &class_attr_add_subsystem);
	class_remove_file(nvme_class, &class_attr_add_endpoint);
	class_remove_file(nvme_class, &class_attr_remove_endpoint);
	class_destroy(nvme_class);
}
