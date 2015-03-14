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
#include <linux/miscdevice.h>

struct async_cmd_info {
	struct kthread_work work;
	struct kthread_worker *worker;
	struct request *req;
	__le32 result;
	int status;
	void *ctx;
};

struct sync_cmd_info {
	struct task_struct *task;
	u32 result;
	int status;
};

/*
 * An NVM Express queue.  Each device has at least two (one for admin
 * commands and one for I/O commands).
 */
struct nvme_common_queue {
	struct device *q_dmadev;
struct nvme_common_dev *dev;
char irqname[24];              /* nvme4294967295-65535\0 */
spinlock_t q_lock;             /* jpf: used in initiator.c */
struct nvme_command *sq_cmds;
struct nvme_completion *cqes;
	dma_addr_t sq_dma_addr;
	dma_addr_t cq_dma_addr;
	wait_queue_head_t sq_full;
	wait_queue_t sq_cong_wait;
	__u32 __iomem *q_db;
	__u16 q_depth;
	__u16 cq_vector;
	__u16 sq_head;
	__u16 sq_tail;
	__u16 cq_head;
	__u16 cq_tail;
	__u16 qid;
	__u8 cq_phase;
	__u8 cqe_seen;
	__u8 q_suspended;
	struct llist_node node;
	struct async_cmd_info cmdinfo;
	struct blk_mq_hw_ctx *hctx;
	unsigned long cmdid_data[];
};

typedef void (*nvme_completion_fn)(struct nvme_common_queue *, void *,
				struct nvme_completion *);

struct nvme_cmd_info {
	nvme_completion_fn fn;
	void *ctx;
	int aborted;
	struct nvme_common_queue *nvmeq;
};

struct nvme_common_dev {
	struct list_head node;
	struct nvme_common_queue **queues;
	struct request_queue *admin_q;
	struct blk_mq_tag_set tagset;
	struct blk_mq_tag_set admin_tagset;
	__le32 __iomem *dbs;
	struct dma_pool *prp_page_pool;  /*TODO/FIXME- PCIe? */
	struct dma_pool *prp_small_pool; /*TODO/FIXME- PCIe? */
	int instance;
	unsigned queue_count;
	unsigned online_queues;
	unsigned max_qid;
	int q_depth;
	__le32 db_stride;    /* TODO/FIXME- PCIe? */
	struct msix_entry *entry;
	struct list_head namespaces;
	struct kref kref;
	struct miscdevice miscdev;
	work_func_t reset_workfn;
	struct work_struct reset_work;
	char name[12];
	char serial[20];
	char model[40];
	char firmware_rev[8];
	__le32 max_hw_sectors;
	__le32 stripe_size;
	__le32 page_size;
	__le16 oncs;
	__le16 abort_limit;
	__u8   event_limit;
	__u8   vwc;
	__u8   initialized;
};

/*
 * An NVM Express namespace is equivalent to a SCSI LUN
 */
struct nvme_common_ns {
	struct list_head list;

	struct nvme_common_dev *dev;
	struct request_queue *queue;
	struct gendisk *disk;

	unsigned ns_id;
	int lba_shift;
	int ms;
	__le64 mode_select_num_blocks;
	__le32 mode_select_block_len;
};

/*
 * The nvme_iod describes the data in an I/O, including the list of PRP
 * entries.  You can't see it in this data structure because C doesn't let
 * me express that.  Use nvme_alloc_iod to ensure there's enough space
 * allocated to store the PRP list.
 */
struct nvme_iod {
	void *private;		/* For the use of the submitter of the I/O */
	int npages;		/* In the PRP list. 0 means small pool in use */
	int offset;		/* Of PRP list */
	int nents;		/* Used in scatterlist */
	int length;		/* Of data, in bytes */
	dma_addr_t first_dma;
	struct list_head node;
	struct scatterlist sg[0];
};

int nvme_ioctl(struct block_device *bdev, fmode_t mode, unsigned int cmd,
			unsigned long arg);
void nvme_common_free_iod(struct nvme_common_dev *dev, struct nvme_iod *iod);
int nvme_common_setup_prps(struct nvme_common_dev *dev, struct nvme_iod *iod,
			   int, gfp_t);
void nvme_common_submit_flush(struct nvme_common_queue *nvmeq,
			      struct nvme_common_ns *ns, int cmdid);
int nvme_common_submit_flush_data(struct nvme_common_queue *nvmeq,
				  struct nvme_common_ns *ns);
int nvme_common_queue_rq(struct blk_mq_hw_ctx *hctx,
			 const struct blk_mq_queue_data *bd);
int nvme_common_submit_admin_cmd(struct nvme_common_dev *dev,
				 struct nvme_command *cmd,
				u32 *result);
int nvme_common_identify(struct nvme_common_dev *dev,
		  unsigned nsid, unsigned cns, dma_addr_t dma_addr);
int nvme_common_get_features(struct nvme_common_dev *dev, unsigned fid,
			     unsigned nsid, dma_addr_t dma_addr, u32 *result);
int nvme_common_set_features(struct nvme_common_dev *dev, unsigned fid,
			     unsigned dword11, dma_addr_t dma_addr,
			     u32 *result);

int nvme_common_setup_io_queues(struct nvme_common_dev *dev);
int nvme_common_create_queue(struct nvme_common_queue *nvmeq, int qid);
struct nvme_common_queue *nvme_common_alloc_queue(struct nvme_common_dev *dev,
					   int qid, int depth, int vector);
struct nvme_common_ns *nvme_common_alloc_ns(struct nvme_common_dev *dev,
				     unsigned nsid,
				     struct nvme_id_ns *id,
				     struct nvme_lba_range_type *rt);
void nvme_common_submit_discard(struct nvme_common_queue *nvmeq,
				struct nvme_common_ns *ns,
				struct request *req, struct nvme_iod *iod);
int nvme_common_process_cq(struct nvme_common_queue *nvmeq);
int nvme_common_dev_add(struct nvme_common_dev *dev);

int nvme_common_init(void);
void nvme_common_exit(void);

/*
 ******** NVMe Generic SGL structs and values *********
 */

/* Figure 19, SGL Descriptor Types, NVMe 1.2 Spec */
#define NVME_SGL_DATA_BLOCK		0x0
#define NVME_SGL_BIT_BUCKET		0x1
#define NVME_SGL_SEGMENT		0x2
#define NVME_SGL_LAST_SEGMENT		0x3

/* TODO, FIXME: Need as part of the "Fabrics TP 002" proposal
 * SGL Tagged Segment and SGL Tagged Last Segment
 * #define NVME_SGL_TAGGED_SEGMENT	??
 * #define NVME_SGL_TAGGED_LAST_SEGMENT	??
 */

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

/*
 * TODO, FIXME: Need as part of the "Fabrics TP 002" proposal
 * SGL Tagged Segment and SGL Tagged Last Segment
 */

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
