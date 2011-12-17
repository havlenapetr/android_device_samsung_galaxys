/*
 * Copyright 2011, Havlena Petr <havlenapetr@gmail.com>
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

#define LOG_NDEBUG 0
#define LOG_TAG "SecTv"

#include <utils/Log.h>

#include <string.h>
#include <stdlib.h>
#include <sys/poll.h>

#include "SecTv.h"

using namespace android;

#define RETURN_IF(return_value)                                      \
    if (return_value < 0) {                                          \
        LOGE("%s::%d fail. errno: %s",                               \
             __func__, __LINE__, strerror(errno));                   \
        return -1;                                                   \
    }

#define LOG_IF(return_value)                                         \
    if (return_value < 0) {                                          \
        LOGE("%s::%d fail. errno: %s",                               \
             __func__, __LINE__, strerror(errno));                   \
    }

#define BPP             2
#define MIN(x, y)       (((x) < (y)) ? (x) : (y))

#define MAX_OUTBUFS		3

#define ALIGN_TO_32B(x)   ((((x) + (1 <<  5) - 1) >>  5) <<  5)
#define ALIGN_TO_128B(x)  ((((x) + (1 <<  7) - 1) >>  7) <<  7)
#define ALIGN_TO_8KB(x)   ((((x) + (1 << 13) - 1) >> 13) << 13)

struct s5p_tv_standart_internal {
    int index;
    unsigned long value;
} s5p_tv_standards[] = {
    {
		S5P_TV_STD_NTSC_M,
		V4L2_STD_NTSC_M,
	}, {
		S5P_TV_STD_PAL_BDGHI,
		V4L2_STD_PAL_BDGHI,
	}, {
		S5P_TV_STD_PAL_M,
		V4L2_STD_PAL_M,
	}, {
		S5P_TV_STD_PAL_N,
		V4L2_STD_PAL_N,
	}, {
		S5P_TV_STD_PAL_Nc,
		V4L2_STD_PAL_Nc,
	}, {
		S5P_TV_STD_PAL_60,
		V4L2_STD_PAL_60,
	}, {
		S5P_TV_STD_NTSC_443,
		V4L2_STD_NTSC_443,
	}, {
		S5P_TV_STD_480P_60_16_9,
		V4L2_STD_480P_60_16_9,
	}, {
		S5P_TV_STD_480P_60_4_3,
		V4L2_STD_480P_60_4_3,
	}, {
		S5P_TV_STD_576P_50_16_9,
		V4L2_STD_576P_50_16_9,
	}, {
		S5P_TV_STD_576P_50_4_3,
		V4L2_STD_576P_50_4_3,
	}, {
		S5P_TV_STD_720P_60,
		V4L2_STD_720P_60,
	}, {
		S5P_TV_STD_720P_50,
		V4L2_STD_720P_50,
	},
};

static inline int calcFrameSize(int format, int width, int height)
{
    int size = 0;

    switch (format) {
        case V4L2_PIX_FMT_YUV420:
        case V4L2_PIX_FMT_NV12:
        case V4L2_PIX_FMT_NV21:
            size = (width * height * 3 / 2);
            break;

        case V4L2_PIX_FMT_NV12T:
            size = ALIGN_TO_8KB(ALIGN_TO_128B(width) * ALIGN_TO_32B(height)) +
            ALIGN_TO_8KB(ALIGN_TO_128B(width) * ALIGN_TO_32B(height / 2));
            break;

        case V4L2_PIX_FMT_YUV422P:
        case V4L2_PIX_FMT_YUYV:
        case V4L2_PIX_FMT_UYVY:
            size = (width * height * 2);
            break;

        default :
            LOGE("ERR(%s):Invalid V4L2 pixel format(%d)\n", __func__, format);
        case V4L2_PIX_FMT_RGB565:
            size = (width * height * BPP);
            break;
    }
    return size;
}

static int get_pixel_depth(unsigned int fmt)
{
    int depth = 0;

    switch (fmt) {
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

// ======================================================================
// Video ioctls

static int tv20_v4l2_querycap(int fp)
{
    struct v4l2_capability cap;

    int ret = ioctl(fp, VIDIOC_QUERYCAP, &cap);
    if (ret < 0) {
        LOGE("ERR(%s):VIDIOC_QUERYCAP failed", __func__);
        return -1;
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_OUTPUT)) {
        LOGE("ERR(%s):no output devices\n", __func__);
        return -1;
    }
    LOGV("Name of cap driver is %s", cap.driver);

    return ret;
}

static const __u8* tv20_v4l2_enum_output(int fp, int index)
{
    static struct v4l2_output output;

    output.index = index;
    if (ioctl(fp, VIDIOC_ENUMOUTPUT, &output) != 0) {
        LOGE("ERR(%s):No matching index found", __func__);
        return NULL;
    }
    LOGV("Name of output channel[%d] is %s", output.index, output.name);

    return output.name;
}

static const __u8* tv20_v4l2_enum_standarts(int fp, int index)
{
    static struct v4l2_standard standart;

    standart.index = index;
    if (ioctl(fp, VIDIOC_ENUMSTD, &standart) != 0) {
        LOGE("ERR(%s):No matching index found\n", __func__);
        return NULL;
    }
    LOGV("Name of output standart[%d] is %s\n", standart.index, standart.name);

    return standart.name;
}

static int tv20_v4l2_s_output(int fp, int index)
{
    struct v4l2_output output;
    int ret;

    output.index = index;

    ret = ioctl(fp, VIDIOC_S_OUTPUT, &output);
    if (ret < 0) {
        LOGE("ERR(%s):VIDIOC_S_OUPUT failed\n", __func__);
        return ret;
    }
    return ret;
}

static int tv20_v4l2_s_std(int fp, unsigned long id)
{
    v4l2_std_id std;
    int ret;

    std = id;

    ret = ioctl(fp, VIDIOC_S_STD, &std);
    if (ret < 0) {
        LOGE("ERR(%s):VIDIOC_S_OUPUT failed\n", __func__);
        return ret;
    }
    return ret;
}

static int tv20_v4l2_enum_fmt(int fp, unsigned int fmt)
{
    struct v4l2_fmtdesc fmtdesc;
    int found = 0;

    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    fmtdesc.index = 0;

    while (ioctl(fp, VIDIOC_ENUM_FMT, &fmtdesc) == 0) {
        if (fmtdesc.pixelformat == fmt) {
            LOGV("passed fmt = %#x found pixel format[%d]: %s\n", fmt, fmtdesc.index, fmtdesc.description);
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

static int tv20_v4l2_s_fmt(int fp, int width, int height,
                           unsigned int fmt, unsigned int yAddr, unsigned int cAddr)
{
    struct v4l2_format v4l2_fmt;
    struct v4l2_pix_format_s5p_tvout pixfmt;
    int ret;

    v4l2_fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;

    memset(&pixfmt, 0, sizeof(pixfmt));
    pixfmt.pix_fmt.width = width;
    pixfmt.pix_fmt.height = height;
    pixfmt.pix_fmt.pixelformat = fmt;
    pixfmt.pix_fmt.sizeimage = (width * height * get_pixel_depth(fmt)) / 8;
    pixfmt.pix_fmt.field = V4L2_FIELD_NONE;

    // here we must set addresses of our memory for video out
    pixfmt.base_y = (void *)yAddr;
    pixfmt.base_c = (void* )cAddr;

    v4l2_fmt.fmt.pix = pixfmt.pix_fmt;
    memcpy(v4l2_fmt.fmt.raw_data, &pixfmt,
            sizeof(struct v4l2_pix_format_s5p_tvout));

    /* Set up for capture */
    ret = ioctl(fp, VIDIOC_S_FMT, &v4l2_fmt);
    if (ret < 0) {
        LOGE("ERR(%s):VIDIOC_S_FMT failed\n", __func__);
        return -1;
    }
    return 0;
}

static int tv20_v4l2_streamon(int fp)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    int ret;

    ret = ioctl(fp, VIDIOC_STREAMON, &type);
    if (ret < 0) {
        LOGE("ERR(%s):VIDIOC_STREAMON failed\n", __func__);
        return ret;
    }

    return ret;
}

static int tv20_v4l2_streamoff(int fp)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    int ret;

    LOGV("%s :", __func__);
    ret = ioctl(fp, VIDIOC_STREAMOFF, &type);
    if (ret < 0) {
        LOGE("ERR(%s):VIDIOC_STREAMOFF failed\n", __func__);
        return ret;
    }

    return ret;
}

static int tv20_v4l2_g_parm(int fp, struct v4l2_streamparm *streamparm)
{
    int ret;

    streamparm->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;

    ret = ioctl(fp, VIDIOC_G_PARM, streamparm);
    if (ret < 0) {
        LOGE("ERR(%s):VIDIOC_G_PARM failed\n", __func__);
        return -1;
    }

    LOGV("%s : timeperframe: numerator %d, denominator %d\n", __func__,
            streamparm->parm.capture.timeperframe.numerator,
            streamparm->parm.capture.timeperframe.denominator);

    return 0;
}

static int tv20_v4l2_s_parm(int fp, struct v4l2_streamparm *streamparm)
{
    int ret;

    streamparm->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;

    ret = ioctl(fp, VIDIOC_S_PARM, streamparm);
    if (ret < 0) {
        LOGE("ERR(%s):VIDIOC_S_PARM failed\n", __func__);
        return ret;
    }
    return 0;
}

static int tv20_v4l2_s_crop(int fp, int offset_x, int offset_y, int width, int height)
{
    struct v4l2_crop crop;

    crop.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    crop.c.left	= offset_x;
    crop.c.top = offset_y;
    crop.c.width = width;
    crop.c.height = height;

    int ret = ioctl(fp, VIDIOC_S_CROP, &crop);
    if (ret < 0) {
        LOGE("ERR(%s):VIDIOC_S_PARM failed\n", __func__);
        return ret;
    }
    return 0;
}

int tv20_v4l2_start_overlay(int fp)
{
    int ret, start = 1;

    ret = ioctl(fp, VIDIOC_OVERLAY, &start);
    if (ret < 0) {
        LOGE("ERR(%s): VIDIOC_OVERLAY start failed\n", __func__);
        return ret;
    }

    return ret;
}

int tv20_v4l2_stop_overlay(int fp)
{
    int ret, stop = 0;

    ret = ioctl(fp, VIDIOC_OVERLAY, &stop);
    if (ret < 0) {
        LOGE("ERR(%s): VIDIOC_OVERLAY stop failed\n", __func__);
        return ret;
    }

    return ret;
}

// ======================================================================
// Audio ioctls

#define VIDIOC_INIT_AUDIO _IOR('V', 103, unsigned int)
#define VIDIOC_AV_MUTE _IOR('V', 104, unsigned int)
#define VIDIOC_G_AVMUTE _IOR('V', 105, unsigned int)

static int tv20_v4l2_audio_enable(int fp)
{
    return ioctl(fp, VIDIOC_INIT_AUDIO, 1);
}

static int tv20_v4l2_audio_disable(int fp)
{
    return ioctl(fp, VIDIOC_INIT_AUDIO, 0);
}

static int tv20_v4l2_audio_mute(int fp)
{
    return ioctl(fp, VIDIOC_AV_MUTE, 1);
}

static int tv20_v4l2_audio_unmute(int fp)
{
    return ioctl(fp, VIDIOC_AV_MUTE, 0);
}

static int tv20_v4l2_audio_get_mute_state(int fp)
{
    return ioctl(fp, VIDIOC_G_AVMUTE, 0);
}

// ======================================================================
// Class which comunicate with kernel driver

/* static */
int SecTv::openTvOut(SecTv** hardware)
{
    LOGV("%s", __func__);
    int fd = open(TV_DEV_NAME, O_RDWR);
    RETURN_IF(fd);

    LOGV("query capabilities");
    int ret = tv20_v4l2_querycap(fd);
    RETURN_IF(ret);

    LOGV("binded to output: %i", TV_DEV_INDEX);
    *hardware = new SecTv(fd, TV_DEV_INDEX);

    return 0;
}

SecTv::SecTv(int fd, int index)
    : mTvOutFd(fd),
      mIndex(index),
      mWidth(-1),
      mHeight(-1),
      mFormat(-1),
      mRunning(false),
      mAudioEnabled(false),
      mTvOutVFd(-1),
      mFrameBuffFd(-1),
      mImageMemory(NULL),
      mHdcpEnabled(0)
{
    mParams = (struct v4l2_window_s5p_tvout*)&mStreamParams.parm.raw_data;
    memset(mParams, 0, sizeof(struct v4l2_window_s5p_tvout));
    memset(&mCropWin, 0, sizeof(struct v4l2_crop));

    int ret = ioctl(mTvOutFd, VIDIOC_HDCP_ENABLE, &mHdcpEnabled);
    LOG_IF(ret);

    memset(&mFimcHandle, 0, sizeof(s5p_fimc_t));
    ret = fimc_open(&mFimcHandle);
    LOG_IF(ret);
}

SecTv::~SecTv()
{
    if(mTvOutFd) {
        close(mTvOutFd);
    }
    if(mFimcHandle.dev_fd) {
        fimc_close(&mFimcHandle);
    }
    if (mImageMemory) {
        free(mImageMemory);
    }
}

/*
int SecTv::init(int fbIndex, int format)
{
    int ret;
    struct fb_var_screeninfo fb_info;

    //ret = (*hardware)->setStandart(S5P_TV_STD_NTSC_M);
    ret = setStandart(S5P_TV_STD_PAL_BDGHI);
    RETURN_IF(ret);
    ret = setOutput(S5P_TV_OUTPUT_TYPE_COMPOSITE);
    RETURN_IF(ret);

    mFrameBuffFd = fb_open(fbIndex);
    RETURN_IF(mFrameBuffFd);

    ret = get_vscreeninfo(mFrameBuffFd, &fb_info);
    RETURN_IF(ret);

    ret = setFormat(fb_info.xres, fb_info.yres, format);
    RETURN_IF(ret);

    ret = setWindow(fb_info.xoffset, fb_info.yoffset, fb_info.xres, fb_info.yres);
    RETURN_IF(ret);

    return 0;
}

int SecTv::initLayer()
{
    LOGV("%s", __func__);
    if(mTvOutVFd > 0) {
        return 0;
    }

    mTvOutVFd = open(TV_DEV_V_NAME, O_RDWR);
    RETURN_IF(mTvOutVFd);
    return 0;
}

void SecTv::deinitLayer()
{
    LOGV("%s", __func__);
    if(mTvOutVFd < 0) {
        return;
    }

    close(mTvOutVFd);
    mTvOutVFd = -1;
}
*/

int SecTv::setStandart(s5p_tv_standart standart)
{
    LOGV("%s", __func__);
    RETURN_IF(mTvOutFd);

    struct s5p_tv_standart_internal std = s5p_tv_standards[(int)standart];

    LOGV("searching for standart: %i", standart);
    if(!tv20_v4l2_enum_standarts(mTvOutFd, std.index))
        return -1;

    int ret = tv20_v4l2_s_std(mTvOutFd, std.value);
    RETURN_IF(ret);
    return 0;
}

int SecTv::setOutput(s5p_tv_output output)
{
    LOGV("%s", __func__);
    RETURN_IF(mTvOutFd);

    LOGV("searching for output: %i", output);
    if (!tv20_v4l2_enum_output(mTvOutFd, output))
        return -1;

    LOGV("selecting output: %i", output);
    int ret = tv20_v4l2_s_output(mTvOutFd, output);
    RETURN_IF(ret);
    return 0;
}

int SecTv::setWindow(int offset_x, int offset_y, int width, int height)
{
    LOGV("%s", __func__);
    RETURN_IF(mTvOutFd);

    //mParams->win.global_alpha = 0;
    //mParams->priority;
    mParams->win.w.left = offset_x;
    mParams->win.w.top = offset_y;
    mParams->win.w.width = width;
    mParams->win.w.height = height;
    
    int ret = tv20_v4l2_s_parm(mTvOutFd, &mStreamParams);
    RETURN_IF(ret);

    return 0;
}

int SecTv::setCrop(int offset_x, int offset_y, int width, int height)
{
    LOGV("%s", __func__);
    RETURN_IF(mTvOutFd);

    mCropWin.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    mCropWin.c.left = offset_x;
    mCropWin.c.top = offset_y;
    mCropWin.c.width = width;
    mCropWin.c.height = height;

    int ret = tv20_v4l2_s_crop(mTvOutFd, offset_x, offset_y, width, height);
    RETURN_IF(ret);

    return ret;
}

int SecTv::setFormat(int width, int height, int format)
{
    int ret, bpp, y_size;
    unsigned int addr;
   
    LOGV("%s", __func__);
    RETURN_IF(mTvOutFd);

    bpp = get_pixel_depth(format);

    LOGV("%s - width(%i), height(%i), format(%i), bpp(%i)", __func__,
         width, height, format, bpp);
    if(mWidth == width && mHeight == height && mFormat == format) {
        LOGV("%s: Nothing changed", __func__);
        return 0;
    }

    /* enum_fmt, s_fmt sample */
    ret = tv20_v4l2_enum_fmt(mTvOutFd, format);
    RETURN_IF(ret);

    addr = (unsigned int)mFimcHandle.out_buf.phys_addr;
    y_size = ALIGN_TO_8KB(ALIGN_TO_128B(width) * ALIGN_TO_32B(height));
    ret = tv20_v4l2_s_fmt(mTvOutFd, width, height, format, 
                          (unsigned int) addr,
                          (unsigned int) addr + y_size);
    RETURN_IF(ret);

    fimc_set_src_img_param(&mFimcHandle, width, height,
                           (void *) addr,
                           (void *) (addr + y_size),
                           0);

    mWidth = width;
    mHeight = height;
    mFormat = format;
    return 0;
}

const __u8* SecTv::getName()
{
    LOGV("%s", __func__);
    return tv20_v4l2_enum_output(mTvOutFd, mIndex);
}

int SecTv::enableAudio()
{
    LOGV("%s", __func__);
    RETURN_IF(mTvOutFd);

    int ret = tv20_v4l2_audio_enable(mTvOutFd);
    RETURN_IF(ret);

    mAudioEnabled = true;
    return 0;
}

int SecTv::disableAudio()
{
    LOGV("%s", __func__);
    RETURN_IF(mTvOutFd);

    int ret = tv20_v4l2_audio_disable(mTvOutFd);
    RETURN_IF(ret);

    mAudioEnabled = false;
    return 0;
}

int SecTv::mute()
{
    LOGV("%s", __func__);
    RETURN_IF(mTvOutFd);

    int ret = 0;
    if(!isMuted()) {
        LOGV("muting");
        ret = tv20_v4l2_audio_mute(mTvOutFd);
    } else {
        LOGV("unmuting");
        ret = tv20_v4l2_audio_unmute(mTvOutFd);
    }

    RETURN_IF(ret);
    return 0;
}

bool SecTv::isMuted()
{
    LOGV("%s", __func__);
    RETURN_IF(mTvOutFd);
    
    return tv20_v4l2_audio_get_mute_state(mTvOutFd) > 0;
}

int SecTv::draw(unsigned int addr, unsigned int size)
{
    int ret;
    s5p_fimc_params_t* params;
    struct fimc_buf fimc_src_buf;

    params = &(mFimcHandle.params);
    fimc_src_buf.base[0] = params->src.buf_addr_phy_rgb_y;
    fimc_src_buf.base[1] = params->src.buf_addr_phy_cb;
    fimc_src_buf.base[2] = params->src.buf_addr_phy_cr;

    // test cpy to tvout
    /*char pixels[100];
    memset(&pixels, 0xF8, 100);
    memcpy(mFimcHandle.out_buf.phys_addr, &pixels, 100);*/

    ret = fimc_queue(mFimcHandle.dev_fd, &fimc_src_buf);
    RETURN_IF(ret);

    ret = fimc_dequeue(mFimcHandle.dev_fd);
    RETURN_IF(ret);

    return 0;
}

int SecTv::enable(bool shouldEnableAudio)
{
    //struct fimc_buf fimc_src_buf;

    LOGV("%s", __func__);
    RETURN_IF(mTvOutFd);

    int ret = tv20_v4l2_streamon(mTvOutFd);
    RETURN_IF(ret);

    if(shouldEnableAudio) {
        LOGV("%s: enabling audio too", __func__);
        ret = enableAudio();
        RETURN_IF(ret);
    }

    s5p_fimc_params_t* params = &(mFimcHandle.params);
    ret = fimc_set_src(mFimcHandle.dev_fd, mFimcHandle.hw_ver, &params->src);
    RETURN_IF(ret);
    
    ret = fimc_set_dst(mFimcHandle.dev_fd, &params->dst, 0,
                       (unsigned int) mFimcHandle.out_buf.phys_addr);
    RETURN_IF(ret);

    ret = fimc_stream_on(mFimcHandle.dev_fd, V4L2_BUF_TYPE_VIDEO_OUTPUT);
    RETURN_IF(ret);

    /*
    fimc_src_buf.base[0] = params->src.buf_addr_phy_rgb_y;
	fimc_src_buf.base[1] = params->src.buf_addr_phy_cb;
	fimc_src_buf.base[2] = params->src.buf_addr_phy_cr;

	ret = fimc_stream_on(mFimcHandle.dev_fd, V4L2_BUF_TYPE_VIDEO_OUTPUT);
    RETURN_IF(ret);

	fimc_queue(mFimcHandle.dev_fd, &fimc_src_buf);
    RETURN_IF(ret);

	fimc_dequeue(mFimcHandle.dev_fd);
    RETURN_IF(ret);

	fimc_stream_off(mFimcHandle.dev_fd);
    RETURN_IF(ret);
    */

    mRunning = true;

    return 0;
}

int SecTv::disable()
{
    int ret;

    LOGV("%s", __func__);
    RETURN_IF(mTvOutFd);

    if (!mRunning) {
        return 0;
    }
    
    ret = fimc_clr_buf(mFimcHandle.dev_fd);
    RETURN_IF(ret);

    ret = fimc_stream_off(mFimcHandle.dev_fd);
    RETURN_IF(ret);

    ret = tv20_v4l2_streamoff(mTvOutFd);
    RETURN_IF(ret);

    if(mAudioEnabled) {
        LOGV("%s: disabling audio too", __func__);
        ret = disableAudio();
        RETURN_IF(ret);
    }

    mRunning = false;

    return 0;
}
