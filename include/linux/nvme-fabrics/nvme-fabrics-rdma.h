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

#include <rdma/rdma_cm.h>
#include <rdma/ib_verbs.h>
#include <rdma/ib_fmr_pool.h>
#include <linux/list.h>
#include <linux/wait.h>
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

static inline char *addr2str(struct sockaddr_in *dst)
{
	static char     addr[64];

	sprintf(addr, "%pI4:%d", &dst->sin_addr.s_addr, dst->sin_port);
	return addr;
}

enum {
	STATE_NOT_CONNECTED = 0,
	STATE_CONNECTED,
	STATE_DISCONNECTING,
	STATE_DRAINING,
	STATE_CLOSING,
	STATE_ERROR = -1,
	STATE_TIMEDOUT = -2,
};

/*TODO: Can this be combined with ep? */
struct xport_conn {
	struct rdma_conn_param  conn_params;
	struct rdma_cm_id       *cm_id;
	struct ib_cq            *cq;
	struct ib_wc            wc;
};

/*
 * Points to an individual remote node
 * ALL ctrlrs on a node get a pointer to a common ep struct
 */
/*TODO: Does some of this need to be pulled into xport_conn?*/
struct ep {
	struct list_head	node;
	struct sockaddr_in      dst;
	int			instance;
	struct ib_device        *ib_dev;
	struct ib_pd            *pd;
	struct ib_mr            *mr;
	int			max_qp_init_rd_atom;
	int			max_qp_rd_atom;
	struct list_head	connections; /* all AQ + IOQs */
};

/*
 * struct that describes the fabric connection session the host
 * is using to communicate with the ep.
 */
struct nvme_fabric_conn {
	struct ep		*ep; /* Remote node info */
	struct xport_conn	xport_conn;
	int                     state;
	int                     stage;
	struct list_head        node;
	u16			port;
	u32                     session_id;
	u16                     rx_depth;
	u16                     tx_depth;
	struct rx_desc          *rx_desc_table;
	struct tx_desc          *tx_desc_table;
	struct completion       comp;
	wait_queue_head_t       sem;
	struct nvme_queue       *nvmeq;
};

#endif  /* _LINUX_NVME_FABRICS_RDMA_H */
