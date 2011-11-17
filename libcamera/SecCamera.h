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

#ifndef ANDROID_HARDWARE_CAMERA_SEC_H
#define ANDROID_HARDWARE_CAMERA_SEC_H

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

#include "JPGApi.h"

#ifdef BOARD_USES_HDMI
#include "hdmi_lib.h"
#endif

#include <camera/CameraHardwareInterface.h>

namespace android {

#define PREVIEW_USING_MMAP //Define this if the preview data is to be shared using memory mapped technique instead of passing physical address.

//#define JPEG_FROM_SENSOR //Define this if the JPEG images are obtained directly from camera sensor. Else on chip JPEG encoder will be used.

//#define DUAL_PORT_RECORDING //Define this if 2 fimc ports are needed for recording.

//#define SEND_YUV_RECORD_DATA //Define this to copy YUV data to encoder instead of sharing the physical address.

#define INCLUDE_JPEG_THUMBNAIL 1 //Valid only for on chip JPEG encoder

//#define CHECK_PREVIEW_PERFORMANCE     //Uncomment to measure performance

//#define DUMP_YUV        //Uncomment to take a dump of YUV frame during capture

//#define CAMERA_HW_DEBUG // check real H/W ioctl.

#define USE_SEC_CROP_ZOOM // use digital zoom using crop ( <-> optical zoom)

#if defined PREVIEW_USING_MMAP
#define DUAL_PORT_RECORDING
#endif

#if defined JPEG_FROM_SENSOR
#define DIRECT_DELIVERY_OF_POSTVIEW_DATA //Define this if postview data is needed in buffer instead of zero copy.
#endif

#if defined(LOG_NDEBUG) && LOG_NDEBUG == 0
#define LOG_CAMERA LOGD
#define LOG_CAMERA_PREVIEW LOGD

#define LOG_TIME_DEFINE(n) \
	struct timeval time_start_##n, time_stop_##n; unsigned long log_time_##n = 0;

#define LOG_TIME_START(n) \
	gettimeofday(&time_start_##n, NULL);

#define LOG_TIME_END(n) \
	gettimeofday(&time_stop_##n, NULL); log_time_##n = measure_time(&time_start_##n, &time_stop_##n);

#define LOG_TIME(n) \
	log_time_##n

#else
#define LOG_CAMERA(...)
#define LOG_CAMERA_PREVIEW(...)
#define LOG_TIME_DEFINE(n)
#define LOG_TIME_START(n)
#define LOG_TIME_END(n)
#define LOG_TIME(n)
#endif

#define LCD_WIDTH		(480)
#define LCD_HEIGHT		(800)

#define JOIN(x, y) JOIN_AGAIN(x, y)
#define JOIN_AGAIN(x, y) x ## y

#define FRONT_CAM VGA
#define BACK_CAM CE147

#if !defined (FRONT_CAM) || !defined(BACK_CAM)
#error "Please define the Camera module"
#endif

#define SWP1_CAMERA_ADD_ADVANCED_FUNCTION

#ifdef SWP1_CAMERA_ADD_ADVANCED_FUNCTION
    #define CE147_PREVIEW_WIDTH    (640)
    #define CE147_PREVIEW_HEIGHT   (480)
    #define CE147_SNAPSHOT_WIDTH  (2560)
    #define CE147_SNAPSHOT_HEIGHT (1920)
#else
    #define CE147_PREVIEW_WIDTH   (1280)
    #define CE147_PREVIEW_HEIGHT   (720)
    #define CE147_SNAPSHOT_WIDTH  (2592)
    #define CE147_SNAPSHOT_HEIGHT (2592)
#endif

#define CE147_POSTVIEW_WIDTH      (640)
#define CE147_POSTVIEW_WIDE_WIDTH (800)
#define CE147_POSTVIEW_HEIGHT     (480)
#define CE147_POSTVIEW_BPP        (16)

#define VGA_PREVIEW_WIDTH      (640)
#define VGA_PREVIEW_HEIGHT     (480)
#define VGA_SNAPSHOT_WIDTH     (640)
#define VGA_SNAPSHOT_HEIGHT    (480)

#define MAX_BACK_CAMERA_PREVIEW_WIDTH		JOIN(BACK_CAM,_PREVIEW_WIDTH)
#define MAX_BACK_CAMERA_PREVIEW_HEIGHT		JOIN(BACK_CAM,_PREVIEW_HEIGHT)
#define MAX_BACK_CAMERA_SNAPSHOT_WIDTH		JOIN(BACK_CAM,_SNAPSHOT_WIDTH)
#define MAX_BACK_CAMERA_SNAPSHOT_HEIGHT		JOIN(BACK_CAM,_SNAPSHOT_HEIGHT)

#define MAX_FRONT_CAMERA_PREVIEW_WIDTH		JOIN(FRONT_CAM,_PREVIEW_WIDTH)
#define MAX_FRONT_CAMERA_PREVIEW_HEIGHT		JOIN(FRONT_CAM,_PREVIEW_HEIGHT)
#define MAX_FRONT_CAMERA_SNAPSHOT_WIDTH		JOIN(FRONT_CAM,_SNAPSHOT_WIDTH)
#define MAX_FRONT_CAMERA_SNAPSHOT_HEIGHT	JOIN(FRONT_CAM,_SNAPSHOT_HEIGHT)

#define BACK_CAMERA_POSTVIEW_WIDTH			JOIN(BACK_CAM,_POSTVIEW_WIDTH)
#define BACK_CAMERA_POSTVIEW_WIDE_WIDTH		JOIN(BACK_CAM,_POSTVIEW_WIDE_WIDTH)
#define BACK_CAMERA_POSTVIEW_HEIGHT			JOIN(BACK_CAM,_POSTVIEW_HEIGHT)
#define BACK_CAMERA_POSTVIEW_BPP			JOIN(BACK_CAM,_POSTVIEW_BPP)

#define DEFAULT_JPEG_THUMBNAIL_WIDTH		(256)
#define DEFAULT_JPEG_THUMBNAIL_HEIGHT		(192)

#define CAMERA_DEV_NAME	      "/dev/video0"
#define CAMERA_DEV_NAME2      "/dev/video2"

#define CAMERA_DEV_NAME_TEMP  "/data/videotmp_000"
#define CAMERA_DEV_NAME2_TEMP "/data/videotemp_002"

#define BPP             (2)
#define MIN(x, y)       ((x < y) ? x : y)
#define MAX_BUFFERS     (4)

/*
 * V 4 L 2   F I M C   E X T E N S I O N S
 *
*/
#define V4L2_CID_ROTATION                   (V4L2_CID_PRIVATE_BASE + 0)
#define V4L2_CID_PADDR_Y                    (V4L2_CID_PRIVATE_BASE + 1)
#define V4L2_CID_PADDR_CB                   (V4L2_CID_PRIVATE_BASE + 2)
#define V4L2_CID_PADDR_CR                   (V4L2_CID_PRIVATE_BASE + 3)
#define V4L2_CID_PADDR_CBCR                 (V4L2_CID_PRIVATE_BASE + 4)
#define V4L2_CID_STREAM_PAUSE               (V4L2_CID_PRIVATE_BASE + 53)

#define V4L2_CID_CAM_JPEG_MAIN_SIZE         (V4L2_CID_PRIVATE_BASE + 32)
#define V4L2_CID_CAM_JPEG_MAIN_OFFSET       (V4L2_CID_PRIVATE_BASE + 33)
#define V4L2_CID_CAM_JPEG_THUMB_SIZE        (V4L2_CID_PRIVATE_BASE + 34)
#define V4L2_CID_CAM_JPEG_THUMB_OFFSET      (V4L2_CID_PRIVATE_BASE + 35)
#define V4L2_CID_CAM_JPEG_POSTVIEW_OFFSET   (V4L2_CID_PRIVATE_BASE + 36)
#define V4L2_CID_CAM_JPEG_QUALITY           (V4L2_CID_PRIVATE_BASE + 37)

#define TPATTERN_COLORBAR		(1)
#define TPATTERN_HORIZONTAL		(2)
#define TPATTERN_VERTICAL		(3)

#define V4L2_PIX_FMT_YVYU       v4l2_fourcc('Y', 'V', 'Y', 'U')

/* FOURCC for FIMC specific */
#define V4L2_PIX_FMT_VYUY       v4l2_fourcc('V', 'Y', 'U', 'Y')
#define V4L2_PIX_FMT_NV16       v4l2_fourcc('N', 'V', '1', '6')
#define V4L2_PIX_FMT_NV61       v4l2_fourcc('N', 'V', '6', '1')
#define V4L2_PIX_FMT_NV12T      v4l2_fourcc('T', 'V', '1', '2')
/*
 * U S E R	 D E F I N E D   T Y P E S
 *
*/

struct fimc_buffer {
    void   *start;
    size_t  length;
};

struct yuv_fmt_list {
    const char   *name;
    const char   *desc;
    unsigned int  fmt;
    int           depth;
    int           planes;
};

class SecCamera
{
public:
    enum CAMERA_ID
    {
        CAMERA_ID_BACK  = 1,
        CAMERA_ID_FRONT = 2,
    };

    enum AUTO_FOCUS_MODE
	{
		AUTO_FOCUS_BASE,
		AUTO_FOCUS_AUTO,
		AUTO_FOCUS_FIXED,
		AUTO_FOCUS_INFINITY,
		AUTO_FOCUS_MACRO,
		AUTO_FOCUS_MAX,
	};

    enum FRAME_RATE
    {
        FRAME_RATE_BASE = 5,
        FRAME_RATE_MAX  = 30,
    };

    enum SCENE_MODE
    {
        SCENE_MODE_BASE,
        SCENE_MODE_AUTO,
        SCENE_MODE_BEACH,
        SCENE_MODE_CANDLELIGHT,
        SCENE_MODE_FIREWORKS,
        SCENE_MODE_LANDSCAPE,
        SCENE_MODE_NIGHT,
        SCENE_MODE_NIGHTPORTRAIT,
        SCENE_MODE_PARTY,
        SCENE_MODE_PORTRAIT,
        SCENE_MODE_SNOW,
        SCENE_MODE_SPORTS,
        SCENE_MODE_STEADYPHOTO,
        SCENE_MODE_SUNSET,
        SCENE_MODE_MAX,
    };

    enum WHILTE_BALANCE
    {
        WHITE_BALANCE_BASE,
        WHITE_BALANCE_AUTO,
        WHITE_BALANCE_CLOUDY,
        WHITE_BALANCE_SUNNY,
        WHITE_BALANCE_FLUORESCENT,  // 형광등
        WHITE_BALANCE_INCANDESCENT,	// 백열등
        WHITE_BALANCE_OFF,
        WHITE_BALANCE_MAX,
    };

    enum IMAGE_EFFECT
    {
        IMAGE_EFFECT_BASE,
        IMAGE_EFFECT_ORIGINAL,
        IMAGE_EFFECT_AQUA,
        IMAGE_EFFECT_MONO,
        IMAGE_EFFECT_NEGATIVE,
        IMAGE_EFFECT_SEPIA,
        IMAGE_EFFECT_WHITEBOARD,
        IMAGE_EFFECT_MAX,
    };

    enum BRIGHTNESS
    {
        BRIGHTNESS_BASE   = 0,
        BRIGHTNESS_NORMAL = 4,
        BRIGHTNESS_MAX    = 9,
    };

    enum CONTRAST
    {
        CONTRAST_BASE   = 0,
        CONTRAST_NORMAL = 2,
        CONTRAST_MAX    = 4,
    };

    enum SHARPNESS
    {
        SHARPNESS_BASE  = 0,
        SHARPNESS_NOMAL = 2,
        SHARPNESS_MAX   = 4,
    };

    enum SATURATION
    {
        SATURATION_BASE  = 0,
        SATURATION_NOMAL = 2,
        SATURATION_MAX   = 4,
    };

    enum ZOOM
	{
		ZOOM_BASE =  0,
		ZOOM_MAX  = 10,
	};

    enum FLAG
	{
		FLAG_OFF = 0,
		FLAG_ON  = 1,
	};

private:
    int m_flag_create;

    int m_cam_fd;
    int m_cam_fd_rec;

    int m_camera_id;

    int m_preview_v4lformat;
    int m_preview_width;
    int m_preview_height;
    int m_preview_max_width;
    int m_preview_max_height;

    int m_snapshot_v4lformat;
    int m_snapshot_width;
    int m_snapshot_height;
    int m_snapshot_max_width;
    int m_snapshot_max_height;

    int m_flag_overlay;
    int m_overlay_x;
    int m_overlay_y;
    int m_overlay_width;
    int m_overlay_height;

    int m_frame_rate;
    int m_scene_mode;
    int m_white_balance;
    int m_image_effect;
    int m_brightness;
    int m_contrast;
    int m_sharpness;
	int m_saturation;
    int m_zoom;
    int m_angle;
    int m_metering;

    int m_af_mode;

    int m_flag_preview_start;
    int m_flag_record_start;
    int m_flag_current_info_changed;

    int m_current_camera_id;

    int m_current_frame_rate;
	int m_current_scene_mode;
    int m_current_white_balance;
    int m_current_image_effect;
	int m_current_brightness;
	int m_current_contrast;
    int m_current_sharpness;
	int m_current_saturation;
	int m_current_zoom;
    int m_current_af_mode;
    int m_current_metering;

    int m_jpeg_fd;
    int m_jpeg_thumbnail_width;
    int m_jpeg_thumbnail_height;
    int m_jpeg_quality;
    double       m_gps_latitude;
    double       m_gps_longitude;
    unsigned int m_gps_timestamp;
    short        m_gps_altitude;

    struct fimc_buffer m_buffers_c[MAX_BUFFERS];
    struct pollfd m_events_c;

    struct fimc_buffer m_buffers_c_rec[MAX_BUFFERS];
    struct pollfd m_events_c_rec;

 public:

    SecCamera();
    ~SecCamera();

     static SecCamera * createInstance(void)
     {
	     static SecCamera singleton;
	     return &singleton;
     }

    status_t dump(int fd, const Vector<String16>& args);

    int               Create();
    void              Destroy();
    int               flagCreate(void) const;

    int               startPreview(void);
    int               stopPreview (void);
    int               flagPreviewStart(void);

    int               startRecord(void);
    int               stopRecord (void);

    int               getPreview(void);
    //int               getPreview (unsigned char * buffer, unsigned int buffer_size);
    //int               getPreview(int *offset, int *size, unsigned char * buffer, unsigned int buffer_size);
    int               getRecord(void);
    int               setPreviewSize(int   width, int	height, int pixel_format);
    int               getPreviewSize(int * width, int * height);
    int               getPreviewSize(int * width, int * height, unsigned int * frame_size);
    int               getPreviewMaxSize(int * width, int * height);
    int               getPreviewPixelFormat(void);
    int               setPreviewImage(int index, unsigned char * buffer, int size);

    unsigned int      getSnapshot(void);
    int               setSnapshotSize(int	width, int	 height);
    int               getSnapshotSize(int * width, int * height);
    int               getSnapshotSize(int * width, int * height, unsigned int * frame_size);
    int               getSnapshotMaxSize(int * width, int * height);
    int               setSnapshotPixelFormat(int pixel_format);
    int               getSnapshotPixelFormat(void);

    unsigned char *   getJpeg  (unsigned char *snapshot_data, unsigned int snapshot_size, unsigned int * jpeg_size);
    unsigned char *   getJpeg  (unsigned int* jpeg_size, unsigned int * phyaddr);
    unsigned char *   yuv2Jpeg (unsigned char * raw_data, unsigned int raw_size,
                                unsigned int * jpeg_size,
                                int width, int height, int pixel_format);

    void              setJpegQuality(int quality);
    int               setJpegThumbnailSize(int   width, int	height);
    int               getJpegThumbnailSize(int * width, int * height);

    int               setOverlay    (int flag_overlay, int x, int y, int width, int height);
    int               getOverlaySize(int * x, int * y, int * width, int * height);
    int               flagOverlay(void);

    int               setCameraId(int camera_id);
    int               getCameraId(void);

    int               SetRotate(int angle);
    int               getRotate(void);

    void              setFrameRate   (int frame_rate);
    int               getFrameRate   (void);
    int               getFrameRateMin(void);
    int               getFrameRateMax(void);

    int               setSceneMode(int scene_mode);
    int               getSceneMode(void);

    int               setWhiteBalance(int white_balance);
    int               getWhiteBalance(void);

    int               setImageEffect(int image_effect);
    int               getImageEffect(void);

    int               setBrightness(int brightness);
    int               getBrightness(void);
    int               getBrightnessMin(void);
    int               getBrightnessMax(void);

    int               setContrast(int contrast);
    int               getContrast(void);
    int               getContrastMin(void);
    int               getContrastMax(void);

    int               setSaturation(int saturation);
    int               getSaturation(void);
    int               getSaturationMin(void);
    int               getSaturationMax(void);

    int               setSharpness(int sharpness);
    int               getSharpness(void);
    int               getSharpnessMin(void);
    int               getSharpnessMax(void);

    int               setZoom(int zoom);
    int               getZoom(void);
    int               getZoomMin(void);
    int               getZoomMax(void);

    int               setAFMode(int af_mode);
    int               getAFMode(void);
    int               runAF(int flag_on, int * flag_focused);

    int               setGpsInfo(double latitude, double longitude, unsigned int timestamp, int altitude);

    int               getCameraFd(void);
    int               getJpegFd(void);
    void              setJpgAddr(unsigned char *addr);
    unsigned int      getPhyAddrY(int);
    unsigned int      getPhyAddrC(int);
    unsigned int      getRecPhyAddrY(int);
    unsigned int      getRecPhyAddrC(int);

#ifdef SEND_YUV_RECORD_DATA
    void getYUVBuffers(unsigned char** virYAddr, unsigned char** virCaddr, int index);
#endif
    void pausePreview();

private:
    int  m_resetCamera    (void);
    int  m_setCameraId    (int camera_id);
    int  m_setWidthHeightColorFormat(int fd, int colorformat, int width, int height);
    int  m_setFrameRate   (int frame_rate);
    int  m_setSceneMode   (int scene_mode);
    int  m_setWhiteBalance(int white_balance);
	int  m_setImageEffect (int image_effect);
    int  m_setBrightness  (int brightness);
	int  m_setContrast    (int contrast);
    int  m_setMetering    (int metering);
    int  m_setSharpness   (int sharpness);
	int  m_setSaturation  (int saturation);
	int  m_setZoom        (int zoom);
    int  m_setAF          (int af_mode, int flag_run, int flag_on, int * flag_focused);

    int  m_getCropRect    (unsigned int   src_width,  unsigned int   src_height,
                           unsigned int   dst_width,  unsigned int   dst_height,
                           unsigned int * crop_x,     unsigned int * crop_y,
                           unsigned int * crop_width, unsigned int * crop_height,
                           int            zoom);
    inline unsigned int m_frameSize(int format, int width, int height);
    inline void m_makeExifParam(exif_file_info_t *exifFileInfo);
};

extern unsigned long measure_time(struct timeval *start, struct timeval *stop);

}; // namespace android

#endif // ANDROID_HARDWARE_CAMERA_SEC_H
