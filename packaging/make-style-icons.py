#!/usr/bin/env python3
"""Draws the few marks the bundled style sheets need.

A Qt style sheet cannot draw a triangle: the border trick every CSS page shows
ends up as a filled rectangle, and there is no way to recolour an existing
image from a rule. So the arrows and the check mark come from the application
resources instead, in two tints - a dark one for light styles, a light one for
dark ones.

    python packaging/make-style-icons.py

Writes images/styles/*.png, listed in xpeccy.qrc as :/images/styles/*.png.
Rerun it after changing a shape or a tint; nothing in the build calls it.
"""

import os

from PIL import Image, ImageDraw

SIZE = 16			# subcontrol box the style sheets ask for
SS = 4				# supersampling, the shapes are drawn at 4x and shrunk
TINTS = {
	"dark": (74, 74, 74),		# on the light style
	"light": (216, 216, 216),	# on the dark ones
}

OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "images", "styles")


def chevron(draw, up, color, w):
	"""a v (or ^) with round joints, like the one the desktop draws"""
	x0, xm, x1 = 4.0, 8.0, 12.0
	y0, y1 = (9.5, 6.0) if up else (6.5, 10.0)
	pts = [(x0, y0), (xm, y1), (x1, y0)]
	draw.line([(x * SS, y * SS) for x, y in pts], fill=color, width=w, joint="curve")
	for x, y in pts:		# round the ends off
		r = w / 2.0 - 0.5
		draw.ellipse([x * SS - r, y * SS - r, x * SS + r, y * SS + r], fill=color)


def check(draw, color, w):
	pts = [(3.5, 8.5), (6.5, 11.5), (12.5, 4.5)]
	draw.line([(x * SS, y * SS) for x, y in pts], fill=color, width=w, joint="curve")
	for x, y in pts:
		r = w / 2.0 - 0.5
		draw.ellipse([x * SS - r, y * SS - r, x * SS + r, y * SS + r], fill=color)


def save(name, painter, color):
	im = Image.new("RGBA", (SIZE * SS, SIZE * SS), (0, 0, 0, 0))
	painter(ImageDraw.Draw(im), color + (255,), int(1.6 * SS))
	im = im.resize((SIZE, SIZE), Image.LANCZOS)
	path = os.path.join(OUT, name)
	im.save(path)
	print(path)


def main():
	if not os.path.isdir(OUT):
		os.makedirs(OUT)
	for tint, color in TINTS.items():
		save("arrow-down-%s.png" % tint, lambda d, c, w: chevron(d, False, c, w), color)
		save("arrow-up-%s.png" % tint, lambda d, c, w: chevron(d, True, c, w), color)
		save("check-%s.png" % tint, check, color)


main()
