#!/usr/bin/env python3
"""Clear the vendor-private 0x800 bit from the MIPS e_flags of an ELF object.

The two T41 ISP firmware objects shipped in this SDK are the only blobs in it
built with that bit set; every sibling - t40's ISP firmware and all three mpsys
firmwares - is 0x...1001, the same value the kernel's own objects have.

binutils 2.40 refuses to merge objects whose e_flags differ at all, so linking
the T41 ISP module fails with:

    libt41-firmware-4-4-94.a: uses different e_flags (0x1800) fields than
    previous modules (0x1000); failed to merge target specific data

Newer linkers drop the unknown bit silently, which is why upstream's own T41
modules come out at 0x70001001. Clearing it here reaches the same result on an
older toolchain. Only the ELF header flag word changes; no code is touched.
"""

import struct
import sys

E_FLAGS_OFFSET = 0x24  # e_flags in a 32-bit ELF header
VENDOR_BIT = 0x800


def main(paths):
    for path in paths:
        with open(path, "r+b") as f:
            if f.read(4) != b"\x7fELF":
                print(f"{path}: not an ELF file, skipped")
                continue
            f.seek(E_FLAGS_OFFSET)
            flags, = struct.unpack("<I", f.read(4))
            if not flags & VENDOR_BIT:
                continue
            f.seek(E_FLAGS_OFFSET)
            f.write(struct.pack("<I", flags & ~VENDOR_BIT))
            print(f"{path}: e_flags {flags:#x} -> {flags & ~VENDOR_BIT:#x}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
