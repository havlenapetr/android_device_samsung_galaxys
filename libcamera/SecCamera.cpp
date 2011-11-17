/*
 * Copyright@ Samsung Electronics Co. LTD
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
************************************
* Filename: SecCamera.cpp
* Author:   Sachin P. Kamat
* Purpose:  This file interacts with the Camera and JPEG drivers.
*************************************
*/

//#define LOG_NDEBUG 0
#define LOG_TAG "SecCamera"
#include <utils/Log.h>

#include "SecCamera.h"
#include <videodev2_samsung.h>

#define CHECK(return_value)                                         \
    if((return_value) < 0) {                                        \
        LOGE("%s::%d fail\n", __func__,__LINE__);                   \
        return -1;                                                  \
    }

#define CHECK_PTR(return_value)                                     \
    if((return_value) < 0) {                                        \
        LOGE("%s::%d fail\n", __func__,__LINE__);                   \
        return NULL;                                                \
    }

#define ALIGN_TO_32B(x)   ((((x) + (1 <<  5) - 1) >>  5) <<  5)
#define ALIGN_TO_128B(x)  ((((x) + (1 <<  7) - 1) >>  7) <<  7)
#define ALIGN_TO_8KB(x)   ((((x) + (1 << 13) - 1) >> 13) << 13)

namespace android {

// ======================================================================
// Camera controls

static struct timeval time_start;
static struct timeval time_stop;

unsigned long measure_time(struct timeval *start, struct timeval *stop)
{
    unsigned long sec, usec, time;

    sec = stop->tv_sec - start->tv_sec;

    if (stop->tv_usec >= start->tv_usec) {
        usec = stop->tv_usec - start->tv_usec;
    } else {
        usec = stop->tv_usec + 1000000 - start->tv_usec;
        sec--;
    }

    time = (sec * 1000000) + usec;

    return time;
}

static inline unsigned long check_performance()
{
    unsigned long time = 0;
    static unsigned long max=0, min=0xffffffff;

    if(time_start.tv_sec == 0 && time_start.tv_usec == 0) {
        gettimeofday(&time_start, NULL);
    } else {
        gettimeofday(&time_stop, NULL);
        time = measure_time(&time_start, &time_stop);
        if(max < time) max = time;
        if(min > time) min = time;
        LOGV("Interval: %lu us (%2.2lf fps), min:%2.2lf fps, max:%2.2lf fps\n", time, 1000000.0/time, 1000000.0/max, 1000000.0/min);
        gettimeofday(&time_start, NULL);
    }

    return time;
}

static int close_buffers(struct fimc_buffer *buffers)
{
    int i;

    for (i = 0; i < MAX_BUFFERS; i++) {
        if (buffers[i].start) {
            munmap(buffers[i].start, buffers[i].length);
            //LOGV("munmap():virt. addr[%d]: 0x%x size = %d\n", i, (unsigned int) buffers[i].start, buffers[i].length);
            buffers[i].start = NULL;
        }
    }

    return 0;
}

static int get_pixel_depth(unsigned int fmt)
{
    int depth = 0;

    switch (fmt)
    {
        case V4L2_PIX_FMT_NV12:
            depth = 12;
            break;
        case V4L2_PIX_FMT_NV12T:
            depth = 12;
            break;
        case V4L2_PIX_FMT_NV21:
            depth = 12;
            break;
        case V4L2_PIX_FMT_YUV420:
            depth = 12;
            break;

        case V4L2_PIX_FMT_RGB565:
        case V4L2_PIX_FMT_YUYV:
        case V4L2_PIX_FMT_YVYU:
        case V4L2_PIX_FMT_UYVY:
        case V4L2_PIX_FMT_VYUY:
        case V4L2_PIX_FMT_NV16:
        case V4L2_PIX_FMT_NV61:
        case V4L2_PIX_FMT_YUV422P:
            depth = 16;
            break;

        case V4L2_PIX_FMT_RGB32:
            depth = 32;
            break;
    }

    return depth;
}

#define ALIGN_W(x)      ((x+0x7F)&(~0x7F)) // Set as multiple of 128
#define ALIGN_H(x)      ((x+0x1F)&(~0x1F)) // Set as multiple of 32
#define ALIGN_BUF(x)    ((x+0x1FFF)&(~0x1FFF)) // Set as multiple of 8K

static int init_yuv_buffers(struct fimc_buffer *buffers, int width, int height, unsigned int fmt)
{
    int i, len;

    len = (width * height * get_pixel_depth(fmt)) / 8;

    for (i = 0; i < MAX_BUFFERS; i++) {
        if(fmt==V4L2_PIX_FMT_NV12T) {
            buffers[i].start = NULL;
            buffers[i].length = ALIGN_BUF(ALIGN_W(width) * ALIGN_H(height))+ ALIGN_BUF(ALIGN_W(width) * ALIGN_H(height/2));
        } else {
            buffers[i].start = NULL;
            buffers[i].length = len;
        }
    }

    return 0;
}

static int fimc_poll(struct pollfd *events)
{
    int ret;

    ret = poll(events, 1, 5000);
    if (ret < 0) {
        LOGE("ERR(%s):poll error\n", __FUNCTION__);
        return ret;
    }

    if (ret == 0) {
        LOGE("ERR(%s):No data in 5 secs..\n", __FUNCTION__);
        return ret;
    }

    return ret;
}
#ifdef DUMP_YUV
static int save_yuv(struct fimc_buffer *m_buffers_c, int width, int height, int depth, int index, int frame_count)
{
    FILE *yuv_fp = NULL;
    char filename[100], *buffer = NULL;

    /* file create/open, note to "wb" */
    yuv_fp = fopen("/sdcard/camera_dump.yuv", "wb");
    if (yuv_fp==NULL) {
        LOGE("Save YUV] file open error");
        return -1;
    }

    buffer = (char *) malloc(m_buffers_c[index].length);
    if(buffer == NULL) {
        LOGE("Save YUV] buffer alloc failed");
        if(yuv_fp) fclose(yuv_fp);
        return -1;
    }

    memcpy(buffer, m_buffers_c[index].start, m_buffers_c[index].length);

    fflush(stdout);

    fwrite(buffer, 1, m_buffers_c[index].length, yuv_fp);

    fflush(yuv_fp);

    if(yuv_fp)
        fclose(yuv_fp);
    if(buffer)
        free(buffer);

    return 0;
}
#endif //DUMP_YUV

static int fimc_v4l2_querycap(int fp)
{
    struct v4l2_capability cap;
    int ret = 0;

    ret = ioctl(fp, VIDIOC_QUERYCAP, &cap);

    if (ret < 0) {
        LOGE("ERR(%s):VIDIOC_QUERYCAP failed\n", __FUNCTION__);
        return -1;
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        LOGE("ERR(%s):no capture devices\n", __FUNCTION__);
        return -1;
    }

    return ret;
}

static int fimc_v4l2_enuminput(int fp, int camera_index)
{
    struct v4l2_input input;

    input.index = camera_index;

    if(ioctl(fp, VIDIOC_ENUMINPUT, &input) != 0) {
        LOGE("ERR(%s):No matching index found\n", __FUNCTION__);
        return -1;
    }
    //LOGI("Name of input channel[%d] is %s\n", input.index, input.name);

    return input.index;
}


static int fimc_v4l2_s_input(int fp, int index)
{
    struct v4l2_input input;
    int ret;

    input.index = index;

    ret = ioctl(fp, VIDIOC_S_INPUT, &input);
    if (ret < 0) {
        LOGE("ERR(%s):VIDIOC_S_INPUT failed\n", __FUNCTION__);
        return ret;
    }

    return ret;
}

static int fimc_v4l2_s_fmt(int fp, int width, int height, unsigned int fmt, int flag_capture)
{
    struct v4l2_format v4l2_fmt;
    struct v4l2_pix_format pixfmt;
    int ret;

    v4l2_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    pixfmt.width = width;
    pixfmt.height = height;
    pixfmt.pixelformat = fmt;

    pixfmt.sizeimage = (width * height * get_pixel_depth(fmt)) / 8;

    v4l2_fmt.fmt.pix = pixfmt;

    /* Set up for capture */
    ret = ioctl(fp, VIDIOC_S_FMT, &v4l2_fmt);
    if (ret < 0) {
        LOGE("ERR(%s):VIDIOC_S_FMT failed\n", __FUNCTION__);
        return -1;
    }

    return 0;
}

static int fimc_v4l2_s_fmt_cap(int fp, int width, int height, unsigned int fmt)
{
    struct v4l2_format v4l2_fmt;
    struct v4l2_pix_format pixfmt;
    int ret;

    v4l2_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    pixfmt.width = width;
    pixfmt.height = height;
    pixfmt.pixelformat = fmt;
    pixfmt.colorspace = V4L2_COLORSPACE_JPEG;

    //pixfmt.sizeimage = (width * height * get_pixel_depth(fmt)) / 8;
    pixfmt.sizeimage = (width * height * get_pixel_depth(fmt)) >> 3;

    v4l2_fmt.fmt.pix = pixfmt;

    /* Set up for capture */
    ret = ioctl(fp, VIDIOC_S_FMT, &v4l2_fmt);
    if (ret < 0) {
        LOGE("ERR(%s):VIDIOC_S_FMT failed\n", __FUNCTION__);
        return ret;
    }

    return ret;
}

static int fimc_v4l2_enum_fmt(int fp, unsigned int fmt)
{
    struct v4l2_fmtdesc fmtdesc;
    int found = 0;

    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmtdesc.index = 0;

    while (ioctl(fp, VIDIOC_ENUM_FMT, &fmtdesc) == 0) {
        if (fmtdesc.pixelformat == fmt) {

            #ifdef CAMERA_HW_DEBUG
                LOGD("passed fmt = %d found pixel format[%d]: %s\n", fmt, fmtdesc.index, fmtdesc.description);
            #endif

            found = 1;
            break;
        }

        fmtdesc.index++;
    }

    if (!found) {
        LOGE("unsupported pixel format\n");
        return -1;
    }

    return 0;
}

static int fimc_v4l2_reqbufs(int fp, enum v4l2_buf_type type, int nr_bufs)
{
    struct v4l2_requestbuffers req;
    int ret;

    req.count = nr_bufs;
    req.type = type;
    req.memory = V4L2_MEMORY_MMAP;

    ret = ioctl(fp, VIDIOC_REQBUFS, &req);
    if(ret < 0) {
	LOGE("ERR(%s):VIDIOC_REQBUFS failed\n", __FUNCTION__);
            return -1;
    }

    return req.count;
}

static int fimc_v4l2_querybuf(int fp, struct fimc_buffer *buffers, enum v4l2_buf_type type, int nr_frames)
{
    struct v4l2_buffer v4l2_buf;
    int i, ret;

    for(i = 0; i < nr_frames; i++) {
        v4l2_buf.type = type;
        v4l2_buf.memory = V4L2_MEMORY_MMAP;
        v4l2_buf.index = i;

        ret = ioctl(fp , VIDIOC_QUERYBUF, &v4l2_buf);
        if(ret < 0) {
            LOGE("ERR(%s):VIDIOC_QUERYBUF failed\n", __FUNCTION__);
            return -1;
        }

        if(nr_frames == 1) {
            buffers[i].length = v4l2_buf.length;
            if ((buffers[i].start = (char *) mmap(0, v4l2_buf.length,
                            PROT_READ | PROT_WRITE, MAP_SHARED,
                            fp, v4l2_buf.m.offset)) < 0) {
                LOGE("%s %d] mmap() failed\n",__FUNCTION__, __LINE__);
                return -1;
            }

            //LOGV("buffers[%d].start = %p v4l2_buf.length = %d", i, buffers[i].start, v4l2_buf.length);
        } else {
#if defined DUMP_YUV || defined (SEND_YUV_RECORD_DATA)
            buffers[i].length = v4l2_buf.length;
            if ((buffers[i].start = (char *) mmap(0, v4l2_buf.length,
                            PROT_READ | PROT_WRITE, MAP_SHARED,
                            fp, v4l2_buf.m.offset)) < 0) {
                LOGE("%s %d] mmap() failed\n",__FUNCTION__, __LINE__);
                return -1;
            }

            //LOGV("buffers[%d].start = %p v4l2_buf.length = %d", i, buffers[i].start, v4l2_buf.length);
#endif
        }

    }

    return 0;
}

static int fimc_v4l2_streamon(int fp)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    int ret;

    ret = ioctl(fp, VIDIOC_STREAMON, &type);
    if (ret < 0) {
        LOGE("ERR(%s):VIDIOC_STREAMON failed\n", __FUNCTION__);
        return ret;
    }

    return ret;
}

static int fimc_v4l2_streamoff(int fp)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    int ret;

    ret = ioctl(fp, VIDIOC_STREAMOFF, &type);
    if (ret < 0) {
        LOGE("ERR(%s):VIDIOC_STREAMOFF failed\n", __FUNCTION__);
        return ret;
    }

    return ret;
}

static int fimc_v4l2_qbuf(int fp, int index)
{
    struct v4l2_buffer v4l2_buf;
    int ret;

    v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    v4l2_buf.memory = V4L2_MEMORY_MMAP;
    v4l2_buf.index = index;

    ret = ioctl(fp, VIDIOC_QBUF, &v4l2_buf);
    if (ret < 0) {
        LOGE("ERR(%s):VIDIOC_QBUF failed\n", __FUNCTION__);
        return ret;
    }

    return 0;
}

static int fimc_v4l2_dqbuf(int fp)
{
    struct v4l2_buffer v4l2_buf;
    int ret;

    v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    v4l2_buf.memory = V4L2_MEMORY_MMAP;

    ret = ioctl(fp, VIDIOC_DQBUF, &v4l2_buf);
    if (ret < 0) {
        LOGE("ERR(%s):VIDIOC_DQBUF failed\n", __FUNCTION__);
        return ret;
    }

    return v4l2_buf.index;
}

static int fimc_v4l2_g_ctrl(int fp, unsigned int id)
{
    struct v4l2_control ctrl;
    int ret;

    ctrl.id = id;

    ret = ioctl(fp, VIDIOC_G_CTRL, &ctrl);
    if (ret < 0) {
        LOGE("ERR(%s):VIDIOC_G_CTRL failed\n", __FUNCTION__);
        return ret;
    }

    return ctrl.value;
}

static int fimc_v4l2_s_ctrl(int fp, unsigned int id, unsigned int value)
{
    struct v4l2_control ctrl;
    int ret;

    ctrl.id = id;
    ctrl.value = value;

    ret = ioctl(fp, VIDIOC_S_CTRL, &ctrl);
    if (ret < 0) {
        LOGE("ERR(%s):VIDIOC_S_CTRL failed\n", __FUNCTION__);
        return ret;
    }

    return ctrl.value;
}

static int fimc_v4l2_g_parm(int fp)
{
    struct v4l2_streamparm stream;
    int ret;

    stream.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    ret = ioctl(fp, VIDIOC_G_PARM, &stream);
    if (ret < 0) {
        LOGE("ERR(%s):VIDIOC_G_PARM failed\n", __FUNCTION__);
        return -1;
    }

    /*
    LOGV("timeperframe: numerator %d, denominator %d\n", \
        stream.parm.capture.timeperframe.numerator, \
        stream.parm.capture.timeperframe.denominator);
        */

    return 0;
}

static int fimc_v4l2_s_parm(int fp, int fps_numerator, int fps_denominator)
{
    struct v4l2_streamparm stream;
    int ret;

    stream.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    stream.parm.capture.capturemode = 0;
    stream.parm.capture.timeperframe.numerator   = fps_numerator;
    stream.parm.capture.timeperframe.denominator = fps_denominator;

    ret = ioctl(fp, VIDIOC_S_PARM, &stream);
    if (ret < 0) {
        LOGE("ERR(%s):VIDIOC_S_PARM failed\n", __FUNCTION__);
        return ret;
    }

    return 0;
}

#if 0
static int fimc_v4l2_s_parm_ex(int fp, int mode, int no_dma_op) //Kamat: not present in new code
{
    struct v4l2_streamparm stream;
    int ret;

    stream.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    stream.parm.capture.capturemode = mode;
    if(no_dma_op)
        stream.parm.capture.reserved[0] = 100;

    ret = ioctl(fp, VIDIOC_S_PARM, &stream);
    if (ret < 0) {
        LOGE("ERR(%s):VIDIOC_S_PARM_EX failed\n", __FUNCTION__);
        return ret;
    }

    return 0;
}
#endif

// ======================================================================
// Constructor & Destructor

SecCamera::SecCamera() :
        m_flag_create          (FLAG_OFF),
        m_cam_fd               (0),
        m_cam_fd_rec           (0),
        m_camera_id            (CAMERA_ID_BACK),
        m_preview_v4lformat    (-1),
        m_preview_width        (MAX_BACK_CAMERA_PREVIEW_WIDTH),
        m_preview_height       (MAX_BACK_CAMERA_PREVIEW_HEIGHT),
        m_preview_max_width    (MAX_BACK_CAMERA_PREVIEW_WIDTH),
        m_preview_max_height   (MAX_BACK_CAMERA_PREVIEW_HEIGHT),
        m_snapshot_v4lformat   (-1),
        m_snapshot_width       (MAX_BACK_CAMERA_SNAPSHOT_WIDTH),
        m_snapshot_height      (MAX_BACK_CAMERA_SNAPSHOT_HEIGHT),
        m_snapshot_max_width   (MAX_BACK_CAMERA_SNAPSHOT_WIDTH),
        m_snapshot_max_height  (MAX_BACK_CAMERA_SNAPSHOT_HEIGHT),
        m_flag_overlay         (FLAG_OFF),
        m_overlay_x            (0),
        m_overlay_y            (0),
        m_overlay_width        (0),
        m_overlay_height       (0),
        m_frame_rate           (FRAME_RATE_MAX),
        m_scene_mode           (SCENE_MODE_AUTO),
        m_white_balance        (WHITE_BALANCE_AUTO),
        m_image_effect         (IMAGE_EFFECT_ORIGINAL),
        m_brightness           (BRIGHTNESS_NORMAL),
        m_contrast             (CONTRAST_NORMAL),
        m_metering             (METERING_CENTER),
        m_sharpness            (SHARPNESS_NOMAL),
        m_saturation           (SATURATION_NOMAL),
        m_zoom                 (ZOOM_BASE),
        m_angle                (0),
        m_af_mode              (AUTO_FOCUS_AUTO),
        m_flag_preview_start   (FLAG_OFF),
        m_flag_current_info_changed(FLAG_ON),
        m_current_camera_id    (-1),
        m_current_frame_rate   (-1),
        m_current_metering     (-1),
        m_current_white_balance(-1),
        m_current_image_effect (IMAGE_EFFECT_ORIGINAL),
        m_current_scene_mode   (SCENE_MODE_AUTO),
        m_current_brightness   (BRIGHTNESS_NORMAL),
        m_current_contrast     (CONTRAST_NORMAL),
        m_current_sharpness    (SHARPNESS_NOMAL),
        m_current_saturation   (SATURATION_NOMAL),
        m_current_zoom         (ZOOM_BASE),
        m_current_af_mode      (AUTO_FOCUS_BASE),
        m_jpeg_fd              (0),
        m_jpeg_thumbnail_width (0),
        m_jpeg_thumbnail_height(0),
        m_jpeg_quality         (100),
        m_gps_latitude         (0.0f),
        m_gps_longitude        (0.0f),
        m_gps_timestamp        (0),
        m_gps_altitude         (169)
{
    LOGV("%s()", __FUNCTION__);

    if(this->Create() < 0)
        LOGE("ERR(%s):Create() fail\n", __FUNCTION__);
}

SecCamera::~SecCamera()
{
    LOGV("%s()", __FUNCTION__);

    this->Destroy();
}

int SecCamera::Create()
{
    LOGV("%s()", __FUNCTION__);
    int index = 0;

    if(m_flag_create == FLAG_OFF)
    {
        #ifdef JPEG_FROM_SENSOR
        #else
        {
                m_jpeg_fd = SsbSipJPEGEncodeInit();
                //LOGD("(%s):JPEG device open ID = %d\n", __FUNCTION__,m_jpeg_fd);
                if(m_jpeg_fd < 0)
                {
                    m_jpeg_fd = 0;
                    LOGE("ERR(%s):Cannot open a jpeg device file\n", __FUNCTION__);
                    goto Create_fail;
                }
        }
        #endif

        m_cam_fd = open(CAMERA_DEV_NAME, O_RDWR);
        if (m_cam_fd < 0) {
            m_cam_fd = 0;
            LOGE("ERR(%s):Cannot open %s (error : %s)\n", __FUNCTION__, CAMERA_DEV_NAME, strerror(errno));
            goto Create_fail;
        }

        int camera_index = 0;
        if(m_current_camera_id == CAMERA_ID_FRONT)
            camera_index = 1;
        else // if(m_current_camera_id == CAMERA_ID_BACK)
            camera_index = 0;

        if(fimc_v4l2_querycap(m_cam_fd) < 0)
        {
            LOGE("ERR(%s):fimc_v4l2_querycap() fail\n", __FUNCTION__);
            goto Create_fail;
        }

        index = fimc_v4l2_enuminput(m_cam_fd, camera_index);
        if(index < 0)
        {
            LOGE("ERR(%s):fimc_v4l2_enuminput() fail\n", __FUNCTION__);
            goto Create_fail;
        }

        if(fimc_v4l2_s_input(m_cam_fd, index) < 0)
        {
            LOGE("ERR(%s):fimc_v4l2_s_input() fail\n", __FUNCTION__);
            goto Create_fail;
        }

#ifdef DUAL_PORT_RECORDING
        m_cam_fd_rec = open(CAMERA_DEV_NAME2, O_RDWR);
        if (m_cam_fd_rec < 0) {
            m_cam_fd_rec = 0;
            LOGE("ERR(%s):Cannot open %s (error : %s)\n", __FUNCTION__, CAMERA_DEV_NAME2, strerror(errno));
            goto Create_fail;
        }

        if(fimc_v4l2_querycap(m_cam_fd_rec) < 0)
        {
            LOGE("ERR(%s):fimc_v4l2_querycap() fail\n", __FUNCTION__);
            goto Create_fail;
        }

        index = fimc_v4l2_enuminput(m_cam_fd_rec, camera_index);
        if(index < 0)
        {
            LOGE("ERR(%s):fimc_v4l2_enuminput() fail\n", __FUNCTION__);
            goto Create_fail;
        }

        if(fimc_v4l2_s_input(m_cam_fd_rec, index) < 0)
        {
            LOGE("ERR(%s):fimc_v4l2_s_input() fail\n", __FUNCTION__);
            goto Create_fail;
        }

#endif //DUAL_PORT_RECORDING

        m_flag_create = FLAG_ON;
    }

    return 0;

Create_fail:

    if(0 < m_cam_fd_rec)
    {
        close(m_cam_fd_rec);
        m_cam_fd_rec = 0;
    }

    if(0 < m_cam_fd)
    {
        close(m_cam_fd);
        m_cam_fd = 0;
    }

    #ifdef JPEG_FROM_SENSOR
    #else
    {
        if(0 < m_jpeg_fd)
        {
            SsbSipJPEGEncodeDeInit(m_jpeg_fd);
            m_jpeg_fd = 0;
        }
    }
    #endif

    return -1;
}

void SecCamera::Destroy()
{
    LOGV("%s()", __FUNCTION__);

    if(m_flag_create == FLAG_ON)
    {
        if(this->stopPreview() < 0)
            LOGE("ERR(%s):Fail on stopPreview()\n", __FUNCTION__);

#ifdef JPEG_FROM_SENSOR
#else
        if(0 < m_jpeg_fd)
        {
            if(SsbSipJPEGEncodeDeInit(m_jpeg_fd) != JPEG_OK)
                LOGE("ERR(%s):Fail on SsbSipJPEGEncodeDeInit\n", __FUNCTION__);

            m_jpeg_fd = 0;
        }
#endif

        if(0 < m_cam_fd)
        {
            close(m_cam_fd);
            m_cam_fd = 0;
        }

        if(0 < m_cam_fd_rec)
        {
            close(m_cam_fd_rec);
            m_cam_fd_rec = 0;
        }

        m_flag_create = FLAG_OFF;
    }
}


int SecCamera::flagCreate(void) const
{
    LOGV("%s() : %d", __FUNCTION__, m_flag_create);

    if(m_flag_create == FLAG_ON)
        return 1;
    else //if(m_flag_create == FLAG_OFF)
        return 0;
}

// ======================================================================
// Preview

int SecCamera::startPreview(void)
{
    LOGV("%s()", __FUNCTION__);

    // aleady started
    if(m_flag_preview_start == FLAG_ON) {
        LOGE("ERR(%s):Preview was already started\n", __FUNCTION__);
        return 0;
    }

    int ret = 0;

    memset(&m_events_c, 0, sizeof(m_events_c));
    m_events_c.fd = m_cam_fd;
    m_events_c.events = POLLIN | POLLERR;

    ret = fimc_v4l2_enum_fmt(m_cam_fd,m_preview_v4lformat);
    CHECK(ret);
    ret = fimc_v4l2_s_fmt(m_cam_fd, m_preview_width, m_preview_height, m_preview_v4lformat, 0);
    CHECK(ret);

    if(m_resetCamera() < 0)
    {
        LOGE("ERR(%s):m_resetCamera() fail\n", __FUNCTION__);
        return -1;
    }

    init_yuv_buffers(m_buffers_c, m_preview_width, m_preview_height, m_preview_v4lformat);
    ret = fimc_v4l2_reqbufs(m_cam_fd, V4L2_BUF_TYPE_VIDEO_CAPTURE, MAX_BUFFERS);
    CHECK(ret);
    ret = fimc_v4l2_querybuf(m_cam_fd, m_buffers_c, V4L2_BUF_TYPE_VIDEO_CAPTURE, MAX_BUFFERS);
    CHECK(ret);


    /* start with all buffers in queue */
    for (int i = 0; i < MAX_BUFFERS; i++) {
        ret = fimc_v4l2_qbuf(m_cam_fd, i);
        CHECK(ret);
    }

    ret = fimc_v4l2_streamon(m_cam_fd);
    CHECK(ret);

    // It is a delay for a new frame, not to show the previous bigger ugly picture frame.
    ret = fimc_poll(&m_events_c);
    CHECK(ret);

    m_flag_preview_start = FLAG_ON;  //Kamat check

    return 0;
}

int SecCamera::stopPreview(void)
{
    LOGV("%s()", __FUNCTION__);

    if(m_flag_preview_start == FLAG_OFF)
        return 0;

    if(m_cam_fd <= 0) {
        LOGE("ERR(%s):Camera was closed\n", __FUNCTION__);
        return -1;
    }

    close_buffers(m_buffers_c);

    int ret = fimc_v4l2_streamoff(m_cam_fd);

    m_flag_preview_start = FLAG_OFF; //Kamat check
    m_flag_current_info_changed = FLAG_ON;
    CHECK(ret);

    return 0;
}

int SecCamera::flagPreviewStart(void)
{
    LOGV("%s:started(%d)", __func__, m_flag_preview_start);

    return m_flag_preview_start;
}

//Recording
int SecCamera::startRecord(void)
{
    LOGV("%s()", __FUNCTION__);

    // aleady started
    if(m_flag_record_start == FLAG_ON) {
        LOGE("ERR(%s):Preview was already started\n", __FUNCTION__);
        return 0;
    }

    int ret = 0;

    memset(&m_events_c_rec, 0, sizeof(m_events_c));
    m_events_c_rec.fd = m_cam_fd_rec;
    m_events_c_rec.events = POLLIN | POLLERR;

    int color_format = V4L2_PIX_FMT_NV12T;

    ret = fimc_v4l2_enum_fmt(m_cam_fd_rec,color_format);
    CHECK(ret);
    ret = fimc_v4l2_s_fmt(m_cam_fd_rec, m_preview_width, m_preview_height, color_format, 0);
    CHECK(ret);

    if(m_resetCamera() < 0)
    {
        LOGE("ERR(%s):m_resetCamera() fail\n", __FUNCTION__);
        return -1;
    }

    init_yuv_buffers(m_buffers_c_rec, m_preview_width, m_preview_height, color_format);
    ret = fimc_v4l2_reqbufs(m_cam_fd_rec, V4L2_BUF_TYPE_VIDEO_CAPTURE, MAX_BUFFERS);
    CHECK(ret);
    ret = fimc_v4l2_querybuf(m_cam_fd_rec, m_buffers_c_rec, V4L2_BUF_TYPE_VIDEO_CAPTURE, MAX_BUFFERS);
    CHECK(ret);

    /* start with all buffers in queue */
    for (int i = 0; i < MAX_BUFFERS; i++) {
        ret = fimc_v4l2_qbuf(m_cam_fd_rec, i);
        CHECK(ret);
    }

    ret = fimc_v4l2_streamon(m_cam_fd_rec);
    CHECK(ret);

    // It is a delay for a new frame, not to show the previous bigger ugly picture frame.
    ret = fimc_poll(&m_events_c_rec);
    CHECK(ret);

    m_flag_record_start = FLAG_ON;  //Kamat check

    return 0;
}

int SecCamera::stopRecord(void)
{
    LOGV("%s()", __FUNCTION__);

    close_buffers(m_buffers_c_rec);

    if(m_flag_record_start == FLAG_OFF)
        return 0;

    if(m_cam_fd_rec <= 0) {
        LOGE("ERR(%s):Camera was closed\n", __FUNCTION__);
        return -1;
    }

    int ret = fimc_v4l2_streamoff(m_cam_fd_rec);

    m_flag_record_start = FLAG_OFF; //Kamat check
    CHECK(ret);

    return 0;
}

#ifdef SEND_YUV_RECORD_DATA
#define PAGE_ALIGN(x)   ((x+0xFFF)&(~0xFFF)) // Set as multiple of 4K
void SecCamera::getYUVBuffers(unsigned char** virYAddr, unsigned char** virCAddr, int index)
{
    *virYAddr = (unsigned char*)m_buffers_c[index].start;
    //*virCAddr = (unsigned char*)m_buffers_c[index].start + PAGE_ALIGN(m_preview_width * m_preview_height);
    *virCAddr = (unsigned char*)m_buffers_c[index].start + ALIGN_TO_8KB(ALIGN_TO_128B(m_preview_width) * ALIGN_TO_32B(m_preview_height));
}
#endif

void SecCamera::pausePreview()
{
    fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_STREAM_PAUSE, 0);
}

int SecCamera::getPreview()
{
    int index;

    if(m_resetCamera() < 0)
    {
        LOGE("ERR(%s):m_resetCamera() fail\n", __FUNCTION__);
        return -1;
    }

#ifdef CHECK_PREVIEW_PERFORMANCE

    LOG_TIME_DEFINE(0)
    LOG_TIME_DEFINE(1)

    LOG_TIME_START(0)
    //	fimc_poll(&m_events_c);
    LOG_TIME_END(0)
    LOG_CAMERA("fimc_poll interval: %lu us", LOG_TIME(0));

    LOG_TIME_START(1)
    index = fimc_v4l2_dqbuf(m_cam_fd);
    LOG_TIME_END(1)
    LOG_CAMERA("fimc_dqbuf interval: %lu us", LOG_TIME(1));

#else
    //	fimc_poll(&m_events_c);
    index = fimc_v4l2_dqbuf(m_cam_fd);
#endif

    if(!(0 <= index && index < MAX_BUFFERS)) {
        LOGE("ERR(%s):wrong index = %d\n", __FUNCTION__, index);
        return -1;
    }

    int ret = fimc_v4l2_qbuf(m_cam_fd, index); //Kamat: is it overhead?
    CHECK(ret);

    return index;

}

int SecCamera::getRecord()
{
    int index;

    if(m_resetCamera() < 0)
    {
        LOGE("ERR(%s):m_resetCamera() fail\n", __FUNCTION__);
        return -1;
    }

#ifdef CHECK_PREVIEW_PERFORMANCE

    LOG_TIME_DEFINE(0)
    LOG_TIME_DEFINE(1)

    LOG_TIME_START(0)
    fimc_poll(&m_events_c_rec);
    LOG_TIME_END(0)
    LOG_CAMERA("fimc_poll interval: %lu us", LOG_TIME(0));

    LOG_TIME_START(1)
    index = fimc_v4l2_dqbuf(m_cam_fd_rec);
    LOG_TIME_END(1)
    LOG_CAMERA("fimc_dqbuf interval: %lu us", LOG_TIME(1));

#else
    fimc_poll(&m_events_c_rec);
    index = fimc_v4l2_dqbuf(m_cam_fd_rec);
#endif
    if(!(0 <= index && index < MAX_BUFFERS)) {
        LOGE("ERR(%s):wrong index = %d\n", __FUNCTION__, index);
        return -1;
    }

    int ret = fimc_v4l2_qbuf(m_cam_fd_rec, index); //Kamat: is it overhead?
    CHECK(ret);

    return index;
}

int SecCamera::setPreviewSize(int width, int height, int pixel_format)
{
    LOGV("%s(width(%d), height(%d), format(%d))", __FUNCTION__, width, height, pixel_format);

    int v4lpixelformat = pixel_format;

    /*
    #if defined(LOG_NDEBUG) && LOG_NDEBUG == 0
             if(v4lpixelformat == V4L2_PIX_FMT_YUV420)  { LOGV("PreviewFormat:V4L2_PIX_FMT_YUV420"); }
        else if(v4lpixelformat == V4L2_PIX_FMT_NV12)    { LOGV("PreviewFormat:V4L2_PIX_FMT_NV12"); }
        else if(v4lpixelformat == V4L2_PIX_FMT_NV12T)   { LOGV("PreviewFormat:V4L2_PIX_FMT_NV12T"); }
        else if(v4lpixelformat == V4L2_PIX_FMT_NV21)    { LOGV("PreviewFormat:V4L2_PIX_FMT_NV21"); }
        else if(v4lpixelformat == V4L2_PIX_FMT_YUV422P) { LOGV("PreviewFormat:V4L2_PIX_FMT_YUV422P"); }
        else if(v4lpixelformat == V4L2_PIX_FMT_YUYV)    { LOGV("PreviewFormat:V4L2_PIX_FMT_YUYV"); }
        else if(v4lpixelformat == V4L2_PIX_FMT_RGB565)  { LOGV("PreviewFormat:V4L2_PIX_FMT_RGB565"); }
        else { LOGV("PreviewFormat:UnknownFormat"); }
    #endif
    */
    m_preview_width     = width;
    m_preview_height    = height;
    m_preview_v4lformat = v4lpixelformat;

    return 0;
}

int SecCamera::getPreviewSize(int * width, int * height)
{
    *width	= m_preview_width;
    *height = m_preview_height;

    return 0;
}

int SecCamera::getPreviewSize(int * width, int * height, unsigned int * frame_size)
{
    *width	    = m_preview_width;
    *height     = m_preview_height;
    *frame_size = m_frameSize(m_preview_v4lformat, m_preview_width, m_preview_height);

    return 0;
}

int SecCamera::getPreviewMaxSize(int * width, int * height)
{
    *width	= m_preview_max_width;
    *height = m_preview_max_height;

    return 0;
}

int SecCamera::getPreviewPixelFormat(void)
{
    return m_preview_v4lformat;
}


// ======================================================================
// Snapshot

unsigned int SecCamera::getSnapshot()
{
    LOGV("%s()", __FUNCTION__);

    int index;
    unsigned int addr;

    LOG_TIME_DEFINE(0)
    LOG_TIME_DEFINE(1)
    LOG_TIME_DEFINE(2)
    LOG_TIME_DEFINE(3)
    LOG_TIME_DEFINE(4)
    LOG_TIME_DEFINE(5)

    LOG_TIME_START(0)
    stopPreview();
    LOG_TIME_END(0)

    memset(&m_events_c, 0, sizeof(m_events_c));
    m_events_c.fd = m_cam_fd;
    m_events_c.events = POLLIN | POLLERR;

    fimc_v4l2_enum_fmt(m_cam_fd,m_snapshot_v4lformat);
    //fimc_v4l2_s_fmt_cap(m_cam_fd, m_snapshot_width, m_snapshot_height, m_snapshot_v4lformat);
    //LOGE("%s: width = %d, height = %d\n", __func__, m_snapshot_width, m_snapshot_height);
    fimc_v4l2_s_fmt(m_cam_fd, m_snapshot_width, m_snapshot_height, m_snapshot_v4lformat, 0);

    if(m_resetCamera() < 0)
    {
        LOGE("ERR(%s):m_resetCamera() fail\n", __FUNCTION__);
        return 0;
    }

    /*
    #if defined(LOG_NDEBUG) && LOG_NDEBUG == 0
             if(m_snapshot_v4lformat == V4L2_PIX_FMT_YUV420)  { LOGV("SnapshotFormat:V4L2_PIX_FMT_YUV420"); }
        else if(m_snapshot_v4lformat == V4L2_PIX_FMT_NV12)    { LOGV("SnapshotFormat:V4L2_PIX_FMT_NV12"); }
        else if(m_snapshot_v4lformat == V4L2_PIX_FMT_NV12T)   { LOGV("SnapshotFormat:V4L2_PIX_FMT_NV12T"); }
        else if(m_snapshot_v4lformat == V4L2_PIX_FMT_NV21)    { LOGV("SnapshotFormat:V4L2_PIX_FMT_NV21"); }
        else if(m_snapshot_v4lformat == V4L2_PIX_FMT_YUV422P) { LOGV("SnapshotFormat:V4L2_PIX_FMT_YUV422P"); }
        else if(m_snapshot_v4lformat == V4L2_PIX_FMT_YUYV)    { LOGV("SnapshotFormat:V4L2_PIX_FMT_YUYV"); }
        else if(m_snapshot_v4lformat == V4L2_PIX_FMT_UYVY)    { LOGV("SnapshotFormat:V4L2_PIX_FMT_UYVY"); }
        else if(m_snapshot_v4lformat == V4L2_PIX_FMT_RGB565)  { LOGV("SnapshotFormat:V4L2_PIX_FMT_RGB565"); }
        else { LOGV("SnapshotFormat:UnknownFormat"); }
    #endif
    */

    LOG_TIME_START(1) // prepare
    int nframe = 1;

    init_yuv_buffers(m_buffers_c, m_snapshot_width, m_snapshot_height, m_snapshot_v4lformat);
    fimc_v4l2_reqbufs(m_cam_fd, V4L2_BUF_TYPE_VIDEO_CAPTURE, nframe);
    fimc_v4l2_querybuf(m_cam_fd, m_buffers_c, V4L2_BUF_TYPE_VIDEO_CAPTURE, nframe);

    fimc_v4l2_qbuf(m_cam_fd, 0);

    fimc_v4l2_streamon(m_cam_fd);
    LOG_TIME_END(1)

    LOG_TIME_START(2) // capture
    fimc_poll(&m_events_c);
    index = fimc_v4l2_dqbuf(m_cam_fd);
    fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_STREAM_PAUSE, 0);
    LOGV("\nsnapshot dqueued buffer = %d snapshot_width = %d snapshot_height = %d\n\n",index, m_snapshot_width, m_snapshot_height);

#ifdef DUMP_YUV
    save_yuv(m_buffers_c, m_snapshot_width, m_snapshot_height, 16, index, 0);
#endif
    LOG_TIME_END(2)

    addr = getPhyAddrY(index);
    if(addr == 0)
        LOGE("%s] Physical address 0", __FUNCTION__);
    LOG_TIME_START(5) // post

    fimc_v4l2_streamoff(m_cam_fd);

#ifdef DUMP_YUV
    close_buffers(m_buffers_c);
#endif
    LOG_TIME_END(5)

    LOG_CAMERA("getSnapshot intervals : stopPreview(%lu), prepare(%lu), capture(%lu), memcpy(%lu), yuv2Jpeg(%lu), post(%lu)  us"
            , LOG_TIME(0), LOG_TIME(1), LOG_TIME(2), LOG_TIME(3), LOG_TIME(4), LOG_TIME(5));

    return addr;
}

int SecCamera::setSnapshotSize(int width, int height)
{
    LOGV("%s(width(%d), height(%d))", __FUNCTION__, width, height);

    m_snapshot_width  = width;
    m_snapshot_height = height;

    return 0;
}

int SecCamera::getSnapshotSize(int * width, int * height)
{
    *width	= m_snapshot_width;
    *height = m_snapshot_height;

    return 0;
}

int SecCamera::getSnapshotSize(int * width, int * height, unsigned int * frame_size)
{
    *width      = m_snapshot_width;
    *height     = m_snapshot_height;
    *frame_size = m_frameSize(m_snapshot_v4lformat, m_snapshot_width, m_snapshot_height);

    return 0;
}

int SecCamera::getSnapshotMaxSize(int * width, int * height)
{
    switch(m_camera_id)
    {
        case CAMERA_ID_FRONT:
            m_snapshot_max_width  = MAX_FRONT_CAMERA_SNAPSHOT_WIDTH;
            m_snapshot_max_height = MAX_FRONT_CAMERA_SNAPSHOT_HEIGHT;
            break;

        case CAMERA_ID_BACK:
        default:

            m_snapshot_max_width  = MAX_BACK_CAMERA_SNAPSHOT_WIDTH;
            m_snapshot_max_height = MAX_BACK_CAMERA_SNAPSHOT_HEIGHT;
            break;
    }

    *width	= m_snapshot_max_width;
    *height = m_snapshot_max_height;

    return 0;
}

int SecCamera::setSnapshotPixelFormat(int pixel_format)
{
    int v4lpixelformat= pixel_format;

    if(m_snapshot_v4lformat != v4lpixelformat)
        m_snapshot_v4lformat = v4lpixelformat;

    /*
    #if defined(LOG_NDEBUG) && LOG_NDEBUG == 0
        if(m_snapshot_v4lformat == V4L2_PIX_FMT_YUV420)       { LOGV("%s():SnapshotFormat:V4L2_PIX_FMT_YUV420", __FUNCTION__); }
        else if(m_snapshot_v4lformat == V4L2_PIX_FMT_NV12)    { LOGV("%s():SnapshotFormat:V4L2_PIX_FMT_NV12", __FUNCTION__); }
        else if(m_snapshot_v4lformat == V4L2_PIX_FMT_NV12T)   { LOGV("%s():SnapshotFormat:V4L2_PIX_FMT_NV12T", __FUNCTION__); }
        else if(m_snapshot_v4lformat == V4L2_PIX_FMT_NV21)    { LOGV("%s():SnapshotFormat:V4L2_PIX_FMT_NV21", __FUNCTION__); }
        else if(m_snapshot_v4lformat == V4L2_PIX_FMT_YUV422P) { LOGV("%s():SnapshotFormat:V4L2_PIX_FMT_YUV422P", __FUNCTION__); }
        else if(m_snapshot_v4lformat == V4L2_PIX_FMT_YUYV)    { LOGV("%s():SnapshotFormat:V4L2_PIX_FMT_YUYV", __FUNCTION__); }
        else if(m_snapshot_v4lformat == V4L2_PIX_FMT_UYVY)    { LOGV("%s():SnapshotFormat:V4L2_PIX_FMT_UYVY", __FUNCTION__); }
        else if(m_snapshot_v4lformat == V4L2_PIX_FMT_RGB565)  { LOGV("%s():SnapshotFormat:V4L2_PIX_FMT_RGB565", __FUNCTION__); }
        else { LOGV("SnapshotFormat:UnknownFormat"); }
    #endif
    */

    return 0;
}

int SecCamera::getSnapshotPixelFormat(void)
{
    return m_snapshot_v4lformat;
}


// ======================================================================
// Jpeg

unsigned char* SecCamera::getJpeg(unsigned int * jpeg_size, unsigned int * phyaddr)
{
    LOGV("%s()", __FUNCTION__);

    int index, ret = 0;
    unsigned char* addr;

    LOG_TIME_DEFINE(0)
    LOG_TIME_DEFINE(1)
    LOG_TIME_DEFINE(2)
    LOG_TIME_DEFINE(3)
    LOG_TIME_DEFINE(4)
    LOG_TIME_DEFINE(5)

    LOG_TIME_START(0)
    stopPreview();
    LOG_TIME_END(0)

    memset(&m_events_c, 0, sizeof(m_events_c));
    m_events_c.fd = m_cam_fd;
    m_events_c.events = POLLIN | POLLERR;

    ret = fimc_v4l2_enum_fmt(m_cam_fd,m_snapshot_v4lformat);
    CHECK_PTR(ret);

    ret = fimc_v4l2_s_fmt_cap(m_cam_fd, m_snapshot_width, m_snapshot_height, V4L2_PIX_FMT_JPEG);
    CHECK_PTR(ret);

    if(m_resetCamera() < 0)
    {
        LOGE("ERR(%s):m_resetCamera() fail\n", __FUNCTION__);
        return NULL;
    }

    LOG_TIME_START(1) // prepare
    int nframe = 1;

    init_yuv_buffers(m_buffers_c, m_snapshot_width, m_snapshot_height, m_snapshot_v4lformat);

    ret = fimc_v4l2_reqbufs(m_cam_fd, V4L2_BUF_TYPE_VIDEO_CAPTURE, nframe);
    CHECK_PTR(ret);

    ret = fimc_v4l2_querybuf(m_cam_fd, m_buffers_c, V4L2_BUF_TYPE_VIDEO_CAPTURE, nframe);
    CHECK_PTR(ret);

    ret = fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAM_JPEG_QUALITY, m_jpeg_quality);
    CHECK_PTR(ret);

    ret = fimc_v4l2_qbuf(m_cam_fd, 0);
    CHECK_PTR(ret);

    ret = fimc_v4l2_streamon(m_cam_fd);
    CHECK_PTR(ret);
    LOG_TIME_END(1)

    LOG_TIME_START(2) // capture
    ret = fimc_poll(&m_events_c);
    CHECK_PTR(ret);

    index = fimc_v4l2_dqbuf(m_cam_fd);
    if(!(0 <= index && index < MAX_BUFFERS)) {
        LOGE("ERR(%s):wrong index = %d\n", __FUNCTION__, index);
        return NULL;
    }

    int temp  = fimc_v4l2_g_ctrl(m_cam_fd, V4L2_CID_CAM_JPEG_MAIN_SIZE);
    CHECK_PTR(temp);
    *jpeg_size = (unsigned int)temp;

    int main_offset = fimc_v4l2_g_ctrl(m_cam_fd, V4L2_CID_CAM_JPEG_MAIN_OFFSET);
    CHECK_PTR(main_offset);

    int postview_offset = fimc_v4l2_g_ctrl(m_cam_fd, V4L2_CID_CAM_JPEG_POSTVIEW_OFFSET);
    CHECK_PTR(postview_offset);

    ret = fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_STREAM_PAUSE, 0);
    CHECK_PTR(ret);
    //LOGV("\nsnapshot dqueued buffer = %d snapshot_width = %d snapshot_height = %d\n\n",index, m_snapshot_width, m_snapshot_height);

    LOG_TIME_END(2)

    addr = (unsigned char*)(m_buffers_c[index].start) + main_offset;
    *phyaddr = getPhyAddrY(index) + postview_offset;

    LOG_TIME_START(5) // post
    ret = fimc_v4l2_streamoff(m_cam_fd);
    CHECK_PTR(ret);
    LOG_TIME_END(5)

    LOG_CAMERA("getJpeg intervals : stopPreview(%lu), prepare(%lu), capture(%lu), memcpy(%lu), yuv2Jpeg(%lu), post(%lu)  us"
            , LOG_TIME(0), LOG_TIME(1), LOG_TIME(2), LOG_TIME(3), LOG_TIME(4), LOG_TIME(5));

    return addr;
}

unsigned char * SecCamera::getJpeg(unsigned char *snapshot_data, unsigned int snapshot_size, unsigned int * jpeg_size)
{
    LOGV("%s()", __FUNCTION__);

    if(m_cam_fd <= 0) {
        LOGE("ERR(%s):Camera was closed\n", __FUNCTION__);
        return NULL;
    }

    unsigned char * jpeg_data = NULL;
    unsigned int    jpeg_gotten_size = 0;

    jpeg_data = yuv2Jpeg(snapshot_data, snapshot_size, &jpeg_gotten_size, m_snapshot_width, m_snapshot_height, m_snapshot_v4lformat);

    *jpeg_size = jpeg_gotten_size;
    return jpeg_data;
}

unsigned char * SecCamera::yuv2Jpeg(unsigned char * raw_data, unsigned int raw_size,
                                    unsigned int * jpeg_size,
                                    int width, int height, int pixel_format)
{
    LOGV("%s:raw_data(%p), raw_size(%d), jpeg_size(%d), width(%d), height(%d), format(%d)",
            __FUNCTION__, raw_data, raw_size, *jpeg_size, width, height, pixel_format);

    if(m_jpeg_fd <= 0) {
        LOGE("ERR(%s):JPEG device was closed\n", __FUNCTION__);
        return NULL;
    }
    if(pixel_format == V4L2_PIX_FMT_RGB565) {
        LOGE("ERR(%s):It doesn't support V4L2_PIX_FMT_RGB565\n", __FUNCTION__);
        return NULL;
    }

    unsigned char * InBuf = NULL;
    unsigned char * OutBuf = NULL;
    unsigned char * jpeg_data = NULL;
    long            frameSize;
    exif_file_info_t	ExifInfo;

    int input_file_format = JPG_MODESEL_YCBCR;

    int out_file_format = JPG_422;
    switch(pixel_format)
    {
    case V4L2_PIX_FMT_NV12:
    case V4L2_PIX_FMT_NV21:
    case V4L2_PIX_FMT_NV12T:
    case V4L2_PIX_FMT_YUV420:
        out_file_format = JPG_420;
        break;
    case V4L2_PIX_FMT_YUYV:
    case V4L2_PIX_FMT_UYVY:
    case V4L2_PIX_FMT_YUV422P:
        out_file_format = JPG_422;
        break;
    }

    //////////////////////////////////////////////////////////////
    // 2. set encode config.                                    //
    //////////////////////////////////////////////////////////////
    LOGV("Step 1 : JPEG_SET_ENCODE_IN_FORMAT(JPG_MODESEL_YCBCR)");
    if (SsbSipJPEGSetConfig(JPEG_SET_ENCODE_IN_FORMAT, input_file_format) != JPEG_OK) {
        LOGE("ERR(%s):Fail on JPEG_SET_ENCODE_IN_FORMAT\n", __FUNCTION__);
        goto YUV2JPEG_END;
    }

    LOGV("Step 2 : JPEG_SET_SAMPING_MODE(JPG_422)");
    if (SsbSipJPEGSetConfig(JPEG_SET_SAMPING_MODE, out_file_format) != JPEG_OK) {
        LOGE("ERR(%s):Fail on JPEG_SET_SAMPING_MODE\n", __FUNCTION__);
        goto YUV2JPEG_END;
    }

    LOGV("Step 3 : JPEG_SET_ENCODE_WIDTH(%d)", width);
    if (SsbSipJPEGSetConfig(JPEG_SET_ENCODE_WIDTH, width) != JPEG_OK) {
        LOGE("ERR(%s):Fail on JPEG_SET_ENCODE_WIDTH \n", __FUNCTION__);
        goto YUV2JPEG_END;
    }

    LOGV("Step 4 : JPEG_SET_ENCODE_HEIGHT(%d)", height);
    if (SsbSipJPEGSetConfig(JPEG_SET_ENCODE_HEIGHT, height) != JPEG_OK) {
        LOGE("ERR(%s):Fail on JPEG_SET_ENCODE_HEIGHT \n", __FUNCTION__);
        goto YUV2JPEG_END;
    }

    LOGV("Step 5 : JPEG_SET_ENCODE_QUALITY(JPG_QUALITY_LEVEL_2)");
    if (SsbSipJPEGSetConfig(JPEG_SET_ENCODE_QUALITY, JPG_QUALITY_LEVEL_2) != JPEG_OK) {
        LOGE("ERR(%s):Fail on JPEG_SET_ENCODE_QUALITY \n", __FUNCTION__);
        goto YUV2JPEG_END;
    }

#if (INCLUDE_JPEG_THUMBNAIL == 1)

    LOGV("Step 6a : JPEG_SET_ENCODE_THUMBNAIL(TRUE)");
    if (SsbSipJPEGSetConfig(JPEG_SET_ENCODE_THUMBNAIL, TRUE) != JPEG_OK) {
        LOGE("ERR(%s):Fail on JPEG_SET_ENCODE_THUMBNAIL \n", __FUNCTION__);
        goto YUV2JPEG_END;
    }

    LOGV("Step 6b : JPEG_SET_THUMBNAIL_WIDTH(%d)", m_jpeg_thumbnail_width);
    if (SsbSipJPEGSetConfig(JPEG_SET_THUMBNAIL_WIDTH, m_jpeg_thumbnail_width) != JPEG_OK) {
        LOGE("ERR(%s):Fail on JPEG_SET_THUMBNAIL_WIDTH(%d) \n", __FUNCTION__, m_jpeg_thumbnail_height);
        goto YUV2JPEG_END;
    }

    LOGV("Step 6c : JPEG_SET_THUMBNAIL_HEIGHT(%d)", m_jpeg_thumbnail_height);
    if (SsbSipJPEGSetConfig(JPEG_SET_THUMBNAIL_HEIGHT, m_jpeg_thumbnail_height) != JPEG_OK) {
        LOGE("ERR(%s):Fail on JPEG_SET_THUMBNAIL_HEIGHT(%d) \n", __FUNCTION__, m_jpeg_thumbnail_height);
        goto YUV2JPEG_END;
    }

#endif

    if(raw_size == 0) //Kamat: This is our code path
    {
        unsigned int addr_y;
        int width, height;
        unsigned int frame_size;
        getSnapshotSize(&width, &height, &frame_size);
        if(raw_data == NULL) {
            LOGE("%s %d] Raw data is NULL \n",__func__,__LINE__);
            goto YUV2JPEG_END;
        } else //Kamat: our path
        {
            addr_y = (unsigned int)raw_data;
        }

        SsbSipJPEGSetEncodeInBuf(m_jpeg_fd, addr_y, frame_size);
    }
    else
    {
        //////////////////////////////////////////////////////////////
        // 4. get Input buffer address                              //
        //////////////////////////////////////////////////////////////
        LOGV("Step 7 : Input buffer size(0x%X", raw_size);
        InBuf = (unsigned char *)SsbSipJPEGGetEncodeInBuf(m_jpeg_fd, raw_size);
        if(InBuf == NULL) {
            LOGE("ERR(%s):Fail on SsbSipJPEGGetEncodeInBuf \n", __FUNCTION__);
            goto YUV2JPEG_END;
        }
        //////////////////////////////////////////////////////////////
        // 5. put YUV stream to Input buffer
        //////////////////////////////////////////////////////////////
        LOGV("Step 8: memcpy(InBuf(%p), raw_data(%p), raw_size(%d)", InBuf, raw_data, raw_size);
        memcpy(InBuf, raw_data, raw_size);
    }

    //////////////////////////////////////////////////////////////
    // 6. Make Exif info parameters
    //////////////////////////////////////////////////////////////
    LOGV("Step 9: m_makeExifParam()");
    memset(&ExifInfo, 0x00, sizeof(exif_file_info_t));
    m_makeExifParam(&ExifInfo);

    //////////////////////////////////////////////////////////////
    // 7. Encode YUV stream
    //////////////////////////////////////////////////////////////
    LOGV("Step a: SsbSipJPEGEncodeExe()");
    if(SsbSipJPEGEncodeExe(m_jpeg_fd, /*&ExifInfo*/0, JPEG_USE_HW_SCALER) != JPEG_OK)	  //with Exif
    {
        LOGE("ERR(%s):Fail on SsbSipJPEGEncodeExe \n", __FUNCTION__);
        goto YUV2JPEG_END;
    }
    //////////////////////////////////////////////////////////////
    // 8. get output buffer address
    //////////////////////////////////////////////////////////////
    LOGV("Step b: SsbSipJPEGGetEncodeOutBuf()");
    OutBuf = (unsigned char *)SsbSipJPEGGetEncodeOutBuf(m_jpeg_fd, &frameSize);
    if(OutBuf == NULL) {
        LOGE("ERR(%s):Fail on SsbSipJPEGGetEncodeOutBuf \n", __FUNCTION__);
        goto YUV2JPEG_END;
    }
    //////////////////////////////////////////////////////////////
    // 9. write JPEG result file
    //////////////////////////////////////////////////////////////
    LOGV("Done");
    jpeg_data  = OutBuf;
    *jpeg_size = (unsigned int)frameSize;

YUV2JPEG_END :

    return jpeg_data;
}

void SecCamera::setJpegQuality(int quality)
{
    m_jpeg_quality = quality;
}

int SecCamera::setJpegThumbnailSize(int width, int height)
{
    LOGV("%s(width(%d), height(%d))", __FUNCTION__, width, height);

    m_jpeg_thumbnail_width	= width;
    m_jpeg_thumbnail_height = height;

    return 0;
}

int SecCamera::getJpegThumbnailSize(int * width, int  * height)
{
    if(width)
        *width	 = m_jpeg_thumbnail_width;
    if(height)
        *height  = m_jpeg_thumbnail_height;

    return 0;
}

int SecCamera::setOverlay(int flag_overlay, int x, int y, int width, int height)
{
	if(m_flag_overlay != flag_overlay)
	{
        m_overlay_x       = x;
        m_overlay_y       = y;
        m_overlay_width   = width;
        m_overlay_height  = height;

		m_flag_overlay = flag_overlay;
		m_flag_current_info_changed = FLAG_ON;
    }
    else if(flag_overlay == FLAG_ON)
    {
        if(   m_overlay_x       != x
           || m_overlay_y       != y
           || m_overlay_width   != width
           || m_overlay_height  != height)
        {
            m_overlay_x       = x;
            m_overlay_y       = y;
            m_overlay_width   = width;
            m_overlay_height  = height;

		    m_flag_overlay = flag_overlay;
		    m_flag_current_info_changed = FLAG_ON;
        }
    }

    return 0;
}

int SecCamera::getOverlaySize(int * x, int * y, int * width, int * height)
{
    *x      = m_overlay_x;
    *y      = m_overlay_y;
    *width  = m_overlay_width;
    *height = m_overlay_height;

    return 0;
}

int SecCamera::flagOverlay(void)
{
    return m_flag_overlay;
}

// ======================================================================
// Settings

int SecCamera::setCameraId(int camera_id)
{
    if(m_camera_id == camera_id)
        return 0;

    LOGV("%s(camera_id(%d))", __FUNCTION__, camera_id);

    switch(camera_id)
    {
        case CAMERA_ID_FRONT:

            m_preview_max_width   = MAX_FRONT_CAMERA_PREVIEW_WIDTH;
            m_preview_max_height  = MAX_FRONT_CAMERA_PREVIEW_HEIGHT;
            m_snapshot_max_width  = MAX_FRONT_CAMERA_SNAPSHOT_WIDTH;
            m_snapshot_max_height = MAX_FRONT_CAMERA_SNAPSHOT_HEIGHT;
            break;

        case CAMERA_ID_BACK:
            m_preview_max_width   = MAX_BACK_CAMERA_PREVIEW_WIDTH;
            m_preview_max_height  = MAX_BACK_CAMERA_PREVIEW_HEIGHT;
            m_snapshot_max_width  = MAX_BACK_CAMERA_SNAPSHOT_WIDTH;
            m_snapshot_max_height = MAX_BACK_CAMERA_SNAPSHOT_HEIGHT;
            break;
        default:
            LOGE("ERR(%s)::Invalid camera id(%d)\n", __func__, camera_id);
            return -1;
    }

    m_camera_id = camera_id;
    m_flag_current_info_changed = FLAG_ON;

    return 0;
}

int SecCamera::getCameraId(void)
{
    return m_camera_id;
}

// -----------------------------------

int SecCamera::SetRotate(int angle)
{
    LOGV("%s(angle(%d))", __FUNCTION__, angle);

    // Kamat: not implemented in the driver yet
    // sw5771.park : we will set on Exif..
    unsigned int id;

    switch(angle)
    {
        case -360 :
        case    0 :
        case  360 :
            //id = V4L2_CID_ROTATE_ORIGINAL;
            m_angle = 0;
            break;

        case -270 :
        case   90 :
            //id = V4L2_CID_ROTATE_90;
            m_angle = 90;
            break;

        case -180 :
        case  180 :
            //id = V4L2_CID_ROTATE_180;
            m_angle = 180;
            break;

        case  -90 :
        case  270 :
            //id = V4L2_CID_ROTATE_270;
            m_angle = 270;
            break;

        default :
            LOGE("ERR(%s):Invalid angle(%d)", __FUNCTION__, angle);
            return -1;
    }

    // just set on the Exif of Jpeg
    // m_flag_current_info_changed = FLAG_ON;

    return 0;
}

int SecCamera::getRotate(void)
{
    LOGV("%s():angle(%d)", __FUNCTION__, m_angle);
    return m_angle;
}

// -----------------------------------


void SecCamera::setFrameRate(int frame_rate)
{
    m_frame_rate = frame_rate;
}

int SecCamera::getFrameRate   (void)
{
    return m_frame_rate;
}

int SecCamera::getFrameRateMin(void)
{
    return FRAME_RATE_BASE;
}

int SecCamera::getFrameRateMax(void)
{
    return FRAME_RATE_MAX;
}

// -----------------------------------

int SecCamera::setSceneMode(int scene_mode)
{
	if(scene_mode <= SCENE_MODE_BASE || SCENE_MODE_MAX <= scene_mode)
	{
		LOGE("%s::invalid Scene mode(%d) it should %d ~ %d\n",
			  __func__, scene_mode, SCENE_MODE_BASE, SCENE_MODE_MAX);
		return -1;
	}

	if(m_scene_mode != scene_mode)
	{
		m_scene_mode = scene_mode;
		m_flag_current_info_changed = FLAG_ON;
	}

	return 0;
}

int SecCamera::setWhiteBalance(int white_balance)
{
    LOGV("%s(white_balance(%d))", __FUNCTION__, white_balance);

    if(white_balance <= WHITE_BALANCE_BASE || WHITE_BALANCE_MAX <= white_balance) {
        LOGE("ERR(%s):Invalid white_balance(%d)", __FUNCTION__, white_balance);
        return -1;
    }

    if(m_white_balance != white_balance)
    {
        m_white_balance = white_balance;
        m_flag_current_info_changed = FLAG_ON;
    }

    return 0;
}

int SecCamera::getWhiteBalance(void)
{
    LOGV("%s():white_balance(%d)", __FUNCTION__, m_white_balance);
    return m_white_balance;
}

// -----------------------------------

int SecCamera::setImageEffect(int image_effect)
{
    LOGV("%s(image_effect(%d))", __FUNCTION__, image_effect);

    if(image_effect <= IMAGE_EFFECT_BASE || IMAGE_EFFECT_MAX <= image_effect) {
        LOGE("ERR(%s):Invalid image_effect(%d)", __FUNCTION__, image_effect);
        return -1;
    }

    if(m_image_effect != image_effect)
    {
        m_image_effect = image_effect;
        m_flag_current_info_changed = FLAG_ON;
    }

    return 0;
}

int SecCamera::getImageEffect(void)
{
    LOGV("%s():image_effect(%d)", __FUNCTION__, m_image_effect);
    return m_image_effect;
}

// -----------------------------------

int SecCamera::setBrightness(int brightness)
{
    LOGV("%s(brightness(%d))", __FUNCTION__, brightness);

    if(brightness < BRIGHTNESS_BASE || BRIGHTNESS_MAX < brightness ) {
        LOGE("ERR(%s):Invalid brightness(%d)", __FUNCTION__, brightness);
        return -1;
    }

    if(m_brightness != brightness)
    {
        m_brightness = brightness;
        m_flag_current_info_changed = FLAG_ON;
    }

    return 0;
}

int SecCamera::getBrightness(void)
{
    LOGV("%s():brightness(%d)", __FUNCTION__, m_brightness);
    return m_brightness;
}


int SecCamera::getBrightnessMin(void)
{
	return BRIGHTNESS_BASE;
}

int SecCamera::getBrightnessMax(void)
{
	return BRIGHTNESS_MAX;
}

// -----------------------------------

int SecCamera::setContrast(int contrast)
{
	if(contrast < CONTRAST_BASE || CONTRAST_MAX < contrast )
	{
		LOGE("%s::invalid contrast mode(%d) it should %d ~ %d\n",
			  __func__, contrast, CONTRAST_BASE, CONTRAST_MAX);
		return -1;
	}

	if(m_contrast != contrast)
	{
		m_contrast = contrast;
		m_flag_current_info_changed = FLAG_ON;
	}

	return 0;
}

int SecCamera::getContrast(void)
{
	return m_contrast;
}

int SecCamera::getContrastMin(void)
{
	return CONTRAST_BASE;
}

int SecCamera::getContrastMax(void)
{
	return CONTRAST_MAX;
}

// -----------------------------------

int SecCamera::setSharpness(int sharpness)
{
	if(sharpness < SHARPNESS_BASE || SATURATION_MAX < sharpness)
	{
		LOGE("%s::invalid Sharpness (%d) it should %d ~ %d\n",
			  __func__, sharpness, SATURATION_BASE, SATURATION_MAX);
		return -1;
	}

	if(m_sharpness!= sharpness)
	{
		m_sharpness = sharpness;
		m_flag_current_info_changed = FLAG_ON;
	}
	return 0;
}

int SecCamera::getSharpness(void)
{
	return m_sharpness;
}

int SecCamera::getSharpnessMin(void)
{
	return SHARPNESS_BASE;
}

int SecCamera::getSharpnessMax(void)
{
	return SHARPNESS_MAX;
}

// -----------------------------------

int SecCamera::setSaturation(int saturation)
{
	if(saturation < SATURATION_BASE || SATURATION_MAX < saturation)
	{
		LOGE("%s::invalid Saturation (%d) it should %d ~ %d\n",
			  __func__, saturation, SATURATION_BASE, SATURATION_MAX);
		return -1;
	}

	if(m_saturation != saturation)
	{
		m_saturation = saturation;
		m_flag_current_info_changed = FLAG_ON;
	}
	return 0;
}

int SecCamera::getSaturation(void)
{
	return m_saturation;
}

int SecCamera::getSaturationMin(void)
{
	return SATURATION_BASE;
}

int SecCamera::getSaturationMax(void)
{
	return SATURATION_MAX;
}

// -----------------------------------

int SecCamera::setZoom(int zoom)
{
    LOGV("%s()", __FUNCTION__);

    if(zoom < ZOOM_BASE || ZOOM_MAX < zoom)
	{
		LOGE("%s::invalid zoom (%d) it should %d ~ %d\n",
			  __func__, zoom, SATURATION_BASE, SATURATION_MAX);
		return -1;
	}

	if(m_zoom != zoom)
	{
		m_zoom = zoom;
		m_flag_current_info_changed = FLAG_ON;
	}

    return 0;
}

int SecCamera::getZoom(void)
{
    return m_zoom;
}

int	SecCamera::getZoomMin(void)
{
	return ZOOM_BASE;
}

int	SecCamera::getZoomMax(void)
{
	return ZOOM_MAX;
}

// -----------------------------------

int SecCamera::setAFMode(int af_mode)
{
    LOGV("%s()", __FUNCTION__);

    if(af_mode < AUTO_FOCUS_BASE || AUTO_FOCUS_MAX < af_mode)
	{
		LOGE("%s::invalid af_mode (%d) it should %d ~ %d\n",
			  __func__, af_mode, AUTO_FOCUS_BASE, AUTO_FOCUS_MAX);
		return -1;
	}

	if(m_af_mode != af_mode)
	{
		m_af_mode = af_mode;
		m_flag_current_info_changed = FLAG_ON;
	}

    return 0;
}

int SecCamera::getAFMode(void)
{
	return m_af_mode;
}

int SecCamera::runAF(int flag_on, int * flag_focused)
{
	if(m_flag_create == FLAG_OFF)
	{
        LOGE("ERR(%s):this is not yet created...\n", __FUNCTION__);
		return -1;
	}

	int ret = 0;

	*flag_focused = 0;


	if(   m_af_mode == AUTO_FOCUS_AUTO
	   || m_af_mode == AUTO_FOCUS_MACRO)
	{
		ret = m_setAF(m_af_mode, FLAG_ON, flag_on, flag_focused);
		if (ret < 0)
		{
            LOGE("ERR(%s):m_setAF(%d) fail\n", __FUNCTION__, m_af_mode);
			return -1; // autofocus is presumed that always succeed....
		}
    }
	else
		*flag_focused = 1;

    return 0;
}

int SecCamera::setGpsInfo(double latitude, double longitude, unsigned int timestamp, int altitude)
{
	m_gps_latitude  = latitude;
	m_gps_longitude = longitude;
	m_gps_timestamp = timestamp;
	m_gps_altitude  = altitude;

	return 0;
}

int SecCamera::getCameraFd(void)
{
    return m_cam_fd;
}

int SecCamera::getJpegFd(void)
{
    return m_jpeg_fd;
}

void SecCamera::setJpgAddr(unsigned char *addr)
{
    SetMapAddr(addr);
}

unsigned int SecCamera::getPhyAddrY(int index)
{
    unsigned int addr_y;

    addr_y = fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_PADDR_Y, index);
    CHECK((int)addr_y);
    return addr_y;
}

unsigned int SecCamera::getPhyAddrC(int index)
{
    unsigned int addr_c;

    addr_c = fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_PADDR_CBCR, index);
    CHECK((int)addr_c);
    return addr_c;
}

unsigned int SecCamera::getRecPhyAddrY(int index)
{
    unsigned int addr_y;

    addr_y = fimc_v4l2_s_ctrl(m_cam_fd_rec, V4L2_CID_PADDR_Y, index);
    CHECK((int)addr_y);
    return addr_y;
}

unsigned int SecCamera::getRecPhyAddrC(int index)
{
    unsigned int addr_c;

    addr_c = fimc_v4l2_s_ctrl(m_cam_fd_rec, V4L2_CID_PADDR_CBCR, index);
    CHECK((int)addr_c);
    return addr_c;
}


int SecCamera::m_resetCamera(void)
{
    int ret = 0;

    if(m_flag_current_info_changed == FLAG_ON)
    {
        #ifdef CAMERA_HW_DEBUG
            LOGD("m_resetCamera running..");
        #endif

        // this is already done by preview
        if(m_setCameraId    (m_camera_id)     < 0)
            LOGE("ERR(%s):m_setCameraId fail\n", __FUNCTION__);

        if(m_setZoom        (m_zoom)          < 0)
            LOGE("ERR(%s):m_setZoom fail\n", __FUNCTION__);

        if(m_setFrameRate   (m_frame_rate)     < 0)
            LOGE("ERR(%s):m_setFrameRate fail\n", __FUNCTION__);

        if(m_setSceneMode   (m_scene_mode)    < 0)
            LOGE("ERR(%s):m_setSceneMode fail\n", __FUNCTION__);

        if(m_setWhiteBalance(m_white_balance) < 0)
            LOGE("ERR(%s):m_setWhiteBalance fail\n", __FUNCTION__);

        if(m_setImageEffect (m_image_effect)  < 0)
            LOGE("ERR(%s):m_setImageEffect fail\n", __FUNCTION__);

        if(m_setBrightness  (m_brightness)    < 0)
            LOGE("ERR(%s):m_setBrightness fail\n", __FUNCTION__);

        if(m_setContrast    (m_contrast)      < 0)
            LOGE("ERR(%s):m_setContrast fail\n", __FUNCTION__);
        
        if (m_setMetering   (m_metering)      < 0)
            LOGE("ERR(%s):m_setMetering fail\n", __FUNCTION__);

        if(m_setSharpness   (m_sharpness)     < 0)
            LOGE("ERR(%s):m_setSharpness fail\n", __FUNCTION__);

        if(m_setSaturation  (m_saturation)    < 0)
            LOGE("ERR(%s):m_setSaturation fail\n", __FUNCTION__);

        int flag_focused              = 0;
        if(m_setAF          (m_af_mode, FLAG_OFF, FLAG_OFF, &flag_focused)    < 0)
            LOGE("ERR(%s):m_setAF fail\n", __FUNCTION__);

        m_flag_current_info_changed = FLAG_OFF;
    }

    return ret;
}

int SecCamera::m_setCameraId(int camera_id)
{
    if(camera_id == m_current_camera_id)
        return 0;

    #ifdef CAMERA_HW_DEBUG
        LOGD("%s(camera_id(%d))", __FUNCTION__, camera_id);
    #endif

    switch(camera_id)
    {
        case CAMERA_ID_FRONT:

            m_preview_max_width   = MAX_FRONT_CAMERA_PREVIEW_WIDTH;
            m_preview_max_height  = MAX_FRONT_CAMERA_PREVIEW_HEIGHT;
            m_snapshot_max_width  = MAX_FRONT_CAMERA_SNAPSHOT_WIDTH;
            m_snapshot_max_height = MAX_FRONT_CAMERA_SNAPSHOT_HEIGHT;
            break;

        case CAMERA_ID_BACK:
            m_preview_max_width   = MAX_BACK_CAMERA_PREVIEW_WIDTH;
            m_preview_max_height  = MAX_BACK_CAMERA_PREVIEW_HEIGHT;
            m_snapshot_max_width  = MAX_BACK_CAMERA_SNAPSHOT_WIDTH;
            m_snapshot_max_height = MAX_BACK_CAMERA_SNAPSHOT_HEIGHT;
            break;
        default:
            LOGE("ERR(%s)::Invalid camera id(%d)\n", __func__, camera_id);
            return -1;
    }

    m_current_camera_id = camera_id;

    return 0;
}

int SecCamera::m_setFrameRate(int frame_rate)
{
    if(frame_rate == m_current_frame_rate)
		return 0;

    #ifdef CAMERA_HW_DEBUG
        LOGD("%s(frame_rate(%d))", __FUNCTION__, frame_rate);
    #endif

    int ret = 0;

    /* g_parm, s_parm sample */
    ret = fimc_v4l2_g_parm(m_cam_fd);
    CHECK(ret);
    ret = fimc_v4l2_s_parm(m_cam_fd, 1, frame_rate);
    CHECK(ret);

    m_current_frame_rate = frame_rate;

    return 0;
}

int SecCamera::m_setSceneMode(int scene_mode)
{
	if(scene_mode == m_current_scene_mode)
		return 0;

    #ifdef CAMERA_HW_DEBUG
        LOGD("%s(scene_mode(%d))", __FUNCTION__, scene_mode);
    #endif

    /*
    int value = 0;

	switch (scene_mode)
	{
		case SCENE_MODE_AUTO:

            // Always load the UI when you are coming back to auto mode

			m_current_white_balance = -1;
			m_current_brightness    = -1;
			m_current_contrast      = -1;
			m_current_saturation    = -1;
			m_current_sharpness     = -1;
			m_current_metering      = -1;
			m_current_iso           = -1;
			m_current_image_effect  = -1;
			value = 0;
			break;
		case SCENE_MODE_BEACH:
			value = 9;
			break;
		case SCENE_MODE_BACKLIGHT:
			value = 11;
			break;
		case SCENE_MODE_CANDLELIGHT:
			value = 5;
			break;
		case SCENE_MODE_DUSKDAWN:
			value = 12;
			break;
		case SCENE_MODE_FALLCOLOR:
			value = 13;
			break;
		case SCENE_MODE_FIREWORKS:
			value = 6;
			break;
		case SCENE_MODE_LANDSCAPE:
			value = 2;
			break;
		case SCENE_MODE_NIGHT:
			value = 8;
			break;
		case SCENE_MODE_PARTY:
			value = 10;
			break;
		case SCENE_MODE_PORTRAIT:
			value = 1;
			break;
		case SCENE_MODE_SNOW:
			value = 9;
			break;
		case SCENE_MODE_SPORTS:
			value = 3;
			break;
		case SCENE_MODE_SUNSET:
			value = 4;
			break;
		case SCENE_MODE_TEXT:
			value = 7;
			break;
		default :
			LOGE("%s::invalid scene_mode : %d\n", __func__, scene_mode);
			return -1;
			break;
	}

    if(fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_SCENE_MODE, value);
	{
        LOGE("ERR(%s):V4L2_CID_SCENE_MODE(%d) fail\n", __FUNCTION__, scene_mode);
		return -1;
	}
    */
	m_current_scene_mode = scene_mode;

	return 0;
}


int SecCamera::m_setWhiteBalance(int white_balance)
{
    if(white_balance == m_current_white_balance)
	    return 0;

    #ifdef CAMERA_HW_DEBUG
        LOGI("%s(white_balance(%d))", __FUNCTION__, white_balance);
    #endif

    if (fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_WHITE_BALANCE, white_balance) < 0) {
        LOGE("ERR(%s):Fail on V4L2_CID_CAMERA_WHITE_BALANCE", __func__);
        return -1;
    }

    m_current_white_balance = white_balance;

    return 0;
}


int SecCamera::m_setImageEffect(int image_effect)
{
    if(image_effect == m_current_image_effect)
	    return 0;

    #ifdef CAMERA_HW_DEBUG
        LOGD("%s(image_effect(%d))", __FUNCTION__, image_effect);
    #endif

    int value = 0;

    switch (image_effect)
    {
	    case IMAGE_EFFECT_ORIGINAL:
		    value = 0;
		    break;
	    case IMAGE_EFFECT_AQUA:
		    value = 4;
		    break;
	    case IMAGE_EFFECT_MONO:
		    value = 1;
		    break;
	    case IMAGE_EFFECT_NEGATIVE:
		    value = 2;
		    break;
	    case IMAGE_EFFECT_SEPIA:
		    value = 3;
		    break;
	    case IMAGE_EFFECT_WHITEBOARD:
		    value = 5;
		    break;
	    default :
		    LOGE("%s::invalid image_effect : %d\n", __func__, image_effect);
		    return -1;
		    break;
    }

    if(fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_COLORFX, value) < 0)
    {
        LOGE("ERR(%s):V4L2_CID_COLORFX(%d) fail\n", __FUNCTION__, image_effect);
        return -1;
    }

    m_current_image_effect = image_effect;

    return 0;
}


int SecCamera::m_setBrightness(int brightness)
{
    if(brightness == m_current_brightness)
	    return 0;

    #ifdef CAMERA_HW_DEBUG
        LOGD("%s(brightness(%d))", __FUNCTION__, brightness);
    #endif

    if(fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_EXPOSURE, brightness) < 0)
    {
        LOGE("ERR(%s):V4L2_CID_EXPOSURE(%d) fail\n", __FUNCTION__, brightness);
	    return -1;
    }

    m_current_brightness = brightness;

    return 0;
}
    
int SecCamera::m_setMetering(int metering)
{
    if(metering == m_current_metering)
	    return 0;

    LOGV("%s(metering (%d))", __func__, metering);
        
    if (fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CAMERA_METERING, metering) < 0) {
        LOGE("ERR(%s):Fail on V4L2_CID_CAMERA_METERING", __func__);
        return -1;
    }
    m_current_metering = metering;
    return 0;
}


int SecCamera::m_setContrast(int contrast)
{
    if(contrast == m_current_contrast)
	    return 0;

    #ifdef CAMERA_HW_DEBUG
        LOGD("%s(contrast(%d))", __FUNCTION__, contrast);
    #endif

    if(fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_CONTRAST, contrast) < 0)
    {
        LOGE("ERR(%s):V4L2_CID_CONTRAST(%d) fail\n", __FUNCTION__, contrast);
        return -1;
    }


    m_current_contrast = contrast;

    return 0;
}

int SecCamera::m_setSharpness(int sharpness)
{
    if(sharpness == m_current_sharpness)
	    return 0;

    #ifdef CAMERA_HW_DEBUG
        LOGD("%s(sharpness(%d))", __FUNCTION__, sharpness);
    #endif

    if(fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_SHARPNESS, sharpness) < 0)
    {
        LOGE("ERR(%s):V4L2_CID_SHARPNESS(%d) fail\n", __FUNCTION__, sharpness);
        return -1;
    }
    m_current_sharpness = sharpness;

    return 0;
}

int  SecCamera::m_setSaturation(int saturation)
{
    if(saturation == m_current_saturation)
	    return 0;

    #ifdef CAMERA_HW_DEBUG
        LOGD("%s(saturation(%d))", __FUNCTION__, saturation);
    #endif

    if(fimc_v4l2_s_ctrl(m_cam_fd, V4L2_CID_SATURATION, saturation) < 0)
    {
        LOGE("ERR(%s):V4L2_CID_CONTRAST(%d) fail\n", __FUNCTION__, saturation);
        return -1;
    }

    m_current_saturation = saturation;

    return 0;
}

int SecCamera::m_setZoom(int zoom)
{
	if(zoom == m_current_zoom)
		return 0;

    #ifdef CAMERA_HW_DEBUG
        LOGD("%s(zoom(%d))", __FUNCTION__, zoom);
    #endif

	#ifdef USE_SEC_CROP_ZOOM
	{
        // set crop
        struct v4l2_cropcap cropcap;
        struct v4l2_crop crop;
        unsigned int crop_x      = 0;
        unsigned int crop_y      = 0;
        unsigned int crop_width  = 0;
        unsigned int crop_height = 0;

        cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        if(ioctl(m_cam_fd, VIDIOC_CROPCAP, &cropcap) >= 0)
        {
            m_getCropRect(cropcap.bounds.width, cropcap.bounds.height,
		                  m_preview_width,     m_preview_height,
			              &crop_x,             &crop_y,
			              &crop_width,         &crop_height,
			              m_zoom);

            if(   (unsigned int)cropcap.bounds.width  != crop_width
               || (unsigned int)cropcap.bounds.height != crop_height)
            {
                cropcap.defrect.left   = crop_x;
                cropcap.defrect.top    = crop_y;
                cropcap.defrect.width  = crop_width;
                cropcap.defrect.height = crop_height;
                crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                crop.c    = cropcap.defrect;

                if (ioctl(m_cam_fd, VIDIOC_S_CROP, &crop) < 0)
	                LOGE("%s(VIDIOC_S_CROP fail(%d))", __FUNCTION__, zoom);
            }

            /*
            LOGD("## 1 cropcap.bounds.width  : %d \n", cropcap.bounds.width);
            LOGD("## 1 cropcap.bounds.height : %d \n", cropcap.bounds.height);
            LOGD("## 1 width                 : %d \n", width);
            LOGD("## 1 height                : %d \n", height);
            LOGD("## 1 m_zoom                : %d \n", m_zoom);
            LOGD("## 2 crop_width            : %d \n", crop_width);
            LOGD("## 2 crop_height           : %d \n", crop_height);
            LOGD("## 2 cropcap.defrect.width : %d \n", cropcap.defrect.width);
            LOGD("## 2 cropcap.defrect.height: %d \n", cropcap.defrect.height);
            */
        }
        else
            LOGE("%s(VIDIOC_CROPCAP fail (bug ignored..))", __FUNCTION__);
	        // ignore errors

        if(m_cam_fd_rec != 0)
        {
            cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

            if(ioctl(m_cam_fd_rec, VIDIOC_CROPCAP, &cropcap) >= 0)
            {
                m_getCropRect(cropcap.bounds.width, cropcap.bounds.height,
		                      m_preview_width,     m_preview_height,
			                  &crop_x,             &crop_y,
			                  &crop_width,         &crop_height,
			                  m_zoom);

                if(   (unsigned int)cropcap.bounds.width  != crop_width
                   || (unsigned int)cropcap.bounds.height != crop_height)
                {
                    cropcap.defrect.left   = crop_x;
                    cropcap.defrect.top    = crop_y;
                    cropcap.defrect.width  = crop_width;
                    cropcap.defrect.height = crop_height;
                    crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                    crop.c    = cropcap.defrect;

                    if (ioctl(m_cam_fd_rec, VIDIOC_S_CROP, &crop) < 0)
	                    LOGE("%s(VIDIOC_S_CROP fail(%d))", __FUNCTION__, zoom);
                }

                /*
                LOGD("## 1 cropcap.bounds.width  : %d \n", cropcap.bounds.width);
                LOGD("## 1 cropcap.bounds.height : %d \n", cropcap.bounds.height);
                LOGD("## 1 width                 : %d \n", width);
                LOGD("## 1 height                : %d \n", height);
                LOGD("## 1 m_zoom                : %d \n", m_zoom);
                LOGD("## 2 crop_width            : %d \n", crop_width);
                LOGD("## 2 crop_height           : %d \n", crop_height);
                LOGD("## 2 cropcap.defrect.width : %d \n", cropcap.defrect.width);
                LOGD("## 2 cropcap.defrect.height: %d \n", cropcap.defrect.height);
                */
            }
            else
                LOGE("%s(VIDIOC_CROPCAP fail (bug ignored..))", __FUNCTION__);
        }
	}
	#else
	{
	}
	#endif // USE_SEC_CROP_ZOOM

	m_current_zoom = zoom;

	return 0;
}


int SecCamera::m_setAF(int af_mode, int flag_run, int flag_on, int * flag_focused)
{
	int ret = -1;

	*flag_focused = 0;
	static int autofocus_running = FLAG_OFF;

	// setting af mode
	if(   m_current_af_mode != af_mode
	   && autofocus_running == FLAG_OFF)
	{
		#ifdef CAMERA_HW_DEBUG
			LOGD("##############::%s called::for setting af_mode : %d \n", __func__, af_mode);
		#endif //CAMERA_HW_DEBUG

		autofocus_running = FLAG_ON;

        #if 0
        {
		    struct v4l2_control ctrl;
		    ctrl.id    = V4L2_CID_FOCUS_AUTO;

		    switch(af_mode)
		    {
			    case AUTO_FOCUS_AUTO :
				    ctrl.value = 2;
				    break;
			    case AUTO_FOCUS_FIXED :
				    ctrl.value = 5;
				    break;
			    case AUTO_FOCUS_INFINITY :
				    ctrl.value = 3;
				    break;
			    case AUTO_FOCUS_MACRO :
				    ctrl.value = 4;
				    break;
			    default :
			    {
                    LOGE("%s(unmatched af mode(%d) fail)", __FUNCTION__, af_mode);
				    goto m_setAF_end;
				    break;
			    }
		    }
		    ret = ioctl(m_camera_fd, VIDIOC_S_CTRL, &ctrl);
		    if (ret < 0)
		    {
                LOGE("%s(VIDIOC_S_FOCUS_AUTO(%d) fail)", __FUNCTION__, flag_on);
			    goto m_setAF_end; // autofocus is presumed that always succeed....
		    }
		    else if (ret == 1)
			    *flag_focused = 1;
		    //else
		    //	*flag_focused = 0;
        }
        #else
            *flag_focused = 1;
        #endif

		m_current_af_mode = af_mode;

		autofocus_running = FLAG_OFF;
	}

	// running af mode
	if(   (flag_run == FLAG_ON)
	   && (af_mode == AUTO_FOCUS_AUTO || af_mode == AUTO_FOCUS_MACRO)
	   && (autofocus_running == FLAG_OFF))
	{
		#ifdef CAMERA_HW_DEBUG
			LOGD("##############::%s called::for running af_mode : %d \n", __func__, af_mode);
		#endif //CAMERA_HW_DEBUG

		autofocus_running = FLAG_ON;

        #if 1
        {
            for(int i = 0; i < 10 ; i++)
            {
                LOGD("######### VIRTUAL AUTOFOCUSING IS ON %d...\n", i);
                usleep(100000);
            }

            *flag_focused = 1;
        }
        #else
        {
		    struct v4l2_control ctrl;
		    ctrl.id    = V4L2_CID_FOCUS_AUTO;

		    if(flag_on == FLAG_ON)
			    ctrl.value = 1;
		    else // if(flag_on == FLAG_OFF)
		    {
			    #ifdef AF_RELEASE_FUNCTION_ENABLE
				    ctrl.value = 0;
			    #else
				    ret = 0;
				    goto m_setAF_end;
			    #endif // AF_RELEASE_FUNCTION_ENABLE
		    }

		    ret = ioctl(m_camera_fd, VIDIOC_S_CTRL, &ctrl);
		    if (ret < 0)
		    {
                LOGE("%s(VIDIOC_S_FOCUS_AUTO(%d) fail)", __FUNCTION__, flag_on);
			    goto m_setAF_end; // autofocus is presumed that always succeed....
		    }
		    else if (ret == 1)
			    *flag_focused = 1;
		    //else
		    //	*flag_focused = 0;
        }
        #endif

		autofocus_running = FLAG_OFF;
	}
	else
	{
		// just set focused..
		*flag_focused = 1;
	}

	ret = 0;

m_setAF_end :

	autofocus_running = FLAG_OFF;

	return ret;
}

int SecCamera::m_getCropRect(unsigned int   src_width,  unsigned int   src_height,
                             unsigned int   dst_width,  unsigned int   dst_height,
                             unsigned int * crop_x,     unsigned int * crop_y,
                             unsigned int * crop_width, unsigned int * crop_height,
                             int            zoom)
{
    #define DEFAULT_ZOOM_RATIO        (4) // 4x zoom
    #define DEFAULT_ZOOM_RATIO_SHIFT  (2)

    unsigned int cal_src_width   = src_width;
    unsigned int cal_src_height  = src_height;

    if(   zoom != 0
       || src_width  != dst_width
       || src_height != dst_height)
    {
        float src_ratio    = 1.0f;
        float dst_ratio    = 1.0f;

        // ex : 1024 / 768
        src_ratio  = (float)src_width / (float)src_height ;

        // ex : 352  / 288
        dst_ratio =  (float)dst_width / (float)dst_height;

        if(src_ratio != dst_ratio)
        {
            if(src_ratio <= dst_ratio)
            {
	            // height 를 줄인다
	            cal_src_width  = src_width;
	            cal_src_height = src_width / dst_ratio;
            }
            else //(src_ratio > dst_ratio)
            {
	            // width 를 줄인다
	            cal_src_width  = src_height * dst_ratio;
	            cal_src_height = src_height;
            }
        }

        if(zoom != 0)
        {
            unsigned int zoom_width_step =
			            (src_width  - (src_width  >> DEFAULT_ZOOM_RATIO_SHIFT)) / ZOOM_MAX;

            unsigned int zoom_height_step =
			            (src_height - (src_height >> DEFAULT_ZOOM_RATIO_SHIFT)) / ZOOM_MAX;


            cal_src_width  = cal_src_width   - (zoom_width_step  * zoom);
            cal_src_height = cal_src_height  - (zoom_height_step * zoom);
        }
    }

    /*
    #define CAMERA_CROP_RESTRAIN_NUM  (0x10)
	unsigned int width_align = (cal_src_width & (CAMERA_CROP_RESTRAIN_NUM-1));
	if(width_align != 0)
	{
		if(    (CAMERA_CROP_RESTRAIN_NUM >> 1) <= width_align
			&& cal_src_width + (CAMERA_CROP_RESTRAIN_NUM - width_align) <= dst_width)
		{
			cal_src_width += (CAMERA_CROP_RESTRAIN_NUM - width_align);
		}
		else
			cal_src_width -= width_align;
	}
    */
    // kcoolsw : this can be camera view weird..
    //           because dd calibrate x y once again
    *crop_x      = (src_width  - cal_src_width ) >> 1;
    *crop_y      = (src_height - cal_src_height) >> 1;
    //*crop_x      = 0;
    //*crop_y      = 0;
    *crop_width  = cal_src_width;
	*crop_height = cal_src_height;

    return 0;
}

inline void SecCamera::m_makeExifParam(exif_file_info_t *exifFileInfo)
{
    strcpy(exifFileInfo->make,	 "Samsung Electronics");
    strcpy(exifFileInfo->Model,  "Samsung Electronics 2009 model");
    strcpy(exifFileInfo->Version,"version 1.0.2.0");

    struct timeval tv;
    struct tm* ptm;
    char time_string[40];
    long milliseconds;
    gettimeofday (&tv, NULL);
    ptm = localtime (&tv.tv_sec);
    strftime (time_string, sizeof (time_string), "%Y:%m:%d %H:%M:%S", ptm);
    //strcpy(exifFileInfo->DateTime,"2007:05:16 12:32:54");
    strcpy(exifFileInfo->DateTime, time_string);
    sprintf(exifFileInfo->CopyRight, "Samsung Electronics@%d:All rights reserved", (1900 + ptm->tm_year));

    exifFileInfo->Width             = m_snapshot_width;
    exifFileInfo->Height            = m_snapshot_height;

	// sw5771.park : we set orientation on the Exif of Jpeg
    #define CAM_EXIF_ORIENTATION_UP              (1)
    #define CAM_EXIF_ORIENTATION_FLIP_LEFT_RIGHT (2)
    #define CAM_EXIF_ORIENTATION_FLIP_UP_DOWN    (4)
    #define CAM_EXIF_ORIENTATION_ROTATE_90       (6)
    #define CAM_EXIF_ORIENTATION_ROTATE180       (3)
    #define CAM_EXIF_ORIENTATION_ROTATE_270      (8)

	switch(m_angle)
	{
		case 0   :
			exifFileInfo->Orientation = CAM_EXIF_ORIENTATION_UP;
			break;
		case 90  :
			exifFileInfo->Orientation = CAM_EXIF_ORIENTATION_ROTATE_90;
			break;
		case 180 :
			exifFileInfo->Orientation = CAM_EXIF_ORIENTATION_ROTATE180;
			break;
		case 270 :
			exifFileInfo->Orientation = CAM_EXIF_ORIENTATION_ROTATE_270;
			break;
		default :
			exifFileInfo->Orientation = CAM_EXIF_ORIENTATION_UP;
			break;
	}

    exifFileInfo->ColorSpace        = 1;
    exifFileInfo->Process           = 1;
    exifFileInfo->Flash             = 0;
    exifFileInfo->FocalLengthNum    = 1;
    exifFileInfo->FocalLengthDen    = 4;
    exifFileInfo->ExposureTimeNum   = 1;
    exifFileInfo->ExposureTimeDen   = 20;
    exifFileInfo->FNumberNum        = 1;
    exifFileInfo->FNumberDen        = 35;
    exifFileInfo->ApertureFNumber   = 1;
    exifFileInfo->SubjectDistanceNum = -20;
    exifFileInfo->SubjectDistanceDen = -7;
    exifFileInfo->CCDWidth           = 1;
    exifFileInfo->ExposureBiasNum    = -16;
    exifFileInfo->ExposureBiasDen    = -2;
    exifFileInfo->WhiteBalance       = 6;
    exifFileInfo->MeteringMode       = 3;
    exifFileInfo->ExposureProgram    = 1;
    exifFileInfo->ISOSpeedRatings[0] = 1;
    exifFileInfo->ISOSpeedRatings[1] = 2;
    exifFileInfo->FocalPlaneXResolutionNum = 65;
    exifFileInfo->FocalPlaneXResolutionDen = 66;
    exifFileInfo->FocalPlaneYResolutionNum = 70;
    exifFileInfo->FocalPlaneYResolutionDen = 71;
    exifFileInfo->FocalPlaneResolutionUnit = 3;
    exifFileInfo->XResolutionNum        = 48;
    exifFileInfo->XResolutionDen        = 20;
    exifFileInfo->YResolutionNum        = 48;
    exifFileInfo->YResolutionDen        = 20;
    exifFileInfo->RUnit                 = 2;
    exifFileInfo->BrightnessNum         = -7;
    exifFileInfo->BrightnessDen         = 1;

    // FYI.. EXIF 2.2
    //exifFileInfo->latitude         = m_gps_latitude;
	//exifFileInfo->longitude        = m_gps_longitude;
	//exifFileInfo->altitude         = (double)m_gps_altitude;

    strcpy(exifFileInfo->UserComments,"Usercomments");
}



// ======================================================================
// Conversions

inline unsigned int SecCamera::m_frameSize(int format, int width, int height)
{
    unsigned int size = 0;
    unsigned int realWidth  = width;
    unsigned int realheight = height;

    switch(format)
    {
        case V4L2_PIX_FMT_YUV420 :
        case V4L2_PIX_FMT_NV12 :
        case V4L2_PIX_FMT_NV21 :
            size = ((realWidth * realheight * 3) >> 1);
            break;

        case V4L2_PIX_FMT_NV12T:
            size =   ALIGN_TO_8KB(ALIGN_TO_128B(realWidth) * ALIGN_TO_32B(realheight))
                   + ALIGN_TO_8KB(ALIGN_TO_128B(realWidth) * ALIGN_TO_32B(realheight >> 1));
            break;

        case V4L2_PIX_FMT_YUV422P :
        case V4L2_PIX_FMT_YUYV :
        case V4L2_PIX_FMT_UYVY :
            size = ((realWidth * realheight) << 1);
            break;

        default :
            LOGE("ERR(%s):Invalid V4L2 pixel format(%d)\n", __FUNCTION__, format);
        case V4L2_PIX_FMT_RGB565 :
            size = (realWidth * realheight * BPP);
            break;
    }

    return size;
}

status_t SecCamera::dump(int fd, const Vector<String16>& args)
{
    const size_t SIZE = 256;
    char buffer[SIZE];
    String8 result;
    snprintf(buffer, 255, "dump(%d)\n", fd);
    result.append(buffer);
    ::write(fd, result.string(), result.size());
    return NO_ERROR;
}


}; // namespace android
