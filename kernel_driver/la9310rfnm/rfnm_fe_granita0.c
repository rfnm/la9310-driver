#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/rfnm-shared.h>
#include "rfnm_fe_granita0.h"
#include "rfnm_fe_generic.h"


void granita0_fa_fm_notch(struct rfnm_dgb * dgb_dt, int en) {
	// enabling notch when not using the frequency will mess with the paths
	if(en) {
		rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_6, 1);
		rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_7, 0);
	} else {
		rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_6, 0);
		rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_7, 1);
	}
}

void granita0_fa_filter_0_70(struct rfnm_dgb * dgb_dt) {
	//printk("granita0_filter_0_70\n");
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_1, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_2, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_3, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_4, 1); // G1I1
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_5, 0); // G1I2
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_6, 0); // G1O1
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_7, 0); // G1O2
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_8, 0); // disable fm filter by default
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_9, 1);
}

void granita0_fa_filter_140_225(struct rfnm_dgb * dgb_dt) {
	//printk("granita0_filter_140_225\n");
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_1, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_2, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_3, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_4, 1); // G1I1
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_5, 1); // G1I2
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_6, 0); // G1O1
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_7, 1); // G1O2
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_8, 0); // disable fm filter by default
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_9, 1);
}

void granita0_fa_filter_0_1000(struct rfnm_dgb * dgb_dt) { 
	//printk("granita0_filter_0_1000\n");
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_1, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_2, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_3, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_4, 0); // G1I1
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_5, 1); // G1I2
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_6, 1); // G1O1
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_7, 1); // G1O2
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_8, 0); // disable fm filter by default
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_9, 1);
}

void granita0_fa_filter_420_700(struct rfnm_dgb * dgb_dt) {
	//printk("granita0_filter_420_700\n");
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_1, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_2, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_3, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_4, 0); // G1I1
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_5, 0); // G1I2
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_6, 1); // G1O1
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_7, 0); // G1O2
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_8, 0); // disable fm filter by default
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_9, 1);
}

void granita0_fa_filter_700_1000(struct rfnm_dgb * dgb_dt) {
	//printk("granita0_filter_700_1000\n");
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_1, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_2, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_3, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_G1UL, 1);
}

void granita0_fa_filter_950_3000(struct rfnm_dgb * dgb_dt) {
	//printk("granita0_filter_950_3000\n");
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_1, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_2, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_5, 1); // G2I1
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_6, 0); // G2I2
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_7, 0); // G2O1
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_8, 0); // G2O2
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_G2L, 1);
}

void granita0_fa_filter_1805_2200(struct rfnm_dgb * dgb_dt) {
	//printk("granita0_filter_1805_2200\n");
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_1, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_2, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_5, 1); // G2I1
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_6, 1); // G2I2
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_7, 0); // G2O1
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_8, 1); // G2O2
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_G2L, 1);
}

void granita0_fa_filter_2300_2690(struct rfnm_dgb * dgb_dt) {
	//printk("granita0_filter_2300_2690\n");
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_1, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_2, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_5, 0); // G2I1
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_6, 1); // G2I2
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_7, 1); // G2O1
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_8, 1); // G2O2
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_G2L, 1);
}

void granita0_fa_filter_1574_1605(struct rfnm_dgb * dgb_dt) {
	//printk("granita0_filter_1574_1605\n");
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_1, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_2, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_3, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_4, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_5, 0); // G2I1
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_6, 0); // G2I2
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_7, 1); // G2O1
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_8, 0); // G2O2
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_G2PL1, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_G2L, 1);
}

void granita0_fa_filter_1166_1229(struct rfnm_dgb * dgb_dt) {
	//printk("granita0_filter_1166_1229\n");
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_1, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_2, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_3, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_4, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_5, 0); // G2I1
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_6, 0); // G2I2
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_7, 1); // G2O1
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_8, 0); // G2O2
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_G2PL2, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_G2L, 1);
}
/*
disabled because of hardware error
void granita0_filter_5150_7125(struct rfnm_dgb * dgb_dt) {
	//printk("granita0_filter_5150_7125\n");
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_1, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_G3L, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_2, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_3, 1);
}

void granita0_fa_filter_4900_5850(struct rfnm_dgb * dgb_dt) {
	//printk("granita0_filter_4900_5850\n");
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_1, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_G3L, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_2, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_3, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_4, 0);
}*/

void granita0_fa_filter_4400_5000(struct rfnm_dgb * dgb_dt) {
	//printk("granita0_filter_4400_5000\n");
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_1, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_G3L, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_2, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_3, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_4, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_5, 1);
}

void granita0_fa_filter_3200_4200(struct rfnm_dgb * dgb_dt) {
	//printk("granita0_filter_3200_4200\n");
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_1, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_G3L, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_2, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_3, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_4, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_5, 1);
}

void granita0_fa_filter_3000_5000(struct rfnm_dgb * dgb_dt) {
	//printk("granita0_filter_3000_5000\n");
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_1, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_G3L, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_2, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_3, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_4, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FA_5, 1);
}


















void granita0_fb_fm_notch(struct rfnm_dgb * dgb_dt, int en) {
	// enabling notch when not using the frequency will mess with the paths
	if(en) {
		rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_6, 1);
		rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_7, 0);
	} else {
		rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_6, 0);
		rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_7, 1);
	}
}

void granita0_fb_filter_0_70(struct rfnm_dgb * dgb_dt) {
	//printk("granita0_filter_0_70\n");
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_1, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_2, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_3, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_4, 1); // G1I1
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_5, 0); // G1I2
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_6, 0); // G1O1
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_7, 0); // G1O2
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_8, 0); // disable fm filter by default
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_9, 1);
}

void granita0_fb_filter_140_225(struct rfnm_dgb * dgb_dt) {
	//printk("granita0_filter_140_225\n");
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_1, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_2, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_3, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_4, 1); // G1I1
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_5, 1); // G1I2
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_6, 0); // G1O1
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_7, 1); // G1O2
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_8, 0); // disable fm filter by default
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_9, 1);
}

void granita0_fb_filter_0_1000(struct rfnm_dgb * dgb_dt) { 
	//printk("granita0_filter_0_1000\n");
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_1, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_2, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_3, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_4, 0); // G1I1
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_5, 1); // G1I2
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_6, 1); // G1O1
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_7, 1); // G1O2
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_8, 0); // disable fm filter by default
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_9, 1);
}

void granita0_fb_filter_420_700(struct rfnm_dgb * dgb_dt) {
	//printk("granita0_filter_420_700\n");
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_1, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_2, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_3, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_4, 0); // G1I1
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_5, 0); // G1I2
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_6, 1); // G1O1
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_7, 0); // G1O2
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_8, 0); // disable fm filter by default
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_9, 1);
}

void granita0_fb_filter_700_1000(struct rfnm_dgb * dgb_dt) {
	//printk("granita0_filter_700_1000\n");
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_1, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_2, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_3, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_G1UL, 1);
}

void granita0_fb_filter_950_3000(struct rfnm_dgb * dgb_dt) {
	//printk("granita0_filter_950_3000\n");
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_1, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_2, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_5, 1); // G2I1
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_6, 0); // G2I2
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_7, 0); // G2O1
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_8, 0); // G2O2
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_G2L, 1);
}

void granita0_fb_filter_1805_2200(struct rfnm_dgb * dgb_dt) {
	//printk("granita0_filter_1805_2200\n");
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_1, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_2, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_5, 1); // G2I1
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_6, 1); // G2I2
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_7, 0); // G2O1
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_8, 1); // G2O2
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_G2L, 1);
}

void granita0_fb_filter_2300_2690(struct rfnm_dgb * dgb_dt) {
	//printk("granita0_filter_2300_2690\n");
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_1, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_2, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_5, 0); // G2I1
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_6, 1); // G2I2
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_7, 1); // G2O1
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_8, 1); // G2O2
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_G2L, 1);
}

void granita0_fb_filter_1574_1605(struct rfnm_dgb * dgb_dt) {
	//printk("granita0_filter_1574_1605\n");
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_1, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_2, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_3, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_4, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_5, 0); // G2I1
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_6, 0); // G2I2
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_7, 1); // G2O1
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_8, 0); // G2O2
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_G2PL1, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_G2L, 1);
}

void granita0_fb_filter_1166_1229(struct rfnm_dgb * dgb_dt) {
	//printk("granita0_filter_1166_1229\n");
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_1, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_2, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_3, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_4, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_5, 0); // G2I1
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_6, 0); // G2I2
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_7, 1); // G2O1
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_8, 0); // G2O2
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_G2PL2, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_G2L, 1);
}
/*
disabled because of hardware error
void granita0_filter_5150_7125(struct rfnm_dgb * dgb_dt) {
	//printk("granita0_filter_5150_7125\n");
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_1, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_G3L, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_2, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_3, 1);
}*/

void granita0_fb_filter_4900_5850(struct rfnm_dgb * dgb_dt) {
	//printk("granita0_filter_4900_5850\n");
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_1, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_G3L, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_2, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_3, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_4, 0);
}

void granita0_fb_filter_4400_5000(struct rfnm_dgb * dgb_dt) {
	//printk("granita0_filter_4400_5000\n");
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_1, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_G3L, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_2, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_3, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_4, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_5, 1);
}

void granita0_fb_filter_3200_4200(struct rfnm_dgb * dgb_dt) {
	//printk("granita0_filter_3200_4200\n");
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_1, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_G3L, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_2, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_3, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_4, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_5, 1);
}

void granita0_fb_filter_3000_5000(struct rfnm_dgb * dgb_dt) {
	//printk("granita0_filter_3000_5000\n");
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_1, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_G3L, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_2, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_3, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_4, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_FB_5, 1);
}

















void granita0_rffc_rx_1166_1229(struct rfnm_dgb * dgb_dt) {
	//printk("granita0_rffc_rx_1166_1229\n");
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_TX_FC_RAI, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_TX_FC_RBI, 0);

	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_TX_FC_RTA, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_TX_FC_RTB, 1);

	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_TX_FC_RFAO1, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_TX_FC_RFAO2, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_TX_FC_RFBO1, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_TX_FC_RFBO2, 1);

	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_TX_FC_RFAI1, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_TX_FC_RFAI2, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_TX_FC_RFBI1, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_TX_FC_RFBI2, 1);
}



void granita0_ant_a_loopback(struct rfnm_dgb * dgb_dt) {
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_T, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_TI, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_TS, 0);

	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_AO1, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_AO2, 0);

	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_TA, 0);
}

void granita0_ant_a_through(struct rfnm_dgb * dgb_dt) {
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_A, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_TA, 1);
}

void granita0_ant_a_embeded_lf(struct rfnm_dgb * dgb_dt) {
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_AO1, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_AO2, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_TA, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_OAS, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_OAI, 0);
}

void granita0_ant_a_embeded_hf(struct rfnm_dgb * dgb_dt) {
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_AO1, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_AO2, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_TA, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_OAS, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_OAI, 1);
}

void granita0_ant_a_attn_12(struct rfnm_dgb * dgb_dt) {
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_A, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_AI1, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_AI2, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_AO1, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_AO2, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_TA, 0);
}

void granita0_ant_a_attn_24(struct rfnm_dgb * dgb_dt) {
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_A, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_AI1, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_AI2, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_AO1, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_AO2, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_TA, 0);
}

void granita0_ant_a_crossover(struct rfnm_dgb * dgb_dt) {
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_A, 1);

	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_AI1, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_AI2, 0);
	
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_H, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_TH, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_TI, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_TS, 1);

	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_BO1, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_BO2, 0);

	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_TB, 1);
}

void granita0_ant_a_tx(struct rfnm_dgb * dgb_dt) {
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_T, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_TH, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_H, 1);

	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_AI1, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_AI2, 0);

	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_A, 1);
}





void granita0_ant_b_loopback(struct rfnm_dgb * dgb_dt) {
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_T, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_TI, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_TS, 1);

	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_BO1, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_BO2, 0);

	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_TB, 1);
}

void granita0_ant_b_through(struct rfnm_dgb * dgb_dt) {
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_B, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_TB, 0);
}

void granita0_ant_b_embeded_lf(struct rfnm_dgb * dgb_dt) {
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_BO1, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_BO2, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_TB, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_OAS, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_OAI, 0);
}

void granita0_ant_b_embeded_hf(struct rfnm_dgb * dgb_dt) {
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_BO1, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_BO2, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_TB, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_OAS, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_OAI, 1);
}

void granita0_ant_b_attn_12(struct rfnm_dgb * dgb_dt) {
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_B, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_BI1, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_BI2, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_BO1, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_BO2, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_TB, 1);
}

void granita0_ant_b_attn_24(struct rfnm_dgb * dgb_dt) {
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_B, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_BI1, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_BI2, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_BO1, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_BO2, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_TB, 1);
}

void granita0_ant_b_crossover(struct rfnm_dgb * dgb_dt) {
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_B, 0);

	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_BI1, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_BI2, 0);
	
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_H, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_TH, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_TI, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_TS, 0);

	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_AO1, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_AO2, 0);

	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_TA, 0);
}

void granita0_ant_b_tx(struct rfnm_dgb * dgb_dt) {
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_T, 0);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_TH, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_H, 0);

	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_BI1, 1);
	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_BI2, 0);

	rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_ANT_B, 0);
}





#define RFNM_GRANITA0_TX_BAND_1 (1)
#define RFNM_GRANITA0_TX_BAND_2 (2)
#define RFNM_GRANITA0_TX_BAND_3 (3)
#define RFNM_GRANITA0_TX_BAND_4 (4)

void granita0_tx_band(struct rfnm_dgb * dgb_dt, int band) {

	switch(band) {
		case RFNM_GRANITA0_TX_BAND_1:
			rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_TX_TF1, 0);
			rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_TX_TF2, 0);
			break;
		case RFNM_GRANITA0_TX_BAND_2:
			rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_TX_TF1, 0);
			rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_TX_TF2, 1);
			rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_TX_TRI, 1);
			break;
		case RFNM_GRANITA0_TX_BAND_3:
			rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_TX_TF1, 1);
			rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_TX_TF2, 0);
			break;
		case RFNM_GRANITA0_TX_BAND_4:
			rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_TX_TF1, 1);
			rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_TX_TF2, 1);
			break;
	}

}



int16_t granita0_tx_manual_table [][2] = {
	// all PA off, all attenuation off
	{1, 2000},
	{10000, 2000}, // above max freq as last one
};

int granita0_tx_power(struct rfnm_dgb * dgb_dt, int freq, int target) {
	target = target * 100;

	int cur_power = 0;
	int i = 0;
	while(granita0_tx_manual_table[i][0] < freq) {
		cur_power = -granita0_tx_manual_table[i][1];
		i++;
	}

	if(1 && target > cur_power) {
		rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_TX_TL1I, 0);
		rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_TX_TL1O, 1);
		rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_TX_PA_S1_EN, 0);
		cur_power += 2000;
	} else {
		rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_TX_TL1I, 1);
		rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_TX_TL1O, 0);
		rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_TX_PA_S1_EN, 1);
	}
	
	if(1 && target > cur_power) {
		rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_TX_TL2I, 0);
		rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_TX_TL2O, 1);
		cur_power += 2000;
	} else {
		rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_TX_TL2I, 1);
		rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_TX_TL2O, 0);
	}
	
	if(target < (cur_power - 2400)) {
		rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_TX_T24I, 0);
		//rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_TX_T24O, 1);
		cur_power -= 2400;
	} else {
		rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_TX_T24I, 1);
		//rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_TX_T24O, 0);
	}
	
	if(target < (cur_power - 1200)) {
		rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_TX_T12I, 0);
		//rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_TX_T12O, 1);
		cur_power -= 1200;
	} else {
		rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_TX_T12I, 1);
		//rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_TX_T12O, 0);
	}
	
	if(target < (cur_power - 600)) {
		rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_TX_T6I, 0);
		rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_TX_T6O, 1);
		cur_power -= 600;
	} else {
		rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_TX_T6I, 1);
		rfnm_fe_srb(dgb_dt, RFNM_GRANITA0_TX_T6O, 0);
	}
	
	//printk("%d vs ", cur_power);
	if(target > cur_power) {
		return abs(target - cur_power);
	} else {
		return -abs(target - cur_power);
	}
}


void granita0_tx_freqsel(struct rfnm_dgb * dgb_dt, int freq) {
    if(freq < 1000) {
        granita0_tx_band(dgb_dt, RFNM_GRANITA0_TX_BAND_1);
    } else if(freq < 3000) {
        granita0_tx_band(dgb_dt, RFNM_GRANITA0_TX_BAND_2);
    } else if(freq < 5000) {
        granita0_tx_band(dgb_dt, RFNM_GRANITA0_TX_BAND_3);
    } else {
        granita0_tx_band(dgb_dt, RFNM_GRANITA0_TX_BAND_4);
    }
}

void granita0_fa(struct rfnm_dgb * dgb_dt, int freq) {
    
    granita0_fa_fm_notch(dgb_dt, 1);

    if(freq < 600) {
        if(freq < 70) {
            granita0_fa_filter_0_70(dgb_dt);
        } else if(freq >= 140 && freq <= 225) {
            granita0_fa_filter_140_225(dgb_dt);
        } else if(freq >= 420) {
            granita0_fa_filter_420_700(dgb_dt);
        } else {
            granita0_fa_filter_0_1000(dgb_dt);
            if(freq > 60 && freq < 130) {
                granita0_fa_fm_notch(dgb_dt, 0);
            }
        }
    } else if(freq < 1000) {
        granita0_fa_filter_700_1000(dgb_dt);
    } else if(freq < 3000) {
        if(freq >= 1166 && freq <= 1229) {
            granita0_fa_filter_1166_1229(dgb_dt);
        } else if(freq >= 1574 && freq <= 1605) {
            granita0_fa_filter_1574_1605(dgb_dt);
        } else if(freq >= 1805 && freq <= 2200) {
            granita0_fa_filter_1805_2200(dgb_dt);
        } else if(freq >= 2300 && freq <= 2690) {
            granita0_fa_filter_2300_2690(dgb_dt);
        } else {
            granita0_fa_filter_950_3000(dgb_dt);
        }
    } else if(freq < 5000) {
        if(freq >= 3200 && freq <= 4200) {
            granita0_fa_filter_3200_4200(dgb_dt);
        } else if(freq >= 4400 && freq <= 5000) {
            granita0_fa_filter_4400_5000(dgb_dt);
        } else {
            granita0_fa_filter_3000_5000(dgb_dt);
        }
    } else {
        // hardware error
        //granita0_fa_filter_4900_5850();
        printk("broken filter in this board revision");
    }    
}

void granita0_fb(struct rfnm_dgb * dgb_dt, int freq) {
    
    granita0_fb_fm_notch(dgb_dt, 1);

    if(freq < 600) {
        if(freq < 70) {
            granita0_fb_filter_0_70(dgb_dt);
        } else if(freq >= 140 && freq <= 225) {
            granita0_fb_filter_140_225(dgb_dt);
        } else if(freq >= 420) {
            granita0_fb_filter_420_700(dgb_dt);
        } else {
            granita0_fb_filter_0_1000(dgb_dt);
            if(freq > 60 && freq < 130) {
                granita0_fb_fm_notch(dgb_dt, 0);
            }
        }
    } else if(freq < 1000) {
        granita0_fb_filter_700_1000(dgb_dt);
    } else if(freq < 3000) {
        if(freq >= 1166 && freq <= 1229) {
            granita0_fb_filter_1166_1229(dgb_dt);
        } else if(freq >= 1574 && freq <= 1605) {
            granita0_fb_filter_1574_1605(dgb_dt);
        } else if(freq >= 1805 && freq <= 2200) {
            granita0_fb_filter_1805_2200(dgb_dt);
        } else if(freq >= 2300 && freq <= 2690) {
            granita0_fb_filter_2300_2690(dgb_dt);
        } else {
            granita0_fb_filter_950_3000(dgb_dt);
        }
    } else if(freq < 5000) {
        if(freq >= 3200 && freq <= 4200) {
            granita0_fb_filter_3200_4200(dgb_dt);
        } else if(freq >= 4400 && freq <= 5000) {
            granita0_fb_filter_4400_5000(dgb_dt);
        } else {
            granita0_fb_filter_3000_5000(dgb_dt);
        }
    } else {
        // hardware error
        //granita0_fb_filter_4900_5850();
        printk("broken filter in this board revision");
    }
}

