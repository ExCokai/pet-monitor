# 低功耗优化记录

## 背景

基于 ArtInChip D13x + RT-Thread + LVGL v9 的宠物饲养箱监控系统。SDK 已完整启用 RT-Thread PM 电源管理框架（支持 IDLE/LIGHT/DEEP 三种睡眠模式），但应用层代码没有配合 PM 框架，导致系统无法进入低功耗状态。

## 优化前问题

| 问题 | 影响 |
|------|------|
| 100ms LVGL 定时器持续轮询刷新 UI | CPU 无法进入空闲 |
| UART3 线程 10ms 忙循环 | 阻塞深度睡眠 |
| 屏幕背光常亮，无超时控制 | 不必要功耗 |
| 无数据变化检测 | 即使传感器数据不变也刷新 7 个 label |
| UART3 RX 引脚未配置为唤醒源 | 深度睡眠后无法被传感器唤醒 |

## SDK 已有的低功耗基础设施

- `RT_USING_PM` — PM 框架已编译启用
- IDLE 睡眠（WFI）、LIGHT 睡眠（PLL 关闭 + 24MHz）、DEEP 睡眠（引脚复位 + SRAM 恢复）
- 背光 PWM 控制（通道 2，1KHz）
- LVGL RT-Thread 移植层集成 PM（`lv_rt_thread_port.c`）
- GPIO 引脚唤醒源管理（`pm_pin.c`）

## 优化步骤

### 步骤 1: 数据驱动 UI 刷新 ✅
- **文件**: `src/custom/custom.c`, `src/custom/user_serial.c`, `src/custom/user_serial.h`
- **改动**:
  - 删除 100ms 定时器 → 500ms + `g_data_changed` 标记检查
  - 新增 `rt_mutex_t g_data_mutex` 互斥锁，保护 UART3 线程和 LVGL 线程之间的共享数据
  - `update_ui_display()` 在锁内检查标记并复制数据，锁外执行 LVGL 操作
  - UART3 线程写数据时加锁，置 `g_data_changed = 1`
  - 修复 `user_serial.c` 末尾 `空格没效果` 编译错误
- **效果**: 无数据变化时 `update_ui_display()` 直接返回，不做任何 UI 重绘

### 步骤 2: 屏幕背光超时控制 ✅ (触摸唤醒待修复)
- **文件**: `src/custom/custom.c`
- **改动**:
  - 通过 `rt_device_find("pwm")` → `rt_pwm_set/enable/disable` 控制 PWM 通道 2
  - 新增 `backlight_timeout_cb()` 1s 定时器：30s → 10% 亮度，60s → 关闭 PWM
  - 新增 `backlight_touch_cb()` + `lv_indev_add_event_cb()` 触摸恢复
  - 修复 OFF 超时条件：`g_backlight_state == 1` → `g_backlight_state != -1`
- **已知问题**: 变暗/关闭后触摸不恢复（`lv_indev_add_event_cb` 在 LVGL v9 可能不触发，需改用轮询方式）
- **效果**: 无人操作 60s 后屏幕背光完全关闭

### 步骤 3: UART3 线程休眠优化
- **文件**: `src/custom/user_serial.c`
- **改动**: `rt_mq_recv` 超时从 10ms 改为 500ms，移除循环中的多余 `rt_thread_mdelay`
- **效果**: 无线数据时线程长时间阻塞，PM 框架可以进入深度睡眠

### 步骤 4: 配置 UART3 RX 引脚为唤醒源
- **文件**: `src/custom/user_serial.c`
- **改动**: 调用 `rt_pm_set_pin_wakeup_source()` 标记 UART3 RX 引脚
- **效果**: 深度睡眠后传感器数据能唤醒 MCU

### 步骤 5: 熄屏后暂停 UI 刷新
- **文件**: `src/custom/custom.c`
- **改动**: 熄屏时暂停 LVGL 数据刷新定时器，亮屏时恢复
- **效果**: 熄屏期间系统可以稳定进入深度睡眠

### 步骤 6: 触摸唤醒 + 完整休眠流程 (合并到步骤2修复)
- **文件**: `src/custom/custom.c`
- **改动**: 在 `backlight_timeout_cb` 中用 `lv_indev_get_state()` 轮询触摸状态代替事件回调
- **效果**: 完整的休眠-唤醒闭环

## 最终架构

```
传感器 → UART3 RX (唤醒源)
            ↓
    UART3 线程 (阻塞在 mq_recv, 500ms超时)
            ↓ 数据变化
    data_changed 标记 + 互斥锁
            ↓
    LVGL 定时器 (500ms, 检查标记)
            ↓ 有变化
    更新 UI labels

触摸屏 → 触摸事件回调
            ↓
    重置空闲计时器
            ↓
    恢复背光 + 恢复 UI 刷新

空闲超时 (60s)
            ↓
    关闭背光 → 暂停 UI 刷新 → PM 框架进入深度睡眠
```
