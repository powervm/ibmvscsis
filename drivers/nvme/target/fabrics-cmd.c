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
#include <linux/blkdev.h>
#include "nvmet.h"

static void nvmet_execute_prop_set(struct nvmet_req *req)
{
	struct nvmf_prop_set_command *c = &req->cmd->prop_set;
	u16 status = 0;

	if (!(c->attrib & 1)) {
		u64 val = le64_to_cpu(c->value);

		switch (le32_to_cpu(c->offset)) {
		case NVME_REG_CC:
			nvmet_update_cc(req->sq->ctrl, val);
			break;
		case NVME_REG_AQA:
			/* ignore */
			break;
		default:
			status = NVME_SC_INVALID_FIELD | NVME_SC_DNR;
			break;
		}
	} else {
		status = NVME_SC_INVALID_FIELD | NVME_SC_DNR;
	}

	nvmet_req_complete(req, status);
}

static void nvmet_execute_prop_get(struct nvmet_req *req)
{
	struct nvmf_prop_get_command *c = &req->cmd->prop_get;
	struct nvmet_ctrl *ctrl = req->sq->ctrl;
	u16 status = 0;
	u64 val = 0;

	if (c->attrib & 1) {
		switch (le32_to_cpu(c->offset)) {
		case NVME_REG_CAP:
			val = ctrl->cap;
			break;
		default:
			status = NVME_SC_INVALID_FIELD | NVME_SC_DNR;
			break;
		}
	} else {
		switch (le32_to_cpu(c->offset)) {
		case NVME_REG_VS:
			val = ctrl->subsys->ver;
			break;
		case NVME_REG_CC:
			val = ctrl->cc;
			break;
		case NVME_REG_CSTS:
			val = ctrl->csts;
			break;
		case NVME_REG_AQA:
			val = (NVMF_AQ_DEPTH - 1) |
			      (((NVMF_AQ_DEPTH - 1) << 16));
			break;
		case NVME_REG_PROPSZ:
			val = (NVME_REG_MAX + 64) / 64;
			break;
		default:
			status = NVME_SC_INVALID_FIELD | NVME_SC_DNR;
			break;
		}
	}

	req->rsp->result64 = cpu_to_le64(val);
	nvmet_req_complete(req, status);
}

static void nvmet_execute_connect(struct nvmet_req *req)
{
	struct nvmf_connect_command *c = &req->cmd->connect;
	struct nvmf_connect_data *d;
	struct nvmet_subsys *subsys;
	struct nvmet_ctrl *ctrl = NULL;
	u16 status = 0;
	u16 cntlid, qid;

	d = kmap(sg_page(req->sg)) + req->sg->offset;

	if (c->recfmt != 0) {
		pr_warn("invalid connect version (%d).\n",
			le16_to_cpu(c->recfmt));
		status = NVME_SC_CONNECT_FORMAT | NVME_SC_DNR;
		goto out;
	}

	subsys = nvmet_find_get_subsys(d->subsysnqn);
	if (!subsys) {
		pr_warn("connect request for invalid subsystem!\n");
		status = NVME_SC_INVALID_FIELD | NVME_SC_DNR;
		goto out;
	}

	cntlid = le16_to_cpu(d->cntlid);
	qid = le16_to_cpu(c->qid);

	mutex_lock(&subsys->lock);
	if (req->sq->ctrl) {
		pr_warn("queue already connected!\n");
		status = NVME_SC_CONNECT_CTRL_BUSY | NVME_SC_DNR;
		goto out_unlock;
	}

	ctrl = nvmet_ctrl_find_get(subsys, cntlid);
	if (ctrl) {
		if (qid == 0) {
			pr_warn("connect for admin queue on active ctrl.\n");
			status = NVME_SC_CONNECT_CTRL_BUSY | NVME_SC_DNR;
			goto out_ctrl_put;
		}

		if (qid > ctrl->subsys->max_qid) {
			pr_warn("invalid queue id (%d)\n", qid);
			status = NVME_SC_INVALID_FIELD | NVME_SC_DNR;
			goto out_ctrl_put;
		}

		if (strncmp(d->hostnqn, ctrl->hostnqn, NVMF_NQN_SIZE)) {
			pr_warn("hostnqn mismatch.\n");
			status = NVME_SC_INVALID_FIELD | NVME_SC_DNR;
			goto out_ctrl_put;
		}

		pr_info("adding queue %d to ctrl %d.\n", qid, ctrl->cntlid);
	} else {
		if (qid != 0) {
			pr_warn("connect for I/O queue before admin queue.\n");
			status = NVME_SC_INVALID_FIELD | NVME_SC_DNR;
			goto out_unlock;
		}

		if (cntlid != 0xffff) {
			pr_warn("reconnect not yet supported!\n");
			status = NVME_SC_CONNECT_CTRL_BUSY | NVME_SC_DNR;
			goto out_unlock;
		}

		ctrl = nvmet_alloc_ctrl(subsys, d->subsysnqn, d->hostnqn);
		if (IS_ERR(ctrl)) {
			status = NVME_SC_CONNECT_CTRL_BUSY | NVME_SC_DNR;
			goto out_unlock;
		}

		pr_info("creating controller %d for NQN %s.\n",
			ctrl->cntlid, ctrl->hostnqn);
	}

	nvmet_cq_setup(ctrl, req->cq, qid, le16_to_cpu(c->sqsize));
	nvmet_sq_setup(ctrl, req->sq, qid, le16_to_cpu(c->sqsize));

	mutex_unlock(&subsys->lock);

out:
	req->rsp->result16 = ctrl ? cpu_to_le16(ctrl->cntlid) : 0;
	kunmap(sg_page(req->sg));

	/*
	 * Just to make life complicated, NVME_SC_INVALID_FIELD has a different
	 * name for Connect only..
	 */
	if (status == NVME_SC_INVALID_FIELD)
		status = NVME_SC_CONNECT_INVALID_PARAM;
	nvmet_req_complete(req, status);
	return;

out_ctrl_put:
	nvmet_ctrl_put(ctrl);
out_unlock:
	mutex_unlock(&subsys->lock);
	nvmet_subsys_put(subsys);
	goto out;
}

int nvmet_parse_fabrics_cmd(struct nvmet_req *req)
{
	struct nvme_command *cmd = req->cmd;

	req->ns = NULL;

	switch (cmd->fabrics.fctype) {
	case NVMF_CC_PROP_SET:
		req->data_len = 0;
		req->execute = nvmet_execute_prop_set;
		break;
	case NVMF_CC_PROP_GET:
		req->data_len = 0;
		req->execute = nvmet_execute_prop_get;
		break;
	case NVMF_CC_CONNECT:
		req->data_len = sizeof(struct nvmf_connect_data);
		req->execute = nvmet_execute_connect;
		req->flags |= NVMET_REQ_CONNECT;
		break;
	default:
		pr_err("received unknown capsule type 0x%x\n",
			cmd->fabrics.fctype);
		return NVME_SC_INVALID_OPCODE | NVME_SC_DNR;
	}

	return 0;
}
