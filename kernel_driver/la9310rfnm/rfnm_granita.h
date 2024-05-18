/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0)
 * Copyright 2017, 2021-2022 NXP
 */

#ifndef __LA9310_RFNM_GRANITA_H__
#define __LA9310_RFNM_GRANITA_H__

#define RFNM_RFFC2071_REGS_NUM 31
#define RFNM_RFFC2071_REF_FREQ 30720000
#define RFNM_RFFC2071_LO_MAX 5400000000

extern void rfnm_rffc_init(struct rfnm_dgb *);
extern void rfnm_rffc_deinit(struct rfnm_dgb *);
extern void rfnm_rffc_disable(struct rfnm_dgb *);
extern void rfnm_rffc_enable(struct rfnm_dgb *);
extern uint64_t rfnm_rffc_set_freq(struct rfnm_dgb *, uint64_t freq_hz);


#endif
