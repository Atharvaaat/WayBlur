#!/usr/bin/env python3
"""Render a synthetic 'desktop' with PII and harmless text rows to a binary PGM.

Usage: make_fixture.py OUT.pgm [FONT.ttf]
Needs Pillow. Row layout must match tests/pipeline_pii_test.cpp.
"""
import sys
from PIL import Image, ImageDraw, ImageFont

out = sys.argv[1]
font_path = sys.argv[2] if len(sys.argv) > 2 else None
font = ImageFont.truetype(font_path, 22) if font_path else ImageFont.load_default(size=22)

img = Image.new("L", (1280, 720), 245)
d = ImageDraw.Draw(img)
rows = [
    (40, "Inbox - 3 unread messages from the team"),
    (120, "From: jane.doe@example.com"),
    (200, "Card: 4111 1111 1111 1111   Exp 09/28"),
    (280, "Call me on +91 98765 43210 tomorrow"),
    (360, "Deploy key: AKIAIOSFODNN7EXAMPLE"),
    (440, "Meeting at 10:30 in room 42, version 3.14"),
    (520, "PAN: ABCDE1234F"),
]
for y, text in rows:
    d.text((40, y), text, fill=20, font=font)

with open(out, "wb") as fh:
    fh.write(b"P5\n1280 720\n255\n")
    fh.write(img.tobytes())
