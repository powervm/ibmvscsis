diff --git a/drivers/net/ethernet/ibm/ibmveth.c b/drivers/net/ethernet/ibm/ibmveth.c
index f210398200ece..cd954e27bde9c 100644
--- a/drivers/net/ethernet/ibm/ibmveth.c
+++ b/drivers/net/ethernet/ibm/ibmveth.c
@@ -507,6 +507,199 @@ static u64 ibmveth_encode_mac_addr(u8 *mac)
 	return encoded;
 }
 
+static void ibmveth_reset_task(struct work_struct *work)
+{
+	struct ibmveth_adapter *adapter = container_of(work,
+						       struct ibmveth_adapter,
+						       reset_task);
+	struct net_device *netdev = adapter->netdev;
+	long lpar_rc;
+	int rxq_entries = 1;
+	u64 mac_address;
+	union ibmveth_buf_desc rxq_desc;
+	struct device *dev;
+	int i;
+
+	netdev_dbg(netdev, "Resetting the NIC \n");
+
+	/* Lock the network stack */
+	rtnl_lock();
+
+	/* Skip reset task if suspended or closed */
+	if (!netif_device_present(netdev) ||
+	    !netif_running(netdev)) {
+		rtnl_unlock();
+		return;
+	}
+
+	netif_trans_update(netdev);
+	napi_disable(&adapter->napi);
+	netif_tx_disable(netdev);
+
+	if (!adapter->pool_config)
+		netif_stop_queue(netdev);
+
+	h_vio_signal(adapter->vdev->unit_address, VIO_IRQ_DISABLE);
+
+	do {
+		lpar_rc = h_free_logical_lan(adapter->vdev->unit_address);
+	} while (H_IS_LONG_BUSY(lpar_rc) || (lpar_rc == H_BUSY));
+
+	if (lpar_rc != H_SUCCESS) {
+		netdev_err(netdev, "h_free_logical_lan failed with %lx, "
+			   "continuing with close\n", lpar_rc);
+	}
+
+	ibmveth_update_rx_no_buffer(adapter);
+
+	dma_unmap_single(dev, adapter->buffer_list_dma, 4096,
+			 DMA_BIDIRECTIONAL);
+	free_page((unsigned long)adapter->buffer_list_addr);
+
+	dma_unmap_single(dev, adapter->filter_list_dma, 4096,
+			 DMA_BIDIRECTIONAL);
+	free_page((unsigned long)adapter->filter_list_addr);
+
+	dma_free_coherent(dev, adapter->rx_queue.queue_len,
+			  adapter->rx_queue.queue_addr,
+			  adapter->rx_queue.queue_dma);
+
+	for (i = 0; i < IBMVETH_NUM_BUFF_POOLS; i++)
+		if (adapter->rx_buff_pool[i].active)
+			ibmveth_free_buffer_pool(adapter,
+						 &adapter->rx_buff_pool[i]);
+
+	dma_unmap_single(&adapter->vdev->dev, adapter->bounce_buffer_dma,
+			 adapter->netdev->mtu + IBMVETH_BUFF_OH,
+			 DMA_BIDIRECTIONAL);
+	kfree(adapter->bounce_buffer);
+
+	for(i = 0; i < IBMVETH_NUM_BUFF_POOLS; i++)
+		rxq_entries += adapter->rx_buff_pool[i].size;
+
+	adapter->buffer_list_addr = (void*) get_zeroed_page(GFP_KERNEL);
+	if (!adapter->buffer_list_addr) {
+		netdev_err(netdev, "unable to allocate list pages\n");
+		goto out;
+	}
+
+	adapter->filter_list_addr = (void*) get_zeroed_page(GFP_KERNEL);
+	if (!adapter->filter_list_addr) {
+		netdev_err(netdev, "unable to allocate filter pages\n");
+		goto out_free_buffer_list;
+	}
+
+	dev = &adapter->vdev->dev;
+
+	adapter->rx_queue.queue_len = sizeof(struct ibmveth_rx_q_entry) *
+						rxq_entries;
+	adapter->rx_queue.queue_addr =
+		dma_alloc_coherent(dev, adapter->rx_queue.queue_len,
+				   &adapter->rx_queue.queue_dma, GFP_KERNEL);
+	if (!adapter->rx_queue.queue_addr)
+		goto out_free_filter_list;
+
+	adapter->buffer_list_dma = dma_map_single(dev,
+			adapter->buffer_list_addr, 4096, DMA_BIDIRECTIONAL);
+	if (dma_mapping_error(dev, adapter->buffer_list_dma)) {
+		netdev_err(netdev, "unable to map buffer list pages\n");
+		goto out_free_queue_mem;
+	}
+
+	adapter->filter_list_dma = dma_map_single(dev,
+			adapter->filter_list_addr, 4096, DMA_BIDIRECTIONAL);
+	if (dma_mapping_error(dev, adapter->filter_list_dma)) {
+		netdev_err(netdev, "unable to map filter list pages\n");
+		goto out_unmap_buffer_list;
+	}
+
+	adapter->rx_queue.index = 0;
+	adapter->rx_queue.num_slots = rxq_entries;
+	adapter->rx_queue.toggle = 1;
+
+	mac_address = ibmveth_encode_mac_addr(netdev->dev_addr);
+
+	rxq_desc.fields.flags_len = IBMVETH_BUF_VALID |
+					adapter->rx_queue.queue_len;
+	rxq_desc.fields.address = adapter->rx_queue.queue_dma;
+
+	netdev_dbg(netdev, "buffer list @ 0x%p\n", adapter->buffer_list_addr);
+	netdev_dbg(netdev, "filter list @ 0x%p\n", adapter->filter_list_addr);
+	netdev_dbg(netdev, "receive q   @ 0x%p\n", adapter->rx_queue.queue_addr);
+
+	h_vio_signal(adapter->vdev->unit_address, VIO_IRQ_DISABLE);
+
+	lpar_rc = ibmveth_register_logical_lan(adapter, rxq_desc, mac_address);
+
+	if (lpar_rc != H_SUCCESS) {
+		netdev_err(netdev, "h_register_logical_lan failed with %ld\n",
+			   lpar_rc);
+		netdev_err(netdev, "buffer TCE:0x%llx filter TCE:0x%llx rxq "
+			   "desc:0x%llx MAC:0x%llx\n",
+				     adapter->buffer_list_dma,
+				     adapter->filter_list_dma,
+				     rxq_desc.desc,
+				     mac_address);
+		goto out_unmap_filter_list;
+	}
+
+	for (i = 0; i < IBMVETH_NUM_BUFF_POOLS; i++) {
+		if (!adapter->rx_buff_pool[i].active)
+			continue;
+		if (ibmveth_alloc_buffer_pool(&adapter->rx_buff_pool[i])) {
+			netdev_err(netdev, "unable to alloc pool\n");
+			adapter->rx_buff_pool[i].active = 0;
+			goto out_free_buffer_pools;
+		}
+	}
+
+	adapter->bounce_buffer =
+	    kmalloc(netdev->mtu + IBMVETH_BUFF_OH, GFP_KERNEL);
+	if (!adapter->bounce_buffer)
+		goto out_free_irq;
+
+	adapter->bounce_buffer_dma =
+	    dma_map_single(&adapter->vdev->dev, adapter->bounce_buffer,
+			   netdev->mtu + IBMVETH_BUFF_OH, DMA_BIDIRECTIONAL);
+	if (dma_mapping_error(dev, adapter->bounce_buffer_dma)) {
+		netdev_err(netdev, "unable to map bounce buffer\n");
+		goto out_free_bounce_buffer;
+	}
+
+	napi_enable(&adapter->napi);
+	ibmveth_interrupt(netdev->irq, netdev);
+	netif_start_queue(netdev);
+	return;
+
+out_free_bounce_buffer:
+	kfree(adapter->bounce_buffer);
+out_free_irq:
+	free_irq(netdev->irq, netdev);
+out_free_buffer_pools:
+	while (--i >= 0) {
+		if (adapter->rx_buff_pool[i].active)
+			ibmveth_free_buffer_pool(adapter,
+						 &adapter->rx_buff_pool[i]);
+	}
+out_unmap_filter_list:
+	dma_unmap_single(dev, adapter->filter_list_dma, 4096,
+			 DMA_BIDIRECTIONAL);
+out_unmap_buffer_list:
+	dma_unmap_single(dev, adapter->buffer_list_dma, 4096,
+			 DMA_BIDIRECTIONAL);
+out_free_queue_mem:
+	dma_free_coherent(dev, adapter->rx_queue.queue_len,
+			  adapter->rx_queue.queue_addr,
+			  adapter->rx_queue.queue_dma);
+out_free_filter_list:
+	free_page((unsigned long)adapter->filter_list_addr);
+out_free_buffer_list:
+	free_page((unsigned long)adapter->buffer_list_addr);
+out:
+	napi_disable(&adapter->napi);
+	return;
+}
+
 static int ibmveth_open(struct net_device *netdev)
 {
 	struct ibmveth_adapter *adapter = netdev_priv(netdev);
@@ -785,8 +978,7 @@ static int ibmveth_set_csum_offload(struct net_device *dev, u32 data)
 	if (netif_running(dev)) {
 		restart = 1;
 		adapter->pool_config = 1;
-		ibmveth_close(dev);
-		adapter->pool_config = 0;
+		schedule_work(&adapter->reset_task);
 	}
 
 	set_attr = 0;
@@ -869,8 +1061,7 @@ static int ibmveth_set_tso(struct net_device *dev, u32 data)
 	if (netif_running(dev)) {
 		restart = 1;
 		adapter->pool_config = 1;
-		ibmveth_close(dev);
-		adapter->pool_config = 0;
+		schedule_work(&adapter->reset_task);
 	}
 
 	set_attr = 0;
@@ -1658,6 +1849,7 @@ static int ibmveth_probe(struct vio_dev *dev, const struct vio_device_id *id)
 	adapter->netdev = netdev;
 	adapter->mcastFilterSize = *mcastFilterSize_p;
 	adapter->pool_config = 0;
+	INIT_WORK(&adapter->reset_task, ibmveth_reset_task);
 
 	netif_napi_add(netdev, &adapter->napi, ibmveth_poll, 16);
 
@@ -1736,6 +1928,8 @@ static int ibmveth_remove(struct vio_dev *dev)
 	struct ibmveth_adapter *adapter = netdev_priv(netdev);
 	int i;
 
+	cancel_work_sync(&adapter->reset_task);
+
 	for (i = 0; i < IBMVETH_NUM_BUFF_POOLS; i++)
 		kobject_put(&adapter->rx_buff_pool[i].kobj);
 
