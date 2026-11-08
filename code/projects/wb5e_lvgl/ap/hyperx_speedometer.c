/*
 * HyperX 圆屏 UI —— A/B 优化实验版
 *
 * UI_MODE 0：基线（整屏失效）
 * UI_MODE 1：方案A —— 只失效「光带变化扇形 + 指针旧/新包围盒」，光带仍实时绘制
 * UI_MODE 2：方案B —— 光带改成预渲染扇区贴图（40 段 × 4 km/h）切换，只失效指针包围盒
 *
 * 两种方案都保持基线的画面构成：底图 → 光带 → 指针 → 数字。
 */
#include "hyperx_speedometer.h"
#include "hyperx_assets.h"
#include "ui_mode.h"
#if UI_MODE == 2
#include "hyperx_sectors.h"
#endif

#include <math.h>
#include <stdio.h>
#include <stdbool.h>

#define DEG2RAD 0.01745329251994329577f

/* 表盘几何：与基线完全一致 */
#define CX      177
#define CY      170
#define R_OUT   188.0f /* 光带外沿（arc radius 157 + width/2 31） */
#define R_IN    126.0f
#define A_MIN   150.0f
#define KMH_MAX 160
#define ANG_PER_KMH 1.5f

static lv_obj_t *root;
static lv_obj_t *dial;
static lv_obj_t *digits[3];
static int speed = 68;
static float speed_f = 68.0f;
static int shown_kmh = -1;
static float drawn_deg = A_MIN; /* 屏幕上当前画到的角度 */

/* 每一位数字上一次画的状态：只有变化的那一位才重新设 src / 位置 */
static int8_t d_cur[3] = {-1, -1, -1};
static int16_t d_x[3] = {-32768, -32768, -32768};
static bool d_vis[3] = {false, false, false};

#if UI_MODE == 2
static lv_obj_t *sector_obj[HX_SECTOR_COUNT];
static bool sector_on[HX_SECTOR_COUNT];
#endif

static void deleted(lv_event_t *event)
{
    (void)event;
    root = NULL;
    dial = NULL;
}

/* ---------------- 指针 ---------------- */
static void pointer_line(lv_layer_t *layer, int x, int y, float angle,
                         int inner, int outer, int width, uint32_t color, int opa)
{
    float c = cosf(angle), s = sinf(angle);
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.p1.x = x + lroundf(inner * c);
    line.p1.y = y + lroundf(inner * s);
    line.p2.x = x + lroundf(outer * c);
    line.p2.y = y + lroundf(outer * s);
    line.width = width;
    line.color = lv_color_hex(color);
    line.opa = opa;
    line.round_start = line.round_end = 1;
    lv_draw_line(layer, &line);
}

/* ---------------- 局部失效（仅 A/B 用） ---------------- */
#if UI_MODE != 0

static void area_join(lv_area_t *a, const lv_area_t *b)
{
    if (b->x1 > b->x2 || b->y1 > b->y2) return;
    a->x1 = LV_MIN(a->x1, b->x1);
    a->y1 = LV_MIN(a->y1, b->y1);
    a->x2 = LV_MAX(a->x2, b->x2);
    a->y2 = LV_MAX(a->y2, b->y2);
}

/* 圆环扇形 [a0,a1] 的包围盒：1° 采样外沿 + 跨 0/90/180/270 时补极值点 */
static void wedge_area(float a0, float a1, lv_area_t *out)
{
    if (a0 > a1) {
        float t = a0;
        a0 = a1;
        a1 = t;
    }
    if (a1 - a0 < 0.05f) {
        out->x1 = 1;
        out->y1 = 1;
        out->x2 = 0;
        out->y2 = 0;
        return;
    }

    int32_t x1 = 100000, y1 = 100000, x2 = -100000, y2 = -100000;

    const int n = (int)((a1 - a0) / 1.0f) + 2;
    for (int i = 0; i < n; ++i) {
        const float a = a0 + (a1 - a0) * (float)i / (float)(n - 1);
        const float r = a * DEG2RAD;
        const int32_t px = CX + (int32_t)lroundf(R_OUT * cosf(r));
        const int32_t py = CY + (int32_t)lroundf(R_OUT * sinf(r));
        if (px < x1) x1 = px;
        if (px > x2) x2 = px;
        if (py < y1) y1 = py;
        if (py > y2) y2 = py;
    }

    /* 扇形跨过坐标轴方向时，极值就在 0/90/180/270 上 */
    for (int k = 0; k < 4; ++k) {
        for (int wrap = -1; wrap <= 1; ++wrap) {
            const float a = 90.0f * (float)k + 360.0f * (float)wrap;
            if (a < a0 || a > a1) continue;
            const float r = a * DEG2RAD;
            const int32_t px = CX + (int32_t)lroundf(R_OUT * cosf(r));
            const int32_t py = CY + (int32_t)lroundf(R_OUT * sinf(r));
            if (px < x1) x1 = px;
            if (px > x2) x2 = px;
            if (py < y1) y1 = py;
            if (py > y2) y2 = py;
        }
    }

    out->x1 = x1 - 2;
    out->y1 = y1 - 2;
    out->x2 = x2 + 2;
    out->y2 = y2 + 2;
}

/* 指针包围盒（含 14px 笔宽与圆头，内端 96-7、外端 152+7） */
static void pointer_area(float deg, lv_area_t *out)
{
    const float r = deg * DEG2RAD;
    const int32_t ax = CX + (int32_t)lroundf(88.0f * cosf(r));
    const int32_t ay = CY + (int32_t)lroundf(88.0f * sinf(r));
    const int32_t bx = CX + (int32_t)lroundf(161.0f * cosf(r));
    const int32_t by = CY + (int32_t)lroundf(161.0f * sinf(r));

    out->x1 = LV_MIN(ax, bx) - 2;
    out->y1 = LV_MIN(ay, by) - 2;
    out->x2 = LV_MAX(ax, bx) + 2;
    out->y2 = LV_MAX(ay, by) + 2;
}
#endif

/* ---------------- 绘制 ---------------- */
static void draw_dial(lv_event_t *event)
{
    lv_obj_t *obj = lv_event_get_target_obj(event);
    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    lv_layer_t *layer = lv_event_get_layer(event);
    const int cx = area.x1 + CX;
    const int cy = area.y1 + CY;
    const float degrees = A_MIN + ANG_PER_KMH * speed_f;

#if UI_MODE != 2
#if !UI_NO_BAND
    if (speed_f > 0.01f) {
        lv_draw_arc_dsc_t arc;
        lv_draw_arc_dsc_init(&arc);
        arc.center.x = cx;
        arc.center.y = cy;
        arc.radius = 157;
        arc.width = 62;
        arc.start_angle = 150;
        arc.end_angle = degrees;
        arc.img_src = &hyperx_illuminated;
        arc.rounded = 0;
        lv_draw_arc(layer, &arc);
    }
#endif
#endif

#if !UI_NO_POINTER
    const float radians = degrees * DEG2RAD;
    pointer_line(layer, cx, cy, radians, 96, 152, 1, 0xF51A24, 220);
    pointer_line(layer, cx, cy, radians, 115, 151, 14, 0xFF0000, 20);
    pointer_line(layer, cx, cy, radians, 114, 152, 10, 0xFF0000, 45);
    pointer_line(layer, cx, cy, radians, 113, 152, 7, 0xFF1020, 180);
    pointer_line(layer, cx, cy, radians, 113, 151, 4, 0xFF3838, 255);
    pointer_line(layer, cx, cy, radians, 114, 150, 2, 0xFFF9F6, 255);
#else
    (void)cx;
    (void)cy;
    (void)layer;
#endif
}

static void update_digits(int kmh)
{
    if (kmh == shown_kmh) return;
    shown_kmh = kmh;

    char text[4];
    snprintf(text, sizeof(text), "%d", kmh);
    const int count = (kmh >= 100) ? 3 : ((kmh >= 10) ? 2 : 1);
    int width = (count - 1) * 2;
    for (int i = 0; i < count; i++) width += hyperx_digits[text[i] - '0']->header.w;

    int x = CX - width / 2;
    for (int i = 0; i < 3; i++) {
        if (i < count) {
            const int d = text[i] - '0';
            if (!d_vis[i]) {
                lv_obj_remove_flag(digits[i], LV_OBJ_FLAG_HIDDEN);
                d_vis[i] = true;
            }
            if (d_cur[i] != d) {
                lv_image_set_src(digits[i], hyperx_digits[d]);
                d_cur[i] = (int8_t)d;
            }
            if (d_x[i] != x) {
                lv_obj_set_pos(digits[i], x, 157);
                d_x[i] = (int16_t)x;
            }
            x += hyperx_digits[d]->header.w + 2;
        } else if (d_vis[i]) {
            lv_obj_add_flag(digits[i], LV_OBJ_FLAG_HIDDEN);
            d_vis[i] = false;
        }
    }
}

#if UI_MODE == 2
static void update_sectors(int kmh)
{
    int lit = kmh / HX_SECTOR_KMH;
    if (lit > HX_SECTOR_COUNT) lit = HX_SECTOR_COUNT;
    for (int i = 0; i < HX_SECTOR_COUNT; i++) {
        const bool on = (i < lit);
        if (on == sector_on[i]) continue;
        sector_on[i] = on;
        if (on) lv_obj_remove_flag(sector_obj[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(sector_obj[i], LV_OBJ_FLAG_HIDDEN);
    }
}
#endif

/* ---------------- 对外接口 ---------------- */
void hyperx_speedometer_create(lv_obj_t *parent)
{
    if (!parent) parent = lv_screen_active();
    if (root) lv_obj_delete(root);
    root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, 360, 360);
    lv_obj_align(root, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(root, lv_color_hex(0), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(root, deleted, LV_EVENT_DELETE, NULL);

    lv_obj_t *background = lv_image_create(root);
    lv_image_set_src(background, &hyperx_backplate);
    lv_obj_set_pos(background, 0, 0);

#if UI_MODE == 2
    for (int i = 0; i < HX_SECTOR_COUNT; i++) {
        sector_obj[i] = lv_image_create(root);
        lv_image_set_src(sector_obj[i], hx_sectors[i].img);
        lv_obj_set_pos(sector_obj[i], hx_sectors[i].x, hx_sectors[i].y);
        lv_obj_add_flag(sector_obj[i], LV_OBJ_FLAG_HIDDEN);
        sector_on[i] = false;
    }
#endif

    dial = lv_obj_create(root);
    lv_obj_remove_style_all(dial);
    lv_obj_set_size(dial, 360, 360);
    lv_obj_set_pos(dial, 0, 0);
    lv_obj_remove_flag(dial, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(dial, draw_dial, LV_EVENT_DRAW_MAIN, NULL);

    for (int i = 0; i < 3; i++) digits[i] = lv_image_create(root);

    drawn_deg = A_MIN;
    shown_kmh = -1;
    speed = 68;
    speed_f = 68.0f;
    for (int i = 0; i < 3; i++) {
        d_cur[i] = -1;
        d_x[i] = -32768;
        d_vis[i] = false;
    }
    hyperx_speedometer_set_speed(68);
}

void hyperx_speedometer_set_speed_f(float kmh)
{
    if (!root) return;
    if (kmh < 0.0f) kmh = 0.0f;
    if (kmh > (float)KMH_MAX) kmh = (float)KMH_MAX;

    const float deg = A_MIN + ANG_PER_KMH * kmh;
    const int kmh_i = (int)lroundf(kmh);

#if UI_MODE == 0
    speed_f = kmh;
    speed = kmh_i;
#if !UI_NO_DIGITS
    update_digits(kmh_i);
#endif
    lv_obj_invalidate(dial);
#else
    if (fabsf(deg - drawn_deg) > 0.01f) {
        lv_area_t u, t;
        pointer_area(drawn_deg, &u);
        pointer_area(deg, &t);
        area_join(&u, &t);
#if UI_MODE == 1 && !UI_NO_BAND
        wedge_area(drawn_deg, deg, &t);
        area_join(&u, &t);
#endif
        /* 三个变化区并成一个包围盒：LVGL 每多一个失效区就要多走一遍刷新流程 */
        lv_obj_invalidate_area(dial, &u);
        drawn_deg = deg;
    }
    speed_f = kmh;
    speed = kmh_i;
#if !UI_NO_DIGITS
    update_digits(kmh_i);
#endif
#if UI_MODE == 2 && !UI_NO_BAND
    update_sectors(kmh_i);
#endif
#endif
}

void hyperx_speedometer_set_speed(int kmh)
{
    hyperx_speedometer_set_speed_f((float)kmh);
}
