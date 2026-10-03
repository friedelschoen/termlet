import sys
from typing import IO, cast
from PIL import ImageFont
from fontTools.ttLib import TTFont
import re
import json

FONTSIZES = [
    14
]


def snake(name):
    s = re.sub(r"(.)([A-Z][a-z]+)", r"\1_\2", name)
    s = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", s)
    s = re.sub(r"[^a-zA-Z0-9_]", "_", s)
    s = re.sub(r"__+", "_", s)
    s = s.strip("_").lower()

    if not s:
        return "unnamed"

    if s[0].isdigit():
        s = "_" + s

    return s


class Generator:
    def __init__(self, source: IO, fontpaths: list[str]):
        self.source = source
        self.fontpaths = fontpaths

        self.fonts = []

    def rasterize(self, font: ImageFont.FreeTypeFont, codepoint: int, stride, cell_width: int, cell_height: int):
        mask, (ox, oy) = font.getmask2(chr(codepoint), mode="L")
        mw, mh = mask.size

#        if ox < 0 or ox + mw > cell_width:
        #            print(
        #                f"warning: U+{codepoint:04X} exceeds cell width: "
        #                f"offset={ox}, mask_width={mw}, cell_width={cell_width}", file=sys.stderr)

        cell = bytearray(stride * cell_height)

        for y in range(mh):
            for x in range(mw):
                dx = ox + x
                dy = oy + y

                if not (0 <= dx < cell_width and 0 <= dy < cell_height):
                    continue

                coverage = cast(int, mask[y * mw + x]) >> 4

                index = dy * stride + dx // 2

                if dx & 1:
                    # odd pixel -> low nibble
                    cell[index] |= coverage
                else:
                    # even pixel -> high nibble
                    cell[index] |= coverage << 4

        return cell

    def get_ranges(self, cmap) -> list[tuple[int, int]]:
        ranges = []

        begin = cmap[0]
        prev = cmap[0]

        for cp in cmap[1:]:
            if cp != prev + 1:
                ranges.append((begin, prev + 1))
                begin = cp

            prev = cp

        ranges.append((begin, prev + 1))
        return ranges

    def emit_font(self, fontpath: str, cmap: list[int], size: int):
        font = ImageFont.truetype(fontpath, size)
        name, style = font.getname()
        if not name:
            name = ""
        cname = snake(name + "_" + style if style else name)

        advance = round(font.getlength("M"))
        ascent, descent = font.getmetrics()

        cell_width = advance
        cell_height = ascent + descent
        stride = (cell_width + 1) // 2

        print(f"generating {fontpath!r} as {cname}", file=sys.stderr)

        self.source.write(f"static const uint8_t {cname}_{size}_data[] = {{\n\t")

        # Rasterize .notdef by asking FreeType for an unmapped codepoint.
        notdef_cp = next(
            cp for cp in range(0x10FFFF, -1, -1)
            if cp not in cmap
        )

        raster = self.rasterize(
            font,
            notdef_cp,
            stride,
            cell_width,
            cell_height,
        )

        self.source.write("\n\t/* .notdef */\n\t")
        for i, b in enumerate(raster):
            if i > 0 and i % stride == 0:
                self.source.write("\n\t")
            self.source.write(f"0x{b:02x}, ")

        for codepoint in cmap:
            raster = self.rasterize(
                font,
                codepoint,
                stride,
                cell_width,
                cell_height,
            )

            self.source.write(f"\n\t/* glyph 0x{codepoint:04x} */\n\t")
            for i, b in enumerate(raster):
                if i > 0 and i % stride == 0:
                    self.source.write("\n\t")
                self.source.write(f"0x{b:02x}, ")

        self.source.write("\n};\n\n")

        ranges = self.get_ranges(cmap)
        self.source.write(f"static const uint32_t {cname}_{size}_ranges[] = {{\n")
        for begin, end in ranges:
            self.source.write(f"\t0x{begin:04x}, 0x{end:04x},\n")
        self.source.write("};\n\n")

        self.source.write(f"static const struct font {cname}_{size} = {{\n")
        self.source.write(f"\t.name = {json.dumps(name)},\n")
        self.source.write(f"\t.style = {json.dumps(style)},\n")
        self.source.write(f"\t.size = {size},\n")
        self.source.write(f"\t.data = {cname}_{size}_data,\n")
        self.source.write(f"\t.width = {cell_width},\n")
        self.source.write(f"\t.height = {cell_height},\n")
        self.source.write(f"\t.stride = {stride},\n")
        self.source.write(f"\t.ranges = {cname}_{size}_ranges,\n")
        self.source.write(f"\t.range_count = {len(ranges)},\n")
        self.source.write(f"\t.glyph_min = {min(cmap)},\n")
        self.source.write(f"\t.glyph_max = {max(cmap)},\n")
        self.source.write("};\n\n")

        self.fonts.append(f'{cname}_{size}')

    def get_codepoints(self, fontpath) -> list[int]:
        cmap = TTFont(fontpath).getBestCmap()
        if cmap == None:
            return []
        return sorted(cmap.keys())

    def generate(self):
        self.source.write("#include <fonts.h>\n\n")

        for fontpath in self.fontpaths:
            codepoints = self.get_codepoints(fontpath)
            for size in FONTSIZES:
                self.emit_font(fontpath, codepoints,  size)

        self.source.write(f"const size_t font_count = {len(self.fonts)};\n")
        self.source.write(f"const struct font *const fonts[] = {{\n")
        for font in self.fonts:
            self.source.write(f"\t&{font},\n")
        self.source.write("};\n\n")


if __name__ == "__main__":
    Generator(sys.stdout, sys.argv[1:]).generate()
