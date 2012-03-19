/**
 * header for onedram driver
 *
 * Copyright (C) 2010 Samsung Electronics. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#ifndef __ONEDRAM_H__
#define __ONEDRAM_H__

#include <linux/ioctl.h>

#define ONEDRAM_GET_AUTH _IOW('o', 0x20, u32)
#define ONEDRAM_PUT_AUTH _IO('o', 0x21)
#define ONEDRAM_REL_SEM _IO('o', 0x22)

#endif /* __ONEDRAM_H__ */
