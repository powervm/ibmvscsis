/*
 * IBM Virtual SCSI Target Driver
 *
 * Copyright (C) 2016 Bryant G. Ly <bgly@us.ibm.com> IBM Corp.
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#ifndef __H_IBMVSCSIS
#define __H_IBMVSCSIS

#define IBMVSCSIS_NAMELEN       32

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

#define vio_iu(IUE) ((union viosrp_iu *)((IUE)->sbuf->buf))

#define h_reg_crq(ua, tok, sz)\
			plpar_hcall_norets(H_REG_CRQ, ua, tok, sz);

#endif
