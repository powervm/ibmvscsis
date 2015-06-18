/*
 * nvme-fabrics.h - NVM protocol paradigm independent of transport
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
 *
 * This file is used to specify all the common data structures and
 * functions that would implement a fabric for a NVMe device.
 */

#ifndef _LINUX_NVME_FABRICS_H
#define _LINUX_NVME_FABRICS_H

#include <linux/nvme-fabrics/nvme-common.h>
#include <linux/moduleparam.h>
#include <linux/list.h>
#include <linux/wait.h>

/*
 * Capsule Command Type opcodes.  In a NVMe Fabric
 * capsule, there can be commands that are related to
 * normal NVMe commands, or there can be commands that are
 * related specifically to an NVMe fabric property.
 * Part of NVMe Org proposal "Fabrics TP 002".
 */
#define CCTYPE_NVME_CMD			0x0
#define CCTYPE_NVME_RSP			0x1
#define CCTYPE_DISCOVER_CMD		0x2
#define CCTYPE_DISCOVER_RSP		0x3
#define CCTYPE_CONNECT_CMD		0x4
#define CCTYPE_CONNECT_RSP		0x5
#define CCTYPE_PROPERTY_SET_CMD		0x6
#define CCTYPE_PROPERTY_SET_RSP		0x7
#define CCTYPE_PROPERTY_GET_CMD		0x8
#define CCTYPE_PROPERTY_GET_RSP		0x9
#define CCTYPE_CPLQUEUE_UPDATE_CMD	0xA
#define CCTYPE_CPLQUEUE_UPDATE_RSP	0xB
#define CCTYPE_DISCOVER_GETINFO_CMD	0xC
#define CCTYPE_DISCOVER_GETINFO_RSP	0xD

/*
 * Value for the sts field of NVMe Capsule Response
 * packets.  Part of NVMe Org proposal "Fabrics TP 002".
 */
#define STS_SUCCESS		0x0
#define STS_INVALID_CMD		0x1
#define STS_INVALID_FIELD	0x2
#define STS_INVALID_SIZE	0x3
#define STS_INVALID_ALIGNMENT	0x4
#define STS_INVALID_ADDRESS	0x5
#define STS_QUEUE_FULL		0x6

/*
 * Specific response status values for the Connect Response
 * Capsule (sts field).  Part of NVMe Org proposal "Fabrics TP 002".
 */
#define STS_CONNECT_BAD_NVME_VERSION	0x50
#define STS_CONNECT_CONNECTION_BUSY	0x51
#define STS_CONNECT_SESSION_BUSY	0x52
#define STS_CONNECT_INVALID_AUTH	0x53
#define STS_CONNECT_RESTART_DISCOVERY	0x54

/*
 * Specific response status values for the Discover Response
 * Capsule (sts field). Part of NVMe Org Proposal "Fabrics TP 002".
 */
#define STS_DISCOVER_BAD_NVME_VERSION	0x40
#define STS_DISCOVER_RESTART_DISCOVERY	0x41

/*
 * For property values, i.e. "virtual registers",
 * they can either hole 4 byte values or 8 byte
 * values.  Thus, the 'attrib' variable in
 * Set/Get Property commands defines the size of
 * the value to be written to a given property.
 * Part of NVMe Org proposal "Fabrics TP 002".
 */
#define PROPERTY_ATTRIB_4BYTES	0x0
#define PROPERTY_ATTRIB_8BYTES	0x1

#define HNSID_LEN		16
#define CNSID_LEN		4
#define FABRIC_STRING_MAX	256
#define MAX_CTRL_PER_SUBSYS	32
#define NVME_FABRIC_INIT_CNTLID	0xFFFF

#define NVME_FABRIC_IQN_MINLEN	16   /* Min IQN length string name */
#define NVME_FABRIC_IQN_MAXLEN	256  /* Max IQN length string name */
#define NVME_FABRIC_VS_LEN	4    /* length of NVMe version # */

#define DNS_ADDR_SIZE	256
#define IPV4_ADDR_SIZE	16
#define IPV6_ADDR_SIZE	40
#define EMAC_ADDR_SIZE	18
#define IBA_ADDR_SIZE	19
#define FC_ADDR_SIZE	33

#define DISCOVER_RETRY          7
#define AQ_RETRY                7
#define IOQ_RETRY               7
#define FABRIC_TIMEOUT          15

extern char fabric_used[FABRIC_STRING_MAX];
extern unsigned char fabric_timeout;
extern unsigned char discover_retry_count;
extern unsigned char admin_retry_count;
extern unsigned char io_retry_count;

enum nvme_fabric_type {
	NVME_FABRIC_PCIE     = 0,     /* PCIe Fabric */
	NVME_FABRIC_RDMA     = 1,     /* RDMA Fabrics; IBA, iWARP, ROCE, ... */
	NVME_FABRIC_FC	     = 2,     /* Fibre Channel Fabric */
	NVME_FABRIC_OMNIPATH = 3,     /* Intel OMNI PATH RDMA technology */
	/* Future NVMe Fabrics */
};

enum nvme_queue_type {
	NVME_DQ = 0,	/* 0h - NVMe discovery queue */
	NVME_AQ,	/* 1h - NVMe Admin queue */
	NVME_IOQ,	/* 2h - NVMe IOQ */
};

/* struct used to capture dns network address */
struct dns_addr_type {
	char  octet[DNS_ADDR_SIZE];
	__u16 tcp_udp_port;
};

/* struct used to capture ipv4 network address */
struct ipv4_addr_type {
	char  octet[IPV4_ADDR_SIZE];
	__u16 tcp_udp_port;
};

/* struct used to capture ipv6 network address */
struct ipv6_addr_type {
	char  octet[IPV6_ADDR_SIZE];
	__u16 tcp_udp_port;
};

/* struct used to capture a MAC address */
struct emac_addr_type {
	char octet[EMAC_ADDR_SIZE];
};

/* struct used to capture an infiniband address */
struct iba_addr_type {
	char octet[IBA_ADDR_SIZE];
};

/* struct used to capture a fibre channel address */
struct fc_addr_type {
	/*
	 * this represents an 8 or 16 byte wwn or
	 * wwpn for fibre channel devices
	 */
	char octet[FC_ADDR_SIZE];
};

enum nvme_fabric_addr_type {
	NVME_FABRIC_DNS  = 0,
	NVME_FABRIC_IP4  = 1,
	NVME_FABRIC_IP6  = 2,
	NVME_FABRIC_EMAC = 3,
	NVME_FABRIC_IBA  = 4,
	NVME_FABRIC_WWID = 5,
};

/* generic nvme fabric address data struct */
struct nvme_fabric_addr {
	__u8 what_addr_type;

	struct addr_type {
		union {
			struct dns_addr_type dns_addr;
			struct ipv4_addr_type ipv4_addr;
			struct ipv6_addr_type ipv6_addr;
			struct emac_addr_type emac_addr;
			struct iba_addr_type iba_addr;
			struct fc_addr_type fc_addr;
		};
	} addr;
};

/*
 * This is the Capsule Header field which is part
 * of the Command Capsule Format being proposed in
 * NVMe Org proposal "Fabrics TP 002".
 *
 */
struct nvme_capsule_header {

	/*
	 * capsule opcode. Depending on the opcode is what
	 * struct is used in the union defined below.
	 */

	struct capsule_type {
		union {
			/*
			 * capsule hdr iff cctype == 0x0h,
			 * NVMe command.
			 */
			struct {
				__u8 cctype;
				__u8 rsvd[7];

				/*
				 * specifies sub queue # for this
				 * command packet.  0 is Admin.
				 */
				__le16 sqidf;

				/*
				 * If the value in this field is different
				 * than the submission queue tail value
				 * contained in the queue 'sqidf' (value
				 * in sqidf), then the tail value to be
				 * written to 'this' submission queue
				 * is held in this field, 'sqt'.
				 */
				__le16 sqt;

				/*
				 * specifices the cplqueue # head doorbell
				 * to be written. 0 is the Admin queue.
				 */
				__le16 cqidf;

				/*
				 * If this field contains a value that is
				 * different from the cpl queue # Header Dbell
				 * specified in CQHf, then cpl queue # Tail
				 * Dbell shall be written with the value
				 * specified after the cmd in the capsule is
				 * processed.
				 */
				__le16 cqh;
			} nvme_cmd;

			/*
			 * capsule hdr iff cctype == 0x01h,
			 * NVMe response.
			 */
			struct {
				__u8 cctype;
				__u8 sts;
				__u8 rsvd[10];

				/*
				 * cpl queue # which this
				 * completion was successful
				 */
				__le16 cqidf;

				__u8 rsvd2[2];
			} nvme_rsp;

			/*
			 * capsule hdr iff cctype == 0x06h,
			 * property set command.
			 */
			struct {
				__u8 cctype;

				__u8 rsvd[7];

				/*
				 * bits 7:3 are rsvd and
				 * should be 0. This field
				 * represents the size
				 * in bytes of the value the property
				 * (i.e., 'virtual register') saves.
				 */
				__u8 attrib;

				__u8 rsvd2[3];

				/* Since this struct is used to represent a
				 *'virtual register', i.e. a property, this
				 * represents the offset property in the
				 * 'virtual BAR' to set.
				 */
				__le32 ofst;

			} prpset_cmd;

			/*
			 * capsule hdr iff cctype == 0x07h,
			 * property set response.
			 */
			struct {
				__u8 cctype;
				/*
				 * return status of a property set command
				 * ('successful', offset out-of-range error,
				 * etc.
				 */
				__u8 sts;
				__u8 rsvd[14];
			} prpset_rsp;

			/*
			 * capsule hdr iff cctype == 0x08h,
			 * property get command
			 */
			struct {
				__u8 cctype;

				__u8 rsvd[7];
				/*
				 * bits 7:3 are rsvd and
				 * should be 0. This represents the size
				 * in bytes of the value the property
				 * (i.e., 'virtual register') saves.
				 */
				__u8 attrib;

				__u8 rsvd2[3];

				/* Since this struct is used to represent a
				 *'virtual register', i.e. a property, this
				 * represents the offset property in the
				 * 'virtual BAR' to read.
				 */
				__le32 ofst;
			} prpget_cmd;

			/*
			 * capsule hdr iff cctype == 0x05h,
			 * property get response.
			 */
			struct {
				__u8 cctype;
				/*
				 * return status of a property set command
				 * ('successful', offset out-of-range error,
				 * etc.
				 */
				__u8 sts;
				__u8 rsvd[6];

				/*
				 * value returned from the property
				 * read.  byte size (width) of the value
				 * is determined by the width of the
				 * 'property' and the 'attrib' parameter.
				*/
				__le64 valu;
			} prpget_rsp;

			/*
			 * capsule hdr iff cctype == 0x0Ah,
			 * completion queue update command.
			 */
			struct {
				__u8 cctype;
				__u8 rsvd[11];

				/*
				 * This field specifies which completion
				 * queue # of head doorbell value to
				 * be written.
				 */
				__le16 cqidf;

				/*
				 * The value of cqidf's tail doorbell
				 * to be written.
				 */
				__le16 cqh;
			} cplqueue_cmd;

			/*
			 * capsule hdr iff cctype == 0x0Bh,
			 * completion queue update response command.
			 */
			struct {
				__u8 cctype;

				/* cpl capsule packet response */
				__u8 sts;
				__u8 rsvd[14];
			} cplqueue_rsp;

			/*
			 * capsule hdr iff cctype == 0x04h, connect command.
			 */
			struct {
				__u8 cctype;

				/*
				 * specifies authentication protocol.
				 * 0 means no authentication required.
				 */
				__u8 authpr;
				__u8 rsvd[6];

				/*
				 * version number, as defined and formatted
				 * in the VS register of the NVMe spec
				 */
				__u8 vs[NVME_FABRIC_VS_LEN];

				/*
				 * Set to value 0 for discovery and admin
				 * cntype. For NVMe IOQ pair, field must
				 * match the QID value used in an earlier,
				 * successful NVMe Create I/O Submission
				 * Queue Admin cmd.  Otherwise, it's
				 * an CSQID Does Not Exist error.
				 */
				__le16 sqid;

				/*
				 * Set to value 0 for discovery & admin
				 * cntype. For NVMe IOQ pair, field must
				 * match the QID value set by an earlier
				 * Create I/O completion queue Admin cmd.
				 * Otherwise, it is a CCQID Does Not Exist
				 * error.
				 */
				__le16 cqid;

			} connect_cmd;

			/*
			 * capsule hdr iff cctype == 0x05h,
			 * connect response command.
			 */
			struct {
				__u8 cctype;
				/*
				 * return success or error status on
				 * Connect cmd.
				 */
				__u8 sts;

				__u8 rsvd[2];

				/* specifies the controller ID value
				 * allocated to the host on a connect
				 * command.  It's the same field as
				 * defined in the identify controller
				 * data structure.
				 */
				__u16 cntlid;

				__u8 rsvd2[10];
			} connect_rsp;

			/*
			 * capsule hdr iff cctype == 0x02h,
			 * discover command.
			 */
			struct {
				__u8 cctype;
				/*
				 * defines type of discovery info requested:
				 *      0h: Request discovery info
				 *          on all subsystems
				 *      1h: Request discovery info
				 *          on NVMe subsystem IQN
				 *      2h: Request discovery info
				 *          on NVMe controller IQN
				 */
				__u8 dirg;
				__u8 rsvd[14];
			} discovery_cmd;

			/*
			 * capsule hdr iff cctype == 0x03h,
			 * discover response command.
			 */
			struct {
				__u8 cctype;

				/* return status for the discovery response */
				__u8 sts;

				__le16 rsvd;

				/*
				 * # of total bytes of info available.
				 * 0 means nothing is available.
				 */
				__le32 dilen;

				/*
				 * max # of bytes returned in a single
				 * discovery get info response capsule.
				 */
				__le16 dicsz;
				__u8 rsvd2[6];
			} discovery_rsp;
			/*
			* capsule hdr iff cctype == 0x0Ch,
			* discover get info command.
			*/
			struct {
				__u8 cctype;
				__u8 rsvd[15];
			} discovery_info_cmd;

			/*
			 * capsule hdr iff cctype == 0x0Dh,
			 * discover get info rsp command.
			 */
			struct {
				__u8 cctype;
				__u8 rsvd[15];
			} discover_info_rsp;
		}; /* union */
	} capsule;
};

/*
 * NVMe command "child struct" of nvme_capsule_packet.
 * An object of this struct will be pointed by 'child'
 * in the base struct nvme_capsule_packet.
 */
struct nvme_cmd_capsule {

	struct nvme_common_command sqe;

	/* This is set to NULL if no data is associated w/the command */
	void *data;
};

/*
 * NVMe response "child struct" of nvme_capsule_packet.
 * An object of this struct will be pointed by 'child'
 * in the base struct nvme_capsule_packet.
 */
struct nvme_rsp_capsule {
	struct nvme_common_completion cqe;

	/* This is set to NULL if no data is associate w/the command */
	void *data;
};

/*
 * This is the connect command capsule body pointed
 * to by 'void *child' of 'struct nvme_capsule_packet'.
 * Between this and the 'hdr' field of 'struct nvme_capsule_packet'
 * gives a complete NVMe Fabric Connect Capsule cmd.
 */
struct connect_cmd_capsule {
	struct {
		/*
		 * Part of TP 002, this is the host NVMe session
		 * Globally Unique Identifier.  This is a host-generated
		 * 128 bit value using RFC-4122 UUID format.
		 */
		__u8 hnsid[HNSID_LEN];

		/*
		 * Specifies the controller ID requested.  It is the same
		 * field as returned in a identify controller data structure.
		 * Value FFFFh is for admin request, which then the controller
		 * will return the responding controller.
		 */
		__u16 cntlid;

		/*
		 * Specifies authentication protocol & attributes to be used
		 * for this connection. Bits 7:2 are reserved.
		 */
		__u8 authpr;

		__u8 rsvd[221];

		/*
		 * iSCSI Qualified Name (IQN) that uniquely identifies
		 * an NVM subsystem.  Minimimum of NVME_FABRIC_IQN_MINLEN
		 * bytes.
		 */
		char subsiqn[NVME_FABRIC_IQN_MAXLEN];

		/*
		 * iSCSI Qualified Name (IQN) that uniquely identifies the
		 * host.  Minimum of NVME-FABRIC_IQN_MINLEN.
		 */
		char hostiqn[NVME_FABRIC_IQN_MAXLEN];

		__u8 rsvd2[256];

	} capsule_body;
};

struct propset_cmd_capsule {
	struct {
		/*
		 * Specifies the value used to update the property.
		 * If the size of the property is 4 bytes, then
		 * the value is specified in LSB's of the field.
		 */
		__le64 valu;
		__le64 rsvd;
	} capsule_body;
};

/*
 * Discover cmd "child struct" of nvme_capsule_packet.
 * An object of this struct will be pointed by 'child'
 * in the base struct nvme_capsule_packet.
 */
struct discover_cmd_capsule {
	struct {
		/*
		 * NVMe data block sgl that describes the in-capsule
		 * offset location from byte 0 of host iqn string.
		 * SGL length must be a value between
		 * NVME_FABRIC_IQN_MINLEN - NVME_FABRIC_IQN_MAXLEN
		 * (IQN spec states the max is really 223).
		 */
		struct nvme_common_sgl_dblk dhnsgl;

		/*
		 * NVMe data block sgl that describes the in-capsule
		 * offset location from byte 0 of host iqn string.
		 * SGL length must be a value between
		 * NVME_FABRIC_IQN_MINLEN - NVME_FABRIC_IQN_MAXLEN
		 * (IQN spec states the max is really 223).
		 *
		 * This field is rsvd if dirg field = 0h.
		 */
		struct nvme_common_sgl_dblk dsnsgl;

		/*
		 * NVMe data block sgl that describes the in-capsule
		 * offset location from byte 0 of host iqn string.
		 * SGL length must be a value between
		 * NVME_FABRIC_IQN_MINLEN - NVME_FABRIC_IQN_MAXLEN
		 * (IQN spec states the max is really 223).
		 *
		 * This field is rsvd if dirg field is NOT equal to 02h.
		 */
		struct nvme_common_sgl_dblk dcnsgl;

		/*
		 * NVME_FABRIC_IQN_MINLEN through NVME_FABRIC_IQN_MAXLEN-1
		 * size unique string to identify the host.
		 * '\0' needs to terminate the string (hence
		 * NVME_FABRIC_IQN_MAXLEN-1).
		 */
		char host_iqn_name[NVME_FABRIC_IQN_MAXLEN];

		/*
		 * NVME_FABRIC_IQN_MINLEN through NVME_FABRIC_IQN_MAXLEN-1
		 * size unique string to identify the subsystem.
		 * '\0' needs to terminate the string.
		 */
		char subsys_iqn_name[NVME_FABRIC_IQN_MAXLEN];

		/*
		 * NVME_FABRIC_IQN_MINLEN through NVME_FABRIC_IQN_MAXLEN-1
		 * size unique string to identify the controller.
		 * '\0' needs to terminate the string.
		 */
		char ctrl_iqn_name[NVME_FABRIC_IQN_MAXLEN];
	} capsule_body;
};


/*
 * Fundamental packet type for NVMe over Fabrics, TP 002.
 * Consider this as the "base class" (base struct).
 */
struct nvme_capsule_packet {

	/*
	 * Every nvme_capsule_packet has a 16 byte header which contains
	 * an overview of what type of command is embedded within it.
	 */
	struct nvme_capsule_header hdr;

	/*
	 * The rest of the capsule packet can look pretty different
	 * depending the cctype. child points to these differences.
	 * This is set to NULL if this is a capsule_header only
	 * nvme_capsule_packet.
	 */
	void *child;
};

enum nvme_conn_stage {
	CONN_DISCOVER = 0,
	CONN_AQ,
	CONN_IOQ,
	CONN_FULLY_INIT,
	CONN_ERROR,
};

/* RC = Reliable Connected
 * RD = Reliable Datagram
 * There may be others later
 */
enum nvme_conn_type {
	RC = 0,
	RD,
};

struct aq {
	/* The fabric-specific NVMe AQ struct */
	void	 *fabric_aq_conn;
};

/* TODO: an instance of this struct is pointed by
 * nvme_common_queue context, to allow us to do
 * things like queue[0].context->aq_conn,
 * which associates the nvme_common layer of the
 * admin queue to the fabric agnostic layer of
 * the admin fabric connection.
 */
struct nvme_fabric_ctrl {
	/* list of all ctrls in subsystem */
	struct list_head	 node;

	/* enum nvme_conn_stage values */
	int			 state;

	/*
	 * controller id, per NVMe subsystem. Same value
	 * retrieved from an NVMe Identify Controller command.
	 * Guaranteed to be unique per NVMe subsystem,
	 * (or the device is not a standard NVMe device).
	 */
	__u16			 cntlid;

	/* Backpointer to nvme_fabric_host.  May not need. */
	struct nvme_fabric_host *host;

	/* NVMe Admin Queue Fabric Connection (queue 0) */
	void			*aq_conn;

	/*
	 * List of NVMe IO Queue Fabric Connections.
	 * List starts at 1 for queue 1.
	 */
	void			*ioq_list;
};

struct nvme_fabric_subsystem {
	/* List of all active conn */
	struct list_head	node;

	/* List of all active conn */
	struct list_head	ctrl_list;
	spinlock_t		ctrl_list_lock;

	unsigned int		num_ctrl;
	unsigned int		fabric;

	/* 'enum nvme_conn_type' value */
	unsigned int		conn_type;

	/*
	 * unique name and equivilent network address for
	 * the NVMe Fabrics target subsystem.
	 */
	char			subsiqn[NVME_FABRIC_IQN_MAXLEN];
	struct nvme_fabric_addr address;

	/*
	 * Not sure if this is needed; a way to refer to this
	 * subsystem via numerical reference.
	 */
	short			reference_num;

};

/*
 * NVMe fabric host data structure that maintains all the
 * subsystems it is successfully communicating and managing.
 */
struct nvme_fabric_host {

	/* host NVMe session GUID - a host-generated 128b value
	 * using RFC-4122 UUID format
	 */
	__u8			hnsid[HNSID_LEN];

	/* Unique IQN name for host */
	char			hostname[NVME_FABRIC_IQN_MAXLEN];

	/* NVMe version number the fabric host follows */
	__u8			vs[NVME_FABRIC_VS_LEN];

	/* number of subsystems host is servicing for this nvme fabric */
	int			num_subsystems;

	/* this is a list of struct nvme_fabric_subsystem */
	struct list_head	subsystem_list;
	spinlock_t		subsystem_list_lock;

	/* Namespace count for a given controller */
	int instance;

	/* API fabric-specific drivers (RDMA, FC, etc) must implement */
	struct nvme_fabric_host_operations *fops;

	/* generic, pci-e free, nvme implementation stuff */
	struct nvme_common_dev *nvme_dev;

	/*
	 * there needs to be a way to go between the local (host) nvme device
	 * and the remote (ep) nvme device.  Host will think it's the nvme
	 * device but this tells it 'to go here instead'.
	 */
	void *xport_context;
};

struct nvme_fabric_host_operations {
	struct module *owner;
	/*
	 * Simple fabric function that sends a connect capsule over the fabric
	 * specific transport. The NVMe over Fabrics agnostic layer will
	 * construct a connect capsule and
	 * a connect response capsule and send to the fabric specific
	 * layer via this function.
	 *
	 * @fabric_context: Specific fabric transport data construct that
	 *                  will be used on a send command.
	 *
	 * @capsule: the capsule that will be sent by the fabric specific layer
	 *	     to the target
	 *
	 * @rsp:      Expected response capsule to the capsule that is being
	 *	      sent.  This will be returned by the fabric specific
	 *            layer's response completion routine.
	 *  Return Value:
	 *      0 for Success
	 *      Any other value, error
	 *
	 *  Notes:
	 *      The connect response capsule size is always constant,
	 *      plus the only value the NVMe fabrics truly cares about
	 *      is the cntlid field (assuming sts field says it's
	 *      successful). Thus it's expected the use of this
	 *      NVMe capsule command is simple, thus, it can be
	 *      broken out in it's own function.
	 */
	int (*send_connect_capsule)(void *fabric_context,
				    struct nvme_capsule_packet *capsule,
				    struct nvme_capsule_packet *rsp);
#if 0
	/*
	 * Function that takes the specific fabric transport and
	 * an NVMe I/O command and packages the contents (in say,
	 * an NVME capsule for example) that will then be sent
	 * over the fabric by this function.
	 *
	 * @dev:	The current nvme_fabric device being operated on.
	 * @nvmeq:	nvme_queue from which the request originated: needed
	 *		to obtain fabric context, qid, and cq_head.
	 * @cmd:	NVMe I/O command to be sent over the fabric to
	 *		the ep.
	 * @len:	The leftover byte count after subtracting from
	 *		a base quantity byte size (like 4k for example).
	 *
	 * Return Value:
	 *      0 for success
	 *      Any other value, error
	 *
	 * Notes:
	 *	This function is based on on nvmerp_submit_io_cmd()
	 *      of the demo.
	 *
	*/
	int (*prepsend_io_cmd)(struct nvme_common_queue *nvmeq,
			       struct nvme_command *cmd, __u32 len);
#endif
	/*
	 * Function that shuts down a fabric connection.
	 *
	 * @subsys_name: The name of the subsystem that will
	 *		 have it's 'endpoints'
	 *		 (NVMe controllers for example)
	 *		 disconnected from fabric communication.
	 * @ctrlname:	 The controller name to be disconnected
	 *		 from fabric communication.
	 * @address:	 Specific network address being used to
	 *		 be shutdown.
	 */
	void (*disconnect)(char *subsys_name, __u16 cntlid,
			   struct nvme_fabric_addr *addr);

	/*
	 * Function that establishes a fabric-specific connection with
	 * the ctrl, as well as create the send work queue and the receive
	 * work queue to establish a queue pair for the host to use
	 * to communicate with a destination via NVMe capsules
	 *
	 * @subsys:		the NVMe Fabric subsystem data structure used
	 *			to establish a connection to an NVMe Fabrics
	 *			target subsystem.
	 * @current_cntlid	The current controller (represented by the
	 *			cntlid value found from Identify Controller
	 *			data structure) that is being configured
	 *			for NVMe queue setup.
	 * @uuid:		A unique ID to be used for this connection
	 *			session (most likely will be of the standard
	 *			RFC-4122 UUID format).
	 * @stage:		What type of NVMe fabric connection for
	 *			ctrl_name we desire (see enum nvme_conn_stage
	 *			for values).
	 * @conn_ptr:		OUT void parameter that will point to the
	 *			specific fabric connection session
	 *			description construct of the host fabric
	 *			device.
	 *
	 *  Return Value:
	 *      0 for success,
	 *      any other value, error.
	 *
	 *  Notes:
	 *      will make a connection first, then create the necessary
	 *      queues requested.
	 */
	int (*connect_create_queue)(struct nvme_fabric_subsystem *subsys,
				    __u16 current_cntlid,
				    __u8 *uuid,
				    int stage,
				    void **conn_ptr);

	/*
	 * Function that builds an sglist that will be packaged and sent
	 * across the fabric.
	 *
	 * @
	 *
	 * @incapsule_num: An IN parameter that is the additional number of
	 *		   sgl elements to be built and packaged
	 *		   and tacked on at the end of the NVMe
	 *		   fabric capsule.  This number does NOT
	 *		   include the first sgl element that is
	 *		   a part of a normal NVMe command.
	 *
	 * @sglist: an OUT parameter that...points to the sglist elements
	 *	    tacked on at the end of an NVMe Fabric capsule
	 *          (incapsule_num sglist elements)??????
	 *
	 * Returns:
	 *      0 for success
	 *      anything else, ERROR
	 */
	int (*build_admin_sglist)(void *prp1, void *prp2,
				  int incapsule_len,
				  struct nvme_common_sgl_desc *sglist);

	/*
	 * OPTIONAL function for fabric transports to implement
	 * set the correct controller id to the subsystem.  It is
	 * assumed that fabric transports may keep some type of
	 * data structure to relate NVMe controllers found
	 * from a subsystem target and it's network connections.
	 * Per NVMe standard, the spec guaruntees
	 * an NVMe controller can be unique by subsystem name
	 * and cntlid field returned from an NVMe Identify
	 * Controller command.
	 *
	 * @subsys_name: Unique subsystem name the controller is
	 *		 found.
	 * @cntlid:	 The valid cntlid number on that controller
	 *		 discovered in the subsystem on the fabric
	 *		 target.
	 * Returns:
	 *      0 for success
	 *      anything else, error
	 */
	int (*finalize_cntlid)(char *subsys_name, __u16 cntlid);
};

int nvme_fabric_parse_addr(int address_type, char *address, int port,
			   struct nvme_fabric_addr *fabric_addr);
int nvme_fabric_register(char *nvme_class_name,
			 struct nvme_fabric_host_operations *new_fabric);
int nvme_fabric_unregister(struct nvme_fabric_subsystem *conn);
int nvme_fabric_discovery(struct nvme_fabric_addr *addr, int fabric,
			  int simulation);
int nvme_fabric_add_controller(char *subsys_name,
			       int fabric, int conn_type,
			       struct nvme_fabric_addr *address);
int nvme_fabric_remove_host_treenode(char *subsys_name, __u16 cntlid);
int nvme_fabric_set_instance(void);
void nvme_fabric_get_hostname(char *hostname);
void nvme_fabric_set_hostname(char *hostname);
void nvme_fabric_create_session_guid(__u8 *hnsid);
void *nvme_fabric_get_xport_context(void);


/******** nvme-sysfs.c function prototype definitions *********/

int nvme_sysfs_init(char *nvme_class_name);
void nvme_sysfs_exit(void);
ssize_t nvme_sysfs_do_add_discover_server(struct class *class,
		struct class_attribute *attr,
		const char *buf, size_t count);
ssize_t nvme_sysfs_show_discover_server(struct class *class,
					struct class_attribute *attr,
					char *buf);
ssize_t nvme_sysfs_do_add_subsystem(struct class *class,
				    struct class_attribute *attr,
				    const char *buf, size_t count);
ssize_t nvme_sysfs_show_add_subsystem(struct class *class,
				      struct class_attribute *attr,
				      char *buf);
ssize_t nvme_sysfs_do_remove_subsystem(struct class *class,
				       struct class_attribute *attr,
				       const char *buf, size_t count);
ssize_t nvme_sysfs_show_remove_subsystem(struct class *class,
		struct class_attribute *attr,
		char *buf);
ssize_t nvme_sysfs_do_remove_controller(struct class *class,
					struct class_attribute *attr,
					const char *buf, size_t count);
ssize_t nvme_sysfs_show_remove_controller(struct class *class,
		struct class_attribute *attr,
		char *buf);
ssize_t nvme_sysfs_do_set_hostname(struct class *class,
				   struct class_attribute *attr,
				   const char *buf, size_t count);
ssize_t nvme_sysfs_show_set_hostname(struct class *class,
				     struct class_attribute *attr,
				     char *buf);

#endif  /* _LINUX_NVME_FABRICS_H */
