#!/usr/bin/env python3
"""Dump the ELF section table of a Wii U RPX/RPL file.

Step 1: confirm our understanding of the format before parsing imports.
RPX is a modified 32-bit big-endian PowerPC ELF, with sections optionally
zlib-compressed.
"""
import struct
import sys
import zlib

SHT_RPL_EXPORTS  = 0x80000001
SHT_RPL_IMPORTS  = 0x80000002
SHT_RPL_CRCS     = 0x80000003
SHT_RPL_FILEINFO = 0x80000004
SHF_RPL_ZLIB     = 0x08000000

SHT_NAMES = {
    0x00000000: "NULL",
    0x00000001: "PROGBITS",
    0x00000002: "SYMTAB",
    0x00000003: "STRTAB",
    0x00000004: "RELA",
    0x00000008: "NOBITS",
    SHT_RPL_EXPORTS: "RPL_EXPORTS",
    SHT_RPL_IMPORTS: "RPL_IMPORTS",
    SHT_RPL_CRCS: "RPL_CRCS",
    SHT_RPL_FILEINFO: "RPL_FILEINFO",
}


def section_data(blob, sh):
    """Return a section's bytes, decompressing if the RPL zlib flag is set."""
    raw = blob[sh["offset"]:sh["offset"] + sh["size"]]
    if not (sh["flags"] & SHF_RPL_ZLIB):
        return raw
    if len(raw) < 4:
        return b""
    expected = struct.unpack(">I", raw[:4])[0]
    out = zlib.decompress(raw[4:])
    if len(out) != expected:
        print(f"  ! size mismatch: header says {expected}, got {len(out)}")
    return out


def main(path):
    with open(path, "rb") as f:
        blob = f.read()

    if blob[:4] != b"\x7fELF":
        sys.exit("not an ELF file")

    ei_class, ei_data = blob[4], blob[5]
    print(f"class={'32' if ei_class == 1 else '64'}-bit  "
          f"endian={'big' if ei_data == 2 else 'little'}")
    if ei_class != 1 or ei_data != 2:
        sys.exit("expected 32-bit big-endian")

    (e_type, e_machine, _ver, _entry, _phoff, e_shoff, _flags,
     _ehsize, _phentsize, _phnum,
     e_shentsize, e_shnum, e_shstrndx) = struct.unpack(">HHIIIIIHHHHHH",
                                                        blob[16:52])
    print(f"type=0x{e_type:04x}  machine=0x{e_machine:04x} "
          f"({'PowerPC' if e_machine == 20 else '?'})")
    print(f"{e_shnum} sections, shstrndx={e_shstrndx}\n")

    sections = []
    for i in range(e_shnum):
        off = e_shoff + i * e_shentsize
        (name, typ, flags, addr, offset, size,
         link, info, align, entsize) = struct.unpack(">IIIIIIIIII",
                                                      blob[off:off + 40])
        sections.append(dict(idx=i, name_off=name, type=typ, flags=flags,
                             addr=addr, offset=offset, size=size,
                             link=link, info=info, entsize=entsize))

    shstr = section_data(blob, sections[e_shstrndx])

    def name_of(sh):
        end = shstr.find(b"\0", sh["name_off"])
        return shstr[sh["name_off"]:end].decode("utf-8", "replace")

    print(f"{'idx':>3} {'name':<28} {'type':<14} {'size':>9}  flags")
    print("-" * 74)
    for sh in sections:
        tname = SHT_NAMES.get(sh["type"], f"0x{sh['type']:08x}")
        zlib_flag = "ZLIB " if sh["flags"] & SHF_RPL_ZLIB else ""
        print(f"{sh['idx']:>3} {name_of(sh):<28} {tname:<14} "
              f"{sh['size']:>9}  {zlib_flag}0x{sh['flags']:08x}")

    print("\nImport sections:")
    found = [sh for sh in sections if sh["type"] == SHT_RPL_IMPORTS]
    if not found:
        print("  none")
    for sh in found:
        n = name_of(sh)
        kind = "functions" if n.startswith(".fimport_") else \
               "data" if n.startswith(".dimport_") else "?"
        lib = n.split("import_", 1)[1] if "import_" in n else n
        print(f"  {n:<28} -> {lib:<12} ({kind}, {sh['size']} bytes)")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: dump_sections.py <file.rpx>")
    main(sys.argv[1])