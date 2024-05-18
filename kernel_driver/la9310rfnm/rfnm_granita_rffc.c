#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/dma-mapping.h>
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
#include <linux/delay.h>

#include <linux/rfnm-shared.h>
#include <linux/rfnm-gpio.h>

#include "rfnm_granita.h"

#include "rfnm_fe_generic.h"
#include "rfnm_fe_granita0.h"

#include <linux/log2.h>



/*
    credit to XGudron from the UA3REO-DDC-Transceiver project
*/

uint16_t rffc2071_regs[RFNM_RFFC2071_REGS_NUM] = {
    0xbefa, /* 00 LF - Loop Filter Configuration */
    0x4064, /* 01 XO - Crystal Oscillator Configuration */
    0x9055, /* 02 CAL_TIME - Calibration Timing */
    0x2d02, /* 03 VCO_CTRL - Calibration Control */
    0xacbf, /* 04 CT_CAL1 - Path 1 Coarse Tuning Calibration */
    0xacbf, /* 05 CT_CAL2 - Path 2 Coarse Tuning Calibration */
    0x0028, /* 06 PLL_CAL1 - Path 1 PLL Calibration */
    0x0028, /* 07 PLL_CAL2 - Path 2 PLL Calibration */
    0xff00, /* 08 VCO_AUTO - Auto VCO select control */
    0x8A20, /* 09 PLL_CTRL - PLL Control 0x8220 */
    0x0202, /* 0A PLL_BIAS - PLL Bias Settings */
    0xFE00, /* 0B MIX_CONT - Mixer Control (0x4800 default + add current + full duplex) */
    0x1a94, /* 0C P1_FREQ1 - Path 1 Frequency 1 */
    0xd89d, /* 0D P1_FREQ2 - Path 1 Frequency 2 */
    0x8900, /* 0E P1_FREQ3 - Path 1 Frequency 3 */
    0x1e84, /* 0F P2_FREQ1 - Path 2 Frequency 1 */
    0x89d8, /* 10 P2_FREQ2 - Path 2 Frequency 2 */
    0x9d00, /* 11 P2_FREQ3 - Path 2 Frequency 3 */
    0x2A80, /* 12 FN_CTRL - Frac-N Control */
    0x0000, /* 13 EXT_MOD - Frequency modulation control 1 */
    0x0000, /* 14 FMOD - Frequency modulation control 2 */
    0x0000, /* 15 SDI_CTRL - SDI Control */
    0x0000, /* 16 GPO - General Purpose Outputs */
    0x4900, /* 17 T_VCO - Temperature Compensation VCO Curves */
    0x0281, /* 18 IQMOD1 – Modulator Calibration */
    0xf00f, /* 19 IQMOD2 – Modulator Control */
    0x0000, /* 1A IQMOD3 - Modulator Buffer Control */
    0x0000, /* 1B IQMOD4– Modulator Core Control */
    0xc840, /* 1C TEMPC_CTRL – Temperature compensation control */
    0x1000, /* 1D DEV_CTRL - Readback register and RSM Control */
    0x0005, /* 1E TEST - Test register */
};

#define SCK_CLR rfnm_gpio_clear(dgb_dt->dgb_id, RFNM_RFFC_CLK);
#define SCK_SET rfnm_gpio_set(dgb_dt->dgb_id, RFNM_RFFC_CLK);

//#define RFFC_DELAY {volatile int rffc_delay; for(rffc_delay = 0; rffc_delay < 100; rffc_delay++) {rffc_delay+=1;}}
#define RFFC_DELAY {}

#define RFNM_RFFC_SS RFNM_DGB_GPIO3_22 //FLEXCAN1_TX -> SAI5_RXD1
#define RFNM_RFFC_RST RFNM_DGB_GPIO3_21
#define RFNM_RFFC_CLK RFNM_DGB_GPIO4_8
#define RFNM_RFFC_DATA RFNM_DGB_GPIO3_23 //FLEXCAN1_RX -> SAI5_RXD2

/*
*(gpio3_dr + 1) = *(gpio3_dr + 1) | (0x1 << 22); // enable ss gpio output
*(gpio3_dr + 1) = *(gpio3_dr + 1) | (0x1 << 21); // enable rst gpio output
*(gpio5_dr + 1) = *(gpio5_dr + 1) | (0x1 << 6) | (0x1 << 7); // enable clk+data gpio output
*/


void rfnm_rffc_shift_bit(struct rfnm_dgb *dgb_dt, uint32_t val, uint8_t size) {
	for (uint8_t i = 0; i < size; i++) {
		SCK_CLR;
		if(!!(val & (1 << (size - 1 - i)))) {
			rfnm_gpio_set(dgb_dt->dgb_id, RFNM_RFFC_DATA);
		} else {
			rfnm_gpio_clear(dgb_dt->dgb_id, RFNM_RFFC_DATA);
		}
		RFFC_DELAY;
		SCK_SET;
		RFFC_DELAY;
	}
}

void rfnm_rffc_write_reg(struct rfnm_dgb *dgb_dt, uint8_t reg, uint16_t val) {

	uint32_t tmp;
/*
    3033_01E4h MX8MP_IOMUXC_ECSPI1_MOSI__ECSPI1_MOSI GPIO7
    3033_01E0h MX8MP_IOMUXC_ECSPI1_SCLK__ECSPI1_SCLK GPIO6
*/	

	//*(iomuxc_data + (0x1E4/4)) = 5;
	//*(iomuxc_data + (0x1E0/4)) = 5;

	SCK_CLR;
	RFFC_DELAY;
	SCK_SET;
	RFFC_DELAY;

	rfnm_gpio_clear(dgb_dt->dgb_id, RFNM_RFFC_SS);

	rfnm_rffc_shift_bit(dgb_dt, (reg & 0x7F), 9);
	rfnm_rffc_shift_bit(dgb_dt, val, 16);

	rfnm_gpio_set(dgb_dt->dgb_id, RFNM_RFFC_SS);

	SCK_CLR;
	RFFC_DELAY;
	SCK_SET;
	RFFC_DELAY;

	//*(iomuxc_data + (0x1E4/4)) = 0;
	//*(iomuxc_data + (0x1E0/4)) = 0;
}

void rfnm_rffc_deinit(struct rfnm_dgb *dgb_dt) {
	rfnm_gpio_clear(dgb_dt->dgb_id, RFNM_RFFC_RST);
}

void rfnm_rffc_init(struct rfnm_dgb *dgb_dt) {

	rfnm_gpio_output(dgb_dt->dgb_id, RFNM_RFFC_SS);
	rfnm_gpio_output(dgb_dt->dgb_id, RFNM_RFFC_RST);
	rfnm_gpio_output(dgb_dt->dgb_id, RFNM_RFFC_CLK);
	rfnm_gpio_output(dgb_dt->dgb_id, RFNM_RFFC_DATA);

	rfnm_gpio_set(dgb_dt->dgb_id, RFNM_RFFC_RST); 

    int i;
	for (i = 0; i < RFNM_RFFC2071_REGS_NUM; i++) {
		rfnm_rffc_write_reg(dgb_dt, i, rffc2071_regs[i]);
	}

	// set ENBL and MODE to be configured via 3-wire IF
	rffc2071_regs[0x15] |= (1 << 15) + (1 << 13);
	rfnm_rffc_write_reg(dgb_dt, 0x15, rffc2071_regs[0x15]);

	// GPOs are active at all time and send LOCK flag to GPO4
	rffc2071_regs[0x16] |= (1 << 1) + (1 << 0);
	rfnm_rffc_write_reg(dgb_dt, 0x16, rffc2071_regs[0x16]);
}

void rfnm_rffc_disable(struct rfnm_dgb *dgb_dt) {
	// clear ENBL BIT
	rffc2071_regs[0x15] &= ~(1 << 14);
	rfnm_rffc_write_reg(dgb_dt, 0x15, rffc2071_regs[0x15]);
}

void rfnm_rffc_enable(struct rfnm_dgb *dgb_dt) {
	rffc2071_regs[0x15] |= (1 << 14);
	rfnm_rffc_write_reg(dgb_dt, 0x15, rffc2071_regs[0x15]);
}

static uint64_t rfnm_rffc_freq_calc(struct rfnm_dgb *dgb_dt, uint64_t lo_freq_Hz) {
	uint8_t n_lo = ilog2(RFNM_RFFC2071_LO_MAX / lo_freq_Hz);
	uint8_t lodiv = 1 << n_lo; // 2^n_lo
	uint64_t fvco = lodiv * lo_freq_Hz;
	uint8_t fbkdiv;

	if (fvco > 3200000000) {
		fbkdiv = 4;
		rffc2071_regs[0x0] &= ~(7);
		rffc2071_regs[0x0] |= 3;
		rfnm_rffc_write_reg(dgb_dt, 0x0, rffc2071_regs[0x0]);
	} else {
		fbkdiv = 2;
		rffc2071_regs[0x0] &= ~(7);
		rffc2071_regs[0x0] |= 2;
		rfnm_rffc_write_reg(dgb_dt, 0x0, rffc2071_regs[0x0]);
	}

	double n_div = (double)fvco / (double)fbkdiv / RFNM_RFFC2071_REF_FREQ;
	uint8_t n = n_div;
	uint16_t nummsb = (1 << 16) * (n_div - n);
	uint8_t numlsb = (1 << 8) * ((1 << 16) * (n_div - n) - nummsb);
	// println(n_lo, " ", lodiv, " ", fvco, " ", fbkdiv, " ", n_div, " ", n, " ", nummsb, " ", numlsb);

	/* Path 1 */

	// p1_freq1
	rffc2071_regs[0x0C] &= ~(0x3fff << 2);
	rffc2071_regs[0x0C] |= (n << 7) + (n_lo << 4) + ((fbkdiv >> 1) << 2);
	rfnm_rffc_write_reg(dgb_dt, 0x0C, rffc2071_regs[0x0C]);

	// p1_freq2
	rffc2071_regs[0x0D] = nummsb;
	rfnm_rffc_write_reg(dgb_dt, 0x0D, rffc2071_regs[0x0D]);

	// p1_freq3
	rffc2071_regs[0x0E] = (numlsb << 8);
	rfnm_rffc_write_reg(dgb_dt, 0x0E, rffc2071_regs[0x0E]);

	/* Path 2 */

	// p2_freq1
	rffc2071_regs[0x0F] &= ~(0x3fff << 2);
	rffc2071_regs[0x0F] |= (n << 7) + (n_lo << 4) + ((fbkdiv >> 1) << 2);
	rfnm_rffc_write_reg(dgb_dt, 0x0F, rffc2071_regs[0x0F]);

	// p2_freq2
	rffc2071_regs[0x10] = nummsb;
	rfnm_rffc_write_reg(dgb_dt, 0x10, rffc2071_regs[0x10]);

	// p2_freq3
	rffc2071_regs[0x11] = (numlsb << 8);
	rfnm_rffc_write_reg(dgb_dt, 0x11, rffc2071_regs[0x11]);

	// reset PLL
	rffc2071_regs[0x09] = (1 << 3);
	rfnm_rffc_write_reg(dgb_dt, 0x09, rffc2071_regs[0x09]);

	// calculate result freq
	uint64_t tune_freq_hz = RFNM_RFFC2071_REF_FREQ * fbkdiv * ((double)n + ((double)((nummsb << 8) | numlsb) / (1 << 24))) / lodiv;
	return tune_freq_hz;
}


uint64_t rfnm_rffc_set_freq(struct rfnm_dgb *dgb_dt, uint64_t lo_freq_Hz) {
    rfnm_rffc_disable(dgb_dt);
	uint64_t set_freq = rfnm_rffc_freq_calc(dgb_dt, lo_freq_Hz);
	rfnm_rffc_enable(dgb_dt);
	printk("RFFC2071 LO Freq: %d\n", set_freq);
	return set_freq;
}