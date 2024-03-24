/*
 * lime.h
 *
 *  Created on: Jul 26, 2023
 *      Author: davide
 */

#ifndef RFNM_GRANITA0_H_
#define RFNM_GRANITA0_H_




// fa: frontend-a
// fb: frontend-b

void granita0_fa_fm_notch(struct rfnm_dgb * dgb_dt, int en);
void granita0_fa_filter_0_70(struct rfnm_dgb * dgb_dt);
void granita0_fa_filter_140_225(struct rfnm_dgb * dgb_dt);
void granita0_fa_filter_0_1000(struct rfnm_dgb * dgb_dt);
void granita0_fa_filter_420_700(struct rfnm_dgb * dgb_dt);
void granita0_fa_filter_700_1000(struct rfnm_dgb * dgb_dt);
void granita0_fa_filter_950_3000(struct rfnm_dgb * dgb_dt);
void granita0_fa_filter_1805_2200(struct rfnm_dgb * dgb_dt);
void granita0_fa_filter_2300_2690(struct rfnm_dgb * dgb_dt);
void granita0_fa_filter_1574_1605(struct rfnm_dgb * dgb_dt);
void granita0_fa_filter_1166_1229(struct rfnm_dgb * dgb_dt);
void granita0_filter_5150_7125(struct rfnm_dgb * dgb_dt) ;
void granita0_fa_filter_4900_5850(struct rfnm_dgb * dgb_dt);
void granita0_fa_filter_4400_5000(struct rfnm_dgb * dgb_dt);
void granita0_fa_filter_3200_4200(struct rfnm_dgb * dgb_dt);
void granita0_fa_filter_3000_5000(struct rfnm_dgb * dgb_dt);




void granita0_fb_fm_notch(struct rfnm_dgb * dgb_dt, int en);
void granita0_fb_filter_0_70(struct rfnm_dgb * dgb_dt);
void granita0_fb_filter_140_225(struct rfnm_dgb * dgb_dt);
void granita0_fb_filter_0_1000(struct rfnm_dgb * dgb_dt);
void granita0_fb_filter_420_700(struct rfnm_dgb * dgb_dt);
void granita0_fb_filter_700_1000(struct rfnm_dgb * dgb_dt);
void granita0_fb_filter_950_3000(struct rfnm_dgb * dgb_dt);
void granita0_fb_filter_1805_2200(struct rfnm_dgb * dgb_dt);
void granita0_fb_filter_2300_2690(struct rfnm_dgb * dgb_dt);
void granita0_fb_filter_1574_1605(struct rfnm_dgb * dgb_dt);
void granita0_fb_filter_1166_1229(struct rfnm_dgb * dgb_dt);
void granita0_filter_5150_7125(struct rfnm_dgb * dgb_dt);
void granita0_fb_filter_4900_5850(struct rfnm_dgb * dgb_dt);
void granita0_fb_filter_4400_5000(struct rfnm_dgb * dgb_dt);
void granita0_fb_filter_3200_4200(struct rfnm_dgb * dgb_dt);
void granita0_fb_filter_3000_5000(struct rfnm_dgb * dgb_dt);






void granita0_rffc_rx_1166_1229(struct rfnm_dgb * dgb_dt);


void granita0_ant_a_loopback(struct rfnm_dgb * dgb_dt);
void granita0_ant_a_through(struct rfnm_dgb * dgb_dt);
void granita0_ant_a_embeded_lf(struct rfnm_dgb * dgb_dt);
void granita0_ant_a_embeded_hf(struct rfnm_dgb * dgb_dt);
void granita0_ant_a_attn_12(struct rfnm_dgb * dgb_dt);
void granita0_ant_a_attn_24(struct rfnm_dgb * dgb_dt);
void granita0_ant_a_crossover(struct rfnm_dgb * dgb_dt);
void granita0_ant_a_tx(struct rfnm_dgb * dgb_dt);

void granita0_ant_b_loopback(struct rfnm_dgb * dgb_dt);
void granita0_ant_b_through(struct rfnm_dgb * dgb_dt);
void granita0_ant_b_embeded_lf(struct rfnm_dgb * dgb_dt);
void granita0_ant_b_embeded_hf(struct rfnm_dgb * dgb_dt);
void granita0_ant_b_attn_12(struct rfnm_dgb * dgb_dt);
void granita0_ant_b_attn_24(struct rfnm_dgb * dgb_dt);
void granita0_ant_b_crossover(struct rfnm_dgb * dgb_dt);
void granita0_ant_b_tx(struct rfnm_dgb * dgb_dt);

void granita0_tx_band(struct rfnm_dgb * dgb_dt, int band);
int granita0_tx_power(struct rfnm_dgb * dgb_dt, int freq, int target);



void granita0_tx_freqsel(struct rfnm_dgb * dgb_dt, int freq);

void granita0_fa(struct rfnm_dgb * dgb_dt, int freq);
void granita0_fb(struct rfnm_dgb * dgb_dt, int freq);



#define RFNM_GRANITA0_NUM_LATCHES_1 3
#define RFNM_GRANITA0_NUM_LATCHES_2 2
#define RFNM_GRANITA0_NUM_LATCHES_3 2
#define RFNM_GRANITA0_NUM_LATCHES_4 2
#define RFNM_GRANITA0_NUM_LATCHES_5 2
#define RFNM_GRANITA0_NUM_LATCHES_6 0

#define RFNM_GRANITA0_NUM_LATCHES (int[]){0, RFNM_GRANITA0_NUM_LATCHES_1, RFNM_GRANITA0_NUM_LATCHES_2, RFNM_GRANITA0_NUM_LATCHES_3, RFNM_GRANITA0_NUM_LATCHES_4, RFNM_GRANITA0_NUM_LATCHES_5, RFNM_GRANITA0_NUM_LATCHES_6}

#define RFNM_LATCH1 (1 << 28)
#define RFNM_LATCH2 (2 << 28)
#define RFNM_LATCH3 (3 << 28)
#define RFNM_LATCH4 (4 << 28)
#define RFNM_LATCH5 (5 << 28)
#define RFNM_LATCH6 (6 << 28)

#define RFNM_LATCH_SEQ1 (1 << 24)
#define RFNM_LATCH_SEQ2 (2 << 24)
#define RFNM_LATCH_SEQ3 (3 << 24)

#define RFNM_LATCH_Q0 (0 << 16)
#define RFNM_LATCH_Q1 (1 << 16)
#define RFNM_LATCH_Q2 (2 << 16)
#define RFNM_LATCH_Q3 (3 << 16)
#define RFNM_LATCH_Q4 (4 << 16)
#define RFNM_LATCH_Q5 (5 << 16)
#define RFNM_LATCH_Q6 (6 << 16)
#define RFNM_LATCH_Q7 (7 << 16)


#define RFNM_GRANITA0_ANT_BI1 (RFNM_LATCH1 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q0)
#define RFNM_GRANITA0_ANT_TB (RFNM_LATCH1 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q1)
#define RFNM_GRANITA0_ANT_BI2 (RFNM_LATCH1 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q2)
#define RFNM_GRANITA0_ANT_B (RFNM_LATCH1 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q3)
#define RFNM_GRANITA0_ANT_BO2 (RFNM_LATCH1 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q4)
#define RFNM_GRANITA0_ANT_BO1 (RFNM_LATCH1 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q5)
#define RFNM_GRANITA0_ANT_TS (RFNM_LATCH1 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q6)
#define RFNM_GRANITA0_ANT_TI (RFNM_LATCH1 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q7)

#define RFNM_GRANITA0_ANT_TH (RFNM_LATCH1 | RFNM_LATCH_SEQ2 | RFNM_LATCH_Q0)
#define RFNM_GRANITA0_ANT_H (RFNM_LATCH1 | RFNM_LATCH_SEQ2 | RFNM_LATCH_Q1)
#define RFNM_GRANITA0_ANT_AO2 (RFNM_LATCH1 | RFNM_LATCH_SEQ2 | RFNM_LATCH_Q2)
#define RFNM_GRANITA0_ANT_AO1 (RFNM_LATCH1 | RFNM_LATCH_SEQ2 | RFNM_LATCH_Q3)
#define RFNM_GRANITA0_ANT_AI1 (RFNM_LATCH1 | RFNM_LATCH_SEQ2 | RFNM_LATCH_Q4)
#define RFNM_GRANITA0_ANT_AI2 (RFNM_LATCH1 | RFNM_LATCH_SEQ2 | RFNM_LATCH_Q5)
#define RFNM_GRANITA0_ANT_A (RFNM_LATCH1 | RFNM_LATCH_SEQ2 | RFNM_LATCH_Q6)
#define RFNM_GRANITA0_ANT_TA (RFNM_LATCH1 | RFNM_LATCH_SEQ2 | RFNM_LATCH_Q7)

#define RFNM_GRANITA0_ANT_OAS (RFNM_LATCH1 | RFNM_LATCH_SEQ3 | RFNM_LATCH_Q3)
#define RFNM_GRANITA0_ANT_T (RFNM_LATCH1 | RFNM_LATCH_SEQ3 | RFNM_LATCH_Q4)
#define RFNM_GRANITA0_GR_TX (RFNM_LATCH1 | RFNM_LATCH_SEQ3 | RFNM_LATCH_Q5)
#define RFNM_GRANITA0_GR_RX (RFNM_LATCH1 | RFNM_LATCH_SEQ3 | RFNM_LATCH_Q6)
#define RFNM_GRANITA0_ANT_OAI (RFNM_LATCH1 | RFNM_LATCH_SEQ3 | RFNM_LATCH_Q7)


#define RFNM_GRANITA0_FA_5 (RFNM_LATCH2 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q0)
#define RFNM_GRANITA0_FA_1 (RFNM_LATCH2 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q1)
#define RFNM_GRANITA0_FA_4 (RFNM_LATCH2 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q2)
#define RFNM_GRANITA0_FA_2 (RFNM_LATCH2 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q3)
#define RFNM_GRANITA0_FA_G2PL1 (RFNM_LATCH2 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q4)
#define RFNM_GRANITA0_FA_G2PL2 (RFNM_LATCH2 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q5)
#define RFNM_GRANITA0_FA_3 (RFNM_LATCH2 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q6)
#define RFNM_GRANITA0_FA_G3L (RFNM_LATCH2 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q7)

#define RFNM_GRANITA0_FA_G1BL (RFNM_LATCH2 | RFNM_LATCH_SEQ2 | RFNM_LATCH_Q0)
#define RFNM_GRANITA0_FA_7 (RFNM_LATCH2 | RFNM_LATCH_SEQ2 | RFNM_LATCH_Q1)
#define RFNM_GRANITA0_FA_6 (RFNM_LATCH2 | RFNM_LATCH_SEQ2 | RFNM_LATCH_Q2)
#define RFNM_GRANITA0_FA_G2L (RFNM_LATCH2 | RFNM_LATCH_SEQ2 | RFNM_LATCH_Q3)
#define RFNM_GRANITA0_FA_8 (RFNM_LATCH2 | RFNM_LATCH_SEQ2 | RFNM_LATCH_Q4)
#define RFNM_GRANITA0_FA_G1UL (RFNM_LATCH2 | RFNM_LATCH_SEQ2 | RFNM_LATCH_Q5)
#define RFNM_GRANITA0_FA_9 (RFNM_LATCH2 | RFNM_LATCH_SEQ2 | RFNM_LATCH_Q6)


#define RFNM_GRANITA0_FB_9 (RFNM_LATCH3 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q1)
#define RFNM_GRANITA0_FB_G1BL (RFNM_LATCH3 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q2)
#define RFNM_GRANITA0_FB_G2L (RFNM_LATCH3 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q3)
#define RFNM_GRANITA0_FB_4 (RFNM_LATCH3 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q4)
#define RFNM_GRANITA0_FB_5 (RFNM_LATCH3 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q5)
#define RFNM_GRANITA0_FB_3 (RFNM_LATCH3 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q6)
#define RFNM_GRANITA0_FB_2 (RFNM_LATCH3 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q7)

#define RFNM_GRANITA0_FB_G3L (RFNM_LATCH3 | RFNM_LATCH_SEQ2 | RFNM_LATCH_Q0)
#define RFNM_GRANITA0_FB_1 (RFNM_LATCH3 | RFNM_LATCH_SEQ2 | RFNM_LATCH_Q1)
#define RFNM_GRANITA0_FB_G2PL1 (RFNM_LATCH3 | RFNM_LATCH_SEQ2 | RFNM_LATCH_Q2)
#define RFNM_GRANITA0_FB_7 (RFNM_LATCH3 | RFNM_LATCH_SEQ2 | RFNM_LATCH_Q3)
#define RFNM_GRANITA0_FB_8 (RFNM_LATCH3 | RFNM_LATCH_SEQ2 | RFNM_LATCH_Q4)
#define RFNM_GRANITA0_FB_6 (RFNM_LATCH3 | RFNM_LATCH_SEQ2 | RFNM_LATCH_Q5)
#define RFNM_GRANITA0_FB_G1UL (RFNM_LATCH3 | RFNM_LATCH_SEQ2 | RFNM_LATCH_Q6)
#define RFNM_GRANITA0_FB_G2PL2 (RFNM_LATCH3 | RFNM_LATCH_SEQ2 | RFNM_LATCH_Q7)


#define RFNM_GRANITA0_TX_TF1 (RFNM_LATCH4 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q0)
#define RFNM_GRANITA0_TX_TFB2 (RFNM_LATCH4 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q1)
#define RFNM_GRANITA0_TX_TFB1 (RFNM_LATCH4 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q2)
#define RFNM_GRANITA0_TX_T12I (RFNM_LATCH4 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q3)
#define RFNM_GRANITA0_TX_T24I (RFNM_LATCH4 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q4)
#define RFNM_GRANITA0_TX_TRI (RFNM_LATCH4 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q5)
#define RFNM_GRANITA0_TX_TRO (RFNM_LATCH4 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q6)
#define RFNM_GRANITA0_TX_TF2 (RFNM_LATCH4 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q7)

#define RFNM_GRANITA0_TX_T6O (RFNM_LATCH4 | RFNM_LATCH_SEQ2 | RFNM_LATCH_Q0)
#define RFNM_GRANITA0_TX_T6I (RFNM_LATCH4 | RFNM_LATCH_SEQ2 | RFNM_LATCH_Q1)
#define RFNM_GRANITA0_TX_TL1O (RFNM_LATCH4 | RFNM_LATCH_SEQ2 | RFNM_LATCH_Q2)
#define RFNM_GRANITA0_TX_TL1I (RFNM_LATCH4 | RFNM_LATCH_SEQ2 | RFNM_LATCH_Q3)
#define RFNM_GRANITA0_TX_PA_S1_EN (RFNM_LATCH4 | RFNM_LATCH_SEQ2 | RFNM_LATCH_Q4)
#define RFNM_GRANITA0_TX_TL2I (RFNM_LATCH4 | RFNM_LATCH_SEQ2 | RFNM_LATCH_Q5)
#define RFNM_GRANITA0_TX_TL2O (RFNM_LATCH4 | RFNM_LATCH_SEQ2 | RFNM_LATCH_Q6)
#define RFNM_GRANITA0_TX_PA_S2_EN (RFNM_LATCH4 | RFNM_LATCH_SEQ2 | RFNM_LATCH_Q7)


#define RFNM_GRANITA0_TX_FC_RTI (RFNM_LATCH5 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q1)
#define RFNM_GRANITA0_TX_FC_RFBI2 (RFNM_LATCH5 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q2)
#define RFNM_GRANITA0_TX_FC_RFBI1 (RFNM_LATCH5 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q3)
#define RFNM_GRANITA0_TX_FC_RFBO2 (RFNM_LATCH5 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q4)
#define RFNM_GRANITA0_TX_FC_RFBO1 (RFNM_LATCH5 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q5)
#define RFNM_GRANITA0_TX_FC_RTB (RFNM_LATCH5 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q6)
#define RFNM_GRANITA0_TX_FC_RAI (RFNM_LATCH5 | RFNM_LATCH_SEQ1 | RFNM_LATCH_Q7)

#define RFNM_GRANITA0_TX_FC_RFAO1 (RFNM_LATCH5 | RFNM_LATCH_SEQ2 | RFNM_LATCH_Q1)
#define RFNM_GRANITA0_TX_FC_RFAI2 (RFNM_LATCH5 | RFNM_LATCH_SEQ2 | RFNM_LATCH_Q2)
#define RFNM_GRANITA0_TX_FC_RFAI1 (RFNM_LATCH5 | RFNM_LATCH_SEQ2 | RFNM_LATCH_Q3)
#define RFNM_GRANITA0_TX_FC_RFAO2 (RFNM_LATCH5 | RFNM_LATCH_SEQ2 | RFNM_LATCH_Q4)
#define RFNM_GRANITA0_TX_FC_RBI (RFNM_LATCH5 | RFNM_LATCH_SEQ2 | RFNM_LATCH_Q5)
#define RFNM_GRANITA0_TX_FC_RTA (RFNM_LATCH5 | RFNM_LATCH_SEQ2 | RFNM_LATCH_Q6)
#define RFNM_GRANITA0_TX_FC_RTO (RFNM_LATCH5 | RFNM_LATCH_SEQ2 | RFNM_LATCH_Q7)


#endif 