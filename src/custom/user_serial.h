/*
 * Copyright (C) 2026 ArtInChip Technology Co., Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef USER_SERIAL_H
#define USER_SERIAL_H

#include <rtthread.h>

#define UART3_MQ_SIZE 128

struct rx_msg
{
    rt_device_t dev;
    rt_size_t size;
};

/* 传感器共享数据（线程安全访问） */
extern rt_mutex_t g_data_mutex;
extern volatile int g_data_changed;
extern volatile int g_data_valid;

int uart3_serial_init(void);
void update_temperature_humidity(float temperature, int humidity);
void update_ui_display(void);
rt_device_t get_uart3_device(void);

#endif /* USER_SERIAL_H */
