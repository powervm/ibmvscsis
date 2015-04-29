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

/*
 * Capsule Command Type opcodes.  In a NVMe Fabric
 * capsule, there can be commands that are related to
 * normal NVMe commands, or there can be commands that are
 * related specifically to an NVMe fabric property.
 * Part of NVMe Org proposal "Fabrics TP 002".
 */
#define CCTYPE_NVME_CMD			0x0
#define CCTYPE_NVME_RSP			0x1
#define CCTYPE_PROPERTY_SET_CMD		0x2
#define CCTYPE_PROPERTY_SET_RSP		0x3
#define CCTYPE_PROPERTY_GET_CMD		0x4
#define CCTYPE_PROPERTY_GET_RSP		0x5
#define CCTYPE_CPLQUEUE_UPDATE_CMD	0x6
#define CCTYPE_CPLQUEUE_UPDATE_RSP	0x7
#define CCTYPE_CONNECT_CMD		0x8
#define CCTYPE_CONNECT_RSP		0x9
#define CCTYPE_DISCOVER_CMD		0xA
#define CCTYPE_DISCOVER_RSP		0xB
#define CCTYPE_DISCOVER_GETINFO_CMD	0xC
#define CCTYPE_DISCOVER_GETINFO_RSP	0xD

/*
 * For property values, i.e. "virtual registers",
 * they can either hole 4 byte values or 8 byte
 * values.  Thus, the 'attrib' variable in
 * Set/Get Property commands defines the size of
 * the value to be written to a given property.
 * Part of NVMe Org proposal "Fabrics TP 002".
 */
#define PROPERTY_ATTRIB_4BYTES		0x0
#define PROPERTY_ATTRIB_8BYTES		0x1

#define FABRIC_STRING_MAX	50 /* CAYTONCAYTON - this should be 1K! */
#define ADDRSIZE		128

#define NVME_FABRIC_IQN_MINLEN	16
#define NVME_FABRIC_IQN_MAXLEN	1024

enum nvme_fabric_type {
	NVME_FABRIC_PCIE = 0,     /* PCIe Fabric */
	NVME_FABRIC_RDMA = 1,     /* RDMA Fabrics; IBA, iWARP, ROCE, ... */
	NVME_FABRIC_FC   = 2,     /* Fibre Channel Fabric */
	NVME_FABRIC_OMNIPATH = 3, /* Intel OMNI PATH RDMA technology */
	/* Future NVMe Fabrics */
};

/* struct used to capture dns network address */
struct dns_addr_type {
	__u8 octet[255];
	__u16 tcp_udp_port;
};

/* struct used to capture ipv4 network address */
struct ipv4_addr_type {
	__u8 octet[4];
	__u16 tcp_udp_port;
};

/* struct used to capture ipv6 network address */
struct ipv6_addr_type {
	__u8 octet[16];
	__u16 tcp_udp_port;
};

/* struct used to capture a MAC address */
struct emac_addr_type {
	__u8 octet[6];
};

/* struct used to capture an infiniband address */
struct iba_addr_type {
	__u8 octet[16];
};

/* struct used to capture a fibre channel address */
struct fc_addr_type {
	/*
	 * this represents an 8 or 16 byte wwn or
	 * wwpn for fibre channel devices
	 */
	__u8 octet[16];
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
	__u8 what_fabric_type;

	struct fabric_type {
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
				__u8 rsvd[11];

				/*
				 * cpl queue # which this
				 * completion was successful
				 */
				__le16 cqidf;

				__u8 rsvd2[2];
			} nvme_rsp;

			/*
			 * capsule hdr iff cctype == 0x02h,
			 * property set command.
			 */
			struct {
				__u8 cctype;
				/*
				 * bits 7:3 are rsvd and
				 * should be 0. This field
				 * represents the size
				 * in bytes of the value the property
				 * (i.e., 'virtual register') saves.
				 */
				__u8 attrib;

				__le16 rsvd;

				/* Since this struct is used to represent a
				 *'virtual register', i.e. a property, this
				 * represents the offset property in the
				 * 'virtual BAR' to set.
				 */
				__le32 ofst;

				/*
				 * value to be written to the property
				 * location. if attrib == 1, then it's an
				 * 8 byte value,
				 * otherwise it is a 4 byte value.
				 */
				__le64 valu;

			} prpset_cmd;

			/*
			 * capsule hdr iff cctype == 0x03h,
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
			 * capsule hdr iff cctype == 0x04h,
			 * property get command
			 */
			struct {
				__u8 cctype;
				/*
				 * bits 7:3 are rsvd and
				 * should be 0. This represents the size
				 * in bytes of the value the property
				 * (i.e., 'virtual register') saves.
				 */
				__u8 attrib;
				__le16 rsvd;

				/* Since this struct is used to represent a
				 *'virtual register', i.e. a property, this
				 * represents the offset property in the
				 * 'virtual BAR' to read.
				 */
				__le32 ofst;
				__le64 rsvd2;
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
			 * capsule hdr iff cctype == 0x06h,
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
			 * capsule hdr iff cctype == 0x07h,
			 * completion queue update command.
			 */
			struct {
				__u8 cctype;

				/* cpl capsule packet response */
				__u8 sts;
				__u8 rsvd[14];
			} cplqueue_rsp;

			/*
			 * capsule hdr iff cctype == 0x08h, connect command.
			 */
			struct {
				__u8 cctype;

				/*
				 * Specifies type of connection to be
				 * established-
				 * 0h - NVMe discovery connection
				 * 1h - NVMe Admin queue connection
				 * 2h - NVMe IOQ pair connection
				 * any other value is rsvd
				 */
				__u8 cntype;

				/*
				 * indicates next capsule will be a
				 * security_send capsule.
				 */
				__u8 ioqsse;

				__u8 rsvd;

				/*
				 * Set to value 0 for discovery and admin
				 * cntype. For NVMe IOQ pair, field must
				 * match the QID value used in an earlier,
				 * successful NVMe Create I/O Submission
				 * Queue Admin cmd.  Otherwise, it's
				 * an CSQID Does Not Exist error.
				 */
				__le16 csqid;

				/*
				 * Set to value 0 for discovery & admin
				 * cntype. For NVMe IOQ pair, field must
				 * match the QID value set by an earlier
				 * Create I/O completion queue Admin cmd.
				 * Otherwise, it is a CCQID Does Not Exist
				 * error.
				 */
				__le16 ccqid;

				/*
				 * This field contains an NVMe controller
				 * session unique identifier.  The following
				 * cases determine the value of this field:
				 *
				 * discovery session           = 0
				 * new admin QP connection     = 0
				 * current admin QP connection = non-zero
				 *				 value
				 *				 created from
				 *				 Admin QP
				 *				 connection.
				 * new IO QP connection        = non-zero
				 *				 value created
				 *				 from Admin QP
				 *				 connection.
				 */
				__le32 cnsid;

				__le32 rsvd2;
			} connect_cmd;

			/*
			 * capsule hdr iff cctype == 0x09h,
			 * connect response command.
			 */
			struct {
				__u8 cctype;
				/*
				 * return success or error status on
				 * Connect cmd.
				 */
				__u8 sts;

				/*
				 * This field contains an NVMe controller
				 * session unique identifier.  The following
				 * cases determine the value of this field:
				 *
				 * discovery session             = 0
				 * new or current admin QP conn. = non-zero
				 *				 value
				 *				 created from
				 *				 Admin QP
				 *				 connection.
				 * new IO QP connection          = non-zero
				 *				 value created
				 *				 from Admin QP
				 *				 connection.
				 */
				/*
				 * BUG?/TODO? There seems to be problem
				 * w/using __le32 here, it
				 * makes the union 8 bytes bigger!?!
				 * Cannot figure it out...long effort
				 * to pinpoint this...
				 */
				__u8 cnsid[4];

				__u8 rsvd[10];
			} connect_rsp;

			/*
			 * capsule hdr iff cctype == 0x0Ah,
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
			 * capsule hdr iff cctype == 0x0Bh,
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

	struct nvme_common_command capsule_body;

	/* This is set to NULL if no data is associated w/the command */
	void *data;
};

/*
 * NVMe response "child struct" of nvme_capsule_packet.
 * An object of this struct will be pointed by 'child'
 * in the base struct nvme_capsule_packet.
 */
struct nvme_rsp_capsule {
	struct nvme_common_completion capsule_body;

	/* This is set to NULL if no data is associate w/the command */
	void *data;
};

/*
 * Connect cmd "child struct" of nvme_capsule_packet.
 * An object of this struct will be pointed by 'child'
 * in the base struct nvme_capsule_packet.
 */
struct connect_cmd_capsule {
	struct {
		/*
		 * Part of TP 002, this is the host NVMe session
		 * Globally Unique Identifier.  This is a host-generated
		 * 128 bit value using RFC-4122 UUID format.
		 */
		__u8 hnsid[16];

		/*
		 * Part of TP 002, This is an NVMe Data block SGL that
		 * describes the in-capsule offset location from byte 0 of
		 * the "Host IQN Name String". The length value must be
		 * between NVME_FABRIC_IQN_MINLEN - NVME_FABRIC_IQN_MAXLEN.
		 */
		struct nvme_common_sgl_dblk hnsgl;

		/*
		 * Part of TP 002, this is the NVMe Data Block SGL that
		 * describes the in-capsule offest location from byte 0
		 * of the Controller IQN Name String field.  The length
		 * value must be between
		 * NVME_FABRIC_IQN_MINLEN - NVME_FABRIC_IQN_MAXLEN.
		 */
		struct nvme_common_sgl_dblk cnsgl;

		/*
		 * Part of TP 002, string that uniquely identifies the host.
		 * String should be terminated with a '\0'. Thus size
		 * of the entry will really be NVME_FABRIC_IQN_MAXLEN - 1
		 * because of the '\0' (and we don't want to make it
		 * NVME_FABRIC_IQN_MAXLEN + 1 and get the bytes
		 * out-of-alignment). Note that IQN spec says max is
		 * 223.
		 */
		char host_iqn_name[NVME_FABRIC_IQN_MAXLEN];

		/*
		 * Part of TP 002, string that uniquely identifies the host.
		 * String should be terminated with a '\0'. Thus size
		 * of the entry will really be NVME_FABRIC_IQN_MAXLEN - 1
		 * because of the '\0' (and we don't want to make it
		 * NVME_FABRIC_IQN_MAXLEN + 1 and get the bytes
		 * out-of-alignment). Note that IQN spec says max is
		 * 223.
		 */
		char ctrl_iqn_name[NVME_FABRIC_IQN_MAXLEN];
	} capsule_body;
};

/*
 * Connect rsp "child struct" of nvme_capsule_packet.
 * An object of this struct will be pointed by 'child'
 * in the base struct nvme_capsule_packet.
 */
struct connect_rsp_capsule {
	struct {
		/*
		 * Part of TP 002, this is the host NVMe session
		 * Globally Unique Identifier.  This is a host-generated
		 * 128 bit value using RFC-4122 UUID format.
		 */
		__u8 hnsid[16];

		/*
		 * Part of TP 002, this is the fabric address the
		 * host should use instead for a discovery sequence
		 * (it's the referral network address).
		 */
		struct nvme_fabric_addr rfad;
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

extern char hostname[FABRIC_STRING_MAX];

extern int instance;

struct aq {
	/* struct *nvme_aq_stuff */
	void	 *fabric_aq_conn;
};

struct ioq {
	/* This should be a "list" of IOQs
	  struct *nvme_ioq_stuff
	*/
	void	 *fabric_ioq_conn;
};

struct nvme_conn {
	struct list_head	node; /*List of all active connections*/
	int			state; /*CONN_PROBE, CONN_AQ, CONN_IOQ, ... */
	char			*ctrlrname[FABRIC_STRING_MAX];
	char			*subsysname[FABRIC_STRING_MAX];
	char			*Address[ADDRSIZE];
	int			port;
	struct aq		*aq;
	struct ioq		*ioq;
};

/* TODO: Add sizeof BUILD_BUG_ON() checks to these struct sizes
 * when space is more fleshed out enough to put a size on these things.
 */

/*
 * This struct would be used to embed more than 1 sgl descriptors
 * with a data block and have that pointed by
 * '*data' in the nvme capsule structs.
 *
 */
struct nvme_sgl_data_capsule {
	struct nvme_common_sgl_desc sgllist[NVME_SGL_SEGMENT_MAXSIZE];
	void *data;
};

/*
 * TODO:  Think this will be the fundamental device type for
 * all NVMe fabric device drivers??
 *
 * For each device instance, it can represent 1 controller discovered.
 */
struct nvme_fabric_dev {

	/* generic, pci-e free, nvme implementation stuff */
	struct nvme_common_dev *dev;

	/* needed for default fabric address to describe host */
	__le64 fabric_address;

	/*
	 * there needs to be a way to go between the local (host)
	 * nvme device and the remote (ep) nvme device.  Host
	 * will think it's the nvme device but this tells it
	 * 'to go here instead'.
	 */
	void *xport_context;

	/*
	 * API fabric-specific drivers (RDMA, INFINIBAND, etc)
	 * must implement
	 */
	struct nvme_fabric_host_operations *fops;

	/* any fabric, implementation specific structs to associate */
	void *fabric_private;
};

struct nvme_fabric_host_operations {
	struct module *owner;

	/*
	 *  Function that takes the specific transport context being
	 *  written for this specific driver plus an NVMe admin command
	 *  and preps the contents (like for example, package
	 *  the data into an NVMe capsule) for an NVMe admin cmd
	 *  submission that will then be sent by this function over the
	 *  fabric.
	 *
	 *  @dev:    Current nvme_fabric_dev being operated on.
	 *  @cmd:    The NVMe Admin command that will be used to prepare
	 *           the command transmission over the fabric.
	 *  @result: pointer to DW0 of the NVMe completion packet, which
	 *           data contents are specific to the admin command sent.
	 *
	 *  Return Value:
	 *      0 for Success
	 *      Any other value, error
	 *
	 *  Caveats:
	 *      if the function does not return 0, result parameter
	 *      must be set to 0.
	 *
	 *  Notes:
	 *      This function is based on nvmerp_submit_aq_cmd()
	 *      of the demo.
	 */
	int (*prepsend_admin_cmd)(struct nvme_fabric_dev *dev,
				  struct nvme_common_command *cmd,
				  __u32 *result);

	/*
	 * Function that takes the specific fabric transport and
	 * an NVMe I/O command and packages the contents (in say,
	 * an NVME capsule for example) that will then be sent
	 * over the fabric by this function.
	 *
	 * @dev:	The current nvme_fabric device being operated on.
	 * @queue_num:	Which nvme_queue nvmeq to use in dev.
	 *\		TODO: Not sure 'why' nvmeq was used here
	 *		other than it was needed in
	 *		the demo.
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
	 */
	int (*prepsend_io_cmd)(struct nvme_fabric_dev *dev,
			       __u16 queue_num,
			       struct nvme_command *cmd,
			       __u32 len);

	/*
	 * The NVMe fabric discover function responsible for
	 * discovery on all fabric paths.  For each discovery,
	 * the name of a controller and the associated fabric
	 * ports will be returned.
	 */
	/*
	    "discover sequence (generic, nvme-free)"
		-"go and get # of controllers on the fabric"
		- track # of controllers discovered
		-structure that keeps # of ns for each dev,
		 name of each controller, fabric address for each controller
	*/
	int (*probe)(char *address, int port,
		     int fabric, struct nvme_conn *disc_conn);

	/*TODO*/
	void (*disconnect)(char *address, int port, int fabric);

	/*
	 * Function that establishes a fabric-specific connection with
	 * the ep, as well as create the send work queue and the receive
	 * work queue to establish a queue pair for the host to use
	 * to communicate NVMe capsules with the ep.
	 *
	 *  @conn: The connection session description construct
	 *         of the host fabric device.
	 *
	 *  Return Value:
	 *      0 for success,
	 *      any other value, error.
	 *
	 *  Notes:
	 *      This function is based on connect_queue() from
	 *      the demo.  From Dave, this function will
	 *      actually make a connection first, then create
	 *      the queues.
	 */
	int (*connect_create_queue)(char *addr, int port, int fabric,
				    struct nvme_conn *nvme_conn, int stage);

	/*
	 * Function that stops processing queues and destroys
	 * the queue resources.
	 *
	 * @conn: The connection session resources construct
	 *        of the host fabric device.
	 *
	 * Notes:
	 *     This function is base on disconnect_queue() from
	 *     the demo.
	 */
	void (*stop_destroy_queues)(int TODO);
};

/*
 *
 * Some notes:
 *      *If you want the device to have an ioctl associated with
 *       it, define a block_device_operations with it.  This
 *       would not be a core function of
 *       nvme_fabric_host_operations.
 *
 */

int nvme_fabric_register(char *nvme_class_name,
			 struct nvme_fabric_host_operations *new_fabric);
int nvme_fabric_unregister(int TODO);
int nvme_fabric_discovery(char *address, int port, int fabric);
int nvme_fabric_add_subsystem(char *subsystem_name, char *ctrlr_name,
			      int fabric, char *address, int port);
int nvme_fabric_remove_endpoint(char *address, int port, int fabric);

/******** nvme-sysfs.c function prototype definitions *********/

int nvme_sysfs_init(char *nvme_class_name);
void nvme_sysfs_exit(void);
ssize_t nvme_sysfs_do_add_endpoint(struct class *class,
				   struct class_attribute *attr,
				   const char *buf, size_t count);
ssize_t nvme_sysfs_do_add_subsystem(struct class *class,
				    struct class_attribute *attr,
				    const char *buf, size_t count);
ssize_t nvme_sysfs_show_add_endpoint(struct class *class,
				     struct class_attribute *attr,
				     char *buf);
ssize_t nvme_sysfs_do_remove_endpoint(struct class *class,
				      struct class_attribute *attr,
				      const char *buf, size_t count);
ssize_t nvme_sysfs_do_set_hostname(struct class *class,
				   struct class_attribute *attr,
				   const char *buf, size_t count);
ssize_t nvme_sysfs_show_set_hostname(struct class *class,
				     struct class_attribute *attr,
				     char *buf);
ssize_t nvme_sysfs_show_add_subsystem(struct class *class,
				      struct class_attribute *attr,
				      char *buf);

#endif  /* _LINUX_NVME_FABRICS_H */
