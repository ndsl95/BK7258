#ifndef HYPERX_SPEEDOMETER_H
#define HYPERX_SPEEDOMETER_H
#include "lvgl.h"
#ifdef __cplusplus
extern "C" {
#endif
void hyperx_speedometer_create(lv_obj_t *parent);

/* 整数接口：读数按 1 km/h 跳（沿用原接口） */
void hyperx_speedometer_set_speed(int speed_kmh);

/* 浮点接口：指针/光带按亚 km/h 连续走，数字仍按整数跳。
   用来突破“5 秒扫 160 就只能是 32 次/秒”的观感上限。 */
void hyperx_speedometer_set_speed_f(float speed_kmh);

#ifdef __cplusplus
}
#endif
#endif
