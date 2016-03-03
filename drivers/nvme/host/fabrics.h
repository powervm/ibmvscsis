/*
 * fabrics.h - NVMe Fabrics implementation header for
 *	       host/initiator devices.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * This is used to implement a specific network fabric technology to the
 * NVMe Fabrics standard found in the NVMe specification.
 */

#ifndef _NVME_FABRICS_H
#define _NVME_FABRICS_H 1

#include <linux/in.h>
#include <linux/inet.h>

#define NVMF_MIN_QUEUE_SIZE	16
#define NVMF_MAX_QUEUE_SIZE	1024
#define NVMF_DEF_QUEUE_SIZE	128

/**
 * enum nvmf_parsing_opts - used to define the sysfs parsing options used.
 *
 * For example, defining NVMF_OPT_IPADDR in the
 * .required_opts and .allowed_opts field in struct nvmf_transport_opts
 * will allow the following sysfs manipulation on the command-line
 * when adding a fabric controller to a host connection:
 * echo "ipaddr=192.168.13.55" > /sys/class/nvme-fabrics/ctl/add_ctrl.
 */
enum {
	NVMF_OPT_ERR		= 0,
	NVMF_OPT_TRANSPORT	= 1 << 0,
	NVMF_OPT_NQN		= 1 << 1,
	NVMF_OPT_IPADDR		= 1 << 2,
	NVMF_OPT_PORT		= 1 << 3,
	NVMF_OPT_QUEUE_SIZE	= 1 << 4,
	NVMF_OPT_NR_IO_QUEUES	= 1 << 5,
	NVMF_OPT_TL_RETRY_COUNT	= 1 << 6,
};

/**
 * struct nvmf_ctrl_options - Used to hold the options specified
 *			      with the parsing opts enum.
 * @mask:	Used by the fabrics library to parse through sysfs options
 *		on adding a NVMe controller.
 * @transport:	Holds the fabric transport "technology name" (for a lack of
 *		better description) that will be used by an NVMe controller
 *		being added.
 * @subsysnqn:	Hold the fully qualified NQN subystem name (format defined
 *		in the NVMe specification, "NVMe Qualified Names").
 * @ipaddr:	IP network address that will be used by the host to communicate
 *		to the added NVMe controller.
 * @queue_size: Number of IO queue elements.
 * @nr_io_queues: Number of controller IO queues that will be established.
 * @tl_retry_count: Number of transport layer retries for a fabric queue before
 *                  kicking upper layer(s) error recovery.
 *
 * Example: echo ipaddr=192.168.13.55,transport=rdma,port=7,\
 * nqn=nqn.2015-01.com.example:nvme:nvm-subsystem-sn-d78432 \
 * > /sys/class/nvme-fabrics/ctl/add_ctrl on the command-line would
 * have transport = "rdma", ipaddr struct variable hold the
 * IP and port address, and
 * subsysnqn = "nqn.2015-01.com.example:nvme:nvm-subsystem-sn-d78432"
 */
struct nvmf_ctrl_options {
	unsigned		mask;
	char			*transport;
	char			*subsysnqn;
	struct sockaddr_in	ipaddr;
	size_t			queue_size;
	unsigned int		nr_io_queues;
	unsigned short		tl_retry_count;
};

/*
 * struct nvmf_transport_ops - used to register a specific
 *			       fabric implementation of NVMe fabrics.
 * @entry:		Used by the fabrics library to add the new
 *			registration entry to its linked-list internal tree.
 * @name:		Name of the NVMe fabric driver implementation.
 * @required_opts:	sysfs command-line options that must be specified
 *			when adding a new NVMe controller.
 * @allowed_opts:	sysfs command-line options that can be specified
 *			when adding a new NVMe controller.
 * @create_ctrl():	function pointer that points to a non-NVMe
 *			implementation-specific fabric technology
 *			that would go into starting up that fabric
 *			for the purpose of conneciton to an NVMe controller
 *			using that fabric technology.
 *
 * Notes:
 *	1. At minimum, 'required_opts' and 'allowed_opts' should
 *	   be set to the same enum parsing options defined earlier.
 *	2. create_ctrl() must be defined (even if it does nothing)
 */
struct nvmf_transport_ops {
	struct list_head	entry;
	const char		*name;
	int			required_opts;
	int			allowed_opts;
	int (*create_ctrl)(struct device *dev, struct nvmf_ctrl_options *opts);
};

int nvmf_reg_read32(struct nvme_ctrl *ctrl, u32 off, u32 *val);
int nvmf_reg_read64(struct nvme_ctrl *ctrl, u32 off, u64 *val);
int nvmf_reg_write32(struct nvme_ctrl *ctrl, u32 off, u32 val);
int nvmf_connect_admin_queue(struct nvme_ctrl *ctrl, const char *subsysnqn,
		uuid_le *hostsid, u16 *cntlid);
int nvmf_connect_io_queue(struct nvme_ctrl *ctrl, const char *subsysnqn,
		uuid_le *hostsid, u16 cntlid, u16 qid);
void nvmf_register_transport(struct nvmf_transport_ops *ops);
void nvmf_unregister_transport(struct nvmf_transport_ops *ops);
void nvmf_identify_attrs(struct nvme_ctrl *ctrl, struct nvme_id_ctrl *id);
void nvmf_free_options(struct nvmf_ctrl_options *opts);
const char *nvmf_get_subsysnqn(struct nvme_ctrl *ctrl);

#endif /* _NVME_FABRICS_H */
