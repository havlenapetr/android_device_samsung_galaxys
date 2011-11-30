/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (C) 2011 Havlena Petr <havlenapetr@gmail.com>
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

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <linux/input.h>

#include <recovery_ui.h>
#include <roots.h>
#include <common.h>

#include <cutils/properties.h>

#define ITEM_FORMAT_VOLUME      4
#define ITEM_CHROOT             5

#define RETURN_IF(return_value)                                      \
    if (return_value < 0) {                                          \
        ui_print("%s::%d fail. errno: %s\n",                         \
             __func__, __LINE__, strerror(errno));                   \
        return return_value;                                         \
    }

char* MENU_HEADERS[] = { "Volume up/down to move highlight;",
                         "power button to select.",
                         "",
                         NULL };

char* MENU_ITEMS[] = { "reboot system now",
                       "apply update from /sdcard",
                       "wipe data/factory reset",
                       "wipe cache partition",
                       "format volume",
                       "start OS from /dev/block/mmcblk1p2",
                       NULL };

int device_recovery_start() {
    // recovery can get started before the kernel has created the EMMC
    // devices, which will make the wipe_data operation fail (trying
    // to open a device that doesn't exist).  Hold up the start of
    // recovery for up to 5 seconds waiting for the userdata partition
    // block device to exist.

    const char* fn = "/dev/block/platform/s3c-sdhci.0/by-name/userdata";

    int tries = 0;
    int ret;
    struct stat buf;
    do {
        ++tries;
        ret = stat(fn, &buf);
        if (ret) {
            printf("try %d: %s\n", tries, strerror(errno));
            sleep(1);
        }
    } while (ret && tries < 5);
    if (!ret) {
        printf("stat() of %s succeeded on try %d\n", fn, tries);
    } else {
        printf("failed to stat %s\n", fn);
    }

    // We let recovery attempt to carry on even if the stat never
    // succeeded.

    return 0;
}

int device_toggle_display(volatile char* key_pressed, int key_code) {
    // hold power and press volume-up
    return key_pressed[KEY_POWER] && key_code == KEY_VOLUMEUP;
}

int device_reboot_now(volatile char* key_pressed, int key_code) {
    // Reboot if the power key is pressed five times in a row, with
    // no other keys in between.
    static int presses = 0;
    if (key_code == KEY_POWER) {   // power button
        ++presses;
        return presses == 5;
    } else {
        presses = 0;
        return 0;
    }
}

int device_handle_key(int key_code, int visible) {
    if (visible) {
        switch (key_code) {
            case KEY_DOWN:
            case KEY_VOLUMEDOWN:
                return HIGHLIGHT_DOWN;

            case KEY_UP:
            case KEY_VOLUMEUP:
                return HIGHLIGHT_UP;

            case KEY_ENTER:
            case KEY_POWER:   // crespo power
                return SELECT_ITEM;
        }
    }

    return NO_ACTION;
}

int start_os_from_chroot() {
    int ret;

    ui_print("preparing . . .\n");
    ret = mkdir("/mnt/chroot", 0755);
    RETURN_IF(ret);

    ui_print("mounting . . .\n");
    ret = mount("/dev/block/mmcblk1p2", "/mnt/chroot", "ext4", 0, NULL);
    RETURN_IF(ret);

    ui_print("killing ueventd . . .\n");
    ret = property_set("persist.service.ueventd.enable", "0");
    RETURN_IF(ret);

    ui_print("killing adb . . .\n");
    ret = property_set("persist.service.adb.enable", "0");
    RETURN_IF(ret);

    ui_print("chrooting . . .\n");
    ret = chroot("/mnt/chroot");
    RETURN_IF(ret);

    ui_print("starting . . .\n");
    return execlp("/init", "/init", NULL);
}

extern char**
prepend_title(const char** headers);

extern int
get_menu_selection(char** headers, char** items, int menu_only,
                   int initial_selection);

static void
device_format_volume()
{
    int ret;
    char** title_headers = NULL;

    char* headers[] = { "Select which volume to format",
        "",
        NULL };
    title_headers = prepend_title((const char**)headers);

    char* items[] = { "/system",
        "/datadata",
        "/radio",
        "/efs",
        NULL };

    int chosen_item = get_menu_selection(title_headers, items, 1, 0);
    switch (chosen_item) {
        case 0:
            ui_print("formating /system\n");
            ret = format_volume("/system");
            if(ret != 0) {
                ui_print("can't format /system!\n");
            } else {
                ui_print("formated\n");
            }
            break;
        case 1:
            ui_print("formating /datadata\n");
            ret = format_volume("/datadata");
            if(ret != 0) {
                ui_print("can't format /datadata!\n");
            } else {
                ui_print("formated\n");
            }
            break;
        case 2:
            ui_print("formating /radio\n");
            ret = format_volume("/radio");
            if(ret != 0) {
                ui_print("can't format /radio!\n");
            } else {
                ui_print("formated\n");
            }
            break;
        case 3:
            ui_print("formating /efs\n");
            ret = format_volume("/efs");
            if(ret != 0) {
                ui_print("can't format /efs!\n");
            } else {
                ui_print("formated\n");
            }
            break;
    }
}

int device_perform_action(int which) {
    int ret;

    switch (which) {
        case ITEM_FORMAT_VOLUME:
            device_format_volume();
            break;
        // here we will mount root folder of new OS
        // chroot to this folder and start booting
        case ITEM_CHROOT:
            ret = start_os_from_chroot();
            if(ret != 0) {
                ui_print("can't boot OS from chroot!\n");
            }
            break;
    }
    return which;
}

int device_wipe_data() {
    return 0;
}