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

int main(int argc, char** argv) {
    SecTv* hardware;

    tvout_init(&hardware);

    printf("initzializing tvout hardware\n");
    int ret = SecTv::openTvOut(&hardware);
    CHECK(ret);

    ret = hardware->init(FRAMEBUFFER_INDEX, V4L2_PIX_FMT_NV12);
    CHECK(ret);

    /*printf("setting format\n");
    ret = hardware->setFormat(WIDTH, HEIGHT, V4L2_PIX_FMT_NV12);
    CHECK(ret);

    printf("setting src window\n");
    ret = hardware->setWindow(0, 0, WIDTH, HEIGHT);
    CHECK(ret);

    printf("setting crop window\n");
    ret = hardware->setCrop(0, 0, WIDTH, HEIGHT);
    CHECK(ret);*/

    printf("enabling\n");
    ret = hardware->enable();
    CHECK(ret);

    while(!killed) {
        usleep(50000);
    }

end:
    printf("disabling\n");
    ret = hardware->disable();
    //CHECK(ret);

    printf("deinitzializing tvout hardware\n");
    if(hardware != NULL)
       delete hardware;
    return 0;
}
