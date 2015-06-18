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
#include <linux/list.h>
#include <linux/wait.h>

#define NVME_UNUSED(x)		((void)x)

/*TODO: Figure out all these defines below */
/*SWAG #1: Current definitions are based on the POC */
/*SWAG #2: It may be that not all of these are used/needed.*/
/*-------------------------------------------------------------------------*/

#define DEFAULT_HOSTNAME	"org.nvmeexpress.rdmahost"
#define MAX_INLINE_DATA		0
#define NVME_RDMA_POLLSIZE	1

#define DISCOVER_POOL_DEPTH	0
#define DISCOVER_SQ_SIZE	1
#define DISCOVER_RQ_SIZE	1
#define MAX_DISCOVER_SEND_WR	DISCOVER_SQ_SIZE
#define MAX_DISCOVER_RECV_WR	DISCOVER_RQ_SIZE
#define MAX_DISCOVER_SEND_SGE	1
#define MAX_DISCOVER_RECV_SGE	1

#define AQ_POOL_DEPTH		0
#define AQ_SQ_SIZE		1
#define AQ_RQ_SIZE		1
#define MAX_AQ_SEND_WR		AQ_SQ_SIZE
#define MAX_AQ_RECV_WR		AQ_RQ_SIZE
#define MAX_AQ_SEND_SGE		1
#define MAX_AQ_RECV_SGE		1

#define IOQ_POOL_DEPTH		0
#define IOQ_SQ_SIZE		40
#define IOQ_RQ_SIZE		40
#define MAX_IOQ_SEND_WR		128
#define MAX_IOQ_RECV_WR		128
#define MAX_IOQ_SEND_SGE	3
#define MAX_IOQ_RECV_SGE	1
/*-------------------------------------------------------------------------*/

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

/*CAYTONCAYTON: Can this be combined with rdma_ctrl? */
struct xport_conn {
	struct rdma_conn_param	 conn_params;
	struct rdma_cm_id       *cm_id;
	struct ib_cq            *cq;
	struct ib_wc		 wc;
};

/*
 * Points to an individual remote node
 * ALL queues on a ctrl get a pointer to a common ctrl struct
 */
struct rdma_ctrl {
	struct list_head	 node;
	char			 subsys_name[NVME_FABRIC_IQN_MAXLEN];
	__u16			 cntlid;
	__u8			 uuid[HNSID_LEN];
	__u8			 uuid_len;
	int			 instance;
	struct ib_device        *ib_dev;
	struct ib_pd            *pd;
	struct ib_mr            *mr;
	int			 max_qp_init_rd_atom;
	int			 max_qp_rd_atom;
	struct list_head	 connections; /* ctrl AQ + IOQs */
};

/*
 * struct that describes a fabric connection session the host
 * is using to communicate with an individual queue (IOQ/AQ/DQ) .
 */
struct nvme_rdma_conn {
	struct list_head		 node;
	struct rdma_ctrl		*rdma_ctrl; /* main ctrl struct */
	struct xport_conn		 xport_conn;
	int				 state;
	spinlock_t			 lock;
	struct list_head		 rx_desc_pool;
	int				 stage;
	int				 rx_depth;
	int				 tx_depth;
	struct sockaddr_in		 dst;
	u32				 session_id;
	struct completion		 comp;
	wait_queue_head_t		 sem;
	struct nvme_common_queue	*nvmeq;
};

struct xport_sge {
	struct ib_sge	sgl;
};

struct rx_desc {
	struct list_head	node;
	struct nvme_rdma_conn	*fabric_conn;
	struct xport_sge	 xport[MAX_IOQ_RECV_SGE];
};

struct tx_desc {
	struct nvme_rdma_conn	*fabric_conn;
	struct xport_sge	 xport[MAX_IOQ_SEND_SGE];
};

#endif  /* _LINUX_NVME_FABRICS_RDMA_H */
