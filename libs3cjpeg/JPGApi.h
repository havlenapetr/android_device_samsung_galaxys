/*
 * Project Name JPEG API for HW JPEG IP IN Linux
 * Copyright  2007 Samsung Electronics Co, Ltd. All Rights Reserved.
 *
 * This software is the confidential and proprietary information
 * of Samsung Electronics  ("Confidential Information").
 * you shall not disclose such Confidential Information and shall use
 * it only in accordance with the terms of the license agreement
 * you entered into with Samsung Electronics
 *
 * This file implements JPEG driver.
 *
 * @name JPEG DRIVER MODULE Module (JPGApi.h)
 * @author Jiun Yu (jiun.yu@samsung.com)
 * @date 17-07-07
 */
#ifndef __JPG_API_H__
#define __JPG_API_H__

#define MAX_JPG_WIDTH				3072
#define MAX_JPG_HEIGHT				2048
#define MAX_YUV_SIZE				(MAX_JPG_WIDTH * MAX_JPG_HEIGHT * 3)
#define	MAX_FILE_SIZE				(MAX_JPG_WIDTH * MAX_JPG_HEIGHT)
#define MAX_JPG_THUMBNAIL_WIDTH			320
#define MAX_JPG_THUMBNAIL_HEIGHT		240
#define MAX_YUV_THUMB_SIZE			(MAX_JPG_THUMBNAIL_WIDTH * MAX_JPG_THUMBNAIL_HEIGHT * 3)
#define	MAX_FILE_THUMB_SIZE			(MAX_JPG_THUMBNAIL_WIDTH * MAX_JPG_THUMBNAIL_HEIGHT)
#define EXIF_FILE_SIZE				28800

#define JPG_STREAM_BUF_SIZE			(MAX_JPG_WIDTH * MAX_JPG_HEIGHT)
#define JPG_STREAM_THUMB_BUF_SIZE	(MAX_JPG_THUMBNAIL_WIDTH * MAX_JPG_THUMBNAIL_HEIGHT)
#define JPG_FRAME_BUF_SIZE			(MAX_JPG_WIDTH * MAX_JPG_HEIGHT * 3)
#define JPG_FRAME_THUMB_BUF_SIZE	(MAX_JPG_THUMBNAIL_WIDTH * MAX_JPG_THUMBNAIL_HEIGHT * 3)

#define JPG_TOTAL_BUF_SIZE			(JPG_STREAM_BUF_SIZE + JPG_STREAM_THUMB_BUF_SIZE \
			      + JPG_FRAME_BUF_SIZE + JPG_FRAME_THUMB_BUF_SIZE)

typedef	unsigned char	UCHAR;
typedef unsigned long	ULONG;
typedef	unsigned int	UINT;
typedef unsigned long	DWORD;
typedef unsigned int	UINT32;
typedef int				INT32;
typedef unsigned char	UINT8;
typedef enum {FALSE, TRUE} BOOL;


typedef enum {
	JPG_444,
	JPG_422,
	JPG_420,
	JPG_400,
	RESERVED1,
	RESERVED2,
	JPG_411,
	JPG_SAMPLE_UNKNOWN
} sample_mode_t;

typedef enum {
	JPG_QUALITY_LEVEL_1 = 0, /*high quality*/
	JPG_QUALITY_LEVEL_2,
	JPG_QUALITY_LEVEL_3,
	JPG_QUALITY_LEVEL_4     /*low quality*/
} image_quality_type_t;

typedef enum {
	JPEG_GET_DECODE_WIDTH,
	JPEG_GET_DECODE_HEIGHT,
	JPEG_SET_DECODE_OUT_FORMAT,
	JPEG_GET_SAMPING_MODE,
	JPEG_SET_ENCODE_WIDTH,
	JPEG_SET_ENCODE_HEIGHT,
	JPEG_SET_ENCODE_QUALITY,
	JPEG_SET_ENCODE_THUMBNAIL,
	JPEG_SET_ENCODE_IN_FORMAT,
	JPEG_SET_SAMPING_MODE,
	JPEG_SET_THUMBNAIL_WIDTH,
	JPEG_SET_THUMBNAIL_HEIGHT
} jpeg_conf;

typedef enum {
	JPEG_FAIL,
	JPEG_OK,
	JPEG_ENCODE_FAIL,
	JPEG_ENCODE_OK,
	JPEG_DECODE_FAIL,
	JPEG_DECODE_OK,
	JPEG_HEADER_PARSE_FAIL,
	JPEG_HEADER_PARSE_OK,
	JPEG_DISPLAY_FAIL,
	JPEG_OUT_OF_MEMORY,
	JPEG_UNKNOWN_ERROR
} jpeg_error_type;

typedef enum {
	JPEG_USE_HW_SCALER = 1,
	JPEG_USE_SW_SCALER = 2
} jpeg_scaler;

typedef enum {
	YCBCR_422,
	YCBCR_420,
	YCBCR_SAMPLE_UNKNOWN
} jpeg_out_mode;

typedef enum {
	JPG_MODESEL_YCBCR = 1,
	JPG_MODESEL_RGB,
	JPG_MODESEL_UNKNOWN
} jpeg_in_mode;

typedef struct {
	char	make[32];
	char	Model[32];
	char	Version[32];
	char	DateTime[32];
	char	CopyRight[32];

	UINT	Height;
	UINT	Width;
	UINT	Orientation;
	UINT	ColorSpace;
	UINT	Process;
	UINT	Flash;

	UINT	FocalLengthNum;
	UINT	FocalLengthDen;

	UINT	ExposureTimeNum;
	UINT	ExposureTimeDen;

	UINT	FNumberNum;
	UINT	FNumberDen;

	UINT	ApertureFNumber;

	int		SubjectDistanceNum;
	int		SubjectDistanceDen;

	UINT	CCDWidth;

	int		ExposureBiasNum;
	int		ExposureBiasDen;


	int		WhiteBalance;

	UINT	MeteringMode;

	int		ExposureProgram;

	UINT	ISOSpeedRatings[2];

	UINT	FocalPlaneXResolutionNum;
	UINT	FocalPlaneXResolutionDen;

	UINT	FocalPlaneYResolutionNum;
	UINT	FocalPlaneYResolutionDen;

	UINT	FocalPlaneResolutionUnit;

	UINT	XResolutionNum;
	UINT	XResolutionDen;
	UINT	YResolutionNum;
	UINT	YResolutionDen;
	UINT	RUnit;

	int		BrightnessNum;
	int		BrightnessDen;

	char	UserComments[150];
} exif_file_info_t;


#ifdef __cplusplus
extern "C" {
#endif



	int SsbSipJPEGDecodeInit(void);
	int SsbSipJPEGEncodeInit(void);
	jpeg_error_type SsbSipJPEGDecodeExe(int dev_fd);
	jpeg_error_type SsbSipJPEGEncodeExe(int dev_fd, exif_file_info_t *Exif, jpeg_scaler scaler);
	void *SsbSipJPEGGetDecodeInBuf(int dev_fd, long size);
	void *SsbSipJPEGGetDecodeOutBuf(int dev_fd, long *size);
	void *SsbSipJPEGGetEncodeInBuf(int dev_fd, long size);
	void *SsbSipJPEGGetEncodeOutBuf(int dev_fd, long *size);
	jpeg_error_type SsbSipJPEGSetConfig(jpeg_conf type, INT32 value);
	jpeg_error_type SsbSipJPEGGetConfig(jpeg_conf type, INT32 *value);
	jpeg_error_type SsbSipJPEGDecodeDeInit(int dev_fd);
	jpeg_error_type SsbSipJPEGEncodeDeInit(int dev_fd);
	void *SsbSipJPEGGetThumbEncInBuf(int dev_fd, long size);
	void SsbSipJPEGSetEncodeInBuf(int dev_fd, UINT32 phys_frmbuf, long size);
	int SetMapAddr(unsigned char*);

#ifdef __cplusplus
}
#endif

#endif
