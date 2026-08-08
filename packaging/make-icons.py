#!/usr/bin/env python3
# Build the application icons from the hand drawn PNG set.
#
#	python packaging/make-icons.py <source-dir>
#
# <source-dir> must hold xpeccy-plus-<size>.png for every size in SIZES,
# each one RGBA with a transparent background. The script writes:
#
#	images/icons/xpeccy-plus-<size>.png	Qt resource set (window icon)
#	images/xpeccy-plus.png			128x128, Linux hicolor + AppImage
#	images/xpeccy-plus.ico			Windows exe resource
#	images/xpeccy-plus.icns			macOS bundle
#
# Only Pillow is needed: the .ico and .icns containers are written by hand so
# the per size artwork is kept as drawn instead of being rescaled from one
# master image.

import os
import struct
import sys
from io import BytesIO

from PIL import Image

NAME = "xpeccy-plus"
SIZES = [16, 24, 32, 48, 64, 128, 256, 512]
# sizes that go into the Qt resource. 512 is left out: it would be dead
# weight in the binary, Windows and macOS take it from .ico/.icns instead
QRC_SIZES = [16, 24, 32, 48, 64, 128, 256]
# sizes Windows looks for; 256 is stored as PNG, the rest as BMP
ICO_SIZES = [16, 24, 32, 48, 64, 128, 256]
# icns element type -> pixel size, the same set iconutil produces
ICNS_TYPES = [
	(b"icp4", 16),		# 16x16
	(b"ic11", 32),		# 16x16@2x
	(b"icp5", 32),		# 32x32
	(b"ic12", 64),		# 32x32@2x
	(b"ic07", 128),		# 128x128
	(b"ic13", 256),		# 128x128@2x
	(b"ic08", 256),		# 256x256
	(b"ic14", 512),		# 256x256@2x
	(b"ic09", 512),		# 512x512
]


def png_bytes(im):
	buf = BytesIO()
	im.save(buf, "png", optimize=True)
	return buf.getvalue()


def bmp_bytes(im):
	# ICO stores a BITMAPINFOHEADER with a doubled height, bottom up BGRA
	# pixels and an AND mask. The mask is unused for 32bpp images but must
	# be there, padded to 4 bytes per row.
	w, h = im.size
	hdr = struct.pack("<IiiHHIIiiII", 40, w, h * 2, 1, 32, 0, 0, 0, 0, 0, 0)
	pix = bytearray()
	for y in range(h - 1, -1, -1):
		for x in range(w):
			r, g, b, a = im.getpixel((x, y))
			pix += bytes((b, g, r, a))
	row = ((w + 31) // 32) * 4
	return hdr + bytes(pix) + bytes(row * h)


def write_ico(images, path):
	entries = []
	blobs = []
	for size in ICO_SIZES:
		im = images[size]
		blob = png_bytes(im) if size >= 256 else bmp_bytes(im)
		entries.append((size, blob))
		blobs.append(blob)
	off = 6 + 16 * len(entries)
	out = struct.pack("<HHH", 0, 1, len(entries))
	for size, blob in entries:
		# 256 is written as 0 in the directory
		out += struct.pack("<BBBBHHII", size & 0xff, size & 0xff, 0, 0,
				   1, 32, len(blob), off)
		off += len(blob)
	with open(path, "wb") as f:
		f.write(out + b"".join(blobs))


def write_icns(images, path):
	body = b""
	for tag, size in ICNS_TYPES:
		blob = png_bytes(images[size])
		body += tag + struct.pack(">I", len(blob) + 8) + blob
	with open(path, "wb") as f:
		f.write(b"icns" + struct.pack(">I", len(body) + 8) + body)


def main():
	if len(sys.argv) != 2:
		sys.exit("usage: make-icons.py <source-dir>")
	src = sys.argv[1]
	root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
	dst = os.path.join(root, "images")

	images = {}
	for size in SIZES:
		name = os.path.join(src, "%s-%d.png" % (NAME, size))
		im = Image.open(name).convert("RGBA")
		if im.size != (size, size):
			sys.exit("%s is %dx%d, expected %dx%d"
				 % (name, im.size[0], im.size[1], size, size))
		images[size] = im

	# every size lands on disk: Linux installs them into hicolor, where the
	# desktop resolves Icon= from the .desktop file
	os.makedirs(os.path.join(dst, "icons"), exist_ok=True)
	for size in SIZES:
		images[size].save(os.path.join(dst, "icons", "%s-%d.png" % (NAME, size)),
				  "png", optimize=True)
	images[128].save(os.path.join(dst, "%s.png" % NAME), "png", optimize=True)
	write_ico(images, os.path.join(dst, "%s.ico" % NAME))
	write_icns(images, os.path.join(dst, "%s.icns" % NAME))
	print("icons written to", dst)


if __name__ == "__main__":
	main()
