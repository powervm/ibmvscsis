/*
 * Copyright (c) 2016 HGST, a Western Digital Company.
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
#include <linux/slab.h>
#include <rdma/mr_pool.h>
#include "rw.h"

static inline u32 rdma_max_sge(struct rdma_rw_ctx *ctx, struct ib_device *dev)
{
	return ctx->dma_dir == DMA_TO_DEVICE ? dev->attrs.max_sge : dev->attrs.max_sge_rd;
}

static int rdma_rw_init_single_wr(struct rdma_rw_ctx *ctx, struct ib_qp *qp,
		u64 remote_addr, u32 rkey)
{
	struct ib_device *dev = qp->pd->device;
	struct ib_rdma_wr *rdma_wr = &ctx->single.wr;

	ctx->nr_wrs = 1;

	ctx->single.sge.lkey = qp->pd->local_dma_lkey;
	ctx->single.sge.addr = ib_sg_dma_address(dev, ctx->sg);
	ctx->single.sge.length = ib_sg_dma_len(dev, ctx->sg);

	memset(rdma_wr, 0, sizeof(*rdma_wr));
	rdma_wr->wr.opcode = ctx->dma_dir == DMA_TO_DEVICE ?
			IB_WR_RDMA_WRITE : IB_WR_RDMA_READ;
	rdma_wr->wr.sg_list = &ctx->single.sge;
	rdma_wr->wr.num_sge = 1;
	rdma_wr->remote_addr = remote_addr;
	rdma_wr->rkey = rkey;

	return 1;
}

static int rdma_rw_build_sg_list(struct rdma_rw_ctx *ctx, struct ib_pd *pd,
		struct ib_sge *sge, u32 data_left, u32 offset)
{
	struct scatterlist *sg;
	u32 sg_nents = min(ctx->dma_nents, rdma_max_sge(ctx, pd->device));
	u32 page_off = offset % PAGE_SIZE;
	int i;

	for_each_sg(ctx->sg, sg, sg_nents, i) {
		sge->addr = ib_sg_dma_address(pd->device, sg) + page_off;
		sge->length = min_t(u32, data_left,
				ib_sg_dma_len(pd->device, sg) - page_off);
		sge->lkey = pd->local_dma_lkey;

		page_off = 0;
		data_left -= sge->length;
		if (!data_left)
			break;
		sge++;
	}

	return i + 1;
}

static int rdma_rw_init_wrs(struct rdma_rw_ctx *ctx, struct ib_qp *qp,
		u64 remote_addr, u32 rkey, u32 length, u32 offset)
{
	u32 max_sge = rdma_max_sge(ctx, qp->pd->device);
	u32 rdma_write_max = max_sge * PAGE_SIZE;
	struct ib_sge *sge;
	u32 va_offset = 0, i;

	ctx->map.sges = sge =
		kcalloc(ctx->dma_nents, sizeof(*ctx->map.sges), GFP_KERNEL);
	if (!ctx->map.sges)
		goto out;

	ctx->nr_wrs = DIV_ROUND_UP(ctx->dma_nents, max_sge);
	ctx->map.wrs = kcalloc(ctx->nr_wrs, sizeof(*ctx->map.wrs), GFP_KERNEL);
	if (!ctx->map.wrs)
		goto out_free_sges;

	for (i = 0; i < ctx->nr_wrs; i++) {
		struct ib_rdma_wr *rdma_wr = &ctx->map.wrs[i];
		u32 data_len = min(length - va_offset, rdma_write_max);

		rdma_wr->wr.opcode = ctx->dma_dir == DMA_TO_DEVICE ?
				IB_WR_RDMA_WRITE : IB_WR_RDMA_READ;
		rdma_wr->wr.sg_list = sge;
		rdma_wr->wr.num_sge = rdma_rw_build_sg_list(ctx, qp->pd, sge,
				data_len, offset + va_offset);
		rdma_wr->remote_addr = remote_addr + va_offset;
		rdma_wr->rkey = rkey;

		if (i + 1 != ctx->nr_wrs)
			rdma_wr->wr.next = &ctx->map.wrs[i + 1].wr;

		sge += rdma_wr->wr.num_sge;
		va_offset += data_len;
	}

	return ctx->nr_wrs;

out_free_sges:
	kfree(ctx->map.sges);
out:
	return -ENOMEM;
}

static int rdma_rw_init_mr_wrs(struct rdma_rw_ctx *ctx, struct ib_qp *qp,
		u64 remote_addr, u32 rkey)
{
	int pages_per_mr = qp->pd->device->attrs.max_fast_reg_page_list_len;
	int pages_left = ctx->dma_nents;
	struct scatterlist *sg = ctx->sg;
	bool use_read_w_invalidate = ctx->dma_dir == DMA_FROM_DEVICE &&
				rdma_protocol_iwarp(qp->device, ctx->port_num);
	u32 va_offset = 0;
	int i, ret = 0, count = 0;

	ctx->nr_wrs = (ctx->dma_nents + pages_per_mr - 1) / pages_per_mr;
	ctx->reg = kcalloc(ctx->nr_wrs, sizeof(*ctx->reg), GFP_KERNEL);
	if (!ctx->reg) {
		ret = -ENOMEM;
		goto out;
	}

	for (i = 0; i < ctx->nr_wrs; i++) {
		struct rdma_rw_reg_ctx *reg = &ctx->reg[i];
		int nents = min(pages_left, pages_per_mr);

		reg->mr = ib_mr_pool_get(qp);
		if (!reg->mr) {
			pr_info("failed to allocate MR from pool\n");
			ret = -EAGAIN;
			goto out_free;
		}

		ib_update_fast_reg_key(reg->mr, ib_inc_rkey(reg->mr->lkey));

		/* XXX: what about a non page sized offset into the SG? */
		ret = ib_map_mr_sg(reg->mr, sg, nents, PAGE_SIZE);
		if (ret < nents) {
			pr_info("failed to map MR\n");
			ib_mr_pool_put(qp, reg->mr);
			ret = -EINVAL;
			goto out_free;
		}

		reg->reg_wr.wr.opcode = IB_WR_REG_MR;
		reg->reg_wr.mr = reg->mr;
		reg->reg_wr.key = reg->mr->lkey;

		reg->reg_wr.access = IB_ACCESS_LOCAL_WRITE;
		if (use_read_w_invalidate)
			reg->reg_wr.access |= IB_ACCESS_REMOTE_WRITE;

		reg->sge.lkey = reg->mr->lkey;
		reg->sge.addr = reg->mr->iova;
		reg->sge.length = reg->mr->length;

		reg->wr.wr.sg_list = &reg->sge;
		reg->wr.wr.num_sge = 1;
		reg->wr.remote_addr = remote_addr + va_offset;
		reg->wr.rkey = rkey;

		if (use_read_w_invalidate) {
			reg->wr.wr.opcode = IB_WR_RDMA_READ_WITH_INV;
			reg->wr.wr.ex.invalidate_rkey = reg->mr->lkey;

			count += 2; /* REG_MR + READ_W_INV  */
		} else {
			if (ctx->dma_dir == DMA_TO_DEVICE)
				reg->wr.wr.opcode = IB_WR_RDMA_WRITE;
			else
				reg->wr.wr.opcode = IB_WR_RDMA_READ;

			reg->inv_wr.opcode = IB_WR_LOCAL_INV;
			reg->inv_wr.ex.invalidate_rkey = reg->mr->lkey;

			/*
			 * FIXME: IB_SEND_FENCE can stall SQ processing.
			 * The LINV WR should be posted after the RDMA
			 * WR completes instead.
			 */
			if (i == 0)
				reg->inv_wr.send_flags |= IB_SEND_FENCE;

			count += 3; /* REG_MR + READ + LOCAL_INV */
		}

		if (i + 1 == ctx->nr_wrs) {
			struct rdma_rw_reg_ctx *first = &ctx->reg[0];

			reg->reg_wr.wr.next = &first->wr.wr;
			if (!use_read_w_invalidate)
				reg->wr.wr.next = &first->inv_wr;
		} else {
			struct rdma_rw_reg_ctx *next = &ctx->reg[i + 1];

			reg->reg_wr.wr.next = &next->reg_wr.wr;
			reg->wr.wr.next = &next->wr.wr;
			if (!use_read_w_invalidate)
				reg->inv_wr.next = &next->inv_wr;
		}

		va_offset += reg->sge.length;
		pages_left -= nents;
		sg += nents; // XXX: use accessors for chained SGLs
	}

	return count;

out_free:
	while (--i >= 0)
		ib_mr_pool_put(qp, ctx->reg[i].mr);
	kfree(ctx->reg);
out:
	return ret;
}

/**
 * rdma_rw_ctx_init - initialize a RDMA READ/WRITE context
 * @ctx:	context to initialize
 * @qp:		queue pair to operate on
 * @port_num:	port num to which the connection is bound
 * @sg:		scatterlist to READ/WRITE from/to
 * @nents:	number of entries in @sg
 * @total_len:	total length of @sg in bytes
 * @remote_addr:remote address to read/write (relative to @rkey)
 * @rkey:	remote key to operate on
 * @dir:	%DMA_TO_DEVICE for RDMA WRITE, %DMA_FROM_DEVICE for RDMA READ
 * @offset:	current byte offset into @sg
 *
 * If we're going to use a FR to map this context @max_nents should be smaller
 * or equal to the MR size.
 *
 * Returns the number of WRs that will be needed on the workqueue if successful,
 * or a negative error code.
 */
int rdma_rw_ctx_init(struct rdma_rw_ctx *ctx, struct ib_qp *qp, u8 port_num,
		struct scatterlist *sg, u32 nents, u32 total_len,
		u64 remote_addr, u32 rkey, enum dma_data_direction dir,
		u32 offset)
{
	struct ib_device *dev = qp->pd->device;
	u32 first_sg_index = offset / PAGE_SIZE;
	int ret = -ENOMEM;

	ctx->sg = sg + first_sg_index;
	ctx->dma_dir = dir;

	ctx->orig_nents = nents - first_sg_index;
	ctx->dma_nents =
		ib_dma_map_sg(dev, ctx->sg, ctx->orig_nents, ctx->dma_dir);
	if (!ctx->dma_nents)
		goto out;

	ctx->port_num = port_num;
	if (rdma_protocol_iwarp(qp->device, ctx->port_num))
		ret = rdma_rw_init_mr_wrs(ctx, qp, remote_addr, rkey);
	else if (ctx->dma_nents == 1)
		ret = rdma_rw_init_single_wr(ctx, qp, remote_addr, rkey);
	else
		ret = rdma_rw_init_wrs(ctx, qp, remote_addr, rkey,
				total_len - offset, offset);

	if (ret < 0)
		goto out_unmap_sg;

	return ret;

out_unmap_sg:
	ib_dma_unmap_sg(dev, ctx->sg, ctx->orig_nents, ctx->dma_dir);
out:
	return ret;
}
EXPORT_SYMBOL(rdma_rw_ctx_init);

/**
 * rdma_rw_ctx_destroy - release all resources allocated by rdma_rw_ctx_init
 * @ctx:	context to release
 * @qp:		queue pair to operate on
 */
void rdma_rw_ctx_destroy(struct rdma_rw_ctx *ctx, struct ib_qp *qp)
{
	if (rdma_protocol_iwarp(qp->device, ctx->port_num)) {
		int i;

		for (i = 0; i < ctx->nr_wrs; i++)
			ib_mr_pool_put(qp, ctx->reg[i].mr);
		kfree(ctx->reg);
	} else if (ctx->dma_nents > 1) {
		kfree(ctx->map.wrs);
		kfree(ctx->map.sges);
	}

	ib_dma_unmap_sg(qp->pd->device, ctx->sg, ctx->orig_nents, ctx->dma_dir);
}
EXPORT_SYMBOL(rdma_rw_ctx_destroy);

/**
 * rdma_rw_post - post a RDMA READ or RDMA WRITE operation
 * @ctx:		context to operate on
 * @qp:			queue pair to operate on
 * @cqe:		completion queue entry for the last WR
 * @chain_wr:		WR to append to the posted chain
 *
 * Post the set of RDMA READ/WRITE operations described by @ctx, as well as
 * any memory registration operations needed.  If @chain_wr is non-NULL the
 * WR it points to will be appended to the chain of WRs posted.  If @chain_wr
 * is not set @cqe must be set so that the caller gets a completion
 * notification.
 */
int rdma_rw_post(struct rdma_rw_ctx *ctx, struct ib_qp *qp, struct ib_cqe *cqe,
		struct ib_send_wr *chain_wr)
{
	struct ib_send_wr *first_wr, *last_wr, *bad_wr;

	if (rdma_protocol_iwarp(qp->device, ctx->port_num)) {
		first_wr = &ctx->reg[0].reg_wr.wr;
		if (ctx->dma_dir == DMA_FROM_DEVICE &&
		    rdma_protocol_iwarp(qp->device, ctx->port_num))
			last_wr = &ctx->reg[ctx->nr_wrs - 1].wr.wr;
		else
			last_wr = &ctx->reg[ctx->nr_wrs - 1].inv_wr;
	} else if (ctx->dma_nents == 1) {
		first_wr = &ctx->single.wr.wr;
		last_wr = &ctx->single.wr.wr;
	} else {
		first_wr = &ctx->map.wrs[0].wr;
		last_wr = &ctx->map.wrs[ctx->nr_wrs - 1].wr;
	}

	if (chain_wr) {
		last_wr->next = chain_wr;
	} else {
		last_wr->wr_cqe = cqe;
		last_wr->send_flags |= IB_SEND_SIGNALED;
	}

	return ib_post_send(qp, first_wr, &bad_wr);
}
EXPORT_SYMBOL(rdma_rw_post);
