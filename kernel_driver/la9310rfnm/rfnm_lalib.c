#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/dma-mapping.h>
#define __RFIC


#include <la9310_base.h>
//#include "rfnm.h"
//#include "rfnm_callback.h"
#include <asm/cacheflush.h>

#include <linux/dma-direct.h>
#include <linux/dma-map-ops.h>
#include <linux/dma-mapping.h>


#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/spinlock.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/list.h>

#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>


#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/usb/composite.h>
#include <linux/err.h>

#include <linux/delay.h>

#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>

#include <linux/timekeeping.h>


//#include "rfnm_types.h"
#include <linux/rfnm-shared.h>
#include <linux/rfnm-gpio.h>

#include "rfnm_lalib.h"

static struct rf_host_stats rfic_stats = {0};
static struct rfdevice *rfdev = NULL;


#define htole32(x) (x)
#define le32toh(x) (x)

#define iowrite32le(v, p) ((*((uint32_t *)(p)) = htole32((v))))
#define ioread32le(p) ((le32toh(*(p))))

#define iowr32(r,v,p) ( iowrite32le ((v),(p)) )
#define iord32(r,p) ( ioread32le((p)) )


/*


#define iowr32(r,v,p) ( r->endian == MOD_BE ? iowrite32be((v), (p)) : iowrite32le ((v),(p)) )
#define iord32(r, p) ( r->endian == MOD_BE ? ioread32be((p)) : ioread32le((p)) )

#define iowr16(r,v,p) ( r->endian == MOD_BE ? iowrite16be((v), (p)) : iowrite16le ((v),(p)) )
#define iord16(r, p) ( r->endian == MOD_BE ? ioread16be((p)) : ioread16le((p)) )

*/

/* #define iowr32(r,v,p) iowrite32be((v), (p)) */
/* #define iord32(r, p) ioread32be((p)) */
/* static inline void iowr32(struct rfdevice *rf, uint32_t val, uint32_t *addr) */
/* { */
/*     if(rf->endian == MOD_BE) */
/*         iowrite32be((val), (addr)); */
/*     else */
/*         iowrite32le((val), (addr)); */
/* } */
/* static inline uint32_t iord32(struct rfdevice *rf, uint32_t *addr) */
/* { */
/*     if(rf->endian == MOD_BE) */
/*         return ioread32be((addr)); */
/*     else */
/*         return ioread32le((addr)); */
/* } */

/*
 * MPIC MSI IRQs to trigger host->modem interrupts
 */

#define SWCMD_SIZE 0x124
#define SWCMD_DATA_SIZE 0x104
#define SWCMD_CMD_SIZE 0x1C

static int la93xx_raise_msgunit_irq(struct rfdevice *rfdev,
			  int msg_unit_idx, int bit_num)
{
	struct la9310_msg_unit *msg_unit = &( ( (struct la9310_msg_unit *)( rfdev->ccsr_p + ( MSG_UNIT_OFFSET ) ) )[msg_unit_idx] );

	iowr32(rfdev, bit_num, &msg_unit->msiir);

	return 0;
}
/* Dummy function till we have proper err2str function in la9310 code */
static inline char *rf_err2str(uint32_t r)
{
    return "";
}

static void rf_raise_modem_irq(struct rfdevice *rfdev)
{
    la93xx_raise_msgunit_irq(rfdev, LA9310_IRQ_MUX_MSG_UNIT, LA9310_RF_SW_CMD_MSG_UNIT_BIT);

}

static void rf_free_cmd(struct rfdevice *rfdev, rf_sw_cmd_desc_t *sw_cmd)
{
	rf_sw_cmd_desc_t *remote_cmd;

	remote_cmd = &((struct rf_host_if *)(rfdev->rfic_p))->rf_mdata.host_swcmd;
//	iowr32(rfdev, RF_SW_CMD_STATUS_FREE, &remote_cmd->status);
	iowr32(rfdev, RF_SW_CMD_STATUS_FREE, &remote_cmd);
	sw_cmd->status = RF_SW_CMD_STATUS_FREE;
}

static void rf_swcmd_get_data( struct rfdevice *rfdev, struct rf_sw_cmd_desc *sw_cmd,
		       int data_size)
{
	struct rf_sw_cmd_desc *remote_cmd;
	u32 *data_r, *data_l, size_wrds, i;

	remote_cmd = &((struct rf_host_if *)(rfdev->rfic_p))->rf_mdata.host_swcmd;
	data_r = (u32 *) &remote_cmd->data[0];
	data_l = (u32 *) &sw_cmd->data[0];
	size_wrds = data_size >> 2;
	for (i = 0; i < size_wrds; i++) {
		data_l[i] = iord32(rfdev, data_r);

		//printk("%d: 0x%x, 0x%x\n", i, data_l[i], *data_r);
		data_r++;
	}
}


long long current_timestamp(void) {
    return ktime_get_ns() / (1000LL * 1000);
}


/*
 * This function will use shared memory to send RFIC SW command
 */
static int rf_send_swcmd(struct rfdevice *rfdev, struct rf_sw_cmd_desc *sw_cmd,
		  int data_size)
{
	int ret = 0, retries = RF_SWCMD_TIMEOUT_RETRIES;
	struct rf_sw_cmd_desc *remote_cmd;
	int cmd_size, i;
	struct rf_host_stats *stats = &rfic_stats;
	//struct gul_dev *gul_dev = NULL;
	u32 *cmd_wrd_l, *cmd_wrd_r;
    //printk("rfdev : rfic_p : %p, ccsr_p : %p, endian : %d\n", rfdev->rfic_p, rfdev->ccsr_p, rfdev->endian);

	remote_cmd = &((struct rf_host_if *)(rfdev->rfic_p))->rf_mdata.host_swcmd;

	if (iord32(rfdev, &remote_cmd->status) != RF_SW_CMD_STATUS_FREE) {
		if (iord32(rfdev, &remote_cmd->status) == RF_SW_CMD_STATUS_DONE) {
			/* This can happen when command processing takes more
			 * time than timeout configured in this driver. We can
			 * process successfully in this case as DONE from MODEM
			 * means FREE for this driver.
			 */
			//printk( "RFIC: timeout is insufficient for command : %d\n",iord32(rfdev, &remote_cmd->cmd));
		} else {
			//printk( "RFIC: mdata host swcmd busy [%d]\n", iord32(rfdev, &remote_cmd->status));
			stats->sw_cmds_desc_busy++;
			ret = -EBUSY;
			goto busy_out;
		}
	}

#ifdef GEUL
	sw_cmd->timeout = RFIC_REMOTE_CMD_TIMEOUT;
#endif
	sw_cmd->status  = RF_SW_CMD_STATUS_POSTED;
	sw_cmd->flags = RF_SW_CMD_REMOTE;

	cmd_size = SWCMD_CMD_SIZE + data_size;
	/*cmd_size is needed in words*/
	cmd_size = cmd_size / 4;
	cmd_wrd_l = (u32 *) sw_cmd;
	cmd_wrd_r = (u32 *) remote_cmd;
	//printk( "cmd %d, cmdsize %d\ncmd dump", sw_cmd->cmd, cmd_size);
	/*Modem E200 cores are big-endian, thus changing endianness.
	 *Copying local command to remote command is sub-optimal, but because
	 *of different endianness it is better to handle change of endianess via
	 *a copy so that code is less error prone. Moreover there is no real
	 *time or low latency requirement of this interface, so a copy here is
	 *fine
	 */
	for (i = 0; i < cmd_size; i++) {
		iowr32(rfdev, *cmd_wrd_l, cmd_wrd_r);
		//printk( "cmd_wrd_l 0x%x, cmd_wrd_r 0x%x\n", *cmd_wrd_l, *cmd_wrd_r);
		cmd_wrd_l++;
		cmd_wrd_r++;
	}
	dma_wmb();
	rf_raise_modem_irq(rfdev);
	dma_wmb();
	/* Wait Modem RF driver to complete processing */
#if 1

    long long start = current_timestamp();
    long long endwait = start + 100;

    while (start < endwait &&
           (iord32(rfdev, &remote_cmd->status) != RF_SW_CMD_STATUS_DONE)) {
        start = current_timestamp();
	}
	if ((iord32(rfdev, &remote_cmd->status)  != RF_SW_CMD_STATUS_DONE)) {
		//printk( "swcmd 0x%x timed out\n", sw_cmd->cmd);
		stats->sw_cmds_timed_out++;
		ret = -EBUSY;
		goto out;
	}
	//printk( "swcmd 0x%x done\n", sw_cmd->cmd);
	stats->sw_cmds_tx++;

	ret = iord32(rfdev, &remote_cmd->result);
	if (ret != RF_SW_CMD_RESULT_OK) {
		//printk( "%s: CMD response error. result[%d:%s]\n", __func__, ret, rf_err2str(ret));
	}
    rf_swcmd_get_data(rfdev, sw_cmd, data_size);

out:
	iowr32(rfdev, RF_SW_CMD_STATUS_FREE, &remote_cmd->status);
#endif
busy_out:

	return ret;
}

int rfnm_last_stream_cmd;

int rfnm_la9310_stream(uint8_t tx, uint8_t *rx) {
	
	uint32_t t = ((tx & 0x3) << 0) | ((rx[0] & 0x3) << 2) | ((rx[1] & 0x3) << 4) | ((rx[2] & 0x3) << 6) | ((rx[3] & 0x3) << 8);

	//printk("stream -> %x\n", t);
	
	// simple code, but important concept: 
	// never trigger this stream command if RFNM_CH_STREAM_OFF is set
	// so that NXP can use the drivers without disabling this lib
	if(t == rfnm_last_stream_cmd) {
		return 0;
	}
	
	struct sw_cmddata_dump_iq * data;
	struct rf_sw_cmd_desc sw_cmd = {0};
	int ret;
	sw_cmd.cmd = RF_SW_CMD_DUMP_IQ_DATA;
	data = (struct sw_cmddata_dump_iq *)&sw_cmd.data[0];
	data->addr = t;
	//data->size = 0x10;

	ret = rf_send_swcmd(rfdev, &sw_cmd, sizeof(struct sw_cmddata_dump_iq));
	
	rf_free_cmd(rfdev, &sw_cmd);

	rfnm_last_stream_cmd = t;

	return ret;
}

EXPORT_SYMBOL(rfnm_la9310_stream);


static __init int rfnm_lalib_init(void)
{
	rfdev = (struct rfdevice *)kzalloc(sizeof(struct rfdevice), GFP_KERNEL);

	void *rfic_p = NULL;
	void __iomem *hif_p = ioremap(HIF_START, HIF_SIZE);
	rfdev->hif_p = hif_p;
	rfic_p = &(((struct la9310_hif *)hif_p)->rf_hif);
    rfdev->rfic_p = rfic_p;

    void __iomem *ccsr_p = ioremap(CCSR_START, CCSR_SIZE);
    rfdev->ccsr_p = ccsr_p;

	rfnm_last_stream_cmd = 0;
	

	return 0;
}

static __exit void rfnm_lalib_exit(void)
{
}


MODULE_PARM_DESC(device, "RFNM LA9310 Command Driver");
module_init(rfnm_lalib_init);
module_exit(rfnm_lalib_exit);
MODULE_LICENSE("GPL");
