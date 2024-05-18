
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/rfnm-shared.h>

#include "rfnm_fe_lime0.h"
#include "rfnm_fe_generic.h"

void lime0_disable_all_pa(struct rfnm_dgb * dgb_dt) {
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_PA_S1, 1);
}

void lime0_disable_all_lna(struct rfnm_dgb * dgb_dt) {
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_G1L, 0);
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_G2L, 0);
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_G2PL1, 0);
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_G2PL2, 0);
}



void lime0_fm_notch(struct rfnm_dgb * dgb_dt, int en) {
	// enabling notch when not using the frequency will mess with the paths
	if(en) {
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA2, 0);
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA3, 1);
		//printk("notch on\n");
	} else {
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA2, 1);
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA3, 0);
		//printk("notch off\n");
	}
}

void lime0_filter_0_2(struct rfnm_dgb * dgb_dt) {
	//printk("lime0_filter_0_2\n");
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA1, 0);
	
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA4, 0); // G1
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA5, 1);
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA6, 0);
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA7, 0); // !FA5

	rfnm_fe_srb(dgb_dt, RFNM_LIME0_G1L, 0);
}

void lime0_filter_2_12(struct rfnm_dgb * dgb_dt) {
	//printk("lime0_filter_2_12\n");
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA1, 0);
	
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA4, 0); // G1
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA5, 1);
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA6, 1);
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA7, 0); // !FA5

	rfnm_fe_srb(dgb_dt, RFNM_LIME0_G1L, 0);
}

void lime0_filter_12_30(struct rfnm_dgb * dgb_dt) {
	//printk("lime0_filter_12_30\n");
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA1, 0);
	
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA4, 0); // G1
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA5, 0);
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA6, 1);
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA7, 1); // !FA5

	rfnm_fe_srb(dgb_dt, RFNM_LIME0_G1L, 0);
}

void lime0_filter_30_60(struct rfnm_dgb * dgb_dt) {
	//printk("lime0_filter_30_60\n");
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA1, 0);
	
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA4, 0); // G1
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA5, 0);
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA6, 0);
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA7, 1); // !FA5

	rfnm_fe_srb(dgb_dt, RFNM_LIME0_G1L, 0);
}

void lime0_filter_60_120(struct rfnm_dgb * dgb_dt) {
	//printk("lime0_filter_60_120\n");
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA1, 0);
	
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA4, 1); // G1
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA5, 1);
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA6, 0);
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA7, 0); // !FA5
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA8, 1);

	rfnm_fe_srb(dgb_dt, RFNM_LIME0_G2L, 1);
}

void lime0_filter_120_250(struct rfnm_dgb * dgb_dt) {
	//printk("lime0_filter_120_250\n");
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA1, 0);
	
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA4, 1); // G1
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA5, 1);
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA6, 1);
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA7, 0); // !FA5
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA8, 1);

	rfnm_fe_srb(dgb_dt, RFNM_LIME0_G2L, 1);
}

void lime0_filter_250_480(struct rfnm_dgb * dgb_dt) {
	//printk("lime0_filter_250_480\n");
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA1, 0);
	
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA4, 1); // G1
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA5, 0);
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA6, 1);
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA7, 1); // !FA5
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA8, 1);

	rfnm_fe_srb(dgb_dt, RFNM_LIME0_G2L, 1);
}

void lime0_filter_480_1000(struct rfnm_dgb * dgb_dt) {
	//printk("lime0_filter_480_1000\n");
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA1, 0);
	
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA4, 1); // G1
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA5, 0);
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA6, 0);
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA7, 1); // !FA5
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA8, 1);

	rfnm_fe_srb(dgb_dt, RFNM_LIME0_G2L, 1);

}

void lime0_filter_950_4000(struct rfnm_dgb * dgb_dt) {

	//printk("lime0_filter_950_4000\n");
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA1, 1);
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA2, 1);
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA3, 0);
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA4, 0); // G2PI
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA5, 0); // G2PO
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA6, 0);
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA7, 0);
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA8, 0); // G1O

	rfnm_fe_srb(dgb_dt, RFNM_LIME0_G2L, 1);

}

void lime0_filter_1805_2200(struct rfnm_dgb * dgb_dt) {
	//printk("lime0_filter_1805_2200\n");
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA1, 1);
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA2, 1);
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA3, 1);
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA4, 0); // G2PI
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA5, 0); // G2PO
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA6, 0);
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA7, 1);
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA8, 0); // G1O

	rfnm_fe_srb(dgb_dt, RFNM_LIME0_G2L, 1);
}

void lime0_filter_2300_2690(struct rfnm_dgb * dgb_dt) {
	//printk("lime0_filter_2300_2690\n");
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA1, 1);
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA2, 0);
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA3, 1);
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA4, 0); // G2PI
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA5, 0); // G2PO
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA6, 1);
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA7, 1);
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA8, 0); // G1O

	rfnm_fe_srb(dgb_dt, RFNM_LIME0_G2L, 1);
}

void lime0_filter_1574_1605(struct rfnm_dgb * dgb_dt) {
	//printk("lime0_filter_1574_1605\n");
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA1, 1);
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA2, 0);
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA3, 0);
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA4, 0); // G2PI
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA5, 1); // G2PO
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA6, 1);
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA7, 0);
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA8, 0); // G1O

	rfnm_fe_srb(dgb_dt, RFNM_LIME0_G2PL1, 1);
	// do not enable second amp on GPS line	
}

void lime0_filter_1166_1229(struct rfnm_dgb * dgb_dt) {
	//printk("lime0_filter_1166_1229\n");
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA1, 1);
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA2, 0);
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA3, 0);
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA4, 1); // G2PI
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA5, 0); // G2PO
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA6, 1);
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA7, 0);
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_FA8, 0); // G1O

	rfnm_fe_srb(dgb_dt, RFNM_LIME0_G2PL2, 1);
	// do not enable second amp on GPS line
}

void lime0_loopback(struct rfnm_dgb * dgb_dt) {
// loopback

		//rfnm_fe_srb(dgb_dt, RFNM_LIME0_T24I, 1);
		//rfnm_fe_srb(dgb_dt, RFNM_LIME0_T24O, 0);

		rfnm_fe_srb(dgb_dt, RFNM_LIME0_T24I, 0);
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_T24O, 1);


		rfnm_fe_srb(dgb_dt, RFNM_LIME0_T12I, 1);
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_T12O, 0);
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_T6I, 1);
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_T6O, 0);

#if 1
		// disable pa
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_TL1I, 1);
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_TL1O, 0);
#else
		// enable pa
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_TL1I, 0);
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_TL1O, 1);
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_PA_S1, 0);
#endif
		
#if 1
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_TL2I, 1);
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_TL2O, 0);
#else	
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_TL2I, 0);
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_TL2O, 1);
#endif
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_ANT_T, 1);

		rfnm_fe_srb(dgb_dt, RFNM_LIME0_AO1, 1);
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_AO2, 0);


		
#if 0
		// disable both PAs
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_TL1I, 1);
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_TL1O, 0);

		rfnm_fe_srb(dgb_dt, RFNM_LIME0_TL2I, 1);
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_TL2O, 0);
#endif
}


#define RFNM_LIME0_TX_BAND_LOW 0
#define RFNM_LIME0_TX_BAND_HIGH 1

void lime0_tx_band(struct rfnm_dgb * dgb_dt, int band) {
	if(band == RFNM_LIME0_TX_BAND_HIGH) {
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_TFI, 1);
	} else {
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_TFI, 0);
	}
}

void lime0_tx_lpf(struct rfnm_dgb * dgb_dt, int freq) {
	//freq = 10000;
	if(freq < 50) {
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_TFA, 0);
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_TFB1, 1);
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_TFB2, 0);
	} else if(freq < 105) {
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_TFA, 0);
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_TFB1, 1);
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_TFB2, 1);
	} else if(freq < 200) {
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_TFA, 0);
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_TFB1, 0);
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_TFB2, 1);
	} else if(freq < 400) {
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_TFA, 1);
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_TFB1, 0);
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_TFB2, 0);
	} else if(freq < 700) {
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_TFA, 0);
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_TFB1, 0);
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_TFB2, 0);		
	} else if(freq < 1500) {
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_TFA, 1);
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_TFB1, 1);
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_TFB2, 1);
	} else if(freq < 4000) {
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_TFA, 1);
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_TFB1, 0);
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_TFB2, 1);		
	} else {
		// bypass filter
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_TFA, 1);
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_TFB1, 1);
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_TFB2, 0);
	}
}


int16_t lime0_tx_manual_table [][2] = {
	// all PA off, all attenuation off
	{30, 2014},
	{40, 1940},
	{50, 2025},
	{60, 1911},
	{70, 1957},
	{90, 2066},
	{130, 1920},
	{150, 2043},
	{240, 1836},
	{300, 1850},
	{400, 2099},
	{500, 2024},
	{600, 2050},
	{700, 2085},
	{800, 2344},
	{900, 2530},
	{999, 2741},
	{1000, 3862},
	{1100, 4085},
	{1200, 4090},
	{1500, 4161},
	{1800, 4171},
	{2200, 4300},
	{2500, 4285},
	{2800, 4115},
	{3199, 4370},
	{10000, 4370}, // above max freq as last one
};

int lime0_tx_power(struct rfnm_dgb * dgb_dt, int freq, int target) {
	target = target * 100;

	int cur_power = 0;
	int i = 0;
	while(lime0_tx_manual_table[i][0] < freq) {
		cur_power = -lime0_tx_manual_table[i][1];
		i++;
	}

	cur_power = 0;

	if(target > cur_power) {
		//printk("+20");
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_TL1I, 0);
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_TL1O, 1);
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_PA_S1, 0);
		cur_power += 2000;
	} else {
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_TL1I, 1);
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_TL1O, 0);
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_PA_S1, 1);
	}
	
	if(target > cur_power) {
		//printk("+20");
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_TL2I, 0);
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_TL2O, 1);
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_PA_S2, 0);
		cur_power += 2000;
	} else {
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_TL2I, 1);
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_TL2O, 0);
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_PA_S2, 1);
	}
	
	if(target < (cur_power - 2400)) {
		//printk("-24");
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_T24I, 0);
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_T24O, 1);
		cur_power -= 2400;
	} else {
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_T24I, 1);
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_T24O, 0);
	}
	
	if(target < (cur_power - 1200)) {
		//printk("-12");
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_T12I, 0);
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_T12O, 1);
		cur_power -= 1200;
	} else {
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_T12I, 1);
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_T12O, 0);
	}
	
	if(target < (cur_power - 600)) {
		//printk("-6");
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_T6I, 0);
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_T6O, 1);
		cur_power -= 600;
	} else {
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_T6I, 1);
		rfnm_fe_srb(dgb_dt, RFNM_LIME0_T6O, 0);
	}
	
	//printk("target pwr %d vs cur %d", target, cur_power);
	if(target > cur_power) {
		return abs(target - cur_power);
	} else {
		return -abs(target - cur_power);
	}
}


void lime0_ant_tx(struct rfnm_dgb * dgb_dt) {
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_ANT_T, 0);

	rfnm_fe_srb(dgb_dt, RFNM_LIME0_AI1, 0);
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_AI2, 0);

	rfnm_fe_srb(dgb_dt, RFNM_LIME0_ANT_A, 1);
}

/*
void lime0_ant_loopback(struct rfnm_dgb * dgb_dt) {

	rfnm_fe_srb(dgb_dt, RFNM_LIME0_ANT_T, 1);

	rfnm_fe_srb(dgb_dt, RFNM_LIME0_AO1, 1);
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_AO2, 0);

}*/

void lime0_ant_through(struct rfnm_dgb * dgb_dt) {
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_ANT_A, 0);
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_AO1, 0);
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_AO2, 0);
}

void lime0_ant_attn_12(struct rfnm_dgb * dgb_dt) {
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_ANT_A, 1);

	rfnm_fe_srb(dgb_dt, RFNM_LIME0_AI1, 1);
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_AI2, 1);

	rfnm_fe_srb(dgb_dt, RFNM_LIME0_AO1, 1);
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_AO2, 0);
}

void lime0_ant_attn_24(struct rfnm_dgb * dgb_dt) {
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_ANT_A, 1);

	rfnm_fe_srb(dgb_dt, RFNM_LIME0_AI1, 0);
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_AI2, 1);

	rfnm_fe_srb(dgb_dt, RFNM_LIME0_AO1, 1);
	rfnm_fe_srb(dgb_dt, RFNM_LIME0_AO2, 1);
}