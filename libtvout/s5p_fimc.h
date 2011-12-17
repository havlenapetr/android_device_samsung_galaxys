/*
 * Test application for Samsung Camera Interface (FIMC) driver
 *
 * Jinsung Yang, Copyright (c) 2009 Samsung Electronics
 * 	http://www.samsungsemi.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef _S5P_FIMC_H_
#define _S5P_FIMC_H_

//#include "videodev2.h"
#include "videodev2_samsung.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * G E N E R A L S
 *
*/
#define MIN(x, y)		((x < y) ? x : y)

/*
 * P I X E L   F O R M A T   G U I D E
 *
 * The 'x' means 'DO NOT CARE'
 * The '*' means 'FIMC SPECIFIC'
 * For some fimc formats, we couldn't find equivalent format in the V4L2 FOURCC.
 *
 * FIMC TYPE	PLANES	ORDER		V4L2_PIX_FMT
 * ---------------------------------------------------------
 * RGB565	x	x		V4L2_PIX_FMT_RGB565
 * RGB888	x	x		V4L2_PIX_FMT_RGB24
 * YUV420	2	LSB_CBCR	V4L2_PIX_FMT_NV12
 * YUV420	2	LSB_CRCB	V4L2_PIX_FMT_NV21
 * YUV420	2	MSB_CBCR	V4L2_PIX_FMT_NV21X*
 * YUV420	2	MSB_CRCB	V4L2_PIX_FMT_NV12X*
 * YUV420	3	x		V4L2_PIX_FMT_YUV420
 * YUV422	1	YCBYCR		V4L2_PIX_FMT_YUYV
 * YUV422	1	YCRYCB		V4L2_PIX_FMT_YVYU
 * YUV422	1	CBYCRY		V4L2_PIX_FMT_UYVY
 * YUV422	1	CRYCBY		V4L2_PIX_FMT_VYUY*
 * YUV422	2	LSB_CBCR	V4L2_PIX_FMT_NV16*
 * YUV422	2	LSB_CRCB	V4L2_PIX_FMT_NV61*
 * YUV422	2	MSB_CBCR	V4L2_PIX_FMT_NV16X*
 * YUV422	2	MSB_CRCB	V4L2_PIX_FMT_NV61X*
 * YUV422	3	x		V4L2_PIX_FMT_YUV422P
 *
*/

/*
 * V 4 L 2   F I M C   E X T E N S I O N S
 *
*/
#define V4L2_PIX_FMT_YVYU		v4l2_fourcc('Y', 'V', 'Y', 'U')

/* FOURCC for FIMC specific */
#define V4L2_PIX_FMT_NV12X		v4l2_fourcc('N', '1', '2', 'X')
#define V4L2_PIX_FMT_NV21X		v4l2_fourcc('N', '2', '1', 'X')
#define V4L2_PIX_FMT_VYUY		v4l2_fourcc('V', 'Y', 'U', 'Y')
#define V4L2_PIX_FMT_NV16		v4l2_fourcc('N', 'V', '1', '6')
#define V4L2_PIX_FMT_NV61		v4l2_fourcc('N', 'V', '6', '1')
#define V4L2_PIX_FMT_NV16X		v4l2_fourcc('N', '1', '6', 'X')
#define V4L2_PIX_FMT_NV61X		v4l2_fourcc('N', '6', '1', 'X')

#define V4L2_PIX_FMT_NV12T    	v4l2_fourcc('T', 'V', '1', '2') /* 12  Y/CbCr 4:2:0 64x32 macroblocks */

/* CID extensions */
#define V4L2_CID_ROTATION		(V4L2_CID_PRIVATE_BASE + 0)
#define V4L2_CID_RESERVED_MEM_BASE_ADDR	(V4L2_CID_PRIVATE_BASE + 20)
#define V4L2_CID_FIMC_VERSION		(V4L2_CID_PRIVATE_BASE + 21)
/*
 * U S E R   D E F I N E D   T Y P E S
 *
*/

typedef unsigned int dma_addr_t;

struct fimc_buf {
	dma_addr_t	base[3];
	size_t		length[3];
};

struct fimc_buffer {
	void	*virt_addr;
	void	*phys_addr;
	size_t	length;
};

struct yuv_fmt_list {
	const char 		*name;
	const char 		*desc;
	unsigned int 	fmt;
	int				bpp;
	int				planes;
};

struct img_offset {
    int y_h;
    int y_v;
    int cb_h;
    int cb_v;
    int cr_h;
    int cr_v;
};

//------------ STRUCT ---------------------------------------------------------//

typedef	struct
{
	unsigned int full_width;			// Source Image Full Width (Virtual screen size)
	unsigned int full_height;			// Source Image Full Height (Virtual screen size)
	unsigned int start_x;				// Source Image Start width offset
	unsigned int start_y;				// Source Image Start height offset
	unsigned int width;					// Source Image Width
	unsigned int height;				// Source Image Height
	unsigned int buf_addr_phy_rgb_y;	// Base Address of the Source Image (RGB or Y): Physical Address
	unsigned int buf_addr_phy_cb;		// Base Address of the Source Image (CB Component) : Physical Address
	unsigned int buf_addr_phy_cr;		// Base Address of the Source Image (CR Component) : Physical Address
	unsigned int color_space;			// Color Space of the Source Image
} s5p_fimc_img_info;

typedef struct
{
	s5p_fimc_img_info	src;
	s5p_fimc_img_info	dst;
} s5p_fimc_params_t;

typedef struct _s5p_fimc_t {
    int                dev_fd;
    struct fimc_buffer  out_buf;

    s5p_fimc_params_t   params;

    int                 use_ext_out_mem;
    unsigned int        hw_ver;
} s5p_fimc_t;

//------------------------  functions for v4l2 ------------------------------//
int fimc_open(s5p_fimc_t* s5p_fimc);
int fimc_close(s5p_fimc_t* s5p_fimc);
int fimc_set_src(int fd, unsigned int hw_ver, s5p_fimc_img_info *src);
int fimc_set_dst(int fd, s5p_fimc_img_info *dst, int rotation, unsigned int addr);
int fimc_stream_on(int fd, enum v4l2_buf_type type);
int fimc_queue(int fd, struct fimc_buf *fimc_buf);
int fimc_dequeue(int fd);
int fimc_stream_off(int fd);
int fimc_clr_buf(int fd);
void fimc_set_src_img_param(s5p_fimc_t* s5p_fimc, unsigned int width, unsigned int height,
                            void *phys_y_addr, void *phys_cb_addr, unsigned int color_space);

#ifdef __cplusplus
}
#endif

#endif
