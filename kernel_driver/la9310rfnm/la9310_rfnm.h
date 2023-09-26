/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0)
 * Copyright 2017, 2021-2022 NXP
 */

#ifndef __LA9310_RFNM_H__
#define __LA9310_RFNM_H__

#define LA9310_DEVICE_NAME_MAX_LEN	(12)
extern void ipc_test_usecase(const char *name);
extern volatile uint64_t num_of_bytes_tx;
extern volatile uint64_t num_of_bytes_rx;
#endif
