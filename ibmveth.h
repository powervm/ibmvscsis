diff --git a/drivers/net/ethernet/ibm/ibmveth.h b/drivers/net/ethernet/ibm/ibmveth.h
index 01c587fc02c70..a18b4833e6688 100644
--- a/drivers/net/ethernet/ibm/ibmveth.h
+++ b/drivers/net/ethernet/ibm/ibmveth.h
@@ -159,6 +159,7 @@ struct ibmveth_adapter {
     bool is_active_trunk;
     void *bounce_buffer;
     dma_addr_t bounce_buffer_dma;
+    struct work_struct reset_task;
 
     u64 fw_ipv6_csum_support;
     u64 fw_ipv4_csum_support;
