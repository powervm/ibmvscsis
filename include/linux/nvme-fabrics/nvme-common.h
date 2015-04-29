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

/*
 * From the NVMe spec, this is the maximum IO queue
 * number that can be used to label an IO queue.
 */
#define NVME_MAX_QUEUE_NUM		65534

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

/* Figure 18, SGL Descriptor Format, NVMe 1.2 Spec */
struct sgl_identifier {

	/*
	 * if cleared to 0, data associated w/the sgl is transferred
	 * through memory. If set to 1, sgl dta is transferred via
	 * command and response capsules.
	 */
	__u8 mem_cap  :1;

	/*
	 * specification says "the zero field shall have the value 0h".
	 */
	__u8 zero     :3;

	/* value set is from Figure 19 in the NVMe 1.2 spec */
	__u8 sgl_type :4;
};

/*
 * The SGL fields can be pretty different depending if CC.KEY
 * is set or not.  Figure 20-23, NVMe 1.2 spec.
 */
struct sgl_len_key {
	union {
		struct {
			__u8 length[4];
			__u8  rsvd[3];
		} cckey_0;
		struct {
			__u8 length[3];
			__u8 key[4];
		} cckey_1;
	};
};

/* Figure 20, SGL Data Block descriptor, NVMe 1.2 Spec */
struct nvme_common_sgl_dblk {
	__le64 addr;
	struct sgl_len_key len_key;
	struct sgl_identifier sgl_id;
};


/* Figure 21, SGL Bit Bucket descriptor, NVMe 1.2 Spec */
struct nvme_common_sgl_bbkt {
	__le64 rsvd1;
	__le32 len;
	__u8   rsvd2[3];
	struct sgl_identifier sgl_id;
};

/* Figure 22, SGL Segment descriptor, NVMe 1.2 Spec */
struct nvme_common_sgl_seg {
	__le64 addr;
	struct sgl_len_key len_key;
	struct sgl_identifier sgl_id;
};

/* Figure 23, SGL Last Segment descriptor, NVMe 1.2 Spec */
struct nvme_common_sgl_lseg {
	__le64 addr;
	struct sgl_len_key len_key;
	struct sgl_identifier sgl_id;
};

/* Overall struct for section 4.4, Scatter Gather List, NVMe 1.2 spec  */
struct nvme_common_sgl_desc {
	union {
		struct nvme_common_sgl_dblk datablk;
		struct nvme_common_sgl_bbkt bitbkt;
		struct nvme_common_sgl_seg seg;
		struct nvme_common_sgl_lseg lastseg;
	};
};

/*
 ******** End of Generic SGL structs and values *********
 */

struct nvme_base_cmd {
	__u8			opcode;
	__u8			flags;
	__u16			command_id;
	__le32			nsid;
	__le32			cdw2[2];
	__le64			metadata;
	__le64			prp1;
	__le64			prp2;
	__le32			cdw10[6];
};

struct nvme_common_sgl_cmd {
	__u8				opcode;
	__u8				flags;
	__u16				command_id;
	__le32				nsid;
	__le32				cdw2[2];
	__le64				metadata;
	struct nvme_common_sgl_desc	sgl1;
	__le32				cdw10[6];
};

struct nvme_common_rw_cmd {
	__u8			opcode;
	__u8			flags;
	__u16			command_id;
	__le32			nsid;
	__u64			rsvd2;
	__le64			metadata;
	__le64			prp1;
	__le64			prp2;
	__le64			slba;
	__le16			length;
	__le16			control;
	__le32			dsmgmt;
	__le32			reftag;
	__le16			apptag;
	__le16			appmask;
};

struct nvme_common_identify {
	__u8			opcode;
	__u8			flags;
	__u16			command_id;
	__le32			nsid;
	__u64			rsvd2[2];
	__le64			prp1;
	__le64			prp2;
	__le32			cns;
	__u32			rsvd11[5];
};

struct nvme_common_features {
	__u8			opcode;
	__u8			flags;
	__u16			command_id;
	__le32			nsid;
	__u64			rsvd2[2];
	__le64			prp1;
	__le64			prp2;
	__le32			fid;
	__le32			dword11;
	__u32			rsvd12[4];
};

struct nvme_common_create_cq {
	__u8			opcode;
	__u8			flags;
	__u16			command_id;
	__u32			rsvd1[5];
	__le64			prp1;
	__u64			rsvd8;
	__le16			cqid;
	__le16			qsize;

	/*
	 * bits 15:04 are rsvd.
	 * bit 3 is icd, in-capsule data. If
	 *	this value is >= 8 (icd == 1),
	 *      then capsules could contain in-capsule data.
	 *      if value is <= 7 (icd == 0), then
	 *      response capsules cannot contain in-capsule data.
	 * bit 2 is rsvd
	 * bit 1 is ien bit (interrupts enabled)
	 * bit 0 is pc bit (physically contig.)
	 */
	__le16			icd_en_flags;

	__le16			irq_vector;
	__u32			rsvd12[4];
};

struct nvme_common_create_sq {
	__u8			opcode;
	__u8			flags;
	__u16			command_id;
	__u32			rsvd1[5];
	__le64			prp1;
	__u64			rsvd8;
	__le16			sqid;
	__le16			qsize;

	/*
	 * bits 15:04 are rsvd.
	 * bit 3 is icd, in-capsule data. If
	 *	this value is >= 8 (icd == 1),
	 *      then cmd capsules could contain in-capsule data.
	 *      if value is <= 7 (icd == 0), then
	 *      cmd capsules cannot contain in-capsule data.
	 * bits 2:1 is the qprio field.
	 * bit 0 is pc bit (physically contig.)
	 */
	__le16			icd_qpc_flags;
	__le16			cqid;
	__u32			rsvd12[4];
};

struct nvme_common_delete_queue {
	__u8			opcode;
	__u8			flags;
	__u16			command_id;
	__u32			rsvd1[9];
	__le16			qid;
	__u16			rsvd10;
	__u32			rsvd11[5];
};

struct nvme_common_abort_cmd {
	__u8			opcode;
	__u8			flags;
	__u16			command_id;
	__u32			rsvd1[9];
	__le16			sqid;
	__u16			cid;
	__u32			rsvd11[5];
};

struct nvme_common_download_firmware {
	__u8			opcode;
	__u8			flags;
	__u16			command_id;
	__u32			rsvd1[5];
	__le64			prp1;
	__le64			prp2;
	__le32			numd;
	__le32			offset;
	__u32			rsvd12[4];
};

struct nvme_common_format_cmd {
	__u8			opcode;
	__u8			flags;
	__u16			command_id;
	__le32			nsid;
	__u64			rsvd2[4];
	__le32			cdw10;
	__u32			rsvd11[5];
};

struct nvme_common_cmd {
	union {
		struct nvme_base_cmd base;
		struct nvme_common_sgl_desc base_sgl;
		struct nvme_common_rw_cmd rw;
		struct nvme_common_identify identify;
		struct nvme_common_features features;
		struct nvme_common_create_cq create_cq;
		struct nvme_common_create_sq create_sq;
		struct nvme_common_delete_queue delete_queue;
		struct nvme_common_download_firmware dlfw;
		struct nvme_common_format_cmd format;
		struct nvme_common_abort_cmd abort;
	};
};

struct nvme_common_completion {
	__le32	result;		/* Used by admin commands to return data */
	__u32	rsvd;
	__le16	sq_head;	/* how much of this queue may be reclaimed */
	__le16	sq_id;		/* sub queue that generated this entry */
	__u16	command_id;	/* of the command which completed */
	__le16	status;		/* did the command fail, and if so, why? */
};

struct nvme_common_id_ctrl {
	__le16			vid;
	__le16			ssvid;
	char			sn[20];
	char			mn[40];
	char			fr[8];
	__u8			rab;
	__u8			ieee[3];
	__u8			mic;
	__u8			mdts;
	__u16			cntlid;
	__u32			ver;
	__u8			rsvd84[172];
	__le16			oacs;
	__u8			acl;
	__u8			aerl;
	__u8			frmw;
	__u8			lpa;
	__u8			elpe;
	__u8			npss;
	__u8			avscc;
	__u8			apsta;
	__le16			wctemp;
	__le16			cctemp;
	__u8			rsvd270[242];
	__u8			sqes;
	__u8			cqes;
	__u8			rsvd514[2];
	__le32			nn;
	__le16			oncs;
	__le16			fuses;
	__u8			fna;
	__u8			vwc;
	__le16			awun;
	__le16			awupf;
	__u8			nvscc;
	__u8			rsvd531;
	__le16			acwu;
	__u8			rsvd534[2];
	__le32			sgls;
	__u8			rsvd540[1508];
	struct nvme_id_power_state	psd[32];
	__u8			vs[1024];
};

struct nvme_common_lbaf {
	__le16			ms;
	__u8			ds;
	__u8			rp;
};

struct nvme_common_id_ns {
	__le64			nsze;
	__le64			ncap;
	__le64			nuse;
	__u8			nsfeat;
	__u8			nlbaf;
	__u8			flbas;
	__u8			mc;
	__u8			dpc;
	__u8			dps;
	__u8			nmic;
	__u8			rescap;
	__u8			fpi;
	__u8			rsvd33;
	__le16			nawun;
	__le16			nawupf;
	__le16			nacwu;
	__u8			rsvd40[80];
	__u8			eui64[8];
	struct nvme_common_lbaf	lbaf[16];
	__u8			rsvd192[192];
	__u8			vs[3712];
};

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
 * The nvme_common_iod describes the data in an I/O, including the list of PRP
 * entries.  You can't see it in this data structure because C doesn't let
 * me express that.  Use nvme_alloc_iod to ensure there's enough space
 * allocated to store the PRP list.
 */
struct nvme_common_iod {
	void *private;		/* For the use of the submitter of the I/O */
	int npages;		/* In PRP list. 0 means small pool in use */
	int offset;		/* Of PRP list */
	int nents;		/* Used in scatterlist */
	int length;		/* Of data, in bytes */
	dma_addr_t first_dma;
	struct list_head node;
	struct scatterlist sg[0];
};

int nvme_common_ioctl(struct block_device *bdev, fmode_t mode,
		      unsigned int cmd,
		      unsigned long arg);
void nvme_common_free_iod(struct nvme_common_dev *dev,
			  struct nvme_common_iod *iod);
int nvme_common_setup_prps(struct nvme_common_dev *dev,
			   struct nvme_common_iod *iod,
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
				struct request *req,
				struct nvme_common_iod *iod);
int nvme_common_process_cq(struct nvme_common_queue *nvmeq);
int nvme_common_dev_add(struct nvme_common_dev *dev);

int nvme_common_init(void);
void nvme_common_exit(void);

#endif /* _LINUX_NVME_COMMON_H */
