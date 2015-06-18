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
#include <linux/nvme-fabrics/nvme-fabrics.h>
#include <linux/string.h>

static struct class *nvme_class;

static inline size_t strscpy(char *dest, const char *src, size_t size)
{
	size_t			len;

	len = strnlen(src, size) + 1;
	if (len > size) {
		if (size)
			dest[0] = '\0';
		return 0;
	}
	memcpy(dest, src, len);

	return len;
}

static inline int more_to_parse(const char *p)
{
	return !!*p;
}

static inline char *next_argument(const char *buf)
{
	char			*p;

	for (p = (char *) buf; *p && *p != ' ' && *p != '\n'; )
		p++;

	if (*p) {
		*p++ = 0;
		for (; *p == ' ' || *p == '\n'; )
			p++;
	}

	return p;
}

static inline int parse_string(char *str, int max, const char **buf)
{
	char			*p;

	p = next_argument(*buf);

	if (!strscpy(str, *buf, max))
		return -EINVAL;

	*buf = p;

	return 0;
}

static inline int parse_int(int *val, const char **buf)
{
	char			*p;
	int			ret;

	p = next_argument(*buf);

	ret = kstrtoint(*buf, 0, val);
	if (ret)
		return -EINVAL;

	*buf = p;

	return 0;
}

/*
 * This function is called when someone writes to the file
 * /sys/class/<nvme-fabric>/add_discover_server.
 *
 * @class: the nvme-fabric 'class' being registered to the Linux filesystem.
 * @attr:  the 'files' under the class being created.
 * @buf: PAGE_SIZE buffer containing one or more fabric dependent remote
 *	 nodes assigned to this Host.   For example,
 *       in the case of rdma this in the form of
 *       ip_address port e.g. "x.x.x.x port fabric"
 * @count: return value of this function is the # bytes used from the buffer.
 *	   -In practice this will be the "entire buffer"
 *
 * Note: This is just for 'add_discover_server' file.
 */
ssize_t nvme_sysfs_do_add_discover_server(struct class *class,
		struct class_attribute *attr,
		const char *buf, size_t count)
{
	struct nvme_fabric_addr *fabric_addr = NULL;
	char			*address = NULL;
	char			 hostname[NVME_FABRIC_IQN_MAXLEN] = {0};
	int			 fabric_type;
	int			 address_type;
	int			 port;
	int			 simulation;
	int			 ret = -EINVAL;

	nvme_fabric_get_hostname(hostname);

	pr_info("%s %s %s()\n", hostname, __FILE__, __func__);

	fabric_addr = kzalloc(sizeof(struct nvme_fabric_addr), GFP_KERNEL);
	if (!fabric_addr)
		return -ENOMEM;

	address = kzalloc(DNS_ADDR_SIZE, GFP_KERNEL);
	if (!address) {
		ret = -ENOMEM;
		goto err1;
	}

	ret = parse_string(address, DNS_ADDR_SIZE, &buf);
	if (ret || !more_to_parse(buf))
		goto err2;

	ret = parse_int(&port, &buf);
	if (ret || !more_to_parse(buf))
		goto err2;

	ret = parse_int(&address_type, &buf);
	if (ret || !more_to_parse(buf))
		goto err2;

	ret = parse_int(&fabric_type, &buf);
	if (ret)
		goto err2;

	/* OPTIONAL: check what subsystems are assigned by the discover svr */
	if (more_to_parse(buf)) {
		ret = parse_int(&simulation, &buf);
		if (ret || more_to_parse(buf))
			goto err2;
	}

	pr_info("NVMe Add Remote Controller: %s, %d, %d %d %d\n",
		address, port, address_type, fabric_type, simulation);

	ret = nvme_fabric_parse_addr(address_type, address, port, fabric_addr);
	if (ret)
		goto err2;

	ret = nvme_fabric_discovery(fabric_addr, fabric_type, simulation);
	if (ret < 0)
		goto err2;

	kfree(fabric_addr);
	kfree(address);
	return count;

err2:
	kfree(address);
err1:
	kfree(fabric_addr);
	return ret;
}

/*
 * Called when add_discover_server file under /sys/class/<nvme-fabric> is read.
 */
ssize_t nvme_sysfs_show_discover_server(struct class *class,
					struct class_attribute *attr,
					char *buf)
{
	char *subsys_name	= "{subsys name}";
	char *fabric_stuff	= "{fabric type}";
	char *net_address	= "{address type} {subsys net addr} {port}";
	char *other		= "{dry-run?}";

	return snprintf(buf, PAGE_SIZE,
			"%s %s %s %s\n",
			subsys_name, fabric_stuff, net_address, other);
}

/*
 * Note, 'nvme_sysfs_show_discover_server' is for reading,
 * 'nvme_sysfs_do_add_discover_server' is for writing.
 *
 * Also Note: Must use 'sudo' or have a privileged access to
 * manipulate these controller.  In particular, using:
 * S_IRUGO | S_IWUGO instead of 0600 causes a wrap-around error
 * at least in the 3.19 kernel (unknown about earlier kernels).
 */
static CLASS_ATTR(add_discover_server, 0600, nvme_sysfs_show_discover_server,
		  nvme_sysfs_do_add_discover_server);

/*
 * This function is called when someone writes to the file
 * /sys/class/<nvme-fabric>/set_hostname.
 *
 * @class: the nvme-fabric 'class' being registered to the Linux filesystem.
 * @attr:  the 'files' under the class being created.
 * @buf: hostname this driver should use when connecting to a remote EP
 * @count: the return value of this func is # bytes used from the buffer.
 *
 * Returns the value of count
 */
ssize_t nvme_sysfs_do_set_hostname(struct class *class,
				   struct class_attribute *attr,
				   const char *buf, size_t count)
{
	char hostname[NVME_FABRIC_IQN_MAXLEN];
	int  ret;

	ret = parse_string(hostname, NVME_FABRIC_IQN_MAXLEN, &buf);
	if (ret || more_to_parse(buf))
		goto err;

	ret = strlen(hostname);
	if (ret < NVME_FABRIC_IQN_MINLEN) {
		pr_err("%s(): IQN naming error, name is %d length min.\n",
		       __func__, NVME_FABRIC_IQN_MINLEN);
		pr_err("%s(): Hostname read: %s, is %d bytes long\n",
		       __func__, hostname, ret);
		nvme_fabric_set_hostname("IQN Minlen Error");
		goto err;
	}

	nvme_fabric_set_hostname(hostname);
	nvme_fabric_get_hostname(hostname);
	pr_info("%s: Fabric hostname is %s, %d bytes long\n",
		__func__, hostname, ret);

	return count;
err:
	return ret;
}

/*
 * Called when discover_server file under /sys/class/<nvme-fabric> is read.
 */
ssize_t nvme_sysfs_show_set_hostname(struct class *class,
				     struct class_attribute *attr,
				     char *buf)
{
	char hostname[NVME_FABRIC_IQN_MAXLEN];

	nvme_fabric_get_hostname(hostname);

	return snprintf(buf, PAGE_SIZE, "hostname = %s\n", hostname);
}

/*
 * Note: Same issue here as CLASS_ATTR(discover_server, ...)
 */
static CLASS_ATTR(set_hostname, 0600, nvme_sysfs_show_set_hostname,
		  nvme_sysfs_do_set_hostname);

/*
 *
 * @class: the nvme-fabric 'class' being registered to the Linux filesystem.
 * @attr:  the 'files' under the class being created.
 * @buf: fabric dependent.  For example,
 *       in the case of rdma this in the form of
 *       ip_address port e.g. "x.x.x.x port fabric"
 * @count: return value of this function is the # bytes used from the buffer.
 *	   - In practice this will be the "entire buffer"
 *
 */
ssize_t nvme_sysfs_do_remove_controller(struct class *class,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	char	subsys_name[NVME_FABRIC_IQN_MAXLEN];
	int	cntlid;
	int	ret;

	ret = parse_string(subsys_name, NVME_FABRIC_IQN_MAXLEN, &buf);
	if (ret || !more_to_parse(buf)) {
		ret = -EINVAL;
		goto err;
	}

	ret = parse_int(&cntlid, &buf);
	if (ret || !more_to_parse(buf)) {
		ret = -EINVAL;
		goto err;
	}

	ret = nvme_fabric_remove_host_treenode(subsys_name, cntlid);
	if (ret) {
		pr_err("Could not find subsystem %s / controller %d\n",
		       subsys_name, cntlid);
		goto err;
	}

	return count;
err:
	return ret;
}

/*
 * Called when remove_controller file under /sys/class/<nvme-fabric> is read.
 */
ssize_t nvme_sysfs_show_remove_controller(struct class *class,
		struct class_attribute *attr,
		char *buf)
{
	return snprintf(buf, PAGE_SIZE,
			"{subsystem name} {controller id (cntlid)}\n");
}

/*
 * Note: Same issue here as CLASS_ATTR(discover_server, ...)
 */
static CLASS_ATTR(remove_controller, 0600, nvme_sysfs_show_remove_controller,
		  nvme_sysfs_do_remove_controller);
/*
 *
 * @class: the nvme-fabric 'class' being registered to the Linux filesystem.
 * @attr:  the 'files' under the class being created.
 * @buf: fabric dependent.  For example,
 *       in the case of rdma this in the form of
 *       ip_address port e.g. "x.x.x.x port fabric"
 * @count: return value of this function is the # bytes used from the buffer.
 *	   - In practice this will be the "entire buffer"
 *
 */
ssize_t nvme_sysfs_do_remove_subsystem(struct class *class,
				       struct class_attribute *attr,
				       const char *buf, size_t count)
{
	char	subsys_name[NVME_FABRIC_IQN_MAXLEN];
	int	ret;

	ret = parse_string(subsys_name, NVME_FABRIC_IQN_MAXLEN, &buf);
	if (ret || !more_to_parse(buf)) {
		ret = -EINVAL;
		goto err;
	}

	ret = nvme_fabric_remove_host_treenode(subsys_name, 0xFFFF);
	if (ret) {
		pr_err("Could not remove subsystem %s\n", subsys_name);
		goto err;
	}

	return count;
err:
	return ret;
}

/*
 * Called when remove_subsystem file under /sys/class/<nvme-fabric> is read.
 */
ssize_t nvme_sysfs_show_remove_subsystem(struct class *class,
		struct class_attribute *attr,
		char *buf)
{
	char *subsys	 = "{subsys name}";

	return snprintf(buf, PAGE_SIZE, "%s\n", subsys);
}

/*
 * Note: Same issue here as CLASS_ATTR(discover_server, ...)
 */
static CLASS_ATTR(remove_subsystem, 0600, nvme_sysfs_show_remove_subsystem,
		  nvme_sysfs_do_remove_subsystem);

/*
 * This function is called when someone writes to the file
 * /sys/class/<nvme-fabric>/add_subsystem.
 *
 * @class: the nvme-fabric 'class' being registered to the Linux filesystem.
 * @attr:  the 'files' under the class being created.
 * @buf: PAGE_SIZE buffer containing one or more fabric dependent remote
 *       nodes assigned to this Host.
 *
 * @count: return value of this function is # bytes used from the buffer.
 *	   -In practice this will be the "entire buffer"
 *
 * Note: This is just for 'add_subsystem' file.
 *
 * Note 2: Format of buffer input is:
 * {subsys name} {fabric type} {conn type of fabric}
 * {addr type} {subsys addr} {port}
 */

ssize_t nvme_sysfs_do_add_subsystem(struct class *class,
				    struct class_attribute *attr,
				    const char *buf, size_t count)
{
	struct nvme_fabric_addr	*fabric_subsys_addr;
	char			 subsys_addr[DNS_ADDR_SIZE];
	char			 subsys_name[NVME_FABRIC_IQN_MAXLEN];
	unsigned int		 subsys_port;
	unsigned int		 fabric_type;
	unsigned int		 address_type;
	unsigned int		 conn_type;
	int			 ret = -ENOMEM;

	pr_info("%s %s()\n", __FILE__, __func__);

	fabric_subsys_addr = kzalloc(sizeof(struct nvme_fabric_addr),
				     GFP_KERNEL);
	if (!fabric_subsys_addr)
		return ret;

	ret = parse_string(subsys_name, NVME_FABRIC_IQN_MAXLEN, &buf);
	if (ret || !more_to_parse(buf))
		goto err;
	ret = strlen(subsys_name);
	if (ret < NVME_FABRIC_IQN_MINLEN) {
		pr_err("%s(): IQN naming error, min length is %d\n",
		       __func__, NVME_FABRIC_IQN_MINLEN);
		pr_err("%s(): subsys name %s is %d bytes long.\n",
		       __func__, subsys_name, ret);
		ret = -EINVAL;
		goto err;
	}

	ret = parse_int(&fabric_type, &buf);
	if (ret || !more_to_parse(buf))
		goto err;

	ret = parse_int(&conn_type, &buf);
	if (ret || !more_to_parse(buf))
		goto err;

	ret = parse_int(&address_type, &buf);
	if (ret || !more_to_parse(buf))
		goto err;

	/* Max address length of any network address is DNS;
	 * IP4/IP6 will fit just fine in it.
	 */
	ret = parse_string(subsys_addr, DNS_ADDR_SIZE, &buf);
	if (ret || !more_to_parse(buf))
		goto err;

	ret = parse_int(&subsys_port, &buf);
	if (ret)
		goto err;

	ret = nvme_fabric_parse_addr(address_type, subsys_addr,
				     subsys_port, fabric_subsys_addr);
	if (ret)
		goto err;

	/*
	 * Add the controller to nvme_host's tree as a
	 * new fabric connection
	 */
	pr_info("%s: NVMe Add Subsys: %s %d %d %d %s %d\n",
		__func__, subsys_name, fabric_type, conn_type,
		address_type, subsys_addr, subsys_port);

	ret = nvme_fabric_add_controller(subsys_name,
					 fabric_type, conn_type,
					 fabric_subsys_addr);
	if (ret) {
		pr_err("%s(): cannot add subsystem %s\n",
		       __func__, subsys_name);
		goto err2;
	}
	kfree(fabric_subsys_addr);
	return count;

err:
	pr_err("%s %s(): Parse error on %s\n", __FILE__, __func__, buf);
	ret = -EINVAL;
err2:
	kfree(fabric_subsys_addr);
	return ret;
}

/*
 * Called when add_subsystem file under /sys/class/<nvme-fabric> is read.
 */
ssize_t nvme_sysfs_show_add_subsystem(struct class *class,
				      struct class_attribute *attr,
				      char *buf)
{
	char *subsys	 = "{subsys name}";
	char *fabrictype = "{fabric type}";
	char *connection = "{connection type}";
	char *net_addr   = "{address type} {subsys net addr} {port}";

	return snprintf(buf, PAGE_SIZE, "%s %s %s %s\n",
			subsys, fabrictype, connection, net_addr);
}

/*
 * Note, 'nvme_sysfs_show_add_subsystem' is for reading,
 * 'nvme_sysfs_do_add_subsystem' is for writing.
 *
 * Also Note: Must use 'sudo' or have a privileged access to
 * manipulate these controller.  In particular, using:
 * S_IRUGO | S_IWUGO instead of 0600 causes a wrap-around error
 * at least in the 3.19 kernel (unknown about earlier kernels).
 */
static CLASS_ATTR(add_subsystem, 0600, nvme_sysfs_show_add_subsystem,
		  nvme_sysfs_do_add_subsystem);

/*
 * Creates files in /sys/class/<nvme fabric> which are used to
 * cause a fabric driver connect to a remote nvme controller.  This
 * is only used for remote fabrics (not local PCIe nvme connections)
 */
int nvme_sysfs_init(char *nvme_class_name)
{
	int ret = 0;

	pr_info("%s %s()\n", __FILE__, __func__);

	nvme_class = class_create(THIS_MODULE, nvme_class_name);
	if (IS_ERR(nvme_class)) {
		pr_err("%s: Failed to create sysfs class %s\n",
		       __func__, nvme_class_name);
		ret = PTR_ERR(nvme_class);
		goto failed_class_create;
	}

	ret = class_create_file(nvme_class, &class_attr_add_discover_server);
	if (ret) {
		pr_err("%s: Failed creating add_discover_server entry\n",
		       __func__);
		goto failed_create_add_discover_server;
	}

	ret = class_create_file(nvme_class, &class_attr_remove_controller);
	if (ret) {
		pr_err("%s: Failed creating remove_controller entry\n",
		       __func__);
		goto failed_create_remove_controller;
	}

	ret = class_create_file(nvme_class, &class_attr_add_subsystem);
	if (ret) {
		pr_err("%s: Failed creating add_subsystem entry\n",
		       __func__);
		goto failed_create_add_subsystem;
	}

	ret = class_create_file(nvme_class, &class_attr_remove_subsystem);
	if (ret) {
		pr_err("%s: Failed creating remove_subsystem entry\n",
		       __func__);
		goto failed_create_remove_subsystem;
	}

	ret = class_create_file(nvme_class, &class_attr_set_hostname);
	if (ret) {
		pr_err("%s: Failed creating set_hostname entry\n",
		       __func__);
		goto failed_create_set_hostname;
	}

	return 0;

failed_create_set_hostname:
	class_remove_file(nvme_class, &class_attr_remove_subsystem);
failed_create_remove_subsystem:
	class_remove_file(nvme_class, &class_attr_add_subsystem);
failed_create_add_subsystem:
	class_remove_file(nvme_class, &class_attr_remove_controller);
failed_create_remove_controller:
	class_remove_file(nvme_class, &class_attr_add_discover_server);
failed_create_add_discover_server:
	class_destroy(nvme_class);
failed_class_create:
	return ret;

}

/*
 * Unregisters an nvme sysfs class and associated files with
 * the Linux filesystem. Once it's removed, there is no 'initiator'
 * to the remote controller.
 */
void nvme_sysfs_exit(void)
{
	pr_info("%s %s()\n", __FILE__, __func__);

	class_remove_file(nvme_class, &class_attr_set_hostname);
	class_remove_file(nvme_class, &class_attr_remove_subsystem);
	class_remove_file(nvme_class, &class_attr_add_subsystem);
	class_remove_file(nvme_class, &class_attr_remove_controller);
	class_remove_file(nvme_class, &class_attr_add_discover_server);

	class_destroy(nvme_class);
}
