#pragma once

/*
 * A/B 对比开关（只用于 hyperx_opt_ab 实验工程）
 *
 * UI_MODE 0 = 基线：set_speed 里 lv_obj_invalidate(dial) 整屏失效（当前出货逻辑）
 * UI_MODE 1 = 方案A：只失效「光带变化扇形 + 指针旧/新包围盒」
 * UI_MODE 2 = 方案B：光带改为预渲染扇区贴图切换 + 指针包围盒失效
 *
 * UI_REFR_PERIOD：LVGL 刷新周期(ms)。16 = 出货值（封顶约 60fps）；
 *                 2  = 压掉周期限制，用来看真实单帧开销差距。
 *
 * UI_STRESS：1 = 用定时器每 UI_STRESS_PERIOD ms 让读数变 1 km/h。
 *            5 秒扫 160 km/h 时整数速度每 31ms 才变一次，会把帧率假封顶在 32fps；
 *            压力模式让每帧都有变化，才量得到方案的真实渲染上限。
 *
 * UI_SMOOTH：1 = 演示动画用 0.1 km/h 分辨率驱动 set_speed_f()，指针/光带每帧都动；
 *            0 = 老的整数驱动（32 次/秒上限）。
 *
 * UI_NO_POINTER / UI_NO_DIGITS / UI_NO_BAND：拆分定位开销用，正常测试保持 0。
 */
#define UI_MODE 1   /* 方案A：只失效变化区域，帧率更高 */
#define UI_REFR_PERIOD 10
#define UI_STRESS 0
#define UI_STRESS_PERIOD 5
#define UI_SMOOTH 1

#define UI_NO_POINTER 0
#define UI_NO_DIGITS 0
#define UI_NO_BAND 0
