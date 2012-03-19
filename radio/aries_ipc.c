/**
 * This file is part of libsamsung-ipc.
 *
 * Copyright (C) 2011 Paul Kocialkowski <contact@paulk.fr>
 *                    Joerie de Gram <j.de.gram@gmail.com>
 *                    Simon Busch <morphis@gravedo.de>
 *                    Igor Almeida <igor.contato@gmail.com>
 * Copyright (C) 2012 Petr Havlena <havlenapetr@gmail.com>
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
 */

#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <stdbool.h>
#include <termios.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <linux/netlink.h>
#include <net/if.h>
#include <asm/types.h>
#include <mtd/mtd-abi.h>
#include <assert.h>
#include <errno.h>

#include <ipc_private.h>

#include "aries_ipc.h"
#include "sipc4.h"
#include "onedram.h"
#include "phonet.h"

#define IPC_LOG(...) \
    ipc_client_log(client, __VA_ARGS__)

#define CHECK(fd, ...) \
    if(fd < 0) { \
        IPC_LOG(client, __VA_ARGS__); \
        return -1; \
    }

#define SELECT_READ_FDS(fds, timeout) \
    timeout.tv_sec = 5; \
    timeout.tv_usec = 0; \
    if(select(FD_SETSIZE, &fds, NULL, NULL, &timeout) == 0) { \
        IPC_LOG("%s[%i]: select timeout passed", __func__, __LINE__); \
        goto error_loop; \
    }

#define SELECT_WRITE_FDS(fds, timeout) \
    timeout.tv_sec = 5; \
    timeout.tv_usec = 0; \
    if(select(FD_SETSIZE, NULL, &fds, NULL, &timeout) == 0) { \
        IPC_LOG("%s[%i]: select timeout passed", __func__, __LINE__); \
        goto error_loop; \
    }

static int
phonet_iface_ifdown()
{
    struct ifreq ifr;
    int fd;
    int rc;

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, PHONET_IFACE, IFNAMSIZ);

    fd = socket(AF_PHONET, SOCK_DGRAM, 0);

    rc = ioctl(fd, SIOCGIFFLAGS, &ifr);
    if(rc < 0)
        return -1;

    ifr.ifr_flags = (ifr.ifr_flags & (~IFF_UP));

    rc = ioctl(fd, SIOCSIFFLAGS, &ifr);
    if(rc < 0)
        return -1;

    close(fd);

    return 0;
}

static int
phonet_iface_ifup()
{
    struct ifreq ifr;
    int fd;
    int rc;

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, PHONET_IFACE, IFNAMSIZ);

    fd = socket(AF_PHONET, SOCK_DGRAM, 0);

    rc = ioctl(fd, SIOCGIFFLAGS, &ifr);
    if(rc < 0)
        return -1;

    ifr.ifr_flags |= IFF_UP;

    rc = ioctl(fd, SIOCSIFFLAGS, &ifr);
    if(rc < 0)
        return -1;

    close(fd);

    return 0;
}

static int
aries_modem_bootstrap(struct ipc_client *client)
{
    int s3c2410_serial3_fd = -1;
    int onedram_fd = -1;

    /* Control variables. */
    int boot_tries_count = 0;
    int rc = 0;

    /* Boot variables */
    uint8_t *radio_img_p = NULL;
    uint32_t onedram_data = 0;
    uint8_t bootcore_version = 0;
    uint8_t info_size = 0;
    uint8_t crc_byte = 0;
    int block_size = 0;

    /* s3c2410 serial setup variables. */
    struct termios termios;
    int serial;

    /* fds maniplation variables */
    struct timeval timeout;
    fd_set fds;

    /* nv_data variables */
    void *nv_data_p;
    void *onedram_p;

    /* General purpose variables. */
    uint8_t data;
    uint16_t data_16;
    uint8_t *data_p;
    int i;

    IPC_LOG("%s: enter", __func__);

boot_loop_start:
    if(boot_tries_count > 5) {
        IPC_LOG("%s: boot has failed too many times.", __func__);
        goto error;
    }

    /* Read the radio.img image. */
    IPC_LOG("%s: reading radio image", __func__);
    radio_img_p = ipc_mtd_read(client, "/dev/block/bml12", RADIO_IMG_READ_SIZE, RADIO_IMG_READ_SIZE);
    IPC_LOG("%s: radio image read", __func__);

    IPC_LOG("%s: open onedram", __func__);
    onedram_fd = open("/dev/onedram", O_RDWR);
    CHECK(onedram_fd, "%s: can't open onedram device[%s]!", __func__, strerror(errno));

    /* Reset the modem before init to send the first part of modem.img. */
    IPC_LOG("%s: turning %s iface down", __func__, PHONET_IFACE);
    rc = phonet_iface_ifdown();
    CHECK(rc, "%s: can't bring down phonet iface[%s]!", __func__, strerror(errno));

    ipc_client_power_off(client);
    IPC_LOG("%s: sent PHONE \"off\" command", __func__);
    usleep(1000);

    ipc_client_power_on(client);
    IPC_LOG("%s: sent PHONE \"on\" command", __func__);
    usleep(200000);

    IPC_LOG("%s: open s3c2410_serial3", __func__);
    s3c2410_serial3_fd = open("/dev/s3c2410_serial3", O_RDWR);
    CHECK(s3c2410_serial3_fd,
            "%s: can't open /dev/s3c2410_serial3 [%s]!", __func__, strerror(errno));
    
    /* Setup the s3c2410 serial. */
    IPC_LOG("%s: setup s3c2410_serial3", __func__);
    tcgetattr(s3c2410_serial3_fd, &termios);

    cfmakeraw(&termios);
    cfsetispeed(&termios, B115200);
    cfsetospeed(&termios, B115200);

    tcsetattr(s3c2410_serial3_fd, TCSANOW, &termios);

    /* Send 'AT' in ASCII. */
    IPC_LOG("%s: sending AT in ASCII", __func__);
    for(i=0 ; i < 20 ; i++) {
        rc = write(s3c2410_serial3_fd, "AT", 2);
        usleep(50000);
    }
    IPC_LOG("%s: sending AT in ASCII done", __func__);

    usleep(50000); //FIXME

    /* Write the first part of modem.img. */
    FD_ZERO(&fds);
    FD_SET(s3c2410_serial3_fd, &fds);

    /* Get and check bootcore version. */
    SELECT_READ_FDS(fds, timeout);
    read(s3c2410_serial3_fd, &bootcore_version, sizeof(bootcore_version));
    IPC_LOG("%s: got bootcore version: 0x%x", __func__, bootcore_version);

    if(bootcore_version != BOOTCORE_VERSION)
        goto error_loop;

    /* Get info_size. */
    SELECT_READ_FDS(fds, timeout);
    read(s3c2410_serial3_fd, &info_size, sizeof(info_size));
    IPC_LOG("%s: got info_size: 0x%x", __func__, info_size);

    /* Send PSI magic. */
    data = PSI_MAGIC;
    SELECT_WRITE_FDS(fds, timeout);
    write(s3c2410_serial3_fd, &data, sizeof(data));
    IPC_LOG("%s: sent PSI_MAGIC (0x%x)", __func__, PSI_MAGIC);

    /* Send PSI data len. */
    data_16=PSI_DATA_LEN;
    data_p=(uint8_t *)&data_16;

    for(i=0 ; i < 2 ; i++) {
        write(s3c2410_serial3_fd, data_p, 1);
        data_p++;
    }
    IPC_LOG("%s: sent PSI_DATA_LEN (0x%x)", __func__, PSI_DATA_LEN);

    data_p=radio_img_p;

    IPC_LOG("%s: sending the first part of radio.img", __func__);

    for(i=0 ; i < PSI_DATA_LEN ; i++) {
        SELECT_WRITE_FDS(fds, timeout);
        write(s3c2410_serial3_fd, data_p, 1);

        crc_byte = crc_byte ^ *data_p;
        data_p++;
    }

    IPC_LOG("%s: first part of radio.img sent; crc_byte is 0x%x", __func__, crc_byte);

    SELECT_WRITE_FDS(fds, timeout);
    write(s3c2410_serial3_fd, &crc_byte, sizeof(crc_byte));

    IPC_LOG("%s: crc_byte sent");

    data = 0;
    for(i = 0 ; data != 0x01 ; i++) {
        SELECT_READ_FDS(fds, timeout);
        read(s3c2410_serial3_fd, &data, sizeof(data));

        if(i > 50) {
            IPC_LOG("%s: fairly too much attempts to get ACK", __func__);
            goto error_loop;
        }
    }

    IPC_LOG("%s: close s3c2410_serial3", __func__);
    close(s3c2410_serial3_fd);

    FD_ZERO(&fds);
    FD_SET(onedram_fd, &fds);

    SELECT_READ_FDS(fds, timeout);
    IPC_LOG("%s: wait for 0x12341234 from onedram", __func__);
    read(onedram_fd, &onedram_data, sizeof(onedram_data));
    if(onedram_data != ONEDRAM_INIT_READ) {
        IPC_LOG("%s: wrong onedram init magic (got 0x%04x)", __func__, onedram_data);
        goto error_loop;
    }

    IPC_LOG("%s: got 0x%04x", __func__, onedram_data);
    IPC_LOG("%s: writing the rest of modem.img to onedram.", __func__);

    /* Pointer to the remaining part of radio.img. */
    data_p=radio_img_p + PSI_DATA_LEN;

    onedram_p = mmap(NULL, ONENAND_MAP_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, onedram_fd, 0);
    if(onedram_p == NULL || onedram_p < 0 || onedram_p == 0xffffffff) {
        IPC_LOG("%s: could not map onedram to memory", __func__);
        goto error_loop;
    }
 
    // it sometimes hangs here
    memcpy(onedram_p, data_p, RADIO_IMG_READ_SIZE - PSI_DATA_LEN);

    free(radio_img_p);

    /* nv_data part. */

    /* Check if all the nv_data files are ok. */
    nv_data_check(client);

    /* Check if the MD5 is ok. */
    nv_data_md5_check(client);

    /* Write nv_data.bin to modem_ctl. */
    IPC_LOG("%s: write nv_data to onedram", __func__);

    nv_data_p = ipc_file_read(client, "/efs/nv_data.bin", NV_DATA_SIZE, 1024);
    if (nv_data_p == NULL)
        goto error;
    data_p = nv_data_p;

    memcpy(onedram_p + RADIO_IMG_MAX_SIZE, data_p, NV_DATA_SIZE);

    free(nv_data_p);

    munmap(onedram_p, ONENAND_MAP_SIZE);

    if(ioctl(onedram_fd, ONEDRAM_REL_SEM) < 0) {
        IPC_LOG("%s: ONEDRAM_REL_SEM ioctl on onedram failed", __func__);
        goto error_loop;
    }

    onedram_data = ONEDRAM_DEINIT_CMD;

    IPC_LOG("%s: send 0x%04x", __func__, onedram_data);
    write(onedram_fd, &onedram_data, sizeof(onedram_data));

    SELECT_READ_FDS(fds, timeout);
    read(onedram_fd, &onedram_data, sizeof(onedram_data));
    if(onedram_data != ONEDRAM_DEINIT_READ) {
        IPC_LOG("%s: wrong onedram deinit magic (got 0x%04x)", __func__, onedram_data);
        goto error_loop;
    }

    IPC_LOG("%s: got 0x%04x", __func__, onedram_data);

    close(onedram_fd);

    rc = 0;
    goto exit;

error_loop:
    IPC_LOG("%s: something went wrong", __func__);
    boot_tries_count++;
    sleep(2);

    goto boot_loop_start;

error:
    IPC_LOG("%s: something went wrong", __func__);
    rc = -1;
exit:
    IPC_LOG("%s: exit", __func__);
    return rc;
}

static int
aries_ipc_fmt_client_send(struct ipc_client *client, struct ipc_message_info *request)
{
    struct ipc_header reqhdr;
    void *data;
    int rc = 0;

    reqhdr.mseq = request->mseq;
    reqhdr.aseq = request->aseq;
    reqhdr.group = request->group;
    reqhdr.index = request->index;
    reqhdr.type = request->type;
    reqhdr.length = (uint16_t) (request->length + sizeof(struct ipc_header));

    data = malloc(reqhdr.length);

    memcpy(data, &reqhdr, sizeof(struct ipc_header));
    memcpy((unsigned char *) (data + sizeof(struct ipc_header)), request->data, request->length);

    assert(client->handlers->write != NULL);

    IPC_LOG("aries_ipc_fmt_client_send: SEND FMT!");
    IPC_LOG("aries_ipc_fmt_client_send: IPC request (mseq=0x%02x command=%s (0x%04x) type=%s)", 
                    request->mseq, ipc_command_to_str(IPC_COMMAND(request)), IPC_COMMAND(request),
                    ipc_request_type_to_str(request->type));

#ifdef DEBUG
    if(request->length > 0) {
        IPC_LOG("==== FMT DATA DUMP ====");
        ipc_hex_dump(client, (void *) request->data, request->length);
    }
#endif

    rc = client->handlers->write(data, reqhdr.length, client->handlers->write_data);
    return rc;
}

static int
aries_ipc_rfs_client_send(struct ipc_client *client, struct ipc_message_info *request)
{
    struct rfs_hdr *rfs_hdr;
    void *data;
    int rc = 0;

    data = malloc(request->length + sizeof(struct rfs_hdr));
    memset(data, 0, request->length + sizeof(struct rfs_hdr));

    rfs_hdr = (struct rfs_hdr *) data;

    rfs_hdr->id = request->mseq;
    rfs_hdr->cmd = request->index;
    rfs_hdr->len = request->length + sizeof(struct rfs_hdr);

    memcpy((void *) (data + sizeof(struct rfs_hdr)), request->data, request->length);

    assert(client->handlers->write != NULL);

    IPC_LOG("aries_ipc_rfs_client_send: SEND RFS (id=%d cmd=%d len=%d)!",
            rfs_hdr->id, rfs_hdr->cmd, rfs_hdr->len);
    IPC_LOG("aries_ipc_rfs_client_send: IPC request (mseq=0x%02x command=%s (0x%04x))", 
            request->mseq, ipc_command_to_str(IPC_COMMAND(request)), IPC_COMMAND(request));

#ifdef DEBUG
    if(request->length > 0) {
        IPC_LOG("==== RFS DATA DUMP ====");
        ipc_hex_dump(client, (void *) (data + sizeof(struct rfs_hdr)), request->length);
    }
#endif

    return client->handlers->write((uint8_t*) data, rfs_hdr->len, client->handlers->write_data);
}

static int
aries_ipc_fmt_client_recv(struct ipc_client *client, struct ipc_message_info *response)
{
    struct ipc_header *resphdr;
    void *data;
    int bread = 0;

    data = malloc(MAX_MODEM_DATA_SIZE);
    memset(data, 0, MAX_MODEM_DATA_SIZE);

    memset(response, 0, sizeof(struct ipc_message_info));

    assert(client->handlers->read != NULL);
    bread = client->handlers->read((uint8_t*) data, MAX_MODEM_DATA_SIZE, client->handlers->read_data);

    CHECK(bread, "%s: can't receive enough bytes from modem to process " \
                 "incoming response[%s]!", __func__, strerror(errno));

    if(data == NULL) {
        IPC_LOG("aries_ipc_fmt_client_recv: we retrieve less (or fairly too much) bytes from the modem than we exepected!");
        return -1;
    }

    resphdr = (struct ipc_header *) data;

    response->mseq = resphdr->mseq;
    response->aseq = resphdr->aseq;
    response->group = resphdr->group;
    response->index = resphdr->index;
    response->type = resphdr->type;
    response->length = resphdr->length - sizeof(struct ipc_header);
    response->data = NULL;

    IPC_LOG("aries_ipc_fmt_client_recv: RECV FMT!");
    IPC_LOG("aries_ipc_fmt_client_recv: IPC response (aseq=0x%02x command=%s (0x%04x) type=%s)", 
            response->aseq, ipc_command_to_str(IPC_COMMAND(response)), IPC_COMMAND(response),
            ipc_response_type_to_str(response->type));

    if(response->length > 0) {
#ifdef DEBUG
        IPC_LOG("==== FMT DATA DUMP ====");
        ipc_hex_dump(client, (void *) (data + sizeof(struct ipc_header)), response->length);
#endif
        response->data = malloc(response->length);
        memcpy(response->data, (uint8_t *) data + sizeof(struct ipc_header), response->length);
    }

    free(data);

    return 0;
}

static int
aries_ipc_rfs_client_recv(struct ipc_client *client, struct ipc_message_info *response)
{
    void *data;
    int bread = 0;
    struct rfs_hdr *rfs_hdr;

    data = malloc(MAX_MODEM_DATA_SIZE);
    memset(data, 0, MAX_MODEM_DATA_SIZE);

    memset(response, 0, sizeof(struct ipc_message_info));

    assert(client->handlers->read != NULL);
    bread = client->handlers->read((uint8_t*) data, MAX_MODEM_DATA_SIZE, client->handlers->read_data);
    CHECK(bread, "%s: can't receive enough bytes from modem to process " \
                 "incoming response[%s]!", __func__, strerror(errno));

    rfs_hdr = (struct rfs_hdr *) data;

    if(rfs_hdr->len <= 0 || rfs_hdr->len >= MAX_MODEM_DATA_SIZE || data == NULL) {
        IPC_LOG("aries_ipc_rfs_client_recv: we retrieve less (or fairly too much) bytes from the modem than we exepected!");
        return -1;
    }

    response->mseq = 0;
    response->aseq = rfs_hdr->id;
    response->group = IPC_GROUP_RFS;
    response->index = rfs_hdr->cmd;
    response->type = 0;
    response->length = rfs_hdr->len - sizeof(struct rfs_hdr);
    response->data = NULL;

    IPC_LOG("aries_ipc_rfs_client_recv: RECV RFS (id=%d cmd=%d len=%d)!",
            rfs_hdr->id, rfs_hdr->cmd, rfs_hdr->len - sizeof(struct rfs_hdr));
    IPC_LOG("aries_ipc_rfs_client_recv: IPC response (aseq=0x%02x command=%s (0x%04x))", 
            response->mseq, ipc_command_to_str(IPC_COMMAND(response)), IPC_COMMAND(response));

    if(response->length > 0) {
#ifdef DEBUG
        IPC_LOG("==== RFS DATA DUMP ====");
        ipc_hex_dump(client, (void *) (data + sizeof(struct rfs_hdr)), response->length);
#endif
        response->data = malloc(response->length);
        memcpy(response->data, (uint8_t *) (data + sizeof(struct rfs_hdr)), response->length);
    }

    free(data);

    return 0;
}

static int
aries_ipc_open(void *data, unsigned int size, void *io_data)
{
    struct aries_ipc_handlers_common_data *common_data;
    struct sockaddr_pn *spn;
    struct ifreq ifr;

    int type = *((int *) data);

    int reuse;
    int socket_rfs_magic;

    int fd = -1;
    int rc;

    if(io_data == NULL)
        goto error;

    common_data = (struct aries_ipc_handlers_common_data *) io_data;
    spn = common_data->spn;

    memset(&ifr, 0, sizeof(ifr));
    memset(ifr.ifr_name, 0, IFNAMSIZ);
    memset(spn, 0, sizeof(struct sockaddr_pn));

    strncpy(ifr.ifr_name, PHONET_IFACE, IFNAMSIZ);

    spn->spn_family = AF_PHONET;
    spn->spn_dev = 0;

    switch(type)
    {
        case IPC_CLIENT_TYPE_FMT:
            spn->spn_resource = PHONET_SPN_RES_FMT;
            break;
        case IPC_CLIENT_TYPE_RFS:
            spn->spn_resource = PHONET_SPN_RES_RFS;
            break;
        default:
            break;
    }

    fd = socket(AF_PHONET, SOCK_DGRAM, 0);
    if(fd < 0)
        return -1;

    rc = setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, ifr.ifr_name, IFNAMSIZ);
    if(rc < 0)
        goto error;

    rc = ioctl(fd, SIOCGIFINDEX, &ifr);
    if(rc < 0)
        goto error;

    reuse = 1;
    rc = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    if(rc < 0)
        goto error;

    rc = bind(fd, spn, sizeof(struct sockaddr_pn));
    if(rc < 0)
        goto error;

    common_data->fd = fd;

    if(type == IPC_CLIENT_TYPE_RFS)
    {
        socket_rfs_magic = SOCKET_RFS_MAGIC;
        rc = setsockopt(fd, SOL_SOCKET, SO_RFSMAGIC, &socket_rfs_magic, sizeof(socket_rfs_magic));
        if(rc < 0)
            goto error;
    }

    rc = phonet_iface_ifup();
    if(rc < 0)
        goto error;

    goto end;

error:
    return -1;

end:
    return 0;
}

static int
aries_ipc_close(void *data, unsigned int size, void *io_data)
{
    struct aries_ipc_handlers_common_data *common_data;
    int fd = -1;

    if(io_data == NULL)
        return -1;

    common_data = (struct aries_ipc_handlers_common_data *) io_data;
    fd = common_data->fd;
    if(fd < 0)
        return -1;

    close(fd);

    return 0;
}

static int
aries_ipc_read(void *data, unsigned int size, void *io_data)
{
    struct aries_ipc_handlers_common_data *common_data;
    int spn_len;
    int fd = -1;
    int rc;

    if(io_data == NULL)
        return -1;

    if(data == NULL)
        return -1;

    common_data = (struct aries_ipc_handlers_common_data *) io_data;
    fd = common_data->fd;
    if(fd < 0)
        return -1;

    spn_len = sizeof(struct sockaddr_pn);
    rc = recvfrom(fd, data, size, 0, common_data->spn, &spn_len);

    if(rc < 0)
        return -1;

    return 0;
}

static int
aries_ipc_write(void *data, unsigned int size, void *io_data)
{
    struct aries_ipc_handlers_common_data *common_data;
    int spn_len;
    int fd = -1;
    int rc;

    if(io_data == NULL)
        return -1;

    if(data == NULL)
        return -1;

    common_data = (struct aries_ipc_handlers_common_data *) io_data;
    fd = common_data->fd;
    if(fd < 0)
        return -1;

    spn_len = sizeof(struct sockaddr_pn);

    rc = sendto(fd, data, size, 0, common_data->spn, spn_len);

    if(rc < 0)
        return -1;

    return 0;
}


static int
aries_ipc_is_modem_enabled() {
    char status[1] = { 0 };

    int fd = open("/sys/class/modemctl/xmm/status", O_RDONLY);
    if(fd < 0)
        goto end;

    read(fd, status, 1);

    close(fd);

end:
    return status[0] == '1' ? 1 : 0;
}

static int
aries_ipc_power_on(void *data)
{
    char power_data[4] = "on";
    int rc, fd;

    // it's already on
    if(aries_ipc_is_modem_enabled())
        return 0;

    fd = open("/sys/class/modemctl/xmm/control", O_RDWR);
    if(fd < 0)
       return -1;

    rc = write(fd, power_data, 2);

    close(fd);

    if(rc < 0)
        return -1;

    return 0;
}

static int
aries_ipc_power_off(void *data)
{
    char power_data[5] = "off";
    int rc, fd;

    // it's already off
    if(!aries_ipc_is_modem_enabled())
        return 0;

    fd = open("/sys/class/modemctl/xmm/control", O_RDWR);
    if(fd < 0)
        return -1;

    rc = write(fd, power_data, 3);

    close(fd);

    if(rc < 0)
        return -1;

    return 0;
}

static void*
aries_ipc_common_data_create(void)
{
    struct aries_ipc_handlers_common_data *common_data;
    void *io_data;
    int io_data_len;
    int spn_len;

    io_data_len = sizeof(struct aries_ipc_handlers_common_data);
    io_data = malloc(io_data_len);

    if(io_data == NULL)
        return NULL;

    memset(io_data, 0, io_data_len);

    common_data = (struct aries_ipc_handlers_common_data *) io_data;

    spn_len = sizeof(struct sockaddr_pn);
    common_data->spn = malloc(spn_len);

    if(common_data == NULL)
        return NULL;

    memset(common_data->spn, 0, spn_len);

    return io_data;
}

static int
aries_ipc_common_data_destroy(void *io_data)
{
    struct aries_ipc_handlers_common_data *common_data;

    // This was already done, not an error but we need to return
    if(io_data == NULL)
        return 0;

    common_data = (struct aries_ipc_handlers_common_data *) io_data;

    if(common_data->spn != NULL)
        free(common_data->spn);

    free(io_data);

    return 0;
}

static int
aries_ipc_common_data_set_fd(void *io_data, int fd)
{
    struct aries_ipc_handlers_common_data *common_data;

    if(io_data == NULL)
        return -1;

    common_data = (struct aries_ipc_handlers_common_data *) io_data;
    common_data->fd = fd;

    return 0;
}

static int
aries_ipc_common_data_get_fd(void *io_data)
{
    struct aries_ipc_handlers_common_data *common_data;

    if(io_data == NULL)
        return -1;

    common_data = (struct aries_ipc_handlers_common_data *) io_data;

    return common_data->fd;
}

static struct ipc_handlers aries_default_handlers = {
    .read = aries_ipc_read,
    .write = aries_ipc_write,
    .open = aries_ipc_open,
    .close = aries_ipc_close,
    .power_on = aries_ipc_power_on,
    .power_off = aries_ipc_power_off,
    .common_data = NULL,
    .common_data_create = aries_ipc_common_data_create,
    .common_data_destroy = aries_ipc_common_data_destroy,
    .common_data_set_fd = aries_ipc_common_data_set_fd,
    .common_data_get_fd = aries_ipc_common_data_get_fd,
};

static struct ipc_ops aries_fmt_ops = {
    .send = aries_ipc_fmt_client_send,
    .recv = aries_ipc_fmt_client_recv,
    .bootstrap = aries_modem_bootstrap,
};

static struct ipc_ops aries_rfs_ops = {
    .send = aries_ipc_rfs_client_send,
    .recv = aries_ipc_rfs_client_recv,
    .bootstrap = NULL,
};

struct ipc_device_desc ipc_device = {
    .name = "aries",
    .board_name = "aries",
    .handlers = &aries_default_handlers,
    .fmt_ops = &aries_fmt_ops,
    .rfs_ops = &aries_rfs_ops,
};

// vim:ts=4:sw=4:expandtab
