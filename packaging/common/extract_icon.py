#!/usr/bin/env python3
"""Extracts the application icon from a Windows .exe as an .ico file.

Used once to derive packaging icons from PoB's own "Path of Building.exe"
(MIT licensed, PathOfBuildingCommunity/PathOfBuilding). Standard library only.
usage: extract_icon.py <exe> <out.ico>
"""
import struct
import sys

RT_ICON, RT_GROUP_ICON = 3, 14


def main(exe_path, out_path):
    data = open(exe_path, "rb").read()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    nsections = struct.unpack_from("<H", data, pe + 6)[0]
    opt_size = struct.unpack_from("<H", data, pe + 20)[0]
    opt = pe + 24
    magic = struct.unpack_from("<H", data, opt)[0]
    dd = opt + (96 if magic == 0x10B else 112)
    rsrc_rva, _ = struct.unpack_from("<II", data, dd + 2 * 8)
    sections = []
    for i in range(nsections):
        off = opt + opt_size + i * 40
        vsize, va, rawsize, rawptr = struct.unpack_from("<IIII", data, off + 8)
        sections.append((va, max(vsize, rawsize), rawptr))

    def rva_to_off(rva):
        for va, size, raw in sections:
            if va <= rva < va + size:
                return rva - va + raw
        raise ValueError("rva not mapped")

    base = rva_to_off(rsrc_rva)

    def entries(dir_off):
        named, ids = struct.unpack_from("<HH", data, base + dir_off + 12)
        for i in range(named + ids):
            name, target = struct.unpack_from("<II", data, base + dir_off + 16 + i * 8)
            yield name, target

    def leaf_data(target):
        # Descend until a data entry is reached (first language).
        while target & 0x80000000:
            target = next(entries(target & 0x7FFFFFFF))[1]
        rva, size = struct.unpack_from("<II", data, base + target)
        off = rva_to_off(rva)
        return data[off:off + size]

    by_type = {name: target for name, target in entries(0)}
    icons = {}
    for name, target in entries(by_type[RT_ICON] & 0x7FFFFFFF):
        icons[name] = leaf_data(target)
    group = leaf_data(next(entries(by_type[RT_GROUP_ICON] & 0x7FFFFFFF))[1])

    _, _, count = struct.unpack_from("<HHH", group, 0)
    header = struct.pack("<HHH", 0, 1, count)
    dirs, blobs = b"", b""
    offset = 6 + 16 * count
    for i in range(count):
        w, h, colors, res, planes, bits, size, icon_id = struct.unpack_from("<BBBBHHIH", group, 6 + i * 14)
        blob = icons[icon_id]
        dirs += struct.pack("<BBBBHHII", w, h, colors, res, planes, bits, len(blob), offset)
        blobs += blob
        offset += len(blob)
    open(out_path, "wb").write(header + dirs + blobs)
    print(f"{count} images written to {out_path}")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
