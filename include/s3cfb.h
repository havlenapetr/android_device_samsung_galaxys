/* linux/drivers/video/samsung/s3cfb.h
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *              http://www.samsung.com/
 *
 * Header file for Samsung Display Driver (FIMD) driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef _S3CFB_H
#define _S3CFB_H

/*
 * C O M M O N   D E F I N I T I O N S
 *
*/
#define S3CFB_NAME		"s3cfb"

#define S3CFB_AVALUE(r, g, b)	(((r & 0xf) << 8) | \
				((g & 0xf) << 4) | \
				((b & 0xf) << 0))
#define S3CFB_CHROMA(r, g, b)	(((r & 0xff) << 16) | \
				((g & 0xff) << 8) | \
				((b & 0xff) << 0))


/*
 * S T R U C T U R E S  F O R  C U S T O M  I O C T L S
 *
*/
struct s3cfb_user_window {
	int x;
	int y;
};

struct s3cfb_user_plane_alpha {
	int		channel;
	unsigned char	red;
	unsigned char	green;
	unsigned char	blue;
};

struct s3cfb_user_chroma {
	int		enabled;
	unsigned char	red;
	unsigned char	green;
	unsigned char	blue;
};

struct s3cfb_next_info {
	unsigned int phy_start_addr;
	unsigned int xres;		/* visible resolution*/
	unsigned int yres;
	unsigned int xres_virtual;	/* virtual resolution*/
	unsigned int yres_virtual;
	unsigned int xoffset;		/* offset from virtual to visible */
	unsigned int yoffset;		/* resolution */
	unsigned int lcd_offset_x;
	unsigned int lcd_offset_y;
};

/*
 * C U S T O M  I O C T L S
 *
*/
#define S3CFB_WIN_POSITION		_IOW('F', 203, \
						struct s3cfb_user_window)
#define S3CFB_WIN_SET_PLANE_ALPHA	_IOW('F', 204, \
						struct s3cfb_user_plane_alpha)
#define S3CFB_WIN_SET_CHROMA		_IOW('F', 205, \
						struct s3cfb_user_chroma)
#define S3CFB_SET_VSYNC_INT		_IOW('F', 206, u32)
#define S3CFB_GET_VSYNC_INT_STATUS	_IOR('F', 207, u32)
#define S3CFB_GET_LCD_WIDTH		_IOR('F', 302, int)
#define S3CFB_GET_LCD_HEIGHT		_IOR('F', 303, int)
#define S3CFB_SET_WRITEBACK		_IOW('F', 304, u32)
#define S3CFB_GET_CURR_FB_INFO		_IOR('F', 305, struct s3cfb_next_info)
#define S3CFB_SET_WIN_ON		_IOW('F', 306, u32)
#define S3CFB_SET_WIN_OFF		_IOW('F', 307, u32)
#define S3CFB_SET_WIN_PATH		_IOW('F', 308, \
						enum s3cfb_data_path_t)
#define S3CFB_SET_WIN_ADDR		_IOW('F', 309, unsigned long)
#define S3CFB_SET_WIN_MEM		_IOW('F', 310, \
						enum s3cfb_mem_owner_t)

#endif /* _S3CFB_H */
