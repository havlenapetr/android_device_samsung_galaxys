#!/bin/sh

# Copyright (C) 2010 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

DEVICE=galaxys
MANUFACTURER=samsung

mkdir -p ../../../vendor/$MANUFACTURER/$DEVICE/proprietary

# galaxys
adb pull /system/etc/gps.conf ../../../vendor/$MANUFACTURER/$DEVICE/proprietary
adb pull /system/vendor/bin/gpsd ../../../vendor/$MANUFACTURER/$DEVICE/proprietary
chmod 755 ../../../vendor/$MANUFACTURER/$DEVICE/proprietary/gpsd
adb pull /system/vendor/etc/gps.xml ../../../vendor/$MANUFACTURER/$DEVICE/proprietary
adb pull /system/vendor/lib/hw/gps.aries.so ../../../vendor/$MANUFACTURER/$DEVICE/proprietary
adb pull /system/lib/libril.so ../../../vendor/$MANUFACTURER/$DEVICE/proprietary
adb pull /system/lib/libsecril-client.so ../../../vendor/$MANUFACTURER/$DEVICE/proprietary
adb pull /system/lib/libsec-ril.so ../../../vendor/$MANUFACTURER/$DEVICE/proprietary
adb pull /system/bin/rild ../../../vendor/$MANUFACTURER/$DEVICE/proprietary
chmod 755 ../../../vendor/$MANUFACTURER/$DEVICE/proprietary/rild
adb pull /radio/modem.bin ../../../vendor/$MANUFACTURER/$DEVICE/proprietary

adb pull /system/vendor/bin/pvrsrvinit ../../../vendor/$MANUFACTURER/$DEVICE/proprietary
chmod 755 ../../../vendor/$MANUFACTURER/$DEVICE/proprietary/pvrsrvinit
adb pull /system/vendor/firmware/bcm4329.hcd ../../../vendor/$MANUFACTURER/$DEVICE/proprietary
adb pull /system/vendor/firmware/nvram_net.txt ../../../vendor/$MANUFACTURER/$DEVICE/proprietary
adb pull /system/vendor/firmware/cypress-touchkey.bin ../../../vendor/$MANUFACTURER/$DEVICE/proprietary
adb pull /system/vendor/firmware/samsung_mfc_fw.bin ../../../vendor/$MANUFACTURER/$DEVICE/proprietary
adb pull /system/vendor/lib/egl/libEGL_POWERVR_SGX540_120.so ../../../vendor/$MANUFACTURER/$DEVICE/proprietary
adb pull /system/vendor/lib/egl/libGLESv1_CM_POWERVR_SGX540_120.so ../../../vendor/$MANUFACTURER/$DEVICE/proprietary
adb pull /system/vendor/lib/egl/libGLESv2_POWERVR_SGX540_120.so ../../../vendor/$MANUFACTURER/$DEVICE/proprietary
adb pull /system/vendor/lib/hw/gralloc.aries.so ../../../vendor/$MANUFACTURER/$DEVICE/proprietary
adb pull /system/vendor/lib/libakm.so ../../../vendor/$MANUFACTURER/$DEVICE/proprietary
adb pull /system/vendor/lib/libglslcompiler.so ../../../vendor/$MANUFACTURER/$DEVICE/proprietary
adb pull /system/vendor/lib/libIMGegl.so ../../../vendor/$MANUFACTURER/$DEVICE/proprietary
adb pull /system/vendor/lib/libpvr2d.so ../../../vendor/$MANUFACTURER/$DEVICE/proprietary
adb pull /system/vendor/lib/libpvrANDROID_WSEGL.so ../../../vendor/$MANUFACTURER/$DEVICE/proprietary
adb pull /system/vendor/lib/libPVRScopeServices.so ../../../vendor/$MANUFACTURER/$DEVICE/proprietary
adb pull /system/vendor/lib/libsrv_init.so ../../../vendor/$MANUFACTURER/$DEVICE/proprietary
adb pull /system/vendor/lib/libsrv_um.so ../../../vendor/$MANUFACTURER/$DEVICE/proprietary
adb pull /system/vendor/firmware/CE147F02.bin ../../../vendor/$MANUFACTURER/$DEVICE/proprietary
adb pull /system/vendor/lib/libusc.so ../../../vendor/$MANUFACTURER/$DEVICE/proprietary
adb pull /system/vendor/bin/orientationd ../../../vendor/$MANUFACTURER/$DEVICE/proprietary
chmod 755 ../../../vendor/$MANUFACTURER/$DEVICE/proprietary/orientationd
adb pull /system/vendor/bin/geomagneticd ../../../vendor/$MANUFACTURER/$DEVICE/proprietary
chmod 755 ../../../vendor/$MANUFACTURER/$DEVICE/proprietary/geomagneticd
adb pull /system/lib/libActionShot.so ../../../vendor/$MANUFACTURER/$DEVICE/proprietary
adb pull /system/lib/libarccamera.so ../../../vendor/$MANUFACTURER/$DEVICE/proprietary
adb pull /system/lib/libcamerafirmwarejni.so ../../../vendor/$MANUFACTURER/$DEVICE/proprietary
adb pull /system/lib/libCaMotion.so ../../../vendor/$MANUFACTURER/$DEVICE/proprietary
adb pull /system/lib/libcaps.so ../../../vendor/$MANUFACTURER/$DEVICE/proprietary
adb pull /system/lib/libPanoraMax1.so ../../../vendor/$MANUFACTURER/$DEVICE/proprietary
adb pull /system/lib/libPlusMe.so ../../../vendor/$MANUFACTURER/$DEVICE/proprietary
adb pull /system/lib/libs3cjpeg.so ../../../vendor/$MANUFACTURER/$DEVICE/proprietary
adb pull /system/lib/libseccamera.so ../../../vendor/$MANUFACTURER/$DEVICE/proprietary
adb pull /system/lib/libseccameraadaptor.so ../../../vendor/$MANUFACTURER/$DEVICE/proprietary
adb pull /system/lib/libsecjpegencoder.so ../../../vendor/$MANUFACTURER/$DEVICE/proprietary
adb pull /system/lib/libtvout.so ../../../vendor/$MANUFACTURER/$DEVICE/proprietary
adb pull /system/lib/lib_tvoutengine.so ../../../vendor/$MANUFACTURER/$DEVICE/proprietary
adb pull /system/lib/libtvoutfimc.so ../../../vendor/$MANUFACTURER/$DEVICE/proprietary
adb pull /system/lib/libtvouthdmi.so ../../../vendor/$MANUFACTURER/$DEVICE/proprietary
adb pull /system/lib/libtvoutservice.so ../../../vendor/$MANUFACTURER/$DEVICE/proprietary
adb pull /system/bin/tvoutserver ../../../vendor/$MANUFACTURER/$DEVICE/proprietary
chmod 755 ../../../vendor/$MANUFACTURER/$DEVICE/proprietary/tvoutserver
adb pull /system/cameradata/datapattern_420sp.yuv ../../../vendor/$MANUFACTURER/$DEVICE/proprietary
adb pull /system/cameradata/datapattern_front_420sp.yuv ../../../vendor/$MANUFACTURER/$DEVICE/proprietary
adb pull /system/firmware/CE147F00.bin ../../../vendor/$MANUFACTURER/$DEVICE/proprietary
adb pull /system/firmware/CE147F01.bin ../../../vendor/$MANUFACTURER/$DEVICE/proprietary
adb pull /system/firmware/CE147F02.bin ../../../vendor/$MANUFACTURER/$DEVICE/proprietary
adb pull /system/firmware/CE147F03.bin ../../../vendor/$MANUFACTURER/$DEVICE/proprietary


(cat << EOF) | sed s/__DEVICE__/$DEVICE/g | sed s/__MANUFACTURER__/$MANUFACTURER/g > ../../../vendor/$MANUFACTURER/$DEVICE/device-vendor-blobs.mk
# Copyright (C) 2010 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Prebuilt libraries that are needed to build open-source libraries
PRODUCT_COPY_FILES := \\
    vendor/__MANUFACTURER__/__DEVICE__/proprietary/libril.so:obj/lib/libril.so \\
    vendor/__MANUFACTURER__/__DEVICE__/proprietary/libsecril-client.so:obj/lib/libsecril-client.so


# All the blobs necessary for galaxys devices
PRODUCT_COPY_FILES += \\
    vendor/__MANUFACTURER__/__DEVICE__/proprietary/gps.conf:system/etc/gps.conf \\
    vendor/__MANUFACTURER__/__DEVICE__/proprietary/gpsd:system/vendor/bin/gpsd \\
    vendor/__MANUFACTURER__/__DEVICE__/proprietary/gps.xml:system/vendor/etc/gps.xml \\
    vendor/__MANUFACTURER__/__DEVICE__/proprietary/gps.aries.so:system/vendor/lib/hw/gps.aries.so \\
    vendor/__MANUFACTURER__/__DEVICE__/proprietary/libril.so:system/lib/libril.so \\
    vendor/__MANUFACTURER__/__DEVICE__/proprietary/libsecril-client.so:system/lib/libsecril-client.so \\
    vendor/__MANUFACTURER__/__DEVICE__/proprietary/libsec-ril.so:system/lib/libsec-ril.so \\
    vendor/__MANUFACTURER__/__DEVICE__/proprietary/rild:system/bin/rild
    
# All the blobs necessary for galaxys devices
PRODUCT_COPY_FILES += \\
    vendor/__MANUFACTURER__/__DEVICE__/proprietary/pvrsrvinit:system/vendor/bin/pvrsrvinit \\
    vendor/__MANUFACTURER__/__DEVICE__/proprietary/bcm4329.hcd:system/vendor/firmware/bcm4329.hcd \\
    vendor/__MANUFACTURER__/__DEVICE__/proprietary/nvram_net.txt:system/vendor/firmware/nvram_net.txt \\
    vendor/__MANUFACTURER__/__DEVICE__/proprietary/cypress-touchkey.bin:system/vendor/firmware/cypress-touchkey.bin \\
    vendor/__MANUFACTURER__/__DEVICE__/proprietary/samsung_mfc_fw.bin:system/vendor/firmware/samsung_mfc_fw.bin \\
    vendor/__MANUFACTURER__/__DEVICE__/proprietary/libEGL_POWERVR_SGX540_120.so:system/vendor/lib/egl/libEGL_POWERVR_SGX540_120.so \\
    vendor/__MANUFACTURER__/__DEVICE__/proprietary/libGLESv1_CM_POWERVR_SGX540_120.so:system/vendor/lib/egl/libGLESv1_CM_POWERVR_SGX540_120.so \\
    vendor/__MANUFACTURER__/__DEVICE__/proprietary/libGLESv2_POWERVR_SGX540_120.so:system/vendor/lib/egl/libGLESv2_POWERVR_SGX540_120.so \\
    vendor/__MANUFACTURER__/__DEVICE__/proprietary/gralloc.aries.so:system/vendor/lib/hw/gralloc.aries.so \\
    vendor/__MANUFACTURER__/__DEVICE__/proprietary/libakm.so:system/vendor/lib/libakm.so \\
    vendor/__MANUFACTURER__/__DEVICE__/proprietary/libglslcompiler.so:system/vendor/lib/libglslcompiler.so \\
    vendor/__MANUFACTURER__/__DEVICE__/proprietary/libIMGegl.so:system/vendor/lib/libIMGegl.so \\
    vendor/__MANUFACTURER__/__DEVICE__/proprietary/libpvr2d.so:system/vendor/lib/libpvr2d.so \\
    vendor/__MANUFACTURER__/__DEVICE__/proprietary/libpvrANDROID_WSEGL.so:system/vendor/lib/libpvrANDROID_WSEGL.so \\
    vendor/__MANUFACTURER__/__DEVICE__/proprietary/libPVRScopeServices.so:system/vendor/lib/libPVRScopeServices.so \\
    vendor/__MANUFACTURER__/__DEVICE__/proprietary/libsrv_init.so:system/vendor/lib/libsrv_init.so \\
    vendor/__MANUFACTURER__/__DEVICE__/proprietary/libsrv_um.so:system/vendor/lib/libsrv_um.so \\
    vendor/__MANUFACTURER__/__DEVICE__/proprietary/CE147F02.bin:system/vendor/firmware/CE147F02.bin \\
    vendor/__MANUFACTURER__/__DEVICE__/proprietary/libusc.so:system/vendor/lib/libusc.so \\
    vendor/__MANUFACTURER__/__DEVICE__/proprietary/orientationd:system/vendor/bin/orientationd \\
    vendor/__MANUFACTURER__/__DEVICE__/proprietary/geomagneticd:system/vendor/bin/geomagneticd

# camera and tvout
PRODUCT_COPY_FILES += \\
    vendor/__MANUFACTURER__/__DEVICE__/proprietary/libActionShot.so:system/lib/libActionShot.so \\
    vendor/__MANUFACTURER__/__DEVICE__/proprietary/libarccamera.so:system/lib/libarccamera.so \\
    vendor/__MANUFACTURER__/__DEVICE__/proprietary/libcamerafirmwarejni.so:system/lib/libcamerafirmwarejni.so \\
    vendor/__MANUFACTURER__/__DEVICE__/proprietary/libCaMotion.so:system/lib/libCaMotion.so \\
    vendor/__MANUFACTURER__/__DEVICE__/proprietary/libcaps.so:system/lib/libcaps.so \\
    vendor/__MANUFACTURER__/__DEVICE__/proprietary/libPanoraMax1.so:system/lib/libPanoraMax1.so \\
    vendor/__MANUFACTURER__/__DEVICE__/proprietary/libPlusMe.so:system/lib/libPlusMe.so \\
    vendor/__MANUFACTURER__/__DEVICE__/proprietary/libs3cjpeg.so:system/lib/libs3cjpeg.so \\
    vendor/__MANUFACTURER__/__DEVICE__/proprietary/libseccamera.so:system/lib/libseccamera.so \\
    vendor/__MANUFACTURER__/__DEVICE__/proprietary/libseccameraadaptor.so:system/lib/libseccameraadaptor.so \\
    vendor/__MANUFACTURER__/__DEVICE__/proprietary/libsecjpegencoder.so:system/lib/libsecjpegencoder.so \\
    vendor/__MANUFACTURER__/__DEVICE__/proprietary/libtvout.so:system/lib/libtvout.so \\
    vendor/__MANUFACTURER__/__DEVICE__/proprietary/lib_tvoutengine.so:system/lib/lib_tvoutengine.so \\
    vendor/__MANUFACTURER__/__DEVICE__/proprietary/libtvoutfimc.so:system/lib/libtvoutfimc.so \\
    vendor/__MANUFACTURER__/__DEVICE__/proprietary/libtvouthdmi.so:system/lib/libtvouthdmi.so \\
    vendor/__MANUFACTURER__/__DEVICE__/proprietary/libtvoutservice.so:system/lib/libtvoutservice.so \\
    vendor/__MANUFACTURER__/__DEVICE__/proprietary/tvoutserver:system/bin/tvoutserver \\
    vendor/__MANUFACTURER__/__DEVICE__/proprietary/datapattern_420sp.yuv:system/cameradata/datapattern_420sp.yuv \\
    vendor/__MANUFACTURER__/__DEVICE__/proprietary/datapattern_front_420sp.yuv:system/cameradata/datapattern_front_420sp.yuv \\
    vendor/__MANUFACTURER__/__DEVICE__/proprietary/CE147F00.bin:system/firmware/CE147F00.bin \\
    vendor/__MANUFACTURER__/__DEVICE__/proprietary/CE147F01.bin:system/firmware/CE147F01.bin \\
    vendor/__MANUFACTURER__/__DEVICE__/proprietary/CE147F02.bin:system/firmware/CE147F02.bin \\
    vendor/__MANUFACTURER__/__DEVICE__/proprietary/CE147F03.bin:system/firmware/CE147F03.bin

EOF

./setup-makefiles.sh
