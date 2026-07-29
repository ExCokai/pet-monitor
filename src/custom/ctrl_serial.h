/*
 * Copyright (C) 2026 ArtInChip Technology Co., Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CTRL_SERIAL_H
#define CTRL_SERIAL_H

#define DEVICE_AC  1
#define DEVICE_UV  2
#define DEVICE_FAN 3
#define DEVICE_DOOR 4

int ctrl_serial_init(void);
int ctrl_send_switch_state(int device_id, int state);
int ctrl_serial_test(void);

#endif /* CTRL_SERIAL_H */
