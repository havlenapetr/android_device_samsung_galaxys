# Copyright (C) 2012 The Android Open Source Project
# Copyright (C) 2012 Havlena Petr <havlenapetr@gmail.com>
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

"""Emit commands needed for Galaxys during OTA installation
(installing boot image and modem firmware)."""

import os
import common

def FullOTA_Assertions(info):
  devices = ["aries", "galaxys", "GT-I9000", "GT-I9000M", "GT-I9000T"]
  info.script.AssertDevices(devices)

  info.script.UnpackPackageDir("firmware", "/tmp")
  info.script.SetPermissions("/tmp/modem.bin", 0, 0, 0644)
  return True

def FullOTA_WriteBootimg(info):
  out_path = os.getenv('OUT')

  info.output_zip.write(os.path.join(out_path, "modem.bin"), "firmware/modem.bin")

  # write boot.img with start_block
  info.script.WriteRawImage("/boot", "boot.img", 72)
  return True
