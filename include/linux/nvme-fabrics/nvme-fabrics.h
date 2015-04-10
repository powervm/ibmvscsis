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
#include <linux/moduleparam.h>

/*
 * ctype values for a command capsule header. Defines
 * what type of 64-byte command is being sent.  Part
 * of the Command Capsule Format being proposed in
 * NVMe Org proposal "Fabrics TP 002".
 */
#define CTYPE_NVME_CMD		0
#define CTYPE_FABRIC_AGNOSTIC	1
#define CTYPE_FABRIC_SPECIFIC	2

#define FABRIC_STRING_MAX	50 /* CAYTONCAYTON - this should be 1K! */
#define ADDRSIZE		128


enum nvme_fabric_type {
	NVME_FABRIC_PCIE = 0,	/* PCIe Fabric */
	NVME_FABRIC_RDMA = 1,	/* RDMA Fabrics; IBA, iWARP, ROCE, ... */
	/* Future NVMe Fabrics */
};

enum nvme_conn_stage {
	CONN_DISCOVER = 0,
	CONN_AQ,
	CONN_IOQ,
	CONN_FULLY_INIT,
	CONN_ERROR,
};

extern char hostname[FABRIC_STRING_MAX];

extern int                              instance;

struct aq {
	/* struct *nvme_aq_stuff */
	void	 *fabric_aq_conn;
};

struct ioq {
	/* This should be a "list" of IOQs
	  struct *nvme_ioq_stuff
	*/
	void	 *fabric_ioq_conn;
};

struct nvme_conn {
	struct list_head	node; /*List of all active connections*/
	int			state; /*CONN_PROBE, CONN_AQ, CONN_IOQ, ... */
	char			*ctrlrname[FABRIC_STRING_MAX];
	char			*subsysname[FABRIC_STRING_MAX];
	char			*Address[ADDRSIZE];
	int			port;
	struct aq		*aq;
	struct ioq		*ioq;
};

/* TODO: Add sizeof BUILD_BUG_ON() checks to these struct sizes
 * when space is more fleshed out enough to put a size on these things.
 */

/*
 * This is the Queue Information field which is part
 * of the Command Capsule Format being proposed in
 * NVMe Org proposal "Fabrics TP 002".
 */
struct capsule_queue_info {
	union {
		struct {
			__le16 sqidf;
			__le16 sqt;
			__le16 cqidf;
			__le16 cqh;

		} qinfo;
		__le64	qw;
	};
};

/*
 * This is the Capsule Header field which is part
 * of the Command Capsule Format being proposed in
 * NVMe Org proposal "Fabrics TP 002".
 *
 * Note the bits mean something different if the capsule
 * is a command capsule or response capsule.
 */
struct capsule_header {
	union {
		struct {
			__u8 ctype :3;
			__u8 cattr :5;
			__u8 rsvd;

		} cmd_hdr;
		struct {
			__le16 cqidf;
		} cmd_resp;
	};
};

/*
 * This is the Command Capsule Format being proposed in
 * NVMe Org proposal "Fabrics TP 002".
 */
struct nvme_cmd_capsule {

	/* normal 64-byte nvme cmd submission */
	struct nvme_common_command sub;

	/*
	 * specifies if the 'sub' parameter in the command
	 * information area of the capsule is:
	 *
	 * 1. an NVMe command
	 * 2. a specific fabric-centric command
	 * 3. a fabric agnostic command.
	 */
	struct capsule_header hdr;

	/* contains sub/cpl queue doorbell info */
	struct capsule_queue_info info;

	/* This is specific to the NVMe command.
	 * If DPTR of NVMe cmd is SGL1
	 * and
	 * bit 0 of byte 15 of DPTR/SGL1 is 0, then
	 * this entry will be set to NULL.  Otherwise,
	 * if DPTR of NVMe cmd is SGL1
	 * and
	 * bit 0 of byte 15 DPTR/SGL1 is 1, then
	 * it will point to one of the SGL struct types
	 * defined in Figure 19 of the NVMe 1.2 spec.
	 */
	void *data_section;
};

/*
 * This is the Command Capsule Format being proposed in
 * NVMe Org proposal "Fabrics TP 002".
 *
 * nvme_rsp_capsule is the response to a
 * nvme_cmd_capsule packet sent.
 */
struct nvme_rsp_capsule {

	/* normal 16-byte nvme completion packet */
	struct nvme_completion resp;

	/*
	 * this field will specify the completion queue
	 * associated with the completion.
	 */
	struct capsule_header hdr;

	/* contains sub/cpl queue doorbell info */
	struct capsule_queue_info info;

	/*
	 * TODO: I believe the same rules apply here as for
	 * '*data_section' in nvme_cmd_capsule struct
	 * but not sure??
	 */
	void *data_section;
};

/*
 * This struct would be used to embed more than 1 sgl descriptors
 * with a data block and have that pointed by
 * '*data_section' in the nvme_*_capsule structs.
 *
 * For capsule commands that just use SGL1 in the actual
 * NVMe submission command, the '*data_section' variable
 * could still be used without the need for a
 * nvme_sgl_data_capsule struct defined below.
 */
struct nvme_sgl_data_capsule {
	struct nvme_sgl_descriptor sgllist[NVME_SGL_SEGMENT_MAXSIZE];
	void *data;
};

/*
 * TODO:  Think this will be the fundamental device type for
 * all NVMe fabric device drivers??
 *
 * For each device instance, it can represent 1 controller discovered.
 */
struct nvme_fabric_dev {

	/* generic, pci-e free, nvme implementation stuff */
	struct nvme_common_dev *dev;

	/* needed for default fabric address to describe host */
	__le64 fabric_address;

	/*
	 * there needs to be a way to go between the local (host)
	 * nvme device and the remote (ep) nvme device.  Host
	 * will think it's the nvme device but this tells it
	 * 'to go here instead'.
	 */
	void *xport_context;

	/*
	 * API fabric-specific drivers (RDMA, INFINIBAND, etc)
	 * must implement
	 */
	struct nvme_fabric_host_operations *fops;

	/* any fabric, implementation specific structs to associate */
	void *fabric_private;
};

struct nvme_fabric_host_operations {
	struct module *owner;

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
	 * @queue_num:	Which nvme_queue nvmeq to use in dev.
	 *\		TODO: Not sure 'why' nvmeq was used here
	 *		other than it was needed in
	 *		the demo.
	 * @cmd:	NVMe I/O command to be sent over the fabric to
	 *		the ep.
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
	 */
	int (*prepsend_io_cmd)(struct nvme_fabric_dev *dev,
			       __u16 queue_num,
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
	*/
	int (*probe)(char *address, int port,
		     int fabric, struct nvme_conn *disc_conn);

	/*TODO*/
	void (*disconnect)(char *address, int port, int fabric);

	/*
	 * Function that establishes a fabric-specific connection with
	 * the ep, as well as create the send work queue and the receive
	 * work queue to establish a queue pair for the host to use
	 * to communicate NVMe capsules with the ep.
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
	int (*connect_create_queue)(char *addr, int port, int fabric,
				    struct nvme_conn *nvme_conn, int stage);

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
	void (*stop_destroy_queues)(int TODO);
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

int nvme_fabric_register(char *nvme_class_name,
			 struct nvme_fabric_host_operations *new_fabric);
int nvme_fabric_unregister(int TODO);
int nvme_fabric_discovery(char *address, int port, int fabric);
int nvme_fabric_add_subsystem(char *subsystem_name, char *ctrlr_name,
			      int fabric, char *address, int port);
int nvme_fabric_remove_endpoint(char *address, int port, int fabric);

/******** nvme-sysfs.c function prototype definitions *********/

int nvme_sysfs_init(char *nvme_class_name);
void nvme_sysfs_exit(void);
ssize_t nvme_sysfs_do_add_endpoint(struct class *class,
				   struct class_attribute *attr,
				   const char *buf, size_t count);
ssize_t nvme_sysfs_do_add_subsystem(struct class *class,
				    struct class_attribute *attr,
				    const char *buf, size_t count);
ssize_t nvme_sysfs_show_add_endpoint(struct class *class,
				     struct class_attribute *attr,
				     char *buf);
ssize_t nvme_sysfs_do_remove_endpoint(struct class *class,
				      struct class_attribute *attr,
				      const char *buf, size_t count);
ssize_t nvme_sysfs_do_set_hostname(struct class *class,
				   struct class_attribute *attr,
				   const char *buf, size_t count);
ssize_t nvme_sysfs_show_set_hostname(struct class *class,
				     struct class_attribute *attr,
				     char *buf);
ssize_t nvme_sysfs_show_add_subsystem(struct class *class,
				      struct class_attribute *attr,
				      char *buf);

#endif  /* _LINUX_NVME_FABRICS_H */
