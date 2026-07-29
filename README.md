# Pet Monitor - 宠物饲养箱监控系统

基于 ArtInChip D13x 平台 + LVGL + RT-Thread 的宠物饲养箱智能监控系统。

## 功能

- **环境监测**: 温湿度、宠物体温实时显示
- **设备控制**: 空调、紫外线消毒、排气扇、门锁开关
- **状态告警**: 缺水提醒、大便次数统计
- **双串口通信**: UART2 控制指令下发 / UART3 传感器数据采集

## 硬件平台

- 主控: ArtInChip D13x (E907FDP)
- 系统: RT-Thread
- 图形: LVGL v9
- 显示屏: 480×270

## 目录结构

```
src/
├── custom/                # 自定义业务代码
│   ├── custom.c/.h        # 开关事件/定时刷新
│   ├── ctrl_serial.c/.h   # UART2 控制指令发送
│   └── user_serial.c/.h   # UART3 传感器数据接收解析
├── assets/                # 字体/图片资源
├── screen.c               # 数据展示页面
├── start_page.c           # 开关控制页面
├── ui_init.c/.h           # UI 初始化入口
├── ui_objects.h           # UI 对象结构定义
├── ui_util.c/.h           # UI 工具函数
├── lv_conf_custom.h       # LVGL 自定义配置
└── SConscript             # 构建脚本
```

## 构建

本项目为 luban-lite SDK 的应用层代码，需放置在：
```
packages/artinchip/lvgl-ui/aic_demo/ui_builder/
```

## Todo

- [ ] 低功耗处理（屏幕超时熄屏、tickless）
- [ ] UI 数据变化才刷新（减少 100ms 无意义轮询）
- [ ] 线程安全（全局变量加互斥锁）
- [ ] 控制指令 ACK 反馈机制
