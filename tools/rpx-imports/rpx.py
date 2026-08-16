"""Minimal reader for Wii U RPX/RPL files.

RPX is a 32-bit big-endian PowerPC ELF with two additions:
  - custom section types in the 0x8000xxxx range
  - per-section zlib compression, flagged by SHF_RPL_ZLIB

Imports are declared by sections named .fimport_<lib> (functions) and
.dimport_<lib> (data). Those sections do NOT contain the symbol names --
they are headers. The names live in .symtab, where each imported symbol
has st_shndx pointing at its import section.
"""
import struct
import zlib

SHT_SYMTAB       = 0x00000002
SHT_RPL_EXPORTS  = 0x80000001
SHT_RPL_IMPORTS  = 0x80000002
SHT_RPL_CRCS     = 0x80000003
SHT_RPL_FILEINFO = 0x80000004
SHF_RPL_ZLIB     = 0x08000000
SHT_RELA        = 0x00000004

RELA_ENTRY_SIZE = 12
SYMTAB_ENTRY_SIZE = 16


class RpxError(Exception):
    pass


class Rpx:
    def __init__(self, blob):
        self.blob = blob
        self._check_header()
        self._read_sections()

    def _check_header(self):
        b = self.blob
        if b[:4] != b"\x7fELF":
            raise RpxError("not an ELF file")
        if b[4] != 1 or b[5] != 2:
            raise RpxError("expected 32-bit big-endian")
        if struct.unpack(">H", b[18:20])[0] != 20:
            raise RpxError("expected PowerPC (machine 20)")

    def _read_sections(self):
        (_type, _machine, _ver, _entry, _phoff, shoff, _flags,
         _ehsize, _phentsize, _phnum,
         shentsize, shnum, shstrndx) = struct.unpack(">HHIIIIIHHHHHH",
                                                      self.blob[16:52])
        self.sections = []
        for i in range(shnum):
            off = shoff + i * shentsize
            (name, typ, flags, addr, offset, size,
             link, info, align, entsize) = struct.unpack(
                ">IIIIIIIIII", self.blob[off:off + 40])
            self.sections.append(dict(
                idx=i, name_off=name, type=typ, flags=flags, addr=addr,
                offset=offset, size=size, link=link, info=info,
                entsize=entsize))
        self._shstr = self.data(self.sections[shstrndx])

    def data(self, sh):
        """Section bytes, transparently decompressed."""
        raw = self.blob[sh["offset"]:sh["offset"] + sh["size"]]
        if not (sh["flags"] & SHF_RPL_ZLIB):
            return raw
        if len(raw) < 4:
            return b""
        return zlib.decompress(raw[4:])

    def name(self, sh):
        end = self._shstr.find(b"\0", sh["name_off"])
        return self._shstr[sh["name_off"]:end].decode("utf-8", "replace")

    def imports(self, referenced_only=True):
        """{library: {"functions": [...], "data": [...]}}

        referenced_only=True keeps only symbols named by a relocation --
        i.e. actually used. False returns every linked stub, which is an
        upper bound and can be the library's entire export table.
        """
        kinds = {}
        for sh in self.sections:
            if sh["type"] != SHT_RPL_IMPORTS:
                continue
            n = self.name(sh)
            if n.startswith(".fimport_"):
                kinds[sh["idx"]] = (n[len(".fimport_"):], "functions")
            elif n.startswith(".dimport_"):
                kinds[sh["idx"]] = (n[len(".dimport_"):], "data")

        if not kinds:
            return {}

        refs = self.referenced_symbols() if referenced_only else None

        result = {}
        for sh in self.sections:
            if sh["type"] != SHT_SYMTAB:
                continue
            symtab = self.data(sh)
            strtab = self.data(self.sections[sh["link"]])

            count = len(symtab) // SYMTAB_ENTRY_SIZE
            for i in range(count):
                off = i * SYMTAB_ENTRY_SIZE
                (st_name, _value, _size, st_info,
                 _other, st_shndx) = struct.unpack(
                    ">IIIBBH", symtab[off:off + SYMTAB_ENTRY_SIZE])
                if st_shndx not in kinds:
                    continue
                if (st_info & 0xf) == 3:        # STT_SECTION: .fimport_* itself
                    continue
                if refs is not None and (sh["idx"], i) not in refs:
                    continue
                end = strtab.find(b"\0", st_name)
                sym = strtab[st_name:end].decode("utf-8", "replace")
                if not sym or sym.startswith("."):
                    continue
                lib, kind = kinds[st_shndx]
                result.setdefault(lib, {"functions": [], "data": []})
                result[lib][kind].append(sym)

        for lib in result:
            for kind in result[lib]:
                result[lib][kind] = sorted(set(result[lib][kind]))
        return result

    def referenced_symbols(self):
        """(symtab_index, symbol_index) pairs named by any relocation.

        Import sections list every stub the linker pulled in, not what the
        code actually calls. Relocations are the ground truth: if nothing
        relocates against a symbol, nothing calls it.
        """
        refs = set()
        for sh in self.sections:
            if sh["type"] != SHT_RELA:
                continue
            data = self.data(sh)
            for off in range(0, len(data) - RELA_ENTRY_SIZE + 1,
                             RELA_ENTRY_SIZE):
                _r_offset, r_info, _r_addend = struct.unpack(
                    ">III", data[off:off + RELA_ENTRY_SIZE])
                refs.add((sh["link"], r_info >> 8))
        return refs

def load(path):
    with open(path, "rb") as f:
        return Rpx(f.read())