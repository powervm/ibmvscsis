/*
 * nvme-common.h - NVM protocol paradigm independent of transport
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
 * functions that would define an NVMe device.  Initial definition
 * based on the 1.2 NVMe specification, released Nov 3, 2014.
 */

#ifndef _LINUX_NVME_COMMON_H
#define _LINUX_NVME_COMMON_H

#include <linux/types.h>
#include <linux/kthread.h>
#include <uapi/linux/nvme.h>
#include <linux/blk-mq.h>

struct async_cmd_info {
	struct kthread_work work;
	struct kthread_worker *worker;
	struct request *req;
	u32 result;
	int status;
	void *ctx;
};

/*
 * An NVM Express queue.  Each device has at least two (one for admin
 * commands and one for I/O commands).
 */
struct nvme_queue {
	struct llist_node node;
	struct device *q_dmadev;
	struct nvme_dev *dev;
	void *context;		/* for xport context */
	char irqname[24];	/* nvme4294967295-65535\0 */
	spinlock_t q_lock;
	struct nvme_command *sq_cmds;

	/* NOTE: This was defined as 'volatile' but checkpatch
	 * complains that this use is wrong.
	 */
	struct nvme_completion *cqes;
	dma_addr_t sq_dma_addr;
	dma_addr_t cq_dma_addr;
	u32 __iomem *q_db;
	u16 q_depth;
	s16 cq_vector;
	u16 sq_head;
	u16 sq_tail;
	u16 cq_head;
	u16 qid;
	u8 cq_phase;
	u8 cqe_seen;
	struct async_cmd_info cmdinfo;
	struct blk_mq_hw_ctx *hctx;
};

/*
 ******** NVMe Generic SGL structs and values *********
 */

/* Figure 19, SGL Descriptor Types, NVMe 1.2 Spec */
#define NVME_SGL_DATA_BLOCK		0x0
#define NVME_SGL_BIT_BUCKET		0x1
#define NVME_SGL_SEGMENT		0x2
#define NVME_SGL_LAST_SEGMENT		0x3
#define NVME_SGL_TAGGED_DATA_BLOCK	0xE
#define NVME_SGL_VENDOR_SPECIFIC	0xF

/*
 * Section 4.4 and Figure 17 of the NVMe 1.2 spec
 * does not limit the number of sgl segments.
 * The following limit is an arbitrary, practical
 * limit.  If there is the need to make it bigger,
 * this will be revisited.
 */
#define NVME_SGL_SEGMENT_MAXSIZE	16

/* Figure 20, SGL Data Block descriptor, NVMe 1.2 Spec */
struct nvme_sgl_datablock {
	__le64 addr;
	__le32 len;
	__u8   rsvd[3];
	__u8   sgl_identifier; /* contains a SGL Descriptor Type value */
};

/* Figure 21, SGL Bit Bucket descriptor, NVMe 1.2 Spec */
struct nvme_sgl_bitbucket {
	__le64 rsvd1;
	__le32 len;
	__u8   rsvd2[3];
	__u8   sgl_identifier; /* contains a SGL Descriptor Type value */
};

/* Figure 22, SGL Segment descriptor, NVMe 1.2 Spec */
struct nvme_sgl_segment {
	__le64 addr;
	__le32 len;
	__u8   rsvd[3];
	__u8   sgl_identifier; /* contains a SGL Descriptor Type value */
};

/* Figure 23, SGL Last Segment descriptor, NVMe 1.2 Spec */
struct nvme_sgl_lastsegment {
	__le64 addr;
	__le32 len;
	__u8   rsvd[3];
	__u8   sgl_identifier; /* contains a SGL Descriptor Type value */
};

/*
 * Figure XX, SGL Tagged Data Block descriptor, in
 * NVMe Org proposal "Fabrics TP 002".
 */
struct nvme_sgl_tagged_datablock {
	__le64 addr;   /* 64-bit starting memory address of data block */
	__le32 stag;   /* 32-bit tag associated with data block */
	__u8   len[3]; /* length in bytes of the data block- 16MB max  */
	__u8   sgl_identifier; /* contains a SGL Descriptor Type value */
};

/* Overall struct for section 4.4, Scatter Gather List, NVMe 1.2 spec  */
struct nvme_sgl_descriptor {
	union {
		struct nvme_sgl_datablock datablk;
		struct nvme_sgl_bitbucket bitbkt;
		struct nvme_sgl_segment seg;
		struct nvme_sgl_lastsegment lastseg;
		struct nvme_sgl_tagged_datablock tagblk;
	};
};

#endif /* _LINUX_NVME_COMMON_H */
