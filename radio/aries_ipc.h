/**
 * This file is part of libsamsung-ipc.
 *
 * Copyright (C) 2011 Paul Kocialkowski <contact@paulk.fr>
 *                    Igor Almeida <igor.contato@gmail.com>
 *
 * libsamsung-ipc is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * libsamsung-ipc is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with libsamsung-ipc.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef __ARIES_IPC_H__
#define __ARIES_IPC_H__

#include "phonet.h"

#define BOOTCORE_VERSION        0xf0
#define PSI_MAGIC               0x30
#define PSI_DATA_LEN            0x5000
#define RADIO_IMG_MAX_SIZE      0xd80000
#define RADIO_IMG_READ_SIZE     0xa00000
#define ONENAND_MAP_SIZE        0xFFF000
#define ONEDRAM_INIT_READ       0x12341234
#define ONEDRAM_DEINIT_CMD      0x45674567
#define ONEDRAM_DEINIT_READ     0xabcdabcd
#define SO_RFSMAGIC             0x21
#define SOCKET_RFS_MAGIC        0x80000
#define PHONET_IFACE            "svnet0"
#define PHONET_SPN_RES_FMT      0x01
#define PHONET_SPN_RES_RFS      0x41

#define MAX_MODEM_DATA_SIZE     0x1000

int phonet_iface_ifdown(void);
int phonet_iface_ifup(void);

struct aries_ipc_handlers_common_data
{
    int fd;
    struct sockaddr_pn *spn;
};

#endif

// vim:ts=4:sw=4:expandtab
