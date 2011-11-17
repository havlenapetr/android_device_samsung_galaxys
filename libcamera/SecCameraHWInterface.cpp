/*
**
** Copyright 2008, The Android Open Source Project
** Copyright@ Samsung Electronics Co. LTD
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

#ifdef SEND_YUV_RECORD_DATA
#define ALIGN_TO_32B(x)   ((((x) + (1 <<  5) - 1) >>  5) <<  5)
#define ALIGN_TO_128B(x)  ((((x) + (1 <<  7) - 1) >>  7) <<  7)
#define ALIGN_TO_8KB(x)   ((((x) + (1 << 13) - 1) >> 13) << 13)
#define RECORD_HEAP_SIZE (ALIGN_TO_8KB(ALIGN_TO_128B(1280) * ALIGN_TO_32B(720)) + ALIGN_TO_8KB(ALIGN_TO_128B(1280) * ALIGN_TO_32B(720/2)))
//#define RECORD_HEAP_SIZE (1280*270*1.5)
#endif

namespace android {

struct ADDRS
{
    unsigned int addr_y;
    unsigned int addr_cbcr;
    unsigned int buf_idx;
};

struct ADDRS_CAP
{
    unsigned int addr_y;
    unsigned int width;
    unsigned int height;
};

CameraHardwareSec::CameraHardwareSec()
                  : mParameters(),
                    mPreviewHeap(0),
                    mRawHeap(0),
                    mRecordHeap(0),
                    mJpegHeap(0),
                    mSecCamera(NULL),
                    mPreviewRunning(false),
                    mRecordRunning(false),
                    mFlagUseOverlay(false),
                    mOverlay(0),
                    mOverlayBufIndex(0),
                    mOverlayNumOfBuf(0),
                    mPreviewFrameRateMicrosec(33333),
                    mNotifyCb(0),
                    mDataCb(0),
                    mDataCbTimestamp(0),
                    mCallbackCookie(0),
                    mMsgEnabled(0),
                    mAFMode(SecCamera::AUTO_FOCUS_AUTO)
{
    LOGV("%s()", __FUNCTION__);

    mSecCamera = SecCamera::createInstance();
    if(mSecCamera == NULL) {
        LOGE("ERR(%s):Fail on mSecCamera object creation", __FUNCTION__);
    }

    if(mSecCamera->flagCreate() == 0)
    {
        if(mSecCamera->Create() < 0)
        {
            LOGE("ERR(%s):Fail on mSecCamera->Create", __FUNCTION__);
        }
    }

#ifdef PREVIEW_USING_MMAP
#else
    int previewHeapSize = sizeof(struct ADDRS) * kBufferCount;
    LOGV("mPreviewHeap : MemoryHeapBase(previewHeapSize(%d))", previewHeapSize);
    mPreviewHeap = new MemoryHeapBase(previewHeapSize);
    if (mPreviewHeap->getHeapID() < 0) {
        LOGE("ERR(%s): Preview heap creation fail", __func__);
        mPreviewHeap.clear();
    }
#endif

#ifdef SEND_YUV_RECORD_DATA
    int recordHeapSize = RECORD_HEAP_SIZE;
#else
    int recordHeapSize = sizeof(struct ADDRS) * kBufferCount;
#endif
    LOGV("mRecordHeap : MemoryHeapBase(recordHeapSize(%d))", recordHeapSize);

    mRecordHeap = new MemoryHeapBase(recordHeapSize);
    if (mRecordHeap->getHeapID() < 0) {
        LOGE("ERR(%s): Record heap creation fail", __func__);
        mRecordHeap.clear();
    }

    int rawHeapSize = sizeof(struct ADDRS_CAP);
    LOGV("mRawHeap : MemoryHeapBase(previewHeapSize(%d))", rawHeapSize);
    mRawHeap = new MemoryHeapBase(rawHeapSize);
    if (mRawHeap->getHeapID() < 0) {
        LOGE("ERR(%s): Raw heap creation fail", __func__);
        mRawHeap.clear();
    }

    memset(&mFrameRateTimer,  0, sizeof(DurationTimer));
    memset(&mGpsInfo, 0, sizeof(gps_info));

    m_initDefaultParameters();
}

void CameraHardwareSec::m_initDefaultParameters()
{
    if(mSecCamera == NULL) {
        LOGE("ERR(%s):mSecCamera object is NULL", __FUNCTION__);
        return;
    }

    CameraParameters p;

    int preview_max_width	= 0;
    int preview_max_height	= 0;
    int snapshot_max_width	= 0;
    int snapshot_max_height = 0;

    int camera_id = 1;

    p.set("camera-id", camera_id);

    if(camera_id == 1)
        mSecCamera->setCameraId(SecCamera::CAMERA_ID_BACK);
    else
        mSecCamera->setCameraId(SecCamera::CAMERA_ID_FRONT);

    if(mSecCamera->getPreviewMaxSize(&preview_max_width, &preview_max_height) < 0) {
        LOGE("getPreviewMaxSize fail (%d / %d) \n", preview_max_width, preview_max_height);
        preview_max_width  = LCD_WIDTH;
        preview_max_height = LCD_HEIGHT;
    }
    if(mSecCamera->getSnapshotMaxSize(&snapshot_max_width, &snapshot_max_height) < 0) {
        LOGE("getSnapshotMaxSize fail (%d / %d) \n", snapshot_max_width, snapshot_max_height);
        snapshot_max_width	= LCD_WIDTH;
        snapshot_max_height	= LCD_HEIGHT;
    }

#ifdef PREVIEW_USING_MMAP
    p.setPreviewFormat("yuv420sp");
#else
    p.setPreviewFormat("yuv420sp_custom");
#endif
    p.setPreviewSize(preview_max_width, preview_max_height);
    p.setPreviewFrameRate(30);

#ifdef JPEG_FROM_SENSOR
    p.setPictureFormat("uyv422i");
#else
    p.setPictureFormat("yuv422i");
#endif
    p.setPictureSize(snapshot_max_width, snapshot_max_height);
    p.set("jpeg-quality", "100"); // maximum quality

    // List supported picture size values //Kamat
    //p.set("picture-size-values", "2560x1920,2048x1536,1600x1200,1280x960");
    p.set("picture-size-values", "1600x1200,1280x960");

    // These values must be multiples of 16, so we can't do 427x320, which is the exact size on
    // screen we want to display at. 480x360 doesn't work either since it's a multiple of 8.
    p.set("jpeg-thumbnail-width",  (int)DEFAULT_JPEG_THUMBNAIL_WIDTH);
    p.set("jpeg-thumbnail-height", (int)DEFAULT_JPEG_THUMBNAIL_HEIGHT);
    p.set("jpeg-thumbnail-quality", "100");

    int min_preview_fps = mSecCamera->getFrameRateMin();
    int max_preview_fps = mSecCamera->getFrameRateMax();

    String8 fpsBuf;
    char fpsbuf[16];

    snprintf(fpsbuf, 16, "%d", min_preview_fps);
    fpsBuf.append(fpsbuf);

    for(int i = min_preview_fps; i <= max_preview_fps; i += 5)
    {
        snprintf(fpsbuf, 16, ",%d", i);
        fpsBuf.append(fpsbuf);
    }

    p.set("preview-frame-rate-values",  fpsBuf.string());
    p.set("preview-frame-rate",         max_preview_fps);

    p.set("rotation",	    0);

    p.set("scene-mode-values",   "auto,beach,candlelight,fireworks,landscape,night,night-portrait,party,portrait,snow,sports,steadyphoto,sunset");
	p.set("scene-mode",          "auto");

    p.set("whitebalance-values", "auto,cloudy-daylight,daylight,fluorescent,incandescent");
    p.set("whitebalance",        "auto");

    p.set("effect-values",       "none,aqua,mono,negative,sepia,whiteboard");
    p.set("effect",              "none");

    p.set("brightness",	         mSecCamera->getBrightness());
    p.set("brightness-max",      mSecCamera->getBrightnessMax());
	p.set("brightness-min",      mSecCamera->getBrightnessMin());

    p.set("contrast",            mSecCamera->getContrast());
	p.set("contrast-max",        mSecCamera->getContrastMax());
	p.set("contrast-min",        mSecCamera->getContrastMin());

	p.set("sharpness",           mSecCamera->getSharpness());
	p.set("sharpness-max",       mSecCamera->getSharpnessMax());
	p.set("sharpness-min",       mSecCamera->getSharpnessMin());

	p.set("saturation",          mSecCamera->getSaturation());
	p.set("saturation-max",      mSecCamera->getSaturationMin());
	p.set("saturation-min",      mSecCamera->getSaturationMax());

    //p.set("zoom-supported",       "false");
    //p.set("smooth-zoom-supported","false");
    p.set("zoom-supported",       "true");
    p.set("smooth-zoom-supported","true");

    p.set("max-zoom",            mSecCamera->getZoomMax());
	p.set("min-zoom",            mSecCamera->getZoomMin());
    //p.set("zoom-ratios",         "100");
    p.set("zoom-ratios",        "100,130,160,190,220,250,280,310,340,360,400");
    p.set("zoom",                mSecCamera->getZoom());

    p.set("focus-mode-values",   "auto,macro,fixed,infinity");
	p.set("focus-mode",          "auto");

    p.set("gps-latitude",  "0.0");
	p.set("gps-longitude", "0.0");
	p.set("gps-timestamp", "1199145600"); // Jan 1, 2008, 00:00:00
	p.set("gps-altitude",  "0"); // meters

    //p.set("flash-mode-values",   "off,auto,on");
    //p.set("flash-mode",    "off");

    // FYI..
    // http://developer.android.com/reference/android/hardware/Camera.Parameters.html

    if (setParameters(p) != NO_ERROR) {
        LOGE("ERR(%s):Fail on setParameters(p)", __FUNCTION__);
    }
}

CameraHardwareSec::~CameraHardwareSec()
{
    LOGV("%s()", __FUNCTION__);

    this->release();

    mSecCamera = NULL;

    singleton.clear();
}

sp<IMemoryHeap> CameraHardwareSec::getPreviewHeap() const
{
    return mPreviewHeap;
}

sp<IMemoryHeap> CameraHardwareSec::getRawHeap() const
{
    return mRawHeap;
}

//Kamat added: New code as per eclair framework
void CameraHardwareSec::setCallbacks(notify_callback notify_cb,
                                      data_callback data_cb,
                                      data_callback_timestamp data_cb_timestamp,
                                      void* user)
{
    Mutex::Autolock cameraLock(mCameraLock);

    mNotifyCb        = notify_cb;
    mDataCb          = data_cb;
    mDataCbTimestamp = data_cb_timestamp;
    mCallbackCookie  = user;
}

void CameraHardwareSec::enableMsgType(int32_t msgType)
{
    mMsgEnabled |= msgType;
}

void CameraHardwareSec::disableMsgType(int32_t msgType)
{
    mMsgEnabled &= ~msgType;
}

bool CameraHardwareSec::msgTypeEnabled(int32_t msgType)
{
    return (mMsgEnabled & msgType);
}

status_t CameraHardwareSec::startPreview()
{
    LOGV("%s()", __FUNCTION__);

    Mutex::Autolock cameraLock(mCameraLock);

    if (mPreviewThread != 0) {
        // already running
        LOGE("%s::mPreviewThread alreay run...\n", __func__);
        return INVALID_OPERATION;
    }

    //mSecCamera->stopPreview();

    if(mSecCamera->startPreview() < 0) {
        LOGE("ERR(%s):Fail on mSecCamera->startPreview()", __FUNCTION__);
        return UNKNOWN_ERROR;
    }

    #ifdef PREVIEW_USING_MMAP
    {
        if(mPreviewHeap != NULL)
            mPreviewHeap.clear();

        int width, height;
        unsigned int frameSize;

        mSecCamera->getPreviewSize(&width, &height, &frameSize);
        unsigned int previewHeapSize = frameSize * kBufferCount;

        LOGD("MemoryHeapBase(fd(%d), size(%d), %d)", (int)mSecCamera->getCameraFd(), (size_t)(previewHeapSize), (uint32_t)0);

        mPreviewHeap = new MemoryHeapBase((int)mSecCamera->getCameraFd(), (size_t)(previewHeapSize), (uint32_t)0);
    }
    #endif

    mPreviewRunning = true;

    if(mPreviewThread == 0)
        mPreviewThread = new PreviewThread(this);
    return NO_ERROR;
}

void CameraHardwareSec::stopPreview()
{
    LOGV("%s()", __FUNCTION__);

    Mutex::Autolock autofocusLock(&mAutofocusLock);

    sp<PreviewThread> previewThread;

    { // scope for the cameraLock
        Mutex::Autolock cameraLock(mCameraLock);
        previewThread = mPreviewThread;
    }

    // don't hold the cameraLock while waiting for the thread to quit
    if (previewThread != 0) {
        previewThread->requestExitAndWait();
    }

    {
        Mutex::Autolock cameraLock(mCameraLock);

        mPreviewThread.clear();

        if(mSecCamera && mSecCamera->stopPreview() < 0)
        {
            LOGE("ERR(%s):Fail on mSecCamera->stopPreview()", __FUNCTION__);
        }

        // If comment-in the below code..
        // there will be no more preview..
        // this bug is related with "status_t CameraService::Client::setOverlay()" in Camera Service..
        // there is no else part match with "if (mOverlayRef.get() == NULL)"
        /*
        if(mFlagUseOverlay == true && mOverlay != 0)
        {
            mFlagUseOverlay = false;

            mOverlay->destroy();
            mOverlay = 0;
        }
        */

        mPreviewRunning = false;
    }
}

bool CameraHardwareSec::previewEnabled()
{
    LOGV("%s() : %d", __FUNCTION__, mPreviewThread != 0);
    return mPreviewThread != 0;
}

#ifdef BOARD_USES_OVERLAY
bool CameraHardwareSec::useOverlay()
{
    //return true;
    return false;
}
#endif // BOARD_USES_OVERLAY

status_t CameraHardwareSec::setOverlay(const sp<Overlay> &overlay)
{
    LOGV("%s() : ", __FUNCTION__);

    int overlayWidth  = 0;
    int overlayHeight = 0;

    if(overlay == NULL)
    {
        // sw5771.park : this come every time.....
        //LOGD("ERR(%s):overlay arg is NULL()", __FUNCTION__);
        goto setOverlay_fail;
    }

    if(overlay->getHandleRef()== NULL && mFlagUseOverlay == true)
    {
        mFlagUseOverlay = false;

        if(mOverlay != 0)
            mOverlay->destroy();
        mOverlay = 0;

        return NO_ERROR;
    }

    if(overlay->getStatus() != NO_ERROR)
    {
        LOGE("ERR(%s):overlay->getStatus() fail", __FUNCTION__);
        goto setOverlay_fail;
    }

    /*
    overlayWidth  = overlay->getWidth();
    overlayHeight = overlay->getHeight();

    if(overlayWidth == 0 || overlayHeight == 0)
    {
        LOGE("ERR(%s):overlayWidth == 0 || overlayHeight == 0 fail", __FUNCTION__);
        goto setOverlay_fail;
    }
    */

    // sw5771.park : we need to decide what size is exact size..
    mSecCamera->getPreviewSize(&overlayWidth, &overlayHeight);

    if(overlay->setCrop(0, 0, overlayWidth, overlayHeight) != NO_ERROR)
    {
        LOGE("ERR(%s)::(mOverlay->setCrop(0, 0, %d, %d) fail", __FUNCTION__, overlayWidth, overlayHeight);
        goto setOverlay_fail;
    }

    mOverlayNumOfBuf = overlay->getBufferCount();
    if(mOverlayNumOfBuf <= 0)
    {
        LOGE("ERR(%s)::(mOverlay->getBufferCount() fail", __FUNCTION__);
        goto setOverlay_fail;
    }

    mOverlay        = overlay;
    mFlagUseOverlay = true;

    return NO_ERROR;

setOverlay_fail :

    mFlagUseOverlay = false;

    if(mOverlay != 0)
        mOverlay->destroy();
    mOverlay = 0;

    //return BAD_VALUE;
    return UNKNOWN_ERROR;
}

// ---------------------------------------------------------------------------

status_t CameraHardwareSec::startRecording()
{
    LOGV("%s()", __FUNCTION__);

#ifdef DUAL_PORT_RECORDING
    if(mSecCamera->startRecord() < 0) {
        LOGE("ERR(%s):Fail on mSecCamera->startRecord()", __FUNCTION__);
        return UNKNOWN_ERROR;
    }
#endif

    mRecordRunning = true;
    return NO_ERROR;
}

void CameraHardwareSec::stopRecording()
{
    LOGV("%s()", __FUNCTION__);

#ifdef DUAL_PORT_RECORDING
    if(mSecCamera->stopRecord() < 0)
    {
        LOGE("ERR(%s):Fail on mSecCamera->stopRecord()", __FUNCTION__);
        //           return UNKNOWN_ERROR;
    }
#endif
    mRecordRunning = false;
}

bool CameraHardwareSec::recordingEnabled()
{
    LOGV("%s()", __FUNCTION__);

    return mRecordRunning;
}

void CameraHardwareSec::releaseRecordingFrame(const sp<IMemory>& mem)
{
    LOG_CAMERA_PREVIEW("%s()", __FUNCTION__);

//    ssize_t offset; size_t size;
//    sp<MemoryBase>	   mem1	= mem;
//    sp<MemoryHeapBase> heap = mem->getMemory(&offset, &size);
//    sp<IMemoryHeap> heap = mem->getMemory(&offset, &size);

//    mem1.clear();
//    heap.clear();
}

status_t CameraHardwareSec::autoFocus()
{
    LOGV("%s()", __FUNCTION__);

    if (createThread(m_beginAutoFocusThread, this) == false)
        return UNKNOWN_ERROR;

    return NO_ERROR;
}

/* 2009.10.14 by icarus for added interface */
status_t CameraHardwareSec::cancelAutoFocus()
{
    Mutex::Autolock autoFocusLock(&mAutofocusLock);

    int flagFocused = 0;
	if(mSecCamera->runAF(SecCamera::FLAG_OFF, &flagFocused) < 0)
        LOGE("ERR(%s):Fail on mSecCamera->runAF(FLAG_OFF)", __FUNCTION__);

    return NO_ERROR;
}

status_t CameraHardwareSec::takePicture()
{
    LOGV("%s()", __FUNCTION__);

    this->stopPreview();

    if (createThread(m_beginPictureThread, this) == false)
        return UNKNOWN_ERROR;

    return NO_ERROR;
}

status_t CameraHardwareSec::cancelPicture()
{
    return NO_ERROR;
}

status_t CameraHardwareSec::setParameters(const CameraParameters& params)
{
    LOGV("%s()", __FUNCTION__);

    Mutex::Autolock cameraLock(mCameraLock);

    status_t ret = NO_ERROR;

    mParameters = params;

    // set camera id
    int new_camera_id = mParameters.getInt("camera-id");
    if(0 <= new_camera_id) {
        if(mSecCamera->setCameraId(new_camera_id) < 0) {
            LOGE("ERR(%s):Fail on mSecCamera->setCameraId(camera_id(%d))", __FUNCTION__, new_camera_id);
            ret = UNKNOWN_ERROR;
        }
    }

    // preview size
    int new_preview_width  = 0;
    int new_preview_height = 0;
    mParameters.getPreviewSize(&new_preview_width, &new_preview_height);
    const char * new_str_preview_format = mParameters.getPreviewFormat();
    if(0 < new_preview_width && 0 < new_preview_height && new_str_preview_format != NULL) {
        int new_preview_format = 0;

        if (strcmp(new_str_preview_format, "rgb565") == 0)
            new_preview_format = V4L2_PIX_FMT_RGB565;
        else if (strcmp(new_str_preview_format, "yuv420sp") == 0)
            new_preview_format = V4L2_PIX_FMT_NV21; //Kamat
        else if (strcmp(new_str_preview_format, "yuv420sp_custom") == 0)
            new_preview_format = V4L2_PIX_FMT_NV12T; //Kamat
        else if (strcmp(new_str_preview_format, "yuv420p") == 0)
            new_preview_format = V4L2_PIX_FMT_YUV420;
        else if (strcmp(new_str_preview_format, "yuv422i") == 0)
            new_preview_format = V4L2_PIX_FMT_YUYV;
        else if (strcmp(new_str_preview_format, "yuv422p") == 0)
            new_preview_format = V4L2_PIX_FMT_YUV422P;
        else
            new_preview_format = V4L2_PIX_FMT_RGB565;

        if(mSecCamera->setPreviewSize(new_preview_width, new_preview_height, new_preview_format) < 0) {
            LOGE("ERR(%s):Fail on mSecCamera->setPreviewSize(width(%d), height(%d), format(%d))", __FUNCTION__, new_preview_width, new_preview_height, new_preview_format);
            ret = UNKNOWN_ERROR;
        }

        if(mFlagUseOverlay == true && mOverlay != 0)
        {
            if(mOverlay->setCrop(0, 0, new_preview_width, new_preview_height) != NO_ERROR)
            {
                LOGE("ERR(%s)::(mOverlay->setCrop(0, 0, %d, %d) fail", __FUNCTION__, new_preview_width, new_preview_height);
            }
        }
    }

    int new_picture_width  = 0;
    int new_picture_height = 0;
    mParameters.getPictureSize(&new_picture_width, &new_picture_height);
    if(0 < new_picture_width && 0 < new_picture_height) {
        if(mSecCamera->setSnapshotSize(new_picture_width, new_picture_height) < 0) {
            LOGE("ERR(%s):Fail on mSecCamera->setSnapshotSize(width(%d), height(%d))", __FUNCTION__, new_picture_width, new_picture_height);
            ret = UNKNOWN_ERROR;
        }
    }

    // picture format
    const char * new_str_picture_format = mParameters.getPictureFormat();
    if(new_str_picture_format != NULL) {
        int new_picture_format = 0;

        if (strcmp(new_str_picture_format, "rgb565") == 0)
            new_picture_format = V4L2_PIX_FMT_RGB565;
        else if (strcmp(new_str_picture_format, "yuv420sp") == 0)
            new_picture_format = V4L2_PIX_FMT_NV21; //Kamat: Default format
        else if (strcmp(new_str_picture_format, "yuv420sp_custom") == 0)
            new_picture_format = V4L2_PIX_FMT_NV12T;
        else if (strcmp(new_str_picture_format, "yuv420p") == 0)
            new_picture_format = V4L2_PIX_FMT_YUV420;
        else if (strcmp(new_str_picture_format, "yuv422i") == 0)
            new_picture_format = V4L2_PIX_FMT_YUYV;
        else if (strcmp(new_str_picture_format, "uyv422i") == 0)
            new_picture_format = V4L2_PIX_FMT_UYVY;
        else if (strcmp(new_str_picture_format, "yuv422p") == 0)
            new_picture_format = V4L2_PIX_FMT_YUV422P;
        else
            new_picture_format = V4L2_PIX_FMT_RGB565;

        if(mSecCamera->setSnapshotPixelFormat(new_picture_format) < 0) {
            LOGE("ERR(%s):Fail on mSecCamera->setSnapshotPixelFormat(format(%d))", __FUNCTION__, new_picture_format);
            ret = UNKNOWN_ERROR;
        }
    }

    //JPEG image quality
    int new_jpeg_quality = mParameters.getInt("jpeg-quality");
    if (new_jpeg_quality < 0) {
        LOGW("JPEG-image quality is not specified or is negative, defaulting to 100");
        new_jpeg_quality = 100;
        mParameters.set("jpeg-quality", "100");
    }
    mSecCamera->setJpegQuality(new_jpeg_quality);

    // JPEG thumbnail size
    int new_jpeg_thumbnail_width = mParameters.getInt("jpeg-thumbnail-width");
    int new_jpeg_thumbnail_height= mParameters.getInt("jpeg-thumbnail-height");

    if(0 < new_jpeg_thumbnail_width && 0 < new_jpeg_thumbnail_height) {
        if(mSecCamera->setJpegThumbnailSize(new_jpeg_thumbnail_width, new_jpeg_thumbnail_height) < 0) {
            LOGE("ERR(%s):Fail on mSecCamera->setJpegThumbnailSize(width(%d), height(%d))", __FUNCTION__, new_jpeg_thumbnail_width, new_jpeg_thumbnail_height);
            ret = UNKNOWN_ERROR;
        }
    }

    // frame rate
    int min_preview_fps = mSecCamera->getFrameRateMin();
    int max_preview_fps = mSecCamera->getFrameRateMax();
    int new_frame_rate  = mParameters.getPreviewFrameRate();

    if(new_frame_rate < min_preview_fps)
    {
        new_frame_rate = min_preview_fps;
        mParameters.setPreviewFrameRate(new_frame_rate);
    }
    else if(max_preview_fps < new_frame_rate)
    {
        new_frame_rate = max_preview_fps;
        mParameters.setPreviewFrameRate(new_frame_rate);
    }

    // Calculate how long to wait between frames.
    mPreviewFrameRateMicrosec = (int)(1000000.0f / float(new_frame_rate));

    //LOGD("frame rate:%d, mPreviewFrameRateMicrosec:%d", new_frame_rate, mPreviewFrameRateMicrosec);

    mSecCamera->setFrameRate(new_frame_rate);

    // rotation
	int new_rotation = mParameters.getInt("rotation");
	if(new_rotation != -1)
	{
		if(mSecCamera->SetRotate(new_rotation) < 0)
		{
			LOGE("%s::mSecCamera->SetRotate(%d) fail", __func__, new_rotation);
			ret = UNKNOWN_ERROR;
		}
	}

    // scene mode
    const char * new_scene_mode_str = mParameters.get("scene-mode");
    int new_scene_mode = -1;
    if(new_scene_mode_str != NULL)
    {
	    if(strcmp(new_scene_mode_str, "auto") == 0)
		    new_scene_mode = SecCamera::SCENE_MODE_AUTO;
	    else if(strcmp(new_scene_mode_str, "beach") == 0)
		    new_scene_mode = SecCamera::SCENE_MODE_BEACH;
	    else if(strcmp(new_scene_mode_str, "candlelight") == 0)
		    new_scene_mode = SecCamera::SCENE_MODE_CANDLELIGHT;
	    else if(strcmp(new_scene_mode_str, "fireworks") == 0)
		    new_scene_mode = SecCamera::SCENE_MODE_FIREWORKS;
	    else if(strcmp(new_scene_mode_str, "landscape") == 0)
		    new_scene_mode = SecCamera::SCENE_MODE_LANDSCAPE;
	    else if(strcmp(new_scene_mode_str, "night") == 0)
		    new_scene_mode = SecCamera::SCENE_MODE_NIGHT;
	    else if(strcmp(new_scene_mode_str, "night-portrait") == 0)
		    new_scene_mode = SecCamera::SCENE_MODE_NIGHTPORTRAIT;
	    else if(strcmp(new_scene_mode_str, "party") == 0)
		    new_scene_mode = SecCamera::SCENE_MODE_PARTY;
	    else if(strcmp(new_scene_mode_str, "portrait") == 0)
		    new_scene_mode = SecCamera::SCENE_MODE_PORTRAIT;
	    else if(strcmp(new_scene_mode_str, "snow") == 0)
		    new_scene_mode = SecCamera::SCENE_MODE_SNOW;
	    else if(strcmp(new_scene_mode_str, "sports") == 0)
		    new_scene_mode = SecCamera::SCENE_MODE_SPORTS;
        else if(strcmp(new_scene_mode_str, "steadyphoto") == 0)
		    new_scene_mode = SecCamera::SCENE_MODE_STEADYPHOTO;
	    else if(strcmp(new_scene_mode_str, "sunset") == 0)
		    new_scene_mode = SecCamera::SCENE_MODE_SUNSET;
	    else
	    {
		    LOGE("%s::unmatched scene-mode(%s)", __func__, new_scene_mode_str);
		    //ret = UNKNOWN_ERROR;
	    }

	    if(0 <= new_scene_mode)
	    {

		    // set scene mode
		    if(mSecCamera->setSceneMode(new_scene_mode) < 0)
		    {
			    LOGE("ERR(%s):Fail on mSecCamera->setSceneMode(new_scene_mode(%d))", __FUNCTION__, new_scene_mode);
			    ret = UNKNOWN_ERROR;
		    }
	    }
    }

    // whitebalance
    const char * new_white_str = mParameters.get("whitebalance");
    if(new_white_str != NULL) {
        int new_white = -1;

        if(strcmp(new_white_str, "auto") == 0)
            new_white = SecCamera::WHITE_BALANCE_AUTO;
        else if(strcmp(new_white_str, "cloudy-daylight") == 0)
            new_white = SecCamera::WHITE_BALANCE_CLOUDY;
        else if(strcmp(new_white_str, "daylight") == 0)
            new_white = SecCamera::WHITE_BALANCE_SUNNY;
        else if(strcmp(new_white_str, "fluorescent") == 0)
            new_white = SecCamera::WHITE_BALANCE_FLUORESCENT;
        else if(strcmp(new_white_str, "incandescent") == 0)
            new_white = SecCamera::WHITE_BALANCE_INCANDESCENT;
        else {
            LOGE("ERR(%s):Invalid white balance(%s)", __FUNCTION__, new_white_str);
            ret = UNKNOWN_ERROR;
        }

        if(0 <= new_white) {
            // white_balance
            if(mSecCamera->setWhiteBalance(new_white) < 0) {
                LOGE("ERR(%s):Fail on mSecCamera->setWhiteBalance(white(%d))", __FUNCTION__, new_white);
                ret = UNKNOWN_ERROR;
            }
        }
    }

    // image effect
    const char * new_image_effect_str = mParameters.get("effect");
    if(new_image_effect_str != NULL) {
        int  new_image_effect = -1;

        if(strcmp(new_image_effect_str, "none") == 0)
            new_image_effect = SecCamera::IMAGE_EFFECT_ORIGINAL;
        else if(strcmp(new_image_effect_str, "aqua") == 0)
            new_image_effect = SecCamera::IMAGE_EFFECT_AQUA;
        else if(strcmp(new_image_effect_str, "mono") == 0)
            new_image_effect = SecCamera::IMAGE_EFFECT_MONO;
        else if(strcmp(new_image_effect_str, "negative") == 0)
            new_image_effect = SecCamera::IMAGE_EFFECT_NEGATIVE;
        else if(strcmp(new_image_effect_str, "sepia") == 0)
            new_image_effect = SecCamera::IMAGE_EFFECT_SEPIA;
        else if(strcmp(new_image_effect_str, "whiteboard") == 0)
            new_image_effect = SecCamera::IMAGE_EFFECT_WHITEBOARD;
        else {
            LOGE("ERR(%s):Invalid effect(%s)", __FUNCTION__, new_image_effect_str);
            ret = UNKNOWN_ERROR;
        }

        if(new_image_effect >= 0) {
            // white_balance
            if(mSecCamera->setImageEffect(new_image_effect) < 0) {
                LOGE("ERR(%s):Fail on mSecCamera->setImageEffect(effect(%d))", __FUNCTION__, new_image_effect);
                ret = UNKNOWN_ERROR;
            }
        }
    }

    // brightness
    int new_brightness = mParameters.getInt("brightness");
    if(0 <= new_brightness)
    {
        /*
        // calbration for TN's spec
	    switch(new_scene_mode)
	    {
		    case SecCamera::SCENE_MODE_AUTO :
			    break;
		    case SecCamera::SCENE_MODE_BEACH :
			    mParameters.set("brightness", (SecCamera::BRIGHTNESS_NORMAL + 1));
			    new_brightness = (SecCamera::BRIGHTNESS_NORMAL + 1);
			    break;
		    default :
			    mParameters.set("brightness", (SecCamera::BRIGHTNESS_NORMAL));
			    new_brightness = (SecCamera::BRIGHTNESS_NORMAL);
			    break;
	    }
        */

	    if(mSecCamera->setBrightness(new_brightness) < 0)
	    {
            LOGE("ERR(%s):Fail on mSecCamera->setBrightness(new_brightness(%d))", __FUNCTION__, new_brightness);
		    ret = UNKNOWN_ERROR;
	    }
    }

    // contrast
    int new_contrast = mParameters.getInt("contrast");
    if(0 <= new_contrast)
    {
	    if(mSecCamera->setContrast(new_contrast) < 0)
	    {
            LOGE("ERR(%s):Fail on mSecCamera->setContrast(new_contrast(%d))", __FUNCTION__, new_contrast);
		    ret = UNKNOWN_ERROR;
	    }
    }

    // sharpness
    int new_sharpness = mParameters.getInt("sharpness");
    if(0 <= new_sharpness)
    {
        /*
        // calbration for TN's spec
	    switch(new_scene_mode)
	    {
		    case SecCamera::SCENE_MODE_AUTO :
			    break;
		    case SecCamera::SCENE_MODE_PORTRAIT :
			    mParameters.set("sharpness", (SecCamera::SHARPNESS_NOMAL - 1));
			    new_sharpness = (SecCamera::SHARPNESS_NOMAL - 1);
			    break;
		    case SecCamera::SCENE_MODE_LANDSCAPE :
			    mParameters.set("sharpness", (SecCamera::SHARPNESS_NOMAL + 1));
			    new_sharpness = (SecCamera::SHARPNESS_NOMAL + 1);
			    break;
		    case SecCamera::SCENE_MODE_STEADYPHOTO :
			    mParameters.set("sharpness", (SecCamera::SHARPNESS_NOMAL + 2));
			    new_sharpness = (SecCamera::SHARPNESS_NOMAL + 2);
			    break;
		    default :
			    mParameters.set("sharpness", (SecCamera::SHARPNESS_NOMAL));
			    new_sharpness = (SecCamera::SHARPNESS_NOMAL);
			    break;
	    }
        */

	    if(mSecCamera->setSharpness(new_sharpness) < 0)
	    {
            LOGE("ERR(%s):Fail on mSecCamera->setSharpness(new_sharpness(%d))", __FUNCTION__, new_sharpness);
		    ret = UNKNOWN_ERROR;
	    }
    }

    // saturation
    int new_saturation = mParameters.getInt("saturation");
    if(0 <= new_saturation)
    {
        /*
	    // calbration for TN's spec
	    switch(new_scene_mode)
	    {
		    case SecCamera::SCENE_MODE_AUTO :
			    break;
		    case SecCamera::SCENE_MODE_BEACH :
		    case SecCamera::SCENE_MODE_LANDSCAPE :
		    case SecCamera::SCENE_MODE_PARTY :
		    case SecCamera::SCENE_MODE_SNOW :
			    mParameters.set("saturation", (SecCamera::SATURATION_NOMAL + 1));
			    new_saturation = (SecCamera::SATURATION_NOMAL + 1);
			    break;
		    default :
			    mParameters.set("saturation", (SecCamera::SATURATION_NOMAL));
			    new_saturation = (SecCamera::SATURATION_NOMAL);
			    break;
	    }
        */

	    if(mSecCamera->setSaturation(new_saturation) < 0)
	    {
            LOGE("ERR(%s):Fail on mSecCamera->setSaturation(new_saturation(%d))", __FUNCTION__, new_saturation);
		    ret = UNKNOWN_ERROR;
	    }
    }

    // zoom
	int new_zoom = mParameters.getInt("zoom");
	if(0 <= new_zoom)
	{
		// setZoom
		if(mSecCamera->setZoom(new_zoom) < 0)
		{
            LOGE("ERR(%s):Fail on mSecCamera->setZoom(new_zoom(%d))", __FUNCTION__, new_zoom);
			ret = UNKNOWN_ERROR;
		}
	}

    // auto-focus
	const char * new_new_auto_focus_str = mParameters.get("focus-mode");
	if(new_new_auto_focus_str != NULL)
	{
		int new_auto_focus = -1;

		if(strcmp(new_new_auto_focus_str, "auto") == 0)
			new_auto_focus = SecCamera::AUTO_FOCUS_AUTO;
		else if(strcmp(new_new_auto_focus_str, "fixed") == 0)
			new_auto_focus = SecCamera::AUTO_FOCUS_FIXED;
		else if(strcmp(new_new_auto_focus_str, "infinity") == 0)
			new_auto_focus = SecCamera::AUTO_FOCUS_INFINITY;
		else if(strcmp(new_new_auto_focus_str, "macro") == 0)
			new_auto_focus = SecCamera::AUTO_FOCUS_MACRO;
		else
		{
            LOGE("ERR(%s):unmatched af mode(%s))", __FUNCTION__, new_new_auto_focus_str);
			//ret = UNKNOWN_ERROR;
		}

		if(SecCamera::AUTO_FOCUS_BASE < new_auto_focus)
		{
            /*
			// calbration for TN's spec
			switch(new_scene_mode)
			{
				case SecCamera::SCENE_MODE_AUTO :
					break;

				case SecCamera::SCENE_MODE_TEXT :
					mParameters.set("focus-mode", "macro");
					new_auto_focus = SecCamera::AUTO_FOCUS_MACRO;
					break;

				default :
					mParameters.set("focus-mode", "auto");
					new_auto_focus = SecCamera::AUTO_FOCUS_AUTO;
					break;
			}
            */

			if(mSecCamera->setAFMode(new_auto_focus) < 0)
			{
                LOGE("ERR(%s):mSecCamera->setAFMode(%d) fail)", __FUNCTION__, new_auto_focus);
				ret = UNKNOWN_ERROR;
			}

			mAFMode = new_auto_focus;
		}
	}

    return ret;
}

CameraParameters CameraHardwareSec::getParameters() const
{
    LOGV("%s()", __FUNCTION__);
    return mParameters;
}

status_t CameraHardwareSec::sendCommand(int32_t command, int32_t arg1,
                                         int32_t arg2)
{
    int  cur_zoom     = 0;
    int  new_zoom     = 0;
    bool enable_zoom  = false;
    bool flag_stopped = false;
    int  min_zoom     = 0;
    int  max_zoom     = 10;

    switch(command)
    {
        case CAMERA_CMD_START_SMOOTH_ZOOM :
            LOGD("%s::CAMERA_CMD_START_SMOOTH_ZOOM arg1(%d) arg2(%d)\n", __func__, arg1, arg2);

            min_zoom = mSecCamera->getZoomMin();
            max_zoom = mSecCamera->getZoomMax();
            cur_zoom = mSecCamera->getZoom();
            new_zoom = arg1;

            if(cur_zoom < new_zoom && cur_zoom < max_zoom)
	            enable_zoom = true;
            else if(new_zoom < cur_zoom && min_zoom < cur_zoom)
	            enable_zoom = true;

            if(enable_zoom == true && mSecCamera->setZoom(new_zoom) < 0)
            {
                LOGE("ERR(%s):Fail on mSecCamera->setZoom(new_zoom(%d))", __FUNCTION__, new_zoom);
	            enable_zoom = false;
	            //return UNKNOWN_ERROR;
            }

            if(   enable_zoom == false
               || new_zoom    == max_zoom
               || new_zoom    == min_zoom)
            {
	            flag_stopped = true;
            }
            else
	            flag_stopped = false;

            // kcoolsw : we need to check up..
            flag_stopped = true;

            if((mMsgEnabled & CAMERA_MSG_ZOOM) && mNotifyCb)
                mNotifyCb(CAMERA_MSG_ZOOM, new_zoom, flag_stopped, mCallbackCookie);

            break;
        case CAMERA_CMD_STOP_SMOOTH_ZOOM :
            LOGD("%s::CAMERA_CMD_STOP_SMOOTH_ZOOM  arg1(%d) arg2(%d)\n", __func__, arg1, arg2);
            break;
        case CAMERA_CMD_SET_DISPLAY_ORIENTATION :
        default :
            LOGD("%s::default command(%d) arg1(%d) arg2(%d)\n", __func__, command, arg1, arg2);
            break;
    }

    //return BAD_VALUE;
    return NO_ERROR;
}

void CameraHardwareSec::release()
{
    LOGV("%s()", __FUNCTION__);

    sp<PreviewThread> previewThread;

    {
        Mutex::Autolock autofocusLock(&mAutofocusLock);

        Mutex::Autolock cameraLock(&mCameraLock);

        previewThread = mPreviewThread;
    }

    if (previewThread != 0)
        previewThread->requestExitAndWait();

	{
        Mutex::Autolock autofocusLock(&mAutofocusLock);

        Mutex::Autolock cameraLock(&mCameraLock);

        mPreviewThread.clear();

        mSecCamera->Destroy();

        if(mFlagUseOverlay == true && mOverlay != 0)
        {
            mFlagUseOverlay = false;

            mOverlay->destroy();
            mOverlay = 0;
        }

        if(mRawHeap != NULL)
            mRawHeap.clear();

        if(mJpegHeap != NULL)
            mJpegHeap.clear();

        if(mPreviewHeap != NULL)
            mPreviewHeap.clear();

        if(mRecordHeap != NULL)
            mRecordHeap.clear();
    }
}

status_t CameraHardwareSec::dump(int fd, const Vector<String16>& args) const
{
    /*
    const size_t SIZE = 256;
    char buffer[SIZE];
    String8 result;
    AutoMutex cameraLock(&mCameraLock);
    if (mSecCamera != 0) {
        mSecCamera->dump(fd, args);
        mParameters.dump(fd, args);
        snprintf(buffer, 255, " preview frame(%d), size (%d), running(%s)\n", mCurrentPreviewFrame, mPreviewFrameSize, mPreviewRunning?"true": "false");
        result.append(buffer);
    } else {
        result.append("No camera client yet.\n");
    }
    write(fd, result.string(), result.size());
    */
    return NO_ERROR;
}

// ---------------------------------------------------------------------------

int CameraHardwareSec::m_beginAutoFocusThread(void *cookie)
{
    LOGV("%s()", __FUNCTION__);
    CameraHardwareSec *c = (CameraHardwareSec *)cookie;
    return c->m_autoFocusThreadFunc();
}

/*static*/ int CameraHardwareSec::m_beginPictureThread(void *cookie)
{
    LOGV("%s()", __FUNCTION__);
    CameraHardwareSec *c = (CameraHardwareSec *)cookie;
    return c->m_pictureThreadFunc();
}


// ---------------------------------------------------------------------------

int CameraHardwareSec::m_previewThreadFunc()
{
    sp<MemoryBase> previewBuffer;
    sp<MemoryBase> recordBuffer;

    // sleep per fps..
    {
        unsigned int time_gap;

		mFrameRateTimer.stop();

		time_gap = (unsigned int) mFrameRateTimer.durationUsecs();
        time_gap = (time_gap << 1);
        // give it to margin..
        // ex:
        // (10msec * 2) <  33msec) -> usleep();
        // (20msec * 2) >= 33msec) -> no usleep();

		// Wait for the Frame-Per-Second
		if(time_gap < mPreviewFrameRateMicrosec)
		{
			usleep(mPreviewFrameRateMicrosec - time_gap);
			LOG_CAMERA_PREVIEW("####### usleep for timestamp : %d \n", (mPreviewFrameRateMicrosec - time_gap));
		}

		mFrameRateTimer.start();
    }

    {
        Mutex::Autolock cameraLock(mCameraLock);

        #ifdef RUN_VARIOUS_EFFECT_TEST
			m_runEffectTest();
		#endif // RUN_VARIOUS_EFFECT_TEST

        int index;
        unsigned int phyYAddr = 0;
        unsigned int phyCAddr = 0;
        unsigned int chunckSize = 0;

        index = mSecCamera->getPreview();
        if(index < 0) {
            LOGE("ERR(%s):Fail on SecCamera->getPreview()", __FUNCTION__);
            return UNKNOWN_ERROR;
        }

        phyYAddr = mSecCamera->getPhyAddrY(index);
        phyCAddr = mSecCamera->getPhyAddrC(index);
        if(phyYAddr == 0xffffffff || phyCAddr == 0xffffffff) {
            LOGE("ERR(%s):Fail on SecCamera getPhyAddr Y addr = %0x C addr = %0x", __FUNCTION__, phyYAddr, phyCAddr);
            return UNKNOWN_ERROR;
        }

        #ifdef PREVIEW_USING_MMAP
        {

            int width, height;
            unsigned int frameSize;
            mSecCamera->getPreviewSize(&width, &height, &frameSize);
            chunckSize = frameSize;
        }
        #else
        {
            chunckSize = sizeof(struct ADDRS);
        }
        #endif //PREVIEW_USING_MMAP

        previewBuffer = new MemoryBase(mPreviewHeap, chunckSize * index, chunckSize);

        struct ADDRS *addrs = (struct ADDRS *)mPreviewHeap->base();
        #ifdef PREVIEW_USING_MMAP
        #else
            addrs[index].addr_y    = phyYAddr;
            addrs[index].addr_cbcr = phyCAddr;
        #endif //PREVIEW_USING_MMAP

        if(mFlagUseOverlay == true && mOverlay != 0)
        {
            int q_ret  = NO_ERROR;
            int dq_ret = NO_ERROR;

            overlay_buffer_t overlayDQbuf;

            //LOGD("######## kcoolsw : sending 1 \n");

            addrs[index].buf_idx = mOverlayBufIndex;

            q_ret = mOverlay->queueBuffer((void*)(static_cast<unsigned char *>(mPreviewHeap->base()) + (chunckSize * index)));

            switch(q_ret)
            {
                case NOT_YET_CONTROLED :
                    //LOGE("### queueBuffer : NOT_YET_CONTROLED ###");
                    break;

                case ALL_BUFFERS_FLUSHED :
                {
                    //LOGE("### queueBuffer : ALL_BUFFERS_FLUSHED ###");
                    //mOverlayBufIndex = 0;
                    goto Overlay_done;
                    break;
                }
                case NO_ERROR :
                {
                    dq_ret = mOverlay->dequeueBuffer(&overlayDQbuf);
                    if(dq_ret == ALL_BUFFERS_FLUSHED)
                    {
                        //LOGE("### dequeueBuffer : ALL_BUFFERS_FLUSHED ###");
                        //mOverlayBufIndex = 0;
                        goto Overlay_done;
                    }
                    else if(dq_ret != NO_ERROR)
                    {
                        //LOGE("### ERR(%s):Fail on mOverlay->dequeueBuffer() fail", __FUNCTION__);
                        //return UNKNOWN_ERROR;
                    }
                    //else
                        // no error
                    break;
                }
                default : // ERRORS.
                {
                    //LOGE("#### ERR(%s):Fail on mOverlay->queueBuffer(%d) fail", __FUNCTION__, mOverlayBufIndex);
                    //return UNKNOWN_ERROR;
                    break;
                }
            }

            mOverlayBufIndex++;

            if(mOverlayNumOfBuf <= mOverlayBufIndex)
                mOverlayBufIndex = 0;

            //LOGD("######## kcoolsw : sending 2 \n");
        }
        Overlay_done :

        //	LOG_CAMERA_PREVIEW("previewThread: addr_y(0x%08X) addr_cbcr(0x%08X)", addrs[index].addr_y, addrs[index].addr_cbcr);

        if(mRecordRunning == true)
        {
            #ifdef SEND_YUV_RECORD_DATA
            {
                int width, height;
                unsigned int frameSize;
                unsigned char* virYAddr;
                unsigned char* virCAddr;
                mSecCamera->getPreviewSize(&width, &height, &frameSize);
                mSecCamera->getYUVBuffers(&virYAddr, &virCAddr, index);

                recordBuffer = new MemoryBase(mRecordHeap, 0, frameSize);
                //memcpy(mRecordHeap->base(), (void*)virYAddr, width*height);
                //memcpy(mRecordHeap->base()+(width*height),(void*)virCAddr, width*height*0.5);
                memcpy(mRecordHeap->base(),
                       (void*)virYAddr,
                       ALIGN_TO_8KB(ALIGN_TO_128B(width) * ALIGN_TO_32B(height)));

                memcpy(mRecordHeap->base() + ALIGN_TO_8KB(ALIGN_TO_128B(width) * ALIGN_TO_32B(height)),
                      (void*)virCAddr,
                      ALIGN_TO_8KB(ALIGN_TO_128B(width) * ALIGN_TO_32B(height/2)));
            }
            #else
            {
                #ifdef DUAL_PORT_RECORDING
                {
                    index = mSecCamera->getRecord();
                    if(index < 0) {
                        LOGE("ERR(%s):Fail on SecCamera->getRecord()", __FUNCTION__);
                        return UNKNOWN_ERROR;
                    }
                    phyYAddr = mSecCamera->getRecPhyAddrY(index);
                    phyCAddr = mSecCamera->getRecPhyAddrC(index);
                    if(phyYAddr == 0xffffffff || phyCAddr == 0xffffffff) {
                        LOGE("ERR(%s):Fail on SecCamera getRectPhyAddr Y addr = %0x C addr = %0x", __FUNCTION__, phyYAddr, phyCAddr);
                        return UNKNOWN_ERROR;
                    }
                }
                #endif//DUAL_PORT_RECORDING

                struct ADDRS *addrs = (struct ADDRS *)mRecordHeap->base();

                recordBuffer = new MemoryBase(mRecordHeap, sizeof(struct ADDRS) * index , sizeof(struct ADDRS));
                addrs[index].addr_y    = phyYAddr;
                addrs[index].addr_cbcr = phyCAddr;
            }
            #endif
        }
    }

    if(previewBuffer != 0)
    {
        // Notify the client of a new frame. //Kamat --eclair
        if((mMsgEnabled & CAMERA_MSG_PREVIEW_FRAME) && mDataCb) {
            mDataCb(CAMERA_MSG_PREVIEW_FRAME, previewBuffer, mCallbackCookie);
        }
    }

    if(recordBuffer != 0)
    {
        // Notify the client of a new frame. //Kamat --eclair
        if((mMsgEnabled & CAMERA_MSG_VIDEO_FRAME) && mDataCbTimestamp)
            mDataCbTimestamp(systemTime(SYSTEM_TIME_MONOTONIC), CAMERA_MSG_VIDEO_FRAME, recordBuffer, mCallbackCookie);
    }

    // buffer.clear();
    // recordBuffer.clear();

    return NO_ERROR;
}


int CameraHardwareSec::m_autoFocusThreadFunc()
{
    LOGV("%s()", __FUNCTION__);

    Mutex::Autolock autoFocusLock(&mAutofocusLock);

    int  flagFocused    = 0;
    bool flagAFSucced  = true;

    if(   mAFMode == SecCamera::AUTO_FOCUS_AUTO
       || mAFMode == SecCamera::AUTO_FOCUS_MACRO)
    {
        if(mSecCamera->runAF(SecCamera::FLAG_ON, &flagFocused) < 0)
	        LOGE("ERR(%s):Fail on mSecCamera->runAF()", __FUNCTION__);
    }
    else
	    flagFocused = 1;

    if(flagFocused == 1)
		flagAFSucced = true;
	else
		flagAFSucced = false;


    if((mMsgEnabled & CAMERA_MSG_FOCUS) && mNotifyCb)
        mNotifyCb(CAMERA_MSG_FOCUS, flagAFSucced, 0, mCallbackCookie);

    return NO_ERROR;
}


int CameraHardwareSec::m_pictureThreadFunc()
{
    LOGV("%s()", __FUNCTION__);

    Mutex::Autolock cameraLock(&mCameraLock);

    int ret = NO_ERROR;

    sp<MemoryBase>  rawBuffer = NULL;
    int             pictureWidth  = 0;
    int             pictureHeight = 0;
    int             pictureFormat = 0;
    unsigned int    picturePhyAddr = 0;
    bool            flagShutterCallback = false;

    unsigned char * jpegData = NULL;
    unsigned int    jpegSize = 0;

#ifdef JPEG_FROM_SENSOR
    pictureWidth  = 640;
    pictureHeight = 480;
#else
    mSecCamera->getSnapshotSize(&pictureWidth, &pictureHeight);
#endif

    if((mMsgEnabled & CAMERA_MSG_RAW_IMAGE) && mDataCb)
    {
        pictureFormat = mSecCamera->getSnapshotPixelFormat();

        if(0 <= m_getGpsInfo(&mParameters, &mGpsInfo))
        {
            if(mSecCamera->setGpsInfo(mGpsInfo.latitude,  mGpsInfo.longitude,
			                        mGpsInfo.timestamp, mGpsInfo.altitude) < 0)
            {
	            LOGE("%s::setGpsInfo fail.. but making jpeg is progressing\n", __func__);
            }
        }

#ifdef JPEG_FROM_SENSOR
        jpegData = mSecCamera->getJpeg(&jpegSize, &picturePhyAddr);
        if(jpegData == NULL)
        {
            LOGE("ERR(%s):Fail on SecCamera->getJpeg()", __FUNCTION__);
            picturePhyAddr = 0;
            ret = UNKNOWN_ERROR;
        }
#else
        picturePhyAddr = mSecCamera->getSnapshot();
        if(picturePhyAddr == 0)
        {
            LOGE("ERR(%s):Fail on SecCamera->getSnapshot()", __FUNCTION__);
            ret = UNKNOWN_ERROR;
        }

#endif
        if(picturePhyAddr != 0)
        {
            rawBuffer = new MemoryBase(mRawHeap, 0, sizeof(struct ADDRS_CAP));
            struct ADDRS_CAP *addrs = (struct ADDRS_CAP *)mRawHeap->base();

            addrs[0].addr_y = picturePhyAddr;
            addrs[0].width  = pictureWidth;
            addrs[0].height = pictureHeight;
        }

        if((mMsgEnabled & CAMERA_MSG_SHUTTER) && mNotifyCb)
        {
            image_rect_type size;
            size.width  = pictureWidth;
            size.height = pictureHeight;

            mNotifyCb(CAMERA_MSG_SHUTTER, (int32_t)&size, 0, mCallbackCookie);

            flagShutterCallback = true;
        }

        mDataCb(CAMERA_MSG_RAW_IMAGE, rawBuffer, mCallbackCookie);
    }

    if(   (flagShutterCallback == false)
       && (mMsgEnabled & CAMERA_MSG_SHUTTER) && mNotifyCb)
    {
        image_rect_type size;

        size.width  = pictureWidth;
        size.height = pictureHeight;

        mNotifyCb(CAMERA_MSG_SHUTTER, (int32_t)&size, 0, mCallbackCookie);

        flagShutterCallback = true;
    }

    if((mMsgEnabled & CAMERA_MSG_COMPRESSED_IMAGE) && mDataCb)
    {
        #ifdef JPEG_FROM_SENSOR
        #else
            if(picturePhyAddr != 0)
                jpegData = mSecCamera->yuv2Jpeg((unsigned char*)picturePhyAddr, 0, &jpegSize,
                                                  pictureWidth, pictureHeight, pictureFormat);
        #endif

        sp<MemoryBase> jpegMem = NULL;
        if(jpegData != NULL)
        {
            sp<MemoryHeapBase> jpegHeap = new MemoryHeapBase(jpegSize);
                               jpegMem  = new MemoryBase(jpegHeap, 0, jpegSize);

            memcpy(jpegHeap->base(), jpegData, jpegSize);
        }

        mDataCb(CAMERA_MSG_COMPRESSED_IMAGE, jpegMem, mCallbackCookie);
    }

    return ret;
}


int CameraHardwareSec::m_getGpsInfo(CameraParameters * camParams, gps_info * gps)
{
	int flag_gps_info_valid = 1;
	int each_info_valid = 1;

	if(camParams == NULL || gps == NULL)
		return -1;

	#define PARSE_LOCATION(what,type,fmt,desc)                                        \
	do{                                                                               \
		/*if(flag_gps_info_valid)*/                                                   \
		{                                                                             \
			/*gps->what = 0;*/                                                        \
			const char *what##_str = camParams->get("gps-"#what);                     \
			if (what##_str)                                                           \
			{                                                                         \
				type what = 0;                                                        \
				if (sscanf(what##_str, fmt, &what) == 1)                              \
					gps->what = what;                                                 \
				else                                                                  \
				{                                                                     \
					LOGE("GPS " #what " %s could not"                                 \
						  " be parsed as a " #desc,                                   \
						  what##_str);                                                \
					each_info_valid = 0;                                              \
				}                                                                     \
			}                                                                         \
			else                                                                      \
			{                                                                         \
				/*LOGD("%s:: GPS " #what " not specified\n", __func__);*/             \
				each_info_valid = 0;                                                  \
		   }                                                                          \
		}                                                                             \
	} while(0)

	PARSE_LOCATION(latitude,  double, "%lf", "double float");
	PARSE_LOCATION(longitude, double, "%lf", "double float");

	if(each_info_valid == 0)
		flag_gps_info_valid = 0;

	PARSE_LOCATION(timestamp, long, "%ld", "long");
	if (!gps->timestamp)
		gps->timestamp = time(NULL);

	PARSE_LOCATION(altitude, int, "%d", "int");

	#undef PARSE_LOCATION

	if(flag_gps_info_valid == 1)
		return 0;
	else
		return -1;
}


#ifdef RUN_VARIOUS_EFFECT_TEST
void CameraHardwareSec::m_runEffectTest(void)
{
		   int cur_zoom  = 1;
	static int next_zoom = 1;

	// zoom test
	cur_zoom = mSecCamera->getZoom();
	if(cur_zoom == SecCamera::ZOOM_BASE)
		next_zoom = 1;
	if(cur_zoom == SecCamera::ZOOM_MAX)
		next_zoom = -1;
	mSecCamera->setZoom(cur_zoom + next_zoom);

	// scene_test : portrait mode die..
		   int cur_scene  = 1;
	static int next_scene = 1;
	cur_scene = mSecCamera->getSceneMode();
	if(cur_scene == SecCamera::SCENE_MODE_BASE +1)
		next_scene = 1;
	if(cur_scene == SecCamera::SCENE_MODE_MAX  -1)
		next_scene = -1;
	mSecCamera->setSceneMode(cur_scene + next_scene);

	// wb test
			int cur_wb  = 1;
	static int next_wb = 1;
	cur_wb = mSecCamera->getWhiteBalance();
	if(cur_wb == SecCamera::WHITE_BALANCE_BASE +1)
		next_wb = 1;
	if(cur_wb == SecCamera::WHITE_BALANCE_MAX  -1)
		next_wb = -1;
	mSecCamera->setWhiteBalance(cur_wb + next_wb);

	// imageeffect mode
			int cur_effect = 1;
	static int next_effect = 1;
	// color effect
	cur_effect = mSecCamera->getImageEffect();
	if(cur_effect == SecCamera::IMAGE_EFFECT_BASE +1)
		next_effect = 1;
	if(cur_effect == SecCamera::IMAGE_EFFECT_MAX  -1)
		next_effect = -1;
	mSecCamera->setImageEffect(cur_effect + next_effect);

	// brightness : die..
			int cur_br = 1;
	static int next_br = 1;

	cur_br = mSecCamera->getBrightness();
	if(cur_br == SecCamera::BRIGHTNESS_BASE)
		next_br = 1;
	if(cur_br == SecCamera::BRIGHTNESS_MAX)
		next_br = -1;
	mSecCamera->setBrightness(cur_br + next_br);

	// contrast test
		   int cur_cont = 1;
	static int next_cont= 1;
	cur_cont = mSecCamera->getContrast();
	if(cur_cont == SecCamera::CONTRAST_BASE)
		next_cont = 1;
	if(cur_cont == SecCamera::CONTRAST_MAX)
		next_cont = -1;
	mSecCamera->setContrast(cur_cont + next_cont);

	// saturation
		   int cur_sat = 1;
	static int next_sat= 1;
	cur_sat = mSecCamera->getSaturation();
	if(cur_sat == SecCamera::SATURATION_BASE)
		next_sat = 1;
	if(cur_sat == SecCamera::SATURATION_MAX)
		next_sat = -1;
	mSecCamera->setSaturation(cur_sat + next_sat);

	// sharpness
		   int cur_sharp = 1;
	static int next_sharp= 1;
	cur_sharp = mSecCamera->getSharpness();
	if(cur_sharp == SecCamera::SHARPNESS_BASE)
		next_sharp = 1;
	if(cur_sharp == SecCamera::SHARPNESS_MAX)
		next_sharp = -1;
	mSecCamera->setSharpness(cur_sharp + next_sharp);

	// metering
		   int cur_met = 1;
	static int next_met= 1;
	cur_met = mSecCamera->getMetering();
	if(cur_met == SecCamera::METERING_BASE   +1)
		next_met = 1;
	if(cur_met == SecCamera::METERING_MAX    -1)
		next_met = -1;
	mSecCamera->setMetering(cur_met + next_met);
	//
}
#endif // RUN_VARIOUS_EFFECT_TEST


wp<CameraHardwareInterface> CameraHardwareSec::singleton;

sp<CameraHardwareInterface> CameraHardwareSec::createInstance(int cameraId)
{
    LOGV("%s()", __FUNCTION__);
    if (singleton != 0) {
        sp<CameraHardwareInterface> hardware = singleton.promote();
        if (hardware != 0) {
            return hardware;
        }
    }
    sp<CameraHardwareInterface> hardware(new CameraHardwareSec());
    singleton = hardware;
    return hardware;
}

static CameraInfo sCameraInfo[] = {
    {
        CAMERA_FACING_BACK,
        90,  /* orientation */
    },
    {
        CAMERA_FACING_FRONT,
        270,  /* orientation */
    }
};
    
extern "C" int HAL_getNumberOfCameras()
{
    return sizeof(sCameraInfo) / sizeof(sCameraInfo[0]);
}
    
extern "C" void HAL_getCameraInfo(int cameraId, struct CameraInfo *cameraInfo)
{
    memcpy(cameraInfo, &sCameraInfo[cameraId], sizeof(CameraInfo));
}
    
extern "C" sp<CameraHardwareInterface> HAL_openCameraHardware(int cameraId)
{
    return CameraHardwareSec::createInstance(cameraId);
}

}; // namespace android
