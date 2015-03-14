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
 * This file is used to specify all the common data structures and
 * functions that would implement a RDMA fabric for a NVMe device.
 */

#ifndef _LINUX_NVME_FABRICS_RDMA_H
#define _LINUX_NVME_FABRICS_RDMA_H

#define NVME_UNUSED(x)		((void)x)
#define TODO			NULL

/* This is used for both Admin submission and completion queue depth;
 * This can be broken out to separate sub/cpl sizes if needed
 */
#define NVME_RDMA_AQ_DEPTH	64

/* Max NVMe IO queue size, loosely based on CAP.MQES register definition
 * of the NVMe spec.
 */
#define NVME_RDMA_IOQ_DEPTH	4096

#define SQ_SIZE(depth)          (depth * sizeof(struct nvme_command))
#define CQ_SIZE(depth)          (depth * sizeof(struct nvme_completion))
#define NVMF_CLASS		"nvme_rdma"

/*
 * struct that describes the fabric connection session the host
 * is using to communicate with the target.
 *
 * TODO: Needs to be completed and filled out
 */
struct nvme_rdma_connection {

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
	 * according to the demo code, there were specific structs
	 * used to establish the specific fabric connection that
	 * were embedded in the more generic connection struct
	 */
	void *xport_connection;

	/*
	 * More to fill out...
	 */
	void *WIP;
};

#endif  /* _LINUX_NVME_FABRICS_RDMA_H */
