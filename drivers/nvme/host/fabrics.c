/*
 * fabrics.c - NVMe Fabrics implementation library for
 *	       host/initiator devices.
 *
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
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * This is used to implement a specific network fabric technology to the
 * NVMe Fabrics standard found in the NVMe specification.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/parser.h>
#include "nvme.h"
#include "fabrics.h"

static char *nvmf_host_nqn;
static LIST_HEAD(nvmf_transports);
static DEFINE_MUTEX(nvmf_transports_mutex);

/**
 * nvmf_identify_attrs() -  Set fabrics identify controller attributes
 * @ctrl:	Host NVMe controller instance which we got the identify
 *              indormation.
 * @id:         Identify controller layout we got from the controller
 */
void nvmf_identify_attrs(struct nvme_ctrl *ctrl, struct nvme_id_ctrl *id)
{
	ctrl->icdoff = le16_to_cpu(id->icdoff);
	ctrl->ioccsz = le32_to_cpu(id->ioccsz);
	ctrl->iorcsz = le32_to_cpu(id->iorcsz);
}
EXPORT_SYMBOL_GPL(nvmf_identify_attrs);

/**
 * nvmf_reg_read32() -  NVMe Fabrics "Property Get" API function.
 * @ctrl:	Host NVMe controller instance maintaining the admin
 *		queue used to submit the property read command to
 *		the allocated NVMe controller resource on the target system.
 * @off:	Starting offset value of the targeted property
 *		register (see the fabrics section of the NVMe standard).
 * @val:	OUTPUT parameter that will contain the value of
 *		the property after a successful read.
 *
 * Used by the host system to retrieve a 32-bit capsule property value
 * from an NVMe controller on the target system.
 *
 * ("Capsule property" is an "PCIe register concept" applied to the
 * NVMe fabrics space.)
 *
 * Return:
 *	0: successful read
 *	> 0: NVMe error status code
 *	< 0: Linux errno error code
 */
int nvmf_reg_read32(struct nvme_ctrl *ctrl, u32 off, u32 *val)
{
	struct nvme_command cmd;
	struct nvme_completion cqe;
	int ret;

	memset(&cmd, 0, sizeof(cmd));
	cmd.prop_get.opcode = nvme_fabrics_command;
	cmd.prop_get.cctype = NVMF_CC_PROP_GET;
	cmd.prop_get.offset = cpu_to_le32(off);

	ret = __nvme_submit_sync_cmd(ctrl->admin_q, &cmd, &cqe, NULL, 0, 0,
			NVME_QID_ANY, 0, 0);
	if (ret >= 0)
		*val = le64_to_cpu(cqe.result64);
	return ret;
}
EXPORT_SYMBOL_GPL(nvmf_reg_read32);

/**
 * nvmf_reg_read64() -  NVMe Fabrics "Property Get" API function.
 * @ctrl:	Host NVMe controller instance maintaining the admin
 *		queue used to submit the property read command to
 *		the allocated controller resource on the target system.
 * @off:	Starting offset value of the targeted property
 *		register (see the fabrics section of the NVMe standard).
 * @val:	OUTPUT parameter that will contain the value of
 *		the property after a successful read.
 *
 * Used by the host system to retrieve a 64-bit capsule property value
 * from an NVMe controller on the target system.
 *
 * ("Capsule property" is an "PCIe register concept" applied to the
 * NVMe fabrics space.)
 *
 * Return:
 *	0: successful read
 *	> 0: NVMe error status code
 *	< 0: Linux errno error code
 */
int nvmf_reg_read64(struct nvme_ctrl *ctrl, u32 off, u64 *val)
{
	struct nvme_command cmd;
	struct nvme_completion cqe;
	int ret;

	memset(&cmd, 0, sizeof(cmd));
	cmd.prop_get.opcode = nvme_fabrics_command;
	cmd.prop_get.cctype = NVMF_CC_PROP_GET;
	cmd.prop_get.attrib = 1;
	cmd.prop_get.offset = cpu_to_le32(off);

	ret = __nvme_submit_sync_cmd(ctrl->admin_q, &cmd, &cqe, NULL, 0, 0,
			NVME_QID_ANY, 0, 0);
	if (ret >= 0)
		*val = le64_to_cpu(cqe.result64);
	return ret;
}
EXPORT_SYMBOL_GPL(nvmf_reg_read64);

/**
 * nvmf_reg_write32() -  NVMe Fabrics "Property Write" API function.
 * @ctrl:	Host NVMe controller instance maintaining the admin
 *		queue used to submit the property read command to
 *		the allocated NVMe controller resource on the target system.
 * @off:	Starting offset value of the targeted property
 *		register (see the fabrics section of the NVMe standard).
 * @val:	Input parameter that contains the value to be
 *		written to the property.
 *
 * Used by the NVMe host system to write a 32-bit capsule property value
 * to an NVMe controller on the target system.
 *
 * ("Capsule property" is an "PCIe register concept" applied to the
 * NVMe fabrics space.)
 *
 * Return:
 *	0: successful write
 *	> 0: NVMe error status code
 *	< 0: Linux errno error code
 */
int nvmf_reg_write32(struct nvme_ctrl *ctrl, u32 off, u32 val)
{
	struct nvme_command cmd;

	memset(&cmd, 0, sizeof(cmd));
	cmd.prop_set.opcode = nvme_fabrics_command;
	cmd.prop_set.cctype = NVMF_CC_PROP_SET;
	cmd.prop_get.attrib = 0;
	cmd.prop_set.offset = cpu_to_le32(off);
	cmd.prop_set.value = cpu_to_le64(val);

	return __nvme_submit_sync_cmd(ctrl->admin_q, &cmd, NULL, NULL, 0, 0,
			NVME_QID_ANY, 0, 0);
}
EXPORT_SYMBOL_GPL(nvmf_reg_write32);

/**
 * nvmf_connect_admin_queue() - NVMe Fabrics Admin Queue "Connect"
 *				API function.
 * @ctrl:	Host nvme controller instance used to request
 *              a new NVMe controller allocation on the target
 *              system and  establish an NVMe Admin connection to
 *              that controller.
 * @subsysnqn:	NVMe qualified name that uniquely identifies the
 *		NVMe subsystem that contains an NVMe controller the
 *		host is making a connection to. It is a
 *		UTF-8 string that has two supported naming formats
 *		(refer to "NVMe Qualified Names" in the NVMe standard).
 * @hostsid:	NVMe qualified name that uniquely identifies the
 *		host system device.  Value includes using uuid_le_gen()
 *		kernel API function (refer to "NVMe Qualified Names" in the
 *		NVMe Standard).
 * @cntlid:	OUTPUT parameter that will contain a valid cntlid value of
 *		a successfully allocated NVMe controller (found in that
 *		controller's NVMe Identify Controller data structure)
 *		on the target system.
 *
 * This function enables an NVMe host device to request a new allocation of
 * an NVMe controller resource on a target system as well establish a
 * fabrics-protocol connection of the NVMe Admin queue between the
 * host system device and the allocated NVMe controller on the
 * target system via a NVMe Fabrics "Connect" command.
 *
 * Return:
 *	0: success
 *	> 0: NVMe error status code
 *	< 0: Linux errno error code
 *
 */
int nvmf_connect_admin_queue(struct nvme_ctrl *ctrl, const char *subsysnqn,
		uuid_le *hostsid, u16 *cntlid)
{
	struct nvme_command cmd;
	struct nvme_completion cqe;
	struct nvmf_connect_data *data;
	int ret;

	memset(&cmd, 0, sizeof(cmd));
	cmd.connect.opcode = nvme_fabrics_command;
	cmd.connect.cctype = NVMF_CC_CONNECT;
	cmd.connect.qid = 0;
	cmd.connect.sqsize = cpu_to_le16(ctrl->sqsize);

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	memcpy(&data->hostsid, hostsid, sizeof(uuid_le));
	data->cntlid = cpu_to_le16(0xffff);
	strncpy(data->subsysnqn, subsysnqn, NVMF_NQN_SIZE);
	strncpy(data->hostnqn, nvmf_host_nqn, NVMF_NQN_SIZE);

	ret = __nvme_submit_sync_cmd(ctrl->admin_q, &cmd, &cqe,
			data, sizeof(*data), 0, NVME_QID_ANY, 1,
			BLK_MQ_REQ_RESERVED);
	if (ret) {
		dev_err(ctrl->dev,
			"admin queue connect command failed (%d).\n", ret);
		goto out_free_data;
	}

	*cntlid = le16_to_cpu(cqe.result16);

out_free_data:
	kfree(data);
	return ret;
}
EXPORT_SYMBOL_GPL(nvmf_connect_admin_queue);

/**
 * nvmf_connect_io_queue() - NVMe Fabrics I/O Queue "Connect"
 *			     API function.
 * @ctrl:	Host nvme controller instance used to establish an
 *		NVMe I/O queue connection to the already allocated NVMe
 *		controller on the target system.
 * @subsysnqn:	NVMe qualified name that uniquely identifies the
 *		NVMe subsystem that contains an NVMe controller the
 *		host is making a connection to. It is a
 *		UTF-8 string that has two supported naming formats
 *		(refer to "NVMe Qualified Names" in the NVMe standard).
 * @hostsid:	NVMe qualified name that uniquely identifies the
 *		host system device.  Value includes using uuid_le_gen()
 *		kernel API function (refer to "NVMe Qualified Names" in the
 *		NVMe Standard).
 * @cntlid:	Controller ID found in 'subsysnqn' that the host system
 *		is trying to establish a new NVMe I/O queue conneciton.
 * @qid:	NVMe I/O queue number for the new I/O connection between
 *		host and target (note qid == 0 is illegal as this is
 *		the Admin queue, per NVMe standard).
 *
 * This function issues a fabrics-protocol connection
 * of a NVMe I/O queue (via NVMe Fabrics "Connect" command)
 * between the host system device and the allocated NVMe controller
 * on the target system.
 *
 * Return:
 *	0: success
 *	> 0: NVMe error status code
 *	< 0: Linux errno error code
 */
int nvmf_connect_io_queue(struct nvme_ctrl *ctrl, const char *subsysnqn,
		uuid_le *hostsid, u16 cntlid, u16 qid)
{
	struct nvme_command cmd;
	struct nvmf_connect_data *data;
	int ret;

	memset(&cmd, 0, sizeof(cmd));
	cmd.connect.opcode = nvme_fabrics_command;
	cmd.connect.cctype = NVMF_CC_CONNECT;
	cmd.connect.qid = cpu_to_le16(qid);
	cmd.connect.sqsize = cpu_to_le16(ctrl->sqsize);

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	memcpy(&data->hostsid, hostsid, sizeof(uuid_le));
	data->cntlid = cpu_to_le16(cntlid);
	strncpy(data->subsysnqn, subsysnqn, NVMF_NQN_SIZE);
	strncpy(data->hostnqn, nvmf_host_nqn, NVMF_NQN_SIZE);

	ret = __nvme_submit_sync_cmd(ctrl->connect_q, &cmd, NULL,
			data, sizeof(*data), 0, qid, 1, BLK_MQ_REQ_RESERVED);
	if (ret)
		dev_err(ctrl->dev,
			"I/O queue connect command failed (%d).\n", ret);

	kfree(data);
	return ret;
}
EXPORT_SYMBOL_GPL(nvmf_connect_io_queue);

/**
 * nvmf_register_transport() - NVMe Fabrics Library registration function.
 * @ops:	Transport ops instance to be registered to the
 *		common fabrics library.
 *
 * API function that registers the type of specific transport fabric
 * being implemented to the common NVMe fabrics library. Part of
 * the overall init sequence of starting up a fabrics driver.
 */
void nvmf_register_transport(struct nvmf_transport_ops *ops)
{
	mutex_lock(&nvmf_transports_mutex);
	list_add_tail(&ops->entry, &nvmf_transports);
	mutex_unlock(&nvmf_transports_mutex);
}
EXPORT_SYMBOL_GPL(nvmf_register_transport);

/**
 * nvmf_unregister_transport() - NVMe Fabrics Library unregistration function.
 * @ops:	Transport ops instance to be unregistered from the
 *		common fabrics library.
 *
 * Fabrics API function that unregisters the type of specific transport
 * fabric being implemented from the common NVMe fabrics library.
 * Part of the overall exit sequence of unloading the implemented driver.
 */
void nvmf_unregister_transport(struct nvmf_transport_ops *ops)
{
	mutex_lock(&nvmf_transports_mutex);
	list_del(&ops->entry);
	mutex_unlock(&nvmf_transports_mutex);
}
EXPORT_SYMBOL_GPL(nvmf_unregister_transport);

static struct nvmf_transport_ops *nvmf_lookup_transport(
		struct nvmf_ctrl_options *opts)
{
	struct nvmf_transport_ops *ops;

	lockdep_assert_held(&nvmf_transports_mutex);

	list_for_each_entry(ops, &nvmf_transports, entry) {
		if (strcmp(ops->name, opts->transport) == 0)
			return ops;
	}

	return NULL;
}

static const match_table_t opt_tokens = {
	{ NVMF_OPT_TRANSPORT,		"transport=%s"		},
	{ NVMF_OPT_IPADDR,		"ipaddr=%s"		},
	{ NVMF_OPT_PORT,		"port=%d"		},
	{ NVMF_OPT_NQN,			"nqn=%s"		},
	{ NVMF_OPT_QUEUE_SIZE,		"queue_size=%d"		},
	{ NVMF_OPT_NR_IO_QUEUES,	"nr_io_queues=%d"	},
	{ NVMF_OPT_TL_RETRY_COUNT,	"tl_retry_count=%d"	},
	{ NVMF_OPT_ERR,			NULL			}
};

static int nvmf_parse_ipaddr(struct sockaddr_in *in_addr, char *p)
{
	u8 *addr = (u8 *)&in_addr->sin_addr.s_addr;
	size_t buflen = strlen(p);

	/* XXX: handle IPv6 addresses */

	if (buflen > INET_ADDRSTRLEN)
		return -EINVAL;
	if (in4_pton(p, buflen, addr, '\0', NULL) == 0)
		return -EINVAL;
	in_addr->sin_family = AF_INET;
	return 0;
}

static int nvmf_parse_options(struct nvmf_ctrl_options *opts,
		const char *buf)
{
	substring_t args[MAX_OPT_ARGS];
	char *options, *o, *p;
	int token, ret = 0;

	/* Set defaults */
	opts->queue_size = NVMF_DEF_QUEUE_SIZE;
	opts->nr_io_queues = num_online_cpus();
	opts->tl_retry_count = 2;

	options = o = kstrdup(buf, GFP_KERNEL);
	if (!options)
		return -ENOMEM;

	while ((p = strsep(&o, ",\n")) != NULL) {
		if (!*p)
			continue;

		token = match_token(p, opt_tokens, args);
		opts->mask |= token;
		switch (token) {
		case NVMF_OPT_TRANSPORT:
			p = match_strdup(args);
			if (!p) {
				ret = -ENOMEM;
				goto out;
			}
			opts->transport = p;
			break;
		case NVMF_OPT_NQN:
			p = match_strdup(args);
			if (!p) {
				ret = -ENOMEM;
				goto out;
			}
			opts->subsysnqn = p;
			break;
		case NVMF_OPT_IPADDR:
			p = match_strdup(args);
			if (!p) {
				ret = -ENOMEM;
				goto out;
			}
			ret = nvmf_parse_ipaddr(&opts->ipaddr, p);
			kfree(p);
			if (ret)
				goto out;
			break;
		case NVMF_OPT_PORT:
			if (match_int(args, &token)) {
				ret = -EINVAL;
				goto out;
			}
			opts->ipaddr.sin_port = cpu_to_be16(token);
			break;
		case NVMF_OPT_QUEUE_SIZE:
			if (match_int(args, &token)) {
				ret = -EINVAL;
				goto out;
			}
			if (token < NVMF_MIN_QUEUE_SIZE ||
			    token > NVMF_MAX_QUEUE_SIZE) {
				pr_err("Invalid queue_size %d\n", token);
				ret = -EINVAL;
				goto out;
			}
			opts->queue_size = token;
			break;
		case NVMF_OPT_NR_IO_QUEUES:
			if (match_int(args, &token)) {
				ret = -EINVAL;
				goto out;
			}
			if (token <= 0) {
				pr_err("Invalid number of IOQs %d\n", token);
				ret = -EINVAL;
				goto out;
			}
			opts->nr_io_queues = min_t(unsigned int,
					num_online_cpus(), token);
			break;
		case NVMF_OPT_TL_RETRY_COUNT:
			if (match_int(args, &token)) {
				ret = -EINVAL;
				goto out;
			}
			if (token < 0) {
				pr_err("Invalid tl_retry_count %d\n", token);
				ret = -EINVAL;
				goto out;
			}
			opts->tl_retry_count = token;
			break;
		default:
			pr_warn("unknown parameter or missing value '%s' in ctrl creation request\n",
				p);
			ret = -EINVAL;
			goto out;
		}
	}

out:
	kfree(options);
	return ret;
}

static int nvmf_check_required_opts(struct nvmf_ctrl_options *opts,
		unsigned int required_opts)
{
	if ((opts->mask & required_opts) != required_opts) {
		int i;

		for (i = 0; i < ARRAY_SIZE(opt_tokens); i++) {
			if ((opt_tokens[i].token & required_opts) &&
			    !(opt_tokens[i].token & opts->mask)) {
				pr_warn("missing parameter '%s'\n",
					opt_tokens[i].pattern);
			}
		}

		return -EINVAL;
	}

	return 0;
}

static int nvmf_check_allowed_opts(struct nvmf_ctrl_options *opts,
		unsigned int allowed_opts)
{
	if (opts->mask & ~allowed_opts) {
		int i;

		for (i = 0; i < ARRAY_SIZE(opt_tokens); i++) {
			if (opt_tokens[i].token & ~allowed_opts) {
				pr_warn("invalid parameter '%s'\n",
					opt_tokens[i].pattern);
			}
		}

		return -EINVAL;
	}

	return 0;
}

#define NVMF_REQUIRED_OPTS	(NVMF_OPT_TRANSPORT | NVMF_OPT_NQN)

static ssize_t
nvmf_create_ctrl(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct nvmf_ctrl_options *opts;
	struct nvmf_transport_ops *ops;
	int ret;

	opts = kzalloc(sizeof(*opts), GFP_KERNEL);
	if (!opts)
		return -ENOMEM;

	ret = nvmf_parse_options(opts, buf);
	if (ret)
		goto out_free_opts;

	/*
	 * Check the generic options first as we need a valid transport for
	 * the lookup below.  Then clear the generic flags so that transport
	 * drivers don't have to care about them.
	 */
	ret = nvmf_check_required_opts(opts, NVMF_REQUIRED_OPTS);
	if (ret)
		goto out_free_opts;
	opts->mask &= ~NVMF_REQUIRED_OPTS;

	mutex_lock(&nvmf_transports_mutex);
	ops = nvmf_lookup_transport(opts);
	if (!ops) {
		pr_info("no handler found for transport %s.\n",
			opts->transport);
		ret = -EINVAL;
		goto out_unlock;
	}

	ret = nvmf_check_required_opts(opts, ops->required_opts);
	if (ret)
		goto out_unlock;
	ret = nvmf_check_allowed_opts(opts, ops->allowed_opts);
	if (ret)
		goto out_unlock;

	ret = ops->create_ctrl(dev, opts);
	if (!ret)
		ret = count;

out_unlock:
	mutex_unlock(&nvmf_transports_mutex);
out_free_opts:
	kfree(opts);
	return ret;
}

static DEVICE_ATTR(add_ctrl, S_IWUSR, NULL, nvmf_create_ctrl);

static struct class *nvmf_class;
static struct device *nvmf_device;

static int __init nvmf_init(void)
{
	uuid_le uuid;
	int ret;

	uuid_le_gen(&uuid);
	nvmf_host_nqn = kasprintf(GFP_KERNEL,
		"nqn.2014-08.org.nvmexpress:NVMf:uuid:%pUl",
		&uuid);
	if (!nvmf_host_nqn)
		return -ENOMEM;

	WARN_ON_ONCE(strlen(nvmf_host_nqn) > NVMF_NQN_SIZE - 1);

	nvmf_class = class_create(THIS_MODULE, "nvme-fabrics");
	if (IS_ERR(nvmf_class)) {
		pr_err("couldn't register class nvme-fabrics\n");
		ret = PTR_ERR(nvmf_class);
		goto out_free_nqn;
	}

	nvmf_device =
		device_create(nvmf_class, NULL, MKDEV(0, 0), NULL, "ctl");
	if (IS_ERR(nvmf_device)) {
		pr_err("couldn't create nvme-fabris device!\n");
		ret = PTR_ERR(nvmf_device);
		goto out_destroy_class;
	}

	ret = device_create_file(nvmf_device, &dev_attr_add_ctrl);
	if (ret) {
		pr_err("couldn't add device attr.\n");
		goto out_destroy_device;
	}

	return 0;

out_destroy_device:
	device_destroy(nvmf_class, MKDEV(0, 0));
out_destroy_class:
	class_destroy(nvmf_class);
out_free_nqn:
	kfree(nvmf_host_nqn);
	return ret;
}

static void __exit nvmf_exit(void)
{
	device_destroy(nvmf_class, MKDEV(0, 0));
	class_destroy(nvmf_class);
	kfree(nvmf_host_nqn);
}

MODULE_LICENSE("GPL v2");

module_init(nvmf_init);
module_exit(nvmf_exit);
