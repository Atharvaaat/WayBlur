#!/usr/bin/env python3
"""Compare OCR engines on synthetic screens: PII recall, decoy false positives, speed.

Usage: bench_ocr.py [FONT.ttf]
Needs: Pillow, numpy, rapidocr-onnxruntime, and the `tesseract` CLI.
"""
import random, re, subprocess, sys, tempfile, time, os
import numpy as np
from PIL import Image, ImageDraw, ImageFont

FONT = sys.argv[1] if len(sys.argv) > 1 else None

# (text, is_pii)  — decoys look numeric/email-ish but are not PII.
ITEMS = [
    ("jane.doe@example.com", 1), ("Reach me: rahul_k99@gmail.com", 1),
    ("Card 4111 1111 1111 1111 exp 09/28", 1), ("5500-0000-0000-0004", 1),
    ("+91 98765 43210", 1), ("Phone: 9876543210", 1), ("tel +1 415 555 0132", 1),
    ("PAN ABCDE1234F", 1), ("SSN 123-45-6789", 1),
    ("AKIAIOSFODNN7EXAMPLE", 1), ("token ghp_abcdefghijklmnopqrstuvwxyz0123456789", 1),
    ("sk-proj_abcdefghijklmnopqrstuvwx", 1),
    ("Aadhaar 2345 6789 0123", 1), ("billing@acme-corp.io", 1),
    ("support: help.desk@company.co.in", 1), ("+44 20 7946 0958", 1),
    ("Card 3782 822463 10005", 1), ("api key AIzaSyA1234567890abcdefghijklmnopqrstuv", 1),
    ("Meeting at 10:30 in room 42", 0), ("Version 3.14.159 released 2026-11-24", 0),
    ("Order ABC1234567890XYZ shipped", 0), ("Total: 1,234.56 USD", 0),
    ("Inbox - 3 unread messages", 0), ("CPU 45% RAM 8.2 GB / 16 GB", 0),
    ("def compute(total, rate): return total * rate", 0), ("https://docs.example.com/guide", 0),
    ("Build #20481 passed in 3m 12s", 0), ("Ports 8080, 8443, 5432", 0),
    ("Chapter 12 - Pages 104-118", 0), ("Q3 revenue grew 14.2 percent", 0),
]

def render(items, dark, size, noise, seed):
    rnd = random.Random(seed)
    W, H = 1280, 720
    bg, fg = ((30, 31, 36), (225, 228, 235)) if dark else ((248, 248, 250), (25, 25, 30))
    img = Image.new("RGB", (W, H), bg)
    d = ImageDraw.Draw(img)
    font = ImageFont.truetype(FONT, size) if FONT else ImageFont.load_default(size=size)
    boxes, y = [], 20
    for text, pii in items:
        x = rnd.randint(20, 400)
        d.text((x, y), text, fill=fg, font=font)
        boxes.append((text, pii))
        y += size + rnd.randint(14, 24)
    a = np.asarray(img).astype(np.int16)
    if noise:
        a = a + np.random.default_rng(seed).normal(0, noise, a.shape).astype(np.int16)
    return Image.fromarray(np.clip(a, 0, 255).astype(np.uint8))

def norm(s):
    return re.sub(r"[^a-z0-9@+._-]", "", s.lower())

def covered(text, ocr_text):
    """Item counts as found if its alphanumeric core appears in OCR output (≥85% char match)."""
    t = norm(text.split(":")[-1] if ":" in text and "http" not in text else text)
    o = norm(ocr_text)
    if t in o:
        return True
    # tolerate 1 wrong char per ~12 in the key part
    key = norm(max(text.split(), key=len))
    if len(key) < 6:
        return key in o
    k = max(1, len(key) // 12)
    for i in range(0, max(1, len(o) - len(key) + 1)):
        w = o[i:i + len(key)]
        if len(w) == len(key) and sum(a != b for a, b in zip(w, key)) <= k:
            return True
    return False

def tess(img):
    with tempfile.NamedTemporaryFile(suffix=".png", delete=False) as f:
        img.save(f.name)
    t = time.perf_counter()
    out = subprocess.run(["tesseract", f.name, "-", "--psm", "11"], capture_output=True, text=True).stdout
    dt = time.perf_counter() - t
    os.unlink(f.name)
    return out, dt

_rapid = None
def rapid(img):
    global _rapid
    if _rapid is None:
        from rapidocr_onnxruntime import RapidOCR
        _rapid = RapidOCR()
        _rapid(np.asarray(img)[:, :, ::-1])  # warm-up
    t = time.perf_counter()
    res, _ = _rapid(np.asarray(img)[:, :, ::-1])
    dt = time.perf_counter() - t
    return "\n".join(r[1] for r in (res or [])), dt

configs = [("light 22px", False, 22, 0), ("light 16px", False, 16, 0), ("light 13px", False, 13, 0),
           ("dark 16px", True, 16, 0), ("dark 13px noisy", True, 13, 12), ("light 14px noisy", False, 14, 10)]

totals = {"tesseract": [0, 0, 0, 0.0, 0], "rapidocr": [0, 0, 0, 0.0, 0]}
print(f"{'screen':18} {'engine':10} {'PII found':>10} {'decoy leaks':>12} {'sec':>6}")
for si, (name, dark, size, noise) in enumerate(configs):
    items = ITEMS[:]
    random.Random(si).shuffle(items)
    items = items[: 720 // (size + 20) - 1] if len(items) * (size + 24) > 700 else items
    img = render(items, dark, size, noise, si)
    for eng, fn in (("tesseract", tess), ("rapidocr", rapid)):
        text, dt = fn(img)
        pii = [t for t, p in items if p]
        found = sum(covered(t, text) for t in pii)
        # a decoy "leaks" only if it would be misread into something we can't tell apart; here we
        # just report how many decoys were read (reading decoys is fine — classifier filters them).
        decoys_read = sum(covered(t, text) for t, p in items if not p)
        tt = totals[eng]; tt[0] += found; tt[1] += len(pii); tt[2] += decoys_read
        tt[3] += dt; tt[4] += 1
        print(f"{name:18} {eng:10} {found:>4}/{len(pii):<5} {decoys_read:>12} {dt:>6.2f}")
print()
for eng, (f, n, dr, t, c) in totals.items():
    print(f"{eng:10} PII recall {f}/{n} = {100*f/max(n,1):.0f}%   avg {t/c:.2f}s per 1280x720 frame")
