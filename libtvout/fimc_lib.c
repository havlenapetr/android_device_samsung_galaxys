/*
 * Copyright (C) 2011 Havlena Petr, <havlenapetr@gmail.com>
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
#define LOG_TAG "libfimc"
#include <cutils/log.h>

#include <linux/fb.h>
#include <linux/videodev.h>

#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <asm/sizes.h>

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/poll.h>
#include <sys/mman.h>

#include "s5p_fimc.h"

#define  FIMC2_DEV_NAME  "/dev/video2"

#define ALIGN(x, a)    (((x) + (a) - 1) & ~((a) - 1))

int
fimc_set_src(int fd, unsigned int hw_ver, s5p_fimc_img_info *src)
{
	struct v4l2_format      fmt;
	struct v4l2_cropcap     cropcap;
	struct v4l2_crop        crop;
	struct v4l2_requestbuffers req;
	int                     ret_val;
    
	/*
	 * To set size & format for source image (DMA-INPUT)
	 */
    
	fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	fmt.fmt.pix.width = src->full_width;
	fmt.fmt.pix.height = src->full_height;
	fmt.fmt.pix.pixelformat = src->color_space;
	fmt.fmt.pix.field = V4L2_FIELD_NONE;
    
	ret_val = ioctl (fd, VIDIOC_S_FMT, &fmt);
	if (ret_val < 0) {
		LOGE("VIDIOC_S_FMT failed : ret=%d\n", ret_val);
		return -1;
	}
    
	/*
	 * crop input size
	 */
    
	crop.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    
	if(hw_ver == 0x50) {
		crop.c.left = src->start_x;
		crop.c.top = src->start_y;
	} else {
		crop.c.left   = 0;
		crop.c.top    = 0;
	}
    
#ifdef EEEBOT_FIMC
	crop.c.width  = src->width; //CROP_WIDTH;
	crop.c.height = CROP_HEIGHT;
#else
	crop.c.width  = src->width;
	crop.c.height = src->height;
#endif
	ret_val = ioctl(fd, VIDIOC_S_CROP, &crop);
	if (ret_val < 0) {
		LOGE("Error in video VIDIOC_S_CROP (%d)\n",ret_val);
		return -1;
	}
    
	/*
	 * input buffer type
	 */
	req.count = 1;
	req.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	req.memory = V4L2_MEMORY_USERPTR;
    
	ret_val = ioctl (fd, VIDIOC_REQBUFS, &req);
	if (ret_val < 0) {
		LOGE("Error in VIDIOC_REQBUFS (%d)\n", ret_val);
		return -1;
	}
    
	return ret_val;
}

int
fimc_set_dst(int fd, s5p_fimc_img_info *dst, int rotation, unsigned int addr)
{
	struct v4l2_format      sFormat;
	struct v4l2_control     vc;
	struct v4l2_framebuffer fbuf;
	int                     ret_val;
    
	/*
	 * set rotation configuration
	 */
    
	vc.id = V4L2_CID_ROTATION;
	vc.value = rotation;
    
	ret_val = ioctl(fd, VIDIOC_S_CTRL, &vc);
	if (ret_val < 0) {
		LOGE("Error in video VIDIOC_S_CTRL - rotation (%d)\n",ret_val);
		return -1;
	}
    
	/*
	 * set size, format & address for destination image (DMA-OUTPUT)
	 */
	ret_val = ioctl(fd, VIDIOC_G_FBUF, &fbuf);
	if (ret_val < 0) {
		LOGE("Error in video VIDIOC_G_FBUF (%d)\n", ret_val);
		return -1;
	}
    
	fbuf.base = (void *)addr;
	fbuf.fmt.width = dst->full_width;
	fbuf.fmt.height = dst->full_height;
	fbuf.fmt.pixelformat = dst->color_space;
    
	ret_val = ioctl (fd, VIDIOC_S_FBUF, &fbuf);
	if (ret_val < 0) {
		LOGE("Error in video VIDIOC_S_FBUF (%d)\n",ret_val);
		return -1;
	}
    
	/*
	 * set destination window
	 */
    
	sFormat.type 			= V4L2_BUF_TYPE_VIDEO_OVERLAY;
	sFormat.fmt.win.w.left 		= dst->start_x;
	sFormat.fmt.win.w.top 		= dst->start_y;
	sFormat.fmt.win.w.width 	= dst->width;
	sFormat.fmt.win.w.height 	= dst->height;
    
	ret_val = ioctl(fd, VIDIOC_S_FMT, &sFormat);
	if (ret_val < 0) {
		LOGE("Error in video VIDIOC_S_FMT (%d)\n",ret_val);
		return -1;
	}
    
	return 0;
}

int
fimc_stream_on(int fd, enum v4l2_buf_type type)
{
	if (-1 == ioctl (fd, VIDIOC_STREAMON, &type)) {
		LOGE("Error in VIDIOC_STREAMON\n");
		return -1;
	}
    
	return 0;
}

int
fimc_queue(int fd, struct fimc_buf *fimc_buf)
{
	struct v4l2_buffer  buf;
	int                 ret_val;
    
	buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	buf.memory = V4L2_MEMORY_USERPTR;
	buf.m.userptr = (unsigned long)fimc_buf;
	buf.length = 0;
	buf.index = 0;
    
	ret_val = ioctl (fd, VIDIOC_QBUF, &buf);
	if (0 > ret_val) {
		LOGE("Error in VIDIOC_QBUF : (%d) \n", ret_val);
		return -1;
	}
    
	return 0;
}

int
fimc_dequeue(int fd)
{
	struct v4l2_buffer buf;
    
	buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	buf.memory = V4L2_MEMORY_USERPTR;
    
	if (-1 == ioctl (fd, VIDIOC_DQBUF, &buf)) {
		LOGE("Error in VIDIOC_DQBUF\n");
		return -1;
	}
    
	return buf.index;
}

int
fimc_stream_off(int fd)
{
	enum v4l2_buf_type type;
    
	type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    
	if (-1 == ioctl (fd, VIDIOC_STREAMOFF, &type)) {
		LOGE("Error in VIDIOC_STREAMOFF\n");
		return -1;
	}
    
	return 0;
}

int
fimc_clr_buf(int fd)
{
	struct v4l2_requestbuffers req;
    
	req.count = 0;
	req.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	req.memory = V4L2_MEMORY_USERPTR;
    
	if (ioctl (fd, VIDIOC_REQBUFS, &req) == -1) {
		LOGE("Error in VIDIOC_REQBUFS");
	}
    
	return 0;
}

void
fimc_set_src_img_param(s5p_fimc_t* s5p_fimc, unsigned int width, unsigned int height, void *phys_y_addr,void *phys_cb_addr, unsigned int color_space)
{
	/*int color_format = fimc_switch_src_color_format(color_space);
	if(color_format < 0) {
		LOGE("%s unsupported color format", __func__);
		return;
	}*/
	s5p_fimc_params_t *params = &(s5p_fimc->params);
    
	/* set post processor configuration */
	params->src.full_width  = width;
	params->src.full_height = height;
	params->src.start_x     = 0;
	params->src.start_y     = 0;
	params->src.width       = width;
	params->src.height      = height;
	params->src.color_space = /*color_format;*/ V4L2_PIX_FMT_RGB32;

	params->dst.full_width  = width;
	params->dst.full_height = height;
	params->dst.start_x     = 0;
	params->dst.start_y     = 0;
	params->dst.width       = width;
	params->dst.height      = height;
	params->dst.color_space = V4L2_PIX_FMT_NV12T;
    
	params->src.buf_addr_phy_rgb_y 	= phys_y_addr;
	params->src.buf_addr_phy_cb = phys_cb_addr;
	params->src.buf_addr_phy_cr = params->src.buf_addr_phy_cb + (params->src.buf_addr_phy_cb - params->src.buf_addr_phy_rgb_y)/4;
}

int
fimc_open(s5p_fimc_t* s5p_fimc)
{
	struct v4l2_capability  cap;
	struct v4l2_format      fmt;
	struct v4l2_control     vc;
	int                     ret, index;
    
    LOGV("%s - start", __func__);

	/* open device file */
	if(s5p_fimc->dev_fd == 0) {
		s5p_fimc->dev_fd = open(FIMC2_DEV_NAME, O_RDWR);
		if(s5p_fimc->dev_fd <= 0) {
			LOGE("%s::Post processor open error\n", __func__);
			return -1;
		}
	}
    
    LOGV("%s - fd(%i)", __func__, s5p_fimc->dev_fd);

	/* check capability */
	ret = ioctl(s5p_fimc->dev_fd, VIDIOC_QUERYCAP, &cap);
	if (ret < 0) {
		LOGE("VIDIOC_QUERYCAP failed\n");
		return -1;
	}

	if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
		LOGE("%d has no streaming support\n", s5p_fimc->dev_fd);
		return -1;
	}

	if (!(cap.capabilities & V4L2_CAP_VIDEO_OUTPUT)) {
		LOGE("%d is no video output\n", s5p_fimc->dev_fd);
		return -1;
	}

	/*
	 * malloc fimc_outinfo structure
	 */
	fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	ret = ioctl(s5p_fimc->dev_fd, VIDIOC_G_FMT, &fmt);
	if (ret < 0) {
		LOGE("[%s] Error in video VIDIOC_G_FMT\n", __FUNCTION__);
		return -1;
	}

	/*
	 * get baes address of the reserved memory for fimc2
	 */
	vc.id = V4L2_CID_RESERVED_MEM_BASE_ADDR;
	vc.value = 0;

	ret = ioctl(s5p_fimc->dev_fd, VIDIOC_G_CTRL, &vc);
	if (ret < 0) {
		LOGE("Error in video VIDIOC_G_CTRL - V4L2_CID_RESERVED_MEM_BAES_ADDR (%d)\n",ret);
		return -1;
	}

	s5p_fimc->out_buf.phys_addr = vc.value;
	LOGI("[%s] out_buf.phys_addr=%p\n", __func__, s5p_fimc->out_buf.phys_addr);

	vc.id = V4L2_CID_FIMC_VERSION;
	vc.value = 0;

	ret = ioctl(s5p_fimc->dev_fd, VIDIOC_G_CTRL, &vc);
	if (ret < 0) {
		LOGE("Error in video VIDIOC_G_CTRL - V4L2_CID_FIMC_VERSION (%d), FIMC version is set with default\n",ret);
		vc.value = 0x43;
	}
	s5p_fimc->hw_ver = vc.value;
    
    LOGV("%s - end", __func__);

	return 0;
}

int
fimc_close(s5p_fimc_t* s5p_fimc)
{
    LOGV("%s - start", __func__);

	if(s5p_fimc->out_buf.phys_addr != NULL) {
		s5p_fimc->out_buf.phys_addr = NULL;
		s5p_fimc->out_buf.length = 0;
	}

	/* close */
	if(s5p_fimc->dev_fd != 0)
		close(s5p_fimc->dev_fd);

	s5p_fimc->dev_fd = 0;
    
    LOGV("%s - end", __func__);

	return 0;
}