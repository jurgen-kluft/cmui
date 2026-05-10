#include <Arduino.h>
#include <Wire.h>

#include <stdio.h>

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "driver/gpio.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_dev.h"

#include "esp_log.h"
#include "esp_check.h"
#include "esp_compiler.h"

// -------------------------------------------------------------------------------------------------
// -------------------------------------------------------------------------------------------------
// -------------------------------------------------------------------------------------------------

// Note: Comments in English please

// XL9555; The IO expander
#define XL9555_INPUT_PORT0_REG     0
#define XL9555_INPUT_PORT1_REG     1
#define XL9555_OUTPUT_PORT0_REG    2
#define XL9555_OUTPUT_PORT1_REG    3
#define XL9555_INVERSION_PORT0_REG 4
#define XL9555_INVERSION_PORT1_REG 5
#define XL9555_CONFIG_PORT0_REG    6
#define XL9555_CONFIG_PORT1_REG    7

// XL9555 I2C address
#define XL9555_ADDR 0X20

#define XL9555_INPUT_REG  XL9555_INPUT_PORT0_REG
#define XL9555_OUTPUT_REG XL9555_OUTPUT_PORT0_REG
#define XL9555_CONFIG_REG XL9555_CONFIG_PORT0_REG

// XL9555; The IO expander
#define TP_RST_IO  0x0001
#define LCD_RST_IO 0x0002
#define SD_CS_IO   0x0004
#define BL_CTR_IO  0x0008
#define LED1_IO    0x0010
#define IO5        0x0020
#define IO6        0x0040
#define IO7        0x0080
#define IO8        0x0100
#define IO9        0x0200
#define IO10       0x0400
#define IO11       0x0800
#define IO12       0x1000
#define IO13       0x2000
#define IO14       0x4000
#define IO15       0x8000

#define LED1(x)                                                          \
    do                                                                   \
    {                                                                    \
        x ? xl9555_pin_write(LED1_IO, 1) : xl9555_pin_write(LED1_IO, 0); \
    } while (0)

#define LED1_TOGGLE() xl9555_pin_toggle(LED1_IO);

esp_err_t xl9555_init(void);
int16_t   xl9555_pin_read(uint16_t pin);
uint16_t  xl9555_pin_write(uint16_t pin, int val);
esp_err_t xl9555_read_byte(uint8_t reg, uint8_t *data, size_t len);
esp_err_t xl9555_write_byte(uint8_t reg, uint8_t *data, size_t len);
esp_err_t xl9555_pin_toggle(uint16_t pin);

// -------------------------------------------------------------------------------------------------
// Implementations

// Cache the current output state to avoid unnecessary I2C reads
uint16_t g_xl9555_output_cache = 0xFFFF;

esp_err_t xl9555_init(void)
{
    Wire.begin(2, 1);  // SDA: 2, SCL: 1 as per your board

    // Configure pins as per your maker's 0xF003 mask:
    // 0xF003 -> Port 0: 0x03 (bits 0,1 in), Port 1: 0xF0 (bits 4-7 in)
    uint8_t config[] = {0x03, 0xF0};
    return xl9555_write_byte(XL9555_CONFIG_REG, config, 2);
}

uint16_t xl9555_pin_write(uint16_t pin, int val)
{
    if (val)
        g_xl9555_output_cache |= pin;
    else
        g_xl9555_output_cache &= ~pin;

    uint8_t data[2] = {(uint8_t)(g_xl9555_output_cache & 0xFF), (uint8_t)((g_xl9555_output_cache >> 8) & 0xFF)};

    xl9555_write_byte(XL9555_OUTPUT_REG, data, 2);
    return g_xl9555_output_cache;
}

int16_t xl9555_pin_read(uint16_t pin)
{
    uint8_t data[2];
    if (xl9555_read_byte(XL9555_INPUT_REG, data, 2) != ESP_OK)
        return -1;

    uint16_t status = (data[1] << 8) | data[0];
    return (status & pin) ? 1 : 0;
}

esp_err_t xl9555_pin_toggle(uint16_t pin)
{
    bool currentState = (g_xl9555_output_cache & pin);
    xl9555_pin_write(pin, !currentState);
    return ESP_OK;
}

esp_err_t xl9555_write_byte(uint8_t reg, uint8_t *data, size_t len)
{
    Wire.beginTransmission(XL9555_ADDR);
    Wire.write(reg);
    for (size_t i = 0; i < len; i++)
    {
        Wire.write(data[i]);
    }
    if (Wire.endTransmission() == 0)
        return ESP_OK;
    return ESP_FAIL;
}

esp_err_t xl9555_read_byte(uint8_t reg, uint8_t *data, size_t len)
{
    Wire.beginTransmission(XL9555_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);  // Restart
    Wire.requestFrom((uint8_t)XL9555_ADDR, (uint8_t)len);

    size_t i = 0;
    while (Wire.available() && i < len)
    {
        data[i++] = Wire.read();
    }
    return (i == len) ? ESP_OK : ESP_FAIL;
}

// -------------------------------------------------------------------------------------------------

#define st7796_CMD_RAMCTRL            0xb5
#define st7796_DATA_LITTLE_ENDIAN_BIT (1 << 3)

static const char *LCD_PANEL_TAG = "lcd_panel.st7796";

static esp_err_t panel_st7796_del(esp_lcd_panel_t *panel);
static esp_err_t panel_st7796_reset(esp_lcd_panel_t *panel);
static esp_err_t panel_st7796_init(esp_lcd_panel_t *panel);
static esp_err_t panel_st7796_draw_bitmap(esp_lcd_panel_t *panel, int x_start, int y_start, int x_end, int y_end, const void *color_data);
static esp_err_t panel_st7796_invert_color(esp_lcd_panel_t *panel, bool invert_color_data);
static esp_err_t panel_st7796_mirror(esp_lcd_panel_t *panel, bool mirror_x, bool mirror_y);
static esp_err_t panel_st7796_swap_xy(esp_lcd_panel_t *panel, bool swap_axes);
static esp_err_t panel_st7796_set_gap(esp_lcd_panel_t *panel, int x_gap, int y_gap);
static esp_err_t panel_st7796_disp_on_off(esp_lcd_panel_t *panel, bool off);
static esp_err_t panel_st7796_sleep(esp_lcd_panel_t *panel, bool sleep);

struct st7796_panel_t
{
    esp_lcd_panel_t           base;
    esp_lcd_panel_io_handle_t io;
    int                       reset_gpio_num;
    bool                      reset_level;
    int                       x_gap;
    int                       y_gap;
    uint8_t                   fb_bits_per_pixel;
    uint8_t                   madctl_val;  // save current value of LCD_CMD_MADCTL register
    uint8_t                   colmod_val;  // save current value of LCD_CMD_COLMOD register
    uint8_t                   ramctl_val_1;
    uint8_t                   ramctl_val_2;
};

static st7796_panel_t s_st7796_panel = {0};

esp_err_t esp_lcd_new_panel_st7796(const esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *panel_dev_config, esp_lcd_panel_handle_t *ret_panel)
{
    uint8_t fb_bits_per_pixel = 0;

#if CONFIG_LCD_ENABLE_DEBUG_LOG
    esp_log_level_set(LCD_PANEL_TAG, ESP_LOG_DEBUG);
#endif
    esp_err_t       ret    = ESP_OK;
    st7796_panel_t *st7796 = &s_st7796_panel;
    ESP_GOTO_ON_FALSE(io && panel_dev_config && ret_panel, ESP_ERR_INVALID_ARG, err, LCD_PANEL_TAG, "invalid argument");
    ESP_GOTO_ON_FALSE(st7796, ESP_ERR_NO_MEM, err, LCD_PANEL_TAG, "no mem for st7796 panel");

    if (panel_dev_config->reset_gpio_num >= 0)
    {
        gpio_config_t io_conf = {0};
        io_conf.mode          = GPIO_MODE_OUTPUT;
        io_conf.pin_bit_mask  = 1ULL << panel_dev_config->reset_gpio_num;

        ESP_GOTO_ON_ERROR(gpio_config(&io_conf), err, LCD_PANEL_TAG, "configure GPIO for RST line failed");
    }

    switch (panel_dev_config->rgb_ele_order)
    {
        case LCD_RGB_ELEMENT_ORDER_RGB: st7796->madctl_val = 0; break;
        case LCD_RGB_ELEMENT_ORDER_BGR: st7796->madctl_val |= LCD_CMD_BGR_BIT; break;
        default:
            // ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, LCD_PANEL_TAG, "unsupported RGB element order");
            goto err;
            break;
    }

    switch (panel_dev_config->bits_per_pixel)
    {
        case 16:  // RGB565
            st7796->colmod_val = 0x55;
            fb_bits_per_pixel  = 16;
            break;
        case 18:  // RGB666
            st7796->colmod_val = 0x66;
            // each color component (R/G/B) should occupy the 6 high bits of a byte, which means 3 full bytes are required for a pixel
            fb_bits_per_pixel = 24;
            break;
        default: ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, LCD_PANEL_TAG, "unsupported pixel width"); break;
    }

    st7796->ramctl_val_1 = 0x00;
    st7796->ramctl_val_2 = 0xf0;  // Use big endian by default
    if ((panel_dev_config->data_endian) == LCD_RGB_DATA_ENDIAN_LITTLE)
    {
        // Use little endian
        st7796->ramctl_val_2 |= st7796_DATA_LITTLE_ENDIAN_BIT;
    }

    st7796->io                = io;
    st7796->fb_bits_per_pixel = fb_bits_per_pixel;
    st7796->reset_gpio_num    = panel_dev_config->reset_gpio_num;
    st7796->reset_level       = panel_dev_config->flags.reset_active_high;
    st7796->base.del          = panel_st7796_del;
    st7796->base.reset        = panel_st7796_reset;
    st7796->base.init         = panel_st7796_init;
    st7796->base.draw_bitmap  = panel_st7796_draw_bitmap;
    st7796->base.invert_color = panel_st7796_invert_color;
    st7796->base.set_gap      = panel_st7796_set_gap;
    st7796->base.mirror       = panel_st7796_mirror;
    st7796->base.swap_xy      = panel_st7796_swap_xy;
    st7796->base.disp_on_off  = panel_st7796_disp_on_off;
    st7796->base.disp_sleep   = panel_st7796_sleep;
    *ret_panel                = &(st7796->base);
    ESP_LOGD(LCD_PANEL_TAG, "new st7796 panel @%p", st7796);
    return ESP_OK;

err:
    if (st7796)
    {
        if (panel_dev_config->reset_gpio_num >= 0)
        {
            gpio_reset_pin((gpio_num_t)panel_dev_config->reset_gpio_num);
        }
    }
    return ret;
}

static esp_err_t panel_st7796_del(esp_lcd_panel_t *panel)
{
    st7796_panel_t *st7796 = __containerof(panel, st7796_panel_t, base);

    if (st7796->reset_gpio_num >= 0)
    {
        gpio_reset_pin((gpio_num_t)st7796->reset_gpio_num);
    }
    ESP_LOGD(LCD_PANEL_TAG, "del st7796 panel @%p", st7796);
    free(st7796);
    return ESP_OK;
}

static esp_err_t panel_st7796_reset(esp_lcd_panel_t *panel)
{
    st7796_panel_t           *st7796 = __containerof(panel, st7796_panel_t, base);
    esp_lcd_panel_io_handle_t io     = st7796->io;

    // perform hardware reset
    if (st7796->reset_gpio_num >= 0)
    {
        gpio_set_level((gpio_num_t)st7796->reset_gpio_num, st7796->reset_level);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level((gpio_num_t)st7796->reset_gpio_num, !st7796->reset_level);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    else
    {  // perform software reset
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_SWRESET, NULL, 0), LCD_PANEL_TAG, "io tx param failed");
        vTaskDelay(pdMS_TO_TICKS(20));  // spec, wait at least 5m before sending new command  Spec，至少等待5m后再发送新命令
    }

    return ESP_OK;
}

static esp_err_t panel_st7796_init(esp_lcd_panel_t *panel)
{
    st7796_panel_t           *st7796 = __containerof(panel, st7796_panel_t, base);
    esp_lcd_panel_io_handle_t io     = st7796->io;

    // LCD goes into sleep mode and display will be turned off after power on reset, exit sleep mode first
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_SLPOUT, NULL, 0), LCD_PANEL_TAG, "io tx param failed");
    vTaskDelay(pdMS_TO_TICKS(120));
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_MADCTL,
                                                  (uint8_t[]){
                                                    st7796->madctl_val,
                                                  },
                                                  1),
                        LCD_PANEL_TAG, "io tx param failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_COLMOD,
                                                  (uint8_t[]){
                                                    st7796->colmod_val,
                                                  },
                                                  1),
                        LCD_PANEL_TAG, "io tx param failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, st7796_CMD_RAMCTRL, (uint8_t[]){st7796->ramctl_val_1, st7796->ramctl_val_2}, 2), LCD_PANEL_TAG, "io tx param failed");

    return ESP_OK;
}

static esp_err_t panel_st7796_draw_bitmap(esp_lcd_panel_t *panel, int x_start, int y_start, int x_end, int y_end, const void *color_data)
{
    st7796_panel_t           *st7796 = __containerof(panel, st7796_panel_t, base);
    esp_lcd_panel_io_handle_t io     = st7796->io;

    x_start += st7796->x_gap;
    x_end += st7796->x_gap;
    y_start += st7796->y_gap;
    y_end += st7796->y_gap;

    // define an area of frame memory where MCU can access
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_CASET,
                                                  (uint8_t[]){
                                                    (uint8_t)((x_start >> 8) & 0xFF),
                                                    (uint8_t)(x_start & 0xFF),
                                                    (uint8_t)(((x_end - 1) >> 8) & 0xFF),
                                                    (uint8_t)((x_end - 1) & 0xFF),
                                                  },
                                                  4),
                        LCD_PANEL_TAG, "io tx param failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_RASET,
                                                  (uint8_t[]){
                                                    (uint8_t)((y_start >> 8) & 0xFF),
                                                    (uint8_t)(y_start & 0xFF),
                                                    (uint8_t)(((y_end - 1) >> 8) & 0xFF),
                                                    (uint8_t)((y_end - 1) & 0xFF),
                                                  },
                                                  4),
                        LCD_PANEL_TAG, "io tx param failed");
    // transfer frame buffer
    size_t len = (x_end - x_start) * (y_end - y_start) * st7796->fb_bits_per_pixel / 8;
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_color(io, LCD_CMD_RAMWR, color_data, len), LCD_PANEL_TAG, "io tx color failed");

    return ESP_OK;
}

static esp_err_t panel_st7796_invert_color(esp_lcd_panel_t *panel, bool invert_color_data)
{
    st7796_panel_t           *st7796  = __containerof(panel, st7796_panel_t, base);
    esp_lcd_panel_io_handle_t io      = st7796->io;
    int                       command = 0;
    if (invert_color_data)
    {
        command = LCD_CMD_INVON;
    }
    else
    {
        command = LCD_CMD_INVOFF;
    }
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, command, NULL, 0), LCD_PANEL_TAG, "io tx param failed");
    return ESP_OK;
}

static esp_err_t panel_st7796_mirror(esp_lcd_panel_t *panel, bool mirror_x, bool mirror_y)
{
    st7796_panel_t           *st7796 = __containerof(panel, st7796_panel_t, base);
    esp_lcd_panel_io_handle_t io     = st7796->io;
    if (mirror_x)
    {
        st7796->madctl_val |= LCD_CMD_MX_BIT;
    }
    else
    {
        st7796->madctl_val &= ~LCD_CMD_MX_BIT;
    }
    if (mirror_y)
    {
        st7796->madctl_val |= LCD_CMD_MY_BIT;
    }
    else
    {
        st7796->madctl_val &= ~LCD_CMD_MY_BIT;
    }
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_MADCTL, (uint8_t[]){st7796->madctl_val}, 1), LCD_PANEL_TAG, "io tx param failed");
    return ESP_OK;
}

static esp_err_t panel_st7796_swap_xy(esp_lcd_panel_t *panel, bool swap_axes)
{
    st7796_panel_t           *st7796 = __containerof(panel, st7796_panel_t, base);
    esp_lcd_panel_io_handle_t io     = st7796->io;
    if (swap_axes)
    {
        st7796->madctl_val |= LCD_CMD_MV_BIT;
    }
    else
    {
        st7796->madctl_val &= ~LCD_CMD_MV_BIT;
    }
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_MADCTL, (uint8_t[]){st7796->madctl_val}, 1), LCD_PANEL_TAG, "io tx param failed");
    return ESP_OK;
}

static esp_err_t panel_st7796_set_gap(esp_lcd_panel_t *panel, int x_gap, int y_gap)
{
    st7796_panel_t *st7796 = __containerof(panel, st7796_panel_t, base);
    st7796->x_gap          = x_gap;
    st7796->y_gap          = y_gap;
    return ESP_OK;
}

static esp_err_t panel_st7796_disp_on_off(esp_lcd_panel_t *panel, bool on_off)
{
    st7796_panel_t           *st7796  = __containerof(panel, st7796_panel_t, base);
    esp_lcd_panel_io_handle_t io      = st7796->io;
    int                       command = 0;
    if (on_off)
    {
        command = LCD_CMD_DISPON;
    }
    else
    {
        command = LCD_CMD_DISPOFF;
    }
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, command, NULL, 0), LCD_PANEL_TAG, "io tx param failed");
    return ESP_OK;
}

static esp_err_t panel_st7796_sleep(esp_lcd_panel_t *panel, bool sleep)
{
    st7796_panel_t           *st7796  = __containerof(panel, st7796_panel_t, base);
    esp_lcd_panel_io_handle_t io      = st7796->io;
    int                       command = 0;
    if (sleep)
    {
        command = LCD_CMD_SLPIN;
    }
    else
    {
        command = LCD_CMD_SLPOUT;
    }
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, command, NULL, 0), LCD_PANEL_TAG, "io tx param failed");
    vTaskDelay(pdMS_TO_TICKS(100));

    return ESP_OK;
}

// -------------------------------------------------------------------------------------------------
// -------------------------------------------------------------------------------------------------
// -------------------------------------------------------------------------------------------------

// RGB_BL; Control the LCD backlight
#define LCD_BL(x)                                                            \
    do                                                                       \
    {                                                                        \
        x ? xl9555_pin_write(BL_CTR_IO, 1) : xl9555_pin_write(BL_CTR_IO, 0); \
    } while (0)

// RGB_RST; Reset the LCD
#define LCD_RST(x)                                                             \
    do                                                                         \
    {                                                                          \
        x ? xl9555_pin_write(LCD_RST_IO, 1) : xl9555_pin_write(LCD_RST_IO, 0); \
    } while (0)

#define LCD_NUM_CS  GPIO_NUM_39
#define LCD_NUM_DC  GPIO_NUM_38
#define LCD_NUM_RD  GPIO_NUM_14
#define LCD_NUM_WR  GPIO_NUM_45
#define LCD_NUM_RST GPIO_NUM_NC

#define GPIO_LCD_D0  GPIO_NUM_13
#define GPIO_LCD_D1  GPIO_NUM_12
#define GPIO_LCD_D2  GPIO_NUM_11
#define GPIO_LCD_D3  GPIO_NUM_10
#define GPIO_LCD_D4  GPIO_NUM_9
#define GPIO_LCD_D5  GPIO_NUM_46
#define GPIO_LCD_D6  GPIO_NUM_3
#define GPIO_LCD_D7  GPIO_NUM_8
#define GPIO_LCD_D8  GPIO_NUM_18
#define GPIO_LCD_D9  GPIO_NUM_17
#define GPIO_LCD_D10 GPIO_NUM_16
#define GPIO_LCD_D11 GPIO_NUM_15
#define GPIO_LCD_D12 GPIO_NUM_7
#define GPIO_LCD_D13 GPIO_NUM_6
#define GPIO_LCD_D14 GPIO_NUM_5
#define GPIO_LCD_D15 GPIO_NUM_4

#define WHITE   0xFFFF
#define BLACK   0x0000
#define RED     0xF800
#define GREEN   0x07E0
#define BLUE    0x001F
#define MAGENTA 0XF81F
#define YELLOW  0XFFE0
#define CYAN    0X07FF

#define BROWN      0XBC40
#define BRRED      0XFC07
#define GRAY       0X8430
#define DARKBLUE   0X01CF
#define LIGHTBLUE  0X7D7C
#define GRAYBLUE   0X5458
#define LIGHTGREEN 0X841F
#define LGRAY      0XC618
#define LGRAYBLUE  0XA651
#define LBBLUE     0X2B12

/* LCD信息结构体 */
typedef struct _lcd_obj_t
{
    uint16_t width;   /* 宽度 */
    uint16_t height;  /* 高度 */
    uint16_t pwidth;  /* 宽度 */
    uint16_t pheight; /* 高度 */
    uint8_t  dir;     /* 横屏还是竖屏控制：0，竖屏；1，横屏。 */
    uint16_t wramcmd; /* 开始写gram指令 */
    uint16_t setxcmd; /* 设置x坐标指令 */
    uint16_t setycmd; /* 设置y坐标指令 */
    uint16_t wr;      /* 命令/数据IO */
    uint16_t cs;      /* 片选IO */
    uint16_t dc;      /* dc */
    uint16_t rd;      /* rd */
} lcd_obj_t;

typedef struct _lcd_config_t
{
    void                                  *user_ctx;
    esp_lcd_panel_io_color_trans_done_cb_t notify_flush_ready;
} lcd_cfg_t;

void lcd_init(lcd_cfg_t lcd_config);
void lcd_clear(uint16_t color);
void lcd_display_dir(uint8_t dir);
void lcd_fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint16_t color);
void lcd_color_fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint16_t *color);

static const char     *LCD_TAG      = "LCD";
esp_lcd_panel_handle_t panel_handle = NULL; /* LCD句柄 */
uint32_t               g_back_color = 0xFFFF;
lcd_obj_t              lcd_dev;

void lcd_clear(uint16_t color)
{
    uint16_t       y               = 0;
    const uint32_t MAX_BUFFER_SIZE = 65536;
    uint16_t       block_height    = MAX_BUFFER_SIZE / (lcd_dev.width * sizeof(uint16_t));
    if (block_height == 0)
        block_height = 1;

    uint16_t *buffer = (uint16_t *)heap_caps_malloc(lcd_dev.width * block_height * sizeof(uint16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (NULL == buffer)
    {
        ESP_LOGE(TAG, "Memory for bitmap is not enough");
        return;
    }

    for (uint32_t i = 0; i < lcd_dev.width * block_height; i++)
    {
        buffer[i] = color;
    }

    while (y < lcd_dev.height)
    {
        uint16_t current_height = (y + block_height > lcd_dev.height) ? (lcd_dev.height - y) : block_height;
        esp_lcd_panel_draw_bitmap(panel_handle, 0, y, lcd_dev.width, y + current_height, buffer);
        y += current_height;
    }

    heap_caps_free(buffer);
    vTaskDelay(1);
}

void lcd_fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint16_t color)
{
    if (sx >= lcd_dev.width || sy >= lcd_dev.height || ex >= lcd_dev.width || ey >= lcd_dev.height || sx > ex || sy > ey)
    {
        ESP_LOGE(LCD_TAG, "Invalid fill area: sx=%d, sy=%d, ex=%d, ey=%d", sx, sy, ex, ey);
        return;
    }

    uint16_t width  = ex - sx + 1;
    uint16_t height = ey - sy + 1;

    uint16_t *buffer = (uint16_t *)heap_caps_malloc(width * sizeof(uint16_t), MALLOC_CAP_INTERNAL);
    if (NULL == buffer)
    {
        ESP_LOGE(LCD_TAG, "Memory for bitmap is not enough");
        return;
    }

    for (uint16_t i = 0; i < width; i++)
    {
        buffer[i] = color;
    }

    for (uint16_t y = 0; y < height; y++)
    {
        esp_lcd_panel_draw_bitmap(panel_handle, sx, sy + y, ex + 1, sy + y + 1, buffer);
    }

    lcd_draw_point(ex, ey, color);

    heap_caps_free(buffer);
}

/**
 * @brief       在指定区域内填充指定颜色块
 * @note        此函数仅支持uint16_t,RGB565格式的颜色数组填充.
 *              (sx,sy),(ex,ey):填充矩形对角坐标,区域大小为:(ex - sx + 1) * (ey - sy + 1)
 *              注意:sx,ex,不能大于lcd_dev.width - 1; sy,ey,不能大于lcd_dev.height - 1
 * @param       sx,sy:起始坐标
 * @param       ex,ey:结束坐标
 * @param       color:填充的颜色数组首地址
 * @retval      无
 */
void lcd_color_fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint16_t *color)
{
    if (sx >= lcd_dev.width || sy >= lcd_dev.height || ex >= lcd_dev.width || ey >= lcd_dev.height || sx > ex || sy > ey)
    {
        ESP_LOGE("LCD_TAG", "Invalid fill area: sx=%d, sy=%d, ex=%d, ey=%d", sx, sy, ex, ey);
        return;
    }

    uint16_t width        = ex - sx + 1;
    uint16_t height       = ey - sy + 1;
    uint32_t total_pixels = width * height;
    uint32_t buf_index    = 0;

    uint16_t *buffer = (uint16_t *)heap_caps_malloc(width * sizeof(uint16_t), MALLOC_CAP_INTERNAL);
    if (NULL == buffer)
    {
        ESP_LOGE(LCD_TAG, "Memory for bitmap is not enough");
        return;
    }

    for (uint16_t y_index = 0; y_index < height; y_index++)
    {
        /* 填充当前行的颜色数据 */
        for (uint16_t x_index = 0; x_index < width; x_index++)
        {
            if (buf_index < total_pixels)
            {
                buffer[x_index] = color[buf_index];
                buf_index++;
            }
        }

        esp_lcd_panel_draw_bitmap(panel_handle, sx, sy + y_index, ex + 1, sy + y_index + 1, buffer);
    }

    lcd_draw_point(ex, ey, color[total_pixels - 1]);
    heap_caps_free(buffer);
}

void lcd_display_dir(uint8_t dir)
{
    lcd_dev.dir = dir; /* 显示方向 */

    if (lcd_dev.dir == 0) /* 竖屏 */
    {
        lcd_dev.width  = lcd_dev.pwidth;
        lcd_dev.height = lcd_dev.pheight;
        esp_lcd_panel_swap_xy(panel_handle, false);      /* 交换X和Y轴 */
        esp_lcd_panel_mirror(panel_handle, true, false); /* 对屏幕的Y轴不进行镜像处理 */
    }
    else if (lcd_dev.dir == 1) /* 横屏 */
    {
        lcd_dev.width  = lcd_dev.pheight;
        lcd_dev.height = lcd_dev.pwidth;
        esp_lcd_panel_swap_xy(panel_handle, true);        /* 不需要交换X和Y轴 */
        esp_lcd_panel_mirror(panel_handle, false, false); /* 对屏幕的XY轴进行镜像处理 */
    }
}

void lcd_draw_point(uint16_t x, uint16_t y, uint16_t color) { esp_lcd_panel_draw_bitmap(panel_handle, x, y, x + 1, y + 1, (uint16_t *)&color); }

void lcd_init(lcd_cfg_t lcd_config)
{
    gpio_config_t             gpio_init_struct = {0};
    esp_lcd_panel_io_handle_t io_handle        = NULL;

    lcd_dev.wr = LCD_NUM_WR;
    lcd_dev.cs = LCD_NUM_CS;
    lcd_dev.dc = LCD_NUM_DC;
    lcd_dev.rd = LCD_NUM_RD;

    lcd_dev.pwidth  = 320;
    lcd_dev.pheight = 480;

    gpio_init_struct.intr_type    = GPIO_INTR_DISABLE;      /* 失能引脚中断 */
    gpio_init_struct.mode         = GPIO_MODE_INPUT_OUTPUT; /* 配置输出模式 */
    gpio_init_struct.pin_bit_mask = 1ull << lcd_dev.rd;     /* 配置引脚位掩码 */
    gpio_init_struct.pull_down_en = GPIO_PULLDOWN_DISABLE;  /* 失能下拉 */
    gpio_init_struct.pull_up_en   = GPIO_PULLUP_ENABLE;     /* 使能下拉 */
    gpio_config(&gpio_init_struct);                         /* 引脚配置 */
    gpio_set_level((gpio_num_t)lcd_dev.rd, 1);              /* RD管脚拉高 */

    esp_lcd_i80_bus_handle_t i80_bus    = NULL;
    esp_lcd_i80_bus_config_t bus_config = {0};
    bus_config.clk_src                  = LCD_CLK_SRC_DEFAULT;
    bus_config.dc_gpio_num              = lcd_dev.dc;
    bus_config.wr_gpio_num              = lcd_dev.wr;
    bus_config.data_gpio_nums[0]        = GPIO_LCD_D0;
    bus_config.data_gpio_nums[1]        = GPIO_LCD_D1;
    bus_config.data_gpio_nums[2]        = GPIO_LCD_D2;
    bus_config.data_gpio_nums[3]        = GPIO_LCD_D3;
    bus_config.data_gpio_nums[4]        = GPIO_LCD_D4;
    bus_config.data_gpio_nums[5]        = GPIO_LCD_D5;
    bus_config.data_gpio_nums[6]        = GPIO_LCD_D6;
    bus_config.data_gpio_nums[7]        = GPIO_LCD_D7;
    bus_config.data_gpio_nums[8]        = GPIO_LCD_D8;
    bus_config.data_gpio_nums[9]        = GPIO_LCD_D9;
    bus_config.data_gpio_nums[10]       = GPIO_LCD_D10;
    bus_config.data_gpio_nums[11]       = GPIO_LCD_D11;
    bus_config.data_gpio_nums[12]       = GPIO_LCD_D12;
    bus_config.data_gpio_nums[13]       = GPIO_LCD_D13;
    bus_config.data_gpio_nums[14]       = GPIO_LCD_D14;
    bus_config.data_gpio_nums[15]       = GPIO_LCD_D15;
    bus_config.bus_width                = 16;
    bus_config.max_transfer_bytes       = lcd_dev.pwidth * lcd_dev.pheight * sizeof(uint16_t);
    bus_config.psram_trans_align        = 64;
    bus_config.sram_trans_align         = 4;

    ESP_ERROR_CHECK(esp_lcd_new_i80_bus(&bus_config, &i80_bus)); /* 新建80并口总线 */

    esp_lcd_panel_io_i80_config_t io_config = {0};
    io_config.cs_gpio_num                   = lcd_dev.cs;
    io_config.pclk_hz                       = (40 * 1000 * 1000);
    io_config.trans_queue_depth             = 10;
    io_config.dc_levels.dc_idle_level       = 0;
    io_config.dc_levels.dc_cmd_level        = 0;
    io_config.dc_levels.dc_dummy_level      = 0;
    io_config.dc_levels.dc_data_level       = 1;
    io_config.flags.swap_color_bytes        = 0;
    io_config.on_color_trans_done           = lcd_config.notify_flush_ready;
    io_config.user_ctx                      = lcd_config.user_ctx;
    io_config.lcd_cmd_bits                  = 8;
    io_config.lcd_param_bits                = 8;

    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i80(i80_bus, &io_config, &io_handle));

    /* LCD复位 */
    LCD_RST(1);
    vTaskDelay(10);
    LCD_RST(0);
    vTaskDelay(50);
    LCD_RST(1);
    vTaskDelay(200);

    esp_lcd_panel_dev_config_t panel_config = {
      .reset_gpio_num = LCD_NUM_RST,
      .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_BGR,
      .bits_per_pixel = 16,
    };

    ESP_ERROR_CHECK(esp_lcd_new_panel_st7796(io_handle, &panel_config, &panel_handle));

    esp_lcd_panel_reset(panel_handle);              /* 复位屏幕 */
    esp_lcd_panel_init(panel_handle);               /* 初始化屏幕 */
    esp_lcd_panel_invert_color(panel_handle, true); /* 开启颜色反转 */
    esp_lcd_panel_set_gap(panel_handle, 0, 0);      /* 设置XY偏移 */
    esp_lcd_panel_io_tx_param(io_handle, 0x36, (uint8_t[]){0x08}, 1);
    esp_lcd_panel_io_tx_param(io_handle, 0x3A, (uint8_t[]){0x55}, 1);

    lcd_display_dir(0);                                             /* 设置屏幕方向 */
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true)); /* 启动屏幕 */
    lcd_clear(WHITE);                                               /* 默认填充白色 */
    LCD_BL(1);                                                      /* 打开背光 */
}

void setup()
{
    // initialize serial
    Serial.begin(115200);

    // initialize xl9555
    if (xl9555_init() != ESP_OK)
    {
        Serial.println("Failed to initialize XL9555");
        while (1)
            vTaskDelay(1000);
    }

    // initialize LCD
    lcd_cfg_t lcd_config = {
      .user_ctx           = NULL,
      .notify_flush_ready = NULL,
    };
    lcd_init(lcd_config);
}

uint64_t toggle_led_time      = 0;
uint64_t toggle_lcd_fill_time = 0;

void loop()
{
    // Example: Toggle LED1 every second
    if (millis() - toggle_led_time > 1000)
    {
        LED1_TOGGLE();
        toggle_led_time = millis();
    }

    if (millis() - toggle_lcd_fill_time > 2000)
    {
        // Fill a random area with a random color every 2 seconds
        uint16_t color = random(0xFFFF);  // Random RGB565 color
        uint16_t sx    = random(lcd_dev.width);
        uint16_t sy    = random(lcd_dev.height);
        uint16_t ex    = sx + random(10, 50);  // Random width between 10 and 50 pixels
        uint16_t ey    = sy + random(10, 50);  // Random height between 10 and 50 pixels

        // Ensure ex and ey are within bounds
        if (ex >= lcd_dev.width)
            ex = lcd_dev.width - 1;
        if (ey >= lcd_dev.height)
            ey = lcd_dev.height - 1;

        lcd_fill(sx, sy, ex, ey, color);
        toggle_lcd_fill_time = millis();
    }
}
