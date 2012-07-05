/*
**
** Copyright 2008, The Android Open Source Project
** Copyright 2010, Samsung Electronics Co. LTD
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

#ifndef ANDROID_HARDWARE_CAMERA_HARDWARE_SEC_H
#define ANDROID_HARDWARE_CAMERA_HARDWARE_SEC_H

#include "SecCamera.h"
#include "SecCameraParameters.h"

#include <utils/threads.h>
#include <utils/RefBase.h>
#include <binder/MemoryBase.h>
#include <binder/MemoryHeapBase.h>

#include <hardware/camera.h>
#include <hardware/gralloc.h>

namespace android {
class CameraHardwareSec : public virtual RefBase {
public:
    CameraHardwareSec(int cameraId);
    ~CameraHardwareSec();

    void        setCallbacks(camera_notify_callback notify_cb,
                             camera_data_callback data_cb,
                             camera_data_timestamp_callback data_cb_timestamp,
                             camera_request_memory get_memory,
                             void *user);

    void        enableMsgType(int32_t msgType);
    void        disableMsgType(int32_t msgType);
    bool        msgTypeEnabled(int32_t msgType);

    status_t    setPreviewWindow(struct preview_stream_ops *window);
    status_t    storeMetaDataInBuffers(bool enable);

    status_t    startPreview();
    void        stopPreview();
    bool        previewEnabled();

    status_t    startRecording();
    void        stopRecording();
    bool        recordingEnabled();
    void        releaseRecordingFrame(const void *opaque);

    status_t    startFaceDetection();
    status_t    stopFaceDetection();

    status_t    init();
    status_t    autoFocus();
    status_t    cancelAutoFocus();
    status_t    takePicture();
    status_t    cancelPicture();
    status_t    setParameters(const char *parameters);
    status_t    setParameters(const CameraParameters& params);
    char*       getParameters() const;
    void        putParameters(char *parms);
    status_t    sendCommand(int32_t command, int32_t arg1,
                                    int32_t arg2);
    void        release();

private:
    static  const int   kBufferCount = MAX_BUFFERS;

    class PreviewThread : public Thread {
        CameraHardwareSec *mHardware;
    public:
        PreviewThread(CameraHardwareSec *hw):
        Thread(false),
        mHardware(hw) { }
        virtual void onFirstRef() {
            run("CameraPreviewThread", PRIORITY_URGENT_DISPLAY);
        }
        virtual bool threadLoop() {
            mHardware->previewThreadWrapper();
            return false;
        }
    };

    class PictureThread : public Thread {
        CameraHardwareSec *mHardware;
    public:
        PictureThread(CameraHardwareSec *hw):
        Thread(false),
        mHardware(hw) { }
        virtual bool threadLoop() {
            mHardware->pictureThread();
            mHardware->mSecCamera->endSnapshot();
            return false;
        }
    };

    class AutoFocusThread : public Thread {
        CameraHardwareSec *mHardware;
    public:
        AutoFocusThread(CameraHardwareSec *hw): Thread(false), mHardware(hw) { }
        virtual void onFirstRef() {
            run("CameraAutoFocusThread", PRIORITY_DEFAULT);
        }
        virtual bool threadLoop() {
            mHardware->autoFocusThread();
            return true;
        }
    };

    void                initDefaultParameters(int cameraId);

    status_t            startPreview_l();
    void                stopPreview_l();

    sp<PreviewThread>   mPreviewThread;
            int         previewThread();
            int         previewThreadWrapper();

    sp<AutoFocusThread> mAutoFocusThread;
            int         autoFocusThread();

    sp<PictureThread>   mPictureThread;
            int         pictureThread();
            bool        mCaptureInProgress;

            bool        scaleDownYuv422(char *srcBuf, uint32_t srcWidth,
                                        uint32_t srcHight, char *dstBuf,
                                        uint32_t dstWidth, uint32_t dstHight);

            void        setSkipFrame(int frame);
            bool        isSupportedPreviewSize(const int width,
                                               const int height) const;
    /* used by auto focus thread to block until it's told to run */
    mutable Mutex       mFocusLock;
    mutable Condition   mFocusCondition;
    bool                mExitAutoFocusThread;

    /* used by preview thread to block until it's told to run */
    mutable Mutex       mPreviewLock;
    mutable Condition   mPreviewCondition;
    mutable Condition   mPreviewStoppedCondition;
    bool                mPreviewRunning;
    bool                mPreviewStartDeferred;
    bool                mExitPreviewThread;
    bool                mFaceDetectStarted;

    /* used to guard threading state */
    mutable Mutex       mStateLock;

    CameraParameters    mParameters;
    CameraParameters    mInternalParameters;

    camera_memory_t*    mPreviewMemory;
    camera_memory_t*    mRawHeap;
    camera_memory_t*    mRecordHeap;

    SecCamera           *mSecCamera;
    const __u8          *mCameraSensorName;

    mutable Mutex       mSkipFrameLock;
    int                 mSkipFrame;

    preview_stream_ops* mWindow;

    camera_notify_callback mNotifyCb;
    camera_data_callback mDataCb;
    camera_data_timestamp_callback mDataCbTimestamp;
    camera_request_memory mGetMemoryCb;
    void                *mCallbackCookie;

    int32_t             mMsgEnabled;

    bool                mRecordRunning;
    mutable Mutex       mRecordLock;
    int                 mPostViewWidth;
    int                 mPostViewHeight;
    int                 mPostViewSize;

    Vector<Size>        mSupportedPreviewSizes;

    static gralloc_module_t const* mGrallocHal;
};

}; // namespace android

#endif
