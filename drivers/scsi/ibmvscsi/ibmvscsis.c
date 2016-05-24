/*******************************************************************************
 * IBM Virtual SCSI Target Driver
 * Copyright (C) 2003-2005 Dave Boutcher (boutcher@us.ibm.com) IBM Corp.
 *			   Santiago Leon (santil@us.ibm.com) IBM Corp.
 *			   Linda Xie (lxie@us.ibm.com) IBM Corp.
 *
 * Copyright (C) 2005-2011 FUJITA Tomonori <tomof@acm.org>
 * Copyright (C) 2010 Nicholas A. Bellinger <nab@kernel.org>
 * Copyright (C) 2016 Bryant G. Ly <bgly@us.ibm.com> IBM Corp.
 *
 * Authors: Bryant G. Ly <bryantly@linux.vnet.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 ****************************************************************************/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/utsname.h>
#include <asm/unaligned.h>
#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_tcq.h>
#include <scsi/libsrp.h>
#include <generated/utsrelease.h>

#include <target/target_core_base.h>
#include <target/target_core_fabric.h>
#include <target/target_core_backend.h>

#include <asm/hvcall.h>
#include <asm/iommu.h>
#include <asm/prom.h>
#include <asm/vio.h>

#include "ibmvscsi.h"
#include "ibmvscsis.h"
#include "viosrp.h"

#define IBMVSCSIS_VERSION	"v0.1"

#define	INITIAL_SRP_LIMIT	15
#define	DEFAULT_MAX_SECTORS	256

#define MAX_H_COPY_RDMA		(128*1024)

#define SRP_RSP_SENSE_DATA_LEN	18

static struct workqueue_struct *vtgtd;
static unsigned max_vdma_size = MAX_H_COPY_RDMA;

static DEFINE_SPINLOCK(ibmvscsis_dev_lock);
static LIST_HEAD(ibmvscsis_dev_list);

static int ibmvscsis_probe(struct vio_dev *vdev,
			   const struct vio_device_id *id);
static void ibmvscsis_dev_release(struct device *dev);
static void ibmvscsis_modify_rep_luns(struct se_cmd *se_cmd);
static void ibmvscsis_modify_std_inquiry(struct se_cmd *se_cmd);
static int read_dma_window(struct vio_dev *vdev,
				struct ibmvscsis_adapter *adapter);
static char *ibmvscsis_get_fabric_name(void);
static char *ibmvscsis_get_fabric_wwn(struct se_portal_group *se_tpg);
static u16 ibmvscsis_get_tag(struct se_portal_group *se_tpg);
static u32 ibmvscsis_get_default_depth(struct se_portal_group *se_tpg);
static int ibmvscsis_check_true(struct se_portal_group *se_tpg);
static int ibmvscsis_check_false(struct se_portal_group *se_tpg);
static u32 ibmvscsis_tpg_get_inst_index(struct se_portal_group *se_tpg);
static int ibmvscsis_check_stop_free(struct se_cmd *se_cmd);
static void ibmvscsis_release_cmd(struct se_cmd *se_cmd);
static int ibmvscsis_shutdown_session(struct se_session *se_sess);
static void ibmvscsis_close_session(struct se_session *se_sess);
static u32 ibmvscsis_sess_get_index(struct se_session *se_sess);
static int ibmvscsis_write_pending(struct se_cmd *se_cmd);
static int ibmvscsis_write_pending_status(struct se_cmd *se_cmd);
static void ibmvscsis_set_default_node_attrs(struct se_node_acl *nacl);
static int ibmvscsis_get_cmd_state(struct se_cmd *se_cmd);
static int ibmvscsis_queue_data_in(struct se_cmd *se_cmd);
static int ibmvscsis_queue_status(struct se_cmd *se_cmd);
static void ibmvscsis_queue_tm_rsp(struct se_cmd *se_cmd);
static void ibmvscsis_aborted_task(struct se_cmd *se_cmd);
static struct se_wwn *ibmvscsis_make_tport(struct target_fabric_configfs *tf,
					   struct config_group *group,
					   const char *name);
static void ibmvscsis_drop_tport(struct se_wwn *wwn);
static struct se_portal_group *ibmvscsis_make_tpg(struct se_wwn *wwn,
						  struct config_group *group,
						  const char *name);
static void ibmvscsis_drop_tpg(struct se_portal_group *se_tpg);
static int ibmvscsis_remove(struct vio_dev *vdev);
static ssize_t system_id_show(struct device *dev,
			      struct device_attribute *attr,
			      char *buf);
static ssize_t partition_number_show(struct device *dev,
				     struct device_attribute *attr,
				     char *buf);
static ssize_t unit_address_show(struct device *dev,
				 struct device_attribute *attr, char *buf);
static int get_system_info(void);
static irqreturn_t ibmvscsis_interrupt(int dummy, void *data);
static int process_srp_iu(struct iu_entry *iue);
static void process_iu(struct viosrp_crq *crq,
		       struct ibmvscsis_adapter *adapter);
static void process_crq(struct viosrp_crq *crq,
			struct ibmvscsis_adapter *adapter);
static void handle_crq(struct work_struct *work);
static int ibmvscsis_reset_crq_queue(struct ibmvscsis_adapter *adapter);
static void crq_queue_destroy(struct ibmvscsis_adapter *adapter);
static inline struct viosrp_crq *next_crq(struct crq_queue *queue);
static int send_iu(struct iu_entry *iue, u64 length, u8 format);
static int send_adapter_info(struct iu_entry *iue,
			     dma_addr_t remote_buffer, u16 length);
static int process_mad_iu(struct iu_entry *iue);
static void ibmvscsis_srp_i_logout(struct iu_entry *iue);
static void process_login(struct iu_entry *iue);
static void process_tsk_mgmt(struct ibmvscsis_adapter *adapter,
			     struct iu_entry *iue);
static int ibmvscsis_rdma(struct scsi_cmnd *sc, struct scatterlist *sg,
			  int nsg, struct srp_direct_buf *md, int nmd,
			  enum dma_data_direction dir, unsigned int rest);
static int ibmvscsis_queuecommand(struct ibmvscsis_adapter *adapter,
				  struct iu_entry *iue);
static uint64_t ibmvscsis_unpack_lun(const uint8_t *lun, int len);
static int tcm_queuecommand(struct ibmvscsis_adapter *adapter,
			    struct ibmvscsis_cmnd *vsc,
			    struct srp_cmd *scmd);
static void ibmvscsis_determine_resid(struct se_cmd *se_cmd,
				      struct srp_rsp *rsp);
static bool connection_broken(struct ibmvscsis_adapter *adapter);

static inline long h_copy_rdma(s64 length, u64 sliobn, u64 slioba,
	u64 dliobn, u64 dlioba)
{
	long rc = 0;

	/* Ensure all writes to source memory are visible before hcall */
	mb();

	rc = plpar_hcall_norets(H_COPY_RDMA, length, sliobn, slioba,
			dliobn, dlioba);
	return rc;
}

static inline void h_free_crq(uint32_t unit_address)
{
	long rc = 0;

	do {
		if (H_IS_LONG_BUSY(rc))
			msleep(get_longbusy_msecs(rc));

		rc = plpar_hcall_norets(H_FREE_CRQ, unit_address);
	} while ((rc == H_BUSY) || (H_IS_LONG_BUSY(rc)));
}

static inline long h_send_crq(struct ibmvscsis_adapter *adapter,
			u64 word1, u64 word2)
{
	long rc;
	struct vio_dev *vdev = adapter->dma_dev;

	pr_debug("ibmvscsis: ibmvscsis_send_crq(0x%x, 0x%016llx, 0x%016llx)\n",
			vdev->unit_address, word1, word2);

	/*
	 * Ensure the command buffer is flushed to memory before handing it
	 * over to the other side to prevent it from fetching any stale data.
	 */
	mb();
	rc = plpar_hcall_norets(H_SEND_CRQ, vdev->unit_address, word1, word2);
	pr_debug("ibmvscsis: ibmvcsis_send_crq rc = 0x%lx\n", rc);

	return rc;
}

/*****************************************************************************/
/* Global device driver data areas                                           */
/*****************************************************************************/

static const char ibmvscsis_driver_name[] = "ibmvscsis";
static char system_id[64] = "";
static char partition_name[97] = "UNKNOWN";
static unsigned int partition_number = -1;

static struct class_attribute ibmvscsis_class_attrs[] = {
	__ATTR_NULL,
};

static struct device_attribute dev_attr_system_id =
	__ATTR(system_id, S_IRUGO, system_id_show, NULL);

static struct device_attribute dev_attr_partition_number =
	__ATTR(partition_number, S_IRUGO, partition_number_show, NULL);

static struct device_attribute dev_attr_unit_address =
	__ATTR(unit_address, S_IRUGO, unit_address_show, NULL);

static struct attribute *ibmvscsis_dev_attrs[] = {
	&dev_attr_system_id.attr,
	&dev_attr_partition_number.attr,
	&dev_attr_unit_address.attr,
};
ATTRIBUTE_GROUPS(ibmvscsis_dev);

static struct class ibmvscsis_class = {
	.name           = "ibmvscsis",
	.dev_release    = ibmvscsis_dev_release,
	.class_attrs    = ibmvscsis_class_attrs,
	.dev_groups     = ibmvscsis_dev_groups,
};

static ssize_t ibmvscsis_wwn_version_show(struct config_item *item,
					       char *page)
{
	return sprintf(page, "IBMVSCSIS fabric %s on %s/%s on "UTS_RELEASE"\n",
		       IBMVSCSIS_VERSION, utsname()->sysname,
		       utsname()->machine);
}
CONFIGFS_ATTR_RO(ibmvscsis_wwn_, version);

static struct configfs_attribute *ibmvscsis_wwn_attrs[] = {
	&ibmvscsis_wwn_attr_version,
	NULL,
};

static ssize_t ibmvscsis_tpg_enable_show(struct config_item *item,
				char *page)
{
	struct se_portal_group *se_tpg = to_tpg(item);
	struct ibmvscsis_tport *tport = container_of(se_tpg,
						struct ibmvscsis_tport, se_tpg);

	return snprintf(page, PAGE_SIZE, "%d\n", (tport->enabled) ? 1 : 0);
}

static ssize_t ibmvscsis_tpg_enable_store(struct config_item *item,
		const char *page, size_t count)
{
	struct se_portal_group *se_tpg = to_tpg(item);
	struct ibmvscsis_tport *tport = container_of(se_tpg,
						struct ibmvscsis_tport, se_tpg);
	unsigned long tmp;
	int ret;

	ret = kstrtoul(page, 0, &tmp);
	if (ret < 0) {
		pr_err("Unable to extract ibmvscsis_tpg_store_enable\n");
		return -EINVAL;
	}

	if ((tmp != 0) && (tmp != 1)) {
		pr_err("Illegal value for ibmvscsis_tpg_store_enable: %lu\n",
			tmp);
		return -EINVAL;
	}

	if (tmp == 1)
		tport->enabled = true;
	else
		tport->enabled = false;

	return count;
}
CONFIGFS_ATTR(ibmvscsis_tpg_, enable);

static struct configfs_attribute *ibmvscsis_tpg_attrs[] = {
			&ibmvscsis_tpg_attr_enable,
			NULL,
};

static const struct target_core_fabric_ops ibmvscsis_ops = {
	.module				= THIS_MODULE,
	.name				= "ibmvscsis",
	.max_data_sg_nents		= SCSI_MAX_SG_SEGMENTS,
	.get_fabric_name		= ibmvscsis_get_fabric_name,
	.tpg_get_wwn			= ibmvscsis_get_fabric_wwn,
	.tpg_get_tag			= ibmvscsis_get_tag,
	.tpg_get_default_depth		= ibmvscsis_get_default_depth,
	.tpg_check_demo_mode		= ibmvscsis_check_true,
	.tpg_check_demo_mode_cache	= ibmvscsis_check_true,
	.tpg_check_demo_mode_write_protect = ibmvscsis_check_false,
	.tpg_check_prod_mode_write_protect = ibmvscsis_check_false,
	.tpg_get_inst_index		= ibmvscsis_tpg_get_inst_index,
	.check_stop_free		= ibmvscsis_check_stop_free,
	.release_cmd			= ibmvscsis_release_cmd,
	.shutdown_session		= ibmvscsis_shutdown_session,
	.close_session			= ibmvscsis_close_session,
	.sess_get_index			= ibmvscsis_sess_get_index,
	.write_pending			= ibmvscsis_write_pending,
	.write_pending_status		= ibmvscsis_write_pending_status,
	.set_default_node_attributes	= ibmvscsis_set_default_node_attrs,
	.get_cmd_state			= ibmvscsis_get_cmd_state,
	.queue_data_in			= ibmvscsis_queue_data_in,
	.queue_status			= ibmvscsis_queue_status,
	.queue_tm_rsp			= ibmvscsis_queue_tm_rsp,
	.aborted_task			= ibmvscsis_aborted_task,
	/*
	 * Setup function pointers for logic in target_cor_fabric_configfs.c
	 */
	.fabric_make_wwn		= ibmvscsis_make_tport,
	.fabric_drop_wwn		= ibmvscsis_drop_tport,
	.fabric_make_tpg		= ibmvscsis_make_tpg,
	.fabric_drop_tpg		= ibmvscsis_drop_tpg,

	.tfc_wwn_attrs			= ibmvscsis_wwn_attrs,
	.tfc_tpg_base_attrs             = ibmvscsis_tpg_attrs,
};

static struct vio_device_id ibmvscsis_device_table[] = {
	{"v-scsi-host", "IBM,v-scsi-host"},
	{"", ""}
};
MODULE_DEVICE_TABLE(vio, ibmvscsis_device_table);

static struct vio_driver ibmvscsis_driver = {
	.name = ibmvscsis_driver_name,
	.id_table = ibmvscsis_device_table,
	.probe = ibmvscsis_probe,
	.remove = ibmvscsis_remove,
};

/*****************************************************************************/
/* End of global device driver data areas                                    */
/*****************************************************************************/
static int crq_queue_create(struct crq_queue *queue,
				struct ibmvscsis_adapter *adapter)
{
	int retrc;
	int err;
	struct vio_dev *vdev = adapter->dma_dev;

	queue->msgs = (struct viosrp_crq *)get_zeroed_page(GFP_KERNEL);

	if (!queue->msgs)
		goto malloc_failed;

	queue->size = PAGE_SIZE / sizeof(*queue->msgs);

	queue->msg_token = dma_map_single(&vdev->dev, queue->msgs,
					  queue->size * sizeof(*queue->msgs),
					  DMA_BIDIRECTIONAL);

	if (dma_mapping_error(&vdev->dev, queue->msg_token))
		goto map_failed;

	retrc = err = h_reg_crq(vdev->unit_address, queue->msg_token,
			PAGE_SIZE);

	/* If the adapter was left active for some reason (like kexec)
	 * try freeing and re-registering
	 */
	if (err == H_RESOURCE)
		err = ibmvscsis_reset_crq_queue(adapter);
	if (err == 2) {
		pr_warn("ibmvscsis: Partner adapter not ready\n");
		retrc = 0;
	} else if (err != 0) {
		pr_err("ibmvscsis: Error 0x%x opening virtual adapter\n", err);
		goto reg_crq_failed;
	}

	queue->cur = 0;
	spin_lock_init(&queue->lock);

	INIT_WORK(&adapter->crq_work, handle_crq);

	err = request_irq(vdev->irq, &ibmvscsis_interrupt,
			  0, "ibmvscsis", adapter);
	if (err) {
		pr_err("ibmvscsis: Error 0x%x h_send_crq\n", err);
		goto req_irq_failed;
	}

	err = vio_enable_interrupts(vdev);
	if (err != 0) {
		pr_err("ibmvscsis: Error %d enabling interrupts!!!\n", err);
		goto req_irq_failed;
	}

	return retrc;

req_irq_failed:
	h_free_crq(vdev->unit_address);
reg_crq_failed:
	dma_unmap_single(&vdev->dev, queue->msg_token,
			 queue->size * sizeof(*queue->msgs), DMA_BIDIRECTIONAL);
map_failed:
	free_page((unsigned long) queue->msgs);
malloc_failed:
	return -1;
}

/*
 * ibmvscsis_probe - ibm vscsis target initialize entry point
 * @param  dev vio device struct
 * @param  id  vio device id struct
 * @return	0 - Success
 *		Non-zero - Failure
 */
static int ibmvscsis_probe(struct vio_dev *vdev, const struct vio_device_id *id)
{
	int ret = -ENOMEM;
	struct ibmvscsis_adapter *adapter;
	struct srp_target *target;
	struct ibmvscsis_tport *tport;
	unsigned long flags;

	pr_debug("ibmvscsis: Probe for UA 0x%x\n", vdev->unit_address);

	adapter = kzalloc(sizeof(struct ibmvscsis_adapter), GFP_KERNEL);
	if (!adapter)
		return ret;
	target = kzalloc(sizeof(struct srp_target), GFP_KERNEL);
	if (!target)
		goto free_adapter;

	adapter->dma_dev = vdev;
	adapter->target = target;
	tport = &adapter->tport;

	tport->enabled = false;
	snprintf(&adapter->tport.tport_name[0], 256, "%s",
		 dev_name(&vdev->dev));

	ret = read_dma_window(adapter->dma_dev, adapter);
	if (ret != 0)
		goto free_target;

	pr_debug("ibmvscsis: Probe: liobn 0x%x, riobn 0x%x\n", adapter->liobn,
			adapter->riobn);

	spin_lock_irqsave(&ibmvscsis_dev_lock, flags);
	list_add_tail(&adapter->list, &ibmvscsis_dev_list);
	spin_unlock_irqrestore(&ibmvscsis_dev_lock, flags);

	ret = srp_target_alloc(target, &vdev->dev,
				INITIAL_SRP_LIMIT,
				SRP_MAX_IU_LEN);

	adapter->target->ldata = adapter;

	if (ret) {
		pr_err("ibmvscsis: failed target alloc ret: %d\n", ret);
		goto free_srp_target;
	}

	ret = crq_queue_create(&adapter->crq_queue, adapter);
	if (ret != 0 && ret != H_RESOURCE) {
		pr_err("ibmvscsis: failed crq_queue_create ret: %d\n", ret);
		ret = -1;
	}

	if (h_send_crq(adapter, 0xC001000000000000LL, 0) != 0
			&& ret != H_RESOURCE) {
		pr_warn("ibmvscsis: Failed to send CRQ message\n");
		ret = 0;
	}

	dev_set_drvdata(&vdev->dev, adapter);

	return 0;

free_srp_target:
	srp_target_free(target);
free_target:
	kfree(target);
free_adapter:
	kfree(adapter);
	return ret;
}

static int ibmvscsis_remove(struct vio_dev *dev)
{
	unsigned long flags;
	struct ibmvscsis_adapter *adapter = dev_get_drvdata(&dev->dev);
	struct srp_target *target;

	target = adapter->target;

	spin_lock_irqsave(&ibmvscsis_dev_lock, flags);
	list_del(&adapter->list);
	spin_unlock_irqrestore(&ibmvscsis_dev_lock, flags);

	crq_queue_destroy(adapter);
	srp_target_free(target);

	kfree(target);
	kfree(adapter);

	return 0;
}

static void ibmvscsis_modify_rep_luns(struct se_cmd *se_cmd)
{
	s32 len = se_cmd->data_length;
	u16 data_len;
	unsigned char *buf = NULL;

	if (len <= 8)
		return;

	len -= 8;
	buf = transport_kmap_data_sg(se_cmd);
	if (buf) {
		data_len = be32_to_cpu(*(u32 *)buf);
		pr_debug("ibmvscsis: modify_rep_luns: len %d data_len %hud\n",
			len, data_len);
		if (data_len < len)
			len = data_len;
		buf += 8;
		while (len > 0) {
			*buf |= SCSI_LUN_ADDR_METHOD_FLAT << 6;
			len -= 8;
			buf += 8;
		}
		transport_kunmap_data_sg(se_cmd);
	}
}

static void ibmvscsis_modify_std_inquiry(struct se_cmd *se_cmd)
{
	struct se_device *dev = se_cmd->se_dev;
	unsigned char *buf = NULL;
	u32 cmd_len = se_cmd->data_length;

	if (cmd_len <= INQ_DATA_OFFSET)
		return;

	buf = transport_kmap_data_sg(se_cmd);
	if (buf) {
		memcpy(&buf[8], "IBM	     ", 8);
		if (dev->transport->get_device_type(dev) == TYPE_ROM)
			memcpy(&buf[16], "VOPTA           ", 16);
		else
			memcpy(&buf[16], "3303      NVDISK", 16);
		memcpy(&buf[32], "0001", 4);
		transport_kunmap_data_sg(se_cmd);
	}
}

static int read_dma_window(struct vio_dev *vdev,
				struct ibmvscsis_adapter *adapter)
{
	const __be32 *dma_window;
	const __be32 *prop;

	/* TODO Using of_parse_dma_window would be better, but it doesn't give
	 * a way to read multiple windows without already knowing the size of
	 * a window or the number of windows
	 */
	dma_window =
		(const __be32 *)vio_get_attribute(vdev, "ibm,my-dma-window",
						NULL);
	if (!dma_window) {
		pr_err("ibmvscsis: Couldn't find ibm,my-dma-window property\n");
		return -1;
	}

	adapter->liobn = be32_to_cpu(*dma_window);
	dma_window++;

	prop = (const __be32 *)vio_get_attribute(vdev, "ibm,#dma-address-cells",
						NULL);
	if (!prop) {
		pr_warn("ibmvscsis: Couldn't find ibm, #dma-address-cells property\n");
		dma_window++;
	} else
		dma_window += be32_to_cpu(*prop);

	prop = (const __be32 *)vio_get_attribute(vdev, "ibm,#dma-size-cells",
						NULL);
	if (!prop) {
		pr_warn("ibmvscsis: Couldn't find ibm, #dma-size-cells property\n");
		dma_window++;
	} else
		dma_window += be32_to_cpu(*prop);

	/* dma_window should point to the second window now */
	adapter->riobn = be32_to_cpu(*dma_window);

	return 0;
}

static void ibmvscsis_dev_release(struct device *dev) {};

static char *ibmvscsis_get_fabric_name(void)
{
	return "ibmvscsis";
}

static char *ibmvscsis_get_fabric_wwn(struct se_portal_group *se_tpg)
{
	struct ibmvscsis_tport *tport =
		container_of(se_tpg, struct ibmvscsis_tport, se_tpg);

	return &tport->tport_name[0];
}

static u16 ibmvscsis_get_tag(struct se_portal_group *se_tpg)
{
	struct ibmvscsis_tport *tport =
		container_of(se_tpg, struct ibmvscsis_tport, se_tpg);

	return tport->tport_tpgt;
}

static u32 ibmvscsis_get_default_depth(struct se_portal_group *se_tpg)
{
	return 1;
}

static int ibmvscsis_check_true(struct se_portal_group *se_tpg)
{
	return 1;
}

static int ibmvscsis_check_false(struct se_portal_group *se_tpg)
{
	return 0;
}

static u32 ibmvscsis_tpg_get_inst_index(struct se_portal_group *se_tpg)
{
	return 1;
}

static int ibmvscsis_check_stop_free(struct se_cmd *se_cmd)
{
	struct ibmvscsis_cmnd *cmd = container_of(se_cmd,
			struct ibmvscsis_cmnd, se_cmd);

	return target_put_sess_cmd(&cmd->se_cmd);
}

static void ibmvscsis_release_cmd(struct se_cmd *se_cmd)
{
	struct ibmvscsis_cmnd *cmd =
		container_of(se_cmd, struct ibmvscsis_cmnd, se_cmd);

	kfree(cmd);
}

static int ibmvscsis_shutdown_session(struct se_session *se_sess)
{
	return 0;
}

static void ibmvscsis_close_session(struct se_session *se_sess)
{
}

static u32 ibmvscsis_sess_get_index(struct se_session *se_sess)
{
	return 0;
}

static int ibmvscsis_write_pending(struct se_cmd *se_cmd)
{
	struct ibmvscsis_cmnd *cmd = container_of(se_cmd,
			struct ibmvscsis_cmnd, se_cmd);
	struct scsi_cmnd *sc = &cmd->sc;
	struct iu_entry *iue = (struct iu_entry *)sc->SCp.ptr;
	int ret;

	pr_debug("ibmvscsis: ibmvscsis_write_pending\n");
	sc->sdb.length = se_cmd->data_length;
	sc->sdb.table.nents = se_cmd->t_data_nents;
	sc->sdb.table.sgl = se_cmd->t_data_sg;

	ret = srp_transfer_data(sc, &vio_iu(iue)->srp.cmd,
				ibmvscsis_rdma, 1, 1);
	if (ret) {
		pr_err("ibmvscsis: srp_transfer_data() failed: %d\n", ret);
		return -EAGAIN;
	}
	/*
	 * We now tell TCM to add this WRITE CDB directly into the TCM storage
	 * object execution queue.
	 */
	target_execute_cmd(&cmd->se_cmd);
	return 0;
}

static int ibmvscsis_write_pending_status(struct se_cmd *se_cmd)
{
	return 0;
}

static void ibmvscsis_set_default_node_attrs(struct se_node_acl *nacl)
{
}

static int ibmvscsis_get_cmd_state(struct se_cmd *se_cmd)
{
	return 0;
}

static void ibmvscsis_determine_resid(struct se_cmd *se_cmd,
				      struct srp_rsp *rsp)
{
	if (se_cmd->residual_count) {
		if (se_cmd->se_cmd_flags & SCF_UNDERFLOW_BIT) {
			if (se_cmd->data_direction == DMA_TO_DEVICE) {
				/* residual data from an underflow write */
				rsp->flags = SRP_RSP_FLAG_DOUNDER;
				rsp->data_out_res_cnt =
					cpu_to_be32(se_cmd->residual_count);
			} else if (se_cmd->data_direction == DMA_FROM_DEVICE) {
				/* residual data from an underflow read */
				rsp->flags = SRP_RSP_FLAG_DIUNDER;
				rsp->data_in_res_cnt =
					cpu_to_be32(se_cmd->residual_count);
			}
		} else if (se_cmd->se_cmd_flags & SCF_OVERFLOW_BIT) {
			if (se_cmd->data_direction == DMA_TO_DEVICE) {
				/*  residual data from an overflow write */
				rsp->flags = SRP_RSP_FLAG_DOOVER;
				rsp->data_out_res_cnt =
					cpu_to_be32(se_cmd->residual_count);
			} else if (se_cmd->data_direction ==
				   DMA_FROM_DEVICE) {
				/* residual data from an overflow read */
				rsp->flags = SRP_RSP_FLAG_DIOVER;
				rsp->data_in_res_cnt =
					cpu_to_be32(se_cmd->residual_count);
			}
		}
	}
}

static int ibmvscsis_queue_data_in(struct se_cmd *se_cmd)
{
	struct ibmvscsis_cmnd *cmd = container_of(se_cmd,
			struct ibmvscsis_cmnd, se_cmd);
	struct scsi_cmnd *sc = &cmd->sc;
	struct iu_entry *iue = (struct iu_entry *)sc->SCp.ptr;
	struct srp_cmd *srp = (struct srp_cmd *)iue->sbuf->buf;
	struct srp_rsp *rsp;
	char *sd;
	char *data;
	int ret;
	uint len;

	struct srp_target *target = iue->target;
	struct ibmvscsis_adapter *adapter = target->ldata;

	/*
	 * Check for overflow residual count
	 */
	pr_debug("ibmvscsis: ibmvscsis_queue_data_in\n");

	if (se_cmd->se_cmd_flags & SCF_OVERFLOW_BIT)
		scsi_set_resid(sc, se_cmd->residual_count);

	sc->sdb.length = se_cmd->data_length;
	sc->sdb.table.nents = se_cmd->t_data_nents;
	sc->sdb.table.sgl = se_cmd->t_data_sg;

	if (scsi_sg_count(sc)) {
		if (srp->cdb[0] == REPORT_LUNS &&
					adapter->client_data.os_type != LINUX)
			ibmvscsis_modify_rep_luns(se_cmd);
		if ((srp->cdb[0] == INQUIRY) && ((srp->cdb[1] & 0x1) == 0))
			ibmvscsis_modify_std_inquiry(se_cmd);
		ret = srp_transfer_data(sc, &vio_iu(iue)->srp.cmd,
					ibmvscsis_rdma, 1, 1);
		if (ret) {
			pr_err("ibmvscsis: srp_transfer_data failed: %d\n",
				ret);
			sd = cmd->se_cmd.sense_buffer;
			cmd->se_cmd.scsi_sense_length = 18;
			memset(cmd->se_cmd.sense_buffer, 0,
				cmd->se_cmd.scsi_sense_length);
			sd[0] = 0x70;
			sd[2] = 3;
			sd[7] = 10;
			sd[12] = 8;
			sd[13] = 1;
		}
	}

	rsp = &vio_iu(iue)->srp.rsp;
	len = sizeof(*rsp);
	memset(rsp, 0, len);
	data = rsp->data;

	rsp->tag = se_cmd->tag;
	rsp->req_lim_delta = cpu_to_be32(1);
	rsp->opcode = SRP_RSP;

	ibmvscsis_determine_resid(se_cmd, rsp);
	rsp->status = se_cmd->scsi_status;

	if (se_cmd->scsi_sense_length && se_cmd->sense_buffer) {
		rsp->sense_data_len = cpu_to_be32(se_cmd->scsi_sense_length);
		rsp->flags |= SRP_RSP_FLAG_SNSVALID;
		len += se_cmd->scsi_sense_length;
		memcpy(data, se_cmd->sense_buffer, se_cmd->scsi_sense_length);
	}

	send_iu(iue, len, VIOSRP_SRP_FORMAT);
	return 0;
}

static int ibmvscsis_queue_status(struct se_cmd *se_cmd)
{
	struct ibmvscsis_cmnd *cmd = container_of(se_cmd,
					struct ibmvscsis_cmnd, se_cmd);
	struct scsi_cmnd *sc = &cmd->sc;
	struct iu_entry *iue = (struct iu_entry *) sc->SCp.ptr;
	struct srp_rsp *rsp;
	uint len;
	char *data;

	/*
	 * Copy any generated SENSE data into sc->sense_buffer and
	 * set the appropriate sc->result to be translated by
	 * ibmvscsis_cmnd_done()
	 */
	pr_debug("ibmvscsis: ibmvscsis_queue_status\n");

	rsp = &vio_iu(iue)->srp.rsp;
	len = sizeof(*rsp);
	memset(rsp, 0, len);
	data = rsp->data;

	rsp->tag = se_cmd->tag;
	rsp->req_lim_delta = cpu_to_be32(1);
	rsp->opcode = SRP_RSP;

	ibmvscsis_determine_resid(se_cmd, rsp);
	rsp->status = se_cmd->scsi_status;

	if (se_cmd->scsi_sense_length && se_cmd->sense_buffer) {
		rsp->sense_data_len = cpu_to_be32(se_cmd->scsi_sense_length);
		rsp->flags |= SRP_RSP_FLAG_SNSVALID;
		len += se_cmd->scsi_sense_length;
		memcpy(data, se_cmd->sense_buffer, se_cmd->scsi_sense_length);
	}
	send_iu(iue, len, VIOSRP_SRP_FORMAT);
	return 0;
}

static void ibmvscsis_queue_tm_rsp(struct se_cmd *se_cmd)
{
	struct ibmvscsis_cmnd *cmd = container_of(se_cmd,
			struct ibmvscsis_cmnd, se_cmd);
	struct scsi_cmnd *sc = &cmd->sc;
	struct iu_entry *iue = (struct iu_entry *) sc->SCp.ptr;
	struct srp_target *target = iue->target;
	struct ibmvscsis_adapter *adapter = target->ldata;
	struct srp_rsp *rsp;
	uint len;
	char *data;
	u32 *tsk_status;
	u32 rsp_code;

	pr_debug("ibmvscsis: ibmvscsis_queue_tm_rsp\n");
	rsp = &vio_iu(iue)->srp.rsp;

	if (transport_check_aborted_status(se_cmd, false) != 0) {
		pr_debug("ibmvscsis: queue_tm_rsp aborted\n");
		atomic_inc(&adapter->req_lim_delta);
		srp_iu_put(iue);
	} else {
		rsp->req_lim_delta = cpu_to_be32(1
				+ atomic_xchg(&adapter->req_lim_delta, 0));
	}

	len = sizeof(*rsp);
	memset(rsp, 0, len);
	data = rsp->data;

	rsp->opcode = SRP_RSP;
	rsp->tag = se_cmd->se_tmr_req->ref_task_tag;
	rsp->status = 0;
	rsp->resp_data_len = cpu_to_be32(4);
	rsp->flags |= SRP_RSP_FLAG_RSPVALID;
	rsp->req_lim_delta = cpu_to_be32(1);

	switch (se_cmd->se_tmr_req->response) {
	case TMR_FUNCTION_COMPLETE:
	case TMR_TASK_DOES_NOT_EXIST:
		rsp_code = SRP_TASK_MANAGEMENT_FUNCTION_COMPLETE;
		break;
	case TMR_TASK_MGMT_FUNCTION_NOT_SUPPORTED:
	case TMR_LUN_DOES_NOT_EXIST:
		rsp_code = SRP_TASK_MANAGEMENT_FUNCTION_NOT_SUPPORTED;
		break;
	case TMR_FUNCTION_FAILED:
	case TMR_FUNCTION_REJECTED:
	default:
		rsp_code = SRP_TASK_MANAGEMENT_FUNCTION_FAILED;
		break;
	}

	tsk_status = (u32 *)data;
	*tsk_status = cpu_to_be32(rsp_code);
	data = (char *)(tsk_status + 1);
	len += 4;

	send_iu(iue, len, VIOSRP_SRP_FORMAT);
}

static void ibmvscsis_aborted_task(struct se_cmd *se_cmd)
{
}

static struct se_portal_group *ibmvscsis_make_nexus(
				struct ibmvscsis_tport *tport,
				const char *name)
{
	struct se_node_acl *acl;

	pr_debug("ibmvscsis: make nexus");
	if (tport->se_sess) {
		pr_debug("tport->se_sess already exists\n");
		return &tport->se_tpg;
	}

	/*
	 *  Initialize the struct se_session pointer and setup tagpool
	 *  for struct ibmvscsis_cmd descriptors
	 */
	tport->se_sess = transport_init_session(TARGET_PROT_NORMAL);
	if (IS_ERR(tport->se_sess))
		goto transport_init_fail;

	/*
	 * Since we are running in 'demo mode' this call will generate a
	 * struct se_node_acl for the ibmvscsis struct se_portal_group with
	 * the SCSI Initiator port name of the passed configfs group 'name'.
	 */

	acl = core_tpg_check_initiator_node_acl(&tport->se_tpg,
				(unsigned char *)name);
	if (!acl) {
		pr_debug("core_tpg_check_initiator_node_acl() failed for %s\n",
			name);
		goto acl_failed;
	}
	tport->se_sess->se_node_acl = acl;

	/*
	 * Now register the TCM ibmvscsis virtual I_T Nexus as active.
	 */
	transport_register_session(&tport->se_tpg,
					tport->se_sess->se_node_acl,
					tport->se_sess, tport);

	tport->se_sess->se_tpg = &tport->se_tpg;

	return &tport->se_tpg;

acl_failed:
	transport_free_session(tport->se_sess);
transport_init_fail:
	kfree(tport);
	return ERR_PTR(-ENOMEM);
}

static int ibmvscsis_drop_nexus(struct ibmvscsis_tport *tport)
{
	struct se_session *se_sess;

	se_sess = tport->se_sess;
	if (!se_sess)
		return -ENODEV;

	transport_deregister_session(tport->se_sess);
	transport_free_session(tport->se_sess);
	return 0;
}

static struct ibmvscsis_tport *ibmvscsis_lookup_port(const char *name)
{
	struct ibmvscsis_tport *tport;
	struct vio_dev *vdev;
	struct ibmvscsis_adapter *adapter;
	int ret;
	unsigned long flags;

	spin_lock_irqsave(&ibmvscsis_dev_lock, flags);
	list_for_each_entry(adapter, &ibmvscsis_dev_list, list) {
		vdev = adapter->dma_dev;
		ret = strcmp(dev_name(&vdev->dev), name);
		if (ret == 0)
			tport = &adapter->tport;
		if (tport)
			goto found;
	}
	spin_unlock_irqrestore(&ibmvscsis_dev_lock, flags);
	return NULL;
found:
	spin_unlock_irqrestore(&ibmvscsis_dev_lock, flags);
	return tport;
}

static struct se_wwn *ibmvscsis_make_tport(struct target_fabric_configfs *tf,
					   struct config_group *group,
					   const char *name)
{
	struct ibmvscsis_tport *tport;
	int ret;

	tport = ibmvscsis_lookup_port(name);
	ret = -EINVAL;

	if (!tport)
		goto err;

	tport->tport_proto_id = SCSI_PROTOCOL_SRP;
	pr_debug("ibmvscsis: make_tport(%s), pointer:%p tport_id:%x\n", name,
					tport, tport->tport_proto_id);

	return &tport->tport_wwn;
err:
	return ERR_PTR(ret);
}

static void ibmvscsis_drop_tport(struct se_wwn *wwn)
{
	struct ibmvscsis_tport *tport = container_of(wwn,
				struct ibmvscsis_tport, tport_wwn);

	pr_debug("drop_tport(%s\n",
		config_item_name(&tport->tport_wwn.wwn_group.cg_item));
}

static struct se_portal_group *ibmvscsis_make_tpg(struct se_wwn *wwn,
						  struct config_group *group,
						  const char *name)
{
	struct ibmvscsis_tport *tport =
		container_of(wwn, struct ibmvscsis_tport, tport_wwn);
	int ret;

	tport->releasing = false;

	ret = core_tpg_register(&tport->tport_wwn,
				&tport->se_tpg,
				tport->tport_proto_id);
	if (ret)
		return ERR_PTR(ret);

	return &tport->se_tpg;
}

static void ibmvscsis_drop_tpg(struct se_portal_group *se_tpg)
{
	struct ibmvscsis_tport *tport = container_of(se_tpg,
				struct ibmvscsis_tport, se_tpg);

	tport->releasing = true;
	tport->enabled = false;

	/*
	 * Release the virtual I_T Nexus for this ibmvscsis TPG
	 */
	ibmvscsis_drop_nexus(tport);
	/*
	 * Deregister the se_tpg from TCM..
	 */
	core_tpg_deregister(se_tpg);
}

static ssize_t system_id_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n", system_id);
}

static ssize_t partition_number_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%x\n", partition_number);
}

static ssize_t unit_address_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct ibmvscsis_adapter *adapter =
			container_of(dev, struct ibmvscsis_adapter, dev);

	return snprintf(buf, PAGE_SIZE, "%x\n", adapter->dma_dev->unit_address);
}

static int get_system_info(void)
{
	struct device_node *rootdn, *vdevdn;
	const char *id, *model, *name;
	const unsigned int *num;

	pr_debug("ibmvscsis: getsysteminfo");
	rootdn = of_find_node_by_path("/");
	if (!rootdn)
		return -ENOENT;

	model = of_get_property(rootdn, "model", NULL);
	id = of_get_property(rootdn, "system-id", NULL);
	if (model && id)
		snprintf(system_id, sizeof(system_id), "%s-%s", model, id);

	name = of_get_property(rootdn, "ibm,partition-name", NULL);
	if (name)
		strncpy(partition_name, name, sizeof(partition_name));

	num = of_get_property(rootdn, "ibm,partition-no", NULL);
	if (num)
		partition_number = of_read_number(num, 1);

	of_node_put(rootdn);

	vdevdn = of_find_node_by_path("/vdevice");
	vdevdn = of_find_node_by_path("/vdevice");
	if (vdevdn) {
		const unsigned *mvds;

		mvds = of_get_property(vdevdn, "ibm,max-virtual-dma-size",
				       NULL);
		if (mvds)
			max_vdma_size = *mvds;
		of_node_put(vdevdn);
	}

	return 0;
};

static irqreturn_t ibmvscsis_interrupt(int dummy, void *data)
{
	struct ibmvscsis_adapter *adapter = data;

	pr_debug("ibmvscsis: there is an interrupt\n");
	vio_disable_interrupts(adapter->dma_dev);
	queue_work(vtgtd, &adapter->crq_work);

	return IRQ_HANDLED;
}

static int process_srp_iu(struct iu_entry *iue)
{
	union viosrp_iu *iu = vio_iu(iue);
	struct srp_target *target = iue->target;
	struct ibmvscsis_adapter *adapter = target->ldata;
	u8 opcode = iu->srp.rsp.opcode;
	unsigned long flags;
	int err = 1;

	spin_lock_irqsave(&target->lock, flags);
	if (adapter->tport.releasing == true) {
		pr_err("ibmvscsis: process_srp_iu error, tport is released:%x\n",
			adapter->tport.releasing);
		goto done;
	}
	if (adapter->tport.enabled == false) {
		pr_err("ibmvscsis: process_srp_iu, tport not enabled:%x\n",
			adapter->tport.enabled);
		goto done;
	}
	spin_unlock_irqrestore(&target->lock, flags);

	switch (opcode) {
	case SRP_LOGIN_REQ:
		process_login(iue);
		break;
	case SRP_TSK_MGMT:
		process_tsk_mgmt(adapter, iue);
		break;
	case SRP_CMD:
		err = ibmvscsis_queuecommand(adapter, iue);
		if (err) {
			srp_iu_put(iue);
			pr_err("ibmvscsis: can't queue cmd\n");
		}
		break;
	case SRP_LOGIN_RSP:
	case SRP_I_LOGOUT:
		ibmvscsis_srp_i_logout(iue);
		break;
	case SRP_T_LOGOUT:
	case SRP_RSP:
	case SRP_CRED_REQ:
	case SRP_CRED_RSP:
	case SRP_AER_REQ:
	case SRP_AER_RSP:
		pr_err("ibmvscsis: Unsupported type %u\n", opcode);
		break;
	default:
		pr_err("ibmvscsis: Unknown type %u\n", opcode);
	}
	return err;

done:
	spin_unlock_irqrestore(&target->lock, flags);
	srp_iu_put(iue);
	return err;
}

static void process_iu(struct viosrp_crq *crq,
		       struct ibmvscsis_adapter *adapter)
{
	struct iu_entry *iue;
	long err;

	iue = srp_iu_get(adapter->target);
	if (!iue) {
		pr_err("ibmvscsis: Error getting IU from pool %p\n", iue);
		return;
	}

	iue->remote_token = crq->IU_data_ptr;

	err = h_copy_rdma(be16_to_cpu(crq->IU_length), adapter->riobn,
				be64_to_cpu(crq->IU_data_ptr),
				adapter->liobn, iue->sbuf->dma);

	switch (err) {
	case H_SUCCESS:
		break;
	case H_PERMISSION:
	case H_SOURCE_PARM:
	case H_DEST_PARM:
		if (connection_broken(adapter))
			pr_debug("ibmvscsis: rdma connection broken\n");
	default:
		pr_err("ibmvscsis: process iu error\n");
		break;
	}

	if (crq->format == VIOSRP_MAD_FORMAT)
		process_mad_iu(iue);
	else {
		pr_debug("ibmvscsis: process srpiu");
		process_srp_iu(iue);
	}
}

static void process_crq(struct viosrp_crq *crq,
			struct ibmvscsis_adapter *adapter)
{
	switch (crq->valid) {
	case 0xC0:
		/* initialization */
		switch (crq->format) {
		case 0x01:
			h_send_crq(adapter, 0xC002000000000000, 0);
			break;
		case 0x02:
			break;
		default:
			pr_err("ibmvscsis: Unknown format %u\n", crq->format);
		}
		break;
	case 0xFF:
		/* transport event */
		switch (crq->format) {
		case MIGRATED:
		case PARTNER_FAILED:
		case PARTNER_DEREGISTER:
			adapter->client_data.os_type = 0;
			pr_debug("ibmvscsis (%s):trans_event:good format %d\n",
			dev_name(&adapter->dma_dev->dev), (uint)crq->format);
			break;
		default:
			pr_err("ibmvscsis (%s):trans_event:invalid format %d\n",
			dev_name(&adapter->dma_dev->dev), (uint)crq->format);
		}
		break;
	case 0x80:
		/* real payload */
		switch (crq->format) {
		case VIOSRP_SRP_FORMAT:
		case VIOSRP_MAD_FORMAT:
			process_iu(crq, adapter);
			break;
		case VIOSRP_OS400_FORMAT:
		case VIOSRP_AIX_FORMAT:
		case VIOSRP_LINUX_FORMAT:
		case VIOSRP_INLINE_FORMAT:
			pr_err("ibmvscsis: Unsupported format %u\n",
					crq->format);
			break;
		default:
			pr_err("ibmvscsis: Unknown format %u\n",
					crq->format);
		}
		break;
	default:
		pr_err("ibmvscsis: unknown message type 0x%02x!?\n",
				crq->valid);
	}
}

static void handle_crq(struct work_struct *work)
{
	struct ibmvscsis_adapter *adapter =
			container_of(work, struct ibmvscsis_adapter, crq_work);
	struct viosrp_crq *crq;
	int done = 0;

	while (!done) {
		while ((crq = next_crq(&adapter->crq_queue)) != NULL) {
			process_crq(crq, adapter);
			crq->valid = 0x00;
		}

		vio_enable_interrupts(adapter->dma_dev);

		crq = next_crq(&adapter->crq_queue);
		if (crq) {
			vio_disable_interrupts(adapter->dma_dev);
			process_crq(crq, adapter);
			crq->valid = 0x00;
		} else
			done = 1;
	}
}

static int ibmvscsis_reset_crq_queue(struct ibmvscsis_adapter *adapter)
{
	int rc = 0;
	struct vio_dev *vdev = adapter->dma_dev;
	struct crq_queue *queue = &adapter->crq_queue;

	/* Close the CRQ */
	h_free_crq(vdev->unit_address);

	/* Clean out the queue */
	memset(queue->msgs, 0x00, PAGE_SIZE);
	queue->cur = 0;

	/* And re-open it again */
	rc = h_reg_crq(vdev->unit_address, queue->msg_token,
			PAGE_SIZE);
	if (rc == 2)
		/* Adapter is good, but other end is not ready */
		pr_warn("ibmvscsis: Partner adapter not ready\n");
	else if (rc != 0)
		pr_err("ibmvscsis: couldn't register crq--rc 0x%x\n", rc);

	return rc;
}

static void crq_queue_destroy(struct ibmvscsis_adapter *adapter)
{
	struct vio_dev *vdev = adapter->dma_dev;
	struct crq_queue *queue = &adapter->crq_queue;

	free_irq(vdev->irq, (void *)adapter);
	flush_work(&adapter->crq_work);
	h_free_crq(vdev->unit_address);
	dma_unmap_single(&adapter->dma_dev->dev, queue->msg_token,
			 queue->size * sizeof(*queue->msgs),
			 DMA_BIDIRECTIONAL);

	free_page((unsigned long)queue->msgs);
}

static inline struct viosrp_crq *next_crq(struct crq_queue *queue)
{
	struct viosrp_crq *crq;
	unsigned long flags;

	spin_lock_irqsave(&queue->lock, flags);
	crq = &queue->msgs[queue->cur];
	if (crq->valid & 0x80 || crq->valid & 0xFF) {
		if (++queue->cur == queue->size)
			queue->cur = 0;

		/* Ensure the read of the valid bit occurs before reading any
		 * other bits of the CRQ entry
		 */
		rmb();
	} else
		crq = NULL;
	spin_unlock_irqrestore(&queue->lock, flags);

	return crq;
}

static int send_iu(struct iu_entry *iue, u64 length, u8 format)
{
	struct srp_target *target = iue->target;
	struct ibmvscsis_adapter *adapter = target->ldata;
	struct ibmvscsis_crq_msg crq_msg;
	struct srp_rsp *rsp;
	__be64 *crq_as_u64 = (__be64 *)&crq_msg;
	long rc, rc1;

	rsp = &vio_iu(iue)->srp.rsp;
	pr_debug("ibmvscsis: send_iu: 0x%lx 0x%lx 0x%lx 0x%lx 0x%lx\n",
			(unsigned long)length,
			(unsigned long)adapter->liobn,
			(unsigned long)iue->sbuf->dma,
			(unsigned long)adapter->riobn,
			(unsigned long)be64_to_cpu(iue->remote_token));

	/* First copy the SRP */
	rc = h_copy_rdma(length, adapter->liobn, iue->sbuf->dma,
			 adapter->riobn, be64_to_cpu(iue->remote_token));

	switch (rc) {
	case H_SUCCESS:
		break;
	case H_PERMISSION:
	case H_SOURCE_PARM:
	case H_DEST_PARM:
		if (connection_broken(adapter)) {
			pr_debug("ibmvscsis: rdma connection broken\n");
			goto end;
		}
		break;
	default:
		pr_err("ibmvscsis: Error %ld transferring data\n", rc);
		length = 0;
		break;
	}

	pr_debug("ibmvscsis: crq pre cooked: 0x%x, 0x%llx, 0x%llx\n",
			format, length, vio_iu(iue)->srp.rsp.tag);

	crq_msg.valid = 0x80;
	crq_msg.format = format;
	crq_msg.rsvd = 0;
	if (rc == 0)
		crq_msg.status = 0x99;
	else
		crq_msg.status = rsp->status;
	crq_msg.rsvd1 = 0;
	crq_msg.IU_length = cpu_to_be16(length);
	crq_msg.IU_data_ptr = vio_iu(iue)->srp.rsp.tag;

	pr_debug("ibmvscsis: send crq: 0x%x, 0x%llx, 0x%llx\n",
			adapter->dma_dev->unit_address,
			be64_to_cpu(crq_as_u64[0]),
			be64_to_cpu(crq_as_u64[1]));

	srp_iu_put(iue);

	rc1 = h_send_crq(adapter, be64_to_cpu(crq_as_u64[0]),
				be64_to_cpu(crq_as_u64[1]));

	if (rc1) {
		pr_err("ibmvscsis: %ld sending response\n", rc1);
		return rc1;
	}
	return rc;
end:
	return rc;
}

static int send_adapter_info(struct iu_entry *iue,
			     dma_addr_t remote_buffer, u16 length)
{
	struct srp_target *target = iue->target;
	struct ibmvscsis_adapter *adapter = target->ldata;
	dma_addr_t data_token;
	struct viosrp_adapter_info *mad = &vio_iu(iue)->mad.adapter_info;
	struct mad_adapter_info_data *info;
	int err;

	mad->common.status = cpu_to_be16(VIOSRP_MAD_SUCCESS);

	if (be16_to_cpu(mad->common.length) > sizeof(*info)) {
		mad->common.status = cpu_to_be16(VIOSRP_MAD_FAILED);
		return 0;
	}

	info = dma_alloc_coherent(&adapter->dma_dev->dev, sizeof(*info),
				  &data_token, GFP_KERNEL);
	if (!info) {
		pr_err("ibmvscsis: bad dma_alloc_coherent %p\n", target);
		mad->common.status = cpu_to_be16(VIOSRP_MAD_FAILED);
		return 1;
	}

	/* Get remote info */
	err = h_copy_rdma(sizeof(*info), adapter->riobn,
				be64_to_cpu(remote_buffer),
				adapter->liobn, data_token);

	if (err == H_SUCCESS) {
		pr_err("ibmvscsis: Client connect: %s (%d)\n",
		       info->partition_name, info->partition_number);

		if (adapter->client_data.partition_number == 0)
			adapter->client_data.partition_number =
				be32_to_cpu(info->partition_number);
		strncpy(adapter->client_data.srp_version, info->srp_version,
			sizeof(adapter->client_data.srp_version));
		strncpy(adapter->client_data.partition_name,
			info->partition_name,
			sizeof(adapter->client_data.partition_name));
		adapter->client_data.mad_version =
						be32_to_cpu(info->mad_version);
		adapter->client_data.os_type = be32_to_cpu(info->os_type);
		pr_debug("ibmvscsis: adapterinfo client adapter %u\n",
				adapter->client_data.os_type);

		strcpy(info->srp_version, "16.a");
		strncpy(info->partition_name, partition_name,
			sizeof(info->partition_name));

		info->partition_number = cpu_to_be32(partition_number);
		info->mad_version = cpu_to_be32(1);
		info->os_type = cpu_to_be32(2);
		memset(&info->port_max_txu[0], 0, sizeof(info->port_max_txu));
		info->port_max_txu[0] = cpu_to_be32(SCSI_MAX_SG_SEGMENTS *
						PAGE_SIZE);

		dma_rmb();
		/* Send our info to remote */
		err = h_copy_rdma(sizeof(*info), adapter->liobn, data_token,
				  adapter->riobn, be64_to_cpu(remote_buffer));

		switch (err) {
		case H_SUCCESS:
			break;
		case H_PERMISSION:
		case H_SOURCE_PARM:
		case H_DEST_PARM:
			if (connection_broken(adapter))
				pr_debug("ibmvscsis: rdma connection broken\n");
		default:
			pr_err("ibmvscsis: Error sending adapter info %d\n",
			       err);
			return -EIO;
		}
	} else {
		pr_err("ibmvscsis: Error sending adapter info %d\n", err);
		return 1;
	}

	dma_free_coherent(&adapter->dma_dev->dev, sizeof(*info), info,
			  data_token);

	return 0;
}

static int process_mad_iu(struct iu_entry *iue)
{
	union viosrp_iu *iu = vio_iu(iue);
	struct viosrp_adapter_info *info;
	struct viosrp_host_config *conf;

	switch (be32_to_cpu(iu->mad.empty_iu.common.type)) {
	case VIOSRP_EMPTY_IU_TYPE:
		pr_err("ibmvscsis: %s\n", "Unsupported EMPTY MAD IU");
		break;
	case VIOSRP_ERROR_LOG_TYPE:
		pr_err("ibmvscsis: %s\n", "Unsupported ERROR LOG MAD IU");
		iu->mad.error_log.common.status = 1;
		send_iu(iue, sizeof(iu->mad.error_log),	VIOSRP_MAD_FORMAT);
		break;
	case VIOSRP_ADAPTER_INFO_TYPE:
		info = &iu->mad.adapter_info;
		info->common.status = send_adapter_info(iue, info->buffer,
							info->common.length);
		send_iu(iue, sizeof(*info), VIOSRP_MAD_FORMAT);
		break;
	case VIOSRP_HOST_CONFIG_TYPE:
		conf = &iu->mad.host_config;
		conf->common.status = 1;
		send_iu(iue, sizeof(*conf), VIOSRP_MAD_FORMAT);
		break;
	default:
		pr_err("ibmvscsis: Unknown type %u\n", iu->srp.rsp.opcode);
		iu->mad.empty_iu.common.status =
					cpu_to_be16(VIOSRP_MAD_NOT_SUPPORTED);
		send_iu(iue, sizeof(iu->mad), VIOSRP_MAD_FORMAT);
		break;
	}

	return 1;
}

static void ibmvscsis_srp_i_logout(struct iu_entry *iue)
{
	union viosrp_iu *iu = vio_iu(iue);
	struct srp_i_logout *log_out = &vio_iu(iue)->srp.i_logout;
	u64 tag = iu->srp.rsp.tag;

	log_out->opcode = SRP_I_LOGOUT;
	log_out->tag = tag;
	send_iu(iue, sizeof(*log_out), VIOSRP_SRP_FORMAT);
}

static void process_login(struct iu_entry *iue)
{
	union viosrp_iu *iu = vio_iu(iue);
	struct srp_login_rsp *rsp = &iu->srp.login_rsp;
	struct srp_login_rej *rej = &iu->srp.login_rej;
	struct srp_target *target = iue->target;
	struct ibmvscsis_adapter *adapter = target->ldata;
	struct vio_dev *vdev = adapter->dma_dev;
	struct se_portal_group *se_tpg;
	char name[16];
	u64 tag = iu->srp.rsp.tag;

	/*
	 * TODO handle case that requested size is wrong and buffer
	 * format is wrong
	 */
	memset(iu, 0, max(sizeof(*rsp), sizeof(*rej)));

	snprintf(name, sizeof(name), "%x", vdev->unit_address);

	if (adapter->tport.enabled == false) {
		rej->reason = cpu_to_be32(SRP_LOGIN_REJ_INSUFFICIENT_RESOURCES);
		pr_err("ibmvscsis: Rejected SRP_LOGIN_REQ because target %s has not yet been enabled",
		       name);
		goto reject;
	}

	se_tpg = ibmvscsis_make_nexus(&adapter->tport,
				      &adapter->tport.tport_name[0]);
	if (se_tpg == NULL) {
		pr_debug("ibmvscsis: login make nexus fail se_tpg(%p)\n",
						se_tpg);
		goto reject;
	}

	rsp->opcode = SRP_LOGIN_RSP;

	rsp->req_lim_delta = cpu_to_be32(INITIAL_SRP_LIMIT);

	pr_debug("ibmvscsis: process_login, tag:%llu\n", tag);

	rsp->tag = tag;
	rsp->max_it_iu_len = cpu_to_be32(sizeof(union srp_iu));
	rsp->max_ti_iu_len = cpu_to_be32(sizeof(union srp_iu));
	/* direct and indirect */
	rsp->buf_fmt = cpu_to_be16(SRP_BUF_FORMAT_DIRECT
					| SRP_BUF_FORMAT_INDIRECT);

	send_iu(iue, sizeof(*rsp), VIOSRP_SRP_FORMAT);
	return;

reject:
	rej->opcode = SRP_LOGIN_REJ;
	rej->tag = tag;
	rej->buf_fmt = cpu_to_be16(SRP_BUF_FORMAT_DIRECT
				   | SRP_BUF_FORMAT_INDIRECT);

	send_iu(iue, sizeof(*rej), VIOSRP_SRP_FORMAT);
}

static void process_tsk_mgmt(struct ibmvscsis_adapter *adapter,
			     struct iu_entry *iue)
{
	struct srp_tsk_mgmt *srp_tsk = &vio_iu(iue)->srp.tsk_mgmt;
	struct ibmvscsis_cmnd *cmd = adapter->cmd;
	struct srp_rsp *rsp;
	u64 unpacked_lun = 0;
	u64 tag_to_abort = 0;
	int tcm_type;
	int rc = 0;

	rsp = &vio_iu(iue)->srp.rsp;
	unpacked_lun = ibmvscsis_unpack_lun((uint8_t *)&srp_tsk->lun,
					    sizeof(srp_tsk->lun));

	switch (srp_tsk->tsk_mgmt_func) {
	case SRP_TSK_ABORT_TASK:
		tcm_type = TMR_ABORT_TASK;
		tag_to_abort = be64_to_cpu(srp_tsk->task_tag);
		srp_iu_put(iue);
		break;
	case SRP_TSK_ABORT_TASK_SET:
		tcm_type = TMR_ABORT_TASK_SET;
		break;
	case SRP_TSK_CLEAR_TASK_SET:
		tcm_type = TMR_CLEAR_TASK_SET;
		break;
	case SRP_TSK_LUN_RESET:
		tcm_type = TMR_LUN_RESET;
		break;
	case SRP_TSK_CLEAR_ACA:
		tcm_type = TMR_CLEAR_ACA;
		break;
	default:
		pr_err("ibmvscsis: unknown task mgmt func %d\n",
						srp_tsk->tsk_mgmt_func);
		cmd->se_cmd.se_tmr_req->response =
					TMR_TASK_MGMT_FUNCTION_NOT_SUPPORTED;
		rc = -1;
		break;
	}

	if (!rc) {
		cmd->se_cmd.tag = be64_to_cpu(srp_tsk->tag);

		pr_debug("ibmvscsis: calling submit_tmr, func %d\n",
			 srp_tsk->tsk_mgmt_func);
		rc = target_submit_tmr(&cmd->se_cmd,
				       adapter->tport.se_sess, NULL,
				       unpacked_lun, srp_tsk, tcm_type,
				       GFP_KERNEL, tag_to_abort,
				       TARGET_SCF_ACK_KREF);
		if (rc != 0) {
			pr_err("ibmvscsis: target_submit_tmr failed, rc %d\n",
			       rc);
			cmd->se_cmd.se_tmr_req->response =
							TMR_FUNCTION_REJECTED;
			goto fail;
		}
	}
fail:
	if (rc)
		transport_send_check_condition_and_sense(&cmd->se_cmd, 0, 0);

}

static bool connection_broken(struct ibmvscsis_adapter *adapter)
{
	u64 buffer[2];
	struct viosrp_crq *crq;
	long h_return_code;
	bool rc = false;

	/* create a PING crq */
	crq = (struct viosrp_crq *)&buffer;
	buffer[0] = buffer[1] = 0;
	crq->valid = 0x80;
	crq->format = 6;
	crq->status = 0xF5;

	h_return_code = h_send_crq(adapter,
				   cpu_to_be64(buffer[0]),
				   cpu_to_be64(buffer[1]));

	pr_debug("ibmvscsis (%s): connection_broken: rc %ld\n",
			dev_name(&adapter->dma_dev->dev), h_return_code);

	if (h_return_code == H_CLOSED)
		rc = true;

	return rc;
}

static int ibmvscsis_rdma(struct scsi_cmnd *sc, struct scatterlist *sg, int nsg,
			  struct srp_direct_buf *md, int nmd,
			  enum dma_data_direction dir, unsigned int rest)
{
	struct iu_entry *iue = (struct iu_entry *) sc->SCp.ptr;
	struct srp_target *target = iue->target;
	struct ibmvscsis_adapter *adapter = target->ldata;
	dma_addr_t token;
	long err;
	unsigned int done = 0;
	int i, sidx, soff;

	sidx = soff = 0;
	token = sg_dma_address(sg + sidx);

	for (i = 0; i < nmd && rest; i++) {
		unsigned int mdone, mlen;

		mlen = min(rest, be32_to_cpu(md[i].len));
		for (mdone = 0; mlen;) {
			int slen = min(sg_dma_len(sg + sidx) - soff, mlen);

			if (dir == DMA_TO_DEVICE)
				err = h_copy_rdma(slen,
						  adapter->riobn,
						  be64_to_cpu(md[i].va) + mdone,
						  adapter->liobn,
						  token + soff);
			else
				err = h_copy_rdma(slen,
						  adapter->liobn,
						  token + soff,
						  adapter->riobn,
						  be64_to_cpu(md[i].va)+mdone);
			switch (err) {
			case H_SUCCESS:
				break;
			case H_PERMISSION:
			case H_SOURCE_PARM:
			case H_DEST_PARM:
				if (connection_broken(adapter))
					pr_debug("ibmvscsis: rdma connection broken\n");
			default:
				pr_err("ibmvscsis: rdma error %d %d %ld\n",
					dir, slen, err);
				return -EIO;
			}

			mlen -= slen;
			mdone += slen;
			soff += slen;
			done += slen;

			if (soff == sg_dma_len(sg + sidx)) {
				sidx++;
				soff = 0;
				token = sg_dma_address(sg + sidx);

				if (sidx > nsg) {
					pr_err("ibmvscsis: out of sg %p %d %d\n",
						iue, sidx, nsg);
					return -EIO;
				}
			}
		}
		rest -= mlen;
	}
	return 0;
}

static int ibmvscsis_queuecommand(struct ibmvscsis_adapter *adapter,
				  struct iu_entry *iue)
{
	struct srp_cmd *cmd = iue->sbuf->buf;
	struct scsi_cmnd *sc;
	struct ibmvscsis_cmnd *vsc;
	int ret;

	pr_debug("ibmvscsis: ibmvscsis_queuecommand\n");

	vsc = kzalloc(sizeof(*vsc), GFP_KERNEL);
	adapter->cmd = vsc;
	sc = &vsc->sc;
	sc->sense_buffer = vsc->se_cmd.sense_buffer;
	sc->cmnd = cmd->cdb;
	sc->SCp.ptr = (char *)iue;

	ret = tcm_queuecommand(adapter, vsc, cmd);

	return ret;
}

static uint64_t ibmvscsis_unpack_lun(const uint8_t *lun, int len)
{
	uint64_t res = NO_SUCH_LUN;
	int addressing_method;

	if (unlikely(len < 2)) {
		pr_err("Illegal LUN length %d, expected 2 bytes or more\n",
			len);
		goto out;
	}

	switch (len) {
	case 8:
		if ((*((__be64 *)lun) & cpu_to_be64(0x0000FFFFFFFFFFFFLL)) != 0)
			goto out_err;
		break;
	case 4:
		if (*((__be16 *)&lun[2]) != 0)
			goto out_err;
		break;
	case 6:
		if (*((__be32 *)&lun[2]) != 0)
			goto out_err;
		break;
	case 2:
		break;
	default:
		goto out_err;
	}

	addressing_method = (*lun) >> 6; /* highest two bits of byte 0 */
	switch (addressing_method) {
	case SCSI_LUN_ADDR_METHOD_PERIPHERAL:
	case SCSI_LUN_ADDR_METHOD_FLAT:
	case SCSI_LUN_ADDR_METHOD_LUN:
		res = *(lun + 1) | (((*lun) & 0x3f) << 8);
		break;

	case SCSI_LUN_ADDR_METHOD_EXTENDED_LUN:
	default:
		pr_err("Unimplemented LUN addressing method %u\n",
			addressing_method);
		break;
	}

out:
	return res;
out_err:
	pr_err("Support for multi-level LUNs has not yet been implemented\n");
	goto out;
}

static int tcm_queuecommand(struct ibmvscsis_adapter *adapter,
			    struct ibmvscsis_cmnd *vsc,
			    struct srp_cmd *scmd)
{
	struct se_cmd *se_cmd;
	int attr;
	u64 data_len;
	int ret;
	uint64_t unpacked_lun;

	switch (scmd->task_attr) {
	case SRP_SIMPLE_TASK:
		attr = TCM_SIMPLE_TAG;
		break;
	case SRP_ORDERED_TASK:
		attr = TCM_ORDERED_TAG;
		break;
	case SRP_HEAD_TASK:
		attr = TCM_HEAD_TAG;
		break;
	case SRP_ACA_TASK:
		attr = TCM_ACA_TAG;
		break;
	default:
		pr_err("ibmvscsis: Task attribute %d not supported\n",
		       scmd->task_attr);
		attr = TCM_SIMPLE_TAG;
	}

	pr_debug("ibmvscsis: srp_data_length: %llx, srp_direction:%x\n",
			srp_data_length(scmd, srp_cmd_direction(scmd)),
			srp_cmd_direction(scmd));
	data_len = srp_data_length(scmd, srp_cmd_direction(scmd));

	vsc->se_cmd.tag = scmd->tag;
	se_cmd = &vsc->se_cmd;

	pr_debug("ibmvscsis: size of lun:%lx, lun:%s\n", sizeof(scmd->lun),
				&scmd->lun.scsi_lun[0]);

	unpacked_lun = ibmvscsis_unpack_lun((uint8_t *)&scmd->lun,
				sizeof(scmd->lun));

	ret = target_submit_cmd(se_cmd, adapter->tport.se_sess,
				&scmd->cdb[0], &vsc->sense_buf[0], unpacked_lun,
				data_len, attr, srp_cmd_direction(scmd),
				TARGET_SCF_ACK_KREF);
	if (ret != 0) {
		ret = TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
		pr_debug("ibmvscsis: tcm_queuecommand fail submit_cmd\n");
		goto send_sense;
	}
	return 0;

send_sense:
	transport_send_check_condition_and_sense(&vsc->se_cmd, ret, 0);
	transport_generic_free_cmd(&vsc->se_cmd, 0);
	return -1;
}

/*
 * ibmvscsis_init() - Kernel Module initialization
 *
 * Note: vio_register_driver() registers callback functions, and atleast one
 * of those call back functions calls TCM - Linux IO Target Subsystem, thus
 * the SCSI Target template must be registered before vio_register_driver()
 * is called.
 */
static int __init ibmvscsis_init(void)
{
	int ret = -ENOMEM;

	pr_info("IBMVSCSIS fabric module %s on %s/%s on "UTS_RELEASE"\n",
		IBMVSCSIS_VERSION,
		utsname()->sysname,
		utsname()->machine);

	ret = get_system_info();
	if (ret) {
		pr_err("ibmvscsis: ret %d from get_system_info\n", ret);
		goto out;
	}

	ret = class_register(&ibmvscsis_class);
	if (ret) {
		pr_err("ibmvscsis failed class register\n");
		goto out;
	}

	ret = target_register_template(&ibmvscsis_ops);
	if (ret) {
		pr_err("ibmvscsis: ret %d from target_register_template\n",
				ret);
		goto unregister_class;
	}

	vtgtd = create_workqueue("ibmvscsis");
	if (!vtgtd)
		goto unregister_target;

	ret = vio_register_driver(&ibmvscsis_driver);
	if (ret) {
		pr_err("ibmvscsis: ret %d from vio_register_driver\n", ret);
		goto destroy_wq;
	}

	return 0;

destroy_wq:
	destroy_workqueue(vtgtd);
unregister_target:
	target_unregister_template(&ibmvscsis_ops);
unregister_class:
	class_unregister(&ibmvscsis_class);
out:
	return ret;
};

static void __exit ibmvscsis_exit(void)
{
	pr_info("ibmvscsis: Unregister IBM virtual SCSI driver\n");
	vio_unregister_driver(&ibmvscsis_driver);
	destroy_workqueue(vtgtd);
	target_unregister_template(&ibmvscsis_ops);
	class_unregister(&ibmvscsis_class);
};

MODULE_DESCRIPTION("IBMVSCSIS fabric driver");
MODULE_AUTHOR("Bryant G. Ly");
MODULE_LICENSE("GPL");
module_init(ibmvscsis_init);
module_exit(ibmvscsis_exit);
