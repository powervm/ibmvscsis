/*
 * nvme-fabrics.c - NVM protocol paradigm independent of transport
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
 */
#include <linux/nvme-fabrics/nvme-common.h>
#include <linux/nvme-fabrics/nvme-fabrics.h>
#include <linux/errno.h>
#include <linux/random.h>

#define NVME_UNUSED(x) ((void)x)

static struct nvme_fabric_host *nvme_host;

/* TODO: Remove before upstreaming: Check we didn't grow the command struct */
static inline void _nvme_check_size(void)
{
	BUILD_BUG_ON(sizeof(struct nvme_common_rw_cmd) != 64);
	BUILD_BUG_ON(sizeof(struct nvme_common_create_cq) != 64);
	BUILD_BUG_ON(sizeof(struct nvme_common_create_sq) != 64);
	BUILD_BUG_ON(sizeof(struct nvme_common_delete_queue) != 64);
	BUILD_BUG_ON(sizeof(struct nvme_common_features) != 64);
	BUILD_BUG_ON(sizeof(struct nvme_common_format_cmd) != 64);
	BUILD_BUG_ON(sizeof(struct nvme_common_abort_cmd) != 64);
	BUILD_BUG_ON(sizeof(struct nvme_common_sgl_cmd) != 64);
	BUILD_BUG_ON(sizeof(struct nvme_common_cmd) != 64);
	BUILD_BUG_ON(sizeof(struct nvme_common_id_ctrl) != 4096);
	BUILD_BUG_ON(sizeof(struct nvme_common_id_ns) != 4096);
	BUILD_BUG_ON(sizeof(struct nvme_common_completion) != 16);

	BUILD_BUG_ON(sizeof(struct nvme_common_sgl_desc) != 16);
	BUILD_BUG_ON(sizeof(struct nvme_common_sgl_dblk) != 16);
	BUILD_BUG_ON(sizeof(struct nvme_common_sgl_bbkt) != 16);
	BUILD_BUG_ON(sizeof(struct nvme_common_sgl_seg) != 16);
	BUILD_BUG_ON(sizeof(struct nvme_common_sgl_lseg) != 16);

	BUILD_BUG_ON(sizeof(struct nvme_connect_capsule) != 1024);
	BUILD_BUG_ON(sizeof(struct nvme_connect_rsp_capsule) != 16);
	BUILD_BUG_ON(sizeof(struct nvme_submit_capsule) != 80);
	BUILD_BUG_ON(sizeof(struct nvme_completion_capsule) != 32);
	BUILD_BUG_ON(sizeof(struct nvme_prpset_capsule) != 32);
	BUILD_BUG_ON(sizeof(struct nvme_prpset_rsp_capsule) != 16);
	BUILD_BUG_ON(sizeof(struct nvme_prpget_capsule) != 16);
	BUILD_BUG_ON(sizeof(struct nvme_prpget_rsp_capsule) != 16);
	BUILD_BUG_ON(sizeof(struct nvme_cplqueue_capsule) != 16);
	BUILD_BUG_ON(sizeof(struct nvme_cplqueue_rsp_capsule) != 16);
	BUILD_BUG_ON(sizeof(struct nvme_discover_capsule) != 1024);
	BUILD_BUG_ON(sizeof(struct nvme_discover_rsp_capsule) != 16);
	BUILD_BUG_ON(sizeof(struct nvme_discoveryinfo_rsp_capsule) != 16);
	BUILD_BUG_ON(sizeof(struct nvme_discoveryinfo_capsule) != 16);
}

/*
 * Function that fills in the values for an nvme_fabric_addr struct.
 *
 * @address_type: parameter that describes what type of fabric
 *		  address was passed into the function.
 * @address: Fabric address
 * @port: port value of the fabric address, if valid (FC addresses
 *        don't have a port, for example)
 *
 * @fabric_addr: An OUT parameter that expects an allocated struct
 *		 of nvme_fabric_addr that will be filled out properly
 *		 based on address_type, address, and port.
 *
 * Returns:
 *      the value of 'address_type' or -EINVAL on error.
 *
 * Caveats:
 *	if fabric_addr is NULL, function will return w/error.
 */
/*
 * TODO: Create new file with these external APIs.  This will enable
 *      any "manager" (including sysfs) to call.
 */
int nvme_fabric_parse_addr(int address_type, char *address, int port,
			   struct nvme_fabric_addr *fabric_addr)
{
	int ret = 0;

	fabric_addr->what_addr_type = address_type;

	switch (address_type) {
	case NVME_FABRIC_DNS:
		memcpy(fabric_addr->addr.dns_addr.octet,
		       address, DNS_ADDR_SIZE);
		fabric_addr->addr.dns_addr.tcp_udp_port = (__u16) port;
		break;
	case NVME_FABRIC_IP4:
		memcpy(fabric_addr->addr.ipv4_addr.octet, address,
		       IPV4_ADDR_SIZE);
		fabric_addr->addr.ipv4_addr.tcp_udp_port = (__u16) port;
		break;
	case NVME_FABRIC_IP6:
		memcpy(fabric_addr->addr.ipv6_addr.octet, address,
		       IPV6_ADDR_SIZE);
		fabric_addr->addr.ipv6_addr.tcp_udp_port = (__u16) port;
		break;
	case NVME_FABRIC_EMAC:
		memcpy(fabric_addr->addr.emac_addr.octet, address,
		       EMAC_ADDR_SIZE);
		break;
	case NVME_FABRIC_IBA:
		memcpy(fabric_addr->addr.iba_addr.octet, address,
		       IBA_ADDR_SIZE);
		break;
	case NVME_FABRIC_WWID:
		memcpy(fabric_addr->addr.fc_addr.octet, address,
		       FC_ADDR_SIZE);
		break;
	default:
		pr_err("Unsupported address type %d\n", address_type);
		ret = -EINVAL;
		break;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(nvme_fabric_parse_addr);

/*
 * Helper function to return the existing target subsystem if
 * it exists (which is ok, as there can be many controllers under a target.
 */
static struct nvme_fabric_subsystem *find_subsystem(char *subsys_name)
{
	struct list_head	     *i;
	struct list_head	     *q;
	struct nvme_fabric_subsystem *ret;

	list_for_each_safe(i, q, &nvme_host->subsystem_list) {
		ret = list_entry(i, struct nvme_fabric_subsystem, node);
		if ((!strcmp(subsys_name, ret->subsiqn)))
			return ret;
	}
	return NULL;
}

/*
 * Helper function to see if a ctrlr already exists in a known target ss
 * (which is not ok, as ctrlr names should be unique per subsystem).
 *
 * Currently not used; commented out or the compiler will complain.
 *
static int does_ctrl_exist(struct nvme_fabric_subsystem *conn, __u16 cntlid)
{
	struct list_head *i, *q;
	struct nvme_fabric_ctrl *iter;

	list_for_each_safe(i, q, &conn->ctrl_list) {
		iter = list_entry(i, struct nvme_fabric_ctrl, node);
		if (cntlid == iter->cntlid)
			return -EEXIST;
	}
	return 0;
}
*/

static union nvme_capsule_rsp *
create_nvme_capsule_rsp(struct nvme_common_cmd *cmd,
			int queue_type, __u32 queue_num,
			__u32 *len)
{

	return NULL;
}

static union nvme_capsule_cmd *
create_nvme_capsule(struct nvme_common_cmd *cmd,
		    int queue_type, __u32 queue_num,
		    __u32 *len)
{

	/*IF this command is for the AQ:
		Build a capsule in which the SGL uses "SGL Last segment"
		Allocate that capsule for two in-capsule data block SGLs.
		Copy the NVMe command contents to the capsule
	*/

	return NULL;
}

/* This replaces nvme_core's __nvme_submit_cmd() for agnostic code */
static int nvme_fabric_submit_admin_cmd(struct nvme_common_queue *nvmeq,
					struct nvme_common_cmd *cmd)
{
	int ret				= 0;
	__u32 len			= 0;
	__u32 rsp_len			= 0;
	union nvme_capsule_cmd *capsule	= NULL;
	union nvme_capsule_rsp *rsp	= NULL;

	capsule = create_nvme_capsule(cmd, NVME_AQ, 0, &len);
	if (!capsule)
		goto err1;

	/* TODO: revisit.  NOTE creating union of responses so dont
			   need response length
	*/
	rsp = create_nvme_capsule_rsp(cmd, NVME_AQ, 0, &rsp_len);
	if (!rsp)
		goto err2;

	/* TODO:
		If prp1 is !0:
			Call fops->build_admin_sglist (with prp1, prp2,
			pointer to SGL created in nvme_create_nvme_capsule
			If prp2 is 0
				length is 4KB
			Else if prp2 is !0
				length is 8KB
		else
			No reason to build SGL. Do nothing

		nvme_host->fops->send_fabric_capsule(nvmeq->context,
						     capsule,
						     rsp,
						      expected_bytes);

	NOTE: For now fabric layer will complete this when it
	      has confirmation of send completion and therefore
	      we can free the command capsule.  For perf opt phase
	      explore async callback to free command capsule
	*/

	goto out;
err1:

err2:

out:
	return ret;
}

/*
 * TODO - Milestone 2: Flush out what is instance?!
 */
int nvme_fabric_set_instance(void)
{
	return (nvme_host->instance)++;
}
EXPORT_SYMBOL_GPL(nvme_fabric_set_instance);

/*
 * TODO - Milestone 2: Guessing this is ok w/new use of nvme_fabric_host...
 * DO NOT CONSIDER REMOVING UNTIL MILESTONE 3 IS SOLIDIFIED AT LEAST.
 */
void *nvme_fabric_get_xport_context(void)
{
	return nvme_host->xport_context;
}
EXPORT_SYMBOL_GPL(nvme_fabric_get_xport_context);

static void nvme_fabric_destroy_ctrl(struct nvme_fabric_subsystem *subsys,
				     struct nvme_fabric_ctrl *ctrl)
{
	pr_info("%s: Removing controller %d @ subsys %s\n",
		__func__, ctrl->cntlid, subsys->subsiqn);

	nvme_host->fops->disconnect(subsys->subsiqn,
				    ctrl->cntlid,
				    &subsys->address);
	list_del(&ctrl->node);
	subsys->num_ctrl--;
	kfree(ctrl);
	ctrl = NULL;
}

/*
 * Public function that starts a single controller's removal process.
 * Tells the fabric specific code to shutdown all connections
 * associated with that controller and clean up associated lists.
 *
 * @subsys_name:  Name of the subsystem name to remove from the
 *		  host's data tree (if cntlid == NULL), or
 *		  name of the subsystem to look for the controller
 *		  name we want to remove.
 *
 * @cntlid:       If 0xFFFF, which is an illegal value for NVMe
 *		  Controllers in Fabrics, the subsystem
 *		  (and everything under it)
 *		  will get removed.  Otherwise, remove just the
 *		  controller from the host's data structure tree.
 *
 * Return Value:
 *      O for success,
 *	Any other value, error
 */
/*
 * TODO: Add to new "API" file - see earlier TODO
 */
int nvme_fabric_remove_host_treenode(char *subsys_name, __u16 cntlid)
{
	struct nvme_fabric_subsystem	*subsys;
	struct nvme_fabric_ctrl		*ctrl;
	struct list_head		*ictrl;
	struct list_head		*tmpctrl;
	unsigned long			 flags;
	int				 ret = -ENXIO;

	pr_info("%s: %s()\n", __FILE__, __func__);

	subsys = find_subsystem(subsys_name);
	if (!subsys) {
		pr_err("%s: Did not find subsys %s\n", __func__, subsys_name);
		return -ENXIO;
	}

	spin_lock_irqsave(&nvme_host->subsystem_list_lock, flags);

	/*
	 * This value is illegal for an NVMe controller's cntlid to have,
	 * so we use it as a way to get this function to remove all
	 * controllers in a subsystem.
	 */
	if (cntlid != 0xFFFF) {
		/* delete just the one controller in the subsystem */
		list_for_each_safe(ictrl, tmpctrl,  &subsys->ctrl_list) {
			ctrl = list_entry(ictrl,
					  struct nvme_fabric_ctrl, node);
			if (cntlid == ctrl->cntlid) {
				nvme_fabric_destroy_ctrl(subsys, ctrl);
				subsys->num_ctrl--;
				ret = 0;
				break;
			}
		}
	} else {
		/* delete all controllers in subsystem */
		list_for_each_safe(ictrl, tmpctrl,  &subsys->ctrl_list) {
			ctrl = list_entry(ictrl,
					  struct nvme_fabric_ctrl, node);
			nvme_fabric_destroy_ctrl(subsys, ctrl);
		}

		if (unlikely(subsys->num_ctrl))
			pr_err("%s: Ctrl count in subsys %s should be 0: %d\n",
			       __func__, subsys->subsiqn,
			       subsys->num_ctrl);

		pr_info("%s: Removing subsys %s\n", __func__,
			subsys->subsiqn);

		/* Now remove the subsystem */
		list_del(&subsys->node);
		nvme_host->num_subsystems--;
		kfree(subsys);

		ret = 0;
	}

	spin_unlock_irqrestore(&nvme_host->subsystem_list_lock, flags);
	return ret;
}
EXPORT_SYMBOL_GPL(nvme_fabric_remove_host_treenode);

/*
 * Creates an NVMe Connect Capsule Packet.
 *
 * TODO: Make sure subsys_name really is a subsys_name
 * and not the old ctrlname from last TP002 version
 */
static int create_connect_capsule(union nvme_capsule_cmd *capsule,
				       __u8   queue_type,
				       __u16  cntlid,
				       __le16 queue_number,
				       __u8   *hnsid,
				       char   *hostname,
				       char   *subsys_name)
{


	capsule->connect.hdr.cctype = CCTYPE_CONNECT_CMD;
	capsule->connect.hdr.authpr = 0;
	memcpy(capsule->connect.hdr.vs, nvme_host->vs, NVME_FABRIC_VS_LEN);
	if (queue_type == NVME_AQ) {
		capsule->connect.hdr.sqid  = 0;
		capsule->connect.hdr.cqid  = 0;
	} else {
		capsule->connect.hdr.sqid  = queue_number;
		capsule->connect.hdr.cqid  = queue_number;
	}

	if (hnsid)
		memcpy(capsule->connect.body.hnsid, hnsid, HNSID_LEN);

	if (queue_type == NVME_AQ) {
		capsule->connect.body.cntlid = 0xFFFF;
	} else {
		pr_err("%s: TODO: Connect via IOQ WIP\n", __func__);
		return -EINVAL;
	}
	capsule->connect.body.authpr = 0;

	if (subsys_name) {
		strncpy(capsule->connect.body.subsiqn,
			subsys_name, NVME_FABRIC_IQN_MAXLEN);
	}
	if (hostname) {
		strncpy(capsule->connect.body.hostiqn,
			hostname, NVME_FABRIC_IQN_MAXLEN);
	}

	pr_info("\n===%s Created Connect Capsule ===\n", __func__);
	pr_info("cctype:  %#x      authpr: %d\n",
		capsule->connect.hdr.cctype,
		capsule->connect.hdr.authpr);
	pr_info("vs[3]: %x vs[2]: %x vs[1]: %x vs[0]: %x\n",
		capsule->connect.hdr.vs[3],
		capsule->connect.hdr.vs[2],
		capsule->connect.hdr.vs[1],
		capsule->connect.hdr.vs[0]);
	pr_info("sqid:    %d        cqid:   %d\n",
		capsule->connect.hdr.sqid,
		capsule->connect.hdr.cqid);
	pr_info("hnsid:   %pUX\n", capsule->connect.body.hnsid);
	pr_info("cntlid:  %#x   authpr: %d\n",
		capsule->connect.body.cntlid,
		capsule->connect.body.authpr);
	pr_info("subsiqn: %s\n", capsule->connect.body.subsiqn);
	pr_info("hostiqn: %s\n", capsule->connect.body.hostiqn);
	pr_info("========================\n");

	return 0;
}

/*
 * Creates an NVMe Connect Response Packet.
 *
 * Notes:
 *    *KEEP THIS FUNCTION UNTIL NVME FABRIC SPEC BECOMES MORE STABLE.
 *     THEN IT CAN BE DECIDED IF THIS IS STILL NEEDED.
 */
static int create_connect_capsule_rsp(union nvme_capsule_rsp *rsp)
{
	/*
	 * These values are illegal for a controller to send back to the
	 * host, so this is a good initial value to set to catch
	 * bugs.
	 */
	rsp->connect.hdr.cctype = 255;
	rsp->connect.hdr.sts    = 69;
	rsp->connect.hdr.cntlid = 0xFFFF;
	return 0;
}

/* TODO - Milestone 2: Define function to "discover disks" */
/* Once the Administrative Queue for a given subsystem has been connected,
 * logged in, and info exchanged, this function calls the subsytem via
 * administrative capsules to obtain subsystem info to discover and configure
 * the NS on that subsystem and creates/connects IOQs
 */
static int nvme_fabric_initialize_disks(struct nvme_fabric_subsystem *conn)
{
	int                      ret = 0;

	pr_info("%s: %s()\n", __FILE__, __func__);

	NVME_UNUSED(conn);

	return ret;
}

/* spinlock enabled function that adds a new subsystem to the fabric
 * host's data tree
 */
static struct nvme_fabric_subsystem *
nvme_fabric_add_subsystem(char *subsys_name,
			  struct nvme_fabric_addr *address,
			  int fabric, int conn)
{
	unsigned long			 flags = 0;
	struct nvme_fabric_subsystem	 *subsystem;

	subsystem = kzalloc(sizeof(struct nvme_fabric_subsystem), GFP_KERNEL);
	if (!subsystem)
		return NULL;

	strncpy(subsystem->subsiqn, subsys_name, NVME_FABRIC_IQN_MAXLEN);
	memcpy(&subsystem->address, address, sizeof(struct nvme_fabric_addr));
	subsystem->conn_type = conn;
	subsystem->fabric = fabric;
	subsystem->num_ctrl = 0;
	INIT_LIST_HEAD(&subsystem->ctrl_list);
	spin_lock_init(&subsystem->ctrl_list_lock);

	spin_lock_irqsave(&nvme_host->subsystem_list_lock, flags);
	subsystem->reference_num = nvme_host->num_subsystems++;
	list_add_tail(&subsystem->node, &nvme_host->subsystem_list);
	spin_unlock_irqrestore(&nvme_host->subsystem_list_lock, flags);

	return subsystem;
}

static int
nvme_fabric_connect_login_aq(struct nvme_fabric_ctrl *new_ctrl,
			     struct nvme_fabric_subsystem *subsystem)
{
	union nvme_capsule_cmd		 capsule;
	union nvme_capsule_rsp		 rsp;
	int				 ret;
	unsigned long			 flags = 0;

	/* TODO - Milestone 2: Deal with this hnsid/uuid crap - gets
	 * weird on a reconnect Connect/Create an Fabric specific (not NVMe)
	 * AQ Connection
	 */
	ret = nvme_host->fops->connect_create_queue(subsystem,
			new_ctrl->cntlid,
			nvme_host->hnsid,
			CONN_AQ,
			&(new_ctrl->aq_conn));

	if (ret) {
		pr_err("%s(): Error connect_create_queue(AQ) %d\n",
		       __func__, ret);
		goto err1;
	}

	if (new_ctrl->aq_conn == NULL) {
		pr_err("%s(): Error! aq_conn NULL from *_create_queue()\n",
		       __func__);
		ret = -ENODEV;
		goto err1;
	} else {
		pr_info("%s(): aq_conn ptr has been set to %p\n",
			__func__, new_ctrl->aq_conn);
	}

	ret =  create_connect_capsule(&capsule,
				      NVME_AQ,
				      new_ctrl->cntlid,
				      0,
				      nvme_host->hnsid,
				      nvme_host->hostname,
				      subsystem->subsiqn);
	if (ret)
		goto err1;

	ret = create_connect_capsule_rsp(&rsp);
	if (ret) {
		pr_err("%s %s(): Error %d creating connect capsule\n",
		       __FILE__, __func__, ret);
		goto err1;
	};
	ret = nvme_host->fops->send_connect_capsule(
		      new_ctrl->aq_conn,
		      &capsule,
		      &rsp,
		      sizeof(struct nvme_connect_rsp_capsule));
	if (ret) {
		pr_err("%s(): Error send_capsule() returned %d\n",
		       __func__, ret);
		goto err1;
	}

	if ((rsp.connect.hdr.cctype != CCTYPE_CONNECT_RSP) ||
	    (rsp.connect.hdr.cntlid == 0xFFFF) ||
	    (rsp.connect.hdr.sts != 0)) {
		pr_err("%s(): Error! Unexpected Connect response values!\n",
		       __func__);
		pr_err("connect rsp cctype: %d (must be '5')\n",
		       rsp.connect.hdr.cctype);
		pr_err("connect rsp cntlid: %#x (cannot be 0xFFFF)\n",
		       rsp.connect.hdr.cntlid);
		pr_err("connect rsp sts:    %d (should be 0)\n\n",
		       rsp.connect.hdr.sts);

		/*
		 * TODO: Add this in when send_capsule() RDMA code is fully
		 * written to check for wacky connect response errors
		 * coming from the controller.
		 *
		ret = -ENODATA;
		goto err1;
		 */
	}

	/* Now that we have a valid controller id for the subsystem,
	 * optionally inform the implementation of the value.
	 * This is implementation specific, so the specific fabric
	 * transport does not have to fill this function out if
	 * it does not need it.
	 */

	spin_lock_irqsave(&subsystem->ctrl_list_lock, flags);
	new_ctrl->cntlid = rsp.connect.hdr.cntlid;
	if (!nvme_host->fops->finalize_cntlid) {
		ret = nvme_host->fops->finalize_cntlid(subsystem->subsiqn,
						       new_ctrl->cntlid);
	}
	spin_unlock_irqrestore(&subsystem->ctrl_list_lock, flags);

err1:
	return ret;
}
/*
 * Public function that adds an NVMe remote Subsystem by do_add_subsystem or
 * nvme_fabric_discovery
 *
 *	it initializes memory to hold a list of controller connections
 *	calls fops->connect_create_queue() which obtains an AQ
 *	Conn logs into the Administration Queue on the remote ctrlr
 *	obtains I/O information
 *	obtains the proper I/O Connections
 *	sets up the drives properly
 *
 * @subsys_name: Name of the subsystem
 * @cntlid:      Name of the controller we will add.  This is the NVMe
 *               Identify Controller parameter cntlid.
 * @fabric_type: The type of fabric used for the connection.
 * @conn_type:   The type of connection (e.g., RC/RD) used
 * @address:     address of the controller (address + port)
 * Return Value:
 *      O for success,
 *	Any other value, error
 */
int nvme_fabric_add_controller(char *subsys_name,
			       int fabric_type, int conn_type,
			       struct nvme_fabric_addr *address)
{
	struct nvme_fabric_ctrl		*new_ctrl;
	struct nvme_fabric_subsystem	*subsystem;
	unsigned long			 flags = 0;
	int				 ret   = 0;

	pr_info("%s: %s()\n", __FILE__, __func__);

	subsystem = find_subsystem(subsys_name);
	if (!subsystem) {
		pr_info("%s: Creating subsystem %s.\n", __func__, subsys_name);
		subsystem = nvme_fabric_add_subsystem(subsys_name, address,
						      fabric_type, conn_type);
		if (!subsystem)
			goto err1;
	}

	new_ctrl = kzalloc(sizeof(struct nvme_fabric_ctrl), GFP_KERNEL);
	if (!new_ctrl) {
		ret = -ENOMEM;
		goto err1;
	}
	new_ctrl->cntlid	= NVME_FABRIC_INIT_CNTLID;
	new_ctrl->state		= CONN_AQ;

	ret = nvme_fabric_connect_login_aq(new_ctrl, subsystem);
	if (ret)
		goto err2;

	/* finish filling out ctrl info and adding it to subsys list
	 * as it is now a valid ctrl by this point
	 */
	new_ctrl->state  = CONN_IOQ;
	new_ctrl->host	 = nvme_host;

	spin_lock_irqsave(&subsystem->ctrl_list_lock, flags);
	list_add_tail(&new_ctrl->node, &subsystem->ctrl_list);
	subsystem->num_ctrl++;
	spin_unlock_irqrestore(&subsystem->ctrl_list_lock, flags);

	ret = nvme_fabric_initialize_disks(subsystem);
	if (ret)
		goto err3;

	new_ctrl->state  = CONN_FULLY_INIT;

	return ret;
err3:
	/* TODO/FIXME: add the following cleanup: shutdown the ctrl node fabric
	 * connections, remove new_ctrl->node from list, and decrement
	 * subsystem->num_ctrl
	 */
err2:
	kfree(new_ctrl);
err1:
	return ret;
}
EXPORT_SYMBOL_GPL(nvme_fabric_add_controller);

/*
 * Public function that starts a discovery
 *
 * @address:     struct nvme_fabric_addr that contains the fabric address
 *               to use on a discovery operation (of a controller).
 * @fabric_type: What specific fabric we are operating on (RDMA, FC, etc)
 * @dry_run:     If non-zero value, then go through the motions of discovering
 *               a controller, but don't actually add to the fabric host's
 *               data tree and connect to it.
 *               (synonymous to 'patch --dry-run' command)
 *
 * Return Value:
 *      O for success,
 *      Any other value, error
 */
/*
 * TODO: Add to new "API" file - see earlier TODO
 */
int nvme_fabric_discovery(struct nvme_fabric_addr *address, int fabric_type,
			  int dry_run)
{
	NVME_UNUSED(address);
	NVME_UNUSED(fabric_type);
	NVME_UNUSED(dry_run);

	/* TODO, REDO: This is all so screwed up it just needs to be re-done.
	 * First, IP addresses are now subsystem-level, not controller level,
	 * and it may actually finally stick this way with the NVMe committee
	 * as this is what an iSCSI kernel target does.
	 * Original guess of this function was based on
	 * assigning a unqiue IP address to a
	 * controller, which is not right.  Second, discovery proposals
	 * have shifted so wildly there is no point to even guess at this
	 * until at least Milestone 3.
	 *
	 * You want to see old crap, visit the git repo history.
	 */
	return 0;

}
EXPORT_SYMBOL_GPL(nvme_fabric_discovery);


/*
 * Retrieves the IQN name of the fabric host.
 *
 * @hostname: an out variable, will contain the name of the host
 *	      that is no longer than NVME_FABRIC_IQN_MAXLEN bytes.
 *
 * Caveats:
 *	if hostname is NULL, nothing will happen.
 */
/*
 * TODO: Add to new "API" file - see earlier TODO
 */
void nvme_fabric_get_hostname(char *hostname)
{
	if (hostname)
		strncpy(hostname, nvme_host->hostname,
			NVME_FABRIC_IQN_MAXLEN);
}
EXPORT_SYMBOL_GPL(nvme_fabric_get_hostname);

/*
 * Sets the IQN name of the fabric host to the fabric host
 * data structure.
 *
 * @hostname: an in-variable, will contain the name of the host
 *	      that is no longer than NVME_FABRIC_IQN_MAXLEN bytes.
 *
 * Caveats:
 *	if hostname is NULL, nothing will happen.
 */
/*
 * TODO: Add to new "API" file - see earlier TODO
 */
void nvme_fabric_set_hostname(char *hostname)
{
	if (hostname)
		strncpy(nvme_host->hostname, hostname,
			NVME_FABRIC_IQN_MAXLEN);
}
EXPORT_SYMBOL_GPL(nvme_fabric_set_hostname);

/*
 * Fabric specific NVMe common function definitions
 */
static struct nvme_common_host_operations nvme_common_ops = {
	.owner			= THIS_MODULE,
	.submit_admin_cmd	= nvme_fabric_submit_admin_cmd,
};

/*
 * Public function that registers this module as a new NVMe fabric driver.
 *
 * @new_fabric: New fabric with specific operations defined for
 *            that NVMe fabric driver.
 *
 * Return Value:
 *      O for success,
 *	Any other value, error
 */
int nvme_fabric_register(char *nvme_class_name,
			 struct nvme_fabric_host_operations *new_fabric)
/*
 * TODO: Remove the export as this will be an internal module function
 * debate...why???  Fabric-specific transport has to register w/the
 * fabric agnostic layer somehow.
 */
{
	int ret = -EINVAL;

	/* BUILD CHECK! Take out when intial NVMe fabric development done */
	_nvme_check_size();

	pr_info("%s: %s()\n", __FILE__, __func__);

	if (fabric_used[0] == 0 || !fabric_timeout ||
			!discover_retry_count || !admin_retry_count ||
			!io_retry_count) {
		pr_err("%s(): Error parameters not properly filled out!\n",
		       __func__);
		ret = -ENODATA;
		goto err1;
	}

	nvme_host = kzalloc(sizeof(struct nvme_fabric_host), GFP_KERNEL);
	if (!nvme_host)
		goto err1;

	nvme_host->fops = new_fabric;
	if ((nvme_host->fops->connect_create_queue == NULL) ||
			(nvme_host->fops->disconnect == NULL)           ||
			(nvme_host->fops->send_connect_capsule == NULL)  ||
			(nvme_host->fops->build_admin_sglist == NULL)) {
		pr_err("%s(): Error, a fabric function not implemented!\n",
		       __func__);
		ret = -ENOSYS;
		goto err1;
	}

	INIT_LIST_HEAD(&nvme_host->subsystem_list);
	spin_lock_init(&nvme_host->subsystem_list_lock);
	generate_random_uuid(nvme_host->hnsid);

	/* See section "Offset 08h: VS- Version" section of NVMe spec,
	 * at or around section 3.1.2.
	 */
	nvme_host->vs[1] = 3;
	nvme_host->vs[2] = 1;

	ret = nvme_common_init(&nvme_common_ops);
	if (ret) {
		pr_err("%s %s(): Error- nvme_common_init() failed\n",
		       __FILE__, __func__);
		goto err1;
	}

	ret = nvme_sysfs_init(nvme_class_name);
	if (ret) {
		pr_err("%s %s(): Error- nvme_sysfs_init() failed\n",
		       __FILE__, __func__);
		goto err2;
	}

	pr_info("%s %s() exited with %d\n", __FILE__, __func__, ret);
	return ret;

err2:
	nvme_common_exit();
err1:
	return ret;
}
EXPORT_SYMBOL_GPL(nvme_fabric_register);

/*
 * Public function that unregisters this module as a new NVMe fabric driver.
 *
 * Return Value:
 *      O for success,
 *      Any other value, error
 */
/*
 * TODO: Remove the export as this will be an internal module function
 */
int nvme_fabric_unregister(struct nvme_fabric_subsystem *conn)
{
	struct nvme_fabric_subsystem *ss;
	struct list_head	     *pos;
	struct list_head	     *q;

	pr_info("%s: %s()\n", __FILE__, __func__);

	list_for_each_safe(pos, q, &nvme_host->subsystem_list) {
		ss = list_entry(pos, struct nvme_fabric_subsystem, node);
		nvme_fabric_remove_host_treenode(ss->subsiqn, 0xFFFF);
	}

	nvme_sysfs_exit();
	nvme_common_exit();

	kfree(nvme_host);

	return 0;
}
EXPORT_SYMBOL_GPL(nvme_fabric_unregister);
