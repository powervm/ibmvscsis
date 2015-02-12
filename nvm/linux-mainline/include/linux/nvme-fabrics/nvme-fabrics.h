/*
 * nvme-fabrics.h - NVM protocol paradigm independent of transport
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
 * This file is used to specify all the common data structures and
 * functions that would implement a fabric for a NVMe device.
 */

#ifndef _LINUX_NVME_FABRICS_H
#define _LINUX_NVME_FABRICS_H

#include <linux/bio.h>
#include <linux/nvme-fabrics/nvme-common.h>

struct cmd_info {
	__u16 sqid;
	__u16 sqt;
	__u16 cqid;
	__u16 cqh;
};

struct nvme_cmd_capsule {
	struct cmd_info capsule_cmd;
	struct nvme_common_command cmd;

	/*
	 * README: this points to sgl entry for a driver using
	 * sgl lists, or an address that points to data,
	 * or...
	 */
	void *data_contents;
};

struct nvme_rsp_capsule {
	struct cmd_info capsule_cmd;
	struct nvme_completion cpl;

	/*
	 * README: this points to sgl entry for a driver using
	 * sgl lists, or an address that points to data,
	 * or...
	 */
	void *data_contents;
};

/*
 *  README: Theory is we want to encapsulate the NVMe specific queues into
 *  more abstract capsule queues for fabrics.  Thus we will have, say, 1024
 *  (for example), deep nvme_cmd_capsule and nvme_response_capsule queues.
 */
struct nvme_fabric_cmd_queue {
	__le16 capsule_entries;
	__le16 sq_cmd_head;
	__le16 sq_cmd_tail;
	struct nvme_cmd_capsule *cmd_capsules;
};

struct nvme_fabric_rsp_queue {
	__le16 capsule_entries;
	__le16 cp_rsp_head;
	__le16 cp_rsp_tail;
	struct nvme_response_capsule *rsp_capsules;
};

/*
 * ????????????????
 */
struct nvme_fabric {
	struct nvme_fabric_host_operations *fops;
	void *TODO;
};

/*
 * struct that describes the fabric connection session the host
 * is using to communicate with the target.
 *
 * TODO: Needs to be completed and filled out
 */
struct connection {

	/*
	 * set when the target accepts our login request
	 */
	__u32 session_id;

	/*
	 * holds the size of the send queue
	 */
	__u16  send_depth;

	/*
	 * holds the size of the receive queue
	 */
	__u16 receive_depth;
	/*
	   according to the demo code, there were specific structs
	   used to establish the specific fabric connection that
	   were embedded in the more generic connection struct
	*/
	void *xport_connection;

	/*
	 * More to fill out...
	 */
	void *TODO;
};

/*
 * TODO:  Think this will be the fundamental device type for
 * all NVMe fabric device drivers??
 *
 * For each device instance, it can represent 1 controller discovered.
 */
struct nvme_fabric_dev {
	__le16 num_capsule_pairs;
	struct nvme_fabric_cmd_queue *cmd_capsule;
	struct nvme_fabric_rsp_queue *rsp_capsule;
	char name[12];  /* name can be name of controller */
	char serial[20];
	char model[40];
	char firmware_rev[8];
	struct nvme_fabric *fabric;
	struct connection *conn;
	__le64 fabric_address;
	__le16 num_namespaces;
};

/*
 *
 * Some notes:
 *      *If you want the device to have an ioctl associated with
 *       it, define a block_device_operations with it.  This
 *       would not be a core function of
 *       nvme_fabric_host_operations.
 *
 */
struct nvme_fabric_host_operations {
	struct module *owner;

	/*
	 * Function that...
	 *
	 * @capsule:
	 * @pbio:
	 * @sq:
	 * @dbell_value:
	 *
	 * Return Value:
	 *      0 for success,
	 *      Any other value, error
	 */
	int (*new_capsule)(struct nvme_cmd_capsule *capsule,
		struct bio *pbio, struct nvme_common_command cmd,
		__u32 dbell_value);

	/*
	 * Function that...??
	 *
	 * @capsule:
	 *
	 * Figure there is no need for a return value because
	 * once it's sent, it's sent...
	 */
	void (*send_capsule)(struct nvme_cmd_capsule *capsule);

	/*
	 *  Function that takes the specific transport context being
	 *  written for this specific driver plus an NVMe admin command
	 *  and preps the contents (like for example, package
	 *  the data into an NVMe capsule) for an NVMe admin cmd
	 *  submission that will then be sent by this function over the
	 *  fabric.
	 *
	 *  @dev:    Current nvme_fabric_dev being operated on.
	 *  @cmd:    The NVMe Admin command that will be used to prepare
	 *           the command transmission over the fabric.
	 *  @result: pointer to DW0 of the NVMe completion packet, which
	 *           data contents are specific to the admin command sent.
	 *
	 *  Return Value:
	 *      0 for Success
	 *      Any other value, error
	 *
	 *  Caveats:
	 *      if the function does not return 0, result parameter
	 *      must be set to 0.
	 *
	 *  Notes:
	 *      This function is based on nvmerp_submit_aq_cmd()
	 *      of the demo.
	 */
	int (*prepsend_admin_cmd)(struct nvme_fabric_dev *dev,
				  struct nvme_common_command *cmd,
				  __u32 *result);

	/*
	 * Function that takes the specific fabric transport and
	 * an NVMe I/O command and packages the contents (in say,
	 * an NVME capsule for example) that will then be sent
	 * over the fabric by this function.
	 *
	 * @dev:	The current nvme_fabric device being operated on.
	 * @nvmeq:	TODO: Not sure 'why' other than it was needed in
	 *		the demo.
	 * @cmd:	NVMe I/O command to be sent over the fabric to
	 *		the target.
	 * @len:	The leftover byte count after subtracting from
	 *		a base quantity byte size (like 4k for example).
	 *
	 * Return Value:
	 *      0 for success
	 *      Any other value, error
	 *
	 * Notes:
	 *	This function is based on on nvmerp_submit_io_cmd()
	 *      of the demo.
	 *
	 *      TODO: I think this function needs a 'result' like
	 *      prepsend_admin_cmd().  Why did the demo return
	 *      an nvme_completion status for nvmerp_submit_aq_cmd()
	 *      but not nvmerp_submit_io_cmd()?
	 */
	int (*prepsend_io_cmd)(struct nvme_fabric_dev *dev,
			struct nvme_queue *nvmeq,
			struct nvme_command *cmd,
			__u32 len);

	/*
	 * The NVMe fabric discover function responsible for
	 * discovery on all fabric paths.  For each discovery,
	 * the name of a controller and the associated fabric
	 * ports will be returned.
	 */
	/*
	    "discover sequence (generic, nvme-free)"
		-"go and get # of controllers on the fabric"
		- track # of controllers discovered
		-structure that keeps # of ns for each dev,
		 name of each controller, fabric address for each controller
	   TODO
	*/
	int (*probe)(struct nvme_fabric_dev *dev);

	/*
	 * Function that establishes a fabric-specific connection with
	 * the target, as well as create the send work queue and the receive
	 * work queue to establish a queue pair for the host to use
	 * to communicate NVMe capsules with the target.
	 *
	 *  @conn: The connection session description construct
	 *         of the host fabric device.
	 *
	 *  Return Value:
	 *      0 for success,
	 *      any other value, error.
	 *
	 *  Notes:
	 *      This function is based on connect_queue() from
	 *      the demo.  From Dave, this function will
	 *      actually make a connection first, then create
	 *      the queues.
	 */
	int (*connect_create_queues)(struct connection *conn);

	/*
	 * Function that stops processing queues and destroys
	 * the queue resources.
	 *
	 * @conn: The connection session resources construct
	 *        of the host fabric device.
	 *
	 * Notes:
	 *     This function is base on disconnect_queue() from
	 *     the demo.
	 */
	void (*stop_destroy_queues)(struct connection *conn);
};

void nvme_new_capsule(struct nvme_fabric *fabric,
		      struct nvme_cmd_capsule *capsule,
		      struct bio *pbio,
		      struct nvme_common_command cmd,
		      __u32 dbell_value);

int nvme_register_fabric(int TODO,
			 struct nvme_fabric_host_operations *new_fabric);

int nvme_unregister_fabric(int TODO);
#endif  /* _LINUX_NVME_FABRICS_H */
