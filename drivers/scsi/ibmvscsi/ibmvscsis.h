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

#ifndef __H_IBMVSCSIS
#define __H_IBMVSCSIS

#define SYS_ID_NAME_LEN		64
#define PARTITION_NAMELEN	97
#define IBMVSCSIS_NAMELEN       32

#define SCSOLNT_RESP_SHIFT      1
#define UCSOLNT_RESP_SHIFT      2

#define SCSOLNT		BIT(SCSOLNT_RESP_SHIFT)
#define UCSOLNT		BIT(UCSOLNT_RESP_SHIFT)

#define INQ_DATA_OFFSET 8
#define NO_SUCH_LUN ((u64)-1LL)

struct crq_queue {
	struct viosrp_crq *msgs;
	int size, cur;
	dma_addr_t msg_token;
	spinlock_t lock;
};

struct client_info {
#define SRP_VERSION "16.a"
	char srp_version[8];
	/* root node property ibm,partition-name */
	char partition_name[PARTITION_NAMELEN];
	/* root node property ibm,partition-no */
	u32 partition_number;
	/* initially 1 */
	u32 mad_version;
	u32 os_type;
};

struct ibmvscsis_cmnd {
	/* Used for libsrp processing callbacks */
	struct scsi_cmnd sc;
	/* Used for TCM Core operations */
	struct se_cmd se_cmd;
	/* Sense buffer that will be mapped into outgoing status */
	unsigned char sense_buf[TRANSPORT_SENSE_BUFFER];
	u32 lun;
};

struct ibmvscsis_crq_msg {
	u8 valid;
	u8 format;
	u8 rsvd;
	u8 status;
	u16 rsvd1;
	__be16 IU_length;
	__be64 IU_data_ptr;
};

struct ibmvscsis_tport {
	/* SCSI protocol the tport is providing */
	u8 tport_proto_id;
	/* ASCII formatted WWPN for SRP Target port */
	char tport_name[IBMVSCSIS_NAMELEN];
	/* Returned by ibmvscsis_make_tport() */
	struct se_wwn tport_wwn;
	int lun_count;
	/* Returned by ibmvscsis_make_tpg() */
	struct se_portal_group se_tpg;
	/* ibmvscsis port target portal group tag for TCM */
	u16 tport_tpgt;
	/* Pointer to TCM session for I_T Nexus */
	struct se_session *se_sess;
	struct ibmvscsis_cmnd *cmd;
	bool enabled;
	bool releasing;
};

struct ibmvscsis_adapter {
	struct device dev;
	struct vio_dev *dma_dev;
	struct list_head siblings;

	struct crq_queue crq_queue;
	struct work_struct crq_work;

	atomic_t req_lim_delta;
	u32 liobn;
	u32 riobn;

	struct srp_target *target;

	struct list_head list;
	struct ibmvscsis_tport tport;
	struct ibmvscsis_cmnd *cmd;
	struct client_info client_data;
};

struct ibmvscsis_nacl {
	/* Returned by ibmvscsis_make_nexus */
	struct se_node_acl se_node_acl;
};

enum srp_trans_event {
	UNUSED_FORMAT = 0,
	PARTNER_FAILED = 1,
	PARTNER_DEREGISTER = 2,
	MIGRATED = 6
};

enum scsi_lun_addr_method {
	SCSI_LUN_ADDR_METHOD_PERIPHERAL   = 0,
	SCSI_LUN_ADDR_METHOD_FLAT         = 1,
	SCSI_LUN_ADDR_METHOD_LUN          = 2,
	SCSI_LUN_ADDR_METHOD_EXTENDED_LUN = 3,
};

enum srp_os_type {
	OS400 = 1,
	LINUX = 2,
	AIX = 3,
	OFW = 4
};

#define vio_iu(IUE) ((union viosrp_iu *)((IUE)->sbuf->buf))

#define h_reg_crq(ua, tok, sz)\
			plpar_hcall_norets(H_REG_CRQ, ua, tok, sz)

#endif
