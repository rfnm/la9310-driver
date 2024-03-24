#ifndef RFNM_FE_GENERIC_H_
#define RFNM_FE_GENERIC_H_

#include <linux/rfnm-shared.h>

int rfnm_fe_generic_init(struct rfnm_dgb * dgb_dt, int * num_latches);

void rfnm_fe_manual_clock(int dgb_id, int id);

void rfnm_fe_load_data_bit(int dgb_id, int latch, int bit);

void rfnm_fe_srb(struct rfnm_dgb * dgb_dt, int bit, int val);

void rfnm_fe_load_latch(struct rfnm_dgb * dgb_dt, int id);

void rfnm_fe_load_latches(struct rfnm_dgb * dgb_dt);

void rfnm_fe_trigger_latches(struct rfnm_dgb * dgb_dt);

void rfnm_fe_load_order(struct rfnm_dgb * dgb_dt, int l0, int l1, int l2, int l3, int l4, int l5, int l6, int l7);

#endif