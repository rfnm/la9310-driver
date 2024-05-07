/*
 * lime.h
 *
 *  Created on: Jul 26, 2023
 *      Author: davide
 */

// RFNM_GRANITA0 SCOPE0 HAM0 GRANITALITE0

#ifndef RFNM_LIME0_H_
#define RFNM_LIME0_H_

#define RFNM_LIME0_TX_BAND_LOW 0
#define RFNM_LIME0_TX_BAND_HIGH 1

void lime0_disable_all_pa(struct rfnm_dgb * dgb_dt);
void lime0_disable_all_lna(struct rfnm_dgb * dgb_dt);

void lime0_fm_notch(struct rfnm_dgb * dgb_dt, int en);
void lime0_filter_0_2(struct rfnm_dgb * dgb_dt);
void lime0_filter_2_12(struct rfnm_dgb * dgb_dt);
void lime0_filter_12_30(struct rfnm_dgb * dgb_dt);
void lime0_filter_30_60(struct rfnm_dgb * dgb_dt);
void lime0_filter_60_120(struct rfnm_dgb * dgb_dt);
void lime0_filter_120_250(struct rfnm_dgb * dgb_dt);
void lime0_filter_250_480(struct rfnm_dgb * dgb_dt);
void lime0_filter_480_1000(struct rfnm_dgb * dgb_dt);
void lime0_filter_950_4000(struct rfnm_dgb * dgb_dt);
void lime0_filter_1805_2200(struct rfnm_dgb * dgb_dt);
void lime0_filter_2300_2690(struct rfnm_dgb * dgb_dt);
void lime0_filter_1574_1605(struct rfnm_dgb * dgb_dt) ;

void lime0_filter_1166_1229(struct rfnm_dgb * dgb_dt);
void lime0_loopback(struct rfnm_dgb * dgb_dt);

void lime0_tx_band(struct rfnm_dgb * dgb_dt, int band);
void lime0_tx_lpf(struct rfnm_dgb * dgb_dt, int freq);
int lime0_tx_power(struct rfnm_dgb * dgb_dt, int freq, int target);

void lime0_ant_tx(struct rfnm_dgb * dgb_dt);
void lime0_ant_loopback(struct rfnm_dgb * dgb_dt);
void lime0_ant_through(struct rfnm_dgb * dgb_dt);
void lime0_ant_attn_12(struct rfnm_dgb * dgb_dt);
void lime0_ant_attn_24(struct rfnm_dgb * dgb_dt);






#define RFNM_LIME0_NUM_LATCHES_1 1
#define RFNM_LIME0_NUM_LATCHES_2 2
#define RFNM_LIME0_NUM_LATCHES_3 2
#define RFNM_LIME0_NUM_LATCHES_4 0
#define RFNM_LIME0_NUM_LATCHES_5 0
#define RFNM_LIME0_NUM_LATCHES_6 0

#define RFNM_LIME0_NUM_LATCHES (int[]){0, RFNM_LIME0_NUM_LATCHES_1, RFNM_LIME0_NUM_LATCHES_2, RFNM_LIME0_NUM_LATCHES_3, RFNM_LIME0_NUM_LATCHES_4, RFNM_LIME0_NUM_LATCHES_5, RFNM_LIME0_NUM_LATCHES_6}

#define RFNM_LATCH1 (1 << 28)
#define RFNM_LATCH2 (2 << 28)
#define RFNM_LATCH3 (3 << 28)
#define RFNM_LATCH4 (4 << 28)
#define RFNM_LATCH5 (5 << 28)
#define RFNM_LATCH6 (6 << 28)

#define RFNM_LATCH_SEQ1 (1 << 24)
#define RFNM_LATCH_SEQ2 (2 << 24)

#define RFNM_LATCH_Q0 (0 << 16)
#define RFNM_LATCH_Q1 (1 << 16)
#define RFNM_LATCH_Q2 (2 << 16)
#define RFNM_LATCH_Q3 (3 << 16)
#define RFNM_LATCH_Q4 (4 << 16)
#define RFNM_LATCH_Q5 (5 << 16)
#define RFNM_LATCH_Q6 (6 << 16)
#define RFNM_LATCH_Q7 (7 << 16)

#define RFNM_LO_LIME0_ANT 1
#define RFNM_LO_LIME0_FA 2
#define RFNM_LO_LIME0_TX 3
#define RFNM_LO_LIME0_END 0, 0, 0, 0, 0


#define RFNM_LIME0_ANT_BIAS (RFNM_LATCH1 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q0)
#define RFNM_LIME0_ANT_T 	(RFNM_LATCH1 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q1)
#define RFNM_LIME0_ANT_E 	(RFNM_LATCH1 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q2)
#define RFNM_LIME0_ANT_A 	(RFNM_LATCH1 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q3)
#define RFNM_LIME0_AI1 		(RFNM_LATCH1 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q4)
#define RFNM_LIME0_AI2 		(RFNM_LATCH1 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q5)
#define RFNM_LIME0_AO2 		(RFNM_LATCH1 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q6)
#define RFNM_LIME0_AO1 		(RFNM_LATCH1 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q7)


#define RFNM_LIME0_FA1 		(RFNM_LATCH2 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q3)
#define RFNM_LIME0_FA3 		(RFNM_LATCH2 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q4)
#define RFNM_LIME0_FA2 		(RFNM_LATCH2 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q5)
#define RFNM_LIME0_FA5 		(RFNM_LATCH2 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q6)
#define RFNM_LIME0_FA4 		(RFNM_LATCH2 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q7)

#define RFNM_LIME0_G2PL1 	(RFNM_LATCH2 | RFNM_LATCH_SEQ2 | RFNM_LATCH_Q0)
#define RFNM_LIME0_FA7 		(RFNM_LATCH2 | RFNM_LATCH_SEQ2 | RFNM_LATCH_Q1)
#define RFNM_LIME0_FA6 		(RFNM_LATCH2 | RFNM_LATCH_SEQ2 | RFNM_LATCH_Q2)
#define RFNM_LIME0_FA8 		(RFNM_LATCH2 | RFNM_LATCH_SEQ2 | RFNM_LATCH_Q3)
#define RFNM_LIME0_G1L 		(RFNM_LATCH2 | RFNM_LATCH_SEQ2 | RFNM_LATCH_Q4)
#define RFNM_LIME0_G2L 		(RFNM_LATCH2 | RFNM_LATCH_SEQ2 | RFNM_LATCH_Q5)
#define RFNM_LIME0_LRIM 	(RFNM_LATCH2 | RFNM_LATCH_SEQ2 | RFNM_LATCH_Q6)
#define RFNM_LIME0_G2PL2 	(RFNM_LATCH2 | RFNM_LATCH_SEQ2 | RFNM_LATCH_Q7)


#define RFNM_LIME0_T12O 	(RFNM_LATCH3 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q0)
#define RFNM_LIME0_T12I 	(RFNM_LATCH3 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q1)
#define RFNM_LIME0_T24O 	(RFNM_LATCH3 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q2)
#define RFNM_LIME0_T24I 	(RFNM_LATCH3 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q3)
#define RFNM_LIME0_TFB2 	(RFNM_LATCH3 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q4)
#define RFNM_LIME0_TFB1 	(RFNM_LATCH3 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q5)
#define RFNM_LIME0_TFA		(RFNM_LATCH3 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q6)
#define RFNM_LIME0_TFI	 	(RFNM_LATCH3 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q7)

#define RFNM_LIME0_PA_S2	(RFNM_LATCH3 | RFNM_LATCH_SEQ2 | RFNM_LATCH_Q0)
#define RFNM_LIME0_TL2I 	(RFNM_LATCH3 | RFNM_LATCH_SEQ2 | RFNM_LATCH_Q1)
#define RFNM_LIME0_TL1O 	(RFNM_LATCH3 | RFNM_LATCH_SEQ2 | RFNM_LATCH_Q2)
#define RFNM_LIME0_TL2O 	(RFNM_LATCH3 | RFNM_LATCH_SEQ2 | RFNM_LATCH_Q3)
#define RFNM_LIME0_T6I	 	(RFNM_LATCH3 | RFNM_LATCH_SEQ2 | RFNM_LATCH_Q4)
#define RFNM_LIME0_TL1I 	(RFNM_LATCH3 | RFNM_LATCH_SEQ2 | RFNM_LATCH_Q5)
#define RFNM_LIME0_T6O		(RFNM_LATCH3 | RFNM_LATCH_SEQ2 | RFNM_LATCH_Q6)
#define RFNM_LIME0_PA_S1	(RFNM_LATCH3 | RFNM_LATCH_SEQ2 | RFNM_LATCH_Q7)

#endif /* FE_LIME_H_ */