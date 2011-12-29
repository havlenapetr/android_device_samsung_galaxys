/*
**
** Copyright 2011, Havlena Petr <havlenapetr@gmail.com>
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

#ifndef ANDROID_HARDWARE_CAMERA_SEC_PARAMETERS_H
#define ANDROID_HARDWARE_CAMERA_SEC_PARAMETERS_H

#include <camera/CameraParameters.h>

#include <stdlib.h>

namespace android {

struct SecCameraArea {
    int left;
    int top;
    int right;
    int bottom;
    int weight;

    SecCameraArea() {
        left = top = right = bottom = weight = 0;
    }

    SecCameraArea(int l, int t, int r, int b, int w) {
        left = l;
        top = t;
        right = r;
        bottom = b;
        weight = w;
    }

    SecCameraArea(const char* str) {
        char* end;
        bool ok;

        ok = false;
        if (str != NULL && str[0] == '(') {
            left = (int)strtol(str+1, &end, 10);
            if (*end != ',') goto dummy;
            top = (int)strtol(end+1, &end, 10);
            if (*end != ',') goto dummy;
            right = (int)strtol(end+1, &end, 10);
            if (*end != ',') goto dummy;
            bottom = (int)strtol(end+1, &end, 10);
            if (*end != ',') goto dummy;
            weight = (int)strtol(end+1, &end, 10);
            if (*end != ')') goto dummy;
            ok = true;
        }

    dummy:
        if (!ok) {
            left = top = right = bottom = weight = 0;
        }
    }

    void getXY(int* x, int* y) {
        int diffY = (top - bottom) / 2;
        int diffX = (right - left) / 2;

        *y = bottom + diffY + 1000;
        *x = left + diffX + 1000;
    }

    bool isDummy() {
        return left == 0 && top == 0 && right == 0 && bottom == 0;
    }

    String8 toString8() {
        return String8::format("(%i,%i,%i,%i,%i)",
                               left, top, right, bottom, weight);
    }
};

class SecCameraParameters : public CameraParameters
{
public:
     SecCameraParameters();
     SecCameraParameters(const String8 &params);

     static const char KEY_ISO[];
     static const char KEY_SUPPORTED_ISO_MODES[];

     static const char KEY_CONTRAST[];
     static const char KEY_MAX_CONTRAST[];
     static const char KEY_MIN_CONTRAST[];
     static const char KEY_CONTRAST_STEP[];

     static const char EFFECT_ANTIQUE[];
     static const char EFFECT_SHARPEN[];

     // Values for ISO settings.
     static const char ISO_AUTO[];
     static const char ISO_50[];
     static const char ISO_100[];
     static const char ISO_200[];
     static const char ISO_400[];
     static const char ISO_800[];
     static const char ISO_1600[];
     static const char ISO_SPORTS[];
     static const char ISO_NIGHT[];
     static const char ISO_MOVIE[];
};

}; // namespace android

#endif // ANDROID_HARDWARE_CAMERA_SEC_PARAMETERS_H
