
#include <stdio.h>
#include <errno.h>

#define RETURN_IF(return_value)                                      \
    if (return_value < 0) {                                          \
        printf("%s::%d fail. errno: %s\n",                           \
             __func__, __LINE__, strerror(errno));                   \
        return return_value;                                         \
    }

#define CHROOT_PATH         "/system/bin/chroot"
#define MKDIR_PATH          "/system/bin/mkdir"
#define MOUNT_PATH          "/system/bin/mount"
#define UMOUNT_PATH         "/system/bin/umount"

char *CHROOT_CMD[]  = {CHROOT_PATH, "/mnt/sdboot", /*"/init",*/ (char *) 0 };
char *MKDIR_CMD[]   = {MKDIR_PATH, "/mnt/chroot", (char *) 0 };
char *MOUNT_CMD[]   = {MOUNT_PATH, "/dev/block/mmcblk1p2", "/mnt/chroot", (char *) 0 };

int main(int argc, char** argv) {
    int ret;

    printf("creating mnt directory\n");
    ret = execve(MKDIR_PATH, MKDIR_CMD);
    RETURN_IF(ret);

    printf("chrooting into mnt directory\n");
    ret = execve(CHROOT_PATH, CHROOT_CMD);
    RETURN_IF(ret);

    printf("everything ok\n");
    return 0;
}
