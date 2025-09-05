# Auto-generate a 1-bit bitmap for the exact Font Awesome "wifi" glyph
# - Downloads Font Awesome Free Solid TTF (cached under .pio)
# - Renders U+F1EB at a target box size
# - Emits include/fa_wifi_icon.h containing PROGMEM bitmap + defines

from pathlib import Path
import sys

Import("env")  # PlatformIO SCons env

PROJECT_DIR = Path(env["PROJECT_DIR"])  # workspace root
CACHE_DIR = PROJECT_DIR / ".pio" / "fa_cache"
CACHE_DIR.mkdir(parents=True, exist_ok=True)

HEADER_OUT = PROJECT_DIR / "include" / "fa_wifi_icon.h"
HEADER_OUT.parent.mkdir(parents=True, exist_ok=True)

FA_TTF_URLS = [
    # Try a few well-known CDNs/versions for Font Awesome Free Solid
    "https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.5.1/webfonts/fa-solid-900.ttf",
    "https://cdnjs.cloudflare.com/ajax/libs/font-awesome/5.15.4/webfonts/fa-solid-900.ttf",
]

TTF_PATH = CACHE_DIR / "fa-solid-900.ttf"

def ensure_pillow():
    try:
        import PIL  # noqa: F401
        return True
    except Exception:
        py = env.subst("$PYTHONEXE")
        print("[fa-gen] Installing Pillow into PlatformIO env...")
        # Best-effort install
        ret = env.Execute(f'"{py}" -m pip install --disable-pip-version-check --no-input pillow')
        if ret != 0:
            print("[fa-gen] Pillow install failed; proceeding without exact FA glyph.")
            return False
        try:
            import PIL  # noqa: F401
            return True
        except Exception:
            return False

def download_ttf():
    import urllib.request
    for url in FA_TTF_URLS:
        try:
            print(f"[fa-gen] Downloading FA TTF from {url}")
            with urllib.request.urlopen(url, timeout=20) as r, open(TTF_PATH, "wb") as f:
                f.write(r.read())
            if TTF_PATH.stat().st_size > 0:
                return True
        except Exception as e:
            print(f"[fa-gen] Fetch failed: {e}")
    print("[fa-gen] Could not download Font Awesome TTF.")
    return False

def render_bitmap():
    from PIL import Image, ImageDraw, ImageFont

    # Target box size in pixels (square). Adjust if you want larger/smaller icon.
    BOX = 24
    codepoint = 0xF1EB  # fa-wifi

    # Try multiple sizes to best fit the box height
    font_sizes = [48, 40, 36, 32, 28, 24]
    for fs in font_sizes:
        font = ImageFont.truetype(str(TTF_PATH), fs)
        # Render once to get bbox
        img = Image.new("L", (BOX*2, BOX*2), 0)
        drw = ImageDraw.Draw(img)
        drw.text((0, 0), chr(codepoint), font=font, fill=255)
        bbox = img.getbbox()
        if not bbox:
            continue
        glyph_w = bbox[2] - bbox[0]
        glyph_h = bbox[3] - bbox[1]
        # If height fits reasonably, pick this size
        if glyph_h <= BOX:
            # Center into BOX x BOX
            out = Image.new("L", (BOX, BOX), 0)
            ox = (BOX - glyph_w) // 2
            oy = (BOX - glyph_h) // 2
            out.paste(img.crop(bbox), (ox, oy))
            # Binarize
            out = out.point(lambda p: 255 if p > 128 else 0, mode='1')
            return out
    # Fallback to last attempted size, scaled down
    font = ImageFont.truetype(str(TTF_PATH), font_sizes[-1])
    img = Image.new("L", (BOX*2, BOX*2), 0)
    drw = ImageDraw.Draw(img)
    drw.text((0, 0), chr(codepoint), font=font, fill=255)
    bbox = img.getbbox()
    out = Image.new("L", (BOX, BOX), 0)
    if bbox:
        glyph = img.crop(bbox).resize((BOX, BOX))
        out.paste(glyph, (0, 0))
    out = out.point(lambda p: 255 if p > 128 else 0, mode='1')
    return out

def emit_header(img):
    W, H = img.size
    # Pack rows MSB first per byte
    row_bytes = (W + 7) // 8
    bits = img.tobytes()
    # PIL mode '1' packs LSB first per byte; we need MSB-first bit order.
    # Convert per pixel.
    data = bytearray()
    pix = img.load()
    for y in range(H):
        b = 0
        bit_count = 0
        for x in range(W):
            b <<= 1
            b |= 1 if pix[x, y] != 0 else 0
            bit_count += 1
            if bit_count == 8:
                data.append(b)
                b = 0
                bit_count = 0
        if bit_count:
            b <<= (8 - bit_count)
            data.append(b)

    with open(HEADER_OUT, "w", newline="\n") as f:
        f.write("// Auto-generated from Font Awesome Free Solid (fa-wifi)\n")
        f.write("// License: Font Awesome Free (https://fontawesome.com), CC BY 4.0 for icons\n")
        f.write("#pragma once\n\n")
        f.write("#include <stdint.h>\n")
        f.write("#include <pgmspace.h>\n\n")
        f.write("#define FA_WIFI_ICON_AVAILABLE 1\n")
        f.write(f"#define FA_WIFI_ICON_WIDTH {W}\n")
        f.write(f"#define FA_WIFI_ICON_HEIGHT {H}\n\n")
        f.write("static const uint8_t FA_WIFI_ICON_BITMAP[] PROGMEM = {\n")
        # Emit hex rows, 12 bytes per line for readability
        for i, byte in enumerate(data):
            if i % 12 == 0:
                f.write("    ")
            f.write(f"0x{byte:02X}, ")
            if i % 12 == 11:
                f.write("\n")
        if len(data) % 12 != 0:
            f.write("\n")
        f.write("};\n")
    # Signal to the compiler
    env.Append(CPPDEFINES=["FA_WIFI_ICON_AVAILABLE"]) 
    print(f"[fa-gen] Wrote header: {HEADER_OUT}")


def main():
    # Try to prepare everything; on any failure, just skip (fallback icon used)
    ok_pil = ensure_pillow()
    if not ok_pil:
        print("[fa-gen] Pillow missing; skipping FA icon generation.")
        return
    if not TTF_PATH.exists():
        if not download_ttf():
            return
    try:
        img = render_bitmap()
        emit_header(img)
    except Exception as e:
        print(f"[fa-gen] Generation failed: {e}")


main()

