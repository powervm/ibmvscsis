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
#ifndef _RDMA_RW_H
#define _RDMA_RW_H

#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>
#include <rdma/ib_verbs.h>
#include <rdma/rdma_cm.h>
#include <rdma/mr_pool.h>

struct rdma_rw_ctx {
	/*
	 * The scatterlist passed in, and the number of entries and total
	 * length operated on.  Note that these might be smaller than the
	 * values originally passed in if an offset or max_nents value was
	 * passed to rdma_rw_ctx_init.
	 *
	 * dma_nents is the value returned from dma_map_sg, which might be
	 * smaller than the original value passed in.
	 */
	struct scatterlist     *sg;
	u32			orig_nents;
	u32			dma_nents;

	/* data direction of the transfer */
	enum dma_data_direction dma_dir;

	/* number of RDMA READ/WRITE WRs (not counting MR WRs) */
	int			nr_wrs;

	/*
	 * The device port number pass in for the connection. Needed to call
	 * rdma_protocol_iwarp() for enabling iwarp-specific features.
	 */
	u8			port_num;

	union {
		/* for mapping a single SGE or registering a single WR: */
		struct {
			struct ib_sge		sge;
			struct ib_rdma_wr	wr;
		} single;

		/* for mapping of multiple SGEs: */
		struct {
			struct ib_sge		*sges;
			struct ib_rdma_wr	*wrs;
		} map;

		/* for registering multiple WRs: */
		struct rdma_rw_reg_ctx {
			struct ib_sge		sge;
			struct ib_rdma_wr	wr;
			struct ib_reg_wr	reg_wr;
			struct ib_send_wr	inv_wr;
			struct ib_mr		*mr;
		} *reg;
	};
};

int rdma_rw_ctx_init(struct rdma_rw_ctx *ctx, struct ib_qp *qp, u8 port_num,
		struct scatterlist *sg, u32 nents, u32 length,
		u64 remote_addr, u32 rkey, enum dma_data_direction dir,
		u32 offset);
void rdma_rw_ctx_destroy(struct rdma_rw_ctx *ctx, struct ib_qp *qp);

int rdma_rw_post(struct rdma_rw_ctx *ctx, struct ib_qp *qp, struct ib_cqe *cqe,
		struct ib_send_wr *chain_wr);

#endif /* _RDMA_RW_H */
