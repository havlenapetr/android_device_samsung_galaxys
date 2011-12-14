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

#ifndef ANDROID_HARDWARE_SEC_TV_H
#define ANDROID_HARDWARE_SEC_TV_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/stat.h>

#include <linux/videodev2.h>
#include <videodev2_samsung.h>

namespace android {

#define TV_DEV_NAME         "/dev/video14"

/* TVOUT video */
#define TV_DEV_V_NAME       "/dev/video21"

// Analog  SVIDEO int s5p_tv_v4l2.c
#define TV_DEV_INDEX        1
#define TV_STANDART_INDEX   0

#define DEFAULT_FB          0

/*
* V4L2 TVOUT EXTENSIONS
*
*/
#define V4L2_INPUT_TYPE_MSDMA			3
#define V4L2_INPUT_TYPE_FIFO			4

#define V4L2_OUTPUT_TYPE_MSDMA			4
#define V4L2_OUTPUT_TYPE_COMPOSITE		5
#define V4L2_OUTPUT_TYPE_SVIDEO			6
#define V4L2_OUTPUT_TYPE_YPBPR_INERLACED	7
#define V4L2_OUTPUT_TYPE_YPBPR_PROGRESSIVE	8
#define V4L2_OUTPUT_TYPE_RGB_PROGRESSIVE	9
#define V4L2_OUTPUT_TYPE_DIGITAL		10
#define V4L2_OUTPUT_TYPE_HDMI			V4L2_OUTPUT_TYPE_DIGITAL
#define V4L2_OUTPUT_TYPE_HDMI_RGB		11
#define V4L2_OUTPUT_TYPE_DVI			12

#define V4L2_STD_PAL_BDGHI      (V4L2_STD_PAL_B| \
				 V4L2_STD_PAL_D| \
				 V4L2_STD_PAL_G| \
				 V4L2_STD_PAL_H| \
				 V4L2_STD_PAL_I)

#define V4L2_STD_480P_60_16_9	((v4l2_std_id)0x04000000)
#define V4L2_STD_480P_60_4_3	((v4l2_std_id)0x05000000)
#define V4L2_STD_576P_50_16_9	((v4l2_std_id)0x06000000)
#define V4L2_STD_576P_50_4_3	((v4l2_std_id)0x07000000)
#define V4L2_STD_720P_60	((v4l2_std_id)0x08000000)
#define V4L2_STD_720P_50	((v4l2_std_id)0x09000000)
#define V4L2_STD_1080P_60	((v4l2_std_id)0x0a000000)
#define V4L2_STD_1080P_50	((v4l2_std_id)0x0b000000)
#define V4L2_STD_1080I_60	((v4l2_std_id)0x0c000000)
#define V4L2_STD_1080I_50	((v4l2_std_id)0x0d000000)
#define V4L2_STD_480P_59	((v4l2_std_id)0x0e000000)
#define V4L2_STD_720P_59	((v4l2_std_id)0x0f000000)
#define V4L2_STD_1080I_59	((v4l2_std_id)0x10000000)
#define V4L2_STD_1080P_59	((v4l2_std_id)0x11000000)
#define V4L2_STD_1080P_30	((v4l2_std_id)0x12000000)

#define FORMAT_FLAGS_DITHER		0x01
#define FORMAT_FLAGS_PACKED		0x02
#define FORMAT_FLAGS_PLANAR		0x04
#define FORMAT_FLAGS_RAW		0x08
#define FORMAT_FLAGS_CrCb		0x10

#define V4L2_FBUF_FLAG_PRE_MULTIPLY	0x0040
#define V4L2_FBUF_CAP_PRE_MULTIPLY	0x0080

struct v4l2_window_s5p_tvout {
    unsigned int capability;
    unsigned int flags;
    unsigned int priority;

    struct v4l2_window win;
};

struct v4l2_pix_format_s5p_tvout {
    void *base_y;
    void *base_c;
    bool src_img_endian;

    struct v4l2_pix_format pix_fmt;
};

enum s5p_tv_standart {
    S5P_TV_STD_NTSC_M = 0,
	S5P_TV_STD_PAL_BDGHI,
	S5P_TV_STD_PAL_M,
	S5P_TV_STD_PAL_N,
	S5P_TV_STD_PAL_Nc,
	S5P_TV_STD_PAL_60,
	S5P_TV_STD_NTSC_443,
	S5P_TV_STD_480P_60_16_9,
	S5P_TV_STD_480P_60_4_3,
	S5P_TV_STD_576P_50_16_9,
	S5P_TV_STD_576P_50_4_3,
	S5P_TV_STD_720P_60,
	S5P_TV_STD_720P_50
};

// must match with s5p_tv_outputs in s5p_tv_v4l.c
enum s5p_tv_output {
    S5P_TV_OUTPUT_TYPE_COMPOSITE = 0,
    S5P_TV_OUTPUT_TYPE_SVIDEO,
    S5P_TV_OUTPUT_TYPE_YPBPR_INERLACED,
    S5P_TV_OUTPUT_TYPE_YPBPR_PROGRESSIVE,
    S5P_TV_OUTPUT_TYPE_RGB_PROGRESSIVE,
    S5P_TV_OUTPUT_TYPE_HDMI,
};

class SecTv {

public:
    ~SecTv();

    const __u8*     getName();

    int             init(int fbIndex, int format);

    int             setStandart(s5p_tv_standart standart);
    int             setOutput(s5p_tv_output output);

    int             setWindow(int offset_x, int offset_y, int width, int height);
    int             setCrop(int offset_x, int offset_y, int width, int height);
    int             setFormat(int width, int height, int format);

    int             enable(bool shouldEnableAudio = false);
    int             disable();
    int             mute();
    bool            isMuted();
    bool            isEnabled() { return mRunning; };
    bool            isAudioEnabled() { return mAudioEnabled; };

    // open hardware via this method
    static int      openTvOut(SecTv** hardware);

private:
    SecTv(int fd, int index);

    int             mTvOutFd;
    int             mFrameBuffFd;
    int             mTvOutVFd;
    int             mIndex;
    
    int             mWidth;
    int             mHeight;
    int             mFormat;

    bool            mRunning;
    bool            mAudioEnabled;

    char*           mImageMemory;

    int             initLayer();
    void            deinitLayer();
    int             enableAudio();
    int             disableAudio();

    v4l2_streamparm mStreamParams;
    struct v4l2_crop mCropWin;
    struct v4l2_window_s5p_tvout* mParams;
};

}; // namespace android

#endif // ANDROID_HARDWARE_SEC_TV_H
