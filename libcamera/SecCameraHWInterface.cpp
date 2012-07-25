/*
**
** Copyright 2008, The Android Open Source Project
** Copyright 2010, Samsung Electronics Co. LTD
** Copyright 2011, Havlena Petr <havlenapetr@gmail.com>
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

//#define LOG_NDEBUG 0
#define LOG_TAG "CameraHardwareSec"
#include <utils/Log.h>

#include "SecCameraHWInterface.h"
#include <utils/threads.h>
#include <fcntl.h>
#include <sys/mman.h>

#include <MetadataBufferType.h>

#define BACK_CAMERA_AUTO_FOCUS_DISTANCES_STR       "0.10,1.20,Infinity"
#define BACK_CAMERA_MACRO_FOCUS_DISTANCES_STR      "0.10,0.20,Infinity"
#define BACK_CAMERA_INFINITY_FOCUS_DISTANCES_STR   "0.10,1.20,Infinity"
#define FRONT_CAMERA_FOCUS_DISTANCES_STR           "0.20,0.25,Infinity"

#define RELEASE_MEMORY_BUFFER(buffer)                                \
    if (buffer) {                                                    \
        buffer->release(buffer);                                     \
        buffer = NULL;                                               \
    }

namespace android {

struct addrs {
    uint32_t type;  // make sure that this is 4 byte.
    unsigned int addr_y;
    unsigned int addr_cbcr;
    unsigned int buf_index;
    unsigned int reserved;
};

struct addrs_cap {
    unsigned int addr_y;
    unsigned int width;
    unsigned int height;
};

static const int INITIAL_SKIP_FRAME = 3;
static const int EFFECT_SKIP_FRAME = 1;

gralloc_module_t const* CameraHardwareSec::mGrallocHal = NULL;

CameraHardwareSec::CameraHardwareSec(int cameraId)
        :
          mCaptureInProgress(false),
          mFaceDetectStarted(false),
          mParameters(),
          mPreviewMemory(0),
          mRawHeap(0),
          mRecordHeap(0),
          mSecCamera(NULL),
          mCameraSensorName(NULL),
          mSkipFrame(0),
          mWindow(NULL),
          mNotifyCb(0),
          mDataCb(0),
          mDataCbTimestamp(0),
          mCallbackCookie(0),
          mMsgEnabled(0),
          mRecordRunning(false),
          mPostViewWidth(0),
          mPostViewHeight(0),
          mPostViewSize(0)
{
    int ret;

    ALOGV("%s :", __func__);
    mSecCamera = SecCamera::createInstance();
    if (mSecCamera == NULL) {
        ALOGE("ERR(%s):Fail on mSecCamera object creation", __func__);
        return;
    }
    
    if (!mGrallocHal) {
        ret = hw_get_module(GRALLOC_HARDWARE_MODULE_ID, (const hw_module_t **)&mGrallocHal);
        if (ret)
            ALOGE("ERR(%s):Fail on loading gralloc HAL", __func__);
    }

    ret = mSecCamera->initCamera(cameraId);
    if (ret < 0) {
        ALOGE("ERR(%s):Fail on mSecCamera init", __func__);
        return;
    }
}

status_t CameraHardwareSec::init()
{
    if (mSecCamera->flagCreate() == 0) {
        ALOGE("ERR(%s):Fail on mSecCamera->flagCreate()", __func__);
        return UNKNOWN_ERROR;
    }
    
    mSecCamera->getPostViewConfig(&mPostViewWidth, &mPostViewHeight, &mPostViewSize);
    ALOGV("mPostViewWidth = %d mPostViewHeight = %d mPostViewSize = %d",
         mPostViewWidth,mPostViewHeight,mPostViewSize);
    
    initDefaultParameters(mSecCamera->getCameraId());
    
    mExitAutoFocusThread = false;
    mExitPreviewThread = false;
    /* whether the PreviewThread is active in preview or stopped.  we
     * create the thread but it is initially in stopped state.
     */
    mPreviewRunning = false;
    mPreviewThread = new PreviewThread(this);
    mAutoFocusThread = new AutoFocusThread(this);
    mPictureThread = new PictureThread(this);
    
    return NO_ERROR;
}

void CameraHardwareSec::initDefaultParameters(int cameraId)
{
    if (mSecCamera == NULL) {
        ALOGE("ERR(%s):mSecCamera object is NULL", __func__);
        return;
    }

    SecCameraParameters p;
    SecCameraParameters ip;

    mCameraSensorName = mSecCamera->getCameraSensorName();
    ALOGV("CameraSensorName: %s", mCameraSensorName);

    int preview_max_width   = 0;
    int preview_max_height  = 0;
    int snapshot_max_width  = 0;
    int snapshot_max_height = 0;

    if (cameraId == SecCamera::CAMERA_ID_BACK) {
        p.set(SecCameraParameters::KEY_SUPPORTED_PREVIEW_SIZES,
              "1280x720,800x480,720x480,640x480,592x480,320x240,176x144");
        p.set(SecCameraParameters::KEY_SUPPORTED_PICTURE_SIZES,
              "2560x1920,2560x1536,2048x1536,2048x1232,1600x1200,1600x960,800x480,640x480");
    } else {
        p.set(SecCameraParameters::KEY_SUPPORTED_PREVIEW_SIZES,
              "640x480,320x240,176x144");
        p.set(SecCameraParameters::KEY_SUPPORTED_PICTURE_SIZES,
              "640x480");
    }

    p.getSupportedPreviewSizes(mSupportedPreviewSizes);

    // If these fail, then we are using an invalid cameraId and we'll leave the
    // sizes at zero to catch the error.
    mSecCamera->getPreviewMaxSize(&preview_max_width,
                                  &preview_max_height);

    mSecCamera->getSnapshotMaxSize(&snapshot_max_width,
                                   &snapshot_max_height);

    p.setPictureFormat(SecCameraParameters::PIXEL_FORMAT_JPEG);
    p.setPictureSize(snapshot_max_width, snapshot_max_height);
    p.set(SecCameraParameters::KEY_JPEG_QUALITY, "100"); // maximum quality

    String8 previewColorString;
    previewColorString = CameraParameters::PIXEL_FORMAT_YUV420SP;
    previewColorString.append(",");
    previewColorString.append(CameraParameters::PIXEL_FORMAT_YUV420P);

    p.setPreviewFormat(SecCameraParameters::PIXEL_FORMAT_YUV420SP);
    p.set(SecCameraParameters::KEY_SUPPORTED_PREVIEW_FORMATS,
          previewColorString.string());
    p.set(SecCameraParameters::KEY_SUPPORTED_PICTURE_FORMATS,
          SecCameraParameters::PIXEL_FORMAT_JPEG);
    p.set(SecCameraParameters::KEY_VIDEO_FRAME_FORMAT,
          SecCameraParameters::PIXEL_FORMAT_YUV420P);
    p.setPreviewSize(preview_max_width, preview_max_height);

    String8 parameterString;

    if (cameraId == SecCamera::CAMERA_ID_BACK) {
        parameterString = SecCameraParameters::FOCUS_MODE_AUTO;
        parameterString.append(",");
        parameterString.append(CameraParameters::FOCUS_MODE_INFINITY);
        parameterString.append(",");
        parameterString.append(SecCameraParameters::FOCUS_MODE_MACRO);
        p.set(SecCameraParameters::KEY_SUPPORTED_FOCUS_MODES,
              parameterString.string());
        p.set(SecCameraParameters::KEY_FOCUS_MODE,
              SecCameraParameters::FOCUS_MODE_AUTO);
        p.set(SecCameraParameters::KEY_FOCUS_DISTANCES,
              BACK_CAMERA_AUTO_FOCUS_DISTANCES_STR);
        p.set(SecCameraParameters::KEY_SUPPORTED_JPEG_THUMBNAIL_SIZES,
              "320x240,0x0");
        p.set(SecCameraParameters::KEY_JPEG_THUMBNAIL_WIDTH, "320");
        p.set(SecCameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT, "240");
        p.set(SecCameraParameters::KEY_SUPPORTED_PREVIEW_FRAME_RATES, "30");
        p.setPreviewFrameRate(30);
    } else {
        p.set(CameraParameters::KEY_SUPPORTED_FOCUS_MODES,
              CameraParameters::FOCUS_MODE_AUTO);
        p.set(CameraParameters::KEY_FOCUS_MODE,
              CameraParameters::FOCUS_MODE_AUTO);
        p.set(SecCameraParameters::KEY_FOCUS_DISTANCES,
              FRONT_CAMERA_FOCUS_DISTANCES_STR);
        p.set(SecCameraParameters::KEY_SUPPORTED_JPEG_THUMBNAIL_SIZES,
              "160x120,0x0");
        p.set(SecCameraParameters::KEY_JPEG_THUMBNAIL_WIDTH, "160");
        p.set(SecCameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT, "120");
        p.set(SecCameraParameters::KEY_SUPPORTED_PREVIEW_FRAME_RATES, "15");
        p.setPreviewFrameRate(15);
    }

    parameterString = SecCameraParameters::EFFECT_NONE;
    parameterString.append(",");
    parameterString.append(SecCameraParameters::EFFECT_MONO);
    parameterString.append(",");
    parameterString.append(SecCameraParameters::EFFECT_NEGATIVE);
    parameterString.append(",");
    parameterString.append(SecCameraParameters::EFFECT_SEPIA);
    // back camera has other effects, so show them for app
    if (cameraId == SecCamera::CAMERA_ID_BACK) {
        parameterString.append(",");
        parameterString.append(SecCameraParameters::EFFECT_ANTIQUE);
        parameterString.append(",");
        parameterString.append(SecCameraParameters::EFFECT_SHARPEN);
    }
    p.set(SecCameraParameters::KEY_SUPPORTED_EFFECTS, parameterString.string());
    p.set(SecCameraParameters::KEY_EFFECT, SecCameraParameters::EFFECT_NONE);

    // ISO
    if (cameraId == SecCamera::CAMERA_ID_BACK) {
        parameterString = SecCameraParameters::ISO_AUTO;
        parameterString.append(",");
        parameterString.append(SecCameraParameters::ISO_50);
    } else {
        parameterString = SecCameraParameters::ISO_50;
    }
    parameterString.append(",");
    parameterString.append(SecCameraParameters::ISO_100);
    parameterString.append(",");
    parameterString.append(SecCameraParameters::ISO_200);
    parameterString.append(",");
    parameterString.append(SecCameraParameters::ISO_400);
    if (cameraId == SecCamera::CAMERA_ID_BACK) {
        parameterString.append(",");
        parameterString.append(SecCameraParameters::ISO_800);
        parameterString.append(",");
        parameterString.append(SecCameraParameters::ISO_1600);
        parameterString.append(",");
        parameterString.append(SecCameraParameters::ISO_SPORTS);
        parameterString.append(",");
        parameterString.append(SecCameraParameters::ISO_NIGHT);
        parameterString.append(",");
        parameterString.append(SecCameraParameters::ISO_MOVIE);
    }
    p.set(SecCameraParameters::KEY_SUPPORTED_ISO_MODES,
            parameterString.string());
    p.set(SecCameraParameters::KEY_ISO, cameraId == SecCamera::CAMERA_ID_BACK ?
            SecCameraParameters::ISO_AUTO : SecCameraParameters::ISO_100);

    if (cameraId == SecCamera::CAMERA_ID_BACK) {
        parameterString = SecCameraParameters::FLASH_MODE_OFF;
        p.set(SecCameraParameters::KEY_SUPPORTED_FLASH_MODES,
              parameterString.string());
        p.set(SecCameraParameters::KEY_FLASH_MODE,
              SecCameraParameters::FLASH_MODE_OFF);

        // focus areas
        SecCameraArea dummyArea;
        p.set(SecCameraParameters::KEY_MAX_NUM_FOCUS_AREAS, "1");
        p.set(SecCameraParameters::KEY_FOCUS_AREAS, dummyArea.toString8());

        /*
        ce147 know nothing about scene modes
        parameterString = SecCameraParameters::SCENE_MODE_AUTO;
        parameterString.append(",");
        parameterString.append(SecCameraParameters::SCENE_MODE_PORTRAIT);
        parameterString.append(",");
        parameterString.append(SecCameraParameters::SCENE_MODE_LANDSCAPE);
        parameterString.append(",");
        parameterString.append(SecCameraParameters::SCENE_MODE_NIGHT);
        parameterString.append(",");
        parameterString.append(SecCameraParameters::SCENE_MODE_BEACH);
        parameterString.append(",");
        parameterString.append(SecCameraParameters::SCENE_MODE_SNOW);
        parameterString.append(",");
        parameterString.append(SecCameraParameters::SCENE_MODE_SUNSET);
        parameterString.append(",");
        parameterString.append(SecCameraParameters::SCENE_MODE_FIREWORKS);
        parameterString.append(",");
        parameterString.append(SecCameraParameters::SCENE_MODE_SPORTS);
        parameterString.append(",");
        parameterString.append(SecCameraParameters::SCENE_MODE_PARTY);
        parameterString.append(",");
        parameterString.append(SecCameraParameters::SCENE_MODE_CANDLELIGHT);
        p.set(SecCameraParameters::KEY_SUPPORTED_SCENE_MODES,
              parameterString.string());
        p.set(SecCameraParameters::KEY_SCENE_MODE,
              SecCameraParameters::SCENE_MODE_AUTO);*/

        p.set(SecCameraParameters::KEY_ZOOM, "0");
        p.set(SecCameraParameters::KEY_MAX_ZOOM, "12");
        p.set(SecCameraParameters::KEY_ZOOM_RATIOS, "100,125,150,175,200,225,250,275,300,325,350,375,400");
        p.set(SecCameraParameters::KEY_ZOOM_SUPPORTED, SecCameraParameters::TRUE);

        /* signal that we have face detection in back camera
         * TODO: findout how much faces ce147 can detect
         */
        p.set(SecCameraParameters::KEY_MAX_NUM_DETECTED_FACES_HW, 5);
        p.set(SecCameraParameters::KEY_MAX_NUM_DETECTED_FACES_SW, 0);

        /* we have two ranges, 4-30fps for night mode and
         * 15-30fps for all others
         */
        p.set(SecCameraParameters::KEY_SUPPORTED_PREVIEW_FPS_RANGE, "(15000,30000)");
        p.set(SecCameraParameters::KEY_PREVIEW_FPS_RANGE, "15000,30000");

        p.set(SecCameraParameters::KEY_FOCAL_LENGTH, "3.43");
    } else {
        p.set(SecCameraParameters::KEY_SUPPORTED_PREVIEW_FPS_RANGE, "(7500,30000)");
        p.set(SecCameraParameters::KEY_PREVIEW_FPS_RANGE, "7500,30000");

        p.set(SecCameraParameters::KEY_FOCAL_LENGTH, "0.9");
    }

    parameterString = SecCameraParameters::WHITE_BALANCE_AUTO;
    parameterString.append(",");
    parameterString.append(SecCameraParameters::WHITE_BALANCE_INCANDESCENT);
    parameterString.append(",");
    parameterString.append(SecCameraParameters::WHITE_BALANCE_FLUORESCENT);
    parameterString.append(",");
    parameterString.append(SecCameraParameters::WHITE_BALANCE_DAYLIGHT);
    parameterString.append(",");
    parameterString.append(SecCameraParameters::WHITE_BALANCE_CLOUDY_DAYLIGHT);
    p.set(SecCameraParameters::KEY_SUPPORTED_WHITE_BALANCE,
          parameterString.string());
    p.set(SecCameraParameters::KEY_WHITE_BALANCE, SecCameraParameters::WHITE_BALANCE_AUTO);

    ip.set("sharpness-min", 0);
    ip.set("sharpness-max", 4);
    ip.set("saturation-min", 0);
    ip.set("saturation-max", 4);

    p.set(SecCameraParameters::KEY_JPEG_THUMBNAIL_QUALITY, "100");

    p.set(SecCameraParameters::KEY_ROTATION, 0);

    ip.set("sharpness", SHARPNESS_DEFAULT);
    ip.set("saturation", SATURATION_DEFAULT);
    ip.set("metering", "center");
    ip.set("contrast", CONTRAST_DEFAULT);
    ip.set("iso", "auto");

    ip.set("wdr", 0);
    ip.set("chk_dataline", 0);
    if (cameraId == SecCamera::CAMERA_ID_FRONT) {
        ip.set("vtmode", 0);
        ip.set("blur", 0);
    }

    p.set(SecCameraParameters::KEY_HORIZONTAL_VIEW_ANGLE, "51.2");
    p.set(SecCameraParameters::KEY_VERTICAL_VIEW_ANGLE, "39.4");

    p.set(SecCameraParameters::KEY_EXPOSURE_COMPENSATION, "0");
    p.set(SecCameraParameters::KEY_MAX_EXPOSURE_COMPENSATION, "4");
    p.set(SecCameraParameters::KEY_MIN_EXPOSURE_COMPENSATION, "-4");
    p.set(SecCameraParameters::KEY_EXPOSURE_COMPENSATION_STEP, "0.5");

    p.set(SecCameraParameters::KEY_CONTRAST, "0");
    p.set(SecCameraParameters::KEY_MAX_CONTRAST, "2");
    p.set(SecCameraParameters::KEY_MIN_CONTRAST, "-2");
    p.set(SecCameraParameters::KEY_CONTRAST_STEP, "0.5");

    mParameters = p;
    mInternalParameters = ip;

    /* make sure mSecCamera has all the settings we do.  applications
     * aren't required to call setParameters themselves (only if they
     * want to change something.
     */
    setParameters(p);
    mSecCamera->setISO(ISO_AUTO);
    mSecCamera->setContrast(CONTRAST_DEFAULT);
    mSecCamera->setMetering(METERING_CENTER);
    mSecCamera->setSharpness(SHARPNESS_DEFAULT);
    mSecCamera->setSaturation(SATURATION_DEFAULT);
    if (cameraId == SecCamera::CAMERA_ID_BACK)
        mSecCamera->setFrameRate(30);
    else
        mSecCamera->setFrameRate(15);
}

CameraHardwareSec::~CameraHardwareSec()
{
    ALOGV("%s :", __func__);

    release();
}

// here I don't know what I am doing, so now debugging
status_t CameraHardwareSec::setPreviewWindow(struct preview_stream_ops *window)
{
    int min_bufs;

    ALOGV("%s: mWindow %p", __func__, mWindow);

    Mutex::Autolock lock(mPreviewLock);

    mWindow = window;
    if (!window) {
        ALOGE("preview window is NULL!");
        return OK;
    }

    if (mPreviewRunning && !mPreviewStartDeferred) {
        ALOGI("stop preview (window change)");
        stopPreview_l();
    }

    if (window->get_min_undequeued_buffer_count(window, &min_bufs)) {
        ALOGE("%s: could not retrieve min undequeued buffer count", __func__);
        return INVALID_OPERATION;
    }

    if (min_bufs >= kBufferCount) {
        ALOGE("%s: min undequeued buffer count %d is too high (expecting at most %d)", __func__,
             min_bufs, kBufferCount - 1);
    }
    
    ALOGV("%s: setting buffer count to %d", __func__, kBufferCount);
    if (window->set_buffer_count(window, kBufferCount)) {
        ALOGE("%s: could not set buffer count", __func__);
        return INVALID_OPERATION;
    }
    
    int preview_width;
    int preview_height;
    mParameters.getPreviewSize(&preview_width, &preview_height);
    int hal_pixel_format = HAL_PIXEL_FORMAT_YV12;
    
    const char *str_preview_format = mParameters.getPreviewFormat();
    ALOGV("%s: preview format %s", __func__, str_preview_format);
    
    if (window->set_usage(window, GRALLOC_USAGE_SW_WRITE_OFTEN)) {
        ALOGE("%s: could not set usage on gralloc buffer", __func__);
        return INVALID_OPERATION;
    }
    
    if (window->set_buffers_geometry(window,
                                preview_width, preview_height,
                                hal_pixel_format)) {
        ALOGE("%s: could not set buffers geometry to %s",
             __func__, str_preview_format);
        return INVALID_OPERATION;
    }

    // if we were called after startPreview
    if(mPreviewRunning && mPreviewStartDeferred) {
        int ret = startPreview_l();
        if (ret < 0) {
            ALOGE("ERR(%s):Fail on startPreview_l", __func__);
            return UNKNOWN_ERROR;
        } else {
            mPreviewStartDeferred = false;
            mPreviewCondition.signal();
        }
    }

    return NO_ERROR;
}

status_t CameraHardwareSec::storeMetaDataInBuffers(bool enable)
{
    // FIXME:
    // metadata buffer mode can be turned on or off.
    // Samsung needs to fix this.
    if (!enable) {
        ALOGE("Non-metadata buffer mode is not supported!");
        return INVALID_OPERATION;
    }
    return OK;
}

void CameraHardwareSec::setCallbacks(camera_notify_callback notify_cb,
                                     camera_data_callback data_cb,
                                     camera_data_timestamp_callback data_cb_timestamp,
                                     camera_request_memory get_memory,
                                     void *user)
{
    mNotifyCb = notify_cb;
    mDataCb = data_cb;
    mDataCbTimestamp = data_cb_timestamp;
    mGetMemoryCb = get_memory;
    mCallbackCookie = user;
}

void CameraHardwareSec::enableMsgType(int32_t msgType)
{
    ALOGV("%s : msgType = 0x%x, mMsgEnabled before = 0x%x",
         __func__, msgType, mMsgEnabled);
    mMsgEnabled |= msgType;
    ALOGV("%s : mMsgEnabled = 0x%x", __func__, mMsgEnabled);
}

void CameraHardwareSec::disableMsgType(int32_t msgType)
{
    ALOGV("%s : msgType = 0x%x, mMsgEnabled before = 0x%x",
         __func__, msgType, mMsgEnabled);
    mMsgEnabled &= ~msgType;
    ALOGV("%s : mMsgEnabled = 0x%x", __func__, mMsgEnabled);
}

bool CameraHardwareSec::msgTypeEnabled(int32_t msgType)
{
    return (mMsgEnabled & msgType);
}

// ---------------------------------------------------------------------------
void CameraHardwareSec::setSkipFrame(int frame)
{
    Mutex::Autolock lock(mSkipFrameLock);
    if (frame < mSkipFrame)
        return;

    mSkipFrame = frame;
}

int CameraHardwareSec::previewThreadWrapper()
{
    ALOGI("%s: starting", __func__);
    while (1) {
        mPreviewLock.lock();
        while (!mPreviewRunning) {
            ALOGV("%s: calling mSecCamera->stopPreview() and waiting", __func__);
            mSecCamera->stopPreview();
            /* signal that we're stopping */
            mPreviewStoppedCondition.signal();
            mPreviewCondition.wait(mPreviewLock);
            ALOGV("%s: return from wait", __func__);
        }
        mPreviewLock.unlock();

        if (mExitPreviewThread) {
            ALOGV("%s: exiting", __func__);
            mSecCamera->stopPreview();
            return 0;
        }
        previewThread();
    }
}

int CameraHardwareSec::previewThread()
{
    int             index;
    nsecs_t         timestamp;
    unsigned int    phyYAddr;
    unsigned int    phyCAddr;
    struct addrs*   addrs;
    int             width, height, frame_size, offset;

    index = mSecCamera->getPreview();
    if (index < 0) {
        ALOGE("ERR(%s):Fail on SecCamera->getPreview()", __func__);
        return UNKNOWN_ERROR;
    }
    mSkipFrameLock.lock();
    if (mSkipFrame > 0) {
        mSkipFrame--;
        mSkipFrameLock.unlock();
        return NO_ERROR;
    }
    mSkipFrameLock.unlock();

    timestamp = systemTime(SYSTEM_TIME_MONOTONIC);

    phyYAddr = mSecCamera->getPhyAddrY(index);
    phyCAddr = mSecCamera->getPhyAddrC(index);
    if (phyYAddr == 0xffffffff || phyCAddr == 0xffffffff) {
        ALOGE("ERR(%s):Fail on SecCamera getPhyAddr Y addr = %0x C addr = %0x",
             __func__, phyYAddr, phyCAddr);
        return UNKNOWN_ERROR;
    }

    mSecCamera->getPreviewSize(&width, &height, &frame_size);
    offset = frame_size * index;

    // draw new frame into window
    if(mWindow && mGrallocHal) {
        buffer_handle_t *buf_handle;
        int stride;
        int ret;

        if (0 != (ret = mWindow->dequeue_buffer(mWindow, &buf_handle, &stride))) {
            ALOGE("%s: Could not dequeue gralloc buffer: %i!", __func__, ret);
        } else {
            void *vaddr;
            if (!mGrallocHal->lock(mGrallocHal,
                                   *buf_handle,
                                   GRALLOC_USAGE_SW_WRITE_OFTEN,
                                   0, 0, width, height, &vaddr)) {
                char *frame = ((char *)mPreviewMemory->data) + offset;

                // the code below assumes YUV, not RGB
                {
                    int h;
                    char *src = frame;
                    char *ptr = (char *)vaddr;
                
                    // Copy the Y plane, while observing the stride
                    for (h = 0; h < height; h++) {
                        memcpy(ptr, src, width);
                        ptr += stride;
                        src += width;
                    }
                
                    {
                        // U
                        char *v = ptr;
                        ptr += stride * height / 4;
                        for (h = 0; h < height / 2; h++) {
                            memcpy(ptr, src, width / 2);
                            ptr += stride / 2;
                            src += width / 2;
                        }
                        // V
                        ptr = v;
                        for (h = 0; h < height / 2; h++) {
                            memcpy(ptr, src, width / 2);
                            ptr += stride / 2;
                            src += width / 2;
                        }
                    }
                }

                mGrallocHal->unlock(mGrallocHal, *buf_handle);
            } else {
                ALOGE("%s: Could not obtain gralloc buffer", __func__);
            }

            if (0 != (ret = mWindow->enqueue_buffer(mWindow, buf_handle))) {
                ALOGE("%s: Could not enqueue gralloc buffer: %i!", __func__, ret);
            }
        }
    }

    // Notify the client of a new frame.
    if (mMsgEnabled & CAMERA_MSG_PREVIEW_FRAME) {
        const char * preview_format = mParameters.getPreviewFormat();
        if (!strcmp(preview_format, CameraParameters::PIXEL_FORMAT_YUV420SP)) {
            // Color conversion from YUV420 to NV21
            char *vu = ((char *)mPreviewMemory->data) + offset + width * height;
            const int uv_size = (width * height) >> 1;
            char saved_uv[uv_size];
            memcpy(saved_uv, vu, uv_size);
            char *u = saved_uv;
            char *v = u + (uv_size >> 1);

            int h = 0;
            while (h < width * height / 4) {
                *vu++ = *v++;
                *vu++ = *u++;
                ++h;
            }
        }
        mDataCb(CAMERA_MSG_PREVIEW_FRAME, mPreviewMemory, index, NULL, mCallbackCookie);
    }

    Mutex::Autolock lock(mRecordLock);
    if (mRecordRunning == true) {
        index = mSecCamera->getRecordFrame();
        if (index < 0) {
            ALOGE("ERR(%s):Fail on SecCamera->getRecord()", __func__);
            return UNKNOWN_ERROR;
        }

        phyYAddr = mSecCamera->getRecPhyAddrY(index);
        phyCAddr = mSecCamera->getRecPhyAddrC(index);
        if (phyYAddr == 0xffffffff || phyCAddr == 0xffffffff) {
            ALOGE("ERR(%s):Fail on SecCamera getRectPhyAddr Y addr = %0x C addr = %0x", __func__, phyYAddr, phyCAddr);
            return UNKNOWN_ERROR;
        }

        addrs = (struct addrs *)mRecordHeap->data;

        addrs[index].type   = kMetadataBufferTypeCameraSource;
        addrs[index].addr_y = phyYAddr;
        addrs[index].addr_cbcr = phyCAddr;
        addrs[index].buf_index = index;

        // Notify the client of a new frame.
        if (mMsgEnabled & CAMERA_MSG_VIDEO_FRAME) {
            mDataCbTimestamp(timestamp, CAMERA_MSG_VIDEO_FRAME, mRecordHeap,
                             index, mCallbackCookie);
        } else {
            mSecCamera->releaseRecordFrame(index);
        }
    }
    return NO_ERROR;
}

status_t CameraHardwareSec::startPreview()
{
    ALOGV("%s - start", __func__);

    Mutex::Autolock lock(mStateLock);
    if (mCaptureInProgress) {
        ALOGE("%s : capture in progress, not allowed", __func__);
        return INVALID_OPERATION;
    }

    Mutex::Autolock previewLock(mPreviewLock);
    if (mPreviewRunning) {
        ALOGE("%s : preview thread already running", __func__);
        return INVALID_OPERATION;
    }

    mPreviewRunning = true;
    mPreviewStartDeferred = false;

    if(!mWindow) {
        mPreviewStartDeferred = true;
        return NO_ERROR;
    }

    int ret = startPreview_l();
    if (ret == OK) {
        mPreviewCondition.signal();
    }

    return ret;
}

status_t CameraHardwareSec::startPreview_l()
{
    status_t ret;
    int width, height, frame_size;

    ret = mSecCamera->startPreview();
    if (ret < 0) {
        ALOGE("ERR(%s):Fail on mSecCamera->startPreview()", __func__);
        return UNKNOWN_ERROR;
    }

    setSkipFrame(INITIAL_SKIP_FRAME);

    mSecCamera->getPreviewSize(&width, &height, &frame_size);
    ALOGD("MemoryHeapBase(fd(%d), size(%d), width(%d), height(%d))",
             mSecCamera->getCameraFd(), frame_size * kBufferCount, width, height);

    RELEASE_MEMORY_BUFFER(mPreviewMemory);
    mPreviewMemory = mGetMemoryCb(mSecCamera->getCameraFd(),
                                  frame_size,
                                  kBufferCount,
                                  mCallbackCookie);
    if (!mPreviewMemory) {
        ALOGE("ERR(%s): Preview heap creation fail", __func__);
        return NO_MEMORY;
    }

    mSecCamera->getPostViewConfig(&mPostViewWidth, &mPostViewHeight, &mPostViewSize);
    ALOGV("CameraHardwareSec: mPostViewWidth = %d mPostViewHeight = %d mPostViewSize = %d",
             mPostViewWidth,mPostViewHeight,mPostViewSize);

    return OK;
}

void CameraHardwareSec::stopPreview()
{
    ALOGV("%s :", __func__);

    Mutex::Autolock lock(mPreviewLock);
    stopPreview_l();
}

status_t CameraHardwareSec::startFaceDetection()
{
    if (mFaceDetectStarted) {
        ALOGD("%s : face detection already running", __func__);
        return NO_ERROR;
    }

    mFaceDetectStarted = mSecCamera->setFaceDetect(FACE_DETECTION_ON) >= 0;
    return mFaceDetectStarted ? NO_ERROR : UNKNOWN_ERROR;
}

status_t CameraHardwareSec::stopFaceDetection()
{
    if (!mFaceDetectStarted) {
        ALOGD("%s : face detection isn't running", __func__);
        return NO_ERROR;
    }

    mFaceDetectStarted = !(mSecCamera->setFaceDetect(FACE_DETECTION_OFF) >= 0);
    return !mFaceDetectStarted ? NO_ERROR : UNKNOWN_ERROR;
}

void CameraHardwareSec::stopPreview_l()
{
    ALOGV("%s - start", __func__);

    if (!mPreviewRunning) {
        ALOGD("%s : preview not running, doing nothing", __func__);
        return;
    }

    /* if face detect is running we must stop it and than app
     * must reenable face detection
     */
    if(mFaceDetectStarted) {
        stopFaceDetection();
    }

    mPreviewRunning = false;
    if (!mPreviewStartDeferred) {
        mPreviewCondition.signal();
        /* wait until preview thread is stopped */
        mPreviewStoppedCondition.wait(mPreviewLock);
    } else {
        ALOGD("%s : preview running but deferred, doing nothing", __func__);
    }
}

bool CameraHardwareSec::previewEnabled()
{
    Mutex::Autolock lock(mPreviewLock);
    ALOGV("%s : %d", __func__, mPreviewRunning);
    return mPreviewRunning;
}

// ---------------------------------------------------------------------------

status_t CameraHardwareSec::startRecording()
{
    ALOGV("%s :", __func__);

    Mutex::Autolock lock(mRecordLock);

    RELEASE_MEMORY_BUFFER(mRecordHeap);
    mRecordHeap = mGetMemoryCb(-1, sizeof(struct addrs), kBufferCount, NULL);
    if (!mRecordHeap) {
        ALOGE("ERR(%s): Record heap creation fail", __func__);
        return UNKNOWN_ERROR;
    }

    if (mRecordRunning == false) {
        if (mSecCamera->startRecord() < 0) {
            ALOGE("ERR(%s):Fail on mSecCamera->startRecord()", __func__);
            return UNKNOWN_ERROR;
        }
        mRecordRunning = true;
    }
    return NO_ERROR;
}

void CameraHardwareSec::stopRecording()
{
    ALOGV("%s :", __func__);

    Mutex::Autolock lock(mRecordLock);

    if (mRecordRunning == true) {
        if (mSecCamera->stopRecord() < 0) {
            ALOGE("ERR(%s):Fail on mSecCamera->stopRecord()", __func__);
            return;
        }
        mRecordRunning = false;
    }
}

bool CameraHardwareSec::recordingEnabled()
{
    ALOGV("%s :", __func__);

    Mutex::Autolock lock(mRecordLock);
    return mRecordRunning;
}

void CameraHardwareSec::releaseRecordingFrame(const void *opaque)
{
    struct addrs *addrs = (struct addrs *)opaque;
    mSecCamera->releaseRecordFrame(addrs->buf_index);
}

// ---------------------------------------------------------------------------

int CameraHardwareSec::autoFocusThread()
{
    int count =0;
    int af_status =0 ;

    ALOGV("%s : starting", __func__);

    /* block until we're told to start.  we don't want to use
     * a restartable thread and requestExitAndWait() in cancelAutoFocus()
     * because it would cause deadlock between our callbacks and the
     * caller of cancelAutoFocus() which both want to grab the same lock
     * in CameraServices layer.
     */
    mFocusLock.lock();
    /* check early exit request */
    if (mExitAutoFocusThread) {
        mFocusLock.unlock();
        ALOGV("%s : exiting on request0", __func__);
        return NO_ERROR;
    }
    mFocusCondition.wait(mFocusLock);
    /* check early exit request */
    if (mExitAutoFocusThread) {
        mFocusLock.unlock();
        ALOGV("%s : exiting on request1", __func__);
        return NO_ERROR;
    }
    mFocusLock.unlock();

    ALOGV("%s : calling setAutoFocus", __func__);
    if (mSecCamera->setAutofocus() < 0) {
        ALOGE("ERR(%s):Fail on mSecCamera->setAutofocus()", __func__);
        return UNKNOWN_ERROR;
    }

    af_status = mSecCamera->getAutoFocusResult();

    if (af_status == 0x01) {
        ALOGV("%s : AF Success!!", __func__);
        if (mMsgEnabled & CAMERA_MSG_FOCUS)
            mNotifyCb(CAMERA_MSG_FOCUS, true, 0, mCallbackCookie);
    } else if (af_status == 0x02) {
        ALOGV("%s : AF Cancelled !!", __func__);
        if (mMsgEnabled & CAMERA_MSG_FOCUS) {
            /* CAMERA_MSG_FOCUS only takes a bool.  true for
             * finished and false for failure.  cancel is still
             * considered a true result.
             */
            mNotifyCb(CAMERA_MSG_FOCUS, true, 0, mCallbackCookie);
        }
    } else {
        ALOGV("%s : AF Fail !!", __func__);
        ALOGV("%s : mMsgEnabled = 0x%x", __func__, mMsgEnabled);
        if (mMsgEnabled & CAMERA_MSG_FOCUS)
            mNotifyCb(CAMERA_MSG_FOCUS, false, 0, mCallbackCookie);
    }

    ALOGV("%s : exiting with no error", __func__);
    return NO_ERROR;
}

status_t CameraHardwareSec::autoFocus()
{
    ALOGV("%s :", __func__);
    /* signal autoFocusThread to run once */
    mFocusCondition.signal();
    return NO_ERROR;
}

/* 2009.10.14 by icarus for added interface */
status_t CameraHardwareSec::cancelAutoFocus()
{
    ALOGV("%s :", __func__);

    if (mSecCamera->cancelAutofocus() < 0) {
        ALOGE("ERR(%s):Fail on mSecCamera->cancelAutofocus()", __func__);
        return UNKNOWN_ERROR;
    }

    return NO_ERROR;
}

bool CameraHardwareSec::scaleDownYuv422(char *srcBuf, uint32_t srcWidth, uint32_t srcHeight,
                                        char *dstBuf, uint32_t dstWidth, uint32_t dstHeight)
{
    int32_t step_x, step_y;
    int32_t iXsrc, iXdst;
    int32_t x, y, src_y_start_pos, dst_pos, src_pos;

    if (dstWidth % 2 != 0 || dstHeight % 2 != 0){
        ALOGE("scale_down_yuv422: invalid width, height for scaling");
        return false;
    }

    step_x = srcWidth / dstWidth;
    step_y = srcHeight / dstHeight;

    dst_pos = 0;
    for (uint32_t y = 0; y < dstHeight; y++) {
        src_y_start_pos = (y * step_y * (srcWidth * 2));

        for (uint32_t x = 0; x < dstWidth; x += 2) {
            src_pos = src_y_start_pos + (x * (step_x * 2));

            dstBuf[dst_pos++] = srcBuf[src_pos    ];
            dstBuf[dst_pos++] = srcBuf[src_pos + 1];
            dstBuf[dst_pos++] = srcBuf[src_pos + 2];
            dstBuf[dst_pos++] = srcBuf[src_pos + 3];
        }
    }

    return true;
}

int CameraHardwareSec::pictureThread()
{
    ALOGV("%s - start", __FUNCTION__);

    int             ret = NO_ERROR;
    int             pictureWidth  = 0;
    int             pictureHeight = 0;
    int             frameSize = 0;
    int             postViewWidth = 0;
    int             postViewHeight = 0;
    int             postViewSize = 0;
    int             thumbWidth = 0;
    int             thumbHeight = 0;
    int             thumbSize = 0;
    unsigned int    picturePhyAddr = 0;
    bool            flagShutterCallback = false;

    unsigned char*  jpegData = NULL;
    unsigned int    jpegSize = 0;
    /* * * * * * memory buffers * * * * * */
    sp<MemoryHeapBase>  jpegHeap = NULL;
    sp<MemoryHeapBase>  postviewHeap = NULL;
    sp<MemoryHeapBase>  thumbnailHeap = NULL;
    struct addrs_cap*   addrs;

    mSecCamera->getSnapshotSize(&pictureWidth, &pictureHeight, &frameSize);
    mSecCamera->getPostViewConfig(&postViewWidth, &postViewHeight, &postViewSize);
    mSecCamera->getThumbnailConfig(&thumbWidth, &thumbHeight, &thumbSize);

    addrs = (struct addrs_cap *)mRawHeap->data;
    addrs[0].width  = pictureWidth;
    addrs[0].height = pictureHeight;

    if(mSecCamera->getCameraId() == SecCamera::CAMERA_ID_BACK) {
        ret = mSecCamera->setSnapshotCmd();
        if(ret < 0) {
            ALOGE("ERR(%s):Fail on SecCamera->setSnapshotCmd()", __FUNCTION__);
            goto out;
        }
        
        jpegData = mSecCamera->getJpeg(&jpegSize, &picturePhyAddr);
        if(jpegData == NULL) {
            ALOGE("ERR(%s):Fail on SecCamera->getJpeg()", __FUNCTION__);
            picturePhyAddr = 0;
            ret = UNKNOWN_ERROR;
            goto out;
        }

        ALOGV("jpegSize(%i), picturePhyAddr(%i)", jpegSize, picturePhyAddr);
    } else {
        jpegHeap = new MemoryHeapBase(frameSize);
        postviewHeap = new MemoryHeapBase(postViewSize);
        thumbnailHeap = new MemoryHeapBase(thumbSize);

        if (mSecCamera->getSnapshotAndJpeg((unsigned char*)postviewHeap->base(),
                                           (unsigned char*)jpegHeap->base(), &jpegSize) < 0) {
            ALOGE("ERR(%s):Fail on SecCamera->getSnapshotAndJpeg()", __FUNCTION__);
            goto out;
        }

        if(!scaleDownYuv422((char *)postviewHeap->base(), postViewWidth, postViewHeight,
                            (char *)thumbnailHeap->base(), thumbWidth, thumbHeight)) {
            ALOGE("ERR(%s):Fail on scaleDownYuv422()", __FUNCTION__);
            goto out;
        }
    }

    if((mMsgEnabled & CAMERA_MSG_RAW_IMAGE) && mDataCb) {
        if(mSecCamera->getCameraId() == SecCamera::CAMERA_ID_BACK) {
            if(picturePhyAddr != 0) {
                addrs[0].addr_y = picturePhyAddr;
            }
        } else {
            memcpy(mRawHeap->data, postviewHeap->base(), postViewSize);
        }

        if((mMsgEnabled & CAMERA_MSG_SHUTTER) && mNotifyCb) {
            mNotifyCb(CAMERA_MSG_SHUTTER, 0, 0, mCallbackCookie);
            flagShutterCallback = true;
        }

        mDataCb(CAMERA_MSG_RAW_IMAGE, mRawHeap, 0, NULL, mCallbackCookie);
    }

    if(!flagShutterCallback && ((mMsgEnabled & CAMERA_MSG_SHUTTER) && mNotifyCb)) {
        mNotifyCb(CAMERA_MSG_SHUTTER, 0, 0, mCallbackCookie);
    }

    if((mMsgEnabled & CAMERA_MSG_COMPRESSED_IMAGE) && mDataCb) {
        camera_memory_t* jpegMem = NULL;
        if(jpegData != NULL) {
            jpegMem = mGetMemoryCb(-1, jpegSize, 1, 0);
            memcpy(jpegMem->data, jpegData, jpegSize);
        } else {
            sp<MemoryHeapBase> exifHeap = new MemoryHeapBase(EXIF_FILE_SIZE + JPG_STREAM_BUF_SIZE);
            int jpegExifSize = mSecCamera->getExif((unsigned char *)exifHeap->base(),
                    (unsigned char *)thumbnailHeap->base());
            if (jpegExifSize < 0) {
                ret = UNKNOWN_ERROR;
                ALOGE("ERR(%s):Fail on jpegExifSize < 0", __FUNCTION__);
                goto out;
            }
            
            jpegMem = mGetMemoryCb(-1, jpegSize + jpegExifSize, 1, 0);
            uint8_t *ptr = (uint8_t *) jpegMem->data;
            memcpy(ptr, jpegHeap->base(), 2); ptr += 2;
            memcpy(ptr, exifHeap->base(), jpegExifSize); ptr += jpegExifSize;
            memcpy(ptr, (uint8_t *) jpegHeap->base() + 2, jpegSize - 2);
        }

        mDataCb(CAMERA_MSG_COMPRESSED_IMAGE, jpegMem, 0, NULL, mCallbackCookie);

        RELEASE_MEMORY_BUFFER(jpegMem);
    }

out:
    mStateLock.lock();
    mCaptureInProgress = false;
    mStateLock.unlock();
    
    ALOGV("%s - end", __FUNCTION__);

    return ret;
}

status_t CameraHardwareSec::takePicture()
{
    ALOGV("%s :", __func__);

    stopPreview();

    Mutex::Autolock lock(mStateLock);
    if (mCaptureInProgress) {
        ALOGE("%s : capture already in progress", __func__);
        return INVALID_OPERATION;
    }

    if (!mRawHeap) {
        int rawHeapSize = mPostViewSize;
        ALOGV("mRawHeap : MemoryHeapBase(previewHeapSize(%d))", rawHeapSize);
        mRawHeap = mGetMemoryCb(-1, rawHeapSize, 1, 0);
        if (!mRawHeap) {
            ALOGE("ERR(%s): Raw heap creation fail", __func__);
        }
    }

    if (mPictureThread->run("CameraPictureThread", PRIORITY_DEFAULT) != NO_ERROR) {
        ALOGE("%s : couldn't run picture thread", __func__);
        return INVALID_OPERATION;
    }
    mCaptureInProgress = true;

    return NO_ERROR;
}

status_t CameraHardwareSec::cancelPicture()
{
    mPictureThread->requestExitAndWait();

    return NO_ERROR;
}

/*
status_t CameraHardwareSec::dump(int fd, const Vector<String16>& args) const
{
    const size_t SIZE = 256;
    char buffer[SIZE];
    String8 result;

    if (mSecCamera != 0) {
        mSecCamera->dump(fd, args);
        mParameters.dump(fd, args);
        mInternalParameters.dump(fd, args);
        snprintf(buffer, 255, " preview running(%s)\n", mPreviewRunning?"true": "false");
        result.append(buffer);
    } else {
        result.append("No camera client yet.\n");
    }
    write(fd, result.string(), result.size());
    return NO_ERROR;
}
*/

bool CameraHardwareSec::isSupportedPreviewSize(const int width,
                                               const int height) const
{
    unsigned int i;

    for (i = 0; i < mSupportedPreviewSizes.size(); i++) {
        if (mSupportedPreviewSizes[i].width == width &&
                mSupportedPreviewSizes[i].height == height)
            return true;
    }

    return false;
}

void CameraHardwareSec::putParameters(char *parms)
{
    free(parms);
}

status_t CameraHardwareSec::setParameters(const char *parameters)
{
    ALOGV("%s :", __func__);

    CameraParameters params;

    String8 str_params(parameters);
    params.unflatten(str_params);

    return setParameters(params);
}

status_t CameraHardwareSec::setParameters(const CameraParameters& params)
{
    ALOGV("%s :", __func__);

    status_t ret = NO_ERROR;

    /* if someone calls us while picture thread is running, it could screw
     * up the sensor quite a bit so return error.  we can't wait because
     * that would cause deadlock with the callbacks
     */
    mStateLock.lock();
    if (mCaptureInProgress) {
        mStateLock.unlock();
        ALOGE("%s : capture in progress, not allowed", __func__);
        return UNKNOWN_ERROR;
    }
    mStateLock.unlock();

    // preview size
    int new_preview_width  = 0;
    int new_preview_height = 0;
    params.getPreviewSize(&new_preview_width, &new_preview_height);
    const char *new_str_preview_format = params.getPreviewFormat();
    ALOGV("%s : new_preview_width x new_preview_height = %dx%d, format = %s",
         __func__, new_preview_width, new_preview_height, new_str_preview_format);

    if (0 < new_preview_width && 0 < new_preview_height &&
            new_str_preview_format != NULL &&
            isSupportedPreviewSize(new_preview_width, new_preview_height)) {
        int new_preview_format = 0;

        if (!strcmp(new_str_preview_format,
                         SecCameraParameters::PIXEL_FORMAT_YUV420SP))
            new_preview_format = V4L2_PIX_FMT_YUV420;
        else if (!strcmp(new_str_preview_format,
                         SecCameraParameters::PIXEL_FORMAT_YUV420P))
            new_preview_format = V4L2_PIX_FMT_YUV420;
        else
            new_preview_format = V4L2_PIX_FMT_YUV420; //for 3rd party

        if (mSecCamera->setPreviewSize(new_preview_width, new_preview_height, new_preview_format) < 0) {
            ALOGE("ERR(%s):Fail on mSecCamera->setPreviewSize(width(%d), height(%d), format(%d))",
                    __func__, new_preview_width, new_preview_height, new_preview_format);
            ret = UNKNOWN_ERROR;
        } else {
            mParameters.setPreviewSize(new_preview_width, new_preview_height);
            mParameters.setPreviewFormat(new_str_preview_format);
        }
    } else {
        ALOGE("%s: Invalid preview size(%dx%d)",
                __func__, new_preview_width, new_preview_height);

        ret = INVALID_OPERATION;
    }

    int new_picture_width  = 0;
    int new_picture_height = 0;

    params.getPictureSize(&new_picture_width, &new_picture_height);
    ALOGV("%s : new_picture_width x new_picture_height = %dx%d", __func__, new_picture_width, new_picture_height);
    if (0 < new_picture_width && 0 < new_picture_height) {
        if (mSecCamera->setSnapshotSize(new_picture_width, new_picture_height) < 0) {
            ALOGE("ERR(%s):Fail on mSecCamera->setSnapshotSize(width(%d), height(%d))",
                    __func__, new_picture_width, new_picture_height);
            ret = UNKNOWN_ERROR;
        } else {
            mParameters.setPictureSize(new_picture_width, new_picture_height);
        }
    }

    // picture format
    const char *new_str_picture_format = params.getPictureFormat();
    ALOGV("%s : new_str_picture_format %s", __func__, new_str_picture_format);
    if (new_str_picture_format != NULL) {
        int new_picture_format = 0;

        if (!strcmp(new_str_picture_format, SecCameraParameters::PIXEL_FORMAT_RGB565))
            new_picture_format = V4L2_PIX_FMT_RGB565;
        else if (!strcmp(new_str_picture_format, SecCameraParameters::PIXEL_FORMAT_YUV420SP))
            new_picture_format = V4L2_PIX_FMT_NV21;
        else if (!strcmp(new_str_picture_format, "yuv420sp_custom"))
            new_picture_format = V4L2_PIX_FMT_NV12T;
        else if (!strcmp(new_str_picture_format, "yuv420p"))
            new_picture_format = V4L2_PIX_FMT_YUV420;
        else if (!strcmp(new_str_picture_format, "yuv422i"))
            new_picture_format = V4L2_PIX_FMT_YUYV;
        else if (!strcmp(new_str_picture_format, "uyv422i_custom")) //Zero copy UYVY format
            new_picture_format = V4L2_PIX_FMT_UYVY;
        else if (!strcmp(new_str_picture_format, "uyv422i")) //Non-zero copy UYVY format
            new_picture_format = V4L2_PIX_FMT_UYVY;
        else if (!strcmp(new_str_picture_format, SecCameraParameters::PIXEL_FORMAT_JPEG))
            new_picture_format = V4L2_PIX_FMT_YUYV;
        else if (!strcmp(new_str_picture_format, "yuv422p"))
            new_picture_format = V4L2_PIX_FMT_YUV422P;
        else
            new_picture_format = V4L2_PIX_FMT_NV21; //for 3rd party

        if (mSecCamera->setSnapshotPixelFormat(new_picture_format) < 0) {
            ALOGE("ERR(%s):Fail on mSecCamera->setSnapshotPixelFormat(format(%d))", __func__, new_picture_format);
            ret = UNKNOWN_ERROR;
        } else {
            mParameters.setPictureFormat(new_str_picture_format);
        }
    }

    //JPEG image quality
    int new_jpeg_quality = params.getInt(SecCameraParameters::KEY_JPEG_QUALITY);
    ALOGV("%s : new_jpeg_quality %d", __func__, new_jpeg_quality);
    /* we ignore bad values */
    if (new_jpeg_quality >=1 && new_jpeg_quality <= 100) {
        if (mSecCamera->setJpegQuality(new_jpeg_quality) < 0) {
            ALOGE("ERR(%s):Fail on mSecCamera->setJpegQuality(quality(%d))", __func__, new_jpeg_quality);
            ret = UNKNOWN_ERROR;
        } else {
            mParameters.set(SecCameraParameters::KEY_JPEG_QUALITY, new_jpeg_quality);
        }
    }

    // JPEG thumbnail size
    int new_jpeg_thumbnail_width = params.getInt(SecCameraParameters::KEY_JPEG_THUMBNAIL_WIDTH);
    int new_jpeg_thumbnail_height= params.getInt(SecCameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT);
    if (0 <= new_jpeg_thumbnail_width && 0 <= new_jpeg_thumbnail_height) {
        if (mSecCamera->setJpegThumbnailSize(new_jpeg_thumbnail_width, new_jpeg_thumbnail_height) < 0) {
            ALOGE("ERR(%s):Fail on mSecCamera->setJpegThumbnailSize(width(%d), height(%d))", __func__, new_jpeg_thumbnail_width, new_jpeg_thumbnail_height);
            ret = UNKNOWN_ERROR;
        } else {
            mParameters.set(SecCameraParameters::KEY_JPEG_THUMBNAIL_WIDTH, new_jpeg_thumbnail_width);
            mParameters.set(SecCameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT, new_jpeg_thumbnail_height);
        }
    }

    // frame rate
    int new_frame_rate = params.getPreviewFrameRate();
    /* ignore any fps request, we're determine fps automatically based
     * on scene mode.  don't return an error because it causes CTS failure.
     */
    if (new_frame_rate != mParameters.getPreviewFrameRate()) {
        ALOGW("WARN(%s): request for preview frame %d not allowed, != %d\n",
             __func__, new_frame_rate, mParameters.getPreviewFrameRate());
    }

    // rotation
    int new_rotation = params.getInt(SecCameraParameters::KEY_ROTATION);
    ALOGV("%s : new_rotation %d", __func__, new_rotation);
    if (0 <= new_rotation) {
        ALOGV("%s : set orientation:%d\n", __func__, new_rotation);
        if (mSecCamera->setExifOrientationInfo(new_rotation) < 0) {
            ALOGE("ERR(%s):Fail on mSecCamera->setExifOrientationInfo(%d)", __func__, new_rotation);
            ret = UNKNOWN_ERROR;
        } else {
            mParameters.set(SecCameraParameters::KEY_ROTATION, new_rotation);
        }
    }

    // zoom
    int new_zoom = params.getInt(SecCameraParameters::KEY_ZOOM);
    int max_zoom = params.getInt(SecCameraParameters::KEY_MAX_ZOOM);
    ALOGV("%s : new_zoom %d", __func__, new_zoom);
    if (0 <= new_zoom && new_zoom <= max_zoom) {
        ALOGV("%s : set zoom:%d\n", __func__, new_zoom);
        if (mSecCamera->setZoom(new_zoom) < 0) {
            ALOGE("ERR(%s):Fail on mSecCamera->setZoom(%d)", __func__, new_zoom);
            ret = UNKNOWN_ERROR;
        } else {
            mParameters.set(SecCameraParameters::KEY_ZOOM, new_zoom);
        }
    }

    // brightness
    int new_exposure_compensation = params.getInt(SecCameraParameters::KEY_EXPOSURE_COMPENSATION);
    int max_exposure_compensation = params.getInt(SecCameraParameters::KEY_MAX_EXPOSURE_COMPENSATION);
    int min_exposure_compensation = params.getInt(SecCameraParameters::KEY_MIN_EXPOSURE_COMPENSATION);
    ALOGV("%s : new_exposure_compensation %d", __func__, new_exposure_compensation);
    if ((min_exposure_compensation <= new_exposure_compensation) &&
        (max_exposure_compensation >= new_exposure_compensation)) {
        if (mSecCamera->setBrightness(new_exposure_compensation) < 0) {
            ALOGE("ERR(%s):Fail on mSecCamera->setBrightness(brightness(%d))", __func__, new_exposure_compensation);
            ret = UNKNOWN_ERROR;
        } else {
            mParameters.set(SecCameraParameters::KEY_EXPOSURE_COMPENSATION, new_exposure_compensation);
        }
    }

    // contrast
    int new_contrast = params.getInt(SecCameraParameters::KEY_CONTRAST);
    int max_contrast = params.getInt(SecCameraParameters::KEY_MAX_CONTRAST);
    int min_contrast = params.getInt(SecCameraParameters::KEY_MIN_CONTRAST);
    ALOGV("%s : new_exposure_compensation %d", __func__, new_exposure_compensation);
    if ((min_contrast <= new_contrast) &&
        (max_contrast >= new_contrast)) {
        if (mSecCamera->setContrast(new_contrast) < 0) {
            ALOGE("ERR(%s):Fail on mSecCamera->setContrast(brightness(%d))", __func__, new_exposure_compensation);
            ret = UNKNOWN_ERROR;
        } else {
            mParameters.set(SecCameraParameters::KEY_CONTRAST, new_contrast);
        }
    }

    // whitebalance
    const char *new_white_str = params.get(SecCameraParameters::KEY_WHITE_BALANCE);
    ALOGV("%s : new_white_str %s", __func__, new_white_str);
    if (new_white_str != NULL) {
        int new_white = -1;

        if (!strcmp(new_white_str, SecCameraParameters::WHITE_BALANCE_AUTO))
            new_white = WHITE_BALANCE_AUTO;
        else if (!strcmp(new_white_str,
                         SecCameraParameters::WHITE_BALANCE_DAYLIGHT))
            new_white = WHITE_BALANCE_SUNNY;
        else if (!strcmp(new_white_str,
                         SecCameraParameters::WHITE_BALANCE_CLOUDY_DAYLIGHT))
            new_white = WHITE_BALANCE_CLOUDY;
        else if (!strcmp(new_white_str,
                         SecCameraParameters::WHITE_BALANCE_FLUORESCENT))
            new_white = WHITE_BALANCE_FLUORESCENT;
        else if (!strcmp(new_white_str,
                         SecCameraParameters::WHITE_BALANCE_INCANDESCENT))
            new_white = WHITE_BALANCE_TUNGSTEN;
        else {
            ALOGE("ERR(%s):Invalid white balance(%s)", __func__, new_white_str); //twilight, shade, warm_flourescent
            ret = UNKNOWN_ERROR;
        }

        if (0 <= new_white) {
            if (mSecCamera->setWhiteBalance(new_white) < 0) {
                ALOGE("ERR(%s):Fail on mSecCamera->setWhiteBalance(white(%d))", __func__, new_white);
                ret = UNKNOWN_ERROR;
            } else {
                mParameters.set(SecCameraParameters::KEY_WHITE_BALANCE, new_white_str);
            }
        }
    }

    // iso mode
    const char *new_iso_str = params.get(SecCameraParameters::KEY_ISO);
    if (new_iso_str != NULL) {
        int  new_iso = -1;

        if (!strcmp(new_iso_str, SecCameraParameters::ISO_AUTO)) {
            new_iso = ISO_AUTO;
        } else {
            if (!strcmp(new_iso_str, SecCameraParameters::ISO_50)) {
                new_iso = ISO_50;
            } else if (!strcmp(new_iso_str, SecCameraParameters::ISO_100)) {
                new_iso = ISO_100;
            } else if (!strcmp(new_iso_str, SecCameraParameters::ISO_200)) {
                new_iso = ISO_200;
            } else if (!strcmp(new_iso_str, SecCameraParameters::ISO_400)) {
                new_iso = ISO_400;
            } else if (!strcmp(new_iso_str, SecCameraParameters::ISO_800)) {
                new_iso = ISO_800;
            } else if (!strcmp(new_iso_str, SecCameraParameters::ISO_1600)) {
                new_iso = ISO_1600;
            } else if (!strcmp(new_iso_str, SecCameraParameters::ISO_SPORTS)) {
                new_iso = ISO_SPORTS;
            } else if (!strcmp(new_iso_str, SecCameraParameters::ISO_NIGHT)) {
                new_iso = ISO_NIGHT;
            } else if (!strcmp(new_iso_str, SecCameraParameters::ISO_MOVIE)) {
                new_iso = ISO_MOVIE;
            } else {
                ALOGE("%s::unmatched iso(%s)", __func__, new_iso_str);
                ret = UNKNOWN_ERROR;
            }
        }
        if (0 <= new_iso) {
            if (mSecCamera->setISO(new_iso) < 0) {
                ALOGE("%s::mSecCamera->setISO(%d) fail", __func__, new_iso);
                ret = UNKNOWN_ERROR;
            } else {
                mParameters.set(SecCameraParameters::KEY_ISO, new_iso_str);
            }
        }
    }

    // scene mode
    const char *new_scene_mode_str = params.get(SecCameraParameters::KEY_SCENE_MODE);
    const char *current_scene_mode_str = mParameters.get(SecCameraParameters::KEY_SCENE_MODE);

    // fps range
    int new_min_fps = 0;
    int new_max_fps = 0;
    int current_min_fps, current_max_fps;
    params.getPreviewFpsRange(&new_min_fps, &new_max_fps);
    mParameters.getPreviewFpsRange(&current_min_fps, &current_max_fps);
    /* our fps range is determined by the sensor, reject any request
     * that isn't exactly what we're already at.
     * but the check is performed when requesting only changing fps range
     */
    if (new_scene_mode_str && current_scene_mode_str) {
        if (!strcmp(new_scene_mode_str, current_scene_mode_str)) {
            if ((new_min_fps != current_min_fps) || (new_max_fps != current_max_fps)) {
                ALOGW("%s : requested new_min_fps = %d, new_max_fps = %d not allowed",
                        __func__, new_min_fps, new_max_fps);
                ALOGE("%s : current_min_fps = %d, current_max_fps = %d",
                        __func__, current_min_fps, current_max_fps);
                ret = UNKNOWN_ERROR;
            }
        }
    } else {
        /* Check basic validation if scene mode is different */
        if ((new_min_fps > new_max_fps) ||
            (new_min_fps < 0) || (new_max_fps < 0))
        ret = UNKNOWN_ERROR;
    }

    // focus mode
    const char *new_focus_mode_str = params.get(SecCameraParameters::KEY_FOCUS_MODE);
    if (new_focus_mode_str != NULL) {
        int  new_focus_mode = -1;

        if (!strcmp(new_focus_mode_str,
                    SecCameraParameters::FOCUS_MODE_AUTO)) {
            new_focus_mode = FOCUS_MODE_AUTO;
            mParameters.set(SecCameraParameters::KEY_FOCUS_DISTANCES,
                            BACK_CAMERA_AUTO_FOCUS_DISTANCES_STR);
        }
        else if (!strcmp(new_focus_mode_str,
                         SecCameraParameters::FOCUS_MODE_MACRO)) {
            new_focus_mode = FOCUS_MODE_MACRO;
            mParameters.set(SecCameraParameters::KEY_FOCUS_DISTANCES,
                            BACK_CAMERA_MACRO_FOCUS_DISTANCES_STR);
        }
        else if (!strcmp(new_focus_mode_str,
                         SecCameraParameters::FOCUS_MODE_INFINITY)) {
            new_focus_mode = FOCUS_MODE_INFINITY;
            mParameters.set(SecCameraParameters::KEY_FOCUS_DISTANCES,
                            BACK_CAMERA_INFINITY_FOCUS_DISTANCES_STR);
        }
        else {
            ALOGE("%s::unmatched focus_mode(%s)", __func__, new_focus_mode_str);
            ret = UNKNOWN_ERROR;
        }

        if (0 <= new_focus_mode) {
            if (mSecCamera->setFocusMode(new_focus_mode) < 0) {
                ALOGE("%s::mSecCamera->setFocusMode(%d) fail", __func__, new_focus_mode);
                ret = UNKNOWN_ERROR;
            } else {
                mParameters.set(SecCameraParameters::KEY_FOCUS_MODE, new_focus_mode_str);
            }
        }
    }

    if (new_scene_mode_str != NULL) {
        int  new_scene_mode = -1;

        const char *new_flash_mode_str = params.get(SecCameraParameters::KEY_FLASH_MODE);

        // fps range is (15000,30000) by default.
        mParameters.set(SecCameraParameters::KEY_SUPPORTED_PREVIEW_FPS_RANGE, "(15000,30000)");
        mParameters.set(SecCameraParameters::KEY_PREVIEW_FPS_RANGE,
                "15000,30000");

        if (!strcmp(new_scene_mode_str, SecCameraParameters::SCENE_MODE_AUTO)) {
            new_scene_mode = SCENE_MODE_NONE;
        } else {
            // defaults for non-auto scene modes
            if (mSecCamera->getCameraId() == SecCamera::CAMERA_ID_BACK) {
                new_focus_mode_str = SecCameraParameters::FOCUS_MODE_AUTO;
            }
            new_flash_mode_str = SecCameraParameters::FLASH_MODE_OFF;

            if (!strcmp(new_scene_mode_str,
                       SecCameraParameters::SCENE_MODE_PORTRAIT)) {
                new_scene_mode = SCENE_MODE_PORTRAIT;
                new_flash_mode_str = SecCameraParameters::FLASH_MODE_AUTO;
            } else if (!strcmp(new_scene_mode_str,
                               SecCameraParameters::SCENE_MODE_LANDSCAPE)) {
                new_scene_mode = SCENE_MODE_LANDSCAPE;
            } else if (!strcmp(new_scene_mode_str,
                               SecCameraParameters::SCENE_MODE_SPORTS)) {
                new_scene_mode = SCENE_MODE_SPORTS;
            } else if (!strcmp(new_scene_mode_str,
                               SecCameraParameters::SCENE_MODE_PARTY)) {
                new_scene_mode = SCENE_MODE_PARTY_INDOOR;
                new_flash_mode_str = SecCameraParameters::FLASH_MODE_AUTO;
            } else if ((!strcmp(new_scene_mode_str,
                                SecCameraParameters::SCENE_MODE_BEACH)) ||
                        (!strcmp(new_scene_mode_str,
                                 SecCameraParameters::SCENE_MODE_SNOW))) {
                new_scene_mode = SCENE_MODE_BEACH_SNOW;
            } else if (!strcmp(new_scene_mode_str,
                               SecCameraParameters::SCENE_MODE_SUNSET)) {
                new_scene_mode = SCENE_MODE_SUNSET;
            } else if (!strcmp(new_scene_mode_str,
                               SecCameraParameters::SCENE_MODE_NIGHT)) {
                new_scene_mode = SCENE_MODE_NIGHTSHOT;
                mParameters.set(SecCameraParameters::KEY_SUPPORTED_PREVIEW_FPS_RANGE, "(4000,30000)");
                mParameters.set(SecCameraParameters::KEY_PREVIEW_FPS_RANGE,
                                "4000,30000");
            } else if (!strcmp(new_scene_mode_str,
                               SecCameraParameters::SCENE_MODE_FIREWORKS)) {
                new_scene_mode = SCENE_MODE_FIREWORKS;
            } else if (!strcmp(new_scene_mode_str,
                               SecCameraParameters::SCENE_MODE_CANDLELIGHT)) {
                new_scene_mode = SCENE_MODE_CANDLE_LIGHT;
            } else {
                ALOGE("%s::unmatched scene_mode(%s)",
                        __func__, new_scene_mode_str); //action, night-portrait, theatre, steadyphoto
                ret = UNKNOWN_ERROR;
            }
        }

        // flash..
        if (new_flash_mode_str != NULL) {
            int  new_flash_mode = -1;

            if (!strcmp(new_flash_mode_str, SecCameraParameters::FLASH_MODE_OFF))
                new_flash_mode = FLASH_MODE_OFF;
            else if (!strcmp(new_flash_mode_str, SecCameraParameters::FLASH_MODE_AUTO))
                new_flash_mode = FLASH_MODE_AUTO;
            else if (!strcmp(new_flash_mode_str, SecCameraParameters::FLASH_MODE_ON))
                new_flash_mode = FLASH_MODE_ON;
            else if (!strcmp(new_flash_mode_str, SecCameraParameters::FLASH_MODE_TORCH))
                new_flash_mode = FLASH_MODE_TORCH;
            else {
                ALOGE("%s::unmatched flash_mode(%s)", __func__, new_flash_mode_str); //red-eye
                ret = UNKNOWN_ERROR;
            }
            if (0 <= new_flash_mode) {
                if (mSecCamera->setFlashMode(new_flash_mode) < 0) {
                    ALOGE("%s::mSecCamera->setFlashMode(%d) fail", __func__, new_flash_mode);
                    ret = UNKNOWN_ERROR;
                } else {
                    mParameters.set(SecCameraParameters::KEY_FLASH_MODE, new_flash_mode_str);
                }
            }
        }

        //  scene..
        if (0 <= new_scene_mode) {
            if (mSecCamera->setSceneMode(new_scene_mode) < 0) {
                ALOGE("%s::mSecCamera->setSceneMode(%d) fail", __func__, new_scene_mode);
                ret = UNKNOWN_ERROR;
            } else {
                mParameters.set(SecCameraParameters::KEY_SCENE_MODE, new_scene_mode_str);
            }
        }
    }

    // ---------------------------------------------------------------------------

    // image effect
    const char *new_image_effect_str = params.get(SecCameraParameters::KEY_EFFECT);
    if (new_image_effect_str != NULL) {
        int  new_image_effect = -1;

        if (!strcmp(new_image_effect_str, SecCameraParameters::EFFECT_NONE))
            new_image_effect = IMAGE_EFFECT_NONE;
        else if (!strcmp(new_image_effect_str, SecCameraParameters::EFFECT_MONO))
            new_image_effect = IMAGE_EFFECT_BNW;
        else if (!strcmp(new_image_effect_str, SecCameraParameters::EFFECT_SEPIA))
            new_image_effect = IMAGE_EFFECT_SEPIA;
        else if (!strcmp(new_image_effect_str, SecCameraParameters::EFFECT_ANTIQUE))
            new_image_effect = IMAGE_EFFECT_ANTIQUE;
        else if (!strcmp(new_image_effect_str, SecCameraParameters::EFFECT_SHARPEN))
            new_image_effect = IMAGE_EFFECT_SHARPEN;
        else if (!strcmp(new_image_effect_str, SecCameraParameters::EFFECT_AQUA))
            new_image_effect = IMAGE_EFFECT_AQUA;
        else if (!strcmp(new_image_effect_str, SecCameraParameters::EFFECT_NEGATIVE))
            new_image_effect = IMAGE_EFFECT_NEGATIVE;
        else {
            //posterize, whiteboard, blackboard, solarize
            ALOGE("ERR(%s):Invalid effect(%s)", __func__, new_image_effect_str);
            ret = UNKNOWN_ERROR;
        }

        if (new_image_effect >= 0) {
            if (mSecCamera->setImageEffect(new_image_effect) < 0) {
                ALOGE("ERR(%s):Fail on mSecCamera->setImageEffect(effect(%d))", __func__, new_image_effect);
                ret = UNKNOWN_ERROR;
            } else {
                const char *old_image_effect_str = mParameters.get(SecCameraParameters::KEY_EFFECT);

                if (old_image_effect_str) {
                    if (strcmp(old_image_effect_str, new_image_effect_str)) {
                        setSkipFrame(EFFECT_SKIP_FRAME);
                    }
                }

                mParameters.set(SecCameraParameters::KEY_EFFECT, new_image_effect_str);
            }
        }
    }

    //vt mode
    int new_vtmode = mInternalParameters.getInt("vtmode");
    if (0 <= new_vtmode) {
        if (mSecCamera->setVTmode(new_vtmode) < 0) {
            ALOGE("ERR(%s):Fail on mSecCamera->setVTMode(%d)", __func__, new_vtmode);
            ret = UNKNOWN_ERROR;
        }
    }

    //WDR
    int new_wdr = mInternalParameters.getInt("wdr");
    if (0 <= new_wdr) {
        if (mSecCamera->setWDR(new_wdr) < 0) {
            ALOGE("ERR(%s):Fail on mSecCamera->setWDR(%d)", __func__, new_wdr);
            ret = UNKNOWN_ERROR;
        }
    }

    //anti shake
    int new_anti_shake = mInternalParameters.getInt("anti-shake");
    if (0 <= new_anti_shake) {
        if (mSecCamera->setAntiShake(new_anti_shake) < 0) {
            ALOGE("ERR(%s):Fail on mSecCamera->setWDR(%d)", __func__, new_anti_shake);
            ret = UNKNOWN_ERROR;
        }
    }

    // gps latitude
    const char *new_gps_latitude_str = params.get(SecCameraParameters::KEY_GPS_LATITUDE);
    if (mSecCamera->setGPSLatitude(new_gps_latitude_str) < 0) {
        ALOGE("%s::mSecCamera->setGPSLatitude(%s) fail", __func__, new_gps_latitude_str);
        ret = UNKNOWN_ERROR;
    } else {
        if (new_gps_latitude_str) {
            mParameters.set(SecCameraParameters::KEY_GPS_LATITUDE, new_gps_latitude_str);
        } else {
            mParameters.remove(SecCameraParameters::KEY_GPS_LATITUDE);
        }
    }

    // gps longitude
    const char *new_gps_longitude_str = params.get(SecCameraParameters::KEY_GPS_LONGITUDE);
    if (mSecCamera->setGPSLongitude(new_gps_longitude_str) < 0) {
        ALOGE("%s::mSecCamera->setGPSLongitude(%s) fail", __func__, new_gps_longitude_str);
        ret = UNKNOWN_ERROR;
    } else {
        if (new_gps_longitude_str) {
            mParameters.set(SecCameraParameters::KEY_GPS_LONGITUDE, new_gps_longitude_str);
        } else {
            mParameters.remove(SecCameraParameters::KEY_GPS_LONGITUDE);
        }
    }

    // gps altitude
    const char *new_gps_altitude_str = params.get(SecCameraParameters::KEY_GPS_ALTITUDE);
    if (mSecCamera->setGPSAltitude(new_gps_altitude_str) < 0) {
        ALOGE("%s::mSecCamera->setGPSAltitude(%s) fail", __func__, new_gps_altitude_str);
        ret = UNKNOWN_ERROR;
    } else {
        if (new_gps_altitude_str) {
            mParameters.set(SecCameraParameters::KEY_GPS_ALTITUDE, new_gps_altitude_str);
        } else {
            mParameters.remove(SecCameraParameters::KEY_GPS_ALTITUDE);
        }
    }

    // gps timestamp
    const char *new_gps_timestamp_str = params.get(SecCameraParameters::KEY_GPS_TIMESTAMP);
    if (mSecCamera->setGPSTimeStamp(new_gps_timestamp_str) < 0) {
        ALOGE("%s::mSecCamera->setGPSTimeStamp(%s) fail", __func__, new_gps_timestamp_str);
        ret = UNKNOWN_ERROR;
    } else {
        if (new_gps_timestamp_str) {
            mParameters.set(SecCameraParameters::KEY_GPS_TIMESTAMP, new_gps_timestamp_str);
        } else {
            mParameters.remove(SecCameraParameters::KEY_GPS_TIMESTAMP);
        }
    }

    // gps processing method
    const char *new_gps_processing_method_str = params.get(SecCameraParameters::KEY_GPS_PROCESSING_METHOD);
    if (mSecCamera->setGPSProcessingMethod(new_gps_processing_method_str) < 0) {
        ALOGE("%s::mSecCamera->setGPSProcessingMethod(%s) fail", __func__, new_gps_processing_method_str);
        ret = UNKNOWN_ERROR;
    } else {
        if (new_gps_processing_method_str) {
            mParameters.set(SecCameraParameters::KEY_GPS_PROCESSING_METHOD, new_gps_processing_method_str);
        } else {
            mParameters.remove(SecCameraParameters::KEY_GPS_PROCESSING_METHOD);
        }
    }

    // focus areas
    const char *new_focus_area = params.get(SecCameraParameters::KEY_FOCUS_AREAS);
    if (new_focus_area != NULL) {
        ALOGV("focus area: %s", new_focus_area);
        SecCameraArea area(new_focus_area);

        if(!area.isDummy()) {
            int width, height, frame_size;
            mSecCamera->getPreviewSize(&width, &height, &frame_size);

            int x, y;
            area.getXY(&x, &y);

            x = (x * width) / 2000;
            y = (y * height) / 2000;

            ALOGV("area=%s, x=%i, y=%i", area.toString8().string(), x, y);
            if(mSecCamera->setObjectPosition(x, y) < 0) {
                ALOGE("ERR(%s):Fail on mSecCamera->setObjectPosition(%s)", __func__, new_focus_area);
                ret = UNKNOWN_ERROR;
            }
        }

        int val = area.isDummy() ? 0 : 1;
        if(mSecCamera->setTouchAFStartStop(val) < 0) {
            ALOGE("ERR(%s):Fail on mSecCamera->setTouchAFStartStop(%d)", __func__, val);
            ret = UNKNOWN_ERROR;
        }
    }

    // Recording size
    int new_recording_width = mInternalParameters.getInt("recording-size-width");
    int new_recording_height= mInternalParameters.getInt("recording-size-height");
    if (0 < new_recording_width && 0 < new_recording_height) {
        if (mSecCamera->setRecordingSize(new_recording_width, new_recording_height) < 0) {
            ALOGE("ERR(%s):Fail on mSecCamera->setRecordingSize(width(%d), height(%d))", __func__, new_recording_width, new_recording_height);
            ret = UNKNOWN_ERROR;
        }
    } else {
        if (mSecCamera->setRecordingSize(new_preview_width, new_preview_height) < 0) {
            ALOGE("ERR(%s):Fail on mSecCamera->setRecordingSize(width(%d), height(%d))", __func__, new_preview_width, new_preview_height);
            ret = UNKNOWN_ERROR;
        }
    }

    //gamma
    const char *new_gamma_str = mInternalParameters.get("video_recording_gamma");
    if (new_gamma_str != NULL) {
        int new_gamma = -1;
        if (!strcmp(new_gamma_str, "off"))
            new_gamma = GAMMA_OFF;
        else if (!strcmp(new_gamma_str, "on"))
            new_gamma = GAMMA_ON;
        else {
            ALOGE("%s::unmatched gamma(%s)", __func__, new_gamma_str);
            ret = UNKNOWN_ERROR;
        }

        if (0 <= new_gamma) {
            if (mSecCamera->setGamma(new_gamma) < 0) {
                ALOGE("%s::mSecCamera->setGamma(%d) fail", __func__, new_gamma);
                ret = UNKNOWN_ERROR;
            }
        }
    }

    //slow ae
    const char *new_slow_ae_str = mInternalParameters.get("slow_ae");
    if (new_slow_ae_str != NULL) {
        int new_slow_ae = -1;

        if (!strcmp(new_slow_ae_str, "off"))
            new_slow_ae = SLOW_AE_OFF;
        else if (!strcmp(new_slow_ae_str, "on"))
            new_slow_ae = SLOW_AE_ON;
        else {
            ALOGE("%s::unmatched slow_ae(%s)", __func__, new_slow_ae_str);
            ret = UNKNOWN_ERROR;
        }

        if (0 <= new_slow_ae) {
            if (mSecCamera->setSlowAE(new_slow_ae) < 0) {
                ALOGE("%s::mSecCamera->setSlowAE(%d) fail", __func__, new_slow_ae);
                ret = UNKNOWN_ERROR;
            }
        }
    }

    /*Camcorder fix fps*/
    int new_sensor_mode = mInternalParameters.getInt("cam_mode");
    if (0 <= new_sensor_mode) {
        if (mSecCamera->setSensorMode(new_sensor_mode) < 0) {
            ALOGE("ERR(%s):Fail on mSecCamera->setSensorMode(%d)", __func__, new_sensor_mode);
            ret = UNKNOWN_ERROR;
        }
    } else {
        new_sensor_mode=0;
    }

    /*Shot mode*/
    int new_shot_mode = mInternalParameters.getInt("shot_mode");
    if (0 <= new_shot_mode) {
        if (mSecCamera->setShotMode(new_shot_mode) < 0) {
            ALOGE("ERR(%s):Fail on mSecCamera->setShotMode(%d)", __func__, new_shot_mode);
            ret = UNKNOWN_ERROR;
        }
    } else {
        new_shot_mode=0;
    }

    //blur for Video call
    int new_blur_level = mInternalParameters.getInt("blur");
    if (0 <= new_blur_level) {
        if (mSecCamera->setBlur(new_blur_level) < 0) {
            ALOGE("ERR(%s):Fail on mSecCamera->setBlur(%d)", __func__, new_blur_level);
            ret = UNKNOWN_ERROR;
        }
    }

    // chk_dataline
    int new_dataline = mInternalParameters.getInt("chk_dataline");
    if (0 <= new_dataline) {
        if (mSecCamera->setDataLineCheck(new_dataline) < 0) {
            ALOGE("ERR(%s):Fail on mSecCamera->setDataLineCheck(%d)", __func__, new_dataline);
            ret = UNKNOWN_ERROR;
        }
    }

    // galaxys ce147 need this
    mPreviewLock.lock();
    if(ret == NO_ERROR && mPreviewRunning) {
        ret = mSecCamera->setBatchReflection();
    }
    mPreviewLock.unlock();

    ALOGV("%s return ret = %d", __func__, ret);

    return ret;
}

char* CameraHardwareSec::getParameters() const
{
    String8     params_str8;
    char*       params_str;

    ALOGV("%s :", __func__);

    params_str8 = mParameters.flatten();
    
    // camera service frees this string...
    params_str = (char*) malloc(sizeof(char) * (params_str8.length() + 1));
    strcpy(params_str, params_str8.string());

    return params_str;
}

status_t CameraHardwareSec::sendCommand(int32_t command, int32_t arg1, int32_t arg2)
{
    if(!mSecCamera) {
        ALOGE("SecCamera instance isn't created");
        return INVALID_OPERATION;
    }

    if(!previewEnabled()) {
        ALOGE("Preview is not running");
        return INVALID_OPERATION;
    }

    switch(command) {
        case CAMERA_CMD_START_FACE_DETECTION:
            return startFaceDetection();

        case CAMERA_CMD_STOP_FACE_DETECTION:
            return stopFaceDetection();

        default:
            ALOGE("Command [%i] isn't supported", command);
            break;
    }

    return BAD_VALUE;
}

void CameraHardwareSec::release()
{
    ALOGV("%s :", __func__);

    /* shut down any threads we have that might be running.  do it here
     * instead of the destructor.  we're guaranteed to be on another thread
     * than the ones below.  if we used the destructor, since the threads
     * have a reference to this object, we could wind up trying to wait
     * for ourself to exit, which is a deadlock.
     */
    if (mPreviewThread != NULL) {
        /* this thread is normally already in it's threadLoop but blocked
         * on the condition variable or running.  signal it so it wakes
         * up and can exit.
         */
        mPreviewThread->requestExit();
        mExitPreviewThread = true;
        mPreviewRunning = true; /* let it run so it can exit */
        mPreviewCondition.signal();
        mPreviewThread->requestExitAndWait();
        mPreviewThread.clear();
        mPreviewThread = NULL;
    }
    if (mAutoFocusThread != NULL) {
        /* this thread is normally already in it's threadLoop but blocked
         * on the condition variable.  signal it so it wakes up and can exit.
         */
        mFocusLock.lock();
        mAutoFocusThread->requestExit();
        mExitAutoFocusThread = true;
        mFocusCondition.signal();
        mFocusLock.unlock();
        mAutoFocusThread->requestExitAndWait();
        mAutoFocusThread.clear();
        mAutoFocusThread = NULL;
    }
    if (mPictureThread != NULL) {
        mPictureThread->requestExitAndWait();
        mPictureThread.clear();
        mPictureThread = NULL;
    }

    RELEASE_MEMORY_BUFFER(mRawHeap);
    RELEASE_MEMORY_BUFFER(mPreviewMemory);
    RELEASE_MEMORY_BUFFER(mRecordHeap);

    /* close after all the heaps are cleared since those
     * could have dup'd our file descriptor.
     */
    if(mSecCamera != NULL) {
        mSecCamera->deinitCamera();
        mSecCamera = NULL;
    }
}

}; // namespace android
