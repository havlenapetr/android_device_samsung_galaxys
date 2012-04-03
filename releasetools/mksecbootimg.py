#!/usr/bin/env python
import os
import sys
import getopt

class Options(object): pass
OPTIONS = Options()
OPTIONS.out_file = None
OPTIONS.kernel = None
OPTIONS.ramdisk = None
OPTIONS.cmdline = None
OPTIONS.recovery = None
OPTIONS.base = None
OPTIONS.pagesize = None

def copydata(outfile, infile):
  while 1:
    data = infile.read(512)
    if (data):
      outfile.write(data)
    else:
      break

def alignoffset(outfile):
  offset = outfile.tell()
  outfile.seek((offset + 511) & ~511)
  return outfile.tell()

def appendimage(outfile, infile):
  offset = alignoffset(outfile)
  copydata(outfile, infile)
  length = alignoffset(outfile) - offset
  assert (offset % 512 == 0)
  assert (length % 512 == 0)
  return (offset/512, length/512)

def main(argv):
  try:
    opts, args = getopt.getopt(argv, "o", ["kernel=", "ramdisk=", "output=", "cmdline=", "base=", "pagesize="])
  except getopt.GetoptError, err:
    print str(err) # will print something like "option -a not recognized"
    sys.exit(2)

  for o, a in opts:
    if o == "--kernel":
      OPTIONS.kernel = open(a, 'r')
    elif o == "--ramdisk":
      OPTIONS.ramdisk = open(a, 'r')
    elif o in ("-o", "--output"):
      OPTIONS.out_file = open(a, 'wb')
    elif o == "--cmdline":
      OPTIONS.cmdline = a
    elif o == "--base":
      OPTIONS.base = a
    elif o == "--pagesize":
      OPTIONS.pagesize = a
    else:
      assert False, "unhandled option"

  offset_table = "\n\nBOOT_IMAGE_OFFSETS\n"
  copydata(OPTIONS.out_file, OPTIONS.kernel)
  table_loc = alignoffset(OPTIONS.out_file)
  OPTIONS.out_file.write('\x00' * 512)
  offset_table += "boot_offset=%d;boot_len=%d;" % appendimage(OPTIONS.out_file, OPTIONS.ramdisk)
  #if recovery:
  #  offset_table += "recovery_offset=%d;recovery_len=%d;" % appendimage(OPTIONS.out_file, recovery)
  offset_table += "\n\n"

  OPTIONS.out_file.seek(table_loc)
  OPTIONS.out_file.write(offset_table)
  OPTIONS.out_file.flush()
  os.fsync(OPTIONS.out_file.fileno())
  OPTIONS.out_file.close()

if __name__ == '__main__':
  try:
    main(sys.argv[1:])
  except getopt.error, e:
    print
    print "   ERROR: %s" % (e,)
    print
    sys.exit(1)
