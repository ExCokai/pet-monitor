/*
 * Copyright (C) 2026 ArtInChip Technology Co., Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "user_serial.h"
#include "ui_objects.h"
#include "aic_ui.h"
#include <rtthread.h>
#include <rtdevice.h>
#include <drivers/pm.h>

static rt_mq_t g_rx_mq;
rt_device_t g_serial;
static rt_thread_t g_uart3_thread;

/* 互斥锁：保护传感器全局数据的线程安全访问 */
rt_mutex_t g_data_mutex = RT_NULL;

#define RX_BUF_SIZE 128
static char g_rx_buf[RX_BUF_SIZE];
static int g_rx_idx = 0;

static int g_env_temperature = 0;
static float g_pet_temperature = 0.0f;
static int g_humidity = 0;
static int g_water_status = 0;
static int g_feces_count = 0;
volatile int g_data_valid = 0;
volatile int g_data_changed = 0;

static rt_err_t serial_input_mq(rt_device_t dev, rt_size_t size);
static int parse_temperature_humidity(char *data, int *t1, float *t2, int *h, int *w, int *f);
static void uart3_thread_entry(void *parameter);

int uart3_serial_init(void)
{
    rt_kprintf("uart3_serial_init: starting...\n");

    // 初始化全局变量
    g_rx_idx = 0;
    g_rx_buf[0] = '\0';
    g_env_temperature = 0;
    g_pet_temperature = 0;
    g_humidity = 0;
    g_water_status = 0;
    g_feces_count = 0;
    g_data_valid = 0;
    g_data_changed = 0;

    /* 创建互斥锁保护传感器数据（LVGL线程 vs UART3线程） */
    g_data_mutex = rt_mutex_create("data_mtx", RT_IPC_FLAG_PRIO);
    if (!g_data_mutex)
    {
        rt_kprintf("uart3_serial_init: mutex create failed!\n");
        return -1;
    }

    g_rx_mq = rt_mq_create("uart3_rx_mq", sizeof(struct rx_msg), UART3_MQ_SIZE, RT_IPC_FLAG_FIFO);
    if (!g_rx_mq)
    {
        rt_kprintf("uart3_serial_init: rt_mq_create failed!\n");
        return -1;
    }
    rt_kprintf("uart3_serial_init: message queue created\n");

    g_serial = rt_device_find("uart3");
    if (!g_serial)
    {
        rt_kprintf("uart3_serial_init: find uart3 failed!\n");
        return -1;
    }

    rt_kprintf("uart3_serial_init: Found uart3, device pointer: %p\n", g_serial);
    rt_kprintf("uart3_serial_init: Device ref_count: %d\n", g_serial->ref_count);
    rt_kprintf("uart3_serial_init: Device open_flag: 0x%x\n", g_serial->open_flag);

    rt_kprintf("uart3_serial_init: UART3 will use default configuration\n");

    rt_err_t ret = rt_device_open(g_serial, RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_INT_RX);
    if (ret != RT_EOK)
    {
        rt_kprintf("uart3_serial_init: open uart3 failed, ret=%d!\n", ret);
        return -1;
    }
    rt_kprintf("uart3_serial_init: uart3 opened successfully\n");

    rt_device_set_rx_indicate(g_serial, serial_input_mq);//设置接收回调函数
    rt_kprintf("uart3_serial_init: rx indicate callback set\n");

    /* 将 UART3 RX 引脚(PA.7) 标记为唤醒源，
     * 确保深度睡眠时传感器数据到来能唤醒 MCU */
    rt_base_t uart3_rx_pin = rt_pin_get("PA.7");
    if (uart3_rx_pin >= 0) {
        rt_pm_set_pin_wakeup_source(uart3_rx_pin);
        rt_kprintf("uart3_serial_init: PA.7 set as wakeup source\n");
    }

    g_uart3_thread = rt_thread_create("uart3_recv",
                                      uart3_thread_entry,
                                      RT_NULL,
                                      4096, // 增大栈空间
                                      25,
                                      10);
                                      
    if (g_uart3_thread != RT_NULL)
    {
        rt_thread_startup(g_uart3_thread);
        rt_kprintf("uart3_serial_init: thread started successfully\n");
    }
    else
    {
        rt_kprintf("uart3_serial_init: thread create failed!\n");
        return -1;
    }

    rt_kprintf("uart3_serial_init: completed successfully\n");

    // 测试发送
    const char *test_cmd = "TEST\n";
    rt_size_t len = rt_device_write(g_serial, 0, test_cmd, rt_strlen(test_cmd));
    if (len == rt_strlen(test_cmd))
    {
        rt_kprintf("uart3_serial_init: sent test command: %s", test_cmd);
    }
    else
    {
        rt_kprintf("uart3_serial_init: test send failed!\n");
    }

    return 0;
}

static rt_err_t serial_input_mq(rt_device_t dev, rt_size_t size)
{
    struct rx_msg msg = {0};
    rt_err_t result = 0;
    msg.dev = dev;
    msg.size = size;

    result = rt_mq_send(g_rx_mq, &msg, sizeof(msg));
    if (result == -RT_EFULL)
    {
        printf("message queue full!\n");
    }

    return result;
}

static void uart3_thread_entry(void *parameter)
{
    struct rx_msg msg;
    unsigned char data[64];
    rt_err_t ret;

    rt_kprintf("uart3_thread_entry: thread running...\n");

    while (1)
    {
        // 阻塞等待消息，超时 500ms（允许 PM 框架在此期间进入睡眠）
        ret = rt_mq_recv(g_rx_mq, &msg, sizeof(msg), 500);
        if (ret == RT_EOK)
        {
            // 检查消息队列中的设备指针是否有效
            if (msg.dev == RT_NULL)
            {
                rt_kprintf("uart3_thread_entry: msg.dev is NULL!\n");
                rt_thread_mdelay(10);
                continue;
            }

            int len = rt_device_read(msg.dev, 0, data, sizeof(data) - 1);
            if (len > 0)
            {
                // 确保数据以 '\0' 结尾
                data[len] = '\0';

                // 累积数据到缓冲区（过滤非打印字符）
                for (int i = 0; i < len; i++)
                {
                    char ch = data[i];
                    // 只保留可打印字符和空格
                    if ((ch >= 32 && ch <= 126) || ch == '\t' || ch == '\n' || ch == '\r')
                    {
                        if (g_rx_idx < RX_BUF_SIZE - 1)
                        {
                            g_rx_buf[g_rx_idx++] = ch;
                        }
                    }
                }
                g_rx_buf[g_rx_idx] = '\0';

                // 尝试解析新格式 T1=25,T2=39.5,H=55,W=1,F=3
                int t1 = 0, h = 0, w = 0, f = 0;
                float t2 = 0.0f;
                if (parse_temperature_humidity(g_rx_buf, &t1, &t2, &h, &w, &f) == 0)
                {
                    /* ---- 临界区：更新全局传感器数据 ---- */
                    rt_mutex_take(g_data_mutex, RT_WAITING_FOREVER);
                    g_env_temperature = t1;
                    g_pet_temperature = t2;
                    g_humidity = h;
                    g_water_status = w;
                    g_feces_count = f;
                    g_data_valid = 1;
                    g_data_changed = 1;  // 通知 UI 有新数据
                    rt_mutex_release(g_data_mutex);
                    /* ----------------------------------- */
                    // 清空缓冲区
                    g_rx_idx = 0;
                    g_rx_buf[0] = '\0';
                    int pet_temp_int = (int)(g_pet_temperature * 10);
                    rt_kprintf("uart3_thread_entry: parsed T1=%d, T2=%d.%d, H=%d, W=%d, F=%d\n",
                               g_env_temperature, pet_temp_int / 10, pet_temp_int % 10, g_humidity, g_water_status, g_feces_count);
                }
            }
            else if (len < 0)
            {
                rt_kprintf("uart3_thread_entry: rt_device_read failed, ret=%d\n", len);
            }
        }
        else if (ret != -RT_ETIMEOUT)
        {
            rt_kprintf("uart3_thread_entry: rt_mq_recv error, ret=%d\n", ret);
        }

        // 不额外延时：mq_recv 已经阻塞等待，超时后直接下一轮
    }
}

static int parse_temperature_humidity(char *data, int *t1, float *t2, int *h, int *w, int *f)
{
    char *t1_start, *t1_end;
    char *t2_start, *t2_end;
    char *h_start;
    char *w_start;
    char *f_start;

    // 解析 T1（环境温度）
    t1_start = strstr(data, "T1=");
    if (!t1_start)
    {
        return -1;
    }
    t1_start += 3;

    t1_end = strchr(t1_start, ',');
    if (!t1_end)
    {
        t1_end = strchr(t1_start, '\0');
    }

    char t1_buf[16];
    int len = t1_end - t1_start;
    if (len > sizeof(t1_buf) - 1)
        len = sizeof(t1_buf) - 1;
    strncpy(t1_buf, t1_start, len);
    t1_buf[len] = '\0';
    *t1 = atoi(t1_buf);

    // 解析 T2（宠物体温）
    t2_start = strstr(data, "T2=");
    if (!t2_start)
    {
        return -1;
    }
    t2_start += 3;

    t2_end = strchr(t2_start, ',');
    if (!t2_end)
    {
        t2_end = strchr(t2_start, '\0');
    }

    char t2_buf[16];
    len = t2_end - t2_start;
    if (len > sizeof(t2_buf) - 1)
        len = sizeof(t2_buf) - 1;
    strncpy(t2_buf, t2_start, len);
    t2_buf[len] = '\0';
    *t2 = atof(t2_buf); // 使用 atof 支持小数

    // 解析 H（湿度）
    h_start = strstr(data, "H=");
    if (!h_start)
    {
        return -1;
    }
    h_start += 2;
    
    char h_buf[16];
    char *h_end = strchr(h_start, ',');
    if (h_end) {
        len = h_end - h_start;
        if (len > sizeof(h_buf) - 1) len = sizeof(h_buf) - 1;
        strncpy(h_buf, h_start, len);
        h_buf[len] = '\0';
        *h = atoi(h_buf);
    } else {
        *h = atoi(h_start);
    }

    // 解析 W（水量状态）- 可选字段
    *w = 0; // 默认缺水
    w_start = strstr(data, "W=");
    if (w_start)
    {
        w_start += 2;
        char *w_end = strchr(w_start, ',');
        if (w_end) {
            len = w_end - w_start;
            if (len > sizeof(h_buf) - 1) len = sizeof(h_buf) - 1;
            strncpy(h_buf, w_start, len);
            h_buf[len] = '\0';
            *w = atoi(h_buf);
        } else {
            *w = atoi(w_start);
        }
    }

    // 解析 F（大便次数）- 可选字段
    *f = 0; // 默认0次
    f_start = strstr(data, "F=");
    if (f_start)
    {
        f_start += 2;
        *f = atoi(f_start);
    }

    return 0;
}

void update_ui_display(void)
{
    if (!g_data_mutex) return;

    /* ---- 临界区：检查 + 读取传感器数据 ---- */
    rt_mutex_take(g_data_mutex, RT_WAITING_FOREVER);

    if (!g_data_valid || !g_data_changed) {
        rt_mutex_release(g_data_mutex);
        return;  // 数据没变化，跳过刷新
    }

    // 复制数据到局部变量（避免长时间持锁）
    int t1 = g_env_temperature;
    float t2 = g_pet_temperature;
    int h = g_humidity;
    int w = g_water_status;
    int f = g_feces_count;
    g_data_changed = 0;
    rt_mutex_release(g_data_mutex);
    /* ------------------------------------------- */

    // ↓↓↓ 以下在锁外执行 LVGL UI 操作 ↓↓↓

    lv_obj_t *active_screen = lv_scr_act();

    if (active_screen == ui_manager.screen.obj) {
        char temp_str[32];
        char hum_str[32];
        char combined[64];

        if (ui_manager.screen.label_1) {
            snprintf(combined, sizeof(combined), "温湿度：%d℃/%d%%", t1, h);
            lv_label_set_text(ui_manager.screen.label_1, combined);
        }
        if (ui_manager.screen.label_2) {
            snprintf(temp_str, sizeof(temp_str), "%d/", t1);
            lv_label_set_text(ui_manager.screen.label_2, temp_str);
        }
        if (ui_manager.screen.label_3) {
            snprintf(hum_str, sizeof(hum_str), "%d%%", h);
            lv_label_set_text(ui_manager.screen.label_3, hum_str);
        }
        if (ui_manager.screen.label_4) {
            snprintf(temp_str, sizeof(temp_str), "宠物体温：%d.%d℃",
                     (int)t2, (int)(t2 * 10) % 10);
            lv_label_set_text(ui_manager.screen.label_4, temp_str);
        }
        if (ui_manager.screen.label_5) {
            snprintf(temp_str, sizeof(temp_str), "%d.%d℃",
                     (int)t2, (int)(t2 * 10) % 10);
            lv_label_set_text(ui_manager.screen.label_5, temp_str);
        }
        if (ui_manager.screen.label_6) {
            const char *water_text = w ? "      水量充足" : "缺水！请及时加水";
            lv_label_set_text(ui_manager.screen.label_6, water_text);
        }
        if (ui_manager.screen.label_7) {
            snprintf(temp_str, sizeof(temp_str), "大便次数：%d", f);
            lv_label_set_text(ui_manager.screen.label_7, temp_str);
        }
    }
}

void update_temperature_humidity(float temperature, int humidity)
{
    if (!g_data_mutex) return;
    // 兼容旧接口，带互斥保护
    rt_mutex_take(g_data_mutex, RT_WAITING_FOREVER);
    g_env_temperature = (int)temperature;
    g_humidity = humidity;
    g_data_valid = 1;
    g_data_changed = 1;
    rt_mutex_release(g_data_mutex);
}