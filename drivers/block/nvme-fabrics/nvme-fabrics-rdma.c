/*
 * nvme-fabrics-rdma.h - NVM protocol paradigm independent of transport
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
 * This is the device driver implementation of NVMe over RDMA fabric.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/nvme-fabrics/nvme-common.h>
#include <linux/nvme-fabrics/nvme-fabrics.h>
#include <linux/nvme-fabrics/nvme-fabrics-rdma.h>
#include <linux/errno.h>
#include <linux/in.h>
#include <linux/inet.h>

/* TODO: Ideally, what I want, is for these parameter modules to be defined in
 * nvme-fabrics.c or .h file and to force a programmer
 * to define them to default values specific
 * to the xport driver in the .c file e.g., nvme-fabrics-rdma.c.
 * This way we minimize breaking envisioned tools like 'lsnvme', which will
 * collect 'stuff' based on default parameter/file names, and print it out on
 * the command-line, similar to a iscsi command-line tool. Not sure how to do
 * this yet, as simply re-assigning a value to these parameter variables does
 * not necessarily get updated on /sys/module/nvme_rdma/parameters.
 */
static char fabric_used[FABRIC_STRING_MAX];
module_param_string(fabric_used, "rdma", FABRIC_STRING_MAX, 0444);
MODULE_PARM_DESC(fabric_used, "Read-only description of fabric being used");

static unsigned char fabric_timeout = 15;
module_param(fabric_timeout, byte, 0644);
MODULE_PARM_DESC(fabric_timeout, "Timeout for fabric-specific communication");

/*TODO: Should we REALLY make this a parameter?*/
char hostname[FABRIC_STRING_MAX] = "org.nvmeexpress.rdmahost";
/*
module_param_string(hostname, "org.nvmeexpress.rdmahost",
		    FABRIC_STRING_MAX, 0444);
MODULE_PARM_DESC(hostname, "Default name of nvme rdma host.");
*/

static struct nvme_fabric_dev nvme_rdma_dev;

static DEFINE_SPINLOCK(nvme_ep_list_lock);

static int nvme_rdma_submit_aq_cmd(struct nvme_fabric_dev *dev,
				   struct nvme_common_command *cmd,
				   __u32 *result) {
	NVME_UNUSED(dev);
	NVME_UNUSED(cmd);
	NVME_UNUSED(result);

	/*
	 * TODO: guts of NVMe RDMA specific function defined here;
	 * see nvmerp_submit_aq_cmd() from xport_rdma.c from
	 * demo for idea how it should work.
	 */

	/*
	    rdma_specific_function();
	    create_cmd_capsule();
	    if (admin_cmd == identify) {
		nvme_common_identify(...);
	    }
	    send_nvme_capsule(nvme_cmd_capsule cpl);
	*/

	return -1;
}

static int setup_discovery_params(struct nvme_fabric_conn *conn)
{
	return 0;
}

static int setup_aq_params(struct nvme_fabric_conn *conn)
{
	return 0;
}

static int setup_ioq_params(struct nvme_fabric_conn *conn)
{
	return 0;
}

/* Wait until desired state is reached */
static int cm_event_wait(struct nvme_fabric_conn *conn, int desired)
{
	wait_event_interruptible(conn->sem,
				 ((conn->state == desired) ||
				  (conn->state < 0)));
	return conn->state == desired;
}

/* Remote side rejects our connection request*/
static void handle_est_rej(struct nvme_fabric_conn *conn, const void *pdata)
{
	NVME_UNUSED(conn);
	NVME_UNUSED(pdata);
}

/* Remote side accepts our connection request*/
static void handle_est_resp(struct nvme_fabric_conn *conn, const void *pdata)
{
	NVME_UNUSED(conn);
	NVME_UNUSED(pdata);
}

/* Handle events, move the connection along */
static int cm_event_handler(
	struct rdma_cm_id *cm_id,
	struct rdma_cm_event *evt)
{
	struct nvme_fabric_conn	*conn = cm_id->context;
	struct rdma_conn_param	*parms = NULL;
	int			  ret;

	switch (evt->event) {
	case RDMA_CM_EVENT_ADDR_RESOLVED:
		pr_info("Address resolved\n");

		if (!conn->ep->ib_dev)
			conn->ep->ib_dev = cm_id->device;

		ret = rdma_resolve_route(cm_id, fabric_timeout);
		if (ret) {
			if (ret == -ETIMEDOUT) {
				conn->state = STATE_TIMEDOUT;
				pr_info("Resolve route timed out\n");
			} else {
				conn->state = STATE_ERROR;
				pr_info("rdma_resolve_route returned %d\n",
					ret);
			}
		}
		break;
	case RDMA_CM_EVENT_ROUTE_RESOLVED:
		pr_info("Route resolved\n");

		/*if discovery connection...*/
		if (conn->stage == CONN_DISCOVER)
			setup_discovery_params(conn);
		/*ELSE if admin queue connection */
		if (conn->stage == CONN_AQ)
			setup_aq_params(conn);
		/*ELSE if I/O queue connection */
		if (conn->stage == CONN_IOQ)
			setup_ioq_params(conn);
		else {
			pr_info("Trying to connect from invalid state\n");
			ret = -EINVAL;
			break;
		}
		/*parms and private data set up for apppropriate conn type*/
		ret = rdma_connect(cm_id, parms);
		if (ret) {
			conn->state = STATE_ERROR;
			pr_info("rdma_connect returned %d\n", ret);
		}
		break;
	case RDMA_CM_EVENT_ESTABLISHED:
		pr_info("Connection Established\n");

		/*
		if( conn established ) {
		*/
		handle_est_resp(conn, evt->param.conn.private_data);
		conn->state = STATE_CONNECTED;
		/*
		}
		else {
		*/
		handle_est_rej(conn, evt->param.conn.private_data);
		conn->state = STATE_ERROR;
		/*
		}
		*/
		break;
	case RDMA_CM_EVENT_CONNECT_RESPONSE:
		pr_info("Connection Response: status %d\n", evt->status);
		conn->state = STATE_ERROR;
		break;
	case RDMA_CM_EVENT_DISCONNECTED:
		pr_info("Connection Disconnected\n");
		conn->state = STATE_NOT_CONNECTED;
		break;
	case RDMA_CM_EVENT_REJECTED:
		pr_info("Connection Rejected\n");
		conn->state = STATE_ERROR;
		break;
	case RDMA_CM_EVENT_ADDR_ERROR:
		pr_info("Address ERROR, status %d\n", evt->status);
		conn->state = STATE_ERROR;
		break;
	case RDMA_CM_EVENT_ROUTE_ERROR:
		pr_info("Route ERROR, status %d\n", evt->status);
		conn->state = STATE_ERROR;
		break;
	case RDMA_CM_EVENT_CONNECT_ERROR:
		pr_info("Connect ERROR, status %d\n", evt->status);
		conn->state = STATE_ERROR;
		break;
	case RDMA_CM_EVENT_UNREACHABLE:
		pr_info("UNREACHABLE, status %d\n", evt->status);
		conn->state = STATE_ERROR;
		break;
	default:
		pr_info("UNEXPECTED CM Event 0x%X status %d\n",
			evt->event, evt->status);
		conn->state = STATE_ERROR;
	}

	wake_up_interruptible(&conn->sem);

	return 0;
}

static int cm_connect(struct nvme_fabric_conn *fabric_conn)
{
	int                     ret;
	struct sockaddr_in      dst_in;
	struct sockaddr         *dst = (struct sockaddr *) &dst_in;
	struct rdma_cm_id       *cm_id;

	pr_info("%s: %s()\n", __FILE__, __func__);

	cm_id = rdma_create_id(cm_event_handler, fabric_conn,
			       RDMA_PS_TCP, IB_QPT_RC);
	if (IS_ERR(cm_id)) {
		ret = PTR_ERR(cm_id);
		pr_err("%s %s() rdma_create_id returned %d\n",
		       __FILE__, __func__, ret);
		goto err;
	}

	fabric_conn->xport_conn.cm_id = cm_id;

	dst_in = fabric_conn->ep->dst;
	pr_info("%s: %s() EP %p Connecting to %s\n", __FILE__, __func__,
		fabric_conn->ep, addr2str(&dst_in));

	ret = rdma_resolve_addr(cm_id, NULL, dst, fabric_timeout);
	if (ret) {
		if (ret == -ETIMEDOUT) {
			pr_info("%s: %s() rdma_resolve_addr timed out\n",
				__FILE__, __func__);
			fabric_conn->state = STATE_TIMEDOUT;
		} else
			pr_info("%s: %s() rdma_resolve_addr returned %d\n",
				__FILE__, __func__, ret);

		goto err_destroy_cmid;
	}

	/* Wait for cm_event_handler to update the state properly */
	ret = cm_event_wait(fabric_conn, STATE_CONNECTED);
	if (!ret) {
		ret = -ENOTCONN;
		goto err_destroy_cmid;
	}

	return 0;

err_destroy_cmid:
	rdma_destroy_id(cm_id);
err:
	return ret;

}

static void cm_disconnect(struct nvme_fabric_conn *fabric_conn)
{
	int ret;
	struct rdma_cm_id	*cm_id = fabric_conn->xport_conn.cm_id;

	pr_info("%s: %s()\n", __FILE__, __func__);

	if (fabric_conn->state == STATE_CONNECTED) {
		ret = rdma_disconnect(cm_id);
		if (ret == 0) {
			ret = cm_event_wait(fabric_conn, STATE_NOT_CONNECTED);
			if (ret)
				pr_info("rdma_disconnect failed\n");
		}
	}
	if (cm_id) {
		pr_info("rdma_destroy_id %p\n", cm_id);
		rdma_destroy_id(cm_id);
	}
}
static void destroy_tx_buffs(struct nvme_fabric_conn *fabric_conn)
{
	pr_info("%s: %s()\n", __FILE__, __func__);

	kfree(fabric_conn->tx_desc_table);
	fabric_conn->tx_desc_table = NULL;
}

static int create_tx_buffs(struct nvme_fabric_conn *fabric_conn)
{
	int ret = 0;

	pr_info("%s: %s()\n", __FILE__, __func__);

	destroy_tx_buffs(fabric_conn);
	return ret;
}

static void destroy_rx_buffs(struct nvme_fabric_conn *fabric_conn)
{
	pr_info("%s: %s()\n", __FILE__, __func__);

	kfree(fabric_conn->rx_desc_table);
	fabric_conn->rx_desc_table = NULL;
}

static int create_rx_buffs(struct nvme_fabric_conn *fabric_conn)
{
	/*
		struct rx_desc		*rx_desc;
		struct ib_device	*ib_dev;
		int			 len;
	*/
	int			 ret = 0;

	pr_info("%s: %s()\n", __FILE__, __func__);

	/*
		rx_desc =
			kzalloc(sizeof(struct rx_desc) *fabric_conn->rx_depth,
				GFP_KERNEL);
		if(!rx_desc) {
			pr_info("Unable to allocate rx descriptors\n");
			return -ENOMEM;
		}

		ib_dev = fabric_conn->ep->ib_dev;

		//TODO: Set len properly

		return ret;

	err:
	*/
	destroy_rx_buffs(fabric_conn);
	return ret;
}

/*
 * Function that establishes a fabric-specific connection with
 * the ep, as well as create the send work queue and the receive
 * work queue to establish a queue pair for the host to use
 * to communicate NVMe capsules with the ep.
 *
 * Generic function that can connect for Discovery, Admin, or I/O.
 */
static int connect_to_endpoint(struct nvme_fabric_conn *fabric_conn)
{
	struct ep	*ep = fabric_conn->ep;
	int		 ret;

	pr_info("%s: %s()\n", __FILE__, __func__);

	ret = cm_connect(fabric_conn);
	if (ret) {
		if (fabric_conn->state == STATE_TIMEDOUT) {
			/* TODO: Finish the retry code */
			pr_info("Should have retried\n");
			fabric_conn->state = STATE_NOT_CONNECTED;
			goto conn_err;
		} else {
			pr_info("Connection Failed\n");
			goto conn_err;
		}
	}

	if (!ep->mr) {
		struct ib_mr *mr;

		pr_info("%s: %s()Remote connect: call get_dma_mr\n",
			__FILE__, __func__);

		mr = ib_get_dma_mr(ep->pd,
				   IB_ACCESS_LOCAL_WRITE |
				   IB_ACCESS_REMOTE_WRITE|
				   IB_ACCESS_REMOTE_READ);

		if (IS_ERR(mr)) {
			ret = PTR_ERR(mr);
			pr_err("%s %s() ib_get_dma_mr returned %d\n",
			       __FILE__, __func__, ret);
			goto dma_mr_err;
		}

		ep->mr = mr;
	}

	ret = create_rx_buffs(fabric_conn);
	if (ret)
		goto rx_buff_err;

	ret = create_tx_buffs(fabric_conn);
	if (ret)
		goto tx_buff_err;

	return ret;

rx_buff_err:
	/*TODO: Clean up mr?*/
tx_buff_err:
	destroy_rx_buffs(fabric_conn);
dma_mr_err:
	cm_disconnect(fabric_conn);
	/*TODO: Finish me*/
conn_err:
	return ret;
}

static int nvme_rdma_connect_create_queue(
	char *addr, int port, int fabric,
	struct nvme_conn *nvme_conn, int stage)
{
	struct sockaddr		 dstaddr;
	struct sockaddr_in	*dstaddr_in;
	struct ep		*ep;
	struct nvme_fabric_conn *fabric_conn;
	unsigned long		 flags;
	int			 ret = 0;
	LIST_HEAD(nvme_ep_list);
	LIST_HEAD(nvme_ep_vacant_list);

	pr_info("%s: %s()\n", __FILE__, __func__);

	/* Fill in the remote ep address and port */
	dstaddr_in = (struct sockaddr_in *) &dstaddr;
	memset(dstaddr_in, 0, sizeof(*dstaddr_in));
	dstaddr_in->sin_family = AF_INET;

	dstaddr_in->sin_addr.s_addr = in_aton(addr);

	spin_lock_irqsave(&nvme_ep_list_lock, flags);

	if (list_empty(&nvme_ep_vacant_list)) {
		ep = kzalloc(sizeof(*ep), GFP_KERNEL);
		if (ep) {
			pr_info("%s: %s() ep, %p instance %d\n",
				__FILE__, __func__, ep, instance);
			ep->instance = instance++;
			list_add(&ep->node, &nvme_ep_list);
		}
	} else {
		ep = list_first_entry(&nvme_ep_vacant_list,
				      struct ep, node);
		pr_info("%s: %s() ep, %p instance %d\n",
			__FILE__, __func__, ep, instance);
		list_move(&ep->node, &nvme_ep_list);
	}

	spin_unlock_irqrestore(&nvme_ep_list_lock, flags);

	if (!ep)
		return -ENOMEM;

	fabric_conn = kzalloc(sizeof(*fabric_conn), GFP_KERNEL);
	if (!fabric_conn) {
		ret = -ENOMEM;
		goto free;
	}

	ep->dst		= *dstaddr_in;

	fabric_conn->ep		=	ep;
	fabric_conn->port	=	port;
	fabric_conn->state	=	STATE_NOT_CONNECTED;
	fabric_conn->stage	=	stage;

	init_completion(&fabric_conn->comp);
	init_waitqueue_head(&fabric_conn->sem);

	spin_lock_irqsave(&nvme_ep_list_lock, flags);
	list_add(&fabric_conn->node, &ep->connections);
	spin_unlock_irqrestore(&nvme_ep_list_lock, flags);

	/* Create the Admin/IO Connection */
	ret = connect_to_endpoint(fabric_conn);
	if (ret) {
		pr_info("%s: %s() connection failed: %d\n",
			__FILE__, __func__, ret);
		goto fail;
	}
	return ret;

fail:
	spin_lock_irqsave(&nvme_ep_list_lock, flags);
	list_del(&fabric_conn->node);
	spin_unlock_irqrestore(&nvme_ep_list_lock, flags);

free:
	kfree(ep);
	spin_lock_irqsave(&nvme_ep_list_lock, flags);
	list_move(&ep->node, &nvme_ep_vacant_list);
	spin_unlock_irqrestore(&nvme_ep_list_lock, flags);

	return ret;
}

/*
 * This is the specific discovery/probe sequence of rdma. The nvme-fabric
 * middle layer will call the generic probe() function to call this.
 */
static int nvme_rdma_probe(char *addr, int port, int fabric,
			   struct nvme_conn *nvme_conn)
{
	int			 ret = 0;

	pr_info("%s: %s()\n", __FILE__, __func__);

	return ret;
}

/*
 * This is the specific shutdown and cleanup for the NVMe RDMA transport.
 *
 * This should close down all queues
 *
 * TODO, FIXME: The parameter ins stop_destroy_queues should be a "ep"
 */
static void nvme_rdma_cleanup(int FIXME)
{
#if 0
	struct rdma_cm_id       *cm_id = conn->xport.cm_id;

	if (conn->state == STATE_CONNECTED) {
		ret = rdma_disconnect(cm_id);
		if (ret == 0) {
			ret = cm_event_wait(conn, STATE_NOT_CONNECTED);
			if (!ret)
				pr_err("%s: %s() rdma_disconnect failed\n",
				       __FILE__, __func__);
		}
	}
	if (cm_id) {
		pr_err("%s: %s() rdma_destroy_id %p\n",
		       __FILE__, __func__, cm_id);
		if (conn->xport.cq) {
			ib_destroy_cq(conn->xport.cq);
			rdma_destroy_qp(cm_id);
		}
		rdma_destroy_id(cm_id);
	}
#endif
}

/*
 * This is called from the nvme-fabric middle layer sysfs
 * on request to remove an individual ep from a fabric.
 * Does NOT remove all eps from the fabric
 */
static void nvme_rdma_disconnect(char *address, int port, int fabric)
{
	struct sockaddr		 dstaddr;
	struct sockaddr_in	*dstaddr_in;

	pr_info("%s: %s()\n", __FILE__, __func__);

	dstaddr_in = (struct sockaddr_in *) &dstaddr;
	memset(dstaddr_in, 0, sizeof(*dstaddr_in));
	dstaddr_in->sin_family = AF_INET;

	dstaddr_in->sin_addr.s_addr = in_aton(address);

	dstaddr_in->sin_port = cpu_to_be16(port);

	/*TODO:
	 *Find the ep based on the fabric/port and call
	 *the appropriate cleanup and disconnect function
	 */

	/*
	 * FIXME: The parameter in stop_destroy_queues should be a "ep"
	 */
	nvme_rdma_cleanup(0);
}

/*
 * Define the specific NVMe RDMA capsule function definitions
 */
static struct nvme_fabric_host_operations nvme_rdma_ops = {
	.owner			= THIS_MODULE,
	.prepsend_admin_cmd	= nvme_rdma_submit_aq_cmd,
	.prepsend_io_cmd	= TODO,
	.probe			= nvme_rdma_probe,
	.disconnect		= nvme_rdma_disconnect,
	.connect_create_queue  = nvme_rdma_connect_create_queue,
	.stop_destroy_queues    = nvme_rdma_cleanup,
};

static void __exit nvme_rdma_exit(void)
{
	int retval;

	pr_info("\n%s: %s()\n", __FILE__, __func__);
	retval = nvme_fabric_unregister(-666);
	pr_info("%s(): retval is %d\n", __func__, retval);
}

static int __init nvme_rdma_init(void)
{
	int retval;

	pr_info("\n%s: %s() hostname: %s fabric: %s\n",
		__FILE__, __func__, hostname, fabric_used);
	retval = nvme_fabric_register(NVMF_CLASS, &nvme_rdma_ops);
	nvme_rdma_dev.fabric_address = 666;
	pr_info("%s(): retval is %d\n", __func__, retval);
	return retval;
}

module_init(nvme_rdma_init);
module_exit(nvme_rdma_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Phil Cayton, Jay Freyensee");
MODULE_DESCRIPTION("NVMe host driver implementation over RDMA fabric");
MODULE_VERSION("0.000000001");
