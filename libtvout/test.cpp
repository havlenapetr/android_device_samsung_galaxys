/*
 * Copyright (C) 2011 Havlena Petr, <havlenapetr@gmail.com>
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

#include "SecTv.h"
#include "fimd_api.h"
#include "s3cfb.h"

#define CHECK(return_value)                                          \
    if (return_value < 0) {                                          \
        printf("%s::%d fail. errno: %s, return_value = %d\n",        \
             __func__, __LINE__, strerror(errno), return_value);     \
        goto end;                                                    \
    }

#define WIDTH   800
#define HEIGHT  480

#define FRAMEBUFFER_INDEX   1

using namespace android;

static bool killed;

static void tvout_os_handler(int signum, siginfo_t *info, void *ptr) {
    killed = true;
}

static void tvout_init(SecTv** hardware) {
    struct sigaction act;

    hardware = NULL;
    killed = false;

    // init os signal handler
    memset(&act, 0, sizeof(act));
    act.sa_sigaction = tvout_os_handler;
    act.sa_flags = SA_SIGINFO;
    sigaction(SIGTERM, &act, NULL);
    sigaction(SIGINT, &act, NULL);
    sigaction(SIGKILL, &act, NULL);
}

static void
print_fbs(int max)
{
    int ret, fb;
    struct fb_var_screeninfo fb_info;

    for(int i=0;i<max;i++) {
        fb = fb_open(i);
        if(fb > 0) {
            ret = get_vscreeninfo(fb, &fb_info);
            if(ret == 0) {
                printf("fb(%i): x(%i) y(%i) active(%i)\n", i, fb_info.xres,
                    fb_info.yres, fb_info.activate);
            }
            fb_close(i);
        } else {
            printf("can't open fb(%i)\n", i);
        }
    }
}

int main(int argc, char** argv) {
    int ret, fb;
    SecTv* hardware;
    struct s3cfb_next_info s3c_fb_info;
    struct fb_var_screeninfo fb_info;

    fb = 0;
    tvout_init(&hardware);

    //print_fbs(2);

    printf("initzializing tvout hardware\n");
    ret = SecTv::openTvOut(&hardware);
    CHECK(ret);

    ret = hardware->setStandart(S5P_TV_STD_PAL_BDGHI);
    CHECK(ret);

    ret = hardware->setOutput(S5P_TV_OUTPUT_TYPE_COMPOSITE);
    CHECK(ret);

    fb = fb_open(0);
    CHECK(fb);

    ret = get_vscreeninfo(fb, &fb_info);
    CHECK(ret);

    /* get physical framebuffer address for LCD */
    if (ioctl(fb, S3CFB_GET_CURR_FB_INFO, &s3c_fb_info) == -1) {
        printf("%s:ioctl(S3CFB_GET_LCD_ADDR) fail\n", __func__);
        goto end;
    }

    ret = hardware->setFormat(fb_info.xres,
                              fb_info.yres,
                              V4L2_PIX_FMT_NV12);
    CHECK(ret);

    ret = hardware->setWindow(fb_info.xoffset, fb_info.yoffset, fb_info.xres, fb_info.yres);
    CHECK(ret);

    printf("enabling\n");
    ret = hardware->enable();
    CHECK(ret);

    while(!killed) {
        ret = hardware->draw(0, 0);
        CHECK(ret);

        usleep(50000);
    }

end:
    printf("disabling\n");

    if(fb)
        fb_close(fb);

    ret = hardware->disable();
    //CHECK(ret);

    printf("deinitzializing tvout hardware\n");
    if(hardware != NULL)
       delete hardware;
    return 0;
}
