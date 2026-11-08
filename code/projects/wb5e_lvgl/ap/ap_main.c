#include "bk_private/bk_init.h"
#include <components/system.h>
#include <os/os.h>
#include <modules/pm.h>
#include <driver/pwr_clk.h>
#include <driver/gpio.h>
#include <driver/i2c.h>
#include "gpio_driver.h"
#include "cli.h"
#include "media_service.h"
#include "components/bk_display.h"
#include "frame_buffer.h"
#include "lcd_panel_devices.h"
#include "lvgl.h"
#include "lv_vendor.h"
#include <math.h>

#define TAG "wb5e_lvgl"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

/* 屏幕：ST77916 360x360 QSPI
 * PCLK=P22 DATA0=P24 RST=P3 DATA1=P25 CS=P23 BLC=P2 DATA2=P26 DATA3=P27
 */
#define LCD_RESET_PIN   (GPIO_3)
#define LCD_BL_PIN      (GPIO_2)

/* 触摸：CST816D (I2C0) */
#define TP_I2C_ID       I2C_ID_0
#define TP_ADDR         0x15
#define TP_RST_PIN      GPIO_4

static bk_display_qspi_ctlr_config_t qspi_ctlr_config = {
    .lcd_device = &lcd_device_st77916,
    .qspi_id = 0,
    .reset_pin = LCD_RESET_PIN,
    .te_pin = 0,
};

static void lcd_backlight_on(void)
{
    gpio_dev_unmap(LCD_BL_PIN);
    bk_gpio_enable_output(LCD_BL_PIN);
    bk_gpio_set_output_low(LCD_BL_PIN);
}

/* ================= HyperX 仪表盘 UI（从 S3 工程原样移植） ================= */
#include "hyperx_speedometer.h"

static void demo_anim_cb(void *var, int32_t v)
{
    (void)var;
    hyperx_speedometer_set_speed_f((float)v / 10.0f);   /* 0.1 km/h 分辨率 */
}

static uint32_t s_frames = 0;

void wb5e_fps_tick(void)
{
    s_frames++;
}

static uint32_t s_flush_ms_sum = 0, s_flush_cnt = 0;

void wb5e_flush_stat(uint32_t ms)
{
    s_flush_ms_sum += ms;
    s_flush_cnt++;
}

extern void wb5e_qspi_stats(uint32_t *frames, uint32_t *total_ms, uint32_t *max_ms);

static void fps_timer_cb(lv_timer_t *t)
{
    static uint32_t last_tick;
    static uint32_t last_render, last_sent, last_ms;
    uint32_t now = lv_tick_get(), sent, total_ms, max_ms;
    wb5e_qspi_stats(&sent, &total_ms, &max_ms);
    if (last_tick) {
        uint32_t elapsed = now - last_tick;
        uint32_t n = sent - last_sent;
        LOGI("DISPLAY render=%u.%u fps sent=%u.%u fps dma_avg=%u us dma_max=%u ms\r\n",
             (unsigned)((s_frames - last_render) * 1000 / elapsed),
             (unsigned)((s_frames - last_render) * 10000 / elapsed % 10),
             (unsigned)(n * 1000 / elapsed), (unsigned)(n * 10000 / elapsed % 10),
             (unsigned)(n ? (total_ms - last_ms) * 1000 / n : 0), (unsigned)max_ms);
    }
    last_tick = now; last_render = s_frames; last_sent = sent; last_ms = total_ms;
}

static void build_ui(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x050609), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    hyperx_speedometer_create(scr);

    /* 演示扫表：0 -> 160 -> 0，5 秒 */
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_exec_cb(&a, demo_anim_cb);
    lv_anim_set_values(&a, 0, 1600);
    lv_anim_set_duration(&a, 5000);
    lv_anim_set_playback_duration(&a, 5000);
    lv_anim_set_delay(&a, 500);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_start(&a);

    lv_timer_create(fps_timer_cb, 2000, NULL);
    LOGI("hyperx ui built: tile-encode recovery r1, animation ON\r\n");
}

/* ================= 触摸 ================= */
static void tp_init(void)
{
    gpio_dev_unmap(TP_RST_PIN);
    bk_gpio_enable_output(TP_RST_PIN);
    bk_gpio_set_output_low(TP_RST_PIN);
    rtos_delay_milliseconds(20);
    bk_gpio_set_output_high(TP_RST_PIN);
    rtos_delay_milliseconds(60);

    gpio_dev_unmap(GPIO_20);
    gpio_dev_unmap(GPIO_21);
    bk_gpio_enable_input(GPIO_20);
    bk_gpio_enable_input(GPIO_21);
    bk_gpio_pull_up(GPIO_20);
    bk_gpio_pull_up(GPIO_21);

    i2c_config_t cfg = {0};
    cfg.baud_rate = 100000;
    cfg.addr_mode = I2C_ADDR_MODE_7BIT;
    cfg.slave_addr = TP_ADDR;
    LOGI("tp i2c init ret=%d\r\n", bk_i2c_init(TP_I2C_ID, &cfg));
}

static bool tp_read_regs(uint8_t reg, uint8_t *buf, uint8_t len)
{
    i2c_mem_param_t p = {0};
    p.dev_addr = TP_ADDR;
    p.mem_addr = reg;
    p.mem_addr_size = I2C_MEM_ADDR_SIZE_8BIT;
    p.data = buf;
    p.data_size = len;
    p.timeout_ms = 50;
    return bk_i2c_memory_read(TP_I2C_ID, &p) == BK_OK;
}

/* LVGL 9 的 indev 回调签名 */
static void tp_indev_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    uint8_t b[6] = {0};
    if (!tp_read_regs(0x02, b, 5)) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    uint8_t fingers = b[0] & 0x0F;
    if (fingers) {
        data->point.x = ((b[1] & 0x0F) << 8) | b[2];
        data->point.y = ((b[3] & 0x0F) << 8) | b[4];
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

/* ================= 启动 ================= */
extern void bk_psram_frame_buffer_init(void);

static void wb5e_start(void)
{
    lv_vnd_config_t cfg = {0};

    bk_psram_frame_buffer_init();

    cfg.width  = qspi_ctlr_config.lcd_device->width;
    cfg.height = qspi_ctlr_config.lcd_device->height;
    cfg.render_mode = RENDER_PARTIAL_MODE;
    cfg.rotation = ROTATE_90;

    for (int i = 0; i < CONFIG_LVGL_FRAME_BUFFER_NUM; i++) {
        cfg.frame_buffer[i] = frame_buffer_display_malloc(cfg.width * cfg.height * sizeof(bk_color_t));
        if (!cfg.frame_buffer[i]) {
            LOGE("frame buffer malloc failed\r\n");
            return;
        }
    }

    if (bk_display_qspi_new(&cfg.handle, &qspi_ctlr_config) != AVDK_ERR_OK) {
        LOGE("qspi new failed\r\n");
        return;
    }
    lv_vendor_init(&cfg);
    if (bk_display_open(cfg.handle) != AVDK_ERR_OK) {
        LOGE("display open failed\r\n");
        return;
    }
    lcd_backlight_on();
    LOGI("display opened\r\n");

    LOGI("touch polling disabled during display recovery\r\n");

    lv_vendor_disp_lock();
    build_ui();
    lv_vendor_disp_unlock();

    lv_vendor_start();
    LOGI("lvgl started\r\n");
}

int main(void)
{
    bk_init();
    media_service_init();
    bk_pm_module_vote_psram_ctrl(PM_POWER_PSRAM_MODULE_NAME_LVGL_CODE_RUN, PM_POWER_MODULE_STATE_ON);

    wb5e_start();
    return 0;
}

void wb5e_log_flush_ms(uint32_t ms)
{
    LOGI("flush %u ms\r\n", (unsigned)ms);
}
