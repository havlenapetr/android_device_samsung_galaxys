/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 *
 * @author Rama, Meka(v.meka@samsung.com)
           Sangwoo, Park(sw5771.park@samsung.com)
           Jamie Oh (jung-min.oh@samsung.com)
 * @date   2011-07-28
 *
 */

#include "SecHWCUtils.h"

int window_open(struct hwc_win_info_t *win, int id)
{
    char name[64];

    char const * const device_template = "/dev/graphics/fb%u";
    /* window & FB maping
       fb0 -> win-id : 2
       fb1 -> win-id : 3
       fb2 -> win-id : 4
       fb3 -> win-id : 0
       fb4 -> win_id : 1
       it is pre assumed that ...win0 or win1 is used here..
	 */
    switch (id) {
    case 0:
    case 1:
    case 2:
        break;
    default:
        ALOGE("%s::id(%d) is weird", __func__, id);
        goto error;
    }

    snprintf(name, 64, device_template, (id + 3)%5);

    win->fd = open(name, O_RDWR);
    if (win->fd < 0) {
		ALOGE("%s::Failed to open window device (%s) : %s",
				__func__, strerror(errno), device_template);
        goto error;
    }

    return 0;

error:
    if (0 <= win->fd)
        close(win->fd);
    win->fd = -1;

    return -1;
}

int window_close(struct hwc_win_info_t *win)
{
    int ret = 0;

    if (0 <= win->fd)
        ret = close(win->fd);
    win->fd = -1;

    return ret;
}

int window_set_pos(struct hwc_win_info_t *win)
{
    struct secfb_user_window window;

    /* before changing the screen configuration...powerdown the window */
    if(window_hide(win) != 0)
        return -1;

    win->var_info.xres = win->rect_info.w;
    win->var_info.yres = win->rect_info.h;

    win->var_info.activate &= ~FB_ACTIVATE_MASK;
    win->var_info.activate |= FB_ACTIVATE_FORCE;

    if (ioctl(win->fd, FBIOPUT_VSCREENINFO, &(win->var_info)) < 0) {
        ALOGE("%s::FBIOPUT_VSCREENINFO(%d, %d) fail",
          		__func__, win->rect_info.w, win->rect_info.h);
        return -1;
    }

    window.x = win->rect_info.x;
    window.y = win->rect_info.y;

    if (ioctl(win->fd, SECFB_WIN_POSITION, &window) < 0) {
        ALOGE("%s::S3CFB_WIN_POSITION(%d, %d) fail",
            	__func__, window.x, window.y);
      return -1;
    }

    return 0;
}

int window_get_info(struct hwc_win_info_t *win)
{
    if (ioctl(win->fd, FBIOGET_FSCREENINFO, &win->fix_info) < 0) {
        ALOGE("FBIOGET_FSCREENINFO failed : %s", strerror(errno));
        goto error;
    }

    return 0;

error:
    win->fix_info.smem_start = 0;

    return -1;
}

int window_pan_display(struct hwc_win_info_t *win)
{
    struct fb_var_screeninfo *lcd_info = &(win->lcd_info);

    lcd_info->yoffset = lcd_info->yres * win->buf_index;

    if (ioctl(win->fd, FBIOPAN_DISPLAY, lcd_info) < 0) {
        ALOGE("%s::FBIOPAN_DISPLAY(%d / %d / %d) fail(%s)",
            	__func__, lcd_info->yres, win->buf_index, lcd_info->yres_virtual,
            strerror(errno));
        return -1;
    }
    return 0;
}

int window_show(struct hwc_win_info_t *win)
{
    if(win->power_state == 0) {
        if (ioctl(win->fd, FBIOBLANK, FB_BLANK_UNBLANK) < 0) {
            ALOGE("%s: FBIOBLANK failed : (%d:%s)", __func__, win->fd,
                    strerror(errno));
            return -1;
        }
        win->power_state = 1;
    }
    return 0;
}

int window_hide(struct hwc_win_info_t *win)
{
    if (win->power_state == 1) {
        if (ioctl(win->fd, FBIOBLANK, FB_BLANK_POWERDOWN) < 0) {
            ALOGE("%s::FBIOBLANK failed : (%d:%s)",
             		__func__, win->fd, strerror(errno));
            return -1;
        }
        win->power_state = 0;
    }
    return 0;
}

int window_get_global_lcd_info(struct hwc_context_t *ctx)
{
    struct hwc_win_info_t win;
    int ret = 0;

    if (ioctl(ctx->global_lcd_win.fd, FBIOGET_VSCREENINFO, &ctx->lcd_info) < 0) {
        ALOGE("FBIOGET_VSCREENINFO failed : %s", strerror(errno));
        return -1;
    }

    if (ctx->lcd_info.xres == 0) {
        ctx->lcd_info.xres = DEFAULT_LCD_WIDTH;
        ctx->lcd_info.xres_virtual = DEFAULT_LCD_WIDTH;
    }

    if (ctx->lcd_info.yres == 0) {
        ctx->lcd_info.yres = DEFAULT_LCD_HEIGHT;
        ctx->lcd_info.yres_virtual = DEFAULT_LCD_HEIGHT * NUM_OF_WIN_BUF;
    }

    if (ctx->lcd_info.bits_per_pixel == 0)
        ctx->lcd_info.bits_per_pixel = DEFAULT_LCD_BPP;

    return 0;
}