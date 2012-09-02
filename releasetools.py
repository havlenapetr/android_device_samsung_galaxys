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

import re
import common

def FindRadio(zipfile):
  matches = []
  for name in zipfile.namelist():
    if re.match(r"^RADIO/radio[.](.+[.])?img$", name):
      matches.append(name)
  if len(matches) > 1:
    raise ValueError("multiple radio images in target-files zip!")
  if matches:
    print "using %s as modem.bin" % (matches[0],)
    return zipfile.read(matches[0])
  else:
    return None

def FullOTA_InstallEnd(info):
  radio_img = FindRadio(info.input_zip)
  if not radio_img:
    print "no modem.bin in target_files; skipping install"
  else:
    common.ZipWriteStr(info.output_zip, "modem.bin", radio_img)
    info.script.Print("Writing radio...")
    info.script.WriteRawImage("/radio", "modem.bin")

def IncrementalOTA_InstallEnd(info):
  tf = FindRadio(info.target_zip)
  if not tf:
    # failed to read TARGET radio image: don't include any radio in update.
    print "no modem.bin in target target_files; skipping install"
  else:
    tf = common.File("modem.bin", tf)

    sf = FindRadio(info.source_zip)
    if not sf:
      # failed to read SOURCE radio image: include the whole target
      # radio image.
      tf.AddToZip(info.output_zip)
      info.script.Print("Writing radio...")
      info.script.WriteRawImage("/radio", tf.name)
    else:
      sf = common.File("modem.bin", sf)

      if tf.sha1 == sf.sha1:
        print "radio image unchanged; skipping"
      else:
        diff = common.Difference(tf, sf)
        common.ComputeDifferences([diff])
        _, _, d = diff.GetPatch()
        if d is None or len(d) > tf.size * common.OPTIONS.patch_threshold:
          # computing difference failed, or difference is nearly as
          # big as the target:  simply send the target.
          tf.AddToZip(info.output_zip)
          info.script.Print("Writing radio...")
          info.script.WriteRawImage("radio", tf.name)
        else:
          common.ZipWriteStr(info.output_zip, "modem.bin.p", d)
          info.script.Print("Patching radio...")
          radio_type, radio_device = common.GetTypeAndDevice(
              "/radio", info.info_dict)
          info.script.ApplyPatch(
              "%s:%s:%d:%s:%d:%s" % (radio_type, radio_device,
                                     sf.size, sf.sha1, tf.size, tf.sha1),
              "-", tf.size, tf.sha1, sf.sha1, "modem.bin.p")

def FullOTA_Assertions(info):
  devices = ["aries", "galaxys", "GT-I9000", "GT-I9000M", "GT-I9000T"]
  info.script.AssertDevices(devices)
  return True

def FullOTA_WriteBootimg(info):
  # write boot.img with start_block
  info.script.WriteRawImage("/boot", "boot.img", 72)
  return True
