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

#include "SecCameraParameters.h"

namespace android {

const char SecCameraParameters::FOCUS_MODE_FACEDETECT[] = "facedetect";

const char SecCameraParameters::KEY_ISO[] = "iso";
const char SecCameraParameters::KEY_SUPPORTED_ISO_MODES[] = "iso-values";

const char SecCameraParameters::KEY_CONTRAST[] = "contrast";
const char SecCameraParameters::KEY_MAX_CONTRAST[] = "max-contrast";
const char SecCameraParameters::KEY_MIN_CONTRAST[] = "min-contrast";
const char SecCameraParameters::KEY_CONTRAST_STEP[] = "contrast-step";

// Values for effect settings.
const char SecCameraParameters::EFFECT_ANTIQUE[] = "antique";
const char SecCameraParameters::EFFECT_SHARPEN[] = "sharpen";

// Values for ISO settings.
const char SecCameraParameters::ISO_AUTO[] = "auto";
const char SecCameraParameters::ISO_50[] = "50";
const char SecCameraParameters::ISO_100[] = "100";
const char SecCameraParameters::ISO_200[] = "200";
const char SecCameraParameters::ISO_400[] = "400";
const char SecCameraParameters::ISO_800[] = "800";
const char SecCameraParameters::ISO_1600[] = "1600";
const char SecCameraParameters::ISO_SPORTS[] = "sports";
const char SecCameraParameters::ISO_NIGHT[] = "night";
const char SecCameraParameters::ISO_MOVIE[] = "movie";

SecCameraParameters::SecCameraParameters() : CameraParameters()
{
}

SecCameraParameters::SecCameraParameters(const String8 &params) : CameraParameters(params)
{
}

}; // namespace android
