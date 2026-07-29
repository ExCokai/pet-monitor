/*
 * Copyright (C) 2026 ArtInChip Technology Co., Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ctrl_serial.h"
#include <rtdevice.h>
#include <rtthread.h>

static rt_device_t g_ctrl_serial = RT_NULL;

int ctrl_serial_init(void)
{
    rt_kprintf("ctrl_serial_init: starting...\n");

    g_ctrl_serial = rt_device_find("uart2");
    if (!g_ctrl_serial)
    {
        rt_kprintf("ctrl_serial_init: find uart2 failed!\n");
        return -1;
    }

    rt_err_t ret = rt_device_open(g_ctrl_serial, RT_DEVICE_OFLAG_RDWR);
    if (ret != RT_EOK)
    {
        rt_kprintf("ctrl_serial_init: open uart2 failed, ret=%d!\n", ret);
        return -1;
    }
    rt_kprintf("ctrl_serial_init: uart2 opened successfully\n");

    ctrl_serial_test();
    return 0;
}

int ctrl_send_switch_state(int device_id, int state)
{
    if (!g_ctrl_serial)
    {
        rt_kprintf("ctrl_send_switch_state: serial not initialized!\n");
        return -1;
    }

    if (device_id < 1 || device_id > 4)
    {
        rt_kprintf("ctrl_send_switch_state: invalid device_id %d\n", device_id);
        return -1;
    }

    char cmd[8];
    rt_snprintf(cmd, sizeof(cmd), "SW%d%d\n", device_id, state ? 1 : 0);

    rt_size_t len = rt_device_write(g_ctrl_serial, 0, cmd, rt_strlen(cmd));

    if (len != rt_strlen(cmd))
    {
        rt_kprintf("ctrl_send_switch_state: write failed!\n");
        return -1;
    }

    rt_kprintf("ctrl_send_switch_state: sent %s", cmd);
    return 0;
}

int ctrl_serial_test(void)
{
    if (!g_ctrl_serial)
    {
        rt_kprintf("ctrl_serial_test: serial not initialized!\n");
        return -1;
    }

    const char *test_cmd = "TEST\n";
    rt_size_t len = rt_device_write(g_ctrl_serial, 0, test_cmd, rt_strlen(test_cmd));

    if (len != rt_strlen(test_cmd))
    {
        rt_kprintf("ctrl_serial_test: write failed!\n");
        return -1;
    }

    rt_kprintf("ctrl_serial_test: sent test command successfully: %s", test_cmd);
    return 0;
}
