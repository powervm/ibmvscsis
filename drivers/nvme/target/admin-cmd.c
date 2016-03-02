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
#include <linux/module.h>
#include "nvmet.h"

static u16 nvmet_copy_to_sgl(struct nvmet_req *req, const void *buf,
		size_t len)
{
	if (sg_copy_from_buffer(req->sg, req->sg_cnt, buf, len) != len)
		return NVME_SC_SGL_INVALID_DATA | NVME_SC_DNR;
	return 0;
}

static inline u32 nvmet_get_log_page_len(struct nvme_command *cmd)
{
	u32 len = le16_to_cpu(cmd->get_log_page.numdu);

	len <<= 16;
	len += le16_to_cpu(cmd->get_log_page.numdl);
	len *= sizeof(u32);

	return len;
}

static void nvmet_execute_get_log_page(struct nvmet_req *req)
{
	size_t data_len = nvmet_get_log_page_len(req->cmd);
	void *buf;
	u16 status = 0;

	buf = kzalloc(data_len, GFP_KERNEL);
	if (!buf) {
		status = NVME_SC_INTERNAL;
		goto out;
	}

	switch (req->cmd->get_log_page.lid) {
	case 0x01:
		/*
		 * We currently never set the More bit in the status field,
		 * so all error log entries are invalid and can be zeroed out.
		 * This is called a minum viable implementation (TM) of this
		 * mandatory log page.
		 */
		break;
	case 0x02:
		/*
		 * XXX: fill out actual smart log
		 *
		 * We might have a hard time coming up with useful values for
		 * many of the fields, and even when we have useful data
		 * available (e.g. units or commands read/written) those aren't
		 * persistent over power loss.
		 */
		break;
	case 0x03:
		/*
		 * We only support a single firmware slot which always is
		 * active, so we can zero out the whole firmware slot log and
		 * still claim to fully implement this mandatory log page.
		 */
		break;
	default:
		BUG();
	}

	status = nvmet_copy_to_sgl(req, buf, data_len);

	kfree(buf);
out:
	nvmet_req_complete(req, status);
}

static void nvmet_execute_identify_ctrl(struct nvmet_req *req)
{
	struct nvmet_ctrl *ctrl = req->sq->ctrl;
	struct nvme_id_ctrl *id;
	u16 status = 0;

	id = kzalloc(sizeof(*id), GFP_KERNEL);
	if (!id) {
		status = NVME_SC_INTERNAL;
		goto out;
	}

	/* XXX: figure out how to assign real vendors IDs. */
	id->vid = 0;
	id->ssvid = 0;

	/* XXX: figure out real serial / model / revision values */
	memset(id->sn, ' ', sizeof(id->sn));
	memset(id->mn, ' ', sizeof(id->mn));
	memset(id->fr, ' ', sizeof(id->fr));
	strcpy((char *)id->mn, "NVMe Fabrics");

	id->rab = 6;

	/* XXX: figure out a real IEEE OUI */
	id->ieee[0] = 0x00;
	id->ieee[1] = 0x02;
	id->ieee[2] = 0xb3;

	/* we may have multiple controllers attached to the subsystem */
	id->mic = (1 << 1);

	/* no limit on data transfer sizes for now */
	id->mdts = 0;
	id->cntlid = cpu_to_le16(ctrl->cntlid);
	id->ver = cpu_to_le32(ctrl->subsys->ver);

	/* XXX: figure out what to do about RTD3R/RTD3 */

	id->oacs = 0;
	id->acl = 3;
	id->aerl = 3;

	/* first slot is read-only, only one slot supported */
	id->frmw = (1 << 0) | (1 << 1);
	id->lpa = 1 << 0;
#define NVMET_ERROR_LOG_SLOTS	128
	id->elpe = NVMET_ERROR_LOG_SLOTS - 1;
	id->npss = 0;

	id->sqes = (0x6 << 4) | 0x6;
	id->cqes = (0x4 << 4) | 0x4;
	id->nn = cpu_to_le32(ctrl->subsys->max_nsid);

	/* XXX: don't report vwc if the underlying device is write through */
	id->vwc = NVME_CTRL_VWC_PRESENT;

	/*
	 * We can't support atomic writes bigger than a LBA without support
	 * from the backend device.
	 */
	id->awun = 0;
	id->awupf = 0;

	/*
	 * Meh, we don't really support any power state.  Fake up the same
	 * values that qemu does.
	 */
	id->psd[0].max_power = cpu_to_le16(0x9c4);
	id->psd[0].entry_lat = cpu_to_le32(0x10);
	id->psd[0].exit_lat = cpu_to_le32(0x4);

	req->ops->identify_attrs(ctrl, id);

	status = nvmet_copy_to_sgl(req, id, sizeof(*id));

	kfree(id);
out:
	nvmet_req_complete(req, status);
}

static void nvmet_execute_identify_ns(struct nvmet_req *req)
{
	struct nvmet_ns *ns;
	struct nvme_id_ns *id;
	u16 status = 0;

	ns = nvmet_find_namespace(req->sq->ctrl, req->cmd->identify.nsid);
	if (!ns) {
		status = NVME_SC_INVALID_NS | NVME_SC_DNR;
		goto out;
	}

	id = kzalloc(sizeof(*id), GFP_KERNEL);
	if (!id) {
		status = NVME_SC_INTERNAL;
		goto out_put_ns;
	}

	/*
	 * nuse = ncap = nsze isn't aways true, but we have no way to find
	 * that out from the underlying device.
	 */
	id->ncap = id->nuse = id->nsze =
		cpu_to_le64(ns->size >> ns->blksize_shift);

	/*
	 * We just provide a single LBA format that matches what the
	 * underlying device reports.
	 */
	id->nlbaf = 0;
	id->flbas = 0;

	/*
	 * Our namespace might always be shared.  Not just with other
	 * controllers, but also with any other user of the block device.
	 */
	id->nmic = (1 << 0);

	/* XXX: provide a real nguid value! */
	memcpy(&id->nguid, &ns->nguid, sizeof(uuid_le));

	id->lbaf[0].ds = ns->blksize_shift;

	status = nvmet_copy_to_sgl(req, id, sizeof(*id));

	kfree(id);
out_put_ns:
	nvmet_put_namespace(ns);
out:
	nvmet_req_complete(req, status);
}

static void nvmet_execute_identify_nslist(struct nvmet_req *req)
{
	static const int buf_size = 4096;
	struct nvmet_ctrl *ctrl = req->sq->ctrl;
	struct nvmet_ns *ns;
	u32 min_nsid = le32_to_cpu(req->cmd->identify.nsid);
	__le32 *list;
	u16 status = 0;
	int i = 0;

	list = kzalloc(buf_size, GFP_KERNEL);
	if (!list) {
		status = NVME_SC_INTERNAL;
		goto out;
	}

	rcu_read_lock();
	list_for_each_entry_rcu(ns, &ctrl->subsys->namespaces, dev_link) {
		if (ns->nsid <= min_nsid)
			continue;
		list[i++] = cpu_to_le32(ns->nsid);
		if (i == buf_size / sizeof(__le32))
			break;
	}
	rcu_read_unlock();

	status = nvmet_copy_to_sgl(req, list, buf_size);

	kfree(list);
out:
	nvmet_req_complete(req, status);
}

static void nvmet_execute_set_features(struct nvmet_req *req)
{
	struct nvmet_subsys *subsys = req->sq->ctrl->subsys;
	u32 cdw10 = le32_to_cpu(req->cmd->common.cdw10[0]);
	u16 status = 0;

	switch (cdw10 & 0xf) {
	case NVME_FEAT_NUM_QUEUES:
		nvmet_set_result(req,
			subsys->max_qid | (subsys->max_qid << 16));
		break;
	default:
		status = NVME_SC_INVALID_FIELD | NVME_SC_DNR;
		break;
	}

	nvmet_req_complete(req, status);
}

static void nvmet_execute_get_features(struct nvmet_req *req)
{
	struct nvmet_subsys *subsys = req->sq->ctrl->subsys;
	u32 cdw10 = le32_to_cpu(req->cmd->common.cdw10[0]);
	u16 status = 0;

	switch (cdw10 & 0xf) {
	/*
	 * These features are mandatory in the spec, but we don't
	 * have a useful way to implement them.  We'll eventually
	 * need to come up with some fake values for these.
	 */
#if 0
	case NVME_FEAT_ARBITRATION:
		break;
	case NVME_FEAT_POWER_MGMT:
		break;
	case NVME_FEAT_TEMP_THRESH:
		break;
	case NVME_FEAT_ERR_RECOVERY:
		break;
	case NVME_FEAT_IRQ_COALESCE:
		break;
	case NVME_FEAT_IRQ_CONFIG:
		break;
	case NVME_FEAT_WRITE_ATOMIC:
		break;
	case NVME_FEAT_ASYNC_EVENT:
		break;
#endif
	case NVME_FEAT_VOLATILE_WC:
		nvmet_set_result(req, 1);
		break;
	case NVME_FEAT_NUM_QUEUES:
		nvmet_set_result(req,
			subsys->max_qid | (subsys->max_qid << 16));
		break;
	default:
		status = NVME_SC_INVALID_FIELD | NVME_SC_DNR;
		break;
	}

	nvmet_req_complete(req, status);
}

int nvmet_parse_admin_cmd(struct nvmet_req *req)
{
	struct nvme_command *cmd = req->cmd;

	req->ns = NULL;

	switch (cmd->common.opcode) {
	case nvme_admin_get_log_page:
		req->data_len = nvmet_get_log_page_len(cmd);

		switch (cmd->get_log_page.lid) {
		case 0x01:
		case 0x02:
		case 0x03:
			req->execute = nvmet_execute_get_log_page;
			return 0;
		}
		break;
	case nvme_admin_identify:
		req->data_len = 4096;
		switch (le32_to_cpu(cmd->identify.cns)) {
		case 0x00:
			req->execute = nvmet_execute_identify_ns;
			return 0;
		case 0x01:
			req->execute = nvmet_execute_identify_ctrl;
			return 0;
		case 0x02:
			req->execute = nvmet_execute_identify_nslist;
			return 0;
		}
		break;
#if 0
	case nvme_admin_abort_cmd:
		req->execute = nvmet_execute_abort;
		req->data_len = 0;
		return 0;
#endif
	case nvme_admin_set_features:
		req->execute = nvmet_execute_set_features;
		req->data_len = 0;
		return 0;
	case nvme_admin_get_features:
		req->execute = nvmet_execute_get_features;
		req->data_len = 0;
		return 0;
#if 0
	case nvme_admin_async_event:
		req->exectute = nvmet_execute_aen;
		req->data = 0;
		return 0;
#endif
	}

	pr_err("nvmet: unhandled cmd %d\n", cmd->common.opcode);
	return NVME_SC_INVALID_OPCODE | NVME_SC_DNR;
}
