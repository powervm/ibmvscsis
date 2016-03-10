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

static DEFINE_MUTEX(nvmet_subsystem_mutex);
static LIST_HEAD(nvmet_subsystems);

static struct nvmet_ns *__nvmet_find_namespace(struct nvmet_ctrl *ctrl,
		__le32 nsid)
{
	struct nvmet_ns *ns;

	list_for_each_entry_rcu(ns, &ctrl->subsys->namespaces, dev_link) {
		if (ns->nsid == le32_to_cpu(nsid))
			return ns;
	}

	return NULL;
}

struct nvmet_ns *nvmet_find_namespace(struct nvmet_ctrl *ctrl, __le32 nsid)
{
	struct nvmet_ns *ns;

	rcu_read_lock();
	ns = __nvmet_find_namespace(ctrl, nsid);
	if (ns)
		percpu_ref_get(&ns->ref);
	rcu_read_unlock();

	return ns;
}

static void nvmet_destroy_namespace(struct percpu_ref *ref)
{
	struct nvmet_ns *ns = container_of(ref, struct nvmet_ns, ref);

	complete(&ns->free_done);
}

void nvmet_put_namespace(struct nvmet_ns *ns)
{
	percpu_ref_put(&ns->ref);
}

int nvmet_ns_enable(struct nvmet_ns *ns, const char *path)
{
	int ret;

	mutex_lock(&ns->subsys->lock);
	ret = -EBUSY;
	if (ns->device_path)
		goto out_unlock;

	ret = -ENOMEM;
	ns->device_path = kstrdup(path, GFP_KERNEL);
	if (!ns->device_path)
		goto out_unlock;

	ns->bdev = blkdev_get_by_path(path, FMODE_READ|FMODE_WRITE, NULL);
	if (IS_ERR(ns->bdev)) {
		pr_err("nvmet: failed to open block device %s: (%ld)\n",
			path, PTR_ERR(ns->bdev));
		ret = PTR_ERR(ns->bdev);
		ns->bdev = NULL;
		goto out_free_device_path;
	}

	ns->size = i_size_read(ns->bdev->bd_inode);
	ns->blksize_shift = blksize_bits(bdev_logical_block_size(ns->bdev));

	if (ns->nsid > ns->subsys->max_nsid)
		ns->subsys->max_nsid = ns->nsid;

	list_add_rcu(&ns->dev_link, &ns->subsys->namespaces);
	mutex_unlock(&ns->subsys->lock);

	return 0;

out_free_device_path:
	kfree(ns->device_path);
	ns->device_path = NULL;
out_unlock:
	mutex_unlock(&ns->subsys->lock);
	return ret;
}

void nvmet_ns_free(struct nvmet_ns *ns)
{
	struct nvmet_subsys *subsys = ns->subsys;

	mutex_lock(&subsys->lock);
	if (!list_empty(&ns->dev_link))
		list_del_init(&ns->dev_link);
	mutex_unlock(&subsys->lock);

	/*
	 * Now that we removed the namespaces from the lookup list, we
	 * can kill the per_cpu ref and wait for any remaining references
	 * to be dropped, as well as a RCU grace period for anyone only
	 * using the namepace under rcu_read_lock().  Note that we can't
	 * use call_rcu here as we need to ensure the namespaces have
	 * been fully destroyed before unloading the module.
	 */
	percpu_ref_kill(&ns->ref);
	synchronize_rcu();
	wait_for_completion(&ns->free_done);

	if (ns->bdev)
		blkdev_put(ns->bdev, FMODE_WRITE|FMODE_READ);
	percpu_ref_exit(&ns->ref);
	kfree(ns->device_path);
	kfree(ns);
}

struct nvmet_ns *nvmet_ns_alloc(struct nvmet_subsys *subsys, u32 nsid)
{
	struct nvmet_ns *ns;
	int ret;

	ns = kzalloc(sizeof(*ns), GFP_KERNEL);
	if (!ns)
		return NULL;

	INIT_LIST_HEAD(&ns->dev_link);
	ret = percpu_ref_init(&ns->ref, nvmet_destroy_namespace,
				0, GFP_KERNEL);
	if (ret)
		goto free_ns;

	init_completion(&ns->free_done);

	ns->nsid = nsid;
	ns->subsys = subsys;

	/* XXX: Hacking nguids with uuid  */
	uuid_le_gen(&ns->nguid);

	return ns;

free_ns:
	kfree(ns);

	return NULL;
}

void __nvmet_req_complete(struct nvmet_req *req, u16 status)
{
	if (status)
		nvmet_set_status(req, status);

	/* XXX: need to fill in something useful for sq_head */
	req->rsp->sq_head = 0;
	req->rsp->sq_id = cpu_to_le16(req->sq->qid);
	req->rsp->command_id = req->cmd->common.command_id;

	if (req->ns)
		nvmet_put_namespace(req->ns);
	req->ops->queue_response(req);
}

void nvmet_req_complete(struct nvmet_req *req, u16 status)
{
	__nvmet_req_complete(req, status);
	percpu_ref_put(&req->sq->ref);
}
EXPORT_SYMBOL_GPL(nvmet_req_complete);

void nvmet_cq_setup(struct nvmet_ctrl *ctrl, struct nvmet_cq *cq,
		u16 qid, u16 size)
{
	cq->qid = qid;
	cq->size = size;

	ctrl->cqs[qid] = cq;
}

void nvmet_sq_setup(struct nvmet_ctrl *ctrl, struct nvmet_sq *sq,
		u16 qid, u16 size)
{
	sq->ctrl = ctrl;
	sq->qid = qid;
	sq->size = size;

	ctrl->sqs[qid] = sq;
}

void nvmet_sq_destroy(struct nvmet_sq *sq)
{
	percpu_ref_kill(&sq->ref);
	wait_for_completion(&sq->free_done);
	percpu_ref_exit(&sq->ref);

	if (sq->ctrl)
		nvmet_ctrl_put(sq->ctrl);
}
EXPORT_SYMBOL_GPL(nvmet_sq_destroy);

static void nvmet_sq_free(struct percpu_ref *ref)
{
	struct nvmet_sq *sq = container_of(ref, struct nvmet_sq, ref);

	complete(&sq->free_done);
}

int nvmet_sq_init(struct nvmet_sq *sq)
{
	int ret;

	ret = percpu_ref_init(&sq->ref, nvmet_sq_free, 0, GFP_KERNEL);
	if (ret) {
		printk("percpu_ref init failed!\n");
		return ret;
	}
	init_completion(&sq->free_done);

	return 0;
}
EXPORT_SYMBOL_GPL(nvmet_sq_init);

bool nvmet_req_init(struct nvmet_req *req, struct nvmet_cq *cq,
		struct nvmet_sq *sq, struct nvmet_fabrics_ops *ops)
{
	u16 status;

	req->flags = 0;
	req->cq = cq;
	req->sq = sq;
	req->ops = ops;
	req->sg = NULL;
	req->sg_cnt = 0;
	req->rsp->status = 0;

	if (unlikely(req->cmd->common.opcode == nvme_fabrics_command))
		status = nvmet_parse_fabrics_cmd(req);
	else if (unlikely(req->sq->qid == 0))
		status = nvmet_parse_admin_cmd(req);
	else
		status = nvmet_parse_io_cmd(req);

	if (status)
		goto fail;

	if (unlikely(!req->sq->ctrl && !(req->flags & NVMET_REQ_CONNECT))) {
		pr_err("queue not connected!\n");
		status = NVME_SC_INVALID_FIELD | NVME_SC_DNR;
		goto fail;
	}

	if (unlikely(!percpu_ref_tryget_live(&sq->ref))) {
		status = NVME_SC_INVALID_FIELD | NVME_SC_DNR;
		goto fail;
	}

	return true;

fail:
	__nvmet_req_complete(req, status);
	return false;
}
EXPORT_SYMBOL_GPL(nvmet_req_init);

static inline bool nvmet_cc_en(u32 cc)
{
	return cc & 0x1;
}

static inline u8 nvmet_cc_css(u32 cc)
{
	return (cc >> 4) & 0x7;
}

static inline u8 nvmet_cc_mps(u32 cc)
{
	return (cc >> 7) & 0xf;
}

static inline u8 nvmet_cc_ams(u32 cc)
{
	return (cc >> 11) & 0x7;
}

static inline u8 nvmet_cc_shn(u32 cc)
{
	return (cc >> 14) & 0x3;
}

static inline u8 nvmet_cc_iosqes(u32 cc)
{
	return (cc >> 16) & 0xf;
}

static inline u8 nvmet_cc_iocqes(u32 cc)
{
	return (cc >> 20) & 0xf;
}

static void nvmet_start_ctrl(struct nvmet_ctrl *ctrl)
{
	lockdep_assert_held(&ctrl->lock);

	if (nvmet_cc_iosqes(ctrl->cc) != NVME_NVM_IOSQES ||
	    nvmet_cc_iocqes(ctrl->cc) != NVME_NVM_IOCQES ||
	    nvmet_cc_mps(ctrl->cc) != 0 ||
	    nvmet_cc_ams(ctrl->cc) != 0 ||
	    nvmet_cc_css(ctrl->cc) != 0) {
		ctrl->csts = NVME_CSTS_CFS;
		return;
	}

	ctrl->csts = NVME_CSTS_RDY;
}

static void nvmet_clear_ctrl(struct nvmet_ctrl *ctrl)
{
	lockdep_assert_held(&ctrl->lock);

	/* XXX: tear down queues? */
	ctrl->csts &= ~NVME_CSTS_RDY;
	ctrl->cc = 0;
}

void nvmet_update_cc(struct nvmet_ctrl *ctrl, u32 new)
{
	u32 old;

	mutex_lock(&ctrl->lock);
	old = ctrl->cc;
	ctrl->cc = new;

	if (nvmet_cc_en(new) && !nvmet_cc_en(old))
		nvmet_start_ctrl(ctrl);
	if (!nvmet_cc_en(new) && nvmet_cc_en(old))
		nvmet_clear_ctrl(ctrl);
	if (nvmet_cc_shn(new) && !nvmet_cc_shn(old)) {
		nvmet_clear_ctrl(ctrl);
		ctrl->csts |= NVME_CSTS_SHST_CMPLT;
	}
	if (!nvmet_cc_shn(new) && nvmet_cc_shn(old))
		ctrl->csts &= ~NVME_CSTS_SHST_CMPLT;
	mutex_unlock(&ctrl->lock);
}

static void nvmet_init_cap(struct nvmet_ctrl *ctrl)
{
	/* command sets supported: NVMe command set: */
	ctrl->cap = (1ULL << 37);
	/* CC.EN timeout in 500msec units: */
	ctrl->cap |= (15ULL << 24);
	/* maximum queue entries supported: */
	ctrl->cap |= NVMET_QUEUE_SIZE - 1;
}

struct nvmet_ctrl *nvmet_ctrl_find_get(struct nvmet_subsys *subsys, u16 cntlid)
{
	struct nvmet_ctrl *ctrl;

	lockdep_assert_held(&subsys->lock);

	list_for_each_entry(ctrl, &subsys->ctrls, subsys_entry) {
		if (ctrl->cntlid == cntlid) {
			if (kref_get_unless_zero(&ctrl->ref))
				return ctrl;
			return NULL;
		}
	}

	return NULL;
}

struct nvmet_ctrl *nvmet_alloc_ctrl(struct nvmet_subsys *subsys,
		const char *subsys_name, const char *hostnqn)
{
	struct nvmet_ctrl *ctrl;
	int ret = -ENOMEM;

	lockdep_assert_held(&subsys->lock);

	ctrl = kzalloc(sizeof(*ctrl), GFP_KERNEL);
	if (!ctrl)
		goto out;
	mutex_init(&ctrl->lock);

	nvmet_init_cap(ctrl);

	memcpy(ctrl->subsys_name, subsys_name, NVMF_NQN_SIZE);
	memcpy(ctrl->hostnqn, hostnqn, NVMF_NQN_SIZE);

	kref_init(&ctrl->ref);
	ctrl->subsys = subsys;

	ctrl->cqs = kcalloc(subsys->max_qid + 1,
			sizeof(struct nvmet_cq *),
			GFP_KERNEL);
	if (!ctrl->cqs)
		goto out_free_ctrl;

	ctrl->sqs = kcalloc(subsys->max_qid + 1,
			sizeof(struct nvmet_sq *),
			GFP_KERNEL);
	if (!ctrl->sqs)
		goto out_free_cqs;

	ctrl->cntlid = ida_simple_get(&subsys->cntlid_ida, 0, 0xffef,
			GFP_KERNEL);
	if (ctrl->cntlid < 0) {
		ret = ctrl->cntlid;
		goto out_free_sqs;
	}

	list_add_tail(&ctrl->subsys_entry, &subsys->ctrls);
	return ctrl;

out_free_sqs:
	kfree(ctrl->sqs);
out_free_cqs:
	kfree(ctrl->cqs);
out_free_ctrl:
	kfree(ctrl);
out:
	return ERR_PTR(ret);
}

static void nvmet_ctrl_free(struct kref *ref)
{
	struct nvmet_ctrl *ctrl = container_of(ref, struct nvmet_ctrl, ref);
	struct nvmet_subsys *subsys = ctrl->subsys;

	mutex_lock(&subsys->lock);
	list_del(&ctrl->subsys_entry);
	mutex_unlock(&subsys->lock);

	nvmet_subsys_put(subsys);
	ida_simple_remove(&subsys->cntlid_ida, ctrl->cntlid);

	kfree(ctrl->sqs);
	kfree(ctrl->cqs);
	kfree(ctrl);
}

void nvmet_ctrl_put(struct nvmet_ctrl *ctrl)
{
	kref_put(&ctrl->ref, nvmet_ctrl_free);
}

struct nvmet_subsys *nvmet_find_get_subsys(char *subsys_name)
{
	struct nvmet_subsys *subsys;

	mutex_lock(&nvmet_subsystem_mutex);
	list_for_each_entry(subsys, &nvmet_subsystems, entry) {
		if (!strncmp(subsys->subsys_name, subsys_name,
				NVMF_NQN_SIZE)) {
			if (!kref_get_unless_zero(&subsys->ref))
				break;
			mutex_unlock(&nvmet_subsystem_mutex);
			return subsys;
		}
	}
	mutex_unlock(&nvmet_subsystem_mutex);
	return NULL;
}

struct nvmet_subsys *nvmet_subsys_alloc(const char *subsys_name)
{
	struct nvmet_subsys *subsys;

	subsys = kzalloc(sizeof(*subsys), GFP_KERNEL);
	if (!subsys)
		return NULL;

	subsys->ver = (1 << 16) | (2 << 8) | 1; /* NVMe 1.2.1 */
	subsys->subsys_name = kstrndup(subsys_name, NVMF_NQN_SIZE,
			GFP_KERNEL);
	if (IS_ERR(subsys->subsys_name)) {
		kfree(subsys);
		return NULL;
	}

	kref_init(&subsys->ref);

	mutex_init(&subsys->lock);
	INIT_LIST_HEAD(&subsys->namespaces);
	INIT_LIST_HEAD(&subsys->ctrls);

	ida_init(&subsys->cntlid_ida);
	subsys->max_qid = NVMET_NR_QUEUES;

	mutex_lock(&nvmet_subsystem_mutex);
	list_add_tail(&subsys->entry, &nvmet_subsystems);
	mutex_unlock(&nvmet_subsystem_mutex);

	return subsys;
}

static void nvmet_subsys_free(struct kref *ref)
{
	struct nvmet_subsys *subsys = container_of(ref, struct nvmet_subsys, ref);

	WARN_ON_ONCE(!list_empty(&subsys->namespaces));

	mutex_lock(&nvmet_subsystem_mutex);
	list_del(&subsys->entry);
	mutex_unlock(&nvmet_subsystem_mutex);

	kfree(subsys->subsys_name);
	kfree(subsys);
}

void nvmet_subsys_put(struct nvmet_subsys *subsys)
{
	kref_put(&subsys->ref, nvmet_subsys_free);
}

static int __init nvmet_init(void)
{
	return nvmet_init_configfs();
}

static void __exit nvmet_exit(void)
{
	nvmet_exit_configfs();
}

module_init(nvmet_init);
module_exit(nvmet_exit);

MODULE_LICENSE("GPL v2");
