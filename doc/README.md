# WB5E / BK7258 1.8 寸 QSPI 圆屏

面板为 W180TE010I / ST77916，360×360，RGB565，QSPI0 四数据线、80 MHz。主工程使用 LVGL 9 和 HyperX 扫表动画。

2026-11-08 实屏确认：颜色正常、闪烁消失、动画正常。26 个串口统计窗口的完成发送速率为 72.1–79.0 fps，均值约 75.7 fps；面板内部扫描频率未测量。触摸尚未修复，当前不注册触摸轮询。

## 仓库内容

- `code/projects/wb5e_lvgl`：AP/CP 主工程、图像资源与分区配置。
- `code/patches`：厂家初始化表、LVGL 9 显示端口、QSPI 控制器、LVGL 配置。
- `code/scripts/build.sh`：自动同步工程及补丁、构建、导出固件。
- `code/scripts/flash.ps1`：调用本机 Beken 烧录器。
- `code/scripts/capture_display.py`：采集 COM11 帧率日志。
- [修复与验证报告](display-recovery-20261108/恢复与帧率验证.md)：修改原因、测量边界和恢复方法。
- `display-recovery-20261108/evidence`：不含网络配置的帧率实测记录。

上游 SDK、工具链、烧录器、本地账号、旧实验脚本、备份及本地固件均不上传。原来的其他调研材料仍保存在本机。

## 接线

| 屏幕信号 | WB5E |
|---|---|
| PCLK / SCK | P22 |
| CS | P23 |
| DATA0 | P24 |
| DATA1 | P25 |
| DATA2 | P26 |
| DATA3 | P27 |
| RST | P3 |
| BLC | P2，低电平点亮 |
| VCC / GND | 3.3 V / GND |

## 准备 SDK

本次验证的上游为 `https://gitee.com/bekencorp/bk_avdk_smp.git`，本地分支 `v3.1.1`，提交 `bdbce61eee63ee122da54dc97cb975fc49f01ee1`。按上游说明安装 BK7258 工具链和构建依赖，并准备完整 SDK/子模块；补丁以这个基线为准。

本机采用 Windows + Ubuntu-22.04 WSL2：仓库在 `H:\BK7258`，SDK 在 WSL `/root/bk7258`。SDK 应放在 WSL 的 Linux 文件系统中，以减少构建耗时。

```bash
# 已配置好上游工具链时执行
bash /mnt/h/BK7258/code/scripts/build.sh wb5e_lvgl

# 使用其他 SDK 位置
BK7258_SDK_ROOT=/path/to/bk_avdk_smp bash /path/to/BK7258/code/scripts/build.sh wb5e_lvgl
```

脚本会同步主工程与四份补丁，自动创建输出目录，并将固件输出至本仓库 `firmware/all-app_wb5e_lvgl.bin`；该目录由 Git 忽略。保持 AP 分区的 34 KB 对齐要求，不直接打开未经验证的 DCache。

## Wi-Fi 配置

默认不自动连接 Wi-Fi。需要时复制 `code/projects/wb5e_lvgl/cp/wifi_local.example.h` 为同目录的 `wifi_local.h`，填写自己的 SSID 和密码。这个文件被 Git 忽略，构建脚本会同步到本机 SDK。不要公开包含个人网络账号的固件。

## 烧录与帧率采集

烧录脚本当前使用本机路径 `H:\BK7258\tools\BKFIL\windows\bk_loader\bk_loader.dist\bk_loader.exe`。其他电脑应先安装对应工具，并调整脚本中的工具路径。默认串口为 COM11。

```powershell
& H:\BK7258\code\scripts\flash.ps1 -Bin H:\BK7258\firmware\all-app_wb5e_lvgl.bin -Port COM11

python -m pip install pyserial
python H:\BK7258\code\scripts\capture_display.py --seconds 30 --output H:\BK7258\code\logs\display-check.txt
```

日志中 `render` 为绘制帧率，`sent` 为 QSPI 完成发送帧率，`dma_avg` 为发送后的完成等待时间。用户确认的恢复固件仍保存在原开发电脑，仓库不提供包含本地网络配置的二进制文件。

代码保留原有第三方版权声明；上游 SDK 和图形库的许可按其各自项目执行。
