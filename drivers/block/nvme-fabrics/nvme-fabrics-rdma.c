/*
 * nvme-fabrics-rdma.h - NVM protocol paradigm independent of transport
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
#define NO_TARGET

#include <linux/init.h>
#include <linux/module.h>
#include <linux/nvme-fabrics/nvme-common.h>
#include <linux/nvme-fabrics/nvme-fabrics.h>
#include <linux/nvme-fabrics/nvme-fabrics-rdma.h>
#include <linux/errno.h>
#include <linux/in.h>
#include <linux/inet.h>

char fabric_used[NVME_FABRIC_IQN_MAXLEN] = "rdma";
module_param_string(fabric_used, "rdma", FABRIC_STRING_MAX, 0444);
MODULE_PARM_DESC(fabric_used, "Read-only description of fabric being used");

unsigned char fabric_timeout = FABRIC_TIMEOUT;
module_param(fabric_timeout, byte, 0644);
MODULE_PARM_DESC(fabric_timeout, "Timeout for fabric-specific communication");

unsigned char discover_retry_count = DISCOVER_RETRY;
module_param(discover_retry_count, byte, 0644);
MODULE_PARM_DESC(discover_retry_count,
		 "Number of times sender will retry for discover connection");

unsigned char admin_retry_count = AQ_RETRY;
module_param(admin_retry_count, byte, 0644);
MODULE_PARM_DESC(admin_retry_count,
		 "Number of times sender will retry for AQ connection");

unsigned char io_retry_count = IOQ_RETRY;
module_param(io_retry_count, byte, 0644);
MODULE_PARM_DESC(io_retry_count,
		 "Number of times sender will retry for IOQ connection");

unsigned char discover_pool_depth = DISCOVER_POOL_DEPTH;
module_param(discover_pool_depth, byte, 0644);
MODULE_PARM_DESC(discover_pool_depth,
		 "Number of pre allocated RX descriptors for the disc conn");

unsigned char aq_pool_depth = AQ_POOL_DEPTH;
module_param(aq_pool_depth, byte, 0644);
MODULE_PARM_DESC(aq_pool_depth,
		 "Number of pre allocated RX descriptors for the aq conn");

unsigned char ioq_pool_depth = IOQ_POOL_DEPTH;
module_param(ioq_pool_depth, byte, 0644);
MODULE_PARM_DESC(ioq_pool_depth,
		 "Number of pre allocated RX descriptors for the ioq conn");

static DEFINE_SPINLOCK(nvme_ctrl_list_lock);
static DEFINE_SPINLOCK(nvme_fabric_list_lock);

static struct list_head ctrl_list;

static struct {
	enum ib_wc_status status;
	const char *str;
} wc_status_array[] = {
	{ IB_WC_SUCCESS,		"IB_WC_SUCCESS" },
	{ IB_WC_LOC_LEN_ERR,		"IB_WC_LOC_LEN_ERR" },
	{ IB_WC_LOC_QP_OP_ERR,		"IB_WC_LOC_QP_OP_ERR" },
	{ IB_WC_LOC_EEC_OP_ERR,		"IB_WC_LOC_EEC_OP_ERR" },
	{ IB_WC_LOC_PROT_ERR,		"IB_WC_LOC_PROT_ERR" },
	{ IB_WC_WR_FLUSH_ERR,		"IB_WC_WR_FLUSH_ERR" },
	{ IB_WC_MW_BIND_ERR,		"IB_WC_MW_BIND_ERR" },
	{ IB_WC_BAD_RESP_ERR,		"IB_WC_BAD_RESP_ERR" },
	{ IB_WC_LOC_ACCESS_ERR,		"IB_WC_LOC_ACCESS_ERR" },
	{ IB_WC_REM_INV_REQ_ERR,	"IB_WC_REM_INV_REQ_ERR" },
	{ IB_WC_REM_ACCESS_ERR,		"IB_WC_REM_ACCESS_ERR" },
	{ IB_WC_REM_OP_ERR,		"IB_WC_REM_OP_ERR" },
	{ IB_WC_RETRY_EXC_ERR,		"IB_WC_RETRY_EXC_ERR" },
	{ IB_WC_RNR_RETRY_EXC_ERR,	"IB_WC_RNR_RETRY_EXC_ERR" },
	{ IB_WC_LOC_RDD_VIOL_ERR,	"IB_WC_LOC_RDD_VIOL_ERR" },
	{ IB_WC_REM_INV_RD_REQ_ERR,	"IB_WC_REM_INV_RD_REQ_ERR" },
	{ IB_WC_REM_ABORT_ERR,		"IB_WC_REM_ABORT_ERR" },
	{ IB_WC_INV_EECN_ERR,		"IB_WC_INV_EECN_ERR" },
	{ IB_WC_INV_EEC_STATE_ERR,	"IB_WC_INV_EEC_STATE_ERR" },
	{ IB_WC_FATAL_ERR,		"IB_WC_FATAL_ERR" },
	{ IB_WC_RESP_TIMEOUT_ERR,	"IB_WC_RESP_TIMEOUT_ERR" },
	{ IB_WC_GENERAL_ERR,		"IB_WC_GENERAL_ERR" },
	{ -1, NULL }
};

static int rdma_parse_addr(struct nvme_fabric_addr *address,
			   struct sockaddr_in *dstaddr_in)
{
	int ret = 0;
	int address_type = address->what_addr_type;

	if (address_type == NVME_FABRIC_IP4) {
		dstaddr_in->sin_family = AF_INET;
		dstaddr_in->sin_addr.s_addr =
			in_aton(address->addr.ipv4_addr.octet);
		dstaddr_in->sin_port =
			address->addr.ipv4_addr.tcp_udp_port;
	} else if (address_type == NVME_FABRIC_IP6) {
		dstaddr_in->sin_family = AF_INET6;
		dstaddr_in->sin_addr.s_addr =
			in_aton(address->addr.ipv6_addr.octet);
		dstaddr_in->sin_port =
			address->addr.ipv6_addr.tcp_udp_port;
	} else {
		pr_err("Address type %d not supported in RDMA transport\n",
		       address_type);
		ret = -EPROTONOSUPPORT;
	}

	return ret;
}

static struct rdma_ctrl *find_ctrl(char *subsys_name, __u16 cntlid)
{
	struct list_head	*i;
	struct list_head	*q;
	struct rdma_ctrl	*ret = NULL;

	list_for_each_safe(i, q, &ctrl_list) {
		ret = list_entry(i, struct rdma_ctrl, node);
		if ((!strcmp(subsys_name, ret->subsys_name)) &&
				(cntlid == ret->cntlid))
			return ret;
	}
	return NULL;
}

static void reconstruct_nvme_fabric_addr(struct sockaddr_in *dstaddr_in,
		struct nvme_fabric_addr *fabric_addr)
{
	if (dstaddr_in->sin_family == AF_INET) {
		fabric_addr->what_addr_type = NVME_FABRIC_IP4;
		snprintf(fabric_addr->addr.ipv4_addr.octet, IPV4_ADDR_SIZE,
			 "%pI4", &dstaddr_in->sin_addr.s_addr);
		fabric_addr->addr.ipv4_addr.tcp_udp_port =
			dstaddr_in->sin_port;
	} else if (dstaddr_in->sin_family == AF_INET6) {
		fabric_addr->what_addr_type = NVME_FABRIC_IP6;
		snprintf(fabric_addr->addr.ipv6_addr.octet, IPV6_ADDR_SIZE,
			 "%pI6", &dstaddr_in->sin_addr.s_addr);
		fabric_addr->addr.ipv6_addr.tcp_udp_port =
			dstaddr_in->sin_port;
	} else {
		pr_err("unsupported sin_family type\n");
		fabric_addr = NULL;
	}
}

static const char *wc_status_str(enum ib_wc_status status)
{
	int			i;

	for (i = 0; wc_status_array[i].str; i++)
		if (wc_status_array[i].status == status)
			return wc_status_array[i].str;
	return "UNKNOWN IB_WC_STATUS?!?";
}

static void discover_comp_handler(struct ib_cq *cq, void *context)
{
	struct nvme_rdma_conn *fabric_conn = context;

	complete(&fabric_conn->comp);
}

static void aq_comp_handler(struct ib_cq *cq, void *context)
{
	struct nvme_rdma_conn *fabric_conn = context;

	complete(&fabric_conn->comp);
}

inline int post_send(struct nvme_rdma_conn *fabric_conn,
		     struct xport_desc *tx_desc)
{
	struct ib_send_wr	 snd;
	struct ib_send_wr	*bad;

	memset(&snd, 0, sizeof(snd));

	snd.wr_id	=	(uintptr_t)tx_desc;
	snd.opcode	=	IB_WR_SEND;
	snd.sg_list	=	&tx_desc->sgl[0];
	snd.num_sge	=	tx_desc->num_sge;
	snd.send_flags	=	IB_SEND_SIGNALED;

	return ib_post_send(fabric_conn->xport_conn.cm_id->qp, &snd, &bad);
}

static int post_recv(struct nvme_rdma_conn *fabric_conn,
		     struct xport_desc *rx_desc)
{
	struct ib_recv_wr	 rcv;
	struct ib_recv_wr	*bad;

	pr_info("%s: %s()\n", __FILE__, __func__);

	memset(&rcv, 0, sizeof(rcv));

	rcv.wr_id	=	(unsigned long)rx_desc;
	rcv.sg_list	=	&rx_desc->sgl[0];
	rcv.num_sge	=	rx_desc->num_sge;

	return ib_post_recv(fabric_conn->xport_conn.cm_id->qp, &rcv, &bad);
}

static void process_ioq_wc(struct nvme_rdma_conn *fabric_conn,
			   int cnt, struct ib_wc *wc)
{
	struct nvme_common_queue	*nvmeq = fabric_conn->nvmeq;

	NVME_UNUSED(nvmeq);

	for (; cnt; cnt--, wc++) {
		if (wc->status) {
			pr_err("status %s (%d)\n",
			       wc_status_str(wc->status), wc->status);
			continue;
		}
		switch (wc->opcode) {
		case IB_WC_SEND:
			pr_info("RDMA_IB_WC_SEND completion\n");
			continue;
		case IB_WC_RECV:
			pr_info("RDMA_IB_WC_RECV completion\n");
			continue;
		default:
			pr_info("Unexpected completion %x\n", wc->opcode);
		}

		/* TODO: Put receive back on proper queue */
	}
}

static void ioq_comp_handler(struct ib_cq *cq, void *context)
{
	struct nvme_rdma_conn	*fabric_conn = context;
	struct ib_wc		 wc[NVME_RDMA_POLLSIZE];
	int			 ret;

	ib_req_notify_cq(cq, IB_CQ_NEXT_COMP);

	do {
		ret = ib_poll_cq(cq, NVME_RDMA_POLLSIZE, wc);
		if (ret > 0) {
			process_ioq_wc(fabric_conn, ret, &wc[0]);
			break;
		}
		if (ret) {
			pr_err("ib_poll_cq returned %d\n", ret);
			break;
		}
	} while (ret);
}

static void event_handler(struct ib_event *evt, void *context)
{
	pr_info("%s: %s()\n", __FILE__, __func__);
	pr_info("event=%d context=%p\n", evt->event, context);
}

static int setup_cq(struct nvme_rdma_conn *fabric_conn,
		    ib_comp_handler comp_handler)
{
	struct ib_device	*ib_dev = fabric_conn->rdma_ctrl->ib_dev;
	int			 cqes;
	struct ib_cq		*cq;

	pr_info("%s: %s()\n", __FILE__, __func__);

	/*Fill in cqes based on the type of connection (IOQ/AQ/Disc)*/
	if (fabric_conn->stage == CONN_DISCOVER)
		cqes = DISCOVER_RQ_SIZE;
	else if (fabric_conn->stage == CONN_AQ)
		cqes = AQ_RQ_SIZE;
	else
		cqes = IOQ_RQ_SIZE;

	cq = ib_create_cq(ib_dev, comp_handler, event_handler,
			  fabric_conn, cqes, 0);
	if (IS_ERR(cq)) {
		pr_err("ib_create_cq failed\n");
		return -EINVAL;
	}

	ib_req_notify_cq(cq, IB_CQ_NEXT_COMP);

	fabric_conn->xport_conn.cq = cq;

	return 0;
}

static int setup_qp(struct nvme_rdma_conn *fabric_conn, int max_send_wr,
		    int max_recv_wr, int max_send_sge, int max_recv_sge)
{
	struct rdma_ctrl	*rdma_ctrl = fabric_conn->rdma_ctrl;
	struct ib_qp_init_attr	 attr;
	int			 ret;

	pr_info("%s: %s()\n", __FILE__, __func__);

	memset(&attr, 0, sizeof(attr));

	attr.event_handler	 = event_handler;
	attr.send_cq		 =
	attr.recv_cq		 = fabric_conn->xport_conn.cq;
	attr.qp_type		 = IB_QPT_RC;
	attr.qp_context		 = fabric_conn;
	attr.sq_sig_type	 = IB_SIGNAL_ALL_WR;
	attr.cap.max_inline_data = MAX_INLINE_DATA;
	attr.cap.max_send_wr	 = max_send_wr;
	attr.cap.max_recv_wr	 = max_recv_wr;
	attr.cap.max_send_sge	 = max_send_sge;
	attr.cap.max_recv_sge	 = max_recv_sge;

	ret = rdma_create_qp(fabric_conn->xport_conn.cm_id,
			     rdma_ctrl->pd, &attr);
	if (ret)
		pr_err("rdma_create_qp returned %d\n", ret);

	return ret;
}

static inline void setup_rdma_ctrl(struct rdma_ctrl *rdma_ctrl,
				   struct ib_device_attr *attr,
				   struct ib_pd *pd)
{
	rdma_ctrl->max_qp_init_rd_atom	= attr->max_qp_init_rd_atom;
	rdma_ctrl->max_qp_rd_atom	= attr->max_qp_rd_atom;
	rdma_ctrl->pd			= pd;
}

static inline void setup_rdma_parms(struct rdma_conn_param *parms,
				    struct ib_device_attr *attr,
				    u8 retry_count)
{
	parms->retry_count	   = retry_count;
	parms->rnr_retry_count	   = retry_count;
	parms->initiator_depth	   = attr->max_qp_init_rd_atom;
	parms->responder_resources = attr->max_qp_rd_atom;
}

/*CAYTONCAYTON - Can we merge this with setup_aq_params?*/
static int setup_discover_params(struct nvme_rdma_conn *fabric_conn)
{
	struct rdma_ctrl	*rdma_ctrl = fabric_conn->rdma_ctrl;
	struct rdma_conn_param	*parms = &fabric_conn->xport_conn.conn_params;
	struct ib_pd		*pd;
	struct ib_device	*ib_dev;
	struct ib_device_attr	 attr;
	ib_comp_handler		 comp_handler;
	int			 ret;

	pr_info("%s: %s()\n", __FILE__, __func__);

	memset(parms, 0, sizeof(*parms));

	ib_dev = rdma_ctrl->ib_dev = fabric_conn->xport_conn.cm_id->device;

	pd = ib_alloc_pd(ib_dev);
	if (IS_ERR(pd)) {
		ret = PTR_ERR(pd);
		pr_err("setup_pd returned %d\n", ret);
		return ret;
	};

	ret = ib_query_device(ib_dev, &attr);
	if (ret) {
		pr_err("ib_query_device failed with %d\n", ret);
		goto err1;
	}

	setup_rdma_ctrl(rdma_ctrl, &attr, pd);

	/*CAYTONCAYTON - this could be the same comp_handler as AQ,
	 *have to come up with a 'generic' name for both... or
	 *comp_handler and parms come in as arguments to enable AQ
	 *and discover to be set up correctly... */
	comp_handler = discover_comp_handler;

	setup_rdma_parms(parms, &attr, discover_retry_count);

	ret = setup_cq(fabric_conn, comp_handler);
	if (ret)
		goto err1;

	/* CAYTONCAYTON - Do we need this? */
	rdma_ctrl->mr = ib_get_dma_mr(rdma_ctrl->pd,
				      IB_ACCESS_LOCAL_WRITE |
				      IB_ACCESS_REMOTE_WRITE|
				      IB_ACCESS_REMOTE_READ);

	if (IS_ERR(rdma_ctrl->mr)) {
		ret = PTR_ERR(rdma_ctrl->mr);
		pr_err("%s %s() ib_get_dma_mr returned %d\n",
		       __FILE__, __func__, ret);
		goto err2;
	}

	ret = setup_qp(fabric_conn, MAX_DISCOVER_SEND_WR, MAX_DISCOVER_RECV_WR,
		       MAX_DISCOVER_SEND_SGE, MAX_DISCOVER_RECV_SGE);
	if (ret)
		goto err3;

	return 0;

err3:
	ib_dereg_mr(rdma_ctrl->mr);
err2:
	ib_destroy_cq(fabric_conn->xport_conn.cq);
err1:
	ib_dealloc_pd(pd);

	return ret;
}

static int setup_aq_params(struct nvme_rdma_conn *fabric_conn)
{
	struct rdma_ctrl	*rdma_ctrl = fabric_conn->rdma_ctrl;
	struct rdma_conn_param	*parms = &fabric_conn->xport_conn.conn_params;
	struct ib_pd		*pd;
	struct ib_device	*ib_dev;
	struct ib_device_attr	 attr;
	ib_comp_handler		 comp_handler;
	int			 ret;

	pr_info("%s: %s()\n", __FILE__, __func__);

	memset(parms, 0, sizeof(*parms));

	ib_dev = rdma_ctrl->ib_dev = fabric_conn->xport_conn.cm_id->device;

	pd = ib_alloc_pd(ib_dev);
	if (IS_ERR(pd)) {
		ret = PTR_ERR(pd);
		pr_err("setup_pd returned %d\n", ret);
		return ret;
	};

	ret = ib_query_device(ib_dev, &attr);
	if (ret) {
		pr_err("ib_query_device failed with %d\n", ret);
		goto err1;
	}

	setup_rdma_ctrl(rdma_ctrl, &attr, pd);

	/*CAYTONCAYTON - this could be the same comp_handler as DISC, have to
	 *come up with a 'generic' name for both... or comp_handler & parms
	 *come as arguments to enable AQ and DISC to be set up correctly... */
	comp_handler = aq_comp_handler;

	/*CAYTONCAYTON - see comment above.  In addition, we could assume
	 *retry_count can be the same for both AQ and DISCOVER....*/
	setup_rdma_parms(parms, &attr, admin_retry_count);

	ret = setup_cq(fabric_conn, comp_handler);
	if (ret)
		goto err1;

	pr_info("%s: %s()Remote connect: call get_dma_mr\n",
		__FILE__, __func__);

	/* CAYTONCAYTON - Do we need this? */
	rdma_ctrl->mr = ib_get_dma_mr(rdma_ctrl->pd, IB_ACCESS_LOCAL_WRITE  |
				      IB_ACCESS_REMOTE_WRITE |
				      IB_ACCESS_REMOTE_READ);
	if (IS_ERR(rdma_ctrl->mr)) {
		ret = PTR_ERR(rdma_ctrl->mr);
		pr_err("%s %s() ib_get_dma_mr returned %d\n",
		       __FILE__, __func__, ret);
		goto err2;
	}

	ret = setup_qp(fabric_conn, MAX_AQ_SEND_WR, MAX_AQ_RECV_WR,
		       MAX_AQ_SEND_SGE, MAX_AQ_RECV_SGE);
	if (ret)
		goto err3;

	return 0;

err3:
	ib_dereg_mr(rdma_ctrl->mr);
err2:
	ib_destroy_cq(fabric_conn->xport_conn.cq);
err1:
	ib_dealloc_pd(pd);

	return ret;
}

static int setup_ioq_params(struct nvme_rdma_conn *fabric_conn)
{
	struct rdma_ctrl	*rdma_ctrl = fabric_conn->rdma_ctrl;
	struct rdma_conn_param	*parms = &fabric_conn->xport_conn.conn_params;
	ib_comp_handler		 comp_handler;
	int			 ret;

	pr_info("%s: %s()\n", __FILE__, __func__);

	memset(parms, 0, sizeof(*parms));

	comp_handler = ioq_comp_handler;

	parms->retry_count	    = io_retry_count;
	parms->rnr_retry_count	    = io_retry_count;
	parms->initiator_depth	    = rdma_ctrl->max_qp_init_rd_atom;
	parms->responder_resources  = rdma_ctrl->max_qp_rd_atom;
	parms->private_data	    = rdma_ctrl->uuid;
	parms->private_data_len	    = rdma_ctrl->uuid_len;

	ret = setup_cq(fabric_conn, comp_handler);
	if (ret)
		goto err1;

	ret = setup_qp(fabric_conn, MAX_IOQ_SEND_WR, MAX_IOQ_RECV_WR,
		       MAX_IOQ_SEND_SGE, MAX_IOQ_RECV_SGE);
	if (ret)
		goto err2;

	return ret;

err2:
	ib_destroy_cq(fabric_conn->xport_conn.cq);
err1:
	return ret;
}

/*CAYTONCAYTON - Maybe not used; this function is here in case we choose to
 *		 allow a connection establishment confirmation to come back
 *		 with private data. */
static void configure_conn(struct nvme_rdma_conn *fabric_conn,
			   const void *pdata)
{

}

/* Wait until desired state is reached */
static int cm_event_wait(struct nvme_rdma_conn *fabric_conn, int desired)
{
	wait_event_interruptible(fabric_conn->sem,
				 ((fabric_conn->state == desired) ||
				  (fabric_conn->state < 0)));
	return fabric_conn->state == desired;
}

/* Handle events, move the connection along */
static int cm_event_handler(struct rdma_cm_id *cm_id,
			    struct rdma_cm_event *evt)
{
	struct nvme_rdma_conn	*fabric_conn = cm_id->context;
	struct rdma_conn_param	*parms = NULL;
	int			  ret;

	pr_info("%s: %s()\n", __FILE__, __func__);

	switch (evt->event) {
	case RDMA_CM_EVENT_ADDR_RESOLVED:
		pr_info("Address resolved\n");

		if (!fabric_conn->rdma_ctrl->ib_dev)
			fabric_conn->rdma_ctrl->ib_dev = cm_id->device;

		ret = rdma_resolve_route(cm_id, fabric_timeout);
		if (ret) {
			if (ret == -ETIMEDOUT) {
				fabric_conn->state = STATE_TIMEDOUT;
				pr_info("Resolve route timed out\n");
			} else {
				fabric_conn->state = STATE_ERROR;
				pr_info("Resolve route returned %d\n", ret);
			}
		}
		break;
	case RDMA_CM_EVENT_ROUTE_RESOLVED:
		pr_info("Route resolved\n");
		if (fabric_conn->stage == CONN_DISCOVER)
			ret = setup_discover_params(fabric_conn);
		else if (fabric_conn->stage == CONN_AQ)
			ret = setup_aq_params(fabric_conn);
		else if (fabric_conn->stage == CONN_IOQ)
			ret = setup_ioq_params(fabric_conn);
		else
			ret = -EINVAL;

		if (ret) {
			fabric_conn->state = STATE_ERROR;
			pr_err("Setup queue parms returned %d\n", ret);
			break;
		}

		ret = rdma_connect(cm_id, parms);
		if (ret) {
			fabric_conn->state = STATE_ERROR;
			pr_info("rdma_connect returned %d\n", ret);
		}
		break;
	case RDMA_CM_EVENT_ESTABLISHED:
		pr_info("Connection Established\n");
		if (evt->param.conn.private_data)
			configure_conn(fabric_conn,
				       evt->param.conn.private_data);
		fabric_conn->state = STATE_CONNECTED;
		break;
	case RDMA_CM_EVENT_CONNECT_RESPONSE:
		pr_info("Connection Response: status %d\n", evt->status);
		fabric_conn->state = STATE_ERROR;
		break;
	case RDMA_CM_EVENT_DISCONNECTED:
		pr_info("Connection Disconnected\n");
		fabric_conn->state = STATE_NOT_CONNECTED;
		break;
	case RDMA_CM_EVENT_REJECTED:
		pr_info("Connection Rejected\n");
		fabric_conn->state = STATE_ERROR;
		break;
	case RDMA_CM_EVENT_ADDR_ERROR:
		pr_info("Address ERROR, status %d\n", evt->status);
		fabric_conn->state = STATE_ERROR;
		break;
	case RDMA_CM_EVENT_ROUTE_ERROR:
		pr_info("Route ERROR, status %d\n", evt->status);
		fabric_conn->state = STATE_ERROR;
		break;
	case RDMA_CM_EVENT_CONNECT_ERROR:
		pr_info("Connect ERROR, status %d\n", evt->status);
		fabric_conn->state = STATE_ERROR;
		break;
	case RDMA_CM_EVENT_UNREACHABLE:
		pr_info("UNREACHABLE, status %d\n", evt->status);
		fabric_conn->state = STATE_ERROR;
		break;
	default:
		pr_info("UNEXPECTED CM Event 0x%X status %d\n",
			evt->event, evt->status);
		fabric_conn->state = STATE_ERROR;
	}

	wake_up_interruptible(&fabric_conn->sem);

	return 0;
}

static void nvme_rdma_shutdown_connection(struct nvme_rdma_conn *fabric_conn)
{
	struct rdma_cm_id	*cm_id     = fabric_conn->xport_conn.cm_id;
	int			 ret;

	pr_info("%s: %s()\n", __FILE__, __func__);

	/* TODO TEMPORARY - REMOVE WHEN WE GET A TARGET */
	#ifdef NO_TARGET
	return;
	/* TEMPORARY - REMOVE WHEN WE GET A TARGET */
	#endif

	if (fabric_conn->state == STATE_CONNECTED) {
		ret = rdma_disconnect(cm_id);
		if (ret == 0) {
			ret = cm_event_wait(fabric_conn, STATE_NOT_CONNECTED);
			if (!ret)
				pr_err("%s: %s() rdma_disconnect failed\n",
				       __FILE__, __func__);
		}
	}

	/* Poll the CQ to ensure all WQE have been flushed */
	ioq_comp_handler(fabric_conn->xport_conn.cq, fabric_conn);

	if (cm_id) {
		if (fabric_conn->xport_conn.cq) {
			rdma_destroy_qp(cm_id);
			ib_destroy_cq(fabric_conn->xport_conn.cq);
		}

		rdma_destroy_id(cm_id);
		cm_id = NULL;
	}
}

/*
 * This is called from the nvme-fabric middle layer sysfs
 * on request to remove an INDIVIDUAL rdma_ctrl from a fabric.
 * Does NOT remove all rdma_ctrl from the fabric
 */
static void nvme_rdma_disconnect(char *subsys_name, __u16 cntlid,
				 struct nvme_fabric_addr *address)
{
	struct sockaddr		 dstaddr;
	struct sockaddr_in	*dstaddr_in;
	struct rdma_ctrl	*ctrl = NULL;
	struct nvme_rdma_conn	*fabric_conn = NULL;
	struct list_head	*i;
	struct list_head	*q;
	unsigned long		 flags;

	pr_info("%s: %s()\n", __FILE__, __func__);

	/* Fill in the remote rdma_ctrl address and port */
	dstaddr_in = (struct sockaddr_in *) &dstaddr;
	memset(dstaddr_in, 0, sizeof(*dstaddr_in));

	if (rdma_parse_addr(address, dstaddr_in))
		return;

	/* TODO - change to find based on addr/port
	 * to remove names from fabric specific stuff
	 */
	ctrl = find_ctrl(subsys_name, cntlid);
	if (!ctrl) {
		pr_err("%s: Could not find subystem/controller %s/%d\n",
		       __func__, subsys_name, cntlid);
		return;
	}

	list_for_each_safe(i, q, &ctrl->connections) {
		pr_info("%s %d shutting down %s/%d\n",
			__func__, __LINE__, subsys_name, ctrl->cntlid);
		fabric_conn = list_entry(i, struct nvme_rdma_conn, node);
		nvme_rdma_shutdown_connection(fabric_conn);
		list_del(i);
		kfree(fabric_conn);
	}

	/* TODO THIS IS TEMPORARY UNTIL WE GET A TARGET */
	#ifndef NO_TARGET
		ib_dereg_mr(ctrl->mr);
		ib_dealloc_pd(ctrl->pd);
	#endif
	/* TODO: THIS IS TEMPORARY UNTIL WE GET A TARGET */

	spin_lock_irqsave(&nvme_ctrl_list_lock, flags);
	list_del(&ctrl->node);
	kfree(ctrl);
	ctrl = NULL;
	spin_unlock_irqrestore(&nvme_ctrl_list_lock, flags);
}

/*
 * Function that establishes a fabric-specific connection with
 * the rdma_ctrl, as well as create the send and recv work queue
 * to establish a queue pair for the host to use to communicate
 * NVMe capsules with the rdma_ctrl.
 *
 * Generic function that can connect for Discovery, Admin, or I/O.
 */
static int connect_to_rdma_ctrl(struct nvme_rdma_conn *fabric_conn)
{
	struct sockaddr_in	 dst_in;
	struct sockaddr		*dst;
	struct rdma_cm_id       *cm_id;
	struct rdma_ctrl	*rdma_ctrl;
	struct nvme_fabric_addr *nvme_fabric_addr = NULL;
	int			 ret;

	pr_info("%s: %s()\n", __FILE__, __func__);

	dst = (struct sockaddr *) &dst_in;

	cm_id = rdma_create_id(cm_event_handler, fabric_conn,
			       RDMA_PS_TCP, IB_QPT_RC);
	if (IS_ERR(cm_id)) {
		ret = PTR_ERR(cm_id);
		pr_err("%s %s() rdma_create_id returned %d\n",
		       __FILE__, __func__, ret);
		goto err1;
	}

	fabric_conn->xport_conn.cm_id = cm_id;

	dst_in = fabric_conn->dst;

	rdma_ctrl = fabric_conn->rdma_ctrl;

	pr_info("%s(): fabric_conn %p -> ctrlr %p Connecting to %s\n",
		__func__, fabric_conn, rdma_ctrl, addr2str(&dst_in));

	/* TODO THIS IS TEMPORARY UNTIL WE GET A TARGET */
	#ifdef NO_TARGET
	pr_err("\n\n%s - HERE BE DRAGONS\n\n", __func__);
	return 0;  /* FIXME */
	#endif
	/* TODO: THIS IS TEMPORARY UNTIL WE GET A TARGET */

	ret = rdma_resolve_addr(cm_id, NULL, dst, fabric_timeout);
	if (ret) {
		if (ret == -ETIMEDOUT) {
			pr_info("%s: %s() rdma_resolve_addr timed out\n",
				__FILE__, __func__);
			fabric_conn->state = STATE_TIMEDOUT;
		} else
			pr_info("%s: %s() rdma_resolve_addr returned %d\n",
				__FILE__, __func__, ret);
		goto err2;
	}

	/* Wait for cm_event_handler to update the state properly */
	ret = cm_event_wait(fabric_conn, STATE_CONNECTED);
	if (!ret) {
		ret = -ENOTCONN;
		goto err3;
	}

	return 0;
err3:
	reconstruct_nvme_fabric_addr(&dst_in, nvme_fabric_addr);
	nvme_rdma_disconnect(rdma_ctrl->subsys_name, rdma_ctrl->cntlid,
			     nvme_fabric_addr);
err2:
	if (cm_id)
		rdma_destroy_id(cm_id);
err1:
	pr_info("Connection Failed\n");
	fabric_conn->state = STATE_NOT_CONNECTED;
	return ret;
}

static struct xport_desc *wait_on_msg(struct nvme_rdma_conn *fabric_conn)
{
	struct xport_desc	*rx_desc = NULL;

	struct ib_cq		*cq = fabric_conn->xport_conn.cq;
	struct ib_wc		 wc;
	int			 ret;

	pr_info("%s: %s()\n", __FILE__, __func__);

	do {
		ret = ib_poll_cq(cq, 1, &wc);
		if (ret < 0) {
			pr_err("ib_poll_cq returned %d\n", ret);
			goto out;
		}
		if (ret == 0) {
			ib_req_notify_cq(cq, IB_CQ_NEXT_COMP);
			wait_for_completion_interruptible(&fabric_conn->comp);
			continue;
		}
		if (wc.status != IB_WC_SUCCESS) {
			pr_err("request status %d - %s\n", wc.status,
			       wc_status_str(wc.status));
			goto out;
		}
		if (wc.opcode == IB_WC_RECV)
			rx_desc = (void *)wc.wr_id;
		else if (wc.opcode == IB_WC_SEND)
			; /*TODO: Free the capsule...somehow*/
	} while (rx_desc == NULL);

out:
	return rx_desc;
}

/* TODO: implement nvme_rdma_build_admin_sglist() */
static	int nvme_rdma_build_admin_sglist(void *prp1, void *prp2,
		int incapsule_len,
		struct nvme_common_sgl_desc *sglist)
{
	int ret = 0;

	NVME_UNUSED(prp1);
	NVME_UNUSED(prp2);
	NVME_UNUSED(incapsule_len);
	NVME_UNUSED(sglist);
	return ret;
#if 0
	/* If prp1 != 0
		rdma_offset is an OUT param
		rkey = gen_rkey(prp1, 4k, &rdma_offset);
		sglist[0].datablk.addr = rdma_offset;
		sglist[0].datablk.len_key.len = 4k;
		sglist[0].datablk.len_key.key = rkey;

	  If prp2 != 0
		rkey2 = gen_rkey(prp2, 4k, &rdma_offset2);
		sglist[1].datablk.addr = rdma_offset2;
		sglist[1].datablk.len_key.len = 4k;
		sglist[1].datablk.len_key.key = rkey2;

	question: How do the rkeys get invalidated? Some data structure
	the RDMA layer maintains?  The rkeys need to be saved in the context
	of the nvme command so that when the nvme completion happens they get
	invalidated.
	*/

	return 0;
}
#endif
}

static int nvme_rdma_finalize_ctrl(char *subsys_name, __u16 cntlid)
{
	struct rdma_ctrl	*ctrl = NULL;
	int			 ret  = 0;

	/* through the whole init/setup/discover, there should only
	 * be one unitialized ctrl in the ctrl list at a time, or
	 * something is really screwed up.
	 */
	ctrl = find_ctrl(subsys_name, NVME_FABRIC_INIT_CNTLID);
	if (ctrl == NULL) {
		pr_err("%s(): Error, could not find ctrl %x in subsys %s\n",
		       __func__, NVME_FABRIC_INIT_CNTLID, subsys_name);
		ret = -ENXIO;
	} else
		ctrl->cntlid = cntlid;
		pr_info("%s(): rdma_ctrl cntlid in subsystem %s set to %d\n",
		__func__, subsys_name, ctrl->cntlid);

	return ret;

}

static void free_xport_desc(struct xport_desc *desc)
{
	int i;

	ib_dereg_mr(desc->mr);

	for (i = 0; i < desc->num_sge; i++)
		ib_dma_unmap_single(desc->ib_dev, desc->sgl[i].addr,
				    desc->sgl[i].length, desc->dir);
	kfree(desc);
}

static struct xport_desc *alloc_xport_desc(struct nvme_rdma_conn *fabric_conn,
					   void *msg, int len, int dir)
{
	struct xport_desc	*desc;
	struct ib_device	*ib_dev;
	struct rdma_ctrl	*rdma_ctrl;
	struct ib_mr		*mr;
	__u64			 dma_addr;
	u64			 iovbase;
	const int		 flags = IB_ACCESS_LOCAL_WRITE |
					 IB_ACCESS_REMOTE_WRITE|
					 IB_ACCESS_REMOTE_READ;
	int			 ret;

	rdma_ctrl	= fabric_conn->rdma_ctrl;
	ib_dev		= rdma_ctrl->ib_dev;

	desc = kzalloc(sizeof(*desc), GFP_KERNEL);
	if (!desc)
		return NULL;

	dma_addr = ib_dma_map_single(ib_dev, msg, len, dir);

	ret = ib_dma_mapping_error(ib_dev, dma_addr);
	if (ret) {
		pr_err("ib_dma_map_single returned %d\n", ret);
		return NULL;
	}

	iovbase = dma_addr;

	/* TODO: clean up later - dereg_phys_mr */
	mr = ib_reg_phys_mr(rdma_ctrl->pd, msg, len, flags, &iovbase);

	if (IS_ERR(mr))
		return NULL;

	desc->ib_dev		= ib_dev;
	desc->mr		= mr;
	desc->num_sge		= 1;
	desc->dir		= dir;
	desc->sgl[0].addr	= dma_addr;
	desc->sgl[0].length	= len;
	desc->sgl[0].lkey	= mr->lkey;

	return desc;
}

/*TODO: Note this support SYNCHRONOUS Admin commands only.  If ASYNC
	Admin commands are supported we will need to instantiate a
	different submit_aq_cmd
*/
static int nvme_rdma_submit_aq_cmd(void *fabric_context,
				   union nvme_capsule_cmd *capsule,
				   union nvme_capsule_rsp *rsp)
{
	int			ret = 0;
	struct nvme_rdma_conn	*fabric_conn;
	struct xport_desc	*rx_desc;
	struct xport_desc	*tx_desc;

	pr_info("%s: %s()\n", __FILE__, __func__);

	fabric_conn	= (struct nvme_rdma_conn *)fabric_context;

	rx_desc = alloc_xport_desc(fabric_conn, rsp, sizeof(*rsp),
				   DMA_FROM_DEVICE);
	if (!rx_desc) {
		ret = -ENOMEM;
		goto out;
	}

	ret = post_recv(fabric_conn, rx_desc);
	if (ret)
		pr_err("post_recv returned %d\n", ret);

	tx_desc = alloc_xport_desc(fabric_conn, capsule, sizeof(*capsule),
				   DMA_TO_DEVICE);
	if (!tx_desc) {
		ret = -ENOMEM;
		goto err1;
	}

	ret = post_send(fabric_conn, tx_desc);
	if (ret) {
		pr_err("error: %d\n", ret);
		ret = -EFAULT;
		goto err2;
	}

	rx_desc = wait_on_msg(fabric_conn);
	if (!rx_desc) {
		pr_err("message times out\n");
		ret = -ETIMEDOUT;
	}

err2:
	free_xport_desc(tx_desc);
err1:
	free_xport_desc(rx_desc);
out:
	return ret;
}

#if 0
static int nvme_rdma_submit_io_cmd(void *fabric_context,
				   union nvme_capsule_cmd *cmd)
{
	struct nvme_rdma_conn	*fabric_conn;
	struct rdma_cm_id	*cm_id;
	struct rdma_ctrl	*rdma_ctrl;
	int			 ret = 0;

	pr_info("%s: %s()\n", __FILE__, __func__);

	fabric_conn	= (struct nvme_rdma_conn *)fabric_context;
	cm_id		= fabric_conn->xport_conn.cm_id;
	rdma_ctrl	= fabric_conn->rdma_ctrl;

	tx_desc = get_free_tx_desc(fabric_conn);
	if (!tx_desc) {
		pr_err("No free TX desc\n");
		return -ENOMEM;
	}

	sgl = &tx_desc->sgl[0];

	ret = send_msg(fabric_conn, tx_desc, sizeof(*msg), num_sge);
	if (ret) {
		pr_err("opcode %x returned %d\n", cmd->rw.opcode, ret);
		free_tx_desc(tx_desc);
		return -EFAULT;
	}

	return ret;
}
#endif

static struct rdma_ctrl *nvme_rdma_create_ctrl(char *subsys_name,
		__u16 cntlid,
		__u8 *uuid, int stage)
{
	struct rdma_ctrl	*rdma_ctrl;
	unsigned long		 flags;

	rdma_ctrl = kzalloc(sizeof(struct rdma_ctrl), GFP_KERNEL);
	if (!rdma_ctrl)
		return NULL;

	INIT_LIST_HEAD(&rdma_ctrl->connections);
	rdma_ctrl->instance = nvme_fabric_set_instance();
	pr_info("%s: %s() rdma_ctrl %p\n", __FILE__, __func__, rdma_ctrl);

	strncpy(rdma_ctrl->subsys_name, subsys_name, NVME_FABRIC_IQN_MAXLEN);
	rdma_ctrl->cntlid = cntlid;
	if (stage == CONN_AQ) {
		strncpy(rdma_ctrl->uuid, uuid, HNSID_LEN);
		rdma_ctrl->uuid_len = HNSID_LEN;
	}

	spin_lock_irqsave(&nvme_ctrl_list_lock, flags);
	list_add_tail(&rdma_ctrl->node, &ctrl_list);
	spin_unlock_irqrestore(&nvme_ctrl_list_lock, flags);

	return rdma_ctrl;
}

/* Note that conn_ptr is an OUT parameter; it's passed in as NULL */
static int nvme_rdma_connect_create_queue(struct nvme_fabric_subsystem *subsys,
		__u16 current_cntlid,
		__u8 *uuid,
		int stage,
		void **conn_ptr)
{
	struct sockaddr		 dstaddr;
	struct sockaddr_in	*dstaddr_in;
	struct rdma_ctrl	*rdma_ctrl;
	struct nvme_rdma_conn	*fabric_conn;
	unsigned long		 flags;
	int			 ret = -EINVAL;

	pr_info("%s: %s()\n", __FILE__, __func__);

	if (subsys->fabric != NVME_FABRIC_RDMA) {
		pr_err("Attempt to connect to incorrect fabric type\n");
		goto out;
	}

	if (subsys->conn_type != RC) {
		pr_err("Connection type unsupported in this version\n");
		goto out;
	}

	/* Fill in the remote rdma_ctrl address and port */
	dstaddr_in = (struct sockaddr_in *) &dstaddr;
	memset(dstaddr_in, 0, sizeof(*dstaddr_in));

	ret = rdma_parse_addr(&subsys->address, dstaddr_in);
	if (ret)
		goto out;

	if (stage == CONN_IOQ) {
		rdma_ctrl = find_ctrl(subsys->subsiqn,
				      current_cntlid);
		if (!rdma_ctrl) {
			pr_err("%s Could not find subsytem/cntlid %s/%d\n",
			       __func__, subsys->subsiqn, current_cntlid);
			ret = -ENODEV;
			goto out;
		}
		if (rdma_ctrl->cntlid == NVME_FABRIC_INIT_CNTLID) {
			pr_err("%s: Error cntlid %x subsys %s CONN_IOQ try\n",
			       __func__, rdma_ctrl->cntlid,
			       rdma_ctrl->subsys_name);
			ret = -EINVAL;
			goto out;
		}
	} else {
		rdma_ctrl = nvme_rdma_create_ctrl(subsys->subsiqn,
						  current_cntlid,
						  uuid, stage);
		if (!rdma_ctrl) {
			ret = -ENOMEM;
			goto out;
		}
	}

	fabric_conn = kzalloc(sizeof(struct nvme_rdma_conn), GFP_KERNEL);
	if (!fabric_conn) {
		ret = -ENXIO;
		goto free;
	}

	fabric_conn->rdma_ctrl	= rdma_ctrl;
	fabric_conn->state	= STATE_NOT_CONNECTED;
	fabric_conn->stage	= stage;

	memcpy(&fabric_conn->dst, dstaddr_in, sizeof(struct sockaddr_in));

	/* Create the Discover/Admin/IO Connection */
	ret = connect_to_rdma_ctrl(fabric_conn);
	if (ret) {
		pr_info("%s: %s() connection failed: %d\n",
			__FILE__, __func__, ret);
		goto fail;
	}

	init_completion(&fabric_conn->comp);
	init_waitqueue_head(&fabric_conn->sem);

	spin_lock_irqsave(&nvme_fabric_list_lock, flags);
	list_add_tail(&fabric_conn->node, &rdma_ctrl->connections);
	spin_unlock_irqrestore(&nvme_fabric_list_lock, flags);

	*conn_ptr = fabric_conn;
	goto out;
fail:
	spin_lock_irqsave(&nvme_ctrl_list_lock, flags);
	list_del(&rdma_ctrl->node);
	spin_unlock_irqrestore(&nvme_ctrl_list_lock, flags);
	kfree(fabric_conn);
free:
	kfree(rdma_ctrl);
out:
	return ret;
}

/*
 * Fabric specific NVMe RDMA capsule function definitions
 */
static struct nvme_fabric_host_operations nvme_rdma_ops = {
	.owner			= THIS_MODULE,
	.disconnect		= nvme_rdma_disconnect,
	.connect_create_queue	= nvme_rdma_connect_create_queue,
	.send_admin_cmd		= nvme_rdma_submit_aq_cmd,
	.build_admin_sglist	= nvme_rdma_build_admin_sglist,
	.finalize_cntlid	= nvme_rdma_finalize_ctrl,
#if 0
	.send_io_cmd		= nvme_rdma_submit_io_cmd,
#endif
};

static void __exit nvme_rdma_exit(void)
{
	int ret = 0;

	pr_info("\n%s: %s()\n", __FILE__, __func__);

	ret = nvme_fabric_unregister(NULL);
	pr_info("%s(): ret is %d\n", __func__, ret);
}

static int __init nvme_rdma_init(void)
{
	int ret;

	pr_info("\n%s: %s() fabric: %s\n",
		__FILE__, __func__, fabric_used);

	INIT_LIST_HEAD(&ctrl_list);

	ret = nvme_fabric_register(NVMF_CLASS, &nvme_rdma_ops);

	pr_info("%s(): ret is %d\n", __func__, ret);
	return ret;
}

module_init(nvme_rdma_init);
module_exit(nvme_rdma_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Phil Cayton, James Freyensee, Jay Sternberg ");
MODULE_DESCRIPTION("NVMe host driver implementation over RDMA fabric");
MODULE_VERSION("0.000001");
