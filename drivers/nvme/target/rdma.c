/*
 * Copyright (c) 2015 HGST, a Western Digital Company.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/atomic.h>
#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/nvme.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/wait.h>
#include <asm/unaligned.h>

#include <rdma/ib_verbs.h>
#include <rdma/rdma_cm.h>
#include <linux/nvme-rdma.h>

#include "nvmet.h"
#include "rw.h"

struct nvmet_rdma_cmd {
	struct ib_sge		sge[2];
	struct ib_cqe		cqe;
	struct ib_recv_wr	wr;
	struct scatterlist	inline_sg;
	void			*inline_data;
	struct nvme_command     *nvme_cmd;
	struct nvmet_rdma_queue	*queue;
};

struct nvmet_rdma_rsp {
	struct ib_sge		send_sge;
	struct ib_cqe		send_cqe;
	struct ib_send_wr	send_wr;

	struct nvmet_rdma_cmd	*cmd;
	struct nvmet_rdma_queue	*queue;

	struct ib_cqe		read_cqe;
	struct rdma_rw_ctx	rw;

	struct nvmet_req	req;

	u8			n_rdma;
	u32			invalidate_rkey;

	struct list_head	wait_list;
	struct list_head	free_list;
};

enum nvmet_rdma_queue_state {
	NVMET_RDMA_Q_CONNECTING,
	NVMET_RDMA_Q_LIVE,
	NVMET_RDMA_Q_DISCONNECTING,
};

struct nvmet_rdma_queue {
	struct rdma_cm_id	*cm_id;
	struct ib_cq		*cq;
	atomic_t		sq_wr_avail;
	struct nvmet_rdma_device *dev;
	spinlock_t		state_lock;
	enum nvmet_rdma_queue_state state;
	struct nvmet_cq		nvme_cq;
	struct nvmet_sq		nvme_sq;

	struct nvmet_rdma_rsp	*rsps;
	struct list_head	free_rsps;
	spinlock_t		rsps_lock;
	struct nvmet_rdma_cmd	*cmds;

	struct work_struct	release_work;
	struct list_head	rsp_wait_list;
	struct list_head	rsp_wr_wait_list;
	spinlock_t		rsp_wr_wait_lock;

	int			idx;
	struct kref		ref;
	int			host_qid;
	int			recv_queue_size;
	int			send_queue_size;

	struct list_head	queue_list;
};

struct nvmet_rdma_device {
	struct ib_device	*device;
	struct ib_pd		*pd;
	struct ib_srq		*srq;
	struct nvmet_rdma_cmd	*srq_cmds;
	size_t			srq_size;
	struct kref		ref;
	struct list_head	entry;
	bool			need_rdma_read_mr;
};

static u16 nvmet_rdma_cm_port = 1023; // XXX
module_param_named(cm_port, nvmet_rdma_cm_port, short, 0444);
MODULE_PARM_DESC(cm_port, "Port number CM will bind to.");

static bool nvmet_rdma_use_srq;
module_param_named(use_srq, nvmet_rdma_use_srq, bool, 0444);
MODULE_PARM_DESC(use_srq, "Use shared receive queue.");

static struct rdma_cm_id *nvmet_rdma_cm_id;

static DEFINE_IDA(nvmet_rdma_queue_ida);
static LIST_HEAD(nvmet_rdma_queue_list);
static DEFINE_MUTEX(nvmet_rdma_queue_mutex);

static LIST_HEAD(device_list);
static DEFINE_MUTEX(device_list_mutex);

static bool nvmet_rdma_execute_command(struct nvmet_rdma_rsp *rsp);
static void nvmet_rdma_send_done(struct ib_cq *cq, struct ib_wc *wc);
static void nvmet_rdma_cmd_done(struct ib_cq *cq, struct ib_wc *wc);
static void nvmet_rdma_read_data_done(struct ib_cq *cq, struct ib_wc *wc);
static void nvmet_rdma_qp_event(struct ib_event *event, void *priv);
static void nvmet_rdma_queue_response(struct nvmet_req *req);
static void nvmet_rdma_identify_attrs(struct nvmet_ctrl *ctrl,
				struct nvme_id_ctrl *id);

struct nvmet_fabrics_ops nvmet_rdma_ops = {
	.queue_response = nvmet_rdma_queue_response,
	.identify_attrs = nvmet_rdma_identify_attrs,
};

static void nvmet_rdma_identify_attrs(struct nvmet_ctrl *ctrl,
				struct nvme_id_ctrl *id)
{
	/* Max command capsule size is sqe + single page of in-capsule data */
	id->ioccsz = cpu_to_le32(NVMET_CMD_CAPSULE_SIZE / 16);
	/* Max response capsule size is cqe */
	id->iorcsz = cpu_to_le32(sizeof(struct nvme_completion) / 16);
	/* Currently we don't support in-capsule data offset */
	id->icdoff = 0;
	/* We support keyed sgls and in-capsule offset sgl */
	id->sgls = cpu_to_le32(1 << 20 | 1 << 2);
	/* no enforcement soft-limit for maxcmd - pick arbitrary high value */
	id->maxcmd = cpu_to_le16(NVMET_MAX_CMD);
}

static inline bool nvmet_rdma_need_data_in(struct nvmet_req *req)
{
	return nvme_is_write(req->cmd) &&
		req->data_len &&
		!(req->flags & NVMET_REQ_INLINE_DATA);
}

static inline bool nvmet_rdma_need_data_out(struct nvmet_req *req)
{
	return !nvme_is_write(req->cmd) &&
		req->data_len &&
		!req->rsp->status &&
		!(req->flags & NVMET_REQ_INLINE_DATA);
}

static inline struct nvmet_rdma_rsp *
nvmet_rdma_get_rsp(struct nvmet_rdma_queue *queue)
{
	struct nvmet_rdma_rsp *rsp;
	unsigned long flags;

	spin_lock_irqsave(&queue->rsps_lock, flags);
	rsp = list_first_entry(&queue->free_rsps,
				struct nvmet_rdma_rsp, free_list);
	list_del(&rsp->free_list);
	spin_unlock_irqrestore(&queue->rsps_lock, flags);

	return rsp;
}

static inline void
nvmet_rdma_put_rsp(struct nvmet_rdma_rsp *rsp)
{
	unsigned long flags;

	spin_lock_irqsave(&rsp->queue->rsps_lock, flags);
	list_add_tail(&rsp->free_list, &rsp->queue->free_rsps);
	spin_unlock_irqrestore(&rsp->queue->rsps_lock, flags);
}

static void nvmet_rdma_free_sgl(struct scatterlist *sgl, unsigned int nents)
{
	struct scatterlist *sg;
	int count;

	if (!sgl || !nents)
		return;

	for_each_sg(sgl, sg, nents, count)
		__free_page(sg_page(sg));
	kfree(sgl);
}

static int nvmet_rdma_alloc_sgl(struct scatterlist **sgl, unsigned int *nents,
		u32 length)
{
	struct scatterlist *sg;
	struct page *page;
	unsigned int nent;
	int i = 0;

	nent = DIV_ROUND_UP(length, PAGE_SIZE);
	sg = kmalloc_array(nent, sizeof(struct scatterlist), GFP_KERNEL);
	if (!sg)
		goto out;

	sg_init_table(sg, nent);

	while (length) {
		u32 page_len = min_t(u32, length, PAGE_SIZE);

		page = alloc_page(GFP_KERNEL);
		if (!page)
			goto out_free_pages;

		sg_set_page(&sg[i], page, page_len, 0);
		length -= page_len;
		i++;
	}
	*sgl = sg;
	*nents = nent;
	return 0;

out_free_pages:
	while (i > 0) {
		i--;
		__free_page(sg_page(&sg[i]));
	}
	kfree(sg);
out:
	return NVME_SC_INTERNAL;
}

static int nvmet_rdma_alloc_cmd(struct nvmet_rdma_device *ndev,
			struct nvmet_rdma_cmd *c, bool admin)
{
	/* NVMe command / RDMA RECV */
	c->nvme_cmd = kmalloc(sizeof(*c->nvme_cmd), GFP_KERNEL);
	if (!c->nvme_cmd)
		goto out;

	c->sge[0].addr = ib_dma_map_single(ndev->device, c->nvme_cmd,
			sizeof(*c->nvme_cmd), DMA_FROM_DEVICE);
	if (ib_dma_mapping_error(ndev->device, c->sge[0].addr))
		goto out_free_cmd;

	c->sge[0].length = sizeof(*c->nvme_cmd);
	c->sge[0].lkey = ndev->pd->local_dma_lkey;

	if (!admin) {
		c->inline_data = (void *)__get_free_page(GFP_KERNEL);
		if (!c->inline_data)
			goto out_unmap_cmd;
		c->sge[1].addr = ib_dma_map_single(ndev->device,
				c->inline_data, PAGE_SIZE, DMA_FROM_DEVICE);
		if (ib_dma_mapping_error(ndev->device, c->sge[1].addr))
			goto out_free_inline_data;
		c->sge[1].length = PAGE_SIZE;
		c->sge[1].lkey = ndev->pd->local_dma_lkey;
	}

	c->cqe.done = nvmet_rdma_cmd_done;

	c->wr.wr_cqe = &c->cqe;
	c->wr.sg_list = c->sge;
	c->wr.num_sge = admin ? 1 : 2;

	return 0;

out_free_inline_data:
	if (!admin)
		free_page((unsigned long)c->inline_data);
out_unmap_cmd:
	ib_dma_unmap_single(ndev->device, c->sge[0].addr,
			sizeof(*c->nvme_cmd), DMA_FROM_DEVICE);
out_free_cmd:
	kfree(c->nvme_cmd);

out:
	return -ENOMEM;
}

static void nvmet_rdma_free_cmd(struct nvmet_rdma_device *ndev,
		struct nvmet_rdma_cmd *c, bool admin)
{
	if (!admin) {
		ib_dma_unmap_single(ndev->device, c->sge[1].addr,
				PAGE_SIZE, DMA_FROM_DEVICE);
		free_page((unsigned long)c->inline_data);
	}
	ib_dma_unmap_single(ndev->device, c->sge[0].addr,
				sizeof(*c->nvme_cmd), DMA_FROM_DEVICE);
	kfree(c->nvme_cmd);
}

static struct nvmet_rdma_cmd *
nvmet_rdma_alloc_cmds(struct nvmet_rdma_device *ndev,
		int nr_cmds, bool admin)
{
	struct nvmet_rdma_cmd *cmds;
	int ret = -EINVAL, i;

	cmds = kcalloc(nr_cmds, sizeof(struct nvmet_rdma_cmd), GFP_KERNEL);
	if (!cmds)
		goto out;

	for (i = 0; i < nr_cmds; i++) {
		ret = nvmet_rdma_alloc_cmd(ndev, cmds + i, admin);
		if (ret)
			goto out_free;
	}

	return cmds;

out_free:
	while (--i >= 0)
		nvmet_rdma_free_cmd(ndev, cmds + i, admin);
	kfree(cmds);
out:
	return ERR_PTR(ret);
}

static void nvmet_rdma_free_cmds(struct nvmet_rdma_device *ndev,
		struct nvmet_rdma_cmd *cmds, int nr_cmds, bool admin)
{
	int i;

	for (i = 0; i < nr_cmds; i++)
		nvmet_rdma_free_cmd(ndev, cmds + i, admin);
	kfree(cmds);
}

static int nvmet_rdma_alloc_rsp(struct nvmet_rdma_device *ndev,
		struct nvmet_rdma_rsp *r)
{
	/* NVMe CQE / RDMA SEND */
	r->req.rsp = kmalloc(sizeof(*r->req.rsp), GFP_KERNEL);
	if (!r->req.rsp)
		goto out;

	r->send_sge.addr = ib_dma_map_single(ndev->device, r->req.rsp,
			sizeof(*r->req.rsp), DMA_TO_DEVICE);
	if (ib_dma_mapping_error(ndev->device, r->send_sge.addr))
		goto out_free_rsp;

	r->send_sge.length = sizeof(*r->req.rsp);
	r->send_sge.lkey = ndev->pd->local_dma_lkey;

	r->send_cqe.done = nvmet_rdma_send_done;

	r->send_wr.wr_cqe = &r->send_cqe;
	r->send_wr.sg_list = &r->send_sge;
	r->send_wr.num_sge = 1;
	r->send_wr.send_flags = IB_SEND_SIGNALED;

	/* Data In / RDMA READ */
	r->read_cqe.done = nvmet_rdma_read_data_done;
	return 0;

out_free_rsp:
	kfree(r->req.rsp);
out:
	return -ENOMEM;
}

static void nvmet_rdma_free_rsp(struct nvmet_rdma_device *ndev,
		struct nvmet_rdma_rsp *r)
{
	ib_dma_unmap_single(ndev->device, r->send_sge.addr,
				sizeof(*r->req.rsp), DMA_TO_DEVICE);
	kfree(r->req.rsp);
}

static int
nvmet_rdma_alloc_rsps(struct nvmet_rdma_queue *queue)
{
	struct nvmet_rdma_device *ndev = queue->dev;
	int nr_rsps = queue->recv_queue_size * 2;
	int ret = -EINVAL, i;

	queue->rsps = kcalloc(nr_rsps, sizeof(struct nvmet_rdma_rsp), GFP_KERNEL);
	if (!queue->rsps)
		goto out;

	for (i = 0; i < nr_rsps; i++) {
		struct nvmet_rdma_rsp *rsp = &queue->rsps[i];

		ret = nvmet_rdma_alloc_rsp(ndev, rsp);
		if (ret)
			goto out_free;

		list_add_tail(&rsp->free_list, &queue->free_rsps);
	}

	return 0;

out_free:
	while (--i >= 0) {
		struct nvmet_rdma_rsp *rsp = &queue->rsps[i];

		list_del(&rsp->free_list);
		nvmet_rdma_free_rsp(ndev, rsp);
	}
	kfree(queue->rsps);
out:
	return ret;
}

static void nvmet_rdma_free_rsps(struct nvmet_rdma_queue *queue)
{
	struct nvmet_rdma_device *ndev = queue->dev;
	int i, nr_rsps = queue->recv_queue_size * 2;

	for (i = 0; i < nr_rsps; i++) {
		struct nvmet_rdma_rsp *rsp = &queue->rsps[i];

		list_del(&rsp->free_list);
		nvmet_rdma_free_rsp(ndev, rsp);
	}
	kfree(queue->rsps);
}

static int nvmet_rdma_post_recv(struct nvmet_rdma_device *ndev,
		struct nvmet_rdma_cmd *cmd)
{
	struct ib_recv_wr *bad_wr;

	if (ndev->srq)
		return ib_post_srq_recv(ndev->srq, &cmd->wr, &bad_wr);
	return ib_post_recv(cmd->queue->cm_id->qp, &cmd->wr, &bad_wr);
}

static void nvmet_rdma_process_wr_wait_list(struct nvmet_rdma_queue *queue)
{
	spin_lock(&queue->rsp_wr_wait_lock);
	while (!list_empty(&queue->rsp_wr_wait_list)) {
		struct nvmet_rdma_rsp *rsp;
		bool ret;

		rsp = list_entry(queue->rsp_wr_wait_list.next,
				struct nvmet_rdma_rsp, wait_list);
		list_del(&rsp->wait_list);

		spin_unlock(&queue->rsp_wr_wait_lock);
		ret = nvmet_rdma_execute_command(rsp);
		spin_lock(&queue->rsp_wr_wait_lock);

		if (!ret) {
			list_add(&rsp->wait_list, &queue->rsp_wr_wait_list);
			break;
		}
	}
	spin_unlock(&queue->rsp_wr_wait_lock);
}


static void nvmet_rdma_release_rsp(struct nvmet_rdma_rsp *rsp)
{
	struct nvmet_rdma_queue *queue = rsp->queue;

	atomic_add(1 + rsp->n_rdma, &queue->sq_wr_avail);

	if (rsp->n_rdma)
		rdma_rw_ctx_destroy(&rsp->rw, queue->cm_id->qp);

	if (rsp->req.sg != &rsp->cmd->inline_sg)
		nvmet_rdma_free_sgl(rsp->req.sg, rsp->req.sg_cnt);

	if (unlikely(!list_empty_careful(&queue->rsp_wr_wait_list)))
		nvmet_rdma_process_wr_wait_list(queue);

	nvmet_rdma_put_rsp(rsp);
}

static void nvmet_rdma_send_done(struct ib_cq *cq, struct ib_wc *wc)
{
	struct nvmet_rdma_rsp *rsp =
		container_of(wc->wr_cqe, struct nvmet_rdma_rsp, send_cqe);

	nvmet_rdma_release_rsp(rsp);
}

static void nvmet_rdma_queue_response(struct nvmet_req *req)
{
	struct nvmet_rdma_rsp *rsp =
		container_of(req, struct nvmet_rdma_rsp, req);
	struct ib_qp *qp = rsp->queue->cm_id->qp;
	struct ib_send_wr *bad_wr;
	int ret;

	if (rsp->req.flags & NVMET_REQ_INVALIDATE_RKEY) {
		rsp->send_wr.opcode = IB_WR_SEND_WITH_INV;
		rsp->send_wr.ex.invalidate_rkey = rsp->invalidate_rkey;
	} else {
		rsp->send_wr.opcode = IB_WR_SEND;
	}

	if (nvmet_rdma_need_data_out(req)) {
		nvmet_rdma_post_recv(rsp->queue->dev, rsp->cmd);
		ret = rdma_rw_post(&rsp->rw, qp, NULL, &rsp->send_wr);
	} else {
		nvmet_rdma_post_recv(rsp->queue->dev, rsp->cmd);
		ret = ib_post_send(qp, &rsp->send_wr, &bad_wr);
	}

	if (ret) {
		pr_err("sending response failed: %d\n", ret);
		nvmet_rdma_release_rsp(rsp);
	}
}

static void nvmet_rdma_read_data_done(struct ib_cq *cq, struct ib_wc *wc)
{
	struct nvmet_rdma_rsp *rsp =
		container_of(wc->wr_cqe, struct nvmet_rdma_rsp, read_cqe);
	struct nvmet_rdma_queue *queue = cq->cq_context;

	WARN_ON(rsp->n_rdma <= 0);
	atomic_add(rsp->n_rdma, &queue->sq_wr_avail);
	rdma_rw_ctx_destroy(&rsp->rw, queue->cm_id->qp);
	rsp->n_rdma = 0;

	if (unlikely(wc->status != IB_WC_SUCCESS &&
		wc->status != IB_WC_WR_FLUSH_ERR)) {
		pr_info("RDMA READ for CQE 0x%p failed with status %s (%d).\n",
			wc->wr_cqe, ib_wc_status_msg(wc->status), wc->status);
		nvmet_req_complete(&rsp->req, NVME_SC_DATA_XFER_ERROR);
		return;
	}

	rsp->req.execute(&rsp->req);
}

static void *nvmet_rdma_dapsule_ptr(struct nvmet_rdma_rsp *rsp,
		struct nvme_rsgl_desc *rsgl)
{
	u64 offset = le64_to_cpu(rsgl->addr);

	/* XXX: we don't support icdoff */
	WARN_ON_ONCE(offset != 0);

	return rsp->cmd->inline_data + offset;
}

static u16 nvmet_rdma_map_inline_data(struct nvmet_rdma_rsp *rsp)
{
	struct nvme_rsgl_desc *rsgl = &rsp->req.cmd->common.dptr.rsgl;
	void *data = nvmet_rdma_dapsule_ptr(rsp, rsgl);
	int count;

	if (!nvme_is_write(rsp->req.cmd))
		return NVME_SC_INVALID_FIELD | NVME_SC_DNR;
	if (!data) {
		pr_err("invalid inline data offset!\n");
		return NVME_SC_SGL_INVALID_OFFSET | NVME_SC_DNR;
	}

	sg_init_one(&rsp->cmd->inline_sg, data,
		get_unaligned_le24(rsgl->length));
	rsp->req.sg = &rsp->cmd->inline_sg;
	rsp->req.sg_cnt = 1;
	rsp->req.flags |= NVMET_REQ_INLINE_DATA;

	count = ib_dma_map_sg(rsp->queue->dev->device, rsp->req.sg,
			rsp->req.sg_cnt, nvmet_data_dir(&rsp->req));
	if (count != 1)
		return NVME_SC_INTERNAL;
	return 0;
}

static u16 nvmet_rdma_map_sgl_data(struct nvmet_rdma_rsp *rsp,
		struct nvme_rsgl_desc *rsgl)
{
	struct scatterlist *sg;
	int sg_cnt, ret;
	u32 len;
	u16 status;

	switch (rsgl->format & 0xf) {
	case NVME_SGL_FMT_INVALIDATE:
		rsp->invalidate_rkey = get_unaligned_le32(rsgl->key);
		rsp->req.flags |= NVMET_REQ_INVALIDATE_RKEY;
		/* FALLTHRU */
	case NVME_SGL_FMT_ADDRESS:
		break;
	default:
		pr_err("invalid keyed SGL subtype: %#x\n", rsgl->format & 0xf);
		return NVME_SC_SGL_INVALID_SUBTYPE | NVME_SC_DNR;
	}

	len = get_unaligned_le24(rsgl->length);
	if (!len)
		return 0;

	status = nvmet_rdma_alloc_sgl(&sg, &sg_cnt, len);
	if (status)
		return status;

	ret = rdma_rw_ctx_init(&rsp->rw, rsp->queue->cm_id->qp,
			rsp->queue->cm_id->port_num, sg, sg_cnt, len,
			le64_to_cpu(rsgl->addr), get_unaligned_le32(rsgl->key),
			nvmet_data_dir(&rsp->req), 0);
	if (ret < 0)
		return NVME_SC_INTERNAL;

	rsp->n_rdma += ret;

	/*
	 * XXX: to support multiple S/G entries we need to start using
	 * sg_chain() here.  We probably need a containing structure for the
	 * fist struct ib_rdma_wr for each SGE as well.
	 */
	rsp->req.sg = sg;
	rsp->req.sg_cnt = sg_cnt;
	return 0;
}

static u16 nvmet_rdma_map_sgl_seg(struct nvmet_rdma_rsp *rsp,
		struct nvme_rsgl_desc *rsgl, bool last)
{
	struct nvme_rsgl_desc *sgl = nvmet_rdma_dapsule_ptr(rsp, rsgl);
	u32 desc_len = get_unaligned_le24(rsgl->length);
	int nr_sge = desc_len / sizeof(struct nvme_rsgl_desc), i;
	u16 status;

	if (!sgl) {
		pr_err("invalid SGL offset\n");
		return NVME_SC_INVALID_FIELD | NVME_SC_DNR;
	}

	printk_ratelimited("WARNING: out of command SGLs not tested!\n");

	if ((rsgl->format & 0xf) == NVME_SGL_FMT_OFFSET) {
		pr_err("invalid SGL subtype: 0x%x\n", rsgl->format & 0xf);
		return NVME_SC_SGL_INVALID_SUBTYPE | NVME_SC_DNR;
	}

	for (i = 0; i < nr_sge; i++) {
		switch (sgl->format >> 4) {
		case NVME_KEY_SGL_FMT_DATA_DESC:
			status = nvmet_rdma_map_sgl_data(rsp, sgl);
			if (status)
				return status;
			break;
		case NVME_KEY_SGL_FMT_SEG_DESC:
		case NVME_KEY_SGL_FMT_LAST_SEG_DESC:
			pr_err("indirect SGLs not supported!\n");
			return NVME_SC_INVALID_FIELD | NVME_SC_DNR;
		default:
			pr_err("invalid SGL format: 0x%x\n",
				sgl->format);
			return NVME_SC_INVALID_FIELD | NVME_SC_DNR;
		}

		sgl++;
	}

	return 0;
}

static u16 nvmet_rdma_map_sgl(struct nvmet_rdma_rsp *rsp)
{
	struct nvme_rsgl_desc *rsgl = &rsp->req.cmd->common.dptr.rsgl;

	if ((rsgl->format >> 4) == NVME_SGL_FMT_DATA_DESC &&
	    (rsgl->format & 0xf) == NVME_SGL_FMT_OFFSET)
		return nvmet_rdma_map_inline_data(rsp);

	if (unlikely(get_unaligned_le24(rsgl->length) == 0))
		/* no data command */
		return 0;

	switch (rsgl->format >> 4) {
	case NVME_KEY_SGL_FMT_DATA_DESC:
		return nvmet_rdma_map_sgl_data(rsp, rsgl);
	case NVME_KEY_SGL_FMT_LAST_SEG_DESC:
		return nvmet_rdma_map_sgl_seg(rsp, rsgl, true);
	case NVME_KEY_SGL_FMT_SEG_DESC:
		return nvmet_rdma_map_sgl_seg(rsp, rsgl, false);
	default:
		pr_err("invalid SGL format: 0x%x\n", rsgl->format);
		return NVME_SC_INVALID_FIELD | NVME_SC_DNR;
	}

	return 0;
}

static bool nvmet_rdma_execute_command(struct nvmet_rdma_rsp *rsp)
{
	if (unlikely(atomic_sub_return(1 + rsp->n_rdma,
			&rsp->queue->sq_wr_avail) < 0)) {
		pr_warn("IB send queue full (needed %d)\n", 1 + rsp->n_rdma);
		atomic_add(1 + rsp->n_rdma, &rsp->queue->sq_wr_avail);
		return false;
	}

	if (nvmet_rdma_need_data_in(&rsp->req)) {
		if (rdma_rw_post(&rsp->rw, rsp->queue->cm_id->qp,
				 &rsp->read_cqe, NULL))
			nvmet_req_complete(&rsp->req, NVME_SC_DATA_XFER_ERROR);
	} else {
		rsp->req.execute(&rsp->req);
	}

	return true;
}

static void nvmet_rdma_handle_command(struct nvmet_rdma_queue *queue,
		struct nvmet_rdma_rsp *cmd)
{
	u16 status;

	cmd->queue = queue;
	cmd->n_rdma = 0;

	status = nvmet_req_init(&cmd->req, &queue->nvme_cq,
			&queue->nvme_sq, &nvmet_rdma_ops);
	if (status)
		goto out_err;

	status = nvmet_rdma_map_sgl(cmd);
	if (status)
		goto out_err;

	if (unlikely(!nvmet_rdma_execute_command(cmd))) {
		spin_lock(&queue->rsp_wr_wait_lock);
		list_add_tail(&cmd->wait_list, &queue->rsp_wr_wait_list);
		spin_unlock(&queue->rsp_wr_wait_lock);
	}

	return;

out_err:
	nvmet_req_complete(&cmd->req, status);
}

static void nvmet_rdma_cmd_done(struct ib_cq *cq, struct ib_wc *wc)
{
	struct nvmet_rdma_cmd *cmd =
		container_of(wc->wr_cqe, struct nvmet_rdma_cmd, cqe);
	struct nvmet_rdma_queue *queue = cq->cq_context;
	struct nvmet_rdma_rsp *rsp;

	if (unlikely(wc->status != IB_WC_SUCCESS))
		return;

	cmd->queue = queue;
	rsp = nvmet_rdma_get_rsp(queue);
	rsp->cmd = cmd;
	rsp->req.cmd = cmd->nvme_cmd;

	if (unlikely(queue->state != NVMET_RDMA_Q_LIVE)) {
		unsigned long flags;

		spin_lock_irqsave(&queue->state_lock, flags);
		if (queue->state == NVMET_RDMA_Q_CONNECTING)
			list_add_tail(&rsp->wait_list, &queue->rsp_wait_list);
		spin_unlock_irqrestore(&queue->state_lock, flags);
		return;
	}

	nvmet_rdma_handle_command(queue, rsp);
}

static void nvmet_rdma_destroy_srq(struct nvmet_rdma_device *ndev)
{
	if (!ndev->srq)
		return;

	nvmet_rdma_free_cmds(ndev, ndev->srq_cmds, ndev->srq_size, false);
	ib_destroy_srq(ndev->srq);
}

static int nvmet_rdma_init_srq(struct nvmet_rdma_device *ndev)
{
	struct ib_srq_init_attr srq_attr = { NULL, };
	struct ib_srq *srq;
	size_t srq_size;
	int ret, i;

	srq_size = 4095;	// XXX: tune

	srq_attr.attr.max_wr = srq_size;
	srq_attr.attr.max_sge = 2;
	srq_attr.attr.srq_limit = 0;
	srq_attr.srq_type = IB_SRQT_BASIC;
	srq = ib_create_srq(ndev->pd, &srq_attr);
	if (IS_ERR(srq)) {
		/*
		 * If SRQs aren't supported we just go ahead and use normal
		 * non-shared receive queues.
		 */
		pr_info("SRQ requested but not supported.\n");
		return 0;
	}

	ndev->srq_cmds = nvmet_rdma_alloc_cmds(ndev, srq_size, false);
	if (IS_ERR(ndev->srq_cmds)) {
		ret = PTR_ERR(ndev->srq_cmds);
		goto out_destroy_srq;
	}

	ndev->srq = srq;
	ndev->srq_size = srq_size;

	for (i = 0; i < srq_size; i++)
		nvmet_rdma_post_recv(ndev, &ndev->srq_cmds[i]);

	return 0;

out_destroy_srq:
	ib_destroy_srq(srq);
	return ret;
}

static void nvmet_rdma_free_dev(struct kref *ref)
{
	struct nvmet_rdma_device *ndev =
		container_of(ref, struct nvmet_rdma_device, ref);

	mutex_lock(&device_list_mutex);
	list_del(&ndev->entry);
	mutex_unlock(&device_list_mutex);

	nvmet_rdma_destroy_srq(ndev);
	ib_dealloc_pd(ndev->pd);

	kfree(ndev);
}

static struct nvmet_rdma_device *
nvmet_rdma_find_get_device(struct rdma_cm_id *cm_id)
{
	struct nvmet_rdma_device *ndev;
	int ret;

	mutex_lock(&device_list_mutex);
	list_for_each_entry(ndev, &device_list, entry) {
		if (ndev->device->node_guid == cm_id->device->node_guid &&
		    kref_get_unless_zero(&ndev->ref))
			goto out_unlock;
	}

	ndev = kzalloc(sizeof(*ndev), GFP_KERNEL);
	if (!ndev)
		goto out_err;

	ndev->device = cm_id->device;
	kref_init(&ndev->ref);

	if (rdma_protocol_iwarp(ndev->device, cm_id->port_num))
		ndev->need_rdma_read_mr = true;

	ndev->pd = ib_alloc_pd(ndev->device);
	if (IS_ERR(ndev->pd))
		goto out_free_dev;

	if (nvmet_rdma_use_srq) {
		ret = nvmet_rdma_init_srq(ndev);
		if (ret)
			goto out_free_pd;
	}

	list_add(&ndev->entry, &device_list);
out_unlock:
	mutex_unlock(&device_list_mutex);
	pr_debug("added %s.\n", ndev->device->name);
	return ndev;

out_free_pd:
	ib_dealloc_pd(ndev->pd);
out_free_dev:
	kfree(ndev);
out_err:
	mutex_unlock(&device_list_mutex);
	return NULL;
}

static int nvmet_rdma_create_queue_ib(struct nvmet_rdma_queue *queue)
{
	struct ib_qp_init_attr *qp_attr;
	struct nvmet_rdma_device *ndev = queue->dev;
	int comp_vector, send_wrs, nr_cqe, ret, i;

	/*
	 * The admin queue is barely used once the controller is live, so don't
	 * bother to spread it out.
	 */
	if (queue->idx == 0)
		comp_vector = 0;
	else
		comp_vector =
			queue->idx % ndev->device->num_comp_vectors;

	send_wrs = queue->send_queue_size;
	if (ndev->need_rdma_read_mr)
		send_wrs *= 3; /* + REG_WR, INV_WR */

	nr_cqe = send_wrs + queue->recv_queue_size;

	ret = -ENOMEM;
	qp_attr = kzalloc(sizeof(*qp_attr), GFP_KERNEL);
	if (!qp_attr)
		goto out;

	queue->cq = ib_alloc_cq(ndev->device, queue,
			nr_cqe + 1, comp_vector,
			IB_POLL_WORKQUEUE);
	if (IS_ERR(queue->cq)) {
		ret = PTR_ERR(queue->cq);
		pr_err("failed to create CQ cqe= %d ret= %d\n",
		       nr_cqe + 1, ret);
		goto out;
	}

	qp_attr->qp_context = queue;
	qp_attr->event_handler = nvmet_rdma_qp_event;
	qp_attr->send_cq = queue->cq;
	qp_attr->recv_cq = queue->cq;
	qp_attr->sq_sig_type = IB_SIGNAL_REQ_WR;
	qp_attr->qp_type = IB_QPT_RC;
	/* +1 for drain */
	qp_attr->cap.max_send_wr = 1 + send_wrs;
	qp_attr->cap.max_send_sge = max(ndev->device->attrs.max_sge_rd,
					ndev->device->attrs.max_sge);

	if (ndev->srq) {
		qp_attr->srq = ndev->srq;
	} else {
		/* +1 for drain */
		qp_attr->cap.max_recv_wr = 1 + queue->recv_queue_size;
		qp_attr->cap.max_recv_sge = 2;
	}

	ret = rdma_create_qp(queue->cm_id, ndev->pd, qp_attr);
	if (ret) {
		pr_err("failed to create_qp ret= %d\n", ret);
		goto err_destroy_cq;
	}

	if (ndev->need_rdma_read_mr) {
		/*
		 * Allocate one MR per SQE as a start.  For devices with very
		 * small MR sizes we will need a multiplier here.
		 */
		ret = ib_mr_pool_init(queue->cm_id->qp, queue->send_queue_size,
				IB_MR_TYPE_MEM_REG,
				ndev->device->attrs.max_fast_reg_page_list_len);
		if (ret) {
			pr_err("failed to init MR pool ret= %d\n", ret);
			goto err_destroy_qp;
		}
	}

	atomic_set(&queue->sq_wr_avail, qp_attr->cap.max_send_wr);

	pr_debug("%s: max_cqe= %d max_sge= %d sq_size = %d cm_id= %p\n",
		 __func__, queue->cq->cqe, qp_attr->cap.max_send_sge,
		 qp_attr->cap.max_send_wr, queue->cm_id);

	if (!ndev->srq) {
		for (i = 0; i < queue->recv_queue_size; i++) {
			queue->cmds[i].queue = queue;
			nvmet_rdma_post_recv(ndev, &queue->cmds[i]);
		}
	}

out:
	kfree(qp_attr);
	return ret;

err_destroy_qp:
	rdma_destroy_qp(queue->cm_id);
err_destroy_cq:
	ib_free_cq(queue->cq);
	goto out;
}

static void nvmet_rdma_destroy_queue_ib(struct nvmet_rdma_queue *queue)
{
	if (queue->dev->need_rdma_read_mr)
		ib_mr_pool_destroy(queue->cm_id->qp);
	rdma_destroy_qp(queue->cm_id);
	ib_free_cq(queue->cq);
}

static void nvmet_rdma_free_queue(struct nvmet_rdma_queue *queue)
{
	pr_info("freeing queue %d\n", queue->idx);

	nvmet_sq_destroy(&queue->nvme_sq);

	nvmet_rdma_destroy_queue_ib(queue);
	if (!queue->dev->srq) {
		nvmet_rdma_free_cmds(queue->dev, queue->cmds,
				queue->recv_queue_size,
				!queue->nvme_sq.qid);
	}
	nvmet_rdma_free_rsps(queue);
	mutex_lock(&nvmet_rdma_queue_mutex);
	ida_simple_remove(&nvmet_rdma_queue_ida, queue->idx);
	mutex_unlock(&nvmet_rdma_queue_mutex);
	kfree(queue);
}

static void nvmet_rdma_release_queue_work(struct work_struct *w)
{
	struct nvmet_rdma_queue *queue =
		container_of(w, struct nvmet_rdma_queue, release_work);
	struct rdma_cm_id *cm_id = queue->cm_id;
	struct nvmet_rdma_device *dev = queue->dev;

	mutex_lock(&nvmet_rdma_queue_mutex);
	list_del(&queue->queue_list);
	mutex_unlock(&nvmet_rdma_queue_mutex);

	nvmet_rdma_free_queue(queue);
	rdma_destroy_id(cm_id);
	kref_put(&dev->ref, nvmet_rdma_free_dev);
}

/*
 * Schedules the actual release because calling rdma_destroy_id from inside
 * a CM callback would trigger a deadlock. (great API design..)
 */
static void nvmet_rdma_queue_put(struct kref *ref)
{
	struct nvmet_rdma_queue *queue =
		container_of(ref, struct nvmet_rdma_queue, ref);

	schedule_work(&queue->release_work);
}

static int
nvmet_rdma_parse_cm_connect_req(struct rdma_conn_param *conn,
				struct nvmet_rdma_queue *queue)
{
	struct nvme_rdma_cm_req *req;
	int sq_factor = 2; /* reserve SQ slots for RDMA_READs, RDMA_WRITEs */

	req = (struct nvme_rdma_cm_req *)conn->private_data;
	if (!req || conn->private_data_len == 0)
		return NVME_RDMA_CM_INVALID_REQ;

	if (le16_to_cpu(req->recfmt) != NVME_RDMA_CM_FMT_1_0)
		return NVME_RDMA_CM_INVALID_RECFMT;

	/* XXX: Looks like we don't care about the cntlid, hostsid */

	queue->host_qid = le16_to_cpu(req->qid);

	/*
	 * req->sqsize corresponds to our recv queue size
	 * req->cqsize corresponds to our send queue size
	 */
	queue->recv_queue_size = le16_to_cpu(req->sqsize);
	queue->send_queue_size = sq_factor * le16_to_cpu(req->cqsize);

	if (!queue->host_qid && queue->recv_queue_size > NVMF_AQ_DEPTH)
		return NVME_RDMA_CM_INVALID_SQSIZE;

	/* XXX: Should we enforce some kind of max for IO queues? */

	return 0;
}

static int nvmet_rdma_cm_reject(struct rdma_cm_id *cm_id,
				enum nvme_rdma_cm_status status)
{
	struct nvme_rdma_cm_rej rej;

	rej.recfmt = cpu_to_le16(NVME_RDMA_CM_FMT_1_0);
	rej.fsts = cpu_to_le16(status);

	return rdma_reject(cm_id, (void *)&rej, sizeof(rej));
}

static struct nvmet_rdma_queue *
nvmet_rdma_alloc_queue(struct nvmet_rdma_device *ndev,
		struct rdma_cm_id *cm_id,
		struct rdma_cm_event *event)
{
	struct nvmet_rdma_queue *queue;
	int ret;

	queue = kzalloc(sizeof(*queue), GFP_KERNEL);
	if (!queue) {
		ret = NVME_RDMA_CM_NO_RSC;
		goto out_reject;
	}

	ret = nvmet_sq_init(&queue->nvme_sq);
	if (ret)
		goto out_free_queue;

	ret = nvmet_rdma_parse_cm_connect_req(&event->param.conn, queue);
	if (ret)
		goto out_destroy_sq;

	kref_init(&queue->ref);
	INIT_WORK(&queue->release_work, nvmet_rdma_release_queue_work);
	queue->dev = ndev;
	queue->cm_id = cm_id;

	spin_lock_init(&queue->state_lock);
	queue->state = NVMET_RDMA_Q_CONNECTING;
	INIT_LIST_HEAD(&queue->rsp_wait_list);
	INIT_LIST_HEAD(&queue->rsp_wr_wait_list);
	spin_lock_init(&queue->rsp_wr_wait_lock);
	INIT_LIST_HEAD(&queue->free_rsps);
	spin_lock_init(&queue->rsps_lock);

	mutex_lock(&nvmet_rdma_queue_mutex);
	queue->idx = ida_simple_get(&nvmet_rdma_queue_ida, 0, 0, GFP_KERNEL);
	mutex_unlock(&nvmet_rdma_queue_mutex);
	if (queue->idx < 0) {
		ret = NVME_RDMA_CM_NO_RSC;
		goto out_free_queue;
	}

	ret = nvmet_rdma_alloc_rsps(queue);
	if (ret) {
		ret = NVME_RDMA_CM_NO_RSC;
		goto out_ida_remove;
	}

	if (!ndev->srq) {
		queue->cmds = nvmet_rdma_alloc_cmds(ndev,
				queue->recv_queue_size,
				!queue->host_qid);
		if (IS_ERR(queue->cmds)) {
			ret = NVME_RDMA_CM_NO_RSC;
			goto out_free_cmds;
		}
	}

	ret = nvmet_rdma_create_queue_ib(queue);
	if (ret) {
		pr_err("%s: creating RDMA queue failed (%d).\n",
			__func__, ret);
		ret = NVME_RDMA_CM_NO_RSC;
		goto out_free_cmds;
	}

	return queue;

out_free_cmds:
	if (!ndev->srq) {
		nvmet_rdma_free_cmds(queue->dev, queue->cmds,
				queue->recv_queue_size,
				!queue->nvme_sq.qid);
	}
out_ida_remove:
	mutex_lock(&nvmet_rdma_queue_mutex);
	ida_simple_remove(&nvmet_rdma_queue_ida, queue->idx);
	mutex_unlock(&nvmet_rdma_queue_mutex);
out_destroy_sq:
	nvmet_sq_destroy(&queue->nvme_sq);
out_free_queue:
	kfree(queue);
out_reject:
	nvmet_rdma_cm_reject(cm_id, ret);
	return NULL;
}

static void nvmet_rdma_qp_event(struct ib_event *event, void *priv)
{
	struct nvmet_rdma_queue *queue = priv;

	switch (event->event) {
	case IB_EVENT_COMM_EST:
		rdma_notify(queue->cm_id, event->event);
		break;
	default:
		pr_err("received unrecognized IB QP event %d\n", event->event);
		break;
	}
}

static int nvmet_rdma_cm_accept(struct rdma_cm_id *cm_id,
		struct nvmet_rdma_queue *queue)
{
	struct rdma_conn_param  param;
	struct nvme_rdma_cm_rep priv;
	int ret = -ENOMEM;

	param.rnr_retry_count = 7;
	param.flow_control = 1;
	param.responder_resources = 4;
	param.initiator_depth = 4;
	param.private_data = &priv;
	param.private_data_len = sizeof(priv);
	priv.recfmt = cpu_to_le16(NVME_RDMA_CM_FMT_1_0);
	priv.rdmaqprxe = cpu_to_le16(queue->recv_queue_size);

	ret = rdma_accept(cm_id, &param);
	if (ret)
		pr_err("rdma_accept failed (error code = %d)\n", ret);

	return ret;
}

static int nvmet_rdma_queue_connect(struct rdma_cm_id *cm_id,
		struct rdma_cm_event *event)
{
	struct nvmet_rdma_device *ndev;
	struct nvmet_rdma_queue *queue;
	int ret = -EINVAL;

	ndev = nvmet_rdma_find_get_device(cm_id);
	if (!ndev) {
		pr_err("no client data!\n");
		nvmet_rdma_cm_reject(cm_id, NVME_RDMA_CM_NO_RSC);
		return -ECONNREFUSED;
	}

	queue = nvmet_rdma_alloc_queue(ndev, cm_id, event);
	if (!queue) {
		ret = -ENOMEM;
		goto put_device;
	}
	cm_id->context = queue;

	ret = nvmet_rdma_cm_accept(cm_id, queue);
	if (ret)
		goto release_queue;

	mutex_lock(&nvmet_rdma_queue_mutex);
	list_add_tail(&queue->queue_list, &nvmet_rdma_queue_list);
	mutex_unlock(&nvmet_rdma_queue_mutex);

	return 0;

release_queue:
	nvmet_rdma_free_queue(queue);
put_device:
	kref_put(&ndev->ref, nvmet_rdma_free_dev);
	cm_id->context = NULL;

	return ret;
}

static void nvmet_rdma_queue_established(struct nvmet_rdma_queue *queue)
{
	unsigned long flags;

	spin_lock_irqsave(&queue->state_lock, flags);
	if (queue->state != NVMET_RDMA_Q_CONNECTING) {
		pr_warn("trying to establish a connected queue\n");
		goto out_unlock;
	}
	queue->state = NVMET_RDMA_Q_LIVE;

	while (!list_empty(&queue->rsp_wait_list)) {
		struct nvmet_rdma_rsp *cmd;

		cmd = list_first_entry(&queue->rsp_wait_list,
					struct nvmet_rdma_rsp, wait_list);
		list_del(&cmd->wait_list);

		spin_unlock_irqrestore(&queue->state_lock, flags);
		nvmet_rdma_handle_command(queue, cmd);
		spin_lock_irqsave(&queue->state_lock, flags);
	}

out_unlock:
	spin_unlock_irqrestore(&queue->state_lock, flags);
}

static void nvmet_rdma_queue_disconnect(struct nvmet_rdma_queue *queue)
{
	bool disconnect = false;
	unsigned long flags;

	pr_debug("cm_id= %p queue->state= %d\n", queue->cm_id, queue->state);

	spin_lock_irqsave(&queue->state_lock, flags);
	switch (queue->state) {
	case NVMET_RDMA_Q_CONNECTING:
	case NVMET_RDMA_Q_LIVE:
		disconnect = true;
		queue->state = NVMET_RDMA_Q_DISCONNECTING;
		break;
	case NVMET_RDMA_Q_DISCONNECTING:
		break;
	}
	spin_unlock_irqrestore(&queue->state_lock, flags);

	if (disconnect) {
		rdma_disconnect(queue->cm_id);
		ib_drain_qp(queue->cm_id->qp);
		kref_put(&queue->ref, nvmet_rdma_queue_put);
	}
}

static void nvmet_rdma_queue_connect_fail(struct rdma_cm_id *cm_id,
		struct nvmet_rdma_queue *queue)
{
	WARN_ON_ONCE(queue->state != NVMET_RDMA_Q_CONNECTING);

	pr_err("failed to connect queue\n");
	kref_put(&queue->ref, nvmet_rdma_queue_put);
}

static int nvmet_rdma_cm_handler(struct rdma_cm_id *cm_id,
		struct rdma_cm_event *event)
{
	struct nvmet_rdma_queue *queue = cm_id->context;
	int ret = 0;

	pr_debug("%s (%d): status %d id %p\n",
		rdma_event_msg(event->event), event->event,
		event->status, cm_id);

	switch (event->event) {
	case RDMA_CM_EVENT_CONNECT_REQUEST:
		ret = nvmet_rdma_queue_connect(cm_id, event);
		break;
	case RDMA_CM_EVENT_ESTABLISHED:
		if (WARN_ON_ONCE(cm_id == nvmet_rdma_cm_id))
			break;
		nvmet_rdma_queue_established(queue);
		break;
	case RDMA_CM_EVENT_ADDR_CHANGE:
	case RDMA_CM_EVENT_DISCONNECTED:
	case RDMA_CM_EVENT_DEVICE_REMOVAL:
	case RDMA_CM_EVENT_TIMEWAIT_EXIT:
		if (WARN_ON_ONCE(cm_id == nvmet_rdma_cm_id))
			break;
		nvmet_rdma_queue_disconnect(queue);
		break;
	case RDMA_CM_EVENT_REJECTED:
	case RDMA_CM_EVENT_UNREACHABLE:
	case RDMA_CM_EVENT_CONNECT_ERROR:
		if (WARN_ON_ONCE(cm_id == nvmet_rdma_cm_id))
			break;
		nvmet_rdma_queue_connect_fail(cm_id, queue);
		break;
	default:
		pr_err("received unrecognized RDMA CM event %d\n",
			event->event);
		break;
	}

	return ret;
}

static int __init nvmet_rdma_init(void)
{
	struct sockaddr_in addr = {
		.sin_family	= AF_INET,
		.sin_port	= cpu_to_be16(nvmet_rdma_cm_port),
	};
	int ret;

	nvmet_rdma_cm_id = rdma_create_id(&init_net, nvmet_rdma_cm_handler,
			NULL, RDMA_PS_TCP, IB_QPT_RC);
	if (IS_ERR(nvmet_rdma_cm_id)) {
		pr_err("CM ID creation failed\n");
		return PTR_ERR(nvmet_rdma_cm_id);
	}

	ret = rdma_bind_addr(nvmet_rdma_cm_id, (struct sockaddr *)&addr);
	if (ret) {
		pr_err("binding CM ID to port %u failed (%d)\n",
			nvmet_rdma_cm_port, ret);
		goto out_destroy_id;
	}

	ret = rdma_listen(nvmet_rdma_cm_id, 128);
	if (ret) {
		pr_err("rdma_listen failed (%d)\n", ret);
		goto out_destroy_id;
	}

	pr_info("bound to %pISp\n", &addr);

	return 0;

out_destroy_id:
	rdma_destroy_id(nvmet_rdma_cm_id);
	return ret;
}

static void __exit nvmet_rdma_exit(void)
{
	struct nvmet_rdma_queue *queue;

	rdma_destroy_id(nvmet_rdma_cm_id);

	mutex_lock(&nvmet_rdma_queue_mutex);
	list_for_each_entry(queue, &nvmet_rdma_queue_list, queue_list)
		nvmet_rdma_queue_disconnect(queue);
	mutex_unlock(&nvmet_rdma_queue_mutex);

	flush_scheduled_work();
}

module_init(nvmet_rdma_init);
module_exit(nvmet_rdma_exit);

MODULE_LICENSE("GPL v2");
