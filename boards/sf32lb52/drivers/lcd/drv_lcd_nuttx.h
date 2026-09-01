/*
 * NuttX adaptation primitives for the original SiFli LCD driver.
 *
 * The LCD implementation is intentionally kept in its original shape.  This
 * header only retains the data types and constants needed by that layout.  All
 * operating-system operations are performed directly with NuttX APIs at their
 * call sites.
 */

#ifndef __DRV_LCD_NUTTX_H__
#define __DRV_LCD_NUTTX_H__

#include <nuttx/config.h>
#include <nuttx/arch.h>
#include <nuttx/event.h>
#include <nuttx/fs/fs.h>
#include <nuttx/fs/ioctl.h>
#include <nuttx/kthread.h>
#include <nuttx/irq.h>
#include <nuttx/lcd/lcd.h>
#include <nuttx/lcd/lcd_dev.h>
#include <nuttx/nuttx.h>
#include <nuttx/power/pm.h>
#include <nuttx/semaphore.h>
#include <nuttx/spinlock.h>
#include <nuttx/timers/pwm.h>
#include <nuttx/video/fb.h>

#include <debug.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <syslog.h>

#include "bf0_hal.h"
#include "bf0_hal_lcdc.h"

#ifndef FAR
#  define FAR
#endif

#ifndef LCD_HOR_RES_MAX
#  define LCD_HOR_RES_MAX CONFIG_LCD_HOR_RES_MAX
#endif
#ifndef LCD_VER_RES_MAX
#  define LCD_VER_RES_MAX CONFIG_LCD_VER_RES_MAX
#endif
#ifndef NX_IRQ
#  define NX_IRQ(irqn) ((irqn) + 16)
#endif

#if defined(CONFIG_BSP_USING_LCD_FRAMEBUFFER) && !defined(BSP_USING_LCD_FRAMEBUFFER)
#  define BSP_USING_LCD_FRAMEBUFFER 1
#endif
#if defined(CONFIG_BSP_USING_LCDC) && !defined(BSP_USING_LCDC)
#  define BSP_USING_LCDC 1
#endif
#if defined(CONFIG_LCD_USING_PWM_AS_BACKLIGHT) && defined(CONFIG_PWM) && \
    !defined(LCD_USING_PWM_AS_BACKLIGHT)
#  define LCD_USING_PWM_AS_BACKLIGHT 1
#endif
#if defined(CONFIG_BSP_USING_EPIC) && defined(CONFIG_LV_USE_SIFLI_EPIC) && \
    !defined(BSP_USING_EPIC)
#  define BSP_USING_EPIC 1
#endif
#ifndef LCDC_DPI_MAX_WIDTH
#  define LCDC_DPI_MAX_WIDTH 0
#endif

#ifndef RT_NULL
#  define RT_NULL NULL
#endif
#define RT_EOK       0
#define RT_ERROR     EIO
#define RT_ETIMEOUT  ETIMEDOUT
#define RT_EBUSY     EBUSY
#define RT_EEMPTY    ENODATA
#define RT_WAITING_FOREVER UINT32_MAX
#define RT_TICK_PER_SECOND (1000000 / CONFIG_USEC_PER_TICK)
#define RT_ALIGN_SIZE 4
#define RT_ALIGN(n, a) (((n) + ((a) - 1)) & ~((a) - 1))
#define ALIGN(n) __attribute__((aligned(n)))
#define L1_NON_RET_BSS_SECT_BEGIN(name)
#define L1_NON_RET_BSS_SECT(name, decl) decl
#define L1_NON_RET_BSS_SECT_END
#define L2_NON_RET_BSS_SECT_BEGIN(name)
#define L2_NON_RET_BSS_SECT(name, decl) decl
#define L2_NON_RET_BSS_SECT_END
#define RETM_BSS_SECT_BEGIN(name)
#define RETM_BSS_SECT_END
#define L1_RET_CODE_SECT(name, decl) decl
#define RT_ASSERT DEBUGASSERT
#define RT_USED __attribute__((used))
#define RT_DEVICE_FLAG_RDWR 0
#define RT_DEVICE_FLAG_STANDALONE 0
#define RT_DEVICE_OFLAG_RDWR 0
#define RT_DEVICE_OFLAG_RDONLY 0
#define RT_Device_Class_Char 0
#define RT_Device_Class_Graphic 0
#define RT_THREAD_PRIORITY_HIGH 100
#define RT_THREAD_TICK_DEFAULT 1
#define RT_IPC_FLAG_FIFO 0
#define RT_IPC_CMD_RESET 0
#define RT_EVENT_FLAG_OR 0
#define RT_EVENT_FLAG_AND NXEVENT_WAIT_ALL
#define RT_EVENT_FLAG_CLEAR (1 << 4)
#define RT_DEVICE_CTRL_SUSPEND _LCDIOC(60)
#define RT_DEVICE_CTRL_RESUME  _LCDIOC(61)

#define LOG_D(...) syslog(LOG_DEBUG, __VA_ARGS__)
#define LOG_I(...) syslog(LOG_INFO, __VA_ARGS__)
#define LOG_W(...) syslog(LOG_WARNING, __VA_ARGS__)
#define LOG_E(...) syslog(LOG_ERR, __VA_ARGS__)

#define RTGRAPHIC_PIXEL_FORMAT_MONO    LCDC_PIXEL_FORMAT_MONO
#define RTGRAPHIC_PIXEL_FORMAT_RGB332  LCDC_PIXEL_FORMAT_RGB332
#define RTGRAPHIC_PIXEL_FORMAT_RGB565  LCDC_PIXEL_FORMAT_RGB565
#define RTGRAPHIC_PIXEL_FORMAT_RGB666  LCDC_PIXEL_FORMAT_RGB666
#define RTGRAPHIC_PIXEL_FORMAT_RGB888  LCDC_PIXEL_FORMAT_RGB888
#define RTGRAPHIC_PIXEL_FORMAT_ARGB888 LCDC_PIXEL_FORMAT_ARGB888
#define RTGRAPHIC_PIXEL_FORMAT_ARGB565 LCDC_PIXEL_FORMAT_ARGB565
#define RTGRAPHIC_PIXEL_FORMAT_GRAY4   LCDC_PIXEL_FORMAT_A4
#define RTGRAPHIC_PIXEL_FORMAT_A8      LCDC_PIXEL_FORMAT_A8
#define RTGRAPHIC_PIXEL_FORMAT_L8      LCDC_PIXEL_FORMAT_L8

/* These values are private to the original SiFli graphic driver. */
#define RTGRAPHIC_CTRL_GET_INFO             _LCDIOC(70)
#define RTGRAPHIC_CTRL_GET_BUSY             _LCDIOC(71)
#define RTGRAPHIC_CTRL_POWERON              _LCDIOC(72)
#define RTGRAPHIC_CTRL_POWEROFF             _LCDIOC(73)
#define RTGRAPHIC_CTRL_SET_BRIGHTNESS       _LCDIOC(74)
#define RTGRAPHIC_CTRL_GET_BRIGHTNESS_ASYNC _LCDIOC(75)
#define RTGRAPHIC_CTRL_SET_NEXT_TE          _LCDIOC(76)
#define RTGRAPHIC_CTRL_SET_MODE             _LCDIOC(77)
#define RTGRAPHIC_CTRL_RECT_UPDATE          _LCDIOC(78)
#define RTGRAPHIC_CTRL_GET_STATE            _LCDIOC(79)
#define RTGRAPHIC_CTRL_ROTATE_180           _LCDIOC(80)
#define RTGRAPHIC_CTRL_IRQ_INTERVAL_LINE    _LCDIOC(81)
#define RTGRAPHIC_CTRL_GET_BRIGHTNESS       _LCDIOC(82)
#define RTGRAPHIC_CTRL_SELECT_LAYER         _LCDIOC(83)
#define RTGRAPHIC_CTRL_DISABLE_LAYER        _LCDIOC(84)
#define RTGRAPHIC_CTRL_ENABLE_LAYER         _LCDIOC(85)
#define RTGRAPHIC_CTRL_SET_BG_COLOR         _LCDIOC(86)
#define RTGRAPHIC_CTRL_SET_BUF_FORMAT       _LCDIOC(87)
#define RTGRAPHIC_CTRL_SET_LAYER            _LCDIOC(88)

#define PWM_CMD_GET  _LCDIOC(90)
#define PWM_CMD_SET  _LCDIOC(91)

struct rt_device_graphic_info
{
  uint8_t bits_per_pixel;
  uint16_t pixel_format;
  FAR void *framebuffer;
  uint16_t width;
  uint16_t height;
  uint8_t draw_align;
  uint8_t is_round;
  uint32_t bandwidth;
};

struct rt_device_graphic_ops
{
  void (*set_pixel)(const char *, int, int);
  void (*get_pixel)(char *, int, int);
  void *reserved[5];
  void (*draw_rect)(const char *, int, int, int, int);
  void (*draw_rect_async)(const char *, int, int, int, int);
  void (*set_window)(int, int, int, int);
};

typedef int rt_err_t;
typedef int rt_base_t;
typedef int32_t rt_off_t;
typedef uint32_t rt_size_t;
typedef uint32_t rt_tick_t;
typedef uint8_t rt_uint8_t;
typedef uint16_t rt_uint16_t;
typedef uint32_t rt_uint32_t;

struct rt_semaphore
{
  sem_t sem;
  volatile int value;
};
typedef struct rt_semaphore *rt_sem_t;

struct rt_event
{
  nxevent_t event;
  volatile nxevent_mask_t set;
};

struct rt_mq
{
  sem_t slots;
  sem_t items;
  sem_t lock;
  uint8_t *buffer;
  size_t msg_size;
  unsigned depth;
  unsigned head;
  unsigned tail;
  unsigned entry;
};
typedef struct rt_mq *rt_mq_t;

struct rt_thread
{
  pid_t pid;
  void (*entry)(void *);
  void *parameter;
  const char *name;
  int priority;
  int stack_size;
};
typedef struct rt_thread *rt_thread_t;

typedef struct lcd_dev_s *rt_device_t;
typedef struct lcd_dev_s rt_device;

extern FAR struct lcd_dev_s *board_lcd_getdev(int devno);
extern int drv_lcd_set_tx_complete(FAR void (*cb)(rt_device_t, void *));

#ifndef INIT_BOARD_EXPORT
#  define INIT_BOARD_EXPORT(fn)
#endif

#endif /* __DRV_LCD_NUTTX_H__ */
