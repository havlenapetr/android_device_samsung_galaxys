/*
 * Copyright (C) Havlena Petr <havlenapetr@gmail.com>
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

//#define LOG_NDEBUG 0
#define LOG_TAG "CameraHAL"

#include <utils/threads.h>
#include <hardware/camera.h>

#include "SecCameraHWInterface.h"

using namespace android;

#define MAX_SIMUL_CAMERAS_SUPPORTED 2

#define RETURN_IF(hw)                                                       \
    if(!hw) {                                                               \
        LOGE("%s: %i - Can't obtain hw driver!", __func__, __LINE__);       \
        return;                                                             \
    }

#define RETURN_NULL_IF(hw)                                                  \
    if(!hw) {                                                               \
        LOGE("%s: %i - Can't obtain hw driver!", __func__, __LINE__);       \
        return NULL;                                                        \
    }

#define RETURN_EINVAL_IF(hw)                                                \
    if(!hw) {                                                               \
        LOGE("%s: %i - Can't obtain hw driver!", __func__, __LINE__);       \
        return -EINVAL;                                                     \
    }

typedef struct sec_camera_device {
    camera_device_t         base;
    /* Sec specific "private" data can go here (base.priv) */
    CameraHardwareSec*      cam; 
} sec_camera_device_t;

static struct camera_info gCameraInfo[] = {
    {
        CAMERA_FACING_BACK,
        90,     // orientation
    },
    {
        CAMERA_FACING_FRONT,
        270,    // orientation
    }
};

static Mutex gCameraHalDeviceLock;
static unsigned int gCamerasOpen = 0;

static CameraHardwareSec* sec_obtain_hw(struct camera_device * device)
{
    sec_camera_device* sec_dev = NULL;
    CameraHardwareSec* sec_hw = NULL;

    if(!device) {
        LOGE("Can't obtain camera device!");
        goto end;
    }

    sec_dev = (sec_camera_device *) device;
    if(!sec_dev) {
        LOGE("Can't obtain SEC camera device!");
        goto end;
    }

    sec_hw = sec_dev->cam;

end:
    return sec_hw;
}

/*******************************************************************
 * implementation of camera_device_ops functions
 *******************************************************************/

int camera_set_preview_window(struct camera_device * device,
        struct preview_stream_ops *window)
{
    LOGV("%s", __FUNCTION__);

    CameraHardwareSec* hw = sec_obtain_hw(device);
    RETURN_EINVAL_IF(hw);

    return hw->setPreviewWindow(window);
}

void camera_set_callbacks(struct camera_device * device,
        camera_notify_callback notify_cb,
        camera_data_callback data_cb,
        camera_data_timestamp_callback data_cb_timestamp,
        camera_request_memory get_memory,
        void *user)
{
    LOGV("%s", __FUNCTION__);

    CameraHardwareSec* hw = sec_obtain_hw(device);
    RETURN_IF(hw);

    hw->setCallbacks(notify_cb, data_cb, data_cb_timestamp, get_memory, user);
}

void camera_enable_msg_type(struct camera_device * device, int32_t msg_type)
{
    LOGV("%s", __FUNCTION__);

    CameraHardwareSec* hw = sec_obtain_hw(device);
    RETURN_IF(hw);

    return hw->enableMsgType(msg_type);
}

void camera_disable_msg_type(struct camera_device * device, int32_t msg_type)
{
    LOGV("%s", __FUNCTION__);

    CameraHardwareSec* hw = sec_obtain_hw(device);
    RETURN_IF(hw);

    return hw->disableMsgType(msg_type);
}

int camera_msg_type_enabled(struct camera_device * device, int32_t msg_type)
{
    LOGV("%s", __FUNCTION__);

    CameraHardwareSec* hw = sec_obtain_hw(device);
    RETURN_EINVAL_IF(hw);

    return hw->msgTypeEnabled(msg_type);
}

int camera_start_preview(struct camera_device * device)
{
    LOGV("%s", __FUNCTION__);

    CameraHardwareSec* hw = sec_obtain_hw(device);
    RETURN_EINVAL_IF(hw);

    return hw->startPreview();
}

void camera_stop_preview(struct camera_device * device)
{
    LOGV("%s", __FUNCTION__);

    CameraHardwareSec* hw = sec_obtain_hw(device);
    RETURN_IF(hw);

    return hw->stopPreview();
}

int camera_preview_enabled(struct camera_device * device)
{
    LOGV("%s", __FUNCTION__);

    CameraHardwareSec* hw = sec_obtain_hw(device);
    RETURN_EINVAL_IF(hw);

    return hw->previewEnabled();
}

int camera_store_meta_data_in_buffers(struct camera_device * device, int enable)
{
    LOGV("%s", __FUNCTION__);

    CameraHardwareSec* hw = sec_obtain_hw(device);
    RETURN_EINVAL_IF(hw);

    return hw->storeMetaDataInBuffers(enable);
}

int camera_start_recording(struct camera_device * device)
{
    LOGV("%s", __FUNCTION__);

    CameraHardwareSec* hw = sec_obtain_hw(device);
    RETURN_EINVAL_IF(hw);

    return hw->startRecording();
}

void camera_stop_recording(struct camera_device * device)
{
    LOGV("%s", __FUNCTION__);

    CameraHardwareSec* hw = sec_obtain_hw(device);
    RETURN_IF(hw);

    hw->stopRecording();
}

int camera_recording_enabled(struct camera_device * device)
{
    LOGV("%s", __FUNCTION__);

    CameraHardwareSec* hw = sec_obtain_hw(device);
    RETURN_EINVAL_IF(hw);

    return hw->recordingEnabled();
}

void camera_release_recording_frame(struct camera_device * device,
                const void *opaque)
{
    LOGV("%s", __FUNCTION__);

    CameraHardwareSec* hw = sec_obtain_hw(device);
    RETURN_IF(hw);

    hw->releaseRecordingFrame(opaque);
}

int camera_auto_focus(struct camera_device * device)
{
    LOGV("%s", __FUNCTION__);

    CameraHardwareSec* hw = sec_obtain_hw(device);
    RETURN_EINVAL_IF(hw);

    return hw->autoFocus();
}

int camera_cancel_auto_focus(struct camera_device * device)
{
    LOGV("%s", __FUNCTION__);

    CameraHardwareSec* hw = sec_obtain_hw(device);
    RETURN_EINVAL_IF(hw);

    return hw->cancelAutoFocus();
}

int camera_take_picture(struct camera_device * device)
{
    LOGV("%s", __FUNCTION__);

    CameraHardwareSec* hw = sec_obtain_hw(device);
    RETURN_EINVAL_IF(hw);

    return hw->takePicture();
}

int camera_cancel_picture(struct camera_device * device)
{
    LOGV("%s", __FUNCTION__);

    CameraHardwareSec* hw = sec_obtain_hw(device);
    RETURN_EINVAL_IF(hw);

    return hw->cancelPicture();
}

int camera_set_parameters(struct camera_device * device, const char *params)
{
    LOGV("%s", __FUNCTION__);

    CameraHardwareSec* hw = sec_obtain_hw(device);
    RETURN_EINVAL_IF(hw);

    return hw->setParameters(params);
}

char* camera_get_parameters(struct camera_device * device)
{
    LOGV("%s", __FUNCTION__);

    CameraHardwareSec* hw = sec_obtain_hw(device);
    RETURN_NULL_IF(hw);

    return hw->getParameters();
}

static void camera_put_parameters(struct camera_device *device, char *parms)
{
    LOGV("%s", __FUNCTION__);

    CameraHardwareSec* hw = sec_obtain_hw(device);
    RETURN_IF(hw);

    hw->putParameters(parms);
}

int camera_send_command(struct camera_device * device,
            int32_t cmd, int32_t arg1, int32_t arg2)
{
    LOGV("%s", __FUNCTION__);

    CameraHardwareSec* hw = sec_obtain_hw(device);
    RETURN_EINVAL_IF(hw);

    return hw->sendCommand(cmd, arg1, arg2);
}

void camera_release(struct camera_device * device)
{
    LOGV("%s", __FUNCTION__);

    CameraHardwareSec* hw = sec_obtain_hw(device);
    RETURN_IF(hw);

    hw->release();
}

/*
int camera_dump(struct camera_device * device, int fd)
{
    CameraHardwareSec* hw = sec_obtain_hw(device);
    RETURN_EINVAL_IF(hw);

    return hw->dump(fd);
}
*/

extern "C" void heaptracker_free_leaked_memory(void);

int camera_device_close(hw_device_t* device)
{
    Mutex::Autolock lock(gCameraHalDeviceLock);

    LOGV("%s", __FUNCTION__);

    int                 ret = -EINVAL;
    sec_camera_device*  sec_dev;

    if(!device) {
        LOGE("Can't obtain camera device!");
        goto done;
    }

    sec_dev = (sec_camera_device *) device;
    if(!sec_dev) {
        LOGE("Can't obtain SEC camera device!");
        goto done;
    }

    delete sec_dev->cam;
    if (sec_dev->base.ops) {
        free(sec_dev->base.ops);
    }
    free(sec_dev);

    gCamerasOpen--;

done:
#ifdef HEAPTRACKER
    heaptracker_free_leaked_memory();
#endif
    return ret;
}

/*******************************************************************
 * implementation of camera_module functions
 *******************************************************************/

/* open device handle to one of the cameras
 *
 * assume camera service will keep singleton of each camera
 * so this function will always only be called once per camera instance
 */

int camera_device_open(const hw_module_t* module, const char* name,
                hw_device_t** device)
{
    Mutex::Autolock lock(gCameraHalDeviceLock);

    int                     rv = 0;
    int                     num_cameras = 0;
    int                     cameraid;
    sec_camera_device_t*    camera_device = NULL;
    camera_device_ops_t*    camera_ops = NULL;
    CameraHardwareSec*      camera_hw = NULL;

    LOGI("camera_device open");

    if (name != NULL) {
        cameraid = atoi(name);
        num_cameras = sizeof(gCameraInfo);

        if(cameraid > num_cameras) {
            LOGE("camera service provided cameraid out of bounds, "
                    "cameraid = %d, num supported = %d",
                    cameraid, num_cameras);
            rv = -EINVAL;
            goto fail;
        }

        if(gCamerasOpen >= MAX_SIMUL_CAMERAS_SUPPORTED) {
            LOGE("maximum number of cameras already open");
            rv = -ENOMEM;
            goto fail;
        }

        camera_device = (sec_camera_device_t*)malloc(sizeof(*camera_device));
        if(!camera_device) {
            LOGE("camera_device allocation fail");
            rv = -ENOMEM;
            goto fail;
        }

        camera_ops = (camera_device_ops_t*)malloc(sizeof(*camera_ops));
        if(!camera_ops) {
            LOGE("camera_ops allocation fail");
            rv = -ENOMEM;
            goto fail;
        }

        memset(camera_device, 0, sizeof(*camera_device));
        memset(camera_ops, 0, sizeof(*camera_ops));

        camera_device->base.common.tag = HARDWARE_DEVICE_TAG;
        camera_device->base.common.version = 0;
        camera_device->base.common.module = (hw_module_t *)(module);
        camera_device->base.common.close = camera_device_close;
        camera_device->base.ops = camera_ops;

        camera_ops->set_preview_window = camera_set_preview_window;
        camera_ops->set_callbacks = camera_set_callbacks;
        camera_ops->enable_msg_type = camera_enable_msg_type;
        camera_ops->disable_msg_type = camera_disable_msg_type;
        camera_ops->msg_type_enabled = camera_msg_type_enabled;
        camera_ops->start_preview = camera_start_preview;
        camera_ops->stop_preview = camera_stop_preview;
        camera_ops->preview_enabled = camera_preview_enabled;
        camera_ops->store_meta_data_in_buffers = camera_store_meta_data_in_buffers;
        camera_ops->start_recording = camera_start_recording;
        camera_ops->stop_recording = camera_stop_recording;
        camera_ops->recording_enabled = camera_recording_enabled;
        camera_ops->release_recording_frame = camera_release_recording_frame;
        camera_ops->auto_focus = camera_auto_focus;
        camera_ops->cancel_auto_focus = camera_cancel_auto_focus;
        camera_ops->take_picture = camera_take_picture;
        camera_ops->cancel_picture = camera_cancel_picture;
        camera_ops->set_parameters = camera_set_parameters;
        camera_ops->get_parameters = camera_get_parameters;
        camera_ops->put_parameters = camera_put_parameters;
        camera_ops->send_command = camera_send_command;
        camera_ops->release = camera_release;
        camera_ops->dump = NULL;

        *device = &camera_device->base.common;

        // -------- Sec specific stuff --------

        camera_hw = new CameraHardwareSec(cameraid);
        rv = camera_hw->init();
        if(rv != NO_ERROR) {
            goto fail;
        }

        camera_device->cam = camera_hw;

        gCamerasOpen++;
    }

    return rv;

fail:
    if(camera_device) {
        free(camera_device);
        camera_device = NULL;
    }
    if(camera_ops) {
        free(camera_ops);
        camera_ops = NULL;
    }
    if(camera_hw) {
        delete camera_hw;
        camera_device->cam = NULL;
    }
    *device = NULL;
    return rv;
}

int camera_get_number_of_cameras(void)
{
    return sizeof(gCameraInfo)/sizeof(gCameraInfo[0]);
}

int camera_get_camera_info(int camera_id, struct camera_info *info)
{
    memcpy(info, &gCameraInfo[camera_id], sizeof(struct camera_info));
    return 0;
}

static struct hw_module_methods_t camera_module_methods = {
    open: camera_device_open
};

camera_module_t HAL_MODULE_INFO_SYM = {
    common: {
         tag: HARDWARE_MODULE_TAG,
         version_major: 1,
         version_minor: 0,
         id: CAMERA_HARDWARE_MODULE_ID,
         name: "Sec SAMSUNG CameraHal Module",
         author: "Havlena Petr",
         methods: &camera_module_methods,
         dso: NULL, /* remove compilation warnings */
         reserved: {0}, /* remove compilation warnings */
    },
    get_number_of_cameras: camera_get_number_of_cameras,
    get_camera_info: camera_get_camera_info,
};
