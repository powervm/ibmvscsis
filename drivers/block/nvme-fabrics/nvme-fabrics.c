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

#include <linux/nvme-fabrics/nvme-common.h>
#include <linux/nvme-fabrics/nvme-fabrics.h>
#include <linux/errno.h>

#define NVME_UNUSED(x) ((void)x)

/*TODO: Change this to fabric instance...*/
int instance;

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
 * Public function that adds an NVMe remote Subsystem
 *
 * So:
 *      -nvme_sysfs_init() registers files
 *      -do_add_subsystem() gets called
 *      -do_add_subsystem() calls this function (nvme_fabric_add_subsystem)
 *       this function, nvme_fabric_add_subsystem,
 *		establishes an AQ connection to the remote EP
 *		issues a login request to the EP AQ Ctrlr Login/Auth Service.
 *       This function is complete when it:
 *              for each subsystem in the form of:
 *			SubsystemName {CtlrNme/Addr/Port/fabricType, ...}
 *				- obtains an Administrative Connection
 *				- logs into the AQ on the remote EP
 *				- obtains I/O information
 *				- obtains the proper I/O Connections
 *				- sets up the drives properly
 *
 *      this function gets called either:
 *		directly from sysfs "do_add_subsystem" or
 *		indirectly from sysfs "do_add_endpoint"
 *	once for each subsystem.  This function, nvme_fabric_add_subsystem:
 *		initializes memory to hold a list of controller connections
 *		calls fops->nvme_connect_create_queue() which obtains an AQ
 *		Conn logs into the Administration Queue on the remote EP
 *		obtains I/O information
 *		obtains the proper I/O Connections
 *		sets up the drives properly
 *
 * @subsystem_name:
 * @ctrlr_name:
 * @fabric:  The type of fabric used for the connection.
 * @address: The fabric address used for the discovery connection.
 * @port:    The port on the machine used for the discovery connection.
 * Return Value:
 *      O for success,
 *	Any other value, error
 */
int nvme_fabric_add_subsystem(char *subsystem_name, char *ctrlr_name,
			      int fabric, char *address, int port)
{
	int			 ret;
	struct nvme_conn	*conn = NULL;

	pr_info("%s: %s()\n", __FILE__, __func__);

	conn = kzalloc(sizeof(*conn), GFP_KERNEL);
	if (!conn) {
		ret = -ENOMEM;
		goto err1;
	}

	/*Connect/Create an AQ Connection*/
	ret = host_fabric_ops->connect_create_queue(address, port, fabric,
			conn, CONN_AQ);
	if (ret) {
		pr_err("%s %s(): Error %d trying to create AQ\n",
		       __FILE__, __func__, ret);
		goto err2;
	}

	/*TODO: Log into EP via AQ conn using AQ login capsule and info
	 *	returned by discovery capsules
	 */
	if (ret) {
		pr_err("%s %s(): Error %d trying to log into AQ\n",
		       __FILE__, __func__, ret);
		goto err2;
	}
	/*...*/

	return ret;

err2:
	kfree(conn);
err1:
	return ret;
}
EXPORT_SYMBOL_GPL(nvme_fabric_add_subsystem);

/*
 * Public function that starts a ep discovery
 *
 * THIS NEEDS TO BE A CAPSULE FUNCTION
 *
 * TODO, WIP, FIXME: The current guts of this function
 *              is actually the start of the
 *              rdma-specific probe() call, so this code will get
 *              moved to nvme-fabric-rdma.c.
 *
 * So:
 *      -nvme_sysfs_init() registers files
 *      -do_add_endpoint() gets called
 *      -do_add_endpoint() calls this (generic) func (nvme_fabric_discovery)
 *       this function, nvme_fabric_discovery,  calls fops->probe() which
 *		calls the specific-probe/discovery code for the specific
 *		fabric. establishes a "discovery" connection to the remote EP
 *		issues a login request to the Discover Ctrlr Login/Auth
 *		service.
 *       This function is complete when it:
 *              obtains a Discovery Connection
 *              logs into the Discovery service on the remote EP
 *              obtains controller information
	 * SubsystemName CtlrNme/Addr/Port/fabricType Ctlr_name/Addr/Port ...
 *              for each subsystem in the form of:
 *			SubsystemName {CtlrNme/Addr/Port/fabricType, ...}
 *				- obtains an Administrative Connection
 *				- logs into the AQ on the remote EP
 *				- obtains I/O information
 *				- obtains the proper I/O Connections
 *				- sets up the drives properly
 *
 * @address: The fabric address used for the discovery connection.
 * @port:    The port on the machine used for the discovery connection.
 * @fabric:  The type of fabric used for the connection.
 * Return Value:
 *      O for success,
 *      Any other value, error
 */
int nvme_fabric_discovery(char *address, int port, int fabric_type)
{
	int	ret;
	char	subsystem_name[FABRIC_STRING_MAX] = "";
	char	ctrlr_name[FABRIC_STRING_MAX] = "";
	char	ctrlr_address[ADDRSIZE] = "";
	int	ctrlr_port = 0;
	void	*conn = NULL;

	pr_info("%s: %s()\n", __FILE__, __func__);

	/*Create entry for this remote EP and connect/create for Discovery */
	ret = host_fabric_ops->connect_create_queue(address, port, fabric_type,
			conn, CONN_DISCOVER);

	/* ret = host_fabric_ops->probe(address, port, fabric_type, conn);
	 */
	if (ret) {
		pr_err("%s %s(): fabric probe function returned %d\n",
		       __FILE__, __func__, ret);
		goto err1;
	}

	/*TODO: Log into remote EP via discovery conn returned from probe and
	 * using disc login capsule*/

	/*TODO: Use discovery conn returned by probe to send Disc req capsule
	 * asking for EP NVMe subsystem info - which returns list resembling:
	 * SubsystemName CtlrNme/Addr/Port/fabricType Ctlr_name/Addr/Port ...
	 * SubsystemName CtlrName/Addr/Port/fabricType
	 * SubsystemName CtlrName/Addr/Port/fabricType Ctlr_name/Addr/Port ...
	 */

	/* Disconnect the Discovery connection and clean up fabric memory */
	host_fabric_ops->disconnect(address, port, fabric_type);

	/*TODO: Iterate over the list of subsystems returned.
	 *	IF: a given subsystem has >1 ctrlr have to deal with multipath.
	 *
	 *	For each Ctlr returned, Connect/Create the AQs, IOQs, ...
	 */
	ret = nvme_fabric_add_subsystem(subsystem_name, ctrlr_name,
					fabric_type, ctrlr_address,
					ctrlr_port);
	if (ret) {
		pr_err("%s %s(): Error %d trying to create AQ\n",
		       __FILE__, __func__, ret);
		goto err2;
	}

	return ret;

err2:
	/*TODO: CAYTONCAYTON - what do we do if subsystem creation fails? */
err1:
	/*TODO: CAYTONCAYTON - what do we do if discovery connection fails? */
	return ret;
}
EXPORT_SYMBOL_GPL(nvme_fabric_discovery);

/*
 * Public function that starts a single ep's removal process
 *
 * THIS NEEDS TO BE A CAPSULE FUNCTION
 *
 * TODO, WIP, FIXME
 *
 * Return Value:
 *      O for success,
 *	Any other value, error
 */
int nvme_fabric_remove_endpoint(char *address, int port, int fabric)
{
	pr_info("%s: %s()\n", __FILE__, __func__);

	host_fabric_ops->disconnect(address, port, fabric);

	return 0;
}
EXPORT_SYMBOL_GPL(nvme_fabric_remove_endpoint);

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

	instance = 0;
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
	pr_info("%s: %s()\n", __FILE__, __func__);

	/*
	 * TODO, FIXME: Parameter in stop_destroy_queues should be a "ep"
	 */
	host_fabric_ops->stop_destroy_queues(0);
	host_fabric_ops = NULL;
	nvme_sysfs_exit();
	nvme_common_exit();
	return 0;
}
EXPORT_SYMBOL_GPL(nvme_fabric_unregister);
