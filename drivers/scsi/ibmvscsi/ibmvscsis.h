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

#define IBMVSCSIS_NAMELEN       32

#define SCSOLNT_RESP_SHIFT      1
#define UCSOLNT_RESP_SHIFT      2

#define SCSOLNT         (1 << SCSOLNT_RESP_SHIFT)
#define UCSOLNT         (1 << UCSOLNT_RESP_SHIFT)

#define INQ_DATA_OFFSET 8
#define NO_SUCH_LUN ((u64)-1LL)

struct client_info {
#define SRP_VERSION "16.a"
	char srp_version[8];
	/* root node property ibm,partition-name */
	char partition_name[96];
	/* root node property ibm,partition-no */
	uint32_t partition_number;
	/* initially 1 */
	uint32_t mad_version;
	uint32_t os_type;
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

struct inquiry_data {
	u8 qual_type;
	u8 rmb_reserve;
	u8 version;
	u8 aerc_naca_hisup_format;
	u8 addl_len;
	u8 sccs_reserved;
	u8 bque_encserv_vs_multip_mchngr_reserved;
	u8 reladr_reserved_linked_cmdqueue_vs;
	char vendor[8];
	char product[16];
	char revision[4];
	char vendor_specific[20];
	char reserved1[2];
	char version_descriptor[16];
	char reserved2[22];
	char unique[158];
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
