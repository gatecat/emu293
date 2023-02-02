from elftools.elf.elffile import ELFFile

groups = [
    (0, 14, 16, 21),
    (1, 9,  19, 27),
    (2, 17, 20, 28),
    (3, 10, 18, 25),
    (4, 5,  26, 31),
    (6, 15, 22, 30),
    (7, 12, 13, 24),
    (8, 11, 23, 29)
]

group_luts = [
    [0b0100, 0b1110, 0b1100, 0b0110, 0b0001, 0b1011, 0b1001, 0b0011, 0b1000, 0b0010, 0b0000, 0b1010, 0b1101, 0b0111, 0b0101, 0b1111, ],
    [0b1010, 0b0110, 0b0010, 0b1001, 0b0000, 0b0101, 0b0001, 0b1111, 0b1100, 0b1000, 0b1110, 0b0100, 0b0111, 0b1011, 0b0011, 0b1101, ],
    [0b1000, 0b1100, 0b1001, 0b1101, 0b0101, 0b0111, 0b0001, 0b1011, 0b0110, 0b0011, 0b1111, 0b0000, 0b1010, 0b1110, 0b0100, 0b0010, ],
    [0b1001, 0b0111, 0b0001, 0b0000, 0b1000, 0b1101, 0b1111, 0b0101, 0b1010, 0b0100, 0b1100, 0b1110, 0b0110, 0b0010, 0b0011, 0b1011, ],
    [0b0110, 0b1001, 0b1010, 0b0101, 0b0011, 0b1000, 0b0111, 0b0001, 0b1100, 0b0100, 0b0010, 0b1011, 0b1111, 0b0000, 0b1101, 0b1110, ],
    [0b1111, 0b0101, 0b0111, 0b0001, 0b1110, 0b0110, 0b1100, 0b0000, 0b1101, 0b1000, 0b1001, 0b0100, 0b1011, 0b0011, 0b1010, 0b0010, ],
    [0b0110, 0b1111, 0b0100, 0b1001, 0b1000, 0b0000, 0b1110, 0b1010, 0b0010, 0b1100, 0b0011, 0b0001, 0b1011, 0b0111, 0b1101, 0b0101, ],
    [0b0101, 0b1111, 0b0001, 0b0000, 0b1110, 0b1100, 0b0010, 0b1001, 0b0110, 0b0011, 0b1000, 0b1011, 0b0111, 0b1010, 0b1101, 0b0100, ],
]

def get_encrypted_range(elf):
    with open(elf, 'rb') as f:
        elffile = ELFFile(f)
        symtab = elffile.get_section_by_name('.symtab')
        # lead.sys files have different scrambled range...
        is_leadsys = (symtab and symtab.get_symbol_by_name("Leadsysfileflag") is not None)
        for seg in elffile.iter_segments():
            if seg.header.p_filesz > 0:
                break # TODO better way of finding segment...
        # TODO: check their elf descrambling logic to make sure these offsets always work
        if is_leadsys:
            scram_beg = 0x1b00
        else:
            scram_beg = 0x1fc
        scram_len = 0x2000
        return (seg.header.p_paddr + scram_beg), (seg.header.p_offset + scram_beg), scram_len

def descramble_words(words, paddr):
    result = []
    for i, w in enumerate(words):
        x_bits = []
        plain = 0
        for j, g in enumerate(groups):
            enc_val = 0
            for k, bit in enumerate(g):
                if (w >> bit) & 0x1 == 0x1: enc_val |= (1 << k)
            lut_val = group_luts[j][enc_val]
            if lut_val is None:
                x_bits += g
            else:
                for k, bit in enumerate(g):
                    if (lut_val >> k) & 0x1 == 0x1: plain |= (1 << bit)
        result.append(plain)
        if len(x_bits) > 0:
            with_x = list(f"{plain:032b}")
            for b in x_bits:
                with_x[31-b] = 'x'
            with_x = "".join(with_x)
            print(f"FAIL at 0x{paddr+4*i:08x}: {plain:08x} {with_x}")
    return result

def descramble_file(infile, outfile, erange):
    paddr, foffset, l = erange
    print(foffset)
    with open(infile, "rb") as f:
        data = bytearray(f.read())
    words = []
    for i in range(foffset, foffset+l, 4):
        words.append(int.from_bytes(data[i:i+4], 'little'))
    words = descramble_words(words, paddr)
    for i, w in enumerate(words):
        ofs = foffset + i * 4
        data[ofs:ofs+4] = w.to_bytes(4, 'little')
    with open(outfile, "wb") as f:
        f.write(data)

def main():
    import sys
    erange = get_encrypted_range(sys.argv[1])
    descramble_file(sys.argv[1], sys.argv[2], erange)

if __name__ == '__main__':
    main()
