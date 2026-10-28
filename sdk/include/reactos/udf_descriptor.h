/*
 * Copyright 2026 KosBeg
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Bounded, allocation-free validation shared by UDFS and FreeLoader.
 * ECMA TR/112-3 sections 2.2.1, 2.3.1, 2.3.11 and errata 7.8.
 * The caller supplies bytes actually read, never a descriptor's CRC length.
 */
#ifndef _REACTOS_UDF_DESCRIPTOR_H_
#define _REACTOS_UDF_DESCRIPTOR_H_

#define UDF_DESCRIPTOR_ANY_ID 0xffffU

static __inline unsigned int
UdfDescriptorRead16(const unsigned char *p)
{
    return p[0] | ((unsigned int)p[1] << 8);
}

static __inline unsigned int
UdfDescriptorRead32(const unsigned char *p)
{
    return UdfDescriptorRead16(p) |
           (UdfDescriptorRead16(p + 2) << 16);
}

static __inline unsigned int
UdfDescriptorCrc(const unsigned char *p, unsigned int bytes)
{
    unsigned int crc = 0, bit;
    while (bytes--) {
        crc ^= (unsigned int)*p++ << 8;
        for (bit = 0; bit < 8; ++bit)
            crc = ((crc << 1) ^ ((crc & 0x8000U) ? 0x1021U : 0)) & 0xffffU;
    }
    return crc;
}

typedef unsigned int (*UDF_DESCRIPTOR_CRC_ROUTINE)(const unsigned char *, unsigned int);

static __inline int
UdfValidateDescriptorEx(const unsigned char *p, unsigned int bytes,
                        unsigned int expectedId, unsigned int location,
                        UDF_DESCRIPTOR_CRC_ROUTINE crcRoutine)
{
    unsigned int id, crcBytes, i, checksum = 0;
    unsigned long long length;

    if (bytes < 16)
        return 0;
    id = UdfDescriptorRead16(p);
    crcBytes = UdfDescriptorRead16(p + 10);
    if ((expectedId != UDF_DESCRIPTOR_ANY_ID && id != expectedId) ||
        UdfDescriptorRead32(p + 12) != location ||
        (UdfDescriptorRead16(p + 2) != 2 &&
         UdfDescriptorRead16(p + 2) != 3) ||
        crcBytes > bytes - 16)
        return 0;
    for (i = 0; i < 16; ++i)
        if (i != 4)
            checksum += p[i];
    if ((checksum & 0xffU) != p[4])
        return 0;

    /* Read variable-length fields only after proving the fixed header fits.
     * Use wide arithmetic before comparing with the actual read length. */
    switch (id) {
    case 1: case 2: case 3: case 4: case 5: case 8: case 256: case 265:
        length = 512;
        break;
    case 6: /* Logical Volume Descriptor, including the partition maps. */
        if (bytes < 440) return 0;
        length = 440ULL + UdfDescriptorRead32(p + 264);
        break;
    case 7: /* Unallocated Space Descriptor. */
        if (bytes < 24) return 0;
        length = 24ULL + 8ULL * UdfDescriptorRead32(p + 20);
        break;
    case 9: /* Logical Volume Integrity Descriptor. */
        if (bytes < 80) return 0;
        length = 80ULL + 8ULL * UdfDescriptorRead32(p + 72) +
                 UdfDescriptorRead32(p + 76);
        break;
    case 257: /* File Identifier Descriptor, including its four-byte padding. */
        if (bytes < 38) return 0;
        length = (38ULL + UdfDescriptorRead16(p + 36) + p[19] + 3) & ~3ULL;
        break;
    case 258: /* Allocation Extent Descriptor: header-only CRC is permitted. */
        if (bytes < 24) return 0;
        length = 24ULL + UdfDescriptorRead32(p + 20);
        if (length > bytes || (crcBytes != 8 && crcBytes != length - 16))
            return 0;
        return UdfDescriptorRead16(p + 8) == crcRoutine(p + 16, crcBytes);
    case 259: length = 52; break; /* Indirect Entry. */
    case 260: length = 36; break; /* Terminal Entry. */
    case 261:
        if (bytes < 176) return 0;
        length = 176ULL + UdfDescriptorRead32(p + 168) +
                 UdfDescriptorRead32(p + 172);
        break;
    case 262: length = 24; break; /* Extended Attribute Header Descriptor. */
    case 263:
        if (bytes < 40) return 0;
        length = 40ULL + UdfDescriptorRead32(p + 36);
        break;
    case 264: /* A bitmap may extend beyond the first sector being checked. */
        if (bytes < 24) return 0;
        length = 24ULL + UdfDescriptorRead32(p + 20);
        if (crcBytes != 0 && crcBytes != 8 &&
            crcBytes != (length - 16 > 65535 ? 65535 : length - 16))
            return 0;
        return UdfDescriptorRead16(p + 8) == crcRoutine(p + 16, crcBytes);
    case 266:
        if (bytes < 216) return 0;
        length = 216ULL + UdfDescriptorRead32(p + 208) +
                 UdfDescriptorRead32(p + 212);
        break;
    default:
        /* Unknown vendor descriptors get no zero-CRC bypass. Their caller
         * must still decide whether that descriptor type is supported. */
        if (!crcBytes) return 0;
        length = 16ULL + crcBytes;
        break;
    }
    if (length > bytes || crcBytes != (length - 16 > 65535 ? 65535 : length - 16))
        return 0;
    return UdfDescriptorRead16(p + 8) == crcRoutine(p + 16, crcBytes);
}

static __inline int
UdfValidateDescriptor(const unsigned char *p, unsigned int bytes,
                      unsigned int expectedId, unsigned int location)
{
    return UdfValidateDescriptorEx(p, bytes, expectedId, location, UdfDescriptorCrc);
}

/* UDF's type-zero sparing table has a counted body, not an arbitrary
 * vendor descriptor whose CRC length could be used as its actual size. */
static __inline int
UdfValidateSparingTable(const unsigned char *p, unsigned int bytes,
                        unsigned int location)
{
    static const unsigned char Identifier[23] = "*UDF Sparing Table";
    unsigned int i;
    unsigned long long length;
    if (bytes < 56 || p[5] || p[16] || p[50] || p[51])
        return 0;
    length = 56ULL + 8ULL * UdfDescriptorRead16(p + 48);
    if (length > bytes || UdfDescriptorRead16(p + 10) !=
        (length - 16 > 65535 ? 65535 : length - 16))
        return 0;
    for (i = 0; i < sizeof(Identifier); i++)
        if (p[17 + i] != Identifier[i])
            return 0;
    return UdfValidateDescriptor(p, bytes, 0, location);
}

#endif
