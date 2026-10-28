////////////////////////////////////////////////////////////////////
// Copyright (C) Alexander Telyatnikov, Ivan Keliukh, Yegor Anchishkin, SKIF Software, 1999-2013. Kiev, Ukraine
// All rights reserved
// This file was released under the GPLv2 on June 2015.
////////////////////////////////////////////////////////////////////
/*
        Module name:

   udf_info.cpp

        Abstract:

   This file contains filesystem-specific routines

*/

#include "udf.h"
#include <udf_descriptor.h>

#define         UDF_BUG_CHECK_ID                UDF_FILE_UDF_INFO

#define DOS_CRC_MODULUS 41
#define hexChar crcChar
static const char crcChar[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ#_~-@";

/* Used to convert hex digits to ASCII for readability. */
//static const char hexChar[] = "0123456789ABCDEF";

static const uint16 CrcTable[256] = {
    0x0000U, 0x1021U, 0x2042U, 0x3063U, 0x4084U, 0x50a5U, 0x60c6U, 0x70e7U,
    0x8108U, 0x9129U, 0xa14aU, 0xb16bU, 0xc18cU, 0xd1adU, 0xe1ceU, 0xf1efU,
    0x1231U, 0x0210U, 0x3273U, 0x2252U, 0x52b5U, 0x4294U, 0x72f7U, 0x62d6U,
    0x9339U, 0x8318U, 0xb37bU, 0xa35aU, 0xd3bdU, 0xc39cU, 0xf3ffU, 0xe3deU,
    0x2462U, 0x3443U, 0x0420U, 0x1401U, 0x64e6U, 0x74c7U, 0x44a4U, 0x5485U,
    0xa56aU, 0xb54bU, 0x8528U, 0x9509U, 0xe5eeU, 0xf5cfU, 0xc5acU, 0xd58dU,
    0x3653U, 0x2672U, 0x1611U, 0x0630U, 0x76d7U, 0x66f6U, 0x5695U, 0x46b4U,
    0xb75bU, 0xa77aU, 0x9719U, 0x8738U, 0xf7dfU, 0xe7feU, 0xd79dU, 0xc7bcU,
    0x48c4U, 0x58e5U, 0x6886U, 0x78a7U, 0x0840U, 0x1861U, 0x2802U, 0x3823U,
    0xc9ccU, 0xd9edU, 0xe98eU, 0xf9afU, 0x8948U, 0x9969U, 0xa90aU, 0xb92bU,
    0x5af5U, 0x4ad4U, 0x7ab7U, 0x6a96U, 0x1a71U, 0x0a50U, 0x3a33U, 0x2a12U,
    0xdbfdU, 0xcbdcU, 0xfbbfU, 0xeb9eU, 0x9b79U, 0x8b58U, 0xbb3bU, 0xab1aU,
    0x6ca6U, 0x7c87U, 0x4ce4U, 0x5cc5U, 0x2c22U, 0x3c03U, 0x0c60U, 0x1c41U,
    0xedaeU, 0xfd8fU, 0xcdecU, 0xddcdU, 0xad2aU, 0xbd0bU, 0x8d68U, 0x9d49U,
    0x7e97U, 0x6eb6U, 0x5ed5U, 0x4ef4U, 0x3e13U, 0x2e32U, 0x1e51U, 0x0e70U,
    0xff9fU, 0xefbeU, 0xdfddU, 0xcffcU, 0xbf1bU, 0xaf3aU, 0x9f59U, 0x8f78U,
    0x9188U, 0x81a9U, 0xb1caU, 0xa1ebU, 0xd10cU, 0xc12dU, 0xf14eU, 0xe16fU,
    0x1080U, 0x00a1U, 0x30c2U, 0x20e3U, 0x5004U, 0x4025U, 0x7046U, 0x6067U,
    0x83b9U, 0x9398U, 0xa3fbU, 0xb3daU, 0xc33dU, 0xd31cU, 0xe37fU, 0xf35eU,
    0x02b1U, 0x1290U, 0x22f3U, 0x32d2U, 0x4235U, 0x5214U, 0x6277U, 0x7256U,
    0xb5eaU, 0xa5cbU, 0x95a8U, 0x8589U, 0xf56eU, 0xe54fU, 0xd52cU, 0xc50dU,
    0x34e2U, 0x24c3U, 0x14a0U, 0x0481U, 0x7466U, 0x6447U, 0x5424U, 0x4405U,
    0xa7dbU, 0xb7faU, 0x8799U, 0x97b8U, 0xe75fU, 0xf77eU, 0xc71dU, 0xd73cU,
    0x26d3U, 0x36f2U, 0x0691U, 0x16b0U, 0x6657U, 0x7676U, 0x4615U, 0x5634U,
    0xd94cU, 0xc96dU, 0xf90eU, 0xe92fU, 0x99c8U, 0x89e9U, 0xb98aU, 0xa9abU,
    0x5844U, 0x4865U, 0x7806U, 0x6827U, 0x18c0U, 0x08e1U, 0x3882U, 0x28a3U,
    0xcb7dU, 0xdb5cU, 0xeb3fU, 0xfb1eU, 0x8bf9U, 0x9bd8U, 0xabbbU, 0xbb9aU,
    0x4a75U, 0x5a54U, 0x6a37U, 0x7a16U, 0x0af1U, 0x1ad0U, 0x2ab3U, 0x3a92U,
    0xfd2eU, 0xed0fU, 0xdd6cU, 0xcd4dU, 0xbdaaU, 0xad8bU, 0x9de8U, 0x8dc9U,
    0x7c26U, 0x6c07U, 0x5c64U, 0x4c45U, 0x3ca2U, 0x2c83U, 0x1ce0U, 0x0cc1U,
    0xef1fU, 0xff3eU, 0xcf5dU, 0xdf7cU, 0xaf9bU, 0xbfbaU, 0x8fd9U, 0x9ff8U,
    0x6e17U, 0x7e36U, 0x4e55U, 0x5e74U, 0x2e93U, 0x3eb2U, 0x0ed1U, 0x1ef0U
};

static const uint32 crc32_tab[] = {
      0x00000000L, 0x77073096L, 0xee0e612cL, 0x990951baL, 0x076dc419L,
      0x706af48fL, 0xe963a535L, 0x9e6495a3L, 0x0edb8832L, 0x79dcb8a4L,
      0xe0d5e91eL, 0x97d2d988L, 0x09b64c2bL, 0x7eb17cbdL, 0xe7b82d07L,
      0x90bf1d91L, 0x1db71064L, 0x6ab020f2L, 0xf3b97148L, 0x84be41deL,
      0x1adad47dL, 0x6ddde4ebL, 0xf4d4b551L, 0x83d385c7L, 0x136c9856L,
      0x646ba8c0L, 0xfd62f97aL, 0x8a65c9ecL, 0x14015c4fL, 0x63066cd9L,
      0xfa0f3d63L, 0x8d080df5L, 0x3b6e20c8L, 0x4c69105eL, 0xd56041e4L,
      0xa2677172L, 0x3c03e4d1L, 0x4b04d447L, 0xd20d85fdL, 0xa50ab56bL,
      0x35b5a8faL, 0x42b2986cL, 0xdbbbc9d6L, 0xacbcf940L, 0x32d86ce3L,
      0x45df5c75L, 0xdcd60dcfL, 0xabd13d59L, 0x26d930acL, 0x51de003aL,
      0xc8d75180L, 0xbfd06116L, 0x21b4f4b5L, 0x56b3c423L, 0xcfba9599L,
      0xb8bda50fL, 0x2802b89eL, 0x5f058808L, 0xc60cd9b2L, 0xb10be924L,
      0x2f6f7c87L, 0x58684c11L, 0xc1611dabL, 0xb6662d3dL, 0x76dc4190L,
      0x01db7106L, 0x98d220bcL, 0xefd5102aL, 0x71b18589L, 0x06b6b51fL,
      0x9fbfe4a5L, 0xe8b8d433L, 0x7807c9a2L, 0x0f00f934L, 0x9609a88eL,
      0xe10e9818L, 0x7f6a0dbbL, 0x086d3d2dL, 0x91646c97L, 0xe6635c01L,
      0x6b6b51f4L, 0x1c6c6162L, 0x856530d8L, 0xf262004eL, 0x6c0695edL,
      0x1b01a57bL, 0x8208f4c1L, 0xf50fc457L, 0x65b0d9c6L, 0x12b7e950L,
      0x8bbeb8eaL, 0xfcb9887cL, 0x62dd1ddfL, 0x15da2d49L, 0x8cd37cf3L,
      0xfbd44c65L, 0x4db26158L, 0x3ab551ceL, 0xa3bc0074L, 0xd4bb30e2L,
      0x4adfa541L, 0x3dd895d7L, 0xa4d1c46dL, 0xd3d6f4fbL, 0x4369e96aL,
      0x346ed9fcL, 0xad678846L, 0xda60b8d0L, 0x44042d73L, 0x33031de5L,
      0xaa0a4c5fL, 0xdd0d7cc9L, 0x5005713cL, 0x270241aaL, 0xbe0b1010L,
      0xc90c2086L, 0x5768b525L, 0x206f85b3L, 0xb966d409L, 0xce61e49fL,
      0x5edef90eL, 0x29d9c998L, 0xb0d09822L, 0xc7d7a8b4L, 0x59b33d17L,
      0x2eb40d81L, 0xb7bd5c3bL, 0xc0ba6cadL, 0xedb88320L, 0x9abfb3b6L,
      0x03b6e20cL, 0x74b1d29aL, 0xead54739L, 0x9dd277afL, 0x04db2615L,
      0x73dc1683L, 0xe3630b12L, 0x94643b84L, 0x0d6d6a3eL, 0x7a6a5aa8L,
      0xe40ecf0bL, 0x9309ff9dL, 0x0a00ae27L, 0x7d079eb1L, 0xf00f9344L,
      0x8708a3d2L, 0x1e01f268L, 0x6906c2feL, 0xf762575dL, 0x806567cbL,
      0x196c3671L, 0x6e6b06e7L, 0xfed41b76L, 0x89d32be0L, 0x10da7a5aL,
      0x67dd4accL, 0xf9b9df6fL, 0x8ebeeff9L, 0x17b7be43L, 0x60b08ed5L,
      0xd6d6a3e8L, 0xa1d1937eL, 0x38d8c2c4L, 0x4fdff252L, 0xd1bb67f1L,
      0xa6bc5767L, 0x3fb506ddL, 0x48b2364bL, 0xd80d2bdaL, 0xaf0a1b4cL,
      0x36034af6L, 0x41047a60L, 0xdf60efc3L, 0xa867df55L, 0x316e8eefL,
      0x4669be79L, 0xcb61b38cL, 0xbc66831aL, 0x256fd2a0L, 0x5268e236L,
      0xcc0c7795L, 0xbb0b4703L, 0x220216b9L, 0x5505262fL, 0xc5ba3bbeL,
      0xb2bd0b28L, 0x2bb45a92L, 0x5cb36a04L, 0xc2d7ffa7L, 0xb5d0cf31L,
      0x2cd99e8bL, 0x5bdeae1dL, 0x9b64c2b0L, 0xec63f226L, 0x756aa39cL,
      0x026d930aL, 0x9c0906a9L, 0xeb0e363fL, 0x72076785L, 0x05005713L,
      0x95bf4a82L, 0xe2b87a14L, 0x7bb12baeL, 0x0cb61b38L, 0x92d28e9bL,
      0xe5d5be0dL, 0x7cdcefb7L, 0x0bdbdf21L, 0x86d3d2d4L, 0xf1d4e242L,
      0x68ddb3f8L, 0x1fda836eL, 0x81be16cdL, 0xf6b9265bL, 0x6fb077e1L,
      0x18b74777L, 0x88085ae6L, 0xff0f6a70L, 0x66063bcaL, 0x11010b5cL,
      0x8f659effL, 0xf862ae69L, 0x616bffd3L, 0x166ccf45L, 0xa00ae278L,
      0xd70dd2eeL, 0x4e048354L, 0x3903b3c2L, 0xa7672661L, 0xd06016f7L,
      0x4969474dL, 0x3e6e77dbL, 0xaed16a4aL, 0xd9d65adcL, 0x40df0b66L,
      0x37d83bf0L, 0xa9bcae53L, 0xdebb9ec5L, 0x47b2cf7fL, 0x30b5ffe9L,
      0xbdbdf21cL, 0xcabac28aL, 0x53b39330L, 0x24b4a3a6L, 0xbad03605L,
      0xcdd70693L, 0x54de5729L, 0x23d967bfL, 0xb3667a2eL, 0xc4614ab8L,
      0x5d681b02L, 0x2a6f2b94L, 0xb40bbe37L, 0xc30c8ea1L, 0x5a05df1bL,
      0x2d02ef8dL
};

/*
    This routine converts compressed Unicode to standard
 */
VOID
UDFDecompressUnicode(
    IN OUT PUNICODE_STRING UName,
    IN uint8* CS0,
    IN SIZE_T Length,
    OUT uint16* valueCRC
    )
{
    uint16 compID;
    uint32 unicodeIndex = 0;
    uint32 byteIndex = 1;
    PWCHAR buff;
    uint8* _CS0;

    if (!UName) return;
    RtlZeroMemory(UName, sizeof(*UName));
    if (valueCRC) *valueCRC = 0;
    if (!CS0 || !Length) return;
    compID = CS0[0];
    _CS0 = CS0 + 1;
    // First check for valid compID.
    switch(compID) {
    case UDF_COMP_ID_8: {

        /* UNICODE_STRING includes the terminating WCHAR in MaximumLength. */
        if (Length > MAXUSHORT / sizeof(WCHAR)) goto return_empty_str;
        buff = (PWCHAR)MyAllocatePoolTag__(UDF_FILENAME_MT, (Length)*sizeof(WCHAR), MEM_FNAME_TAG);
        if (!buff) goto return_empty_str;
        UName->Buffer = buff;

        // Loop through all the bytes.
        while (byteIndex < Length) {
            (*buff) = (*_CS0);
            _CS0++;
            byteIndex++;
            buff++;
        }
        unicodeIndex = byteIndex-1;
        break;
    }
    case UDF_COMP_ID_16: {

        /* The compression byte is followed by complete big-endian pairs.
         * Reject an odd payload before the loop can read its missing byte. */
        if (!(Length & 1) || Length > MAXUSHORT - sizeof(WCHAR))
            goto return_empty_str;
        buff = (PWCHAR)MyAllocatePoolTag__(UDF_FILENAME_MT, (Length-1)+sizeof(WCHAR), MEM_FNAME16_TAG);
        if (!buff) goto return_empty_str;
        UName->Buffer = buff;

        // Loop through all the bytes.
        while (byteIndex < Length) {
            // Move the first byte to the high bits of the unicode char.
            *buff = ((*_CS0) << 8) | (*(_CS0+1));
            _CS0+=2;
            byteIndex+=2;
            unicodeIndex++;
            buff++;
            ASSERT(byteIndex <= Length);
        }
        break;
    }
    default: {
return_empty_str:
        UName->Buffer = NULL;
        UName->MaximumLength =
        UName->Length = 0;
        return;
    }
    }
    UName->MaximumLength = (UName->Length = (((uint16)unicodeIndex)*sizeof(WCHAR))) + sizeof(WCHAR);
    UName->Buffer[unicodeIndex] = 0;
    if (valueCRC) {
        *valueCRC = UDFCrc(CS0 + 1, Length - 1, 0);
    }
} // end UDFDecompressUnicode()

/*
    This routine converts standard Unicode to compressed
 */
void
__fastcall
UDFCompressUnicode(
    IN PUNICODE_STRING UName,
    IN OUT uint8** _CS0,
    IN OUT PSIZE_T Length
    )
{
    uint8* CS0;
    uint8 compID;
    uint16 unicodeIndex;
    uint32 i, len;
    PWCHAR Buff;

    len = (UName->Length) / sizeof(WCHAR);
    compID = (!len) ? 0 : UDF_COMP_ID_8;
    // check for uncompressable characters
    Buff = UName->Buffer;
    for(i=0; i<len; i++, Buff++) {
        if ((*Buff) & 0xff00) {
            compID = UDF_COMP_ID_16;
            break;
        }
    }

    CS0 = (uint8*)MyAllocatePool__(NonPagedPool, *Length = (((compID==UDF_COMP_ID_8) ? 1 : 2)*len + 1) );
    if (!CS0) return;

    CS0[0] = compID;
    *_CS0 = CS0;
    // init loop
    CS0++;
    unicodeIndex = 0;
    Buff = UName->Buffer;
    if (compID == UDF_COMP_ID_16) {
        // Loop through all the bytes.
        while (unicodeIndex < len) {
            // Move the 2nd byte to the low bits of the compressed unicode char.
            *CS0 = (uint8)((*Buff) >> 8);
            CS0++;
            *CS0 = (uint8)(*Buff);
            CS0++;
            Buff++;
            unicodeIndex++;
        }
    } else {
        // Loop through all the bytes.
        while (unicodeIndex < len) {
            *CS0 = (uint8)(*Buff);
            CS0++;
            Buff++;
            unicodeIndex++;
        }
    }
} // end UDFCompressUnicode()

/*
NTSTATUS UDFFindFile__(IN PVCB Vcb,
                                IN BOOLEAN IgnoreCase,
                                IN PUNICODE_STRING Name,
                                IN PUDF_FILE_INFO DirInfo)
          see 'Udf_info.h'
*/

/* File IDs encode a physical ICB within this mount. A virtual partition has
 * no arithmetic inverse. Read its actual tag, authenticate the descriptor,
 * then prove that the tag's virtual slot maps back to this exact sector.
 * Called without the FCB-table FAST_MUTEX; no FI/Dloc owner is acquired here. */
NTSTATUS
UDFResolvePhysicalIcbAddress(PIRP_CONTEXT IrpContext, PVCB Vcb,
                             uint32 Physical, lb_addr* Address)
{
    uint32 Ref, Logical, Slot;
    uint32* Vat;
    int8* Sector = NULL;
    NTSTATUS Status = STATUS_FILE_CORRUPT_ERROR;
    if (!Address || !Vcb->Partitions || Physical >= LBA_OUT_OF_EXTENT ||
        Physical > Vcb->SessionEndLba)
        return STATUS_INVALID_PARAMETER;
    Vat = (uint32*)InterlockedCompareExchangePointer((PVOID volatile*)&Vcb->Vat, NULL, NULL);
    if (!Vat) {
        Ref = UDFGetRefPartNumByPhysLba(Vcb, Physical);
        if (Ref >= Vcb->PartitionMaps || Ref > MAXUSHORT)
            return STATUS_INVALID_PARAMETER;
        Logical = UDFPhysLbaToPart(Vcb, Ref, Physical);
        if (Logical == LBA_OUT_OF_EXTENT)
            return STATUS_INVALID_PARAMETER;
        Address->partitionReferenceNum = (uint16)Ref;
        Address->logicalBlockNum = Logical;
        return UDFPartLbaToPhys(Vcb, Address) == Physical ?
               STATUS_SUCCESS : STATUS_FILE_CORRUPT_ERROR;
    }
    if (Vcb->VatPartNdx >= Vcb->PartitionMaps || Vcb->VatPartNdx > MAXUSHORT ||
        Vcb->VatPhysPartNdx >= Vcb->PartitionMaps || Vcb->VatPhysPartNdx > MAXUSHORT ||
        !Vcb->VatCapacity || Vcb->VatCapacity > MAXULONG / sizeof(uint32) ||
        Vcb->VatCount > Vcb->VatCapacity || Physical < Vcb->VatPartitionRoot ||
        Physical - Vcb->VatPartitionRoot >= Vcb->VatPartitionLength ||
        Vcb->SectorSize < 512 || Vcb->SectorSize > 0x10000 || Vcb->SectorShift >= 30 ||
        Vcb->SectorSize != (1UL << Vcb->SectorShift))
        return STATUS_FILE_CORRUPT_ERROR;
    Sector = (int8*)MyAllocatePoolTag__(NonPagedPool, Vcb->SectorSize, MEM_FE_TAG);
    if (!Sector)
        return STATUS_INSUFFICIENT_RESOURCES;
    _SEH2_TRY {
        uint16 Ident;
        Status = UDFReadSectors(IrpContext, Vcb, FALSE, Physical, 1, FALSE, Sector);
        if (Status != STATUS_SUCCESS) {
            if (NT_SUCCESS(Status)) Status = STATUS_IO_DEVICE_ERROR;
            _SEH2_LEAVE;
        }
        Ident = UdfDescriptorRead16((const unsigned char*)Sector);
        Slot = UdfDescriptorRead32((const unsigned char*)Sector + 12);
        if (Ident != TID_FILE_ENTRY && Ident != TID_EXTENDED_FILE_ENTRY) {
            Status = STATUS_FILE_CORRUPT_ERROR;
            _SEH2_LEAVE;
        }
        if (!UdfValidateDescriptor((const unsigned char*)Sector, Vcb->SectorSize, Ident, Slot)) {
            Status = STATUS_CRC_ERROR;
            _SEH2_LEAVE;
        }
        if (Slot < Vcb->VatCount && Vat[Slot] != UDF_VAT_FREE_ENTRY &&
            Vat[Slot] < Vcb->VatPartitionLength &&
            (uint64)Vcb->VatPartitionRoot + Vat[Slot] == Physical) {
            Address->partitionReferenceNum = (uint16)Vcb->VatPartNdx;
            Address->logicalBlockNum = Slot;
        } else {
            /* Physical ICBs (including the VAT's own ICB) remain distinct
             * from virtual slots, even if their numeric LBNs coincide. */
            Ref = Vcb->VatPhysPartNdx;
            Logical = UDFPhysLbaToPart(Vcb, Ref, Physical);
            if (Logical == LBA_OUT_OF_EXTENT || Logical != Slot) {
                Status = STATUS_FILE_CORRUPT_ERROR;
                _SEH2_LEAVE;
            }
            Address->partitionReferenceNum = (uint16)Ref;
            Address->logicalBlockNum = Logical;
        }
        Status = UDFPartLbaToPhys(Vcb, Address) == Physical ?
                 STATUS_SUCCESS : STATUS_WRONG_VOLUME;
    } _SEH2_FINALLY {
        MyFreePool__(Sector);
    } _SEH2_END;
    return Status;
}

/*
    This routine reads (Extended)FileEntry according to FileDesc
 */
NTSTATUS
UDFReadFileEntry(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN long_ad* Icb,
    IN OUT PFILE_ENTRY FileEntry, // here we can also get ExtendedFileEntry
    IN OUT uint16* Ident
    )
{
    NTSTATUS status;

    if (!NT_SUCCESS(status = UDFReadTagged(IrpContext, Vcb, (int8*)FileEntry,
                         UDFPartLbaToPhys(Vcb,&(Icb->extLocation)),
                         Icb->extLocation.logicalBlockNum,
                         Ident))) return status;
    if ((FileEntry->descTag.tagIdent != TID_FILE_ENTRY) &&
       (FileEntry->descTag.tagIdent != TID_EXTENDED_FILE_ENTRY)) {
        UDFPrint(("  Not a FileEntry (lbn=%x, tag=%x)\n", Icb->extLocation.logicalBlockNum, FileEntry->descTag.tagIdent));
        return STATUS_FILE_CORRUPT_ERROR;
    }
    return STATUS_SUCCESS;
} // UDFReadFileEntry()

/*
    Decides whether character passed is an illegal character for a
    DOS file name.
*/
BOOLEAN
UDFIsIllegalChar(
    IN WCHAR Char,
    IN BOOLEAN IsStream
    )
{
    if (Char < 0xFF &&
        !FsRtlIsAnsiCharacterLegal((UCHAR)Char,
                                   IsStream ? FSRTL_NTFS_STREAM_LEGAL : FSRTL_HPFS_LEGAL)) {

        return FALSE;
    }

    return TRUE;
} // end UDFIsIllegalChar()

/*
    Translate udfName to dosName using OSTA compliant.
    dosName must be a unicode string with min length of 12.
 */

/*void UDFDOSName__(
    IN PVCB Vcb,
    IN OUT PUNICODE_STRING DosName,
    IN PUNICODE_STRING UdfName,
    IN PUDF_FILE_INFO FileInfo
    )
{
    BOOLEAN         KeepIntact;

    KeepIntact = (FileInfo && (FileInfo->Index < 2));
    UDFDOSName(Vcb, DosName, UdfName, KeepIntact);
}*/

void
__fastcall
UDFDOSName(
    IN PVCB Vcb,
    IN OUT PUNICODE_STRING DosName,
    IN PUNICODE_STRING UdfName,
    IN BOOLEAN KeepIntact
    )
{
    switch(Vcb->UdfRevision) {
    case 0x0100:
    case 0x0101:
    case 0x0102:
        UDFDOSName100(DosName, UdfName, KeepIntact);
        break;

    case 0x0150:
        // in general, we need bytes-from-media to
        // create valid UDF 1.50 name.
        // Curently it is impossible, thus,  we'll use
        // UDF 2.00 translation algorithm
    case 0x0200:
        UDFDOSName200(DosName, UdfName, KeepIntact, Vcb->UdfRevision == 0x0150);
        break;

    case 0x0201:
    default:
        UDFDOSName201(DosName, UdfName, KeepIntact);
    }
}

void
__fastcall
UDFDOSName100(
    IN OUT PUNICODE_STRING DosName,
    IN PUNICODE_STRING UdfName,
    IN BOOLEAN KeepIntact
    )
{
    PWCHAR dosName = DosName->Buffer;
    PWCHAR udfName = UdfName->Buffer;
    uint32 udfLen = UdfName->Length / sizeof(WCHAR);

    uint32 index, dosIndex = 0, extIndex = 0, lastPeriodIndex;
    BOOLEAN needsCRC = FALSE, hasExt = FALSE, writingExt = FALSE, isParent = FALSE;
    uint32 valueCRC;
    WCHAR ext[DOS_EXT_LEN], current;

    if (KeepIntact &&
       (udfLen <= 2) && (udfName[0] == UNICODE_PERIOD)) {
        isParent = TRUE;
        if ((udfLen == 2) && (udfName[1] != UNICODE_PERIOD))
            isParent = FALSE;
    }

    for (index = 0 ; index < udfLen ; index++) {
        current = udfName[index];
        if (current == UNICODE_PERIOD && !isParent) {
            if (dosIndex==0 || hasExt) {
                // Ignore leading periods or any other than used for extension.
                needsCRC = TRUE;
            } else {
                // First, find last character which is NOT a period or space.
                lastPeriodIndex = udfLen - 1;
                while(lastPeriodIndex >=0 &&
                     (udfName[lastPeriodIndex] == UNICODE_PERIOD ||
                      udfName[lastPeriodIndex] == UNICODE_SPACE))
                    lastPeriodIndex--;
                // Now search for last remaining period.
                while(lastPeriodIndex >= 0 &&
                      udfName[lastPeriodIndex] != UNICODE_PERIOD)
                    lastPeriodIndex--;
                // See if the period we found was the last or not.
                if (lastPeriodIndex != index)
                    needsCRC = TRUE; // If not, name needs translation.
                // As long as the period was not trailing,
                // the file name has an extension.
                if (lastPeriodIndex >= 0) hasExt = TRUE;
            }
        } else {
            if ((!hasExt && dosIndex == DOS_NAME_LEN) ||
                 extIndex == DOS_EXT_LEN) {
                // File name or extension is too long for DOS.
                needsCRC = TRUE;
            } else {
                if (current == UNICODE_SPACE) { // Ignore spaces.
                    needsCRC = TRUE;
                } else {
                    // Look for illegal or unprintable characters.
                    if (UDFIsIllegalChar(current, FALSE) /*|| !UnicodeIsPrint(current)*/) {
                        needsCRC = TRUE;
                        current = ILLEGAL_CHAR_MARK;
                        /* Skip Illegal characters(even spaces),
                        * but not periods.
                        */
                        while(index+1 < udfLen &&
                              (UDFIsIllegalChar(udfName[index+1], FALSE) /*||
                              !UnicodeIsPrint(udfName[index+1])*/) &&
                              udfName[index+1] != UNICODE_PERIOD)
                            index++;
                    }
                    // Add current char to either file name or ext.
                    if (writingExt) {
                        ext[extIndex] = current;
                        extIndex++;
                    } else {
                        dosName[dosIndex] = current;
                        dosIndex++;
                    }
                }
            }
        }
        // See if we are done with file name, either because we reached
        // the end of the file name length, or the final period.
        if (!writingExt && hasExt && (dosIndex == DOS_NAME_LEN ||
            index == lastPeriodIndex)) {
            // If so, and the name has an extension, start reading it.
            writingExt = TRUE;
            // Extension starts after last period.
            index = lastPeriodIndex;
        }
    }
    //
    if (needsCRC) {
        // Add CRC to end of file name or at position 4.
        if (dosIndex >4) dosIndex = 4;
        valueCRC = UDFUnicodeCksum(udfName, udfLen);
        // set CRC prefix
        dosName[dosIndex] = UNICODE_CRC_MARK;
        // Convert 12-bit CRC to hex characters.
        dosName[dosIndex+1] = hexChar[(valueCRC & 0x0f00) >> 8];
        dosName[dosIndex+2] = hexChar[(valueCRC & 0x00f0) >> 4];
        dosName[dosIndex+3] = hexChar[(valueCRC & 0x000f)];
        dosIndex+=4;
    }
    // Add extension, if any.
    if (extIndex != 0) {
        dosName[dosIndex] = UNICODE_PERIOD;
        dosIndex++;
        for (index = 0; index < extIndex; index++) {
            dosName[dosIndex] = ext[index];
            dosIndex++;
        }
    }
    DosName->Length = (uint16)dosIndex*sizeof(WCHAR);
    RtlUpcaseUnicodeString(DosName, DosName, FALSE);
} // end UDFDOSName100()

void
__fastcall
UDFDOSName200(
    IN OUT PUNICODE_STRING DosName,
    IN PUNICODE_STRING UdfName,
    IN BOOLEAN KeepIntact,
    IN BOOLEAN Mode150
    )
{
    PWCHAR dosName = DosName->Buffer;
    PWCHAR udfName = UdfName->Buffer;
    uint32 udfLen = UdfName->Length / sizeof(WCHAR);

    uint32 index, dosIndex = 0, extIndex = 0, lastPeriodIndex;
    BOOLEAN needsCRC = FALSE, hasExt = FALSE, writingExt = FALSE, isParent = FALSE;
    uint32 valueCRC;
    WCHAR ext[DOS_EXT_LEN], current;

    if (KeepIntact &&
       (udfLen <= 2) && (udfName[0] == UNICODE_PERIOD)) {
        isParent = TRUE;
        if ((udfLen == 2) && (udfName[1] != UNICODE_PERIOD))
            isParent = FALSE;
    }

    for (index = 0 ; index < udfLen ; index++) {
        current = udfName[index];
        if (current == UNICODE_PERIOD && !isParent) {
            if (dosIndex==0 || hasExt) {
                // Ignore leading periods or any other than used for extension.
                needsCRC = TRUE;
            } else {
                // First, find last character which is NOT a period or space.
                lastPeriodIndex = udfLen - 1;
                while(lastPeriodIndex >=0 &&
                     (udfName[lastPeriodIndex] == UNICODE_PERIOD ||
                      udfName[lastPeriodIndex] == UNICODE_SPACE))
                    lastPeriodIndex--;
                // Now search for last remaining period.
                while(lastPeriodIndex >= 0 &&
                      udfName[lastPeriodIndex] != UNICODE_PERIOD)
                    lastPeriodIndex--;
                // See if the period we found was the last or not.
                if (lastPeriodIndex != index)
                    needsCRC = TRUE; // If not, name needs translation.
                // As long as the period was not trailing,
                // the file name has an extension.
                if (lastPeriodIndex >= 0) hasExt = TRUE;
            }
        } else {
            if ((!hasExt && dosIndex == DOS_NAME_LEN) ||
                 extIndex == DOS_EXT_LEN) {
                // File name or extension is too long for DOS.
                needsCRC = TRUE;
            } else {
                if (current == UNICODE_SPACE) { // Ignore spaces.
                    needsCRC = TRUE;
                } else {
                    // Look for illegal or unprintable characters.
                    if (UDFIsIllegalChar(current, FALSE) /*|| !UnicodeIsPrint(current)*/) {
                        needsCRC = TRUE;
                        current = ILLEGAL_CHAR_MARK;
                        /* Skip Illegal characters(even spaces),
                        * but not periods.
                        */
                        while(index+1 < udfLen &&
                              (UDFIsIllegalChar(udfName[index+1], FALSE) /*||
                              !UnicodeIsPrint(udfName[index+1])*/) &&
                              udfName[index+1] != UNICODE_PERIOD)
                            index++;
                    }
                    // Add current char to either file name or ext.
                    if (writingExt) {
                        ext[extIndex] = current;
                        extIndex++;
                    } else {
                        dosName[dosIndex] = current;
                        dosIndex++;
                    }
                }
            }
        }
        // See if we are done with file name, either because we reached
        // the end of the file name length, or the final period.
        if (!writingExt && hasExt && (dosIndex == DOS_NAME_LEN ||
            index == lastPeriodIndex)) {
            // If so, and the name has an extension, start reading it.
            writingExt = TRUE;
            // Extension starts after last period.
            index = lastPeriodIndex;
        }
    }
    // Now handle CRC if needed.
    if (needsCRC) {
        // Add CRC to end of file name or at position 4.
        if (dosIndex >4) dosIndex = 4;
        valueCRC = Mode150 ? UDFUnicodeCksum150(udfName, udfLen) : UDFUnicodeCksum(udfName, udfLen);
        // Convert 16-bit CRC to hex characters.
        dosName[dosIndex] = hexChar[(valueCRC & 0xf000) >> 12];
        dosName[dosIndex+1] = hexChar[(valueCRC & 0x0f00) >> 8];
        dosName[dosIndex+2] = hexChar[(valueCRC & 0x00f0) >> 4];
        dosName[dosIndex+3] = hexChar[(valueCRC & 0x000f)];
        dosIndex+=4;
    }
    // Add extension, if any.
    if (extIndex != 0) {
        dosName[dosIndex] = UNICODE_PERIOD;
        dosIndex++;
        for (index = 0; index < extIndex; index++) {
            dosName[dosIndex] = ext[index];
            dosIndex++;
        }
    }
    DosName->Length = (uint16)dosIndex*sizeof(WCHAR);
    RtlUpcaseUnicodeString(DosName, DosName, FALSE);
} // end UDFDOSName200()


void
__fastcall
UDFDOSName201(
    IN OUT PUNICODE_STRING DosName,
    IN PUNICODE_STRING UdfName,
    IN BOOLEAN KeepIntact
    )
{
    PWCHAR dosName = DosName->Buffer;
    PWCHAR udfName = UdfName->Buffer;
    uint16 udfLen = UdfName->Length / sizeof(WCHAR);

    int32 index;
    uint16 dosIndex = 0;
    //uint16 extIndex = 0;
    BOOLEAN needsCRC = FALSE, isParent = FALSE;
    //BOOLEAN hasExt = FALSE, writingExt = FALSE;
    uint16 valueCRC;
    WCHAR ext[DOS_EXT_LEN];
    WCHAR current;

    if (KeepIntact &&
       (udfLen <= 2) && (udfName[0] == UNICODE_PERIOD)) {
        isParent = TRUE;
        if ((udfLen == 2) && (udfName[1] != UNICODE_PERIOD))
            isParent = FALSE;
    }

    #define DOS_CRC_LEN 4
    #define DOS_CRC_MODULUS 41

    int16 crcIndex;
    uint16 extLen;
    uint16 nameLen;
    uint16 charLen;
    int16 overlayBytes;
    int16 bytesLeft;

    /* Start at the end of the UDF file name and scan for a period */
    /* ('.'). This will be where the DOS extension starts (if */
    /* any). */
    index = udfLen;
    while (index-- > 0) {
        if (udfName[index] == '.')
            break;
    }
    if ((index < 0) || isParent) {
        /* There name was scanned to the beginning of the buffer */
        /* and no extension was found. */
        extLen = 0;
        nameLen = udfLen;
    } else {
        /* A DOS extension was found, process it first. */
        extLen = udfLen - index - 1;
        nameLen = index;
        dosIndex = 0;
        bytesLeft = DOS_EXT_LEN;
        while (++index < udfLen && bytesLeft > 0) {
            /* Get the current character and convert it to upper */
            /* case. */
            current = udfName[index];
            if (current == ' ') {
                /* If a space is found, a CRC must be appended to */
                /* the mangled file name. */
                needsCRC = TRUE;
            } else {
                /* Determine if this is a valid file name char and */
                /* calculate its corresponding BCS character byte */
                /* length (zero if the char is not legal or */
                /* undisplayable on this system). */

                charLen = (UDFIsIllegalChar(current, FALSE)
                           /*|| !UnicodeIsPrint(current)*/) ? 0 : 1;

                /* If the char is larger than the available space */
                /* in the buffer, pretend it is undisplayable. */
                if (charLen > bytesLeft)
                    charLen = 0;
                if (charLen == 0) {
                /* Undisplayable or illegal characters are */
                /* substituted with an underscore ("_"), and */
                /* required a CRC code appended to the mangled */
                /* file name. */
                needsCRC = TRUE;
                charLen = 1;
                current = '_';
                /* Skip over any following undiplayable or */
                /* illegal chars. */
                while (index +1 <udfLen &&
                        (UDFIsIllegalChar(udfName[index+1], FALSE)
                       /*|| !UnicodeIsPrint(udfName[index+1])*/))
                    index++;
                }
                /* Assign the resulting char to the next index in */
                /* the extension buffer and determine how many BCS */
                /* bytes are left. */
                ext[dosIndex++] = current;
                bytesLeft -= charLen;
            }
        }
        /* Save the number of Unicode characters in the extension */
        extLen = dosIndex;
        /* If the extension was too large, or it was zero length */
        /* (i.e. the name ended in a period), a CRC code must be */
        /* appended to the mangled name. */
        if (index < udfLen || extLen == 0)
            needsCRC = TRUE;
    }
    /* Now process the actual file name. */
    index = 0;
    dosIndex = 0;
    crcIndex = 0;
    overlayBytes = -1;
    bytesLeft = DOS_NAME_LEN;
    while (index < nameLen && bytesLeft > 0) {
        /* Get the current character and convert it to upper case. */
        current = udfName[index];
        if (current ==' ' || (current == '.' && !isParent) ) {
            /* Spaces and periods are just skipped, a CRC code */
            /* must be added to the mangled file name. */
            needsCRC = TRUE;
        } else {
            /* Determine if this is a valid file name char and */
            /* calculate its corresponding BCS character byte */
            /* length (zero if the char is not legal or */
            /* undisplayable on this system). */

            charLen = (UDFIsIllegalChar(current, FALSE)
                       /*|| !UnicodeIsPrint(current)*/) ? 0 : 1;

            /* If the char is larger than the available space in */
            /* the buffer, pretend it is undisplayable. */
            if (charLen > bytesLeft)
                charLen = 0;

            if (charLen == 0) {
                /* Undisplayable or illegal characters are */
                /* substituted with an underscore ("_"), and */
                /* required a CRC code appended to the mangled */
                /* file name. */
                needsCRC = TRUE;
                charLen = 1;
                current = '_';
                /* Skip over any following undisplayable or illegal */
                /* chars. */
                while (index +1 <nameLen &&
                       (UDFIsIllegalChar(udfName[index+1], FALSE)
                        /*|| !UnicodeIsPrint(udfName[index+1])*/))
                    index++;
                /* Terminate loop if at the end of the file name. */
                if (index >= nameLen)
                    break;
            }
            /* Assign the resulting char to the next index in the */
            /* file name buffer and determine how many BCS bytes */
            /* are left. */
            dosName[dosIndex++] = current;
            bytesLeft -= charLen;
            /* This figures out where the CRC code needs to start */
            /* in the file name buffer. */
            if (bytesLeft >= DOS_CRC_LEN) {
                /* If there is enough space left, just tack it */
                /* onto the end. */
                crcIndex = dosIndex;
            } else {
                /* If there is not enough space left, the CRC */
                /* must overlay a character already in the file */
                /* name buffer. Once this condition has been */
                /* met, the value will not change. */
                if (overlayBytes < 0) {
                    /* Determine the index and save the length of */
                    /* the BCS character that is overlayed. It */
                    /* is possible that the CRC might overlay */
                    /* half of a two-byte BCS character depending */
                    /* upon how the character boundaries line up. */
                    overlayBytes = (bytesLeft + charLen > DOS_CRC_LEN)?1 :0;
                    crcIndex = dosIndex - 1;
                }
            }
        }
        /* Advance to the next character. */
        index++;
    }
    /* If the scan did not reach the end of the file name, or the */
    /* length of the file name is zero, a CRC code is needed. */
    if (index < nameLen || index == 0)
        needsCRC = TRUE;

    /* If the name has illegal characters or and extension, it */
    /* is not a DOS device name. */

/*    if (needsCRC == FALSE && extLen == 0) { */
        /* If this is the name of a DOS device, a CRC code should */
        /* be appended to the file name.
        if (IsDeviceName(udfName, udfLen))
            needsCRC = TRUE;
    }*/

    /* Append the CRC code to the file name, if needed. */
    if (needsCRC) {
        /* Get the CRC value for the original Unicode string */
        valueCRC = UDFUnicodeCksum(udfName, udfLen);

        /* begin. */
        dosIndex = crcIndex;
        /* If the character being overlayed is a two-byte BCS */
        /* character, replace the first byte with an underscore. */
        if (overlayBytes > 0)
            dosName[dosIndex++] = '_';
        /* Append the encoded CRC value with delimiter. */
        dosName[dosIndex++] = '#';
        dosName[dosIndex++] =
            crcChar[valueCRC / (DOS_CRC_MODULUS * DOS_CRC_MODULUS)];
        valueCRC %= DOS_CRC_MODULUS * DOS_CRC_MODULUS;
            dosName[dosIndex++] =
        crcChar[valueCRC / DOS_CRC_MODULUS];
            valueCRC %= DOS_CRC_MODULUS;
        dosName[dosIndex++] = crcChar[valueCRC];
    }
    /* Append the extension, if any. */
    if (extLen > 0) {
        /* Tack on a period and each successive byte in the */
        /* extension buffer. */
        dosName[dosIndex++] = '.';
        for (index = 0; index < extLen; index++)
            dosName[dosIndex++] = ext[index];
    }
    /* Return the length of the resulting Unicode string. */
    DosName->Length = (uint16)dosIndex*sizeof(WCHAR);
    RtlUpcaseUnicodeString(DosName, DosName, FALSE);

} // end UDFDOSName201()

/*
    This routine initializes Tag structure. It must be called after all
    manual settings to generate valid CRC & Checksum
 */
void
UDFSetUpTag(
    IN PVCB Vcb,
    IN tag* Tag,
    IN uint16 DataLen,  // total length of descriptor _including_ icbTag
    IN uint32 TagLoc,
    IN uint16 skip)
{
    uint32 i;
    int8* tb;

    AdPrint(("UDF: SetTag Loc=%x(%x), tagIdent=%x, skip=%x\n", TagLoc, Tag->tagLocation, Tag->tagIdent, skip));

    AdPrint(
        ("Vcb->NSRDesc=%x, DataLen=%x, sizeof(tag)=%x descCRCLength=%x\n", Vcb->NSRDesc, DataLen, sizeof(tag),
         Tag->descCRCLength));
    if (DataLen)
        DataLen -= sizeof(tag); // Don't do this here (not all DataLen include the tag)
    //    int8* Data = ((int8*)Tag) + sizeof(tag);
    // Ecma-167 states, that all implementations
    // shall set this field to '3' even if
    // disc contains descriptors recorded with
    // value '2'
    // But we should ignore this to make happy othe UDF implementations :(
    Tag->descVersion = (Vcb->NSRDesc & VRS_NSR03_FOUND) ? 3 : 2;
    Tag->tagLocation = TagLoc;
    Tag->tagSerialNum = (uint16)(Vcb->SerialNumber);
    Tag->descCRCLength = DataLen;
    Tag->descCRC = UDFCrc((uint8*)(Tag + 1 + skip), DataLen - skip, (skip ? Tag->descCRC : 0));
    Tag->tagChecksum = 0;
    tb = ((int8*)Tag);
    for (i=0; i<sizeof(tag); i++,tb++)
        Tag->tagChecksum += (i!=4) ? (*tb) : 0;
    AdPrint(
        ("UDF tag: descVersion=%x tagSerialNum=%x descCRCLength=%x descCRC=%x tagChecksum=%x\n", Tag->descVersion,
         Tag->tagSerialNum, Tag->descCRCLength, Tag->descCRC, Tag->tagChecksum));
} // end UDFSetUpTag()

static VOID
UDFRetainFailedCreateConstruction(PVCB Vcb, PUDF_FILE_INFO FileInfo,
                                 PUDF_FILE_INFO Parent, NTSTATUS Error);

/*
    This routine builds FileEntry & associated AllocDescs for specified
    extent. Before Dloc publication, all buffers and bitmap claims are
    private; the VCB lease owns the claims even if this stack unwinds.
 */
NTSTATUS
UDFBuildFileEntry(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN PUDF_FILE_INFO DirInfo,
    IN PUDF_FILE_INFO FileInfo,
    IN uint32 PartNum,
    IN uint16 AllocMode, // short/long/ext/in-icb
    IN uint32 ExtAttrSz,
    IN BOOLEAN Extended
    )
{
    _SEH2_VOLATILE PFILE_ENTRY FileEntry = NULL;
    _SEH2_VOLATILE PEXTENT_MAP WorkMap = NULL;
    _SEH2_VOLATILE PUDF_ALLOCATION_FAILURE Lease = NULL;
    _SEH2_VOLATILE NTSTATUS status = STATUS_UNSUCCESSFUL;
    EntityID* eID;
    uint32 l, feAllocLen, MapBytes;
    EXTENT_INFO Borrowed, Work;
    uint16* lcp;

    if (!Vcb || !Vcb->Partitions || PartNum >= Vcb->PartitionMaps ||
        !FileInfo || FileInfo->Dloc || FileInfo->RefCount || FileInfo->Constructing)
        return STATUS_INVALID_PARAMETER;
    ASSERT(PartNum < Vcb->PartitionMaps);
    ASSERT(!ExtAttrSz);
    // calculate the length required
    l = Extended ? sizeof(EXTENDED_FILE_ENTRY) : sizeof(FILE_ENTRY);
    if (l > Vcb->SectorSize || ExtAttrSz > Vcb->SectorSize - l)
        return STATUS_INVALID_PARAMETER;
    l += ExtAttrSz;
    feAllocLen = Vcb->SectorSize;
    RtlZeroMemory(&Borrowed, sizeof(Borrowed));
    RtlZeroMemory(&Work, sizeof(Work));
    _SEH2_TRY {
    _SEH2_TRY {
    /* Do every fallible buffer allocation before indexing a constructor.
     * In particular no empty Dloc needs a guessed successful-open hold
     * merely because the FE buffer allocation failed. */
    FileEntry = (PFILE_ENTRY)MyAllocatePoolTag__(NonPagedPool, feAllocLen, MEM_FE_TAG);
    if (!FileEntry) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        _SEH2_LEAVE;
    }
    RtlZeroMemory((int8*)FileEntry, feAllocLen);
    status = UDFAllocateFESpaceLease(IrpContext, Vcb, DirInfo, PartNum,
                                    &Borrowed, l, (PUDF_ALLOCATION_FAILURE*)&Lease);
    if (status != STATUS_SUCCESS) _SEH2_LEAVE;
    if (!Lease || !Borrowed.Mapping ||
        !Borrowed.Mapping[0].extLocation ||
        (uint64)UDFGetExtentLength(Borrowed.Mapping) != Vcb->SectorSize) {
        status = STATUS_FILE_CORRUPT_ERROR;
        _SEH2_LEAVE;
    }
    status = UDFGetMappingByteCount(Borrowed.Mapping, &MapBytes);
    if (status != STATUS_SUCCESS) _SEH2_LEAVE;
    WorkMap = (PEXTENT_MAP)MyAllocatePoolTag__(NonPagedPool, MapBytes, MEM_EXTMAP_TAG);
    if (!WorkMap) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        _SEH2_LEAVE;
    }
    RtlCopyMemory(WorkMap, Borrowed.Mapping, MapBytes);
    Work = Borrowed;
    Work.Mapping = WorkMap;
    // set up in-memory FE structure
    FileEntry->icbTag.flags = AllocMode;
    FileEntry->icbTag.fileType = UDF_FILE_TYPE_REGULAR;
    FileEntry->icbTag.numEntries = 1;
//    if (DirInfo && DirInfo->Dloc && DirInfo->Dloc
    FileEntry->icbTag.strategyType = 4;
//    FileEntry->icbTag.strategyParameter = 0;
    FileEntry->descTag.tagIdent = Extended ? TID_EXTENDED_FILE_ENTRY : TID_FILE_ENTRY;
    FileEntry->descTag.tagLocation = UDFPhysLbaToPart(Vcb, PartNum, WorkMap[0].extLocation);
    FileEntry->uid = -1;
    FileEntry->gid = -1;

    if (Extended) {
        eID = &(((PEXTENDED_FILE_ENTRY)FileEntry)->impIdent);
        lcp = &(((PEXTENDED_FILE_ENTRY)FileEntry)->fileLinkCount);
        ((PEXTENDED_FILE_ENTRY)FileEntry)->checkpoint = 1;
    } else {
        eID = &(FileEntry->impIdent);
        lcp = &(FileEntry->fileLinkCount);
        ((PFILE_ENTRY)FileEntry)->checkpoint = 1;
    }

    UDFSetEntityID_imp(eID, UDF_ID_DEVELOPER);

    *lcp = 0;

    /* Require-new checks uniqueness under the same resource that publishes
     * the Dloc; a separate Debug search provides no additional invariant. */
    status = UDFCreateDloc(Vcb, FileInfo, WorkMap[0].extLocation);
    if (status != STATUS_SUCCESS) {
        /* A cached generation at a supposedly free LBA contradicts bitmap
         * ownership. Do not return that possibly live block to free space. */
        if (status == STATUS_FILE_CORRUPT_ERROR) Lease->Uncertain = TRUE;
        _SEH2_LEAVE;
    }
    FileInfo->Dloc->FELoc = Work;
    WorkMap = NULL; /* only the indexed Dloc owns this mutable map */
    FileInfo->Dloc->FileEntry = (tag*)FileEntry;
    FileEntry = NULL;
    FileInfo->Dloc->FileEntryCapacity = feAllocLen;
    FileInfo->Dloc->FileEntryLen = l;

    FileInfo->Dloc->FE_Flags |= UDF_FE_FLAG_FE_MODIFIED;

    status = UDFPublishAllocationLease(Vcb, Lease);
    if (status == STATUS_SUCCESS)
        status = UDFCommitAllocationLease(Vcb, (PUDF_ALLOCATION_FAILURE*)&Lease);
    } _SEH2_EXCEPT(FsRtlIsNtstatusExpected(_SEH2_GetExceptionCode()) ?
                  EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        status = _SEH2_GetExceptionCode();
    } _SEH2_END;
    } _SEH2_FINALLY {
        BOOLEAN Abnormal = _SEH2_AbnormalTermination();
        _SEH2_TRY {
            if ((Abnormal || status != STATUS_SUCCESS) && FileInfo->Dloc) {
                /* Publication may have completed inside CreateDloc before
                 * an exception was returned. Attach the prepared buffers,
                 * retain the real constructor, and never roll its blocks
                 * back behind an indexed generation. No namespace I/O has
                 * been performed by this constructor. */
                if (WorkMap && !FileInfo->Dloc->FELoc.Mapping) {
                    FileInfo->Dloc->FELoc = Work;
                    WorkMap = NULL;
                }
                if (FileEntry && !FileInfo->Dloc->FileEntry) {
                    FileInfo->Dloc->FileEntry = (tag*)FileEntry;
                    FileInfo->Dloc->FileEntryCapacity = feAllocLen;
                    FileInfo->Dloc->FileEntryLen = l;
                    FileEntry = NULL;
                }
                if (Lease) Lease->Uncertain = TRUE;
                UDFRetainFailedCreateConstruction(Vcb, FileInfo, DirInfo,
                    Abnormal ? STATUS_UNSUCCESSFUL : status);
            }
        } _SEH2_FINALLY {
            _SEH2_TRY {
                if (Lease) {
                    NTSTATUS AbortStatus = UDFAbortAllocationLease(Vcb,
                        (PUDF_ALLOCATION_FAILURE*)&Lease,
                        Abnormal ? STATUS_UNSUCCESSFUL : status);
                    if (!Abnormal && status == STATUS_SUCCESS && AbortStatus != STATUS_SUCCESS)
                        status = AbortStatus;
                    if (Lease) {
                        /* A refused inventory lookup does not transfer or
                         * free the VCB's still-active claim owner. */
                        Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
                        InterlockedCompareExchange(&Vcb->FailedAllocationError,
                            STATUS_FILE_CORRUPT_ERROR, STATUS_SUCCESS);
                    }
                }
            } _SEH2_FINALLY {
                if (WorkMap) MyFreePool__(WorkMap);
                if (FileEntry) MyFreePool__(FileEntry);
            } _SEH2_END;
        } _SEH2_END;
    } _SEH2_END;
    return status;
} // end UDFBuildFileEntry()

/*
    This routine builds ExtentInfo for specified (Ex)FileEntry & associated
    AllocDescs
 */
NTSTATUS
UDFLoadExtInfo(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN PFILE_ENTRY fe,
    IN PLONG_AD fe_loc,
    IN OUT PEXTENT_INFO FExtInfo,
    IN OUT PEXTENT_INFO AExtInfo
    )
{
    /* The parser owns all temporary data/AED buffers until success. Keep
     * its I/O/CRC/resource/unsupported status instead of a generic failure. */
    return UDFReadMappingFromXEntry(IrpContext, Vcb, fe_loc, (const tag*)fe,
                                    FExtInfo, AExtInfo);
} // end UDFLoadExtInfo()

/*
    This routine builds FileIdent for specified FileEntry.
    We shall call UDFSetUpTag after all other initializations
    This structure is a precise copy of on-disk FileIdent
    structure. All modifications of it (including memory block
    size) are reflected on Directory extent. This, allocation of
    too long block (without changes in ImpUseLen) will lead to
    unreadable Directory
 */
NTSTATUS
UDFBuildFileIdent(
    IN PVCB Vcb,
    IN PUNICODE_STRING fn,
    IN PLONG_AD FileEntryIcb,       // virtual address of FileEntry
    IN uint32 ImpUseLen,
    OUT PFILE_IDENT_DESC* _FileId,
    OUT uint32* FileIdLen
    )
{
    PFILE_IDENT_DESC FileId;
    uint8* CS0;
    SIZE_T Nlen;
    uint32 l;
    *_FileId = NULL;
    *FileIdLen = 0;
    if (!fn || !FileEntryIcb || ImpUseLen > MAXUSHORT ||
        (fn->Length && !fn->Buffer) || fn->Length % sizeof(WCHAR))
        return STATUS_INVALID_PARAMETER;
    // prepare filename
    UDFCompressUnicode(fn, &CS0, &Nlen);
    if (!CS0) return STATUS_INSUFFICIENT_RESOURCES;
    if (Nlen < 2) {
        Nlen = 0;
    } else
    if (Nlen > UDF_NAME_LEN) {
        if (CS0) MyFreePool__(CS0);
        return STATUS_OBJECT_NAME_INVALID;
    }
    if (ImpUseLen > MAXUSHORT - sizeof(FILE_IDENT_DESC) - Nlen - 3) {
        MyFreePool__(CS0);
        return STATUS_OBJECT_NAME_INVALID;
    }
    // allocate memory for FI
    l = (sizeof(FILE_IDENT_DESC) + Nlen + ImpUseLen + 3) & ~((uint32)3);
    FileId = (PFILE_IDENT_DESC)MyAllocatePoolTag__(NonPagedPool, l, MEM_FID_TAG);
    if (!FileId) {
        if (CS0) MyFreePool__(CS0);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    // fill FI structure
    RtlZeroMemory( (int8*)FileId, l);
    RtlCopyMemory( ((int8*)(FileId+1))+ImpUseLen, CS0, Nlen);
    FileId->descTag.tagIdent = TID_FILE_IDENT_DESC;
    FileId->fileVersionNum = 1;
    FileId->lengthOfImpUse = (uint16)ImpUseLen;
    FileId->lengthFileIdent = (uint8)Nlen;
    FileId->icb = *FileEntryIcb;
    *_FileId = FileId;
    *FileIdLen = l;

    if (CS0) MyFreePool__(CS0);
    return STATUS_SUCCESS;
} // end UDFBuildFileIdent()

/* Only the creating thread can serialize its prepared generations. The
 * base's temporary parent-link update cannot escape before its child is
 * durable and its complete streamDirectoryICB is ready. A failed context
 * preserves its original error instead of allowing cleanup to publish it. */
static NTSTATUS
UDFStreamCreateWriteStatus(PUDF_FILE_INFO FileInfo)
{
    PUDF_STREAM_CREATE_CONTEXT Context;
    if (!FileInfo || !FileInfo->Dloc) return STATUS_FILE_INVALID;
    Context = FileInfo->Dloc->StreamCreate;
    if (!Context) return STATUS_SUCCESS;
    if (!NT_SUCCESS(Context->FirstError)) return Context->FirstError;
    if (Context->Thread != PsGetCurrentThread()) return STATUS_DEVICE_BUSY;
    if (Context->Child && Context->Child->Dloc == FileInfo->Dloc &&
        Context->Phase >= UdfStreamCreateChildOpen) return STATUS_SUCCESS;
    if (Context->BaseFile && Context->BaseFile->Dloc == FileInfo->Dloc &&
        Context->Phase >= UdfStreamCreateBasePrepared) return STATUS_SUCCESS;
    return STATUS_DEVICE_BUSY;
}

static
void
UDFAddToStreamOwnerSize(
    IN PUDF_FILE_INFO StreamInfo,
    IN int64 Delta
)
{
    if (!Delta || 
        !UDFIsAStream(StreamInfo) || 
        !StreamInfo->ParentFile->ParentFile
     )
        return;

    PUDF_DATALOC_INFO OwnerDloc = StreamInfo->ParentFile->ParentFile->Dloc;
    if (OwnerDloc && OwnerDloc->FileEntry &&
        (OwnerDloc->FileEntry->tagIdent == TID_EXTENDED_FILE_ENTRY))
    {
        ((PEXTENDED_FILE_ENTRY)(OwnerDloc->FileEntry))->objectSize += Delta;
        OwnerDloc->FE_Flags |= UDF_FE_FLAG_FE_MODIFIED;
    }
}

/*
    This routine sets informationLength field in (Ext)FileEntry
 */
void
UDFSetFileSize(
    IN PUDF_FILE_INFO FileInfo,
    IN int64 Size
    )
{
    int64 StreamsSize;
    uint16 Ident;
//    PDIR_INDEX_ITEM DirIndex;

    ValidateFileInfo(FileInfo);
    AdPrint(("UDFSetFileSize: %I64x, FI %x\n", Size, FileInfo));

    //AdPrint(("  Dloc %x\n", FileInfo->Dloc));
    FileInfo->Dloc->FE_Flags |= UDF_FE_FLAG_FE_MODIFIED;
    //AdPrint(("  FileEntry %x\n", FileInfo->Dloc->FileEntry));
    Ident = FileInfo->Dloc->FileEntry->tagIdent;
    //AdPrint(("  Ident %x\n", Ident));
    if (Ident == TID_FILE_ENTRY) {
        PFILE_ENTRY fe = (PFILE_ENTRY)(FileInfo->Dloc->FileEntry);
        //AdPrint(("  fe %x\n", fe));
        fe->informationLength = Size;
    } else if (Ident == TID_EXTENDED_FILE_ENTRY) {
        PEXTENDED_FILE_ENTRY fe = (PEXTENDED_FILE_ENTRY)(FileInfo->Dloc->FileEntry);
        //AdPrint(("  ext-fe %x\n", fe));
        UDFPrint(("informationLength %x objectSize %x\n", Size, fe->objectSize));
        StreamsSize = (fe->objectSize > fe->informationLength) ?
                            (fe->objectSize - fe->informationLength) : 0;
        UDFAddToStreamOwnerSize(FileInfo, Size - fe->informationLength);
        fe->informationLength = Size;
        fe->objectSize = fe->informationLength + StreamsSize;
    }
/*    if (DirIndex = UDFDirIndex(UDFGetDirIndexByFileInfo(FileInfo),FileInfo->Index) ) {
        DirIndex->FileSize = Size;
    }*/
    //AdPrint(("UDFSetFileSize: ok\n"));
    return;
} // end UDFSetFileSize()

void
UDFSetFileSizeInDirNdx(
    IN PVCB Vcb,
    IN PUDF_FILE_INFO FileInfo,
    IN int64* ASize
    )
{
    uint16 Ident;
    PDIR_INDEX_ITEM DirIndex;

    ValidateFileInfo(FileInfo);
    if (ASize) {
        AdPrint(("UDFSetFileSizeInDirNdx: %I64x\n", *ASize));
    } else {
        AdPrint(("UDFSetFileSizeInDirNdx: sync\n"));
    }

    DirIndex = UDFDirIndex(UDFGetDirIndexByFileInfo(FileInfo),FileInfo->Index);
    if (!DirIndex)
       return;

    Ident = FileInfo->Dloc->FileEntry->tagIdent;
    if (Ident == TID_FILE_ENTRY) {
        PFILE_ENTRY fe = (PFILE_ENTRY)(FileInfo->Dloc->FileEntry);
        DirIndex->FileSize = fe->informationLength;
        if (ASize) {
            DirIndex->AllocationSize = *ASize;
//        } else {
//            DirIndex->AllocationSize = (fe->informationLength + Vcb->LBlockSize - 1) & ~(Vcb->LBlockSize - 1);
        }
    } else if (Ident == TID_EXTENDED_FILE_ENTRY) {
        PEXTENDED_FILE_ENTRY fe = (PEXTENDED_FILE_ENTRY)(FileInfo->Dloc->FileEntry);
        DirIndex->FileSize = fe->informationLength;
        if (ASize) {
            DirIndex->AllocationSize = *ASize;
//        } else {
//            DirIndex->AllocationSize = (fe->informationLength + Vcb->LBlockSize - 1) & ~(Vcb->LBlockSize - 1);
        }
    }
    return;
} // end UDFSetFileSizeInDirNdx()

/*
    This routine gets informationLength field in (Ext)FileEntry
 */
int64
UDFGetFileSize(
    IN PUDF_FILE_INFO FileInfo
    )
{
    uint16 Ident;

    ValidateFileInfo(FileInfo);

    Ident = FileInfo->Dloc->FileEntry->tagIdent;
    if (Ident == TID_FILE_ENTRY) {
        PFILE_ENTRY fe = (PFILE_ENTRY)(FileInfo->Dloc->FileEntry);
        return fe->informationLength;
    } else if (Ident == TID_EXTENDED_FILE_ENTRY) {
        PEXTENDED_FILE_ENTRY fe = (PEXTENDED_FILE_ENTRY)(FileInfo->Dloc->FileEntry);
        return fe->informationLength;
    }
    return (-1);
} // end UDFGetFileSize()

int64
UDFGetFileSizeFromDirNdx(
    IN PVCB Vcb,
    IN PUDF_FILE_INFO FileInfo
    )
{
    PDIR_INDEX_ITEM DirIndex;

    ValidateFileInfo(FileInfo);

    DirIndex = UDFDirIndex(UDFGetDirIndexByFileInfo(FileInfo),FileInfo->Index);
    if (!DirIndex)
       return -1;

    return DirIndex->FileSize;
} // end UDFGetFileSizeFromDirNdx()

/*
    This routine sets lengthAllocDesc field in (Ext)FileEntry
 */
NTSTATUS
UDFValidateFileEntryBuffer(PVCB Vcb, PUDF_FILE_INFO FileInfo, uint32* HeaderLength)
{
    PUDF_DATALOC_INFO Dloc;
    uint32 Minimum, Attributes;
    uint16 Ident;
    if (!HeaderLength) return STATUS_INVALID_PARAMETER;
    *HeaderLength = 0;
    if (!Vcb || !FileInfo || !(Dloc = FileInfo->Dloc) || !Dloc->FileEntry ||
        !Vcb->SectorSize || Vcb->SectorShift >= 30 ||
        Vcb->SectorSize != ((uint32)1 << Vcb->SectorShift) ||
        Dloc->FileEntryCapacity < Vcb->SectorSize || Dloc->FileEntryCapacity < sizeof(tag))
        return STATUS_FILE_CORRUPT_ERROR;
    Ident = Dloc->FileEntry->tagIdent;
    if (Ident == TID_FILE_ENTRY) {
        Minimum = sizeof(FILE_ENTRY);
        if (Minimum > Vcb->SectorSize) return STATUS_FILE_CORRUPT_ERROR;
        Attributes = ((PFILE_ENTRY)Dloc->FileEntry)->lengthExtendedAttr;
    } else if (Ident == TID_EXTENDED_FILE_ENTRY) {
        Minimum = sizeof(EXTENDED_FILE_ENTRY);
        if (Minimum > Vcb->SectorSize) return STATUS_FILE_CORRUPT_ERROR;
        Attributes = ((PEXTENDED_FILE_ENTRY)Dloc->FileEntry)->lengthExtendedAttr;
    } else {
        return STATUS_FILE_CORRUPT_ERROR;
    }
    if (Attributes > Vcb->SectorSize - Minimum ||
        Dloc->FileEntryLen != Minimum + Attributes)
        return STATUS_FILE_CORRUPT_ERROR;
    *HeaderLength = Minimum + Attributes;
    return STATUS_SUCCESS;
}

NTSTATUS
UDFSetAllocDescLen(PVCB Vcb, PUDF_FILE_INFO FileInfo)
{
    PUDF_DATALOC_INFO Dloc;
    uint32 Header, Width, Available, Value;
    uint32 Previous;
    uint16 Mode;
    NTSTATUS Status = UDFValidateFileEntryBuffer(Vcb, FileInfo, &Header);
    if (!NT_SUCCESS(Status)) return Status;
    Dloc = FileInfo->Dloc;
    Mode = ((PFILE_ENTRY)Dloc->FileEntry)->icbTag.flags & ICB_FLAG_ALLOC_MASK;
    if (Mode == ICB_FLAG_AD_IN_ICB) {
        if (Dloc->DataLoc.Length < 0 || Dloc->DataLoc.Offset != Header ||
            (uint64)Dloc->DataLoc.Length > Vcb->SectorSize - Header ||
            Dloc->AllocLoc.Mapping || Dloc->AllocLoc.Length || Dloc->AllocLoc.Offset)
            return STATUS_FILE_CORRUPT_ERROR;
        Value = (uint32)Dloc->DataLoc.Length;
    } else if (Mode == ICB_FLAG_AD_SHORT || Mode == ICB_FLAG_AD_LONG) {
        Width = Mode == ICB_FLAG_AD_SHORT ? sizeof(SHORT_AD) : sizeof(LONG_AD);
        if (!Dloc->AllocLoc.Mapping || !Dloc->FELoc.Mapping ||
            Dloc->AllocLoc.Length < 0 || (uint64)Dloc->AllocLoc.Length > MAXULONG ||
            Dloc->AllocLoc.Offset != Header ||
            Dloc->AllocLoc.Mapping[0].extLocation != Dloc->FELoc.Mapping[0].extLocation ||
            (Dloc->AllocLoc.Mapping[0].extLength & UDF_EXTENT_LENGTH_MASK) < Vcb->SectorSize ||
            (Dloc->AllocLoc.Mapping[0].extLength >> 30) > EXTENT_NOT_RECORDED_ALLOCATED)
            return STATUS_FILE_CORRUPT_ERROR;
        Available = (Vcb->SectorSize - Header) / Width * Width;
        Value = (uint32)min((uint64)Available, (uint64)Dloc->AllocLoc.Length);
        if (Value % Width || ((uint64)Dloc->AllocLoc.Length > Available && Available < Width))
            return STATUS_FILE_CORRUPT_ERROR;
    } else {
        return STATUS_NOT_SUPPORTED;
    }
    if (Dloc->FileEntry->tagIdent == TID_FILE_ENTRY) {
        Previous = ((PFILE_ENTRY)Dloc->FileEntry)->lengthAllocDescs;
        ((PFILE_ENTRY)Dloc->FileEntry)->lengthAllocDescs = Value;
    } else {
        Previous = ((PEXTENDED_FILE_ENTRY)Dloc->FileEntry)->lengthAllocDescs;
        ((PEXTENDED_FILE_ENTRY)Dloc->FileEntry)->lengthAllocDescs = Value;
    }
    if (Previous != Value) Dloc->FE_Flags |= UDF_FE_FLAG_FE_MODIFIED;
    return STATUS_SUCCESS;
} // end UDFSetAllocDescLen()

/*
    This routine changes fileLinkCount field in (Ext)FileEntry
 */
void
UDFChangeFileLinkCount(
    IN PUDF_FILE_INFO FileInfo,
    IN BOOLEAN Increase
    )
{
    uint16 Ident;

    ValidateFileInfo(FileInfo);

    FileInfo->Dloc->FE_Flags |= UDF_FE_FLAG_FE_MODIFIED;
    Ident = FileInfo->Dloc->FileEntry->tagIdent;
    if (Ident == TID_FILE_ENTRY) {
        PFILE_ENTRY fe = (PFILE_ENTRY)(FileInfo->Dloc->FileEntry);
        if (Increase) {
            fe->fileLinkCount++;
        } else {
            fe->fileLinkCount--;
        }
        if (fe->fileLinkCount & 0x8000)
            fe->fileLinkCount = 0xffff;
        return;
    } else if (Ident == TID_EXTENDED_FILE_ENTRY) {
        PEXTENDED_FILE_ENTRY fe = (PEXTENDED_FILE_ENTRY)(FileInfo->Dloc->FileEntry);
        if (Increase) {
            fe->fileLinkCount++;
        } else {
            fe->fileLinkCount--;
        }
        if (fe->fileLinkCount & 0x8000)
            fe->fileLinkCount = 0xffff;
        return;
    }
    return;
} // end UDFChangeFileLinkCount()

/*
    This routine gets fileLinkCount field from (Ext)FileEntry
 */
uint16
UDFGetFileLinkCount(
    IN PUDF_FILE_INFO FileInfo
    )
{
    uint16 Ident;

    ValidateFileInfo(FileInfo);

    if (!FileInfo->Dloc->FileEntry)
        return 1;
    Ident = FileInfo->Dloc->FileEntry->tagIdent;
    if (Ident == TID_FILE_ENTRY) {
        return ((PFILE_ENTRY)(FileInfo->Dloc->FileEntry))->fileLinkCount;
    } else if (Ident == TID_EXTENDED_FILE_ENTRY) {
        return ((PEXTENDED_FILE_ENTRY)(FileInfo->Dloc->FileEntry))->fileLinkCount;
    }
    return UDF_INVALID_LINK_COUNT;
} // end UDFGetFileLinkCount()

uint16
UDFGetFileNameLinkCount(
    IN PUDF_FILE_INFO FileInfo
)
{
    uint16 LinkCount;
    
    LinkCount = UDFGetFileLinkCount(FileInfo);

    if (LinkCount && (LinkCount != UDF_INVALID_LINK_COUNT) &&
        !UDFIsAStreamDir(FileInfo) && 
        UDFHasAStreamDir(FileInfo) &&
        !UDFIsSDirDeleted(FileInfo->Dloc->SDirInfo)
      ) {
        LinkCount--;
    }

    return LinkCount;
}

#ifdef UDF_CHECK_UTIL
/*
    This routine sets fileLinkCount field in (Ext)FileEntry
 */
void
UDFSetFileLinkCount(
    IN PUDF_FILE_INFO FileInfo,
    uint16 LinkCount
    )
{
    uint16 Ident;

    ValidateFileInfo(FileInfo);

    if (!FileInfo->Dloc->FileEntry)
        return;
    Ident = FileInfo->Dloc->FileEntry->tagIdent;
    if (Ident == TID_FILE_ENTRY) {
        ((PFILE_ENTRY)(FileInfo->Dloc->FileEntry))->fileLinkCount = LinkCount;
    } else if (Ident == TID_EXTENDED_FILE_ENTRY) {
        ((PEXTENDED_FILE_ENTRY)(FileInfo->Dloc->FileEntry))->fileLinkCount = LinkCount;
    }
    return;
} // end UDFSetFileLinkCount()
#endif //UDF_CHECK_UTIL

/*
    This routine gets lengthExtendedAttr field in (Ext)FileEntry
 */
uint32
UDFGetFileEALength(
    IN PUDF_FILE_INFO FileInfo
    )
{
    uint16 Ident;

    ValidateFileInfo(FileInfo);

    if (!FileInfo->Dloc->FileEntry)
        return 1;
    Ident = FileInfo->Dloc->FileEntry->tagIdent;

    if (Ident == TID_FILE_ENTRY) {
        return ((PFILE_ENTRY)(FileInfo->Dloc->FileEntry))->lengthExtendedAttr;
    } else if (Ident == TID_EXTENDED_FILE_ENTRY) {
        return ((PEXTENDED_FILE_ENTRY)(FileInfo->Dloc->FileEntry))->lengthExtendedAttr;
    }
    return 0;
} // end UDFGetFileEALength()

/*
    This routine sets UniqueID field in (Ext)FileEntry
 */
NTSTATUS
UDFAllocateFileUID(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    OUT PULONGLONG UniqueId
    )
{
    ULONGLONG next, after, oldEnd, newEnd;
    NTSTATUS status;

    *UniqueId = 0;
    /* Make the volume's OPEN checkpoint durable before an ID can reach an
     * FE or FID.  The bitmap resource also serializes the 64-bit counter on
     * i386 and all LVID writers, including the clean-close path. */
    status = UDFOpenIntegrityForWrite(IrpContext, Vcb, FALSE);
    if (!NT_SUCCESS(status))
        return status;

    UDFAcquireResourceExclusive(&(Vcb->BitMapResource1), TRUE);
    next = Vcb->NextUniqueId;
    if ((uint32)next < 16)
        next += 16 - (uint32)next;
    if (next == ~(ULONGLONG)0) {
        status = STATUS_INTEGER_OVERFLOW;
        goto release;
    }
    after = next + 1;
    if ((uint32)after == 0)
        after += 16;

    if (Vcb->LvidJournalState == 1 &&
        after > Vcb->UniqueIdReservedEnd) {
        oldEnd = Vcb->UniqueIdReservedEnd;
        newEnd = after > ~(ULONGLONG)0 - 4096 ?
                 ~(ULONGLONG)0 : after + 4096;
        Vcb->UniqueIdReservedEnd = newEnd;
        status = UDFUpdateLogicalVolInt(IrpContext, Vcb, FALSE);
        if (NT_SUCCESS(status))
            status = UDFBarrierFlushDevice(Vcb->TargetDeviceObject);
        if (!NT_SUCCESS(status)) {
            Vcb->UniqueIdReservedEnd = oldEnd;
            Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
            goto release;
        }
    }
    Vcb->NextUniqueId = after;
    *UniqueId = next;
    status = STATUS_SUCCESS;

release:
    UDFReleaseResource(&(Vcb->BitMapResource1));
    return status;
}

void
UDFSetFileUID(
    IN PUDF_FILE_INFO FileInfo,
    IN ULONGLONG UniqueId
    )
{
    uint16 Ident;

    ValidateFileInfo(FileInfo);

/*    UID = FileInfo->Dloc->FELoc.Mapping[0].extLocation |
          ( FileInfo->ParentFile ? (((int64)(FileInfo->ParentFile->Dloc->FELoc.Mapping[0].extLocation)) << 32) : 0);*/

    FileInfo->Dloc->FE_Flags |= UDF_FE_FLAG_FE_MODIFIED;
    Ident = FileInfo->Dloc->FileEntry->tagIdent;
    if (Ident == TID_FILE_ENTRY) {
        PFILE_ENTRY fe = (PFILE_ENTRY)(FileInfo->Dloc->FileEntry);
        fe->uniqueID = UniqueId;
    } else if (Ident == TID_EXTENDED_FILE_ENTRY) {
        PEXTENDED_FILE_ENTRY fe = (PEXTENDED_FILE_ENTRY)(FileInfo->Dloc->FileEntry);
        fe->uniqueID = UniqueId;
    }
    if (FileInfo->FileIdent)
        ((struct FidADImpUse*)&(FileInfo->FileIdent->icb.impUse))->uniqueID = (uint32)UniqueId;
    return;
} // end UDFSetFileUID()

/*
    This routine gets UniqueID field in (Ext)FileEntry
 */
__inline
int64
UDFGetFileUID_(
    IN tag* FileEntry
    )
{
    uint16 Ident;

    Ident = FileEntry->tagIdent;
    if (Ident == TID_FILE_ENTRY) {
        PFILE_ENTRY fe = (PFILE_ENTRY)(FileEntry);
        return fe->uniqueID;
    } else if (Ident == TID_EXTENDED_FILE_ENTRY) {
        PEXTENDED_FILE_ENTRY fe = (PEXTENDED_FILE_ENTRY)(FileEntry);
        return fe->uniqueID;
    }
    return (-1);
} // end UDFGetFileUID()

int64
UDFGetFileUID(
    IN PUDF_FILE_INFO FileInfo
    )
{
    ValidateFileInfo(FileInfo);

    return UDFGetFileUID_(FileInfo->Dloc->FileEntry);
} // end UDFGetFileUID()

void
UDFChangeFileCounter(
    IN PVCB Vcb,
    IN BOOLEAN FileCounter,
    IN BOOLEAN Increase
    )
{
    uint32* counter;

    counter = FileCounter ?
        &(Vcb->numFiles) :
        &(Vcb->numDirs);
    if (*counter == (ULONG)-1)
        return;
    if (Increase) {
        InterlockedIncrement((int32*)counter);
    } else {
        InterlockedDecrement((int32*)counter);
    }

} // end UDFChangeFileCounter()

void
UDFSetEntityID_imp_(
    IN EntityID* eID,
    IN uint8* Str,
    IN uint32 Len
    )
{
    impIdentSuffix* iis;

    RtlZeroMemory(eID, sizeof(EntityID));
    RtlCopyMemory( (int8*)&(eID->ident), Str, min(Len, sizeof(eID->ident)) );

    iis = (impIdentSuffix*)&(eID->identSuffix);
    iis->OSClass = UDF_OS_CLASS_WINNT;
    iis->OSIdent = UDF_OS_ID_WINNT;

} // end UDFSetEntityID_imp_()

void
UDFReadEntityID_Domain(
    PVCB Vcb,
    EntityID* eID
    )
{
    domainIdentSuffix* dis;

    dis = (domainIdentSuffix*)&(eID->identSuffix);

    UDFPrint(("UDF: Entity Id:\n"));
    UDFPrint(("flags: %x\n", eID->flags));
    UDFPrint(("ident[0]: %x\n", eID->ident[0]));
    UDFPrint(("ident[1]: %x\n", eID->ident[1]));
    UDFPrint(("ident[2]: %x\n", eID->ident[2]));
    UDFPrint(("ident[3]: %x\n", eID->ident[3]));
    UDFPrint(("UDF: Entity Id Domain:\n"));
    // Get current UDF revision
    Vcb->UdfRevision = max(dis->currentRev, Vcb->UdfRevision);
    UDFPrint(("Effective Revision: %x\n", Vcb->UdfRevision));
    if ((Vcb->NSRDesc & VRS_NSR02_FOUND) && (Vcb->NSRDesc & VRS_NSR03_FOUND))
    {
        if (Vcb->UdfRevision >= 0x200)
        {
            Vcb->NSRDesc = VRS_NSR03_FOUND;
        }
        else
        {
            Vcb->NSRDesc = VRS_NSR02_FOUND;
        }
    }

    // Get Read-Only flags

    if (FlagOn(dis->flags, ENTITYID_FLAGS_HARD_RO | ENTITYID_FLAGS_SOFT_RO)) {

        Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
    }

} // end UDFReadEntityID_Domain()

static
NTSTATUS
UDFWriteFileData(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN PUDF_FILE_INFO FileInfo,
    IN int64 Offset,
    IN SIZE_T Length,
    IN BOOLEAN Direct,
    IN int8* Buffer,
    OUT PSIZE_T WrittenBytes
);

/* Prepare the complete inline-to-external write in an unpublished extent.
 * The old inline bytes/size/map remain authoritative on every preparation
 * refusal. The caller already owns Main (or VCB for an internal FI); cached
 * data must have been drained before its Paging acquisition. */
static NTSTATUS
UDFPromoteInlineWrite(PIRP_CONTEXT IrpContext, PVCB Vcb,
    PUDF_FILE_INFO FileInfo, uint32 PartNum, uint32 DataPartNum,
    int64 Offset, SIZE_T Length, int8* Buffer, PSIZE_T WrittenBytes)
{
    PUDF_DATALOC_INFO Dloc = FileInfo->Dloc;
    PFCB Fcb = Dloc->CommonFcb;
    EXTENT_INFO Candidate, AllocPlan;
    PUDF_PRIVATE_ALLOCATION Claims = NULL;
    PEXTENT_MAP OldMap = NULL;
    uint32 Header;
    int64 NewLength = Offset + Length;
    SIZE_T Completed = 0;
    NTSTATUS Status, LeaseStatus;
    BOOLEAN PagingAcquired = FALSE, LiveChanged = FALSE, Installed = FALSE;

    RtlZeroMemory(&Candidate, sizeof(Candidate));
    RtlZeroMemory(&AllocPlan, sizeof(AllocPlan));
    Status = UDFValidateFileEntryBuffer(Vcb, FileInfo, &Header);
    if (!NT_SUCCESS(Status)) return Status;
    if (PartNum >= Vcb->PartitionMaps || DataPartNum >= Vcb->PartitionMaps ||
        Dloc->DataLoc.Length < 0 || Dloc->DataLoc.Offset != Header ||
        (uint64)Dloc->DataLoc.Length > Vcb->SectorSize - Header ||
        NewLength <= (int64)(Vcb->SectorSize - Header) ||
        !Dloc->DataLoc.Mapping || !Dloc->FELoc.Mapping || Dloc->FELoc.Offset ||
        Dloc->DataLoc.Mapping == Dloc->FELoc.Mapping ||
        Dloc->FELoc.Mapping[0].extLength != Vcb->SectorSize || Dloc->FELoc.Mapping[1].extLength ||
        Dloc->DataLoc.Mapping[0].extLength != Vcb->SectorSize || Dloc->DataLoc.Mapping[1].extLength ||
        Dloc->DataLoc.Mapping[0].extLocation != Dloc->FELoc.Mapping[0].extLocation ||
        Dloc->AllocLoc.Mapping || Dloc->AllocLoc.Length || Dloc->AllocLoc.Offset)
        return STATUS_FILE_CORRUPT_ERROR;
    if (Vcb->Vat || Vcb->CDR_Mode || (Vcb->VcbState & VCB_STATE_VOLUME_READ_ONLY))
        return STATUS_MEDIA_WRITE_PROTECTED;
    if (Dloc->FE_Flags & UDF_FE_FLAG_CACHED_DATA_PENDING)
        return STATUS_INVALID_DEVICE_STATE; /* never call Cc while Paging is held */
    if (Fcb) {
        if ((FileInfo->Fcb && FileInfo->Fcb != Fcb) || !Fcb->FcbNonpaged ||
            !Fcb->FileInfo || Fcb->FileInfo->Dloc != Dloc ||
            Fcb->Header.PagingIoResource != &Fcb->FcbNonpaged->FcbPagingIoResource ||
            !ExIsResourceAcquiredExclusiveLite(&Fcb->FcbNonpaged->FcbResource) ||
            (ExIsResourceAcquiredSharedLite(&Fcb->FcbNonpaged->FcbPagingIoResource) &&
             !ExIsResourceAcquiredExclusiveLite(&Fcb->FcbNonpaged->FcbPagingIoResource)))
            return STATUS_INVALID_DEVICE_STATE;
    } else if (FileInfo->Fcb || !ExIsResourceAcquiredExclusiveLite(&Vcb->VcbResource)) {
        return STATUS_INVALID_DEVICE_STATE;
    }
    _SEH2_TRY {
    _SEH2_TRY {
        if (Fcb && !ExIsResourceAcquiredExclusiveLite(&Fcb->FcbNonpaged->FcbPagingIoResource)) {
            UDFAcquirePagingIoExclusive(IrpContext, Fcb);
            PagingAcquired = TRUE;
        }
        Status = UDFFlushDirectoryAppend(IrpContext, Vcb, FileInfo);
        if (!NT_SUCCESS(Status)) goto promotion_exit;
        Status = UDFPreparePrivateExtent(IrpContext, Vcb, DataPartNum, NewLength,
                                          EXTENT_FLAG_ALLOC_STD, &Candidate, &Claims);
        if (!NT_SUCCESS(Status)) goto promotion_exit;
        AllocPlan.Mapping = (PEXTENT_MAP)MyAllocatePoolTag__(NonPagedPool,
                                                       2 * sizeof(EXTENT_MAP), MEM_EXTMAP_TAG);
        if (!AllocPlan.Mapping) {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto promotion_exit;
        }
        RtlZeroMemory(AllocPlan.Mapping, 2 * sizeof(EXTENT_MAP));
        AllocPlan.Mapping[0] = Dloc->FELoc.Mapping[0];
        AllocPlan.Offset = Header;
        AllocPlan.Modified = TRUE;
        if (Dloc->DataLoc.Length) {
            Status = UDFWriteExtent(IrpContext, Vcb, &Candidate, 0,
                (SIZE_T)Dloc->DataLoc.Length, FALSE, (int8*)Dloc->FileEntry + Header, &Completed);
            if (NT_SUCCESS(Status) && Completed != (SIZE_T)Dloc->DataLoc.Length)
                Status = STATUS_IO_DEVICE_ERROR;
            if (!NT_SUCCESS(Status)) goto promotion_exit;
        }
        Completed = 0;
        Status = UDFWriteExtent(IrpContext, Vcb, &Candidate, Offset, Length, FALSE, Buffer, &Completed);
        if (NT_SUCCESS(Status) && Completed != Length) Status = STATUS_IO_DEVICE_ERROR;
        if (!NT_SUCCESS(Status)) goto promotion_exit;
        /* Untouched new sectors remain unrecorded (logical zero), while
         * partial recorded sector padding was initialized by WriteExtent. */
        Status = UDFPublishPrivateExtent(Vcb, Claims);
        if (!NT_SUCCESS(Status)) goto promotion_exit;
        LiveChanged = TRUE;
        OldMap = Dloc->DataLoc.Mapping;
        Dloc->DataLoc = Candidate;
        Candidate.Mapping = NULL;
        Dloc->AllocLoc = AllocPlan;
        AllocPlan.Mapping = NULL;
        ((PFILE_ENTRY)Dloc->FileEntry)->icbTag.flags &= ~ICB_FLAG_ALLOC_MASK;
        ((PFILE_ENTRY)Dloc->FileEntry)->icbTag.flags |=
            PartNum == DataPartNum ? ICB_FLAG_AD_SHORT : ICB_FLAG_AD_LONG;
        if (Dloc->FileEntry->tagIdent == TID_FILE_ENTRY)
            ((PFILE_ENTRY)Dloc->FileEntry)->lengthAllocDescs = 0;
        else
            ((PEXTENDED_FILE_ENTRY)Dloc->FileEntry)->lengthAllocDescs = 0;
        UDFSetFileSize(FileInfo, NewLength);
        if (Fcb) Fcb->FcbState &= ~UDF_FCB_EMBEDDED_DATA;
        Installed = TRUE;
promotion_exit:
        NOTHING;
    } _SEH2_EXCEPT(FsRtlIsNtstatusExpected(_SEH2_GetExceptionCode()) ?
                  EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        Status = _SEH2_GetExceptionCode();
    } _SEH2_END;
    } _SEH2_FINALLY {
        if (_SEH2_AbnormalTermination()) Status = STATUS_FILE_CORRUPT_ERROR;
        LeaseStatus = UDFCompletePrivateExtent(Vcb, &Claims,
            Installed && !_SEH2_AbnormalTermination(), Status);
        if (NT_SUCCESS(Status) && !NT_SUCCESS(LeaseStatus)) Status = LeaseStatus;
        if (OldMap) MyFreePool__(OldMap);
        if (Candidate.Mapping) MyFreePool__(Candidate.Mapping);
        if (AllocPlan.Mapping) MyFreePool__(AllocPlan.Mapping);
        if (LiveChanged && !NT_SUCCESS(Status)) {
            Dloc->FE_Flags |= UDF_FE_FLAG_FE_MODIFIED;
            Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
            UDFRememberMetadataWritebackError(IrpContext, Vcb, FileInfo, Status);
        }
        if (PagingAcquired) UDFReleasePagingIo(IrpContext, Fcb);
    } _SEH2_END;
    if (NT_SUCCESS(Status)) *WrittenBytes = Length;
    return Status;
}

/* Allocation capacity is not logical EOF. Retain any newly owned tail as
 * preallocation while preparing the gap and payload, then publish only the
 * initialized/accepted logical range. No best-effort free/resize rollback. */
static NTSTATUS
UDFWriteExternalExtension(PIRP_CONTEXT IrpContext, PVCB Vcb,
    PUDF_FILE_INFO FileInfo, uint32 DataPartNum, int64 Offset, SIZE_T Length,
    BOOLEAN Direct, int8* Buffer, PSIZE_T WrittenBytes)
{
    PUDF_DATALOC_INFO Dloc = FileInfo->Dloc;
    int64 OldLength = Dloc->DataLoc.Length, End = Offset + Length;
    int64 Position = OldLength, VisibleEnd;
    int64 Allocated = UDFGetExtentLength(Dloc->DataLoc.Mapping);
    SIZE_T Chunk, Zeroed;
    NTSTATUS Status = STATUS_SUCCESS;
    BOOLEAN PayloadStarted = FALSE, Exceptional = FALSE;

    if (Dloc->DataLoc.Offset || Allocated < OldLength || End <= OldLength)
        return STATUS_FILE_CORRUPT_ERROR;
    if (Direct && End > Allocated) return STATUS_INVALID_PARAMETER;
    _SEH2_TRY {
    _SEH2_TRY {
        if (End > Allocated) {
            Status = UDFResizeExtent(IrpContext, Vcb, FileInfo, DataPartNum,
                                      End, FALSE, &Dloc->DataLoc);
            if (!NT_SUCCESS(Status)) goto external_exit;
            /* Private growth has transferred a complete owned map, but it
             * must not publish the requested full logical length yet. */
            Dloc->DataLoc.Length = OldLength;
            Dloc->DataLoc.Modified = TRUE;
        }
        while (Position < Offset) {
            /* Bound the SIZE_T call on i386. Full unrecorded/new sectors
             * already read zero; recorded gaps become unrecorded, and only
             * partial recorded sector tails need actual zero writes. */
            Chunk = (SIZE_T)min((uint64)(Offset - Position), (uint64)(16 * 1024 * 1024));
            Zeroed = 0;
            Status = UDFZeroExtent(IrpContext, Vcb, &Dloc->DataLoc, Position,
                                   Chunk, FALSE, Direct, &Zeroed);
            if (Status == STATUS_SUCCESS && Zeroed != Chunk) Status = STATUS_IO_DEVICE_ERROR;
            if (Status != STATUS_SUCCESS) goto external_exit;
            Position += Chunk;
        }
        PayloadStarted = TRUE;
        Status = UDFWriteFileData(IrpContext, Vcb, FileInfo, Offset, Length,
                                   Direct, Buffer, WrittenBytes);
        if (Status == STATUS_SUCCESS && *WrittenBytes != Length) Status = STATUS_IO_DEVICE_ERROR;
external_exit:
        if (Status != STATUS_SUCCESS && NT_SUCCESS(Status)) Status = STATUS_IO_DEVICE_ERROR;
    } _SEH2_EXCEPT(FsRtlIsNtstatusExpected(_SEH2_GetExceptionCode()) ?
                  EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        Status = _SEH2_GetExceptionCode();
        Exceptional = TRUE;
    } _SEH2_END;
    } _SEH2_FINALLY {
        if (_SEH2_AbnormalTermination()) {
            Status = STATUS_FILE_CORRUPT_ERROR;
            Exceptional = TRUE;
        }
        if (*WrittenBytes > Length) {
            *WrittenBytes = 0;
            Status = STATUS_IO_DEVICE_ERROR;
            Exceptional = TRUE;
        }
        VisibleEnd = OldLength;
        if (Status == STATUS_SUCCESS)
            VisibleEnd = End; /* includes a successful zero-length resize */
        else if (PayloadStarted && *WrittenBytes)
            VisibleEnd = max(OldLength, Offset + (int64)*WrittenBytes);
        Dloc->DataLoc.Length = VisibleEnd;
        Dloc->DataLoc.Modified = TRUE;
        Dloc->FE_Flags |= UDF_FE_FLAG_FE_MODIFIED;
        UDFSetFileSize(FileInfo, VisibleEnd);
        if (Exceptional) Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
        if (!NT_SUCCESS(Status))
            UDFRememberMetadataWritebackError(IrpContext, Vcb, FileInfo, Status);
    } _SEH2_END;
    return Status;
}

/* Grow metadata only after a complete private inline promotion, or retain
 * the existing external extent owner on a later payload failure. */
NTSTATUS
UDFWriteFile__(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN PUDF_FILE_INFO FileInfo,
    IN int64 Offset,
    IN SIZE_T Length,
    IN BOOLEAN Direct,
    IN int8* Buffer,
    OUT PSIZE_T WrittenBytes
    )
{
    int64 End;
    uint32 PartNum, DataPartNum, Header;
    uint16 Mode;
    PUDF_DATALOC_INFO Dloc;
    NTSTATUS Status;
    BOOLEAN Handled;

    if (WrittenBytes) *WrittenBytes = 0;
    if (!IrpContext || !Vcb || !FileInfo || !(Dloc = FileInfo->Dloc) ||
        !WrittenBytes || Offset < 0 || (uint64)Length > (uint64)MAXLONGLONG - (uint64)Offset ||
        (Length && !Buffer) || !Dloc->FELoc.Mapping || !Dloc->DataLoc.Mapping ||
        !Vcb->Partitions || !Vcb->PartitionMaps || Dloc->DataLoc.Length < 0)
        return STATUS_INVALID_PARAMETER;
    Status = UDFValidateFileEntryBuffer(Vcb, FileInfo, &Header);
    if (!NT_SUCCESS(Status)) return Status;
    PartNum = UDFGetRefPartNumByPhysLba(Vcb, Dloc->FELoc.Mapping[0].extLocation);
    if (PartNum >= Vcb->PartitionMaps) return STATUS_FILE_CORRUPT_ERROR;
    DataPartNum = PartNum;
    if (Vcb->MetadataFileInfo && PartNum == Vcb->MetadataRef && !UDFIsADirectory(FileInfo))
        DataPartNum = Vcb->MetadataPhysRef;
    if (DataPartNum >= Vcb->PartitionMaps) return STATUS_FILE_CORRUPT_ERROR;
    End = Offset + Length;
    Mode = ((PFILE_ENTRY)Dloc->FileEntry)->icbTag.flags & ICB_FLAG_ALLOC_MASK;
    if ((Length || End > Dloc->DataLoc.Length) &&
        (Vcb->VcbState & VCB_STATE_VOLUME_READ_ONLY))
        return STATUS_MEDIA_WRITE_PROTECTED;
    Status = UDFTryDirectoryAppend(IrpContext, Vcb, FileInfo, Offset,
                                  Length, Direct, Buffer, WrittenBytes, &Handled);
    if (!NT_SUCCESS(Status) || Handled) return Status;
    if (End <= Dloc->DataLoc.Length)
        return UDFWriteFileData(IrpContext, Vcb, FileInfo, Offset, Length, Direct, Buffer, WrittenBytes);
    if (Mode == ICB_FLAG_AD_IN_ICB && (uint64)End > Vcb->SectorSize - Header) {
        if (Direct) return STATUS_INVALID_PARAMETER;
        return UDFPromoteInlineWrite(IrpContext, Vcb, FileInfo, PartNum, DataPartNum,
                                     Offset, Length, Buffer, WrittenBytes);
    }
    if (Mode == ICB_FLAG_AD_IN_ICB) {
        if (Dloc->DataLoc.Offset != Header ||
            (uint64)Dloc->DataLoc.Length > Vcb->SectorSize - Header ||
            (uint64)End > Vcb->SectorSize - Header)
            return STATUS_FILE_CORRUPT_ERROR;
        RtlZeroMemory((int8*)Dloc->FileEntry + Header + (SIZE_T)Dloc->DataLoc.Length,
                      (SIZE_T)(End - Dloc->DataLoc.Length));
        UDFSetFileSize(FileInfo, End);
        Dloc->DataLoc.Modified = TRUE;
        Dloc->DataLoc.Length = End;
        return UDFWriteFileData(IrpContext, Vcb, FileInfo, Offset, Length, Direct, Buffer, WrittenBytes);
    }
    if (Mode != ICB_FLAG_AD_SHORT && Mode != ICB_FLAG_AD_LONG)
        return STATUS_NOT_SUPPORTED;
    return UDFWriteExternalExtension(IrpContext, Vcb, FileInfo, DataPartNum,
                                      Offset, Length, Direct, Buffer, WrittenBytes);
} // end UDFWriteFile__()

/* Keep the disk's prepared FREE generation visible across ordinary bitmap
 * writeback while a deleted file's FCB still pins its RAM allocation.  The
 * BitMapResource protects this list and every bitmap serialization below. */
NTSTATUS
UDFOverlayDeletePrefreePhysical(PVCB Vcb, int8* Unallocated,
                                int8* Freed, uint32 BitCount)
{
    PUDF_DELETE_PREFREE record;
    uint32 i, j;
    for (record = Vcb->DeletePrefreeList; record; record = record->Next) {
        for (i = 0; i < record->RangeCount; ++i) {
            PUDF_DELETE_PREFREE_RANGE range = &record->Ranges[i];
            if (range->Metadata) continue;
            if (range->FirstBit >= BitCount ||
                range->BitCount > BitCount - range->FirstBit)
                return STATUS_FILE_CORRUPT_ERROR;
            for (j = 0; j < range->BitCount; ++j) {
                if (Unallocated)
                    UDFSetFreeBit(Unallocated, range->FirstBit + j);
                if (Freed)
                    UDFSetFreeBit(Freed, range->FirstBit + j);
            }
        }
    }
    return STATUS_SUCCESS;
}

NTSTATUS
UDFOverlayDeletePrefreeMetadata(PVCB Vcb, int8* Bitmap,
                                uint32 BitCount)
{
    PUDF_DELETE_PREFREE record;
    uint32 i, j;
    for (record = Vcb->DeletePrefreeList; record; record = record->Next) {
        for (i = 0; i < record->RangeCount; ++i) {
            PUDF_DELETE_PREFREE_RANGE range = &record->Ranges[i];
            if (!range->Metadata) continue;
            if (range->FirstBit >= BitCount ||
                range->BitCount > BitCount - range->FirstBit)
                return STATUS_FILE_CORRUPT_ERROR;
            for (j = 0; j < range->BitCount; ++j)
                Bitmap[(range->FirstBit + j) >> 3] |=
                    (int8)(1U << ((range->FirstBit + j) & 7));
        }
    }
    return STATUS_SUCCESS;
}

/* Locate the exact owned record before either bitmap retirement or list
 * removal. A bounded full traversal also rejects cycles/duplicate links;
 * finding a prefix match alone would not make freeing that node safe. */
static NTSTATUS
UDFLocateDeletePrefree(PVCB Vcb, PUDF_DATALOC_INFO Dloc,
                       PUDF_DELETE_PREFREE** Slot)
{
    PUDF_DELETE_PREFREE* cursor = &Vcb->DeletePrefreeList;
    PUDF_DELETE_PREFREE record = Dloc->DeletePrefree;
    ULONG remaining = Vcb->DeletePrefreeCount;
    *Slot = NULL;
    ASSERT(ExIsResourceAcquiredExclusiveLite(&Vcb->BitMapResource1));
    if (!record || record->Dloc != Dloc || !remaining)
        return STATUS_FILE_CORRUPT_ERROR;
    while (remaining--) {
        if (!*cursor || !(*cursor)->Dloc)
            return STATUS_FILE_CORRUPT_ERROR;
        if (*cursor == record) {
            if (*Slot) return STATUS_FILE_CORRUPT_ERROR;
            *Slot = cursor;
        }
        cursor = &(*cursor)->Next;
    }
    if (*cursor || !*Slot)
        return STATUS_FILE_CORRUPT_ERROR;
    return STATUS_SUCCESS;
}

static NTSTATUS
UDFDetachDeletePrefree(PVCB Vcb, PUDF_DATALOC_INFO Dloc)
{
    PUDF_DELETE_PREFREE* slot;
    PUDF_DELETE_PREFREE record = Dloc->DeletePrefree;
    NTSTATUS status;
    if (!record) return STATUS_SUCCESS;
    status = UDFLocateDeletePrefree(Vcb, Dloc, &slot);
    if (status != STATUS_SUCCESS) {
        Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
        return status; /* retain the record and its actual Dloc owner */
    }
    *slot = record->Next;
    Vcb->DeletePrefreeCount--;
    Dloc->DeletePrefree = NULL;
    MyFreePool__(record);
    return STATUS_SUCCESS;
}

VOID
UDFReleaseDeletePrefreeRecords(PVCB Vcb)
{
    PUDF_DELETE_PREFREE record, next;
    /* Mount can call VCB cleanup before the bitmap resource exists.  A
     * prefree record is created only after a successful writable mount. */
    if (!Vcb->DeletePrefreeList) return;
    UDFAcquireResourceExclusive(&(Vcb->BitMapResource1), TRUE);
    record = Vcb->DeletePrefreeList;
    Vcb->DeletePrefreeList = NULL;
    Vcb->DeletePrefreeCount = 0;
    while (record) {
        next = record->Next;
        if (record->Dloc && record->Dloc->DeletePrefree == record)
            record->Dloc->DeletePrefree = NULL;
        MyFreePool__(record);
        record = next;
    }
    UDFReleaseResource(&(Vcb->BitMapResource1));
}

/* Readers hold independent main/mirror arrays. Do not acquire the bitmap
 * writer resource from a Dloc/namespace reader merely to keep a map alive. */
static BOOLEAN
UDFDeleteRangeOverlapsMetadata(PVCB Vcb, uint32 Lba, uint32 Blocks)
{
    PUDF_METADATA_VIEW View = UDFAcquireMetadataView(Vcb);
    const EXTENT_MAP* Map;
    uint32 i, Copy, Count;
    uint64 First = Lba, End = First + Blocks;
    BOOLEAN Overlap = FALSE;
    if (!View) return TRUE; /* no authoritative map: no prefree */
    for (Copy = 0; Copy < 2 && !Overlap; Copy++) {
        Map = Copy ? View->MirrorMap : View->MainMap;
        Count = Copy ? View->MirrorCount : View->MainCount;
        for (i = 0; i < Count; i++) {
            uint64 MapFirst = Map[i].extLocation;
            uint64 MapEnd = MapFirst +
                ((Map[i].extLength & UDF_EXTENT_LENGTH_MASK) >> View->SectorShift);
            if (First < MapEnd && MapFirst < End) {
                Overlap = TRUE;
                break;
            }
        }
    }
    UDFReleaseMetadataView(View);
    return Overlap;
}

/* Only the exact dense, 512-byte, external-SBD case is prepared here. Other
 * layouts retain the older delete path and still fail closed at recovery if
 * a cut exposes an orphan; they are not claimed as crash-safe by this patch. */
static NTSTATUS
UDFBuildDeletePrefree(PVCB Vcb, PUDF_FILE_INFO FileInfo,
                      PUDF_DELETE_PREFREE* Prepared,
                      uint32* PhysicalBitmapLba,
                      uint32* MetadataBitmapLba)
{
    PUDF_DATALOC_INFO dloc = FileInfo->Dloc;
    PEXTENT_MAP map, alloc, feMap, bitmapMap;
    PUDF_DELETE_PREFREE record;
    uint32 i, count = 1, feLba, metadataLbn, partRoot, blocks;
    SIZE_T bytes;
    BOOLEAN embedded;

    *Prepared = NULL;
    if (Vcb->CDR_Mode || Vcb->BitmapFcb || Vcb->SectorSize != 512 ||
        !Vcb->FSBM_Bitmap || !Vcb->FSBM_OldBitmap ||
        !Vcb->MetadataBitmap || !Vcb->MetadataFileInfo ||
        !Vcb->MetadataBitmapFileInfo ||
        !dloc || dloc->DeletePrefree ||
        !Vcb->PartitionMaps || !Vcb->FSBM_ByteCount ||
        Vcb->MetadataPhysRef != 0 ||
        Vcb->Partitions[0].PartitionType == UDF_METADATA_MAP25 ||
        Vcb->Partitions[0].UspaceBitmap == 0xFFFFFFFF ||
        Vcb->Partitions[0].UspaceBitmap >=
            Vcb->Partitions[0].PartitionLen)
        return STATUS_NOT_SUPPORTED;
    partRoot = Vcb->Partitions[0].PartitionRoot;
    feMap = dloc->FELoc.Mapping;
    if (!feMap || feMap[0].extLength != 512 ||
        (feMap[1].extLength & UDF_EXTENT_LENGTH_MASK))
        return STATUS_NOT_SUPPORTED;
    feLba = feMap[0].extLocation;
    embedded = dloc->DataLoc.Offset != 0;
    if (embedded) {
        /* In-ICB data occupies the FE sector itself.  Only its metadata
         * bitmap bit is released; it has no separate physical data blocks.
         * Reject a transition or extra allocation extent that this bounded
         * transaction cannot describe. */
        map = dloc->DataLoc.Mapping;
        if (!dloc->FileEntry || !map ||
            dloc->DataLoc.Offset != dloc->FileEntryLen ||
            dloc->DataLoc.Length < 0 ||
            (uint64)dloc->DataLoc.Offset +
                (uint64)dloc->DataLoc.Length > Vcb->SectorSize ||
            (((PFILE_ENTRY)dloc->FileEntry)->icbTag.flags &
                ICB_FLAG_ALLOC_MASK) != ICB_FLAG_AD_IN_ICB ||
            dloc->AllocLoc.Mapping || dloc->AllocLoc.Length ||
            map[0].extLocation != feLba || map[0].extLength != 512 ||
            (map[1].extLength & UDF_EXTENT_LENGTH_MASK))
            return STATUS_NOT_SUPPORTED;
    }
    metadataLbn = UDFPhysLbaToMetadata(Vcb, feLba);
    if (metadataLbn == LBA_OUT_OF_EXTENT ||
        metadataLbn >= Vcb->MetadataBitCount ||
        UDFMetadataLbaToPhys(Vcb, metadataLbn, NULL) != feLba)
        return STATUS_NOT_SUPPORTED;

    bitmapMap = Vcb->MetadataBitmapFileInfo->Dloc->DataLoc.Mapping;
    if (Vcb->MetadataBitmapFileInfo->Dloc->DataLoc.Offset ||
        !bitmapMap || (bitmapMap[0].extLength >> 30) !=
            EXTENT_RECORDED_ALLOCATED ||
        (bitmapMap[0].extLength & UDF_EXTENT_LENGTH_MASK) <
            sizeof(SPACE_BITMAP_DESC) + Vcb->MetadataBitmapByteCount ||
        (bitmapMap[1].extLength & UDF_EXTENT_LENGTH_MASK))
        return STATUS_NOT_SUPPORTED;
    *PhysicalBitmapLba = partRoot + Vcb->Partitions[0].UspaceBitmap;
    if (Vcb->FSBM_ByteCount > Vcb->Partitions[0].PartitionLen * 512ULL ||
        (uint64)Vcb->Partitions[0].UspaceBitmap +
            ((uint64)sizeof(SPACE_BITMAP_DESC) +
             Vcb->FSBM_ByteCount + 511) / 512 >
            Vcb->Partitions[0].PartitionLen)
        return STATUS_NOT_SUPPORTED;
    *MetadataBitmapLba = bitmapMap[0].extLocation;
    if (UDFPhysLbaToMetadata(Vcb, *MetadataBitmapLba) != LBA_OUT_OF_EXTENT)
        return STATUS_NOT_SUPPORTED;

    alloc = dloc->AllocLoc.Mapping;
    for (i = 0; alloc && i < 4096 &&
                (alloc[i].extLength & UDF_EXTENT_LENGTH_MASK); ++i) {
        if ((alloc[i].extLength >> 30) !=
                EXTENT_NOT_RECORDED_NOT_ALLOCATED &&
            (alloc[i].extLocation != feLba ||
             (alloc[i].extLength & UDF_EXTENT_LENGTH_MASK) > 512))
            return STATUS_NOT_SUPPORTED;
    }
    if (i == 4096) return STATUS_NOT_SUPPORTED;
    map = embedded ? NULL : dloc->DataLoc.Mapping;
    for (i = 0; map && i < 4096 &&
                (map[i].extLength & UDF_EXTENT_LENGTH_MASK); ++i) {
        uint32 length = map[i].extLength & UDF_EXTENT_LENGTH_MASK;
        uint32 kind = map[i].extLength >> 30;
        if (kind == EXTENT_NOT_RECORDED_NOT_ALLOCATED) continue;
        if (kind != EXTENT_RECORDED_ALLOCATED &&
            kind != EXTENT_NOT_RECORDED_ALLOCATED)
            return STATUS_NOT_SUPPORTED;
        blocks = (length + 511) / 512;
        if (!blocks || !map[i].extLocation ||
            map[i].extLocation < partRoot ||
            map[i].extLocation - partRoot >= Vcb->FSBM_BitCount ||
            blocks > Vcb->FSBM_BitCount -
                     (map[i].extLocation - partRoot) ||
            UDFDeleteRangeOverlapsMetadata(Vcb, map[i].extLocation, blocks))
            return STATUS_NOT_SUPPORTED;
        count++;
    }
    if (i == 4096) return STATUS_NOT_SUPPORTED;
    bytes = sizeof(UDF_DELETE_PREFREE) +
            (SIZE_T)(count - 1) * sizeof(UDF_DELETE_PREFREE_RANGE);
    record = (PUDF_DELETE_PREFREE)MyAllocatePool__(NonPagedPool, bytes);
    if (!record) return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(record, bytes);
    record->Dloc = dloc;
    record->Ranges[0].FirstBit = metadataLbn;
    record->Ranges[0].BitCount = 1;
    record->Ranges[0].Metadata = TRUE;
    record->RangeCount = 1;
    for (i = 0; map && (map[i].extLength & UDF_EXTENT_LENGTH_MASK); ++i) {
        uint32 length = map[i].extLength & UDF_EXTENT_LENGTH_MASK;
        if ((map[i].extLength >> 30) ==
                EXTENT_NOT_RECORDED_NOT_ALLOCATED) continue;
        record->Ranges[record->RangeCount].FirstBit =
            map[i].extLocation - partRoot;
        record->Ranges[record->RangeCount].BitCount =
            (length + 511) / 512;
        record->RangeCount++;
    }
    ASSERT(record->RangeCount == count);
    *Prepared = record;
    return STATUS_SUCCESS;
}

static NTSTATUS
UDFValidateDeletePrefreeBitmaps(PIRP_CONTEXT IrpContext, PVCB Vcb,
                                uint32 PhysicalLba, uint32 MetadataLba,
                                int8* Sector)
{
    PSPACE_BITMAP_DESC desc;
    uint16 ident;
    NTSTATUS status;
    status = UDFReadTagged(IrpContext, Vcb, Sector, PhysicalLba,
                           Vcb->Partitions[0].UspaceBitmap, &ident);
    if (!NT_SUCCESS(status)) return status;
    desc = (PSPACE_BITMAP_DESC)Sector;
    if (ident != TID_SPACE_BITMAP_DESC ||
        desc->descTag.descCRCLength >
            sizeof(SPACE_BITMAP_DESC) - sizeof(tag) ||
        desc->numOfBits != Vcb->FSBM_BitCount ||
        desc->numOfBytes < Vcb->FSBM_ByteCount)
        return STATUS_NOT_SUPPORTED;
    status = UDFReadTagged(IrpContext, Vcb, Sector, MetadataLba,
                           UDFPhysLbaToPart(Vcb, Vcb->MetadataPhysRef,
                                            MetadataLba), &ident);
    if (!NT_SUCCESS(status)) return status;
    desc = (PSPACE_BITMAP_DESC)Sector;
    if (ident != TID_SPACE_BITMAP_DESC ||
        desc->descTag.descCRCLength >
            sizeof(SPACE_BITMAP_DESC) - sizeof(tag) ||
        desc->numOfBits != Vcb->MetadataBitCount ||
        desc->numOfBytes < Vcb->MetadataBitmapByteCount)
        return STATUS_NOT_SUPPORTED;
    return STATUS_SUCCESS;
}

static NTSTATUS
UDFWriteDeletePrefreeBitmaps(PIRP_CONTEXT IrpContext, PVCB Vcb,
                             PUDF_DELETE_PREFREE Record,
                             uint32 PhysicalLba, uint32 MetadataLba,
                             int8* Sector, PBOOLEAN PossiblyModified)
{
    uint32 i;
    for (i = 0; i < Record->RangeCount; ++i) {
        PUDF_DELETE_PREFREE_RANGE range = &Record->Ranges[i];
        uint32 bit = range->FirstBit, left = range->BitCount;
        uint32 base = range->Metadata ? MetadataLba : PhysicalLba;
        while (left) {
            uint32 byteOffset = sizeof(SPACE_BITMAP_DESC) + (bit >> 3);
            uint32 sectorOffset = byteOffset % 512;
            uint32 bits = min(left, (512 - sectorOffset) * 8 - (bit & 7));
            uint32 j;
            SIZE_T written = 0;
            BOOLEAN changed = FALSE;
            NTSTATUS status = UDFReadSectors(IrpContext, Vcb, FALSE,
                                    base + byteOffset / 512, 1, FALSE, Sector);
            if (!NT_SUCCESS(status)) return status;
            for (j = 0; j < bits; ++j) {
                uint32 current = bit + j;
                uint8* byte = (uint8*)Sector +
                    (sizeof(SPACE_BITMAP_DESC) + (current >> 3)) % 512;
                uint8 mask = (uint8)(1U << (current & 7));
                /* The RAM allocator must still reserve every target bit.
                 * A freshly allocated block may have a durable FE before
                 * its bitmap generation is published. Accept an on-disk
                 * FREE bit only if this exact bit has an unflushed allocation
                 * recorded under BitMapResource1; unrelated dirty bits do
                 * not establish provenance. */
                if (range->Metadata) {
                    if (current >= Vcb->MetadataBitCount ||
                        !Vcb->MetadataBitmap || !Vcb->MetadataPendingAlloc ||
                        UDFGetFreeBit(Vcb->MetadataBitmap, current))
                        return STATUS_FILE_CORRUPT_ERROR;
                    if (*byte & mask) {
                        if (!(Vcb->MetadataPendingAlloc[current >> 3] & mask))
                            return STATUS_FILE_CORRUPT_ERROR;
                        continue;
                    }
                } else {
                    if (current >= Vcb->FSBM_BitCount ||
                        !Vcb->FSBM_Bitmap || !Vcb->FSBM_OldBitmap ||
                        UDFGetFreeBit(Vcb->FSBM_Bitmap, current))
                        return STATUS_FILE_CORRUPT_ERROR;
                    if (*byte & mask) {
                        if (!UDFGetFreeBit(Vcb->FSBM_OldBitmap, current))
                            return STATUS_FILE_CORRUPT_ERROR;
                        continue;
                    }
                }
                *byte |= mask;
                changed = TRUE;
            }
            if (changed) {
                *PossiblyModified = TRUE;
                status = UDFWriteSectors(IrpContext, Vcb, FALSE,
                                         base + byteOffset / 512, 1, FALSE,
                                         Sector, &written);
                if (!NT_SUCCESS(status)) return status;
                if (written != 512) return STATUS_IO_DEVICE_ERROR;
            }
            bit += bits;
            left -= bits;
        }
    }
    return STATUS_SUCCESS;
}

static NTSTATUS
UDFReleasePrefreedAllocation(PVCB Vcb, PUDF_FILE_INFO DirInfo,
                             PUDF_FILE_INFO FileInfo)
{
    PUDF_DELETE_PREFREE* slot;
    NTSTATUS Status;
    UDFAcquireResourceExclusive(&(Vcb->BitMapResource1), TRUE);
    _SEH2_TRY {
        Status = UDFLocateDeletePrefree(Vcb, FileInfo->Dloc, &slot);
        if (Status == STATUS_SUCCESS)
            Status = UDFFreeFileAllocation(Vcb, DirInfo, FileInfo);
        if (Status == STATUS_SUCCESS)
            Status = UDFDetachDeletePrefree(Vcb, FileInfo->Dloc);
    } _SEH2_FINALLY {
        if (_SEH2_AbnormalTermination())
            Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
        UDFReleaseResource(&(Vcb->BitMapResource1));
    } _SEH2_END;
    return Status;
}

/* The directory transaction is deliberately narrower than the ordinary-file
 * one: its parent FE and child FID must be distinct, mirrored 512-byte
 * sectors.  Recovery must be able to identify either copy after a cut. */
static BOOLEAN
UDFDeleteHasIndependentMetadataMirror(PVCB Vcb, uint32 MainLba)
{
    PUDF_METADATA_VIEW View = UDFAcquireMetadataView(Vcb);
    uint32 Lbn, MirrorLba;
    SIZE_T Available;
    BOOLEAN Independent = FALSE;
    if (!View) return FALSE;
    if (View->SectorSize == 512 && (View->Flags & UDF_METADATA_DUPLICATED)) {
        Lbn = UDFMetadataViewPhysToLba(View, MainLba);
        if (Lbn != LBA_OUT_OF_EXTENT && Lbn < Vcb->MetadataBitCount &&
            UDFMetadataViewLbaToPhys(View, Lbn, FALSE, NULL) == MainLba) {
            MirrorLba = UDFMetadataViewLbaToPhys(View, Lbn, TRUE, &Available);
            Independent = MirrorLba != LBA_OUT_OF_EXTENT &&
                          MirrorLba != MainLba && Available >= 512;
        }
    }
    UDFReleaseMetadataView(View);
    return Independent;
}

static BOOLEAN
UDFDeleteParentHasOtherDirtyFid(PDIR_INDEX_HDR Index, uint_di Target)
{
    PDIR_INDEX_ITEM item;
    uint_di i;
    for (i = 2; (item = UDFDirIndex(Index, i)); ++i)
        if (i != Target && (item->FI_Flags & UDF_FI_FLAG_FI_MODIFIED))
            return TRUE;
    return FALSE;
}

/* A single empty, inline directory has only its FE metadata bit to release.
 * Keep that bit free on disk but reserved in RAM until the parent FE and FID
 * have both passed lower-device barriers.  On any uncertain write, retain
 * OPEN LVID and make this mount read-only; the bounded recovery graph decides
 * whether another mount can safely roll the transaction forward. */
static NTSTATUS
UDFTryPreparedEmptyDirectoryUnlink(PIRP_CONTEXT IrpContext, PVCB Vcb,
                                   PUDF_FILE_INFO FileInfo,
                                   PBOOLEAN Handled, PBOOLEAN NamespaceTouched)
{
    PUDF_FILE_INFO parent;
    PUDF_DATALOC_INFO dloc;
    PDIR_INDEX_HDR parentIndex;
    PDIR_INDEX_ITEM entry;
    PUDF_DELETE_PREFREE record = NULL;
    int8 *sector = NULL, *parentBefore = NULL, *fidBefore = NULL;
    uint32 bitmapPhysical = 0, bitmapMetadata = 0;
    uint32 childPart, parentPart, fidLba, childFeLba, parentFeLba;
    uint32 parentFeFlags;
    uint8 oldFlags, oldCharacteristics;
    NTSTATUS status;
    BOOLEAN bitmapHeld = FALSE, attached = FALSE;
    BOOLEAN parentTouched = FALSE, fidTouched = FALSE;
    BOOLEAN uncertain = FALSE, committed = FALSE;
    FILE_ID removedId;

    *Handled = FALSE;
    if (!IrpContext || IrpContext->MajorFunction != IRP_MJ_CLEANUP ||
        Vcb->CDR_Mode || Vcb->SectorSize != 512 ||
        !Vcb->LVid ||
        Vcb->origIntegrityType != INTEGRITY_TYPE_CLOSE ||
        Vcb->VcbCondition != VcbMounted ||
        Vcb->FsDeviceType != FILE_DEVICE_DISK_FILE_SYSTEM ||
        (Vcb->VcbState & (VCB_STATE_VOLUME_READ_ONLY |
                           VCB_STATE_REMOVABLE_MEDIA)) ||
        !FileInfo || !FileInfo->Fcb ||
        FileInfo->Fcb->FcbCleanup ||
        !UDFIsADirectory(FileInfo) || UDFIsAStreamDir(FileInfo) ||
        UDFHasAStreamDir(FileInfo) || !FileInfo->FileIdent ||
        !(dloc = FileInfo->Dloc) ||
        dloc->CommonFcb != FileInfo->Fcb ||
        !dloc->DirIndex || !UDFIsDirEmpty(dloc->DirIndex) ||
        UDFGetFileLinkCount(FileInfo) != 1 ||
        !dloc->DataLoc.Offset || dloc->DataLoc.Length != 40 ||
        !dloc->FELoc.Mapping ||
        dloc->FELoc.Mapping[0].extLength != 512 ||
        (dloc->FELoc.Mapping[1].extLength & UDF_EXTENT_LENGTH_MASK) ||
        (dloc->FE_Flags & UDF_FE_FLAG_FE_MODIFIED) ||
        dloc->DataLoc.Modified || dloc->AllocLoc.Modified ||
        dloc->FELoc.Modified ||
        !(parent = FileInfo->ParentFile) ||
        !parent->Dloc || !parent->Dloc->FileEntry ||
        !parent->Dloc->FELoc.Mapping ||
        parent->Dloc->FELoc.Mapping[0].extLength != 512 ||
        (parent->Dloc->FELoc.Mapping[1].extLength &
            UDF_EXTENT_LENGTH_MASK) ||
        parent->Dloc->DataLoc.Offset ||
        !parent->Dloc->DataLoc.Mapping ||
        !(parentIndex = parent->Dloc->DirIndex) ||
        FileInfo->Index < 2 ||
        !(entry = UDFDirIndex(parentIndex, FileInfo->Index)) ||
        !(entry->FileCharacteristics & FILE_DIRECTORY) ||
        (entry->FileCharacteristics & FILE_DELETED) ||
        !(FileInfo->FileIdent->fileCharacteristics & FILE_DIRECTORY) ||
        (FileInfo->FileIdent->fileCharacteristics & FILE_DELETED) ||
        UDFDeleteParentHasOtherDirtyFid(parentIndex, FileInfo->Index) ||
        entry->Length != FileInfo->FileIdentLen ||
        !entry->Length || entry->Length > 512 ||
        (entry->Offset & 511) > 512 - entry->Length ||
        parent->Dloc->DataLoc.Length < 0 ||
        (uint64)entry->Offset + entry->Length >
            (uint64)parent->Dloc->DataLoc.Length ||
        UDFGetFileLinkCount(parent) < 2 ||
        UDFGetFileLinkCount(parent) == UDF_INVALID_LINK_COUNT)
        return STATUS_NOT_SUPPORTED;

    childFeLba = dloc->FELoc.Mapping[0].extLocation;
    parentFeLba = parent->Dloc->FELoc.Mapping[0].extLocation;
    childPart = UDFGetRefPartNumByPhysLba(Vcb, childFeLba);
    parentPart = UDFGetRefPartNumByPhysLba(Vcb, parentFeLba);
    fidLba = UDFExtentOffsetToLba(Vcb, parent->Dloc->DataLoc.Mapping,
                                  entry->Offset, NULL, NULL, NULL, NULL);
    if (childPart == (uint32)-1 || parentPart == (uint32)-1 ||
        fidLba == LBA_OUT_OF_EXTENT ||
        !UDFDeleteHasIndependentMetadataMirror(Vcb, childFeLba) ||
        !UDFDeleteHasIndependentMetadataMirror(Vcb, parentFeLba) ||
        !UDFDeleteHasIndependentMetadataMirror(Vcb, fidLba))
        return STATUS_NOT_SUPPORTED;

    status = UDFBuildDeletePrefree(Vcb, FileInfo, &record,
                                   &bitmapPhysical, &bitmapMetadata);
    if (status == STATUS_NOT_SUPPORTED)
        return status;
    *Handled = TRUE;
    if (!NT_SUCCESS(status))
        return status;
    if (record->RangeCount != 1 || !record->Ranges[0].Metadata ||
        record->Ranges[0].BitCount != 1) {
        MyFreePool__(record);
        *Handled = FALSE;
        return STATUS_NOT_SUPPORTED;
    }
    sector = (int8*)MyAllocatePool__(NonPagedPool, 512);
    parentBefore = (int8*)MyAllocatePool__(NonPagedPool, 512);
    fidBefore = (int8*)MyAllocatePool__(NonPagedPool,
                                        FileInfo->FileIdentLen);
    if (!sector || !parentBefore || !fidBefore) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto cleanup;
    }

    status = UDFOpenIntegrityForWrite(IrpContext, Vcb, FALSE);
    if (!NT_SUCCESS(status)) goto cleanup;
    status = UDFFlushFile__(IrpContext, Vcb, parent, 0);
    if (!NT_SUCCESS(status)) {
        Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
        goto cleanup;
    }
    status = UDFBarrierFlushDevice(Vcb->TargetDeviceObject);
    if (!NT_SUCCESS(status)) {
        Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
        goto cleanup;
    }
    /* A prior append or retag may have changed the parent's mapping while
     * preflushing it.  This transaction cannot use a stale FID address. */
    parentIndex = parent->Dloc->DirIndex;
    entry = parentIndex ? UDFDirIndex(parentIndex, FileInfo->Index) : NULL;
    if (parent->Dloc->DataLoc.Modified ||
        parent->Dloc->AllocLoc.Modified ||
        parent->Dloc->FELoc.Modified ||
        (parent->Dloc->FE_Flags & UDF_FE_FLAG_FE_MODIFIED) ||
        parent->Dloc->DataLoc.Offset ||
        !entry || entry->Length != FileInfo->FileIdentLen ||
        (entry->FileCharacteristics & FILE_DELETED) ||
        UDFDeleteParentHasOtherDirtyFid(parentIndex, FileInfo->Index) ||
        fidLba != UDFExtentOffsetToLba(Vcb,
                       parent->Dloc->DataLoc.Mapping, entry->Offset,
                       NULL, NULL, NULL, NULL) ||
        !UDFDeleteHasIndependentMetadataMirror(Vcb, parentFeLba) ||
        !UDFDeleteHasIndependentMetadataMirror(Vcb, fidLba)) {
        status = STATUS_CANNOT_DELETE;
        goto cleanup;
    }
    parentFeFlags = parent->Dloc->FE_Flags;
    oldFlags = entry->FI_Flags;
    oldCharacteristics = entry->FileCharacteristics;
    RtlCopyMemory(parentBefore, parent->Dloc->FileEntry, 512);
    RtlCopyMemory(fidBefore, FileInfo->FileIdent,
                  FileInfo->FileIdentLen);
    removedId = UDFGetNTFileId(Vcb, FileInfo);
    UDFPrint(("UDFS empty-dir prefree start FE %lx parentFE %lx FID %lx\n",
              (ULONG)childFeLba, (ULONG)parentFeLba, (ULONG)fidLba));

    _SEH2_TRY {
        UDFAcquireResourceExclusive(&(Vcb->BitMapResource1), TRUE);
        bitmapHeld = TRUE;
        status = UDFValidateDeletePrefreeBitmaps(IrpContext, Vcb,
                     bitmapPhysical, bitmapMetadata, sector);
        if (NT_SUCCESS(status) && (dloc->DeletePrefree ||
            Vcb->DeletePrefreeCount == MAXULONG))
            status = dloc->DeletePrefree ? STATUS_FILE_CORRUPT_ERROR :
                                          STATUS_INSUFFICIENT_RESOURCES;
        if (NT_SUCCESS(status)) {
            record->Next = Vcb->DeletePrefreeList;
            Vcb->DeletePrefreeList = record;
            Vcb->DeletePrefreeCount++;
            dloc->DeletePrefree = record;
            attached = TRUE;
            uncertain = TRUE;
            *NamespaceTouched = TRUE;
            status = UDFWriteDeletePrefreeBitmaps(IrpContext, Vcb, record,
                         bitmapPhysical, bitmapMetadata, sector,
                         &uncertain);
        }
        if (NT_SUCCESS(status))
            status = UDFBarrierFlushDevice(Vcb->TargetDeviceObject);
        UDFReleaseResource(&(Vcb->BitMapResource1));
        bitmapHeld = FALSE;

        if (NT_SUCCESS(status)) {
            UDFDecFileLinkCount(parent);
            parentTouched = TRUE;
            status = UDFFlushFE(IrpContext, Vcb, parent, parentPart);
        }
        if (NT_SUCCESS(status))
            status = UDFBarrierFlushDevice(Vcb->TargetDeviceObject);
        if (NT_SUCCESS(status)) {
            entry->FI_Flags |= UDF_FI_FLAG_FI_MODIFIED;
            entry->FileCharacteristics |= FILE_DELETED;
            FileInfo->FileIdent->fileCharacteristics |= FILE_DELETED;
            RtlZeroMemory(&FileInfo->FileIdent->icb, sizeof(long_ad));
            fidTouched = TRUE;
            status = UDFFlushFI(IrpContext, Vcb, FileInfo, childPart);
        }
        if (NT_SUCCESS(status))
            status = UDFBarrierFlushDevice(Vcb->TargetDeviceObject);
        if (NT_SUCCESS(status)) {
            committed = TRUE;
            parentIndex->DelCount++;
            UDFChangeFileCounter(Vcb, FALSE, FALSE);
            UDFDecFileLinkCount(FileInfo);
            dloc->FE_Flags &= ~UDF_FE_FLAG_FE_MODIFIED;
            dloc->DeleteCommitted = TRUE;
            dloc->FE_Flags |= UDF_FE_FLAG_FREE_DEFERRED;
            UDFRemoveFileId(Vcb, removedId);
            UDFPrint(("UDFS empty-dir prefree committed FE %lx\n",
                      (ULONG)childFeLba));
            /* Cleanup owns the FCB until teardown. The overlay keeps its
             * freed on-disk FE bit unavailable to RAM allocation meanwhile. */
        }
    } _SEH2_FINALLY {
        if (_SEH2_AbnormalTermination() || !NT_SUCCESS(status)) {
            if (uncertain || _SEH2_AbnormalTermination())
                Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
            if (!committed && fidTouched) {
                RtlCopyMemory(FileInfo->FileIdent, fidBefore,
                              FileInfo->FileIdentLen);
                entry->FileCharacteristics = oldCharacteristics;
                entry->FI_Flags = oldFlags;
            }
            if (!committed && parentTouched) {
                RtlCopyMemory(parent->Dloc->FileEntry, parentBefore, 512);
                parent->Dloc->FE_Flags = parentFeFlags;
            }
            if (!committed && attached) {
                if (!bitmapHeld) {
                    UDFAcquireResourceExclusive(&(Vcb->BitMapResource1),
                                                TRUE);
                    bitmapHeld = TRUE;
                }
                NTSTATUS detachStatus = UDFDetachDeletePrefree(Vcb, dloc);
                if (NT_SUCCESS(status) && detachStatus != STATUS_SUCCESS)
                    status = detachStatus;
                /* A refusal leaves the record attached to Dloc. Do not
                 * free it through this stack-local rollback receipt. */
                record = NULL;
                attached = detachStatus != STATUS_SUCCESS;
            }
        }
        if (bitmapHeld)
            UDFReleaseResource(&(Vcb->BitMapResource1));
    } _SEH2_END;

cleanup:
    if (fidBefore) MyFreePool__(fidBefore);
    if (parentBefore) MyFreePool__(parentBefore);
    if (sector) MyFreePool__(sector);
    if (record && !attached) MyFreePool__(record);
    return status;
}

/*
    This routine marks file as deleted & decrements file link counter.
    It can optionaly free allocation space
 */
/* OpenCount counts references held by children, not user handles of a stream.
 * FcbCleanup is decremented when that stream's last FILE_OBJECT is cleaned up.
 * Check the shared Dloc's FCB because a directory-index FI may be a parallel
 * FI rather than the one attached to the open handle. */
BOOLEAN
UDFStreamDirectoryHasLiveHandles(
    IN PUDF_FILE_INFO SDirInfo
    )
{
    PDIR_INDEX_ITEM Entry;
    PUDF_FILE_INFO StreamInfo;
    PFCB StreamFcb;
    uint_di Index;

    if (!SDirInfo || !SDirInfo->Dloc || !SDirInfo->Dloc->DirIndex)
        return FALSE;

    for (Index = 2;
         (Entry = UDFDirIndex(SDirInfo->Dloc->DirIndex, Index));
         Index++) {
        StreamInfo = Entry->FileInfo;
        if (!StreamInfo)
            continue;
        StreamFcb = StreamInfo->Dloc ? StreamInfo->Dloc->CommonFcb : NULL;
        if (!StreamFcb)
            StreamFcb = StreamInfo->Fcb;
        if (StreamFcb && StreamFcb->FcbCleanup)
            return TRUE;
    }
    return FALSE;
}

/* An SDir is reached through its parent's EFE, not a normal FID. The
 * parent pointer removal must be committed before that SDir's allocation
 * can be retired. The same parent FE ordering covers non-fast stream/dir
 * tombstones without requiring an inline allocation release. */
static NTSTATUS
UDFCommitUnlinkParent(PIRP_CONTEXT IrpContext, PVCB Vcb,
                      PUDF_FILE_INFO FileInfo)
{
    PUDF_FILE_INFO Parent = FileInfo->ParentFile;
    PFCB Fcb;
    BOOLEAN Acquired = FALSE;
    NTSTATUS Status = STATUS_SUCCESS;
    uint32 PartNum;
    if (!Parent || !Parent->Dloc || !Parent->Dloc->FileEntry ||
        !Parent->Dloc->FELoc.Mapping)
        return STATUS_FILE_CORRUPT_ERROR;
    /* A parent already known unreachable must never be serialized again.
     * Child holds still reserve its allocation until their final cleanup. */
    if (Parent->Dloc->DeleteCommitted)
        return STATUS_SUCCESS;
    Fcb = Parent->Dloc->CommonFcb;
    if (Fcb && !ExIsResourceAcquiredExclusiveLite(&Fcb->FcbNonpaged->FcbResource)) {
        if (!UDFAcquireFcbExclusive(IrpContext, Fcb, TRUE))
            return STATUS_SHARING_VIOLATION;
        Acquired = TRUE;
    }
    _SEH2_TRY {
        _SEH2_TRY {
            PartNum = UDFGetRefPartNumByPhysLba(Vcb,
                Parent->Dloc->FELoc.Mapping[0].extLocation);
            Status = PartNum == (uint32)-1 ? STATUS_FILE_CORRUPT_ERROR :
                UDFFlushFE(IrpContext, Vcb, Parent, PartNum);
        } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
            Status = _SEH2_GetExceptionCode();
        } _SEH2_END;
    } _SEH2_FINALLY {
        if (Acquired)
            UDFReleaseFcb(IrpContext, Fcb);
    } _SEH2_END;
    return Status;
}

static NTSTATUS UDFDereferenceFileLocked(PVCB Vcb, PUDF_FILE_INFO FileInfo);
static uint32 UDFCleanUpFileInternal(PVCB Vcb, PUDF_FILE_INFO FileInfo,
                                     PNTSTATUS CleanupStatus);
static uint32 UDFCleanUpFileWithStatus(PVCB Vcb, PUDF_FILE_INFO FileInfo,
                                       PNTSTATUS CleanupStatus);

/* A synchronous stream operation transfers one successful temporary open
 * to this explicit slot. Do not add another reference or copy an aggregate
 * Dloc count. A failed checked release retains the exact slot and prevents
 * clean-close/disposal, rather than losing an anonymous reference. */
NTSTATUS
UDFOwnStreamDeleteFile(PVCB Vcb, PUDF_DATALOC_INFO Owner,
                       PUDF_FILE_INFO FileInfo)
{
    NTSTATUS Status = STATUS_SUCCESS;
    if (!Owner || !FileInfo || !FileInfo->Dloc || Owner == FileInfo->Dloc)
        return STATUS_FILE_INVALID;
    UDFAcquireResourceExclusive(&Vcb->DlocResource2, TRUE);
    if (Owner->StreamDeleteOwner || Owner->StreamCreate)
        Status = STATUS_DEVICE_BUSY;
    else if (!FileInfo->RefCount ||
             FileInfo->Dloc->LinkRefCount < FileInfo->RefCount)
        Status = STATUS_FILE_CORRUPT_ERROR;
    else if (FileInfo->Dloc->CleanupOwner || FileInfo->Dloc->StreamDeleteContextOwner || FileInfo->Dloc->StreamCreate ||
             (!FileInfo->Dloc->CommonFcb &&
              (FileInfo->RefCount != 1 || FileInfo->Dloc->LinkRefCount != 1)))
        Status = STATUS_DEVICE_BUSY;
    else {
        /* Without a common FCB there can be no pre-existing constructor/
         * by-ID reference about to publish one behind our missing lock.
         * Gate later acquisitions until the checked release/cleanup claim. */
        FileInfo->Dloc->StreamDeleteContextOwner = Owner;
        Owner->StreamDeleteOwner = FileInfo;
        InterlockedIncrement(&Vcb->PendingNamespaceCount);
    }
    UDFReleaseResource(&Vcb->DlocResource2);
    return Status;
}

/* Reference consumption is non-I/O. With an exclusive VCB, Dispose can
 * atomically hand an ownerless cold FI to the collector. Shared-VCB callers
 * leave CleanupPending in the indexed Dloc ring for explicit volume retry;
 * never free weak cache topology concurrently with metadata readers.
 * Error-path callers pass FALSE and perform no cleanup/writeback here. */
NTSTATUS
UDFReleaseStreamDeleteFile(PVCB Vcb, PUDF_DATALOC_INFO Owner,
                           PUDF_FILE_INFO* OwnedFileInfo, BOOLEAN Dispose)
{
    PUDF_FILE_INFO FileInfo = *OwnedFileInfo;
    PUDF_DATALOC_INFO Dloc;
    NTSTATUS Status;
    uint32 Result = UDF_FREE_NOTHING;
    BOOLEAN Claimed = FALSE;
    if (!Owner || !FileInfo)
        return STATUS_FILE_INVALID;
    if (Dispose && !ExIsResourceAcquiredSharedLite(&Vcb->VcbResource) &&
        !ExIsResourceAcquiredExclusiveLite(&Vcb->VcbResource))
        return STATUS_INVALID_DEVICE_STATE;
    UDFAcquireResourceExclusive(&Vcb->DlocResource2, TRUE);
    if (Owner->StreamDeleteOwner != FileInfo || !FileInfo->Dloc ||
        FileInfo->Dloc->StreamDeleteContextOwner != Owner) {
        UDFReleaseResource(&Vcb->DlocResource2);
        return STATUS_FILE_INVALID;
    }
    Dloc = FileInfo->Dloc;
    if (Dispose)
        FileInfo->CleanupPending = TRUE;
    Status = UDFDereferenceFileLocked(Vcb, FileInfo);
    if (NT_SUCCESS(Status)) {
        if (Dispose && ExIsResourceAcquiredExclusiveLite(&Vcb->VcbResource) &&
            Dloc && !FileInfo->RefCount && !FileInfo->OpenCount &&
            !FileInfo->Fcb && !Dloc->CommonFcb && !Dloc->LinkRefCount &&
            !Dloc->CleanupOwner && !(Dloc->FE_Flags & UDF_FE_FLAG_UNDER_INIT)) {
            Dloc->CleanupOwner = PsGetCurrentThread();
            Claimed = TRUE;
        }
        Dloc->StreamDeleteContextOwner = NULL;
        Owner->StreamDeleteOwner = NULL;
        NT_ASSERT(Vcb->PendingNamespaceCount > 0);
        if (!Claimed)
            InterlockedDecrement(&Vcb->PendingNamespaceCount);
        /* Otherwise the cleanup claim inherits the slot's close barrier
         * until normal completion; an exception keeps it quarantined. */
    }
    /* A failed checked decrement leaves the explicit slot as owner. The
     * caller must not continue to use an FI after consuming/handover. */
    *OwnedFileInfo = NULL;
    UDFReleaseResource(&Vcb->DlocResource2);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Claimed) {
        NTSTATUS CleanupStatus = STATUS_SUCCESS;
        _SEH2_TRY {
            _SEH2_TRY {
                Result = UDFCleanUpFileInternal(Vcb, FileInfo, &CleanupStatus);
                if (CleanupStatus != STATUS_SUCCESS && CleanupStatus != STATUS_DEVICE_BUSY)
                    Status = CleanupStatus;
                if (Result & UDF_FREE_FILEINFO)
                    MyFreePool__(FileInfo);
            } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
                Status = _SEH2_GetExceptionCode();
                Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
                InterlockedCompareExchange(&Vcb->MetadataWritebackError, Status, STATUS_SUCCESS);
            } _SEH2_END;
        } _SEH2_FINALLY {
            if (NT_SUCCESS(Status)) {
                UDFAcquireResourceExclusive(&Vcb->DlocResource2, TRUE);
                if (!(Result & UDF_FREE_DLOC))
                    Dloc->CleanupOwner = NULL;
                NT_ASSERT(Vcb->PendingNamespaceCount > 0);
                InterlockedDecrement(&Vcb->PendingNamespaceCount);
                UDFReleaseResource(&Vcb->DlocResource2);
            }
        } _SEH2_END;
    }
    return Status;
}

/* Abort a successful open that could not enter the operation slot. Do
 * not access its FI after dropping the last reference. A corrupt decrement
 * retains this exact rejected hold in the otherwise empty owner slot. */
NTSTATUS
UDFAbortStreamDeleteFile(PVCB Vcb, PUDF_DATALOC_INFO Owner,
                         PUDF_FILE_INFO* OwnedFileInfo)
{
    PUDF_FILE_INFO FileInfo = *OwnedFileInfo;
    NTSTATUS Status;
    if (!Owner || !FileInfo)
        return STATUS_FILE_INVALID;
    UDFAcquireResourceExclusive(&Vcb->DlocResource2, TRUE);
    NT_ASSERT(Owner->StreamDeleteOwner == NULL);
    if (Owner->StreamDeleteOwner) {
        UDFReleaseResource(&Vcb->DlocResource2);
        return STATUS_DEVICE_BUSY;
    }
    FileInfo->CleanupPending = TRUE;
    Status = UDFDereferenceFileLocked(Vcb, FileInfo);
    if (!NT_SUCCESS(Status)) {
        Owner->StreamDeleteOwner = FileInfo;
        InterlockedIncrement(&Vcb->PendingNamespaceCount);
        /* No second context gate is installed over another operation's
         * gate. This is an abort-only quarantine hold, not a new unlink. */
    }
    *OwnedFileInfo = NULL;
    UDFReleaseResource(&Vcb->DlocResource2);
    return Status;
}

/* Failed constructors do not own an ordinary successful-open reference.
 * Bound contexts stay in the Dloc's failed-generation inventory; an unbound
 * shell has no on-media allocation and can be disposed as memory only. */
NTSTATUS
UDFRetainFailedStreamOpen(PVCB Vcb, PUDF_FILE_INFO* FailedFileInfo)
{
    PUDF_FILE_INFO FileInfo = *FailedFileInfo;
    NTSTATUS Status = STATUS_SUCCESS;
    if (!FileInfo)
        return STATUS_SUCCESS;
    *FailedFileInfo = NULL;
    _SEH2_TRY {
        if (FileInfo->Dloc) {
            UDFAcquireResourceExclusive(&Vcb->DlocResource2, TRUE);
            if (FileInfo->Constructing)
                FileInfo->ConstructionFailed = TRUE;
            FileInfo->CleanupPending = TRUE;
            UDFReleaseResource(&Vcb->DlocResource2);
        } else if (UDFCleanUpFile__(Vcb, FileInfo) & UDF_FREE_FILEINFO) {
            MyFreePool__(FileInfo);
        } else {
            Status = STATUS_DEVICE_BUSY;
        }
    } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
        Status = _SEH2_GetExceptionCode();
        Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
        InterlockedCompareExchange(&Vcb->MetadataWritebackError, Status, STATUS_SUCCESS);
    } _SEH2_END;
    return Status;
}

/* Caller still owns the main resource, but no longer any temporary FI
 * reference. Indexed teardown may retire the entire FI ring; report whether
 * it consumed that resource before an ancestor flush/exception can occur. */
static NTSTATUS
UDFTeardownClosedStream(PIRP_CONTEXT IrpContext, PVCB Vcb, PFCB Fcb,
                         PBOOLEAN Removed)
{
    NTSTATUS Status = STATUS_SUCCESS, WritebackStatus;
    BOOLEAN WasWriter = BooleanFlagOn(IrpContext->Flags, IRP_CONTEXT_FLAG_METADATA_WRITER);
    *Removed = FALSE;
    if (Fcb->FcbReference || Fcb->FcbCleanup)
        return STATUS_SUCCESS;
    if (Fcb->FileInfo && Fcb->FileInfo->Dloc &&
        (Fcb->FileInfo->Dloc->LinkRefCount || Fcb->FileInfo->OpenCount ||
         Fcb->FileInfo->Dloc->StreamDeleteOwner ||
         Fcb->FileInfo->Dloc->StreamDeleteContextOwner || Fcb->FileInfo->Dloc->StreamCreate ||
         (Fcb->FileInfo->Dloc->FE_Flags & UDF_FE_FLAG_CACHED_DATA_PENDING)))
        return STATUS_SUCCESS; /* keep the prefix with every remaining owner */
    SetFlag(IrpContext->Flags, IRP_CONTEXT_FLAG_METADATA_WRITER);
    _SEH2_TRY {
        _SEH2_TRY {
            Status = UDFTeardownStructuresEx(IrpContext, Fcb, FALSE, FALSE, Removed);
            /* Namespace commit has already completed. A busy memory-GC
             * decision retains its indexed owner for explicit retry; it is
             * not a failed unlink. Actual writeback errors stay latched. */
            if (Status == STATUS_DEVICE_BUSY)
                Status = STATUS_SUCCESS;
        } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
            Status = _SEH2_GetExceptionCode();
        } _SEH2_END;
    } _SEH2_FINALLY {
        if (!WasWriter)
            ClearFlag(IrpContext->Flags, IRP_CONTEXT_FLAG_METADATA_WRITER);
    } _SEH2_END;
    WritebackStatus = UDFTakeMetadataWritebackError(Vcb);
    if (NT_SUCCESS(Status)) Status = WritebackStatus;
    return Status;
}

static NTSTATUS UDFDeleteAllStreamsEx(PIRP_CONTEXT IrpContext, PVCB Vcb,
    PUDF_FILE_INFO FileInfo, PBOOLEAN NamespaceTouched);
static NTSTATUS UDFUnlinkAllFilesInDirEx(PIRP_CONTEXT IrpContext, PVCB Vcb,
    PUDF_FILE_INFO DirInfo, PBOOLEAN NamespaceTouched);

static NTSTATUS
UDFUnlinkFileInternal(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN PUDF_FILE_INFO FileInfo,
    IN BOOLEAN FreeSpace,
    OUT PBOOLEAN NamespaceTouched
    )
{
    uint_di Index;   // index of file to be deleted
    uint16 lc;
    PUDF_DATALOC_INFO Dloc;
    PUDF_FILE_INFO DirInfo;
    PDIR_INDEX_HDR hDirNdx;
    PDIR_INDEX_HDR hCurDirNdx;
    PDIR_INDEX_ITEM DirNdx;
    NTSTATUS status;
    BOOLEAN IsSDir;
    BOOLEAN FinalFileLink;
    uint8 OldCharacteristics = 0;
    uint8 OldFidCharacteristics = 0;
    uint8 OldFiFlags = 0;
    uint32 OldFeFlags = 0;
    long_ad OldIcb;
    FILE_ID RemovedFileId;
    BOOLEAN RemoveFileId;
    BOOLEAN CommitBarrierFailed = FALSE;
    BOOLEAN PrefreeModified = FALSE;
    BOOLEAN PrefreeLockHeld = FALSE;
    BOOLEAN PrefreeAttached = FALSE;
    BOOLEAN DirectoryHandled = FALSE;
    PUDF_DELETE_PREFREE Prefree = NULL;
    int8* PrefreeSector = NULL;
    uint32 PhysicalBitmapLba = 0, MetadataBitmapLba = 0;
    NTSTATUS RestoreStatus;

    AdPrint(("UDFUnlinkFile__:\n"));
    if (!FileInfo) return STATUS_SUCCESS;

    ValidateFileInfo(FileInfo);
    RemoveFileId = FileInfo->Fcb != NULL;
    if (RemoveFileId)
        RemovedFileId = UDFGetNTFileId(Vcb, FileInfo);

    // check references
    Dloc = FileInfo->Dloc;
    if (Vcb->VcbState & VCB_STATE_VOLUME_READ_ONLY)
        return STATUS_MEDIA_WRITE_PROTECTED;
    if (Dloc->DeleteCommitted || FileInfo->LinkDeleteCommitted)
        return STATUS_FILE_DELETED;
    /* RefCount belongs to this exact FI; OpenCount is the contribution
     * from referenced child FIs, not a count of user handles. SDir/directory
     * ownership has separate preflight below. These scalar counts cannot
     * authorize consuming references owned by another CCB or LCB. */
    // When FreeSpace=FALSE (rename/move), we only unlink the FID from the
    // directory — the file data stays intact. OpenCount/RefCount/SDirInfo checks
    // are not relevant because the file remains alive under a new name.
    if (FreeSpace) {
        if (UDFIsAStreamDir(FileInfo) || UDFHasAStreamDir(FileInfo) ||
            UDFIsADirectory(FileInfo)) {
            // Skip OpenCount/RefCount checks for:
            // - SDir: delayed-close streams may hold OpenCount > 0.
            // - File with SDir: SDir + stream LCBs contribute to counts.
            // - Directory: delayed-close children (already unlinked) may
            //   hold OpenCount > 0 via FCB reference chain. Directory
            //   emptiness is verified by UDFIsDirEmpty below.
        } else {
            if (FileInfo->OpenCount) {
                AdPrint(("UDFUnlinkFile__: OpenCount=%d, cannot delete\n", FileInfo->OpenCount));
                return STATUS_CANNOT_DELETE;
            }
            if (FileInfo->RefCount > 2) {
                AdPrint(("UDFUnlinkFile__: RefCount=%d > 2, cannot delete\n", FileInfo->RefCount));
                return STATUS_CANNOT_DELETE;
            }
        }
    }
    ASSERT(FileInfo->RefCount >= 1);
    if (!FileInfo->RefCount || Dloc->LinkRefCount < FileInfo->RefCount) {
        /* Preserve the assertion and fail closed in free builds too. This
         * guard is not a substitute for the exact CCB/LCB owner protocol. */
        Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
        UDFRememberMetadataWritebackError(IrpContext, Vcb, FileInfo,
                                           STATUS_FILE_CORRUPT_ERROR);
        return STATUS_FILE_CORRUPT_ERROR;
    }
    DirInfo = FileInfo->ParentFile;
    // root dir or self
    if (!DirInfo || ((FileInfo->Index < 2) && !UDFIsAStreamDir(FileInfo))) return STATUS_CANNOT_DELETE;
    hDirNdx = DirInfo->Dloc->DirIndex;
    Index = FileInfo->Index;
    // we can't delete modified file
    // it should be closed & reopened (or flushed) before deletion
    DirNdx = UDFDirIndex(hDirNdx,Index);
    if (DirNdx && !UDFIsAStreamDir(FileInfo) && UDFIsDeleted(DirNdx))
        return STATUS_FILE_DELETED; /* no second counter/ref/allocation transition */
#if defined UDF_DBG
    if (DirNdx && DirNdx->FName.Buffer) {
        AdPrint(("Unlink: %wZ\n", &DirNdx->FName));
    }
#endif // UDF_DBG
    if (Dloc->RetiredExtents)
        return STATUS_CANNOT_DELETE; /* flush the replacement FE before unlink */
    if (FreeSpace &&
       ((Dloc->FE_Flags & UDF_FE_FLAG_FE_MODIFIED) ||
         Dloc->DataLoc.Modified ||
         Dloc->AllocLoc.Modified ||
         Dloc->FELoc.Modified ||
        (DirNdx && (DirNdx->FI_Flags & UDF_FI_FLAG_FI_MODIFIED)) )) {
        return STATUS_CANNOT_DELETE;
    }
    /* Last-name/SDir deletion preflights each referenced stream inside the
     * synchronous helper. A surviving base hard link keeps its stream tree;
     * it does not need to delete or reject another link's live streams. */
    /* Complete fallible stream deletion before changing the surviving
     * base FID or its link counts. A child flush/unlink error must leave
     * that namespace owner in place, not a deleted FID awaiting cleanup. */
    if (FreeSpace && UDFIsAStreamDir(FileInfo)) {
        if (!DirInfo->Dloc->FileEntry ||
            DirInfo->Dloc->FileEntry->tagIdent != TID_EXTENDED_FILE_ENTRY ||
            DirInfo->Dloc->FileEntryLen < sizeof(EXTENDED_FILE_ENTRY))
            return STATUS_FILE_CORRUPT_ERROR;
        if (UDFIsSDirDeleted(FileInfo))
            return STATUS_FILE_DELETED;
        status = UDFUnlinkAllFilesInDirEx(IrpContext, Vcb, FileInfo, NamespaceTouched);
        if (NT_SUCCESS(status))
            status = UDFFlushFile__(IrpContext, Vcb, FileInfo, 0);
        if (!NT_SUCCESS(status))
            return status;
    } else if (FreeSpace && UDFHasAStreamDir(FileInfo) &&
               UDFGetFileNameLinkCount(FileInfo) == 1) {
        status = UDFDeleteAllStreamsEx(IrpContext, Vcb, FileInfo, NamespaceTouched);
        if (NT_SUCCESS(status))
            status = UDFFlushFile__(IrpContext, Vcb, FileInfo, 0);
        if (!NT_SUCCESS(status))
            return status;
    }
    if (FreeSpace && UDFIsADirectory(FileInfo)) {
        status = UDFTryPreparedEmptyDirectoryUnlink(IrpContext, Vcb,
                                                     FileInfo,
                                                     &DirectoryHandled,
                                                     NamespaceTouched);
        if (DirectoryHandled) {
            if (!NT_SUCCESS(status))
                UDFRememberMetadataWritebackError(IrpContext, Vcb, FileInfo, status);
            return status;
        }
    }
    /* An ordinary final-link delete may fail while writing its FID. Save the
     * state that must be restored before cleanup can flush the parent. */
    if (FreeSpace && UDFGetFileLinkCount(FileInfo) == 1 &&
        !UDFIsADirectory(FileInfo) && !UDFIsAStream(FileInfo) &&
        !UDFHasAStreamDir(FileInfo) && FileInfo->FileIdent) {
        OldCharacteristics = DirNdx->FileCharacteristics;
        OldFidCharacteristics = FileInfo->FileIdent->fileCharacteristics;
        OldFiFlags = DirNdx->FI_Flags;
        OldFeFlags = Dloc->FE_Flags;
        RtlCopyMemory(&OldIcb, &FileInfo->FileIdent->icb, sizeof(OldIcb));
    }
    // stream directory can be deleted even being not empty
    // otherwise we should perform some checks
    if (!(IsSDir = UDFIsAStreamDir(FileInfo))) {
        // check if not empty direcory
        if ((DirNdx->FileCharacteristics & FILE_DIRECTORY) &&
           (hCurDirNdx = Dloc->DirIndex) &&
            FreeSpace) {
            if (!UDFIsDirEmpty(hCurDirNdx))
                return STATUS_DIRECTORY_NOT_EMPTY;
        }
        *NamespaceTouched = TRUE;
        DirNdx->FI_Flags |= UDF_FI_FLAG_FI_MODIFIED;
        DirNdx->FileCharacteristics |= FILE_DELETED;
        FileInfo->FileIdent->fileCharacteristics |= FILE_DELETED;
        hDirNdx->DelCount++;
        if (!UDFIsAStream(FileInfo))
            UDFChangeFileCounter(Vcb, !UDFIsADirectory(FileInfo), FALSE);
        else if (FreeSpace)
            UDFAddToStreamOwnerSize(FileInfo, -UDFGetFileSize(FileInfo));
        if (UDFIsADirectory(FileInfo) && FileInfo->ParentFile) {
            
            // FID of subdir parent counts as a link to the parent
            UDFDecFileLinkCount(FileInfo->ParentFile);
        }
    } else { // FID of stream dir parent link to owning file
        *NamespaceTouched = TRUE;
        if (FileInfo->ParentFile) {

            UDFDecFileLinkCount(FileInfo->ParentFile);
        }
    }
    lc = UDFGetFileLinkCount(FileInfo);
    FinalFileLink = (lc == 1);

    if (lc) {

        UDFDecFileLinkCount(FileInfo);
        lc--;
    }

    if (!IsSDir && 
        (lc == 1) && 
        UDFHasAStreamDir(FileInfo) && 
        !UDFIsSDirDeleted(Dloc->SDirInfo)
    ) {
        lc = 0;
    }

    if (DirNdx) {
        if (FreeSpace) {
            DirNdx->FI_Flags |= UDF_FI_FLAG_FI_MODIFIED;
            DirNdx->FI_Flags &= ~UDF_FI_FLAG_SYS_ATTR;
        }
        // Root Files (Root/SDir/Vat/etc.) has no FileIdent...
        if (FileInfo->FileIdent)
            RtlZeroMemory(&(FileInfo->FileIdent->icb), sizeof(long_ad));
    }
    // caller wishes to free allocation, but we can't do it due to
    // alive links. In this case we should just remove reference
    if (FreeSpace && lc) {
        ((icbtag*)(Dloc->FileEntry+1))->parentICBLocation.logicalBlockNum = 0;
        ((icbtag*)(Dloc->FileEntry+1))->parentICBLocation.partitionReferenceNum = 0;
        Dloc->FE_Flags |= UDF_FE_FLAG_FE_MODIFIED;
        status = UDFFlushFile__(IrpContext, Vcb, FileInfo, UDF_FLUSH_FLAGS_LITE);
        if (NT_SUCCESS(status))
            status = UDFCommitUnlinkParent(IrpContext, Vcb, FileInfo);
        if (NT_SUCCESS(status))
            status = UDFBarrierFlushDevice(Vcb->TargetDeviceObject);
        if (!NT_SUCCESS(status)) {
            Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
            UDFRememberMetadataWritebackError(IrpContext, Vcb, FileInfo, status);
            return status;
        }
    } else
    // if caller wishes to free file allocation &
    // there are no more references(links) to this file, lets do it >;->
    if (FreeSpace && !lc) {
        if (IsSDir) {
            // update parent FileInfo
            ASSERT(FileInfo->ParentFile->Dloc->FileEntry);
            RtlZeroMemory( &(((PEXTENDED_FILE_ENTRY)(FileInfo->ParentFile->Dloc->FileEntry))->streamDirectoryICB), sizeof(long_ad));
            FileInfo->ParentFile->Dloc->FE_Flags &= ~UDF_FE_FLAG_HAS_SDIR;
            FileInfo->ParentFile->Dloc->FE_Flags |= (UDF_FE_FLAG_FE_MODIFIED |
                                                     UDF_FE_FLAG_HAS_DEL_SDIR);
            FileInfo->Dloc->FE_Flags |= UDF_FE_FLAG_IS_DEL_SDIR;
        }
        // The last name of an ordinary file is being removed. Commit its
        // deleted FID before freeing any blocks; the now unreachable FE need
        // not be rewritten solely to record a zero link count. Its dirty
        // allocation state was rejected above, and open FCBs still defer the
        // physical free until they leave FcbTable.
        // Keep stream, directory, sequential-media and unmount paths intact.
        uint32 PartNum;
        if (FinalFileLink &&
            !UDFIsADirectory(FileInfo) &&
            !UDFIsAStream(FileInfo) &&
            !UDFHasAStreamDir(FileInfo) &&
            FileInfo->FileIdent &&
            !Vcb->CDR_Mode &&
            Vcb->VcbCondition == VcbMounted &&
            Vcb->FsDeviceType == FILE_DEVICE_DISK_FILE_SYSTEM &&
            !(Vcb->VcbState & VCB_STATE_REMOVABLE_MEDIA) &&
            ((PartNum = UDFGetRefPartNumByPhysLba(
                Vcb, Dloc->FELoc.Mapping[0].extLocation)) != (uint32)-1)) {
            /* A supported final-link delete pre-frees its exact on-disk
             * bitmap bits while RAM still reserves them, barriers that
             * generation, then barriers the FID tombstone.  Both crash
             * sides are repairable by the existing complete owner graph. */
            if (UDFExtentOffsetToLba(Vcb,
                    DirInfo->Dloc->DataLoc.Mapping, DirNdx->Offset,
                    NULL, NULL, NULL, NULL) != LBA_OUT_OF_EXTENT)
                status = UDFBuildDeletePrefree(Vcb, FileInfo, &Prefree,
                                &PhysicalBitmapLba, &MetadataBitmapLba);
            else
                status = STATUS_NOT_SUPPORTED;
            if (NT_SUCCESS(status)) {
                _SEH2_TRY {
                    PrefreeSector = (int8*)MyAllocatePool__(NonPagedPool, 512);
                    if (!PrefreeSector)
                        status = STATUS_INSUFFICIENT_RESOURCES;
                    if (NT_SUCCESS(status))
                        status = UDFOpenIntegrityForWrite(IrpContext, Vcb,
                                                          FALSE);
                    if (NT_SUCCESS(status)) {
                        UDFAcquireResourceExclusive(&(Vcb->BitMapResource1),
                                                     TRUE);
                        PrefreeLockHeld = TRUE;
                        status = UDFValidateDeletePrefreeBitmaps(IrpContext,
                            Vcb, PhysicalBitmapLba, MetadataBitmapLba,
                            PrefreeSector);
                        if (NT_SUCCESS(status) && (Dloc->DeletePrefree ||
                            Vcb->DeletePrefreeCount == MAXULONG))
                            status = Dloc->DeletePrefree ? STATUS_FILE_CORRUPT_ERROR :
                                                          STATUS_INSUFFICIENT_RESOURCES;
                        if (NT_SUCCESS(status)) {
                            Prefree->Next = Vcb->DeletePrefreeList;
                            Vcb->DeletePrefreeList = Prefree;
                            Vcb->DeletePrefreeCount++;
                            Dloc->DeletePrefree = Prefree;
                            PrefreeAttached = TRUE;
                            status = UDFWriteDeletePrefreeBitmaps(IrpContext,
                                Vcb, Prefree, PhysicalBitmapLba,
                                MetadataBitmapLba, PrefreeSector,
                                &PrefreeModified);
                            if (NT_SUCCESS(status))
                                status = UDFBarrierFlushDevice(
                                    Vcb->TargetDeviceObject);
                            if (NT_SUCCESS(status)) {
                                uint32 rangeIndex;
                                /* The dense bitmap's old copy tracks the
                                 * actual disk generation.  Without this,
                                 * free then reallocate can leave RAM equal
                                 * to the old copy, so the next volume flush
                                 * skips writing the now-used disk bit. */
                                for (rangeIndex = 0;
                                     rangeIndex < Prefree->RangeCount;
                                     ++rangeIndex) {
                                    PUDF_DELETE_PREFREE_RANGE range =
                                        &Prefree->Ranges[rangeIndex];
                                    if (!range->Metadata)
                                        UDFSetFreeBits(Vcb->FSBM_OldBitmap,
                                                       range->FirstBit,
                                                       range->BitCount);
                                }
                            }
                            if (NT_SUCCESS(status))
                                status = UDFFlushFI(IrpContext, Vcb,
                                                    FileInfo, PartNum);
                            if (NT_SUCCESS(status))
                                status = UDFBarrierFlushDevice(
                                    Vcb->TargetDeviceObject);
                            if (!NT_SUCCESS(status)) {
                                /* Any failed write or barrier leaves an
                                 * uncertain durable generation. */
                                CommitBarrierFailed = TRUE;
                                Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
                                NTSTATUS detachStatus = UDFDetachDeletePrefree(Vcb, Dloc);
                                if (NT_SUCCESS(status) && detachStatus != STATUS_SUCCESS)
                                    status = detachStatus;
                                Prefree = NULL;
                                PrefreeAttached = detachStatus != STATUS_SUCCESS;
                            }
                        }
                    }
                } _SEH2_FINALLY {
                    if (_SEH2_AbnormalTermination()) {
                        Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
                        if (PrefreeAttached) {
                            NTSTATUS detachStatus = UDFDetachDeletePrefree(Vcb, Dloc);
                            if (NT_SUCCESS(status) && detachStatus != STATUS_SUCCESS)
                                status = detachStatus;
                            Prefree = NULL;
                            PrefreeAttached = detachStatus != STATUS_SUCCESS;
                        }
                    }
                    if (PrefreeLockHeld)
                        UDFReleaseResource(&(Vcb->BitMapResource1));
                    if (PrefreeSector)
                        MyFreePool__(PrefreeSector);
                    if (Prefree && (!NT_SUCCESS(status) ||
                                    _SEH2_AbnormalTermination()))
                        MyFreePool__(Prefree);
                } _SEH2_END;
                if (NT_SUCCESS(status))
                    UDFPrint(("UDFS unlink prefree: FE %x, ranges %u\n",
                        Dloc->FELoc.Mapping[0].extLocation,
                        Dloc->DeletePrefree->RangeCount));
            } else if (status == STATUS_NOT_SUPPORTED) {
                /* Preserve ordinary deletion on layouts not yet covered by
                 * the bounded prefree protocol.  R250 still orders any
                 * subsequent reuse behind the tombstone. */
                status = UDFFlushFI(IrpContext, Vcb, FileInfo, PartNum);
                if (NT_SUCCESS(status)) {
                    status = UDFBarrierFlushDevice(Vcb->TargetDeviceObject);
                    if (!NT_SUCCESS(status)) {
                        CommitBarrierFailed = TRUE;
                        Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
                    }
                }
            }
            if (!NT_SUCCESS(status)) {
                /* The FID was changed in memory before UDFFlushFI. A later
                 * parent flush must not turn this failed unlink into a delete.
                 * Keep the FE and its allocation reachable, and retry writing
                 * the original FID in case the failed write was partial. */
                DirNdx->FileCharacteristics = OldCharacteristics;
                FileInfo->FileIdent->fileCharacteristics = OldFidCharacteristics;
                RtlCopyMemory(&FileInfo->FileIdent->icb, &OldIcb, sizeof(OldIcb));
                DirNdx->FI_Flags = OldFiFlags | UDF_FI_FLAG_FI_MODIFIED;
                hDirNdx->DelCount--;
                UDFChangeFileCounter(Vcb, TRUE, TRUE);
                UDFIncFileLinkCount(FileInfo);
                Dloc->FE_Flags = OldFeFlags;
                if (!CommitBarrierFailed) {
                    RestoreStatus = UDFFlushFI(IrpContext, Vcb, FileInfo,
                                               PartNum);
                    if (NT_SUCCESS(RestoreStatus))
                        RestoreStatus = UDFBarrierFlushDevice(
                            Vcb->TargetDeviceObject);
                    if (NT_SUCCESS(RestoreStatus))
                        DirNdx->FI_Flags = OldFiFlags;
                    else
                        Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
                }
                return status;
            }
            Dloc->FE_Flags &= ~UDF_FE_FLAG_FE_MODIFIED;
        } else {
            status = UDFFlushFile__(IrpContext, Vcb, FileInfo, 0);
            if (NT_SUCCESS(status))
                status = UDFCommitUnlinkParent(IrpContext, Vcb, FileInfo);
            if (NT_SUCCESS(status))
                status = UDFBarrierFlushDevice(Vcb->TargetDeviceObject);
            if (!NT_SUCCESS(status)) {
                /* Namespace/FE writeback may be partial. Retain allocation
                 * and stop further writes; never free a still-live ICB. */
                Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
                return status;
            }
        }

        /* The FID, or SDir's parent EFE pointer, is now committed absent.
         * Even without CommonFcb the caller and child FIs still own this
         * mapping. Retire it only at final claimed Dloc cleanup, never here. */
        Dloc->DeleteCommitted = TRUE;
        Dloc->FE_Flags |= UDF_FE_FLAG_FREE_DEFERRED;

    }

    /* A refused or failed unlink leaves the namespace entry reachable.
     * Drop its by-ID path cache only after the namespace change succeeds. */
    if (RemoveFileId)
        UDFRemoveFileId(Vcb, RemovedFileId);
    return STATUS_SUCCESS;
} // end UDFUnlinkFileInternal()

NTSTATUS
UDFUnlinkFileEx__(PIRP_CONTEXT IrpContext, PVCB Vcb,
                  PUDF_FILE_INFO FileInfo, BOOLEAN FreeSpace,
                  PBOOLEAN Touched)
{
    BOOLEAN NamespaceTouched = FALSE, FcbAcquired = FALSE, ParentAcquired = FALSE;
    PFCB Fcb = FileInfo && FileInfo->Dloc ? FileInfo->Dloc->CommonFcb : NULL;
    PFCB ParentFcb = FileInfo && FileInfo->ParentFile ? FileInfo->ParentFile->Fcb : NULL;
    NTSTATUS Status = STATUS_UNSUCCESSFUL;
    if (Touched) *Touched = FALSE;
    if (FileInfo && FileInfo->Dloc && FileInfo->Dloc->StreamCreate)
        return NT_SUCCESS(FileInfo->Dloc->StreamCreate->FirstError) ? STATUS_DEVICE_BUSY :
               FileInfo->Dloc->StreamCreate->FirstError;
    /* Internal stream-tree callers may hold only the parent. Serialize
     * shared FCB/request state too, without waiting in parent->child order. */
    if (FreeSpace && Fcb) {
        if (!Fcb->Header.Resource || !Fcb->FileInfo ||
            Fcb->FileInfo->Dloc != FileInfo->Dloc)
            return STATUS_FILE_INVALID;
        if (!ExIsResourceAcquiredExclusiveLite(Fcb->Header.Resource)) {
            if (!UDFAcquireFcbExclusive(IrpContext, Fcb, TRUE))
                return STATUS_DEVICE_BUSY;
            FcbAcquired = TRUE;
        }
    }
    if (FreeSpace && Fcb && Fcb->FcbCleanup) {
        if (FcbAcquired) UDFReleaseFcb(IrpContext, Fcb);
        return STATUS_CANNOT_DELETE;
    }
    if (FreeSpace && Fcb && !UDFIsADirectory(FileInfo)) {
        NTSTATUS ImageStatus = STATUS_SUCCESS;
        _SEH2_TRY {
            if (!MmFlushImageSection(&Fcb->FcbNonpaged->SegmentObject,
                UDFGetFileNameLinkCount(FileInfo) > 1 ? MmFlushForWrite : MmFlushForDelete))
                ImageStatus = STATUS_CANNOT_DELETE;
        } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
            ImageStatus = _SEH2_GetExceptionCode();
        } _SEH2_END;
        if (!NT_SUCCESS(ImageStatus)) {
            if (FcbAcquired) UDFReleaseFcb(IrpContext, Fcb);
            return ImageStatus;
        }
    }
    if (FreeSpace && ParentFcb && ParentFcb != Fcb) {
        if (!ParentFcb->Header.Resource) {
            if (FcbAcquired) UDFReleaseFcb(IrpContext, Fcb);
            return STATUS_FILE_INVALID;
        }
        if (!ExIsResourceAcquiredExclusiveLite(ParentFcb->Header.Resource)) {
            if (!UDFAcquireFcbExclusive(IrpContext, ParentFcb, TRUE)) {
                if (FcbAcquired) UDFReleaseFcb(IrpContext, Fcb);
                return STATUS_DEVICE_BUSY;
            }
            ParentAcquired = TRUE;
        }
    }
    _SEH2_TRY {
        Status = UDFUnlinkFileInternal(IrpContext, Vcb, FileInfo,
                                       FreeSpace, &NamespaceTouched);
        if (NT_SUCCESS(Status) && FreeSpace && FileInfo)
            UDFNoteCommittedUnlink(Vcb, Fcb, FileInfo);
    } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
        Status = _SEH2_GetExceptionCode();
        if (NamespaceTouched)
            Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
    } _SEH2_END;
    if (ParentAcquired)
        UDFReleaseFcb(IrpContext, ParentFcb);
    if (FcbAcquired)
        UDFReleaseFcb(IrpContext, Fcb);
    if (Touched)
        *Touched = NamespaceTouched;
    if (NamespaceTouched && !NT_SUCCESS(Status))
        UDFRememberMetadataWritebackError(IrpContext, Vcb, FileInfo, Status);
    return Status;
}

NTSTATUS
UDFUnlinkFile__(PIRP_CONTEXT IrpContext, PVCB Vcb,
                PUDF_FILE_INFO FileInfo, BOOLEAN FreeSpace)
{
    return UDFUnlinkFileEx__(IrpContext, Vcb, FileInfo, FreeSpace, NULL);
}


/* Synchronous stream deletion retains the exact SDir/child open until its
 * checked non-I/O release. Prefix and FCB lifetime belong to indexed teardown;
 * do not manually detach a still-owned LCB or manufacture a base-file delete
 * from a stream's canonical parent. NamespaceTouched includes descendants. */
static NTSTATUS
UDFDeleteAllStreamsEx(PIRP_CONTEXT IrpContext, PVCB Vcb,
                      PUDF_FILE_INFO FileInfo, PBOOLEAN NamespaceTouched)
{
    PUDF_DATALOC_INFO Owner = FileInfo ? FileInfo->Dloc : NULL;
    PUDF_FILE_INFO SDirInfo = NULL;
    PFCB SDirFcb = NULL;
    NTSTATUS Status = STATUS_SUCCESS, ReleaseStatus;
    BOOLEAN Opened = FALSE, SlotOwned = FALSE, SDirAcquired = FALSE;
    BOOLEAN Removed = FALSE, ChildTouched = FALSE;
    if (!Owner)
        return STATUS_FILE_INVALID;
    if (!UDFHasAStreamDir(FileInfo) || UDFIsSDirDeleted(Owner->SDirInfo))
        return STATUS_SUCCESS;
    if (Owner->StreamDeleteOwner)
        return STATUS_DEVICE_BUSY;
    _SEH2_TRY {
        _SEH2_TRY {
            Status = UDFOpenStreamDir__(IrpContext, Vcb, FileInfo, &SDirInfo);
            if (!NT_SUCCESS(Status)) _SEH2_LEAVE;
            Opened = TRUE;
            Status = UDFOwnStreamDeleteFile(Vcb, Owner, SDirInfo);
            if (!NT_SUCCESS(Status)) _SEH2_LEAVE;
            SlotOwned = TRUE;
            SDirFcb = SDirInfo->Dloc->CommonFcb;
            if (SDirFcb) {
                if (!SDirFcb->Header.Resource) {
                    Status = STATUS_FILE_INVALID;
                    _SEH2_LEAVE;
                }
                if (!ExIsResourceAcquiredExclusiveLite(SDirFcb->Header.Resource)) {
                    if (!UDFAcquireFcbExclusive(IrpContext, SDirFcb, TRUE)) {
                        Status = STATUS_DEVICE_BUSY;
                        _SEH2_LEAVE;
                    }
                    SDirAcquired = TRUE;
                }
            }
            Status = UDFFlushFile__(IrpContext, Vcb, SDirInfo, 0);
            if (NT_SUCCESS(Status))
                Status = UDFUnlinkFileEx__(IrpContext, Vcb, SDirInfo, TRUE,
                                           &ChildTouched);
        } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
            Status = _SEH2_GetExceptionCode();
        } _SEH2_END;
    } _SEH2_FINALLY {
        if (ChildTouched)
            *NamespaceTouched = TRUE;
        if (Opened) {
            ReleaseStatus = SlotOwned ?
                UDFReleaseStreamDeleteFile(Vcb, Owner, &SDirInfo, NT_SUCCESS(Status)) :
                UDFAbortStreamDeleteFile(Vcb, Owner, &SDirInfo);
            Opened = FALSE;
            if (NT_SUCCESS(Status)) Status = ReleaseStatus;
            if (NT_SUCCESS(Status)) {
                if (SDirFcb && SDirAcquired) {
                    SDirInfo = NULL; /* teardown may free the complete FI ring */
                    Status = UDFTeardownClosedStream(IrpContext, Vcb, SDirFcb, &Removed);
                } /* Cold FI disposal used the atomic release/claim above. */
            }
        }
        ReleaseStatus = UDFRetainFailedStreamOpen(Vcb, &SDirInfo);
        if (NT_SUCCESS(Status)) Status = ReleaseStatus;
        if (SDirAcquired && !Removed)
            UDFReleaseFcb(IrpContext, SDirFcb);
    } _SEH2_END;
    if (!NT_SUCCESS(Status) && *NamespaceTouched) {
        Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
        InterlockedCompareExchange(&Vcb->MetadataWritebackError, Status, STATUS_SUCCESS);
    }
    return Status;
}

NTSTATUS
UDFDeleteAllStreams(PIRP_CONTEXT IrpContext, PVCB Vcb, PUDF_FILE_INFO FileInfo)
{
    BOOLEAN NamespaceTouched = FALSE;
    return UDFDeleteAllStreamsEx(IrpContext, Vcb, FileInfo, &NamespaceTouched);
}

static NTSTATUS
UDFUnlinkAllFilesInDirEx(PIRP_CONTEXT IrpContext, PVCB Vcb,
                        PUDF_FILE_INFO DirInfo, PBOOLEAN NamespaceTouched)
{
    PUDF_DATALOC_INFO Owner = DirInfo ? DirInfo->Dloc : NULL;
    PUDF_FILE_INFO FileInfo = NULL;
    PFCB ChildFcb = NULL;
    NTSTATUS Status = STATUS_SUCCESS, ReleaseStatus;
    BOOLEAN Opened = FALSE, SlotOwned = FALSE, ChildAcquired = FALSE;
    BOOLEAN Removed = FALSE, ChildTouched = FALSE;
    uint_di Index, Limit;
    if (!Owner || !Owner->DirIndex || !UDFIsAStreamDir(DirInfo))
        return STATUS_FILE_INVALID;
    if (Owner->StreamDeleteOwner)
        return STATUS_DEVICE_BUSY;
    /* Every supported stream passes link/image/live-handle preflight before
     * the first tombstone. Recheck each FCB under its own lock during unlink
     * too, since a cold by-ID open need not use the parent's name tree. */
    Status = UDFPreflightStreamDirectoryDelete(IrpContext, Vcb, DirInfo);
    if (!NT_SUCCESS(Status))
        return Status;
    Limit = UDFDirIndexGetLastIndex(Owner->DirIndex);
    for (Index = 2; Index < Limit; Index++) {
        FileInfo = NULL;
        ChildFcb = NULL;
        Opened = SlotOwned = ChildAcquired = Removed = ChildTouched = FALSE;
        _SEH2_TRY {
            _SEH2_TRY {
                Status = UDFOpenFile__(IrpContext, Vcb, FALSE, TRUE, NULL,
                                       DirInfo, &FileInfo, &Index);
                if (Status == STATUS_FILE_DELETED) {
                    Status = STATUS_SUCCESS;
                    _SEH2_LEAVE;
                }
                if (!NT_SUCCESS(Status)) _SEH2_LEAVE;
                Opened = TRUE;
                Status = UDFOwnStreamDeleteFile(Vcb, Owner, FileInfo);
                if (!NT_SUCCESS(Status)) _SEH2_LEAVE;
                SlotOwned = TRUE;
                ChildFcb = FileInfo->Dloc->CommonFcb;
                if (ChildFcb) {
                    if (!UDFAcquireFcbExclusive(IrpContext, ChildFcb, TRUE)) {
                        Status = STATUS_DEVICE_BUSY;
                        _SEH2_LEAVE;
                    }
                    ChildAcquired = TRUE;
                    if (ChildFcb->FcbCleanup) {
                        Status = STATUS_CANNOT_DELETE;
                        _SEH2_LEAVE;
                    }
                }
                Status = UDFFlushFile__(IrpContext, Vcb, FileInfo, 0);
                if (NT_SUCCESS(Status))
                    Status = UDFUnlinkFileEx__(IrpContext, Vcb, FileInfo, TRUE,
                                               &ChildTouched);
            } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
                Status = _SEH2_GetExceptionCode();
            } _SEH2_END;
        } _SEH2_FINALLY {
            if (ChildTouched)
                *NamespaceTouched = TRUE;
            if (Opened) {
                ReleaseStatus = SlotOwned ?
                    UDFReleaseStreamDeleteFile(Vcb, Owner, &FileInfo, NT_SUCCESS(Status)) :
                    UDFAbortStreamDeleteFile(Vcb, Owner, &FileInfo);
                Opened = FALSE;
                if (NT_SUCCESS(Status)) Status = ReleaseStatus;
                if (NT_SUCCESS(Status)) {
                    if (ChildFcb && ChildAcquired) {
                        FileInfo = NULL; /* teardown may dispose all parallel FIs */
                        Status = UDFTeardownClosedStream(IrpContext, Vcb, ChildFcb, &Removed);
                    } /* Cold FI disposal used the atomic release/claim. */
                }
            }
            ReleaseStatus = UDFRetainFailedStreamOpen(Vcb, &FileInfo);
            if (NT_SUCCESS(Status)) Status = ReleaseStatus;
            if (ChildAcquired && !Removed)
                UDFReleaseFcb(IrpContext, ChildFcb);
        } _SEH2_END;
        if (!NT_SUCCESS(Status)) {
            if (*NamespaceTouched) {
                Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
                InterlockedCompareExchange(&Vcb->MetadataWritebackError, Status, STATUS_SUCCESS);
            }
            return Status;
        }
    }
    return STATUS_SUCCESS;
}

NTSTATUS
UDFUnlinkAllFilesInDir(PIRP_CONTEXT IrpContext, PVCB Vcb, PUDF_FILE_INFO DirInfo)
{
    BOOLEAN NamespaceTouched = FALSE;
    return UDFUnlinkAllFilesInDirEx(IrpContext, Vcb, DirInfo, &NamespaceTouched);
}

/*
    This routine inits UDF_FILE_INFO structure for specified file
    If it returns status != STATUS_SUCCESS caller should call UDFCleanUpFile__
    for returned pointer *WITHOUT* using UDFCloseFile__
 */
NTSTATUS
UDFOpenFile__(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN BOOLEAN IgnoreCase,
    IN BOOLEAN NotDeleted,
    IN PUNICODE_STRING fn,
    IN PUDF_FILE_INFO DirInfo,
    OUT PUDF_FILE_INFO* _FileInfo,// this is to be filled & doesn't contain
                                // any pointers
    IN uint_di* IndexToOpen
    )
{
    NTSTATUS status;
    BOOLEAN ClonedReference = FALSE;
    uint_di i=0;
    EXTENT_AD FEExt;
    uint16 Ident;
    PDIR_INDEX_HDR hDirNdx = DirInfo->Dloc->DirIndex;
    PDIR_INDEX_ITEM DirNdx;
    PUDF_FILE_INFO FileInfo;
    PUDF_FILE_INFO ParFileInfo;
    *_FileInfo = NULL;
    if (!hDirNdx) return STATUS_NOT_A_DIRECTORY;

    // find specified file in directory index
    // if it is already known, skip this foolish code
    if (IndexToOpen) {
        i=*IndexToOpen;
    } else
        if (!NT_SUCCESS(status = UDFFindFile(Vcb, IgnoreCase, NotDeleted, fn, DirInfo, &i)))
            return status;
    // do this check for OpenByIndex
    // some routines may send invalid Index
    if (!(DirNdx = UDFDirIndex(hDirNdx,i)))
        return STATUS_OBJECT_NAME_NOT_FOUND;
    if ((FileInfo = DirNdx->FileInfo)) {
        // file is already opened.
        if ((DirNdx->FileCharacteristics & FILE_DELETED) && NotDeleted) {
            AdPrint(("  FILE_DELETED on open\n"));
            return STATUS_FILE_DELETED;
        }
        if ((FileInfo->ParentFile != DirInfo) &&
           (FileInfo->Index >= 2)) {
            ParFileInfo = UDFLocateParallelFI(DirInfo, i, FileInfo);
            BrutePoint();
            if (ParFileInfo->ParentFile != DirInfo) {
                FileInfo = (PUDF_FILE_INFO)MyAllocatePoolTag__(UDF_FILE_INFO_MT,sizeof(UDF_FILE_INFO), MEM_FINF_TAG);
                *_FileInfo = FileInfo;
                if (!FileInfo) return STATUS_INSUFFICIENT_RESOURCES;
                RtlCopyMemory(FileInfo, DirNdx->FileInfo, sizeof(UDF_FILE_INFO));
    //          FileInfo->NextLinkedFile = DirNdx->FileInfo->NextLinkedFile; // is already done
                FileInfo->RefCount = 0;
                FileInfo->OpenCount = 0;
                FileInfo->ListPtr = NULL;
                FileInfo->ParentFile = DirInfo;
                FileInfo->Fcb = NULL;
                status = UDFReferenceClonedFile(Vcb, FileInfo, DirNdx->FileInfo);
                if (!NT_SUCCESS(status)) {
                    MyFreePool__(FileInfo); /* shared FID/Dloc belong to Source */
                    *_FileInfo = NULL;
                    return status;
                }
                ClonedReference = TRUE;
                DirNdx->FI_Flags |= UDF_FI_FLAG_LINKED;
            } else {
                FileInfo = ParFileInfo;
            }
        }
        // Just increase some counters & exit
        if (!ClonedReference) {
            status = UDFReferenceFileChecked(Vcb, FileInfo);
            if (!NT_SUCCESS(status))
                return status;
        }

        ASSERT(FileInfo->ParentFile == DirInfo);
        ValidateFileInfo(FileInfo);

        *_FileInfo = FileInfo;
        return STATUS_SUCCESS;
    } else
    if (IndexToOpen) {
        if ((DirNdx->FileCharacteristics & FILE_DELETED) && NotDeleted) {
            AdPrint(("  FILE_DELETED on open (2)\n"));
            return STATUS_FILE_DELETED;
        }
    }
    FileInfo = (PUDF_FILE_INFO)MyAllocatePoolTag__(UDF_FILE_INFO_MT,sizeof(UDF_FILE_INFO), MEM_FINF_TAG);
    *_FileInfo = FileInfo;
    if (!FileInfo) return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(FileInfo, sizeof(UDF_FILE_INFO));
    // init horizontal links
    FileInfo->NextLinkedFile =
    FileInfo->PrevLinkedFile = FileInfo;
    // read FileIdent
    FileInfo->FileIdent = (PFILE_IDENT_DESC)MyAllocatePoolTag__(NonPagedPool, DirNdx->Length, MEM_FID_TAG);
    if (!(FileInfo->FileIdent)) return STATUS_INSUFFICIENT_RESOURCES;
    FileInfo->FileIdentLen = DirNdx->Length;
    if (!NT_SUCCESS(status = UDFReadFile__(IrpContext, Vcb, DirInfo, DirNdx->Offset,
                             DirNdx->Length, FALSE, (int8*)(FileInfo->FileIdent)) ))
        return status;
    if (FileInfo->FileIdent->descTag.tagIdent != TID_FILE_IDENT_DESC) {
        BrutePoint();
        return STATUS_FILE_CORRUPT_ERROR;
    }
    FileInfo->Index = i;
    FileInfo->ParentFile = DirInfo;
    // check for opened links
    if (!NT_SUCCESS(status = UDFStoreDloc(Vcb, FileInfo, UDFPartLbaToPhys(Vcb, &(FileInfo->FileIdent->icb.extLocation)))))
        return status;
    if (FileInfo->Dloc->FileEntry) {
        // Dloc cached in UDFOpenFile__ — skip FE read from disk
        goto init_tree_entry;
    }
    // read (Ex)FileEntry
    FileInfo->Dloc->FileEntry = (tag*)MyAllocatePoolTag__(NonPagedPool, Vcb->SectorSize, MEM_FE_TAG);
    FileInfo->Dloc->FileEntryCapacity = FileInfo->Dloc->FileEntry ? Vcb->SectorSize : 0;
    if (!(FileInfo->Dloc->FileEntry)) return STATUS_INSUFFICIENT_RESOURCES;
    status = UDFReadFileEntry(IrpContext, Vcb, &FileInfo->FileIdent->icb, (PFILE_ENTRY)(FileInfo->Dloc->FileEntry), &Ident);

    if (!NT_SUCCESS(status)) {
        return status;
    }

    // build mappings for Data & AllocDescs
    if (!FileInfo->Dloc->AllocLoc.Mapping) {
        FEExt.extLength = FileInfo->FileIdent->icb.extLength;
        FEExt.extLocation = UDFPartLbaToPhys(Vcb, &(FileInfo->FileIdent->icb.extLocation) );
        if (FEExt.extLocation == LBA_OUT_OF_EXTENT) {
            return STATUS_FILE_CORRUPT_ERROR;
        }
        FileInfo->Dloc->AllocLoc.Mapping = UDFExtentToMapping(&FEExt);
        if (!(FileInfo->Dloc->AllocLoc.Mapping))
            return STATUS_INSUFFICIENT_RESOURCES;
    }
    // read location info
    status = UDFLoadExtInfo(IrpContext, Vcb, (PFILE_ENTRY)(FileInfo->Dloc->FileEntry), &FileInfo->FileIdent->icb,
                           &FileInfo->Dloc->DataLoc, &FileInfo->Dloc->AllocLoc);
    if (!NT_SUCCESS(status))
        return status;
    // init (Ex)FileEntry mapping
    FileInfo->Dloc->FELoc.Length = (FileInfo->Dloc->DataLoc.Offset) ? FileInfo->Dloc->DataLoc.Offset :
                                                          FileInfo->Dloc->AllocLoc.Offset;
//    FileInfo->Dloc->FELoc.Offset = 0;
    FileInfo->Dloc->FELoc.Mapping = UDFExtentToMapping(&FEExt);
    FileInfo->Dloc->FileEntryLen = (uint32)(FileInfo->Dloc->FELoc.Length);
    // we get here immediately when opened link encountered
init_tree_entry:
    // init back pointer from parent object
    ASSERT(!DirNdx->FileInfo);
    DirNdx->FileInfo = FileInfo;
    // init DirIndex
    if (UDFGetFileLinkCount(FileInfo) > 1) {
        DirNdx->FI_Flags |= UDF_FI_FLAG_LINKED;
    } else {
        DirNdx->FI_Flags &= ~UDF_FI_FLAG_LINKED;
    }
    // Keep FE buffer at SectorSize — FlushFE may write full sector for in-ICB mode
    // check if this file has a SDir
    if ((FileInfo->Dloc->FileEntry->tagIdent == TID_EXTENDED_FILE_ENTRY) &&
       ((PEXTENDED_FILE_ENTRY)(FileInfo->Dloc->FileEntry))->streamDirectoryICB.extLength )
        FileInfo->Dloc->FE_Flags |= UDF_FE_FLAG_HAS_SDIR;
    if (!(FileInfo->FileIdent->fileCharacteristics & FILE_DIRECTORY)) {
        status = UDFCompleteFileInfoConstruction(Vcb, FileInfo);
        if (!NT_SUCCESS(status)) return status;
        ASSERT(FileInfo->ParentFile == DirInfo);
        return STATUS_SUCCESS;
    }

    UDFCheckSpaceAllocation(Vcb, 0, FileInfo->Dloc->DataLoc.Mapping, AS_USED); // check if used

    // build index for directories
    if (!FileInfo->Dloc->DirIndex) {
        status = UDFIndexDirectory(IrpContext, Vcb, FileInfo);
        if (!NT_SUCCESS(status))
            return status;
    }
    status = UDFCompleteFileInfoConstruction(Vcb, FileInfo);
    if (!NT_SUCCESS(status)) return status;
    ASSERT(FileInfo->ParentFile == DirInfo);

    return status;
} // end UDFOpenFile__()


/*
    Open file from directory context.
    This is called after UDFFindDirEntry to open the found file.
 */
NTSTATUS
UDFOpenFileInfoFromDirContext(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN PDIR_ENUM_CONTEXT DirContext,
    IN BOOLEAN NotDeleted,
    OUT PUDF_FILE_INFO* _FileInfo
    )
{
    NTSTATUS status;
    BOOLEAN ClonedReference = FALSE;
    EXTENT_AD FEExt;
    uint16 Ident;
    PDIR_INDEX_ITEM DirNdx = DirContext->DirNdx;
    PUDF_FILE_INFO DirInfo = DirContext->ParentInfo;
    PUDF_FILE_INFO FileInfo;
    PUDF_FILE_INFO ParFileInfo;
    uint_di i = DirContext->Index;

    *_FileInfo = NULL;

    if (!DirNdx) {
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    if ((FileInfo = DirNdx->FileInfo)) {
        // file is already opened
        if ((DirNdx->FileCharacteristics & FILE_DELETED) && NotDeleted) {
            AdPrint(("  FILE_DELETED on open\n"));
            return STATUS_FILE_DELETED;
        }
        if ((FileInfo->ParentFile != DirInfo) &&
           (FileInfo->Index >= 2)) {
            ParFileInfo = UDFLocateParallelFI(DirInfo, i, FileInfo);
            if (!ParFileInfo || ParFileInfo->ParentFile != DirInfo) {
                FileInfo = (PUDF_FILE_INFO)MyAllocatePoolTag__(UDF_FILE_INFO_MT, sizeof(UDF_FILE_INFO), MEM_FINF_TAG);
                *_FileInfo = FileInfo;
                if (!FileInfo) return STATUS_INSUFFICIENT_RESOURCES;
                RtlCopyMemory(FileInfo, DirNdx->FileInfo, sizeof(UDF_FILE_INFO));
                FileInfo->RefCount = 0;
                FileInfo->OpenCount = 0;
                FileInfo->ListPtr = NULL;
                FileInfo->ParentFile = DirInfo;
                FileInfo->Fcb = NULL;
                status = UDFReferenceClonedFile(Vcb, FileInfo, DirNdx->FileInfo);
                if (!NT_SUCCESS(status)) {
                    MyFreePool__(FileInfo); /* shared FID/Dloc belong to Source */
                    *_FileInfo = NULL;
                    return status;
                }
                ClonedReference = TRUE;
                DirNdx->FI_Flags |= UDF_FI_FLAG_LINKED;
            } else {
                FileInfo = ParFileInfo;
            }
        }
        // Just increase some counters & exit
        if (!ClonedReference) {
            status = UDFReferenceFileChecked(Vcb, FileInfo);
            if (!NT_SUCCESS(status))
                return status;
        }

        ASSERT(FileInfo->ParentFile == DirInfo);
        ValidateFileInfo(FileInfo);

        *_FileInfo = FileInfo;
        return STATUS_SUCCESS;
    }

    // Check deleted for new open
    if ((DirNdx->FileCharacteristics & FILE_DELETED) && NotDeleted) {
        AdPrint(("  FILE_DELETED on open (2)\n"));
        return STATUS_FILE_DELETED;
    }

    FileInfo = (PUDF_FILE_INFO)MyAllocatePoolTag__(UDF_FILE_INFO_MT, sizeof(UDF_FILE_INFO), MEM_FINF_TAG);
    *_FileInfo = FileInfo;
    if (!FileInfo) return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(FileInfo, sizeof(UDF_FILE_INFO));
    // init horizontal links
    FileInfo->NextLinkedFile =
    FileInfo->PrevLinkedFile = FileInfo;
    // read FileIdent
    FileInfo->FileIdent = (PFILE_IDENT_DESC)MyAllocatePoolTag__(NonPagedPool, DirNdx->Length, MEM_FID_TAG);
    if (!(FileInfo->FileIdent)) return STATUS_INSUFFICIENT_RESOURCES;
    FileInfo->FileIdentLen = DirNdx->Length;
    if (!NT_SUCCESS(status = UDFReadFile__(IrpContext, Vcb, DirInfo, DirNdx->Offset,
                             DirNdx->Length, FALSE, (int8*)(FileInfo->FileIdent))))
        return status;
    if (FileInfo->FileIdent->descTag.tagIdent != TID_FILE_IDENT_DESC) {
        BrutePoint();
        return STATUS_FILE_CORRUPT_ERROR;
    }
    FileInfo->Index = i;
    FileInfo->ParentFile = DirInfo;
    // check for opened links
    if (!NT_SUCCESS(status = UDFStoreDloc(Vcb, FileInfo, UDFPartLbaToPhys(Vcb, &(FileInfo->FileIdent->icb.extLocation)))))
        return status;
    if (FileInfo->Dloc->FileEntry)
        goto init_tree_entry;
    // read (Ex)FileEntry
    FileInfo->Dloc->FileEntry = (tag*)MyAllocatePoolTag__(NonPagedPool, Vcb->SectorSize, MEM_FE_TAG);
    FileInfo->Dloc->FileEntryCapacity = FileInfo->Dloc->FileEntry ? Vcb->SectorSize : 0;
    if (!(FileInfo->Dloc->FileEntry)) return STATUS_INSUFFICIENT_RESOURCES;
    status = UDFReadFileEntry(IrpContext, Vcb, &FileInfo->FileIdent->icb, (PFILE_ENTRY)(FileInfo->Dloc->FileEntry), &Ident);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    // build mappings for Data & AllocDescs
    FEExt.extLength = FileInfo->FileIdent->icb.extLength;
    FEExt.extLocation = UDFPartLbaToPhys(Vcb, &(FileInfo->FileIdent->icb.extLocation));
    if (FEExt.extLocation == LBA_OUT_OF_EXTENT)
        return STATUS_FILE_CORRUPT_ERROR;
    if (!FileInfo->Dloc->AllocLoc.Mapping) {
        FileInfo->Dloc->AllocLoc.Mapping = UDFExtentToMapping(&FEExt);
        if (!(FileInfo->Dloc->AllocLoc.Mapping))
            return STATUS_INSUFFICIENT_RESOURCES;
    }
    // read location info
    status = UDFLoadExtInfo(IrpContext, Vcb, (PFILE_ENTRY)(FileInfo->Dloc->FileEntry), &FileInfo->FileIdent->icb,
                           &FileInfo->Dloc->DataLoc, &FileInfo->Dloc->AllocLoc);
    if (!NT_SUCCESS(status))
        return status;
    // init (Ex)FileEntry mapping
    FileInfo->Dloc->FELoc.Length = (FileInfo->Dloc->DataLoc.Offset) ? FileInfo->Dloc->DataLoc.Offset :
                                                          FileInfo->Dloc->AllocLoc.Offset;
    FileInfo->Dloc->FELoc.Mapping = UDFExtentToMapping(&FEExt);
    if (!FileInfo->Dloc->FELoc.Mapping) return STATUS_INSUFFICIENT_RESOURCES;
    FileInfo->Dloc->FileEntryLen = (uint32)(FileInfo->Dloc->FELoc.Length);
    // we get here immediately when opened link encountered
init_tree_entry:
    // init back pointer from parent object
    ASSERT(!DirNdx->FileInfo);
    DirNdx->FileInfo = FileInfo;
    // init DirIndex
    if (UDFGetFileLinkCount(FileInfo) > 1) {
        DirNdx->FI_Flags |= UDF_FI_FLAG_LINKED;
    } else {
        DirNdx->FI_Flags &= ~UDF_FI_FLAG_LINKED;
    }
    // Keep FE buffer at SectorSize — FlushFE may write full sector for in-ICB mode
    // check if this file has a SDir
    if ((FileInfo->Dloc->FileEntry->tagIdent == TID_EXTENDED_FILE_ENTRY) &&
       ((PEXTENDED_FILE_ENTRY)(FileInfo->Dloc->FileEntry))->streamDirectoryICB.extLength)
        FileInfo->Dloc->FE_Flags |= UDF_FE_FLAG_HAS_SDIR;
    if (!(FileInfo->FileIdent->fileCharacteristics & FILE_DIRECTORY)) {
        status = UDFCompleteFileInfoConstruction(Vcb, FileInfo);
        if (!NT_SUCCESS(status)) return status;
        ASSERT(FileInfo->ParentFile == DirInfo);
        return STATUS_SUCCESS;
    }

    UDFCheckSpaceAllocation(Vcb, 0, FileInfo->Dloc->DataLoc.Mapping, AS_USED);

    // build index for directories
    if (!FileInfo->Dloc->DirIndex) {
        status = UDFIndexDirectory(IrpContext, Vcb, FileInfo);
        if (!NT_SUCCESS(status)) {
            return status;
        }
    }
    status = UDFCompleteFileInfoConstruction(Vcb, FileInfo);
    if (!NT_SUCCESS(status)) return status;
    ASSERT(FileInfo->ParentFile == DirInfo);

    return status;
} // end UDFOpenFileInfoFromDirContext()


/*
    This routine inits UDF_FILE_INFO structure for root directory
 */
static NTSTATUS
UDFOpenRootFileWithParent__(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN lb_addr* RootLoc,
    IN PUDF_FILE_INFO Parent,
    IN uint8 ExpectedType, /* zero retains the generic root/by-ID contract */
    OUT PUDF_FILE_INFO FileInfo
    )
{
    uint32 RootLBA;
    NTSTATUS status;
//    uint32 PartNum = RootLoc->partitionReferenceNum;
    uint32 LBS;
    uint16 Ident;
    LONG_AD FELoc;
    EXTENT_AD FEExt;
    uint8 FileType;

    if (!FileInfo || FileInfo == Parent) return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(FileInfo, sizeof(UDF_FILE_INFO));
    if (!IrpContext || !Vcb || !RootLoc || !Vcb->Partitions ||
        RootLoc->partitionReferenceNum >= Vcb->PartitionMaps ||
        (Parent && (!Parent->Dloc || !Parent->Dloc->FELoc.Mapping)))
        return STATUS_INVALID_PARAMETER;
    LBS = Vcb->SectorSize;
    RootLBA = UDFPartLbaToPhys(Vcb,RootLoc);
    if (RootLBA == LBA_OUT_OF_EXTENT)
        return STATUS_FILE_CORRUPT_ERROR;
    if (Parent && Parent->Dloc->FELoc.Mapping[0].extLocation == RootLBA)
        return STATUS_FILE_CORRUPT_ERROR;
    FELoc.extLocation = *RootLoc;
    FELoc.extLength = LBS;
    // init horizontal links
    FileInfo->NextLinkedFile =
    FileInfo->PrevLinkedFile = FileInfo;
    /* UDFStoreDloc acquires the constructor's exact FI/Dloc/parent hold.
     * Bind Parent before that acquisition, not after successful completion. */
    FileInfo->ParentFile = Parent;
    if (!NT_SUCCESS(status = UDFStoreDloc(Vcb, FileInfo, RootLBA))) {
        if (!FileInfo->Dloc) FileInfo->ParentFile = NULL; /* no hold was acquired */
        return status;
    }
    if (FileInfo->Dloc->FileEntry)
        goto init_tree_entry;
    // read (Ex)FileEntry
    FileInfo->Dloc->FileEntry = (tag*)MyAllocatePoolTag__(NonPagedPool, LBS, MEM_FE_TAG);
    FileInfo->Dloc->FileEntryCapacity = FileInfo->Dloc->FileEntry ? LBS : 0;
    if (!(FileInfo->Dloc->FileEntry)) return STATUS_INSUFFICIENT_RESOURCES;

    if (!NT_SUCCESS(status = UDFReadFileEntry(IrpContext, Vcb, &FELoc, (PFILE_ENTRY)(FileInfo->Dloc->FileEntry), &Ident)))
        return status;
    /* Type-constrained internal opens do not parse a different object's
     * payload as a metadata/stream mapping, or acquire a successful open
     * which the caller must later guess how to undo. */
    FileType = ((icbtag*)(FileInfo->Dloc->FileEntry + 1))->fileType;
    if (ExpectedType && FileType != ExpectedType) {
        UDFPrint(("UDFS: internal type %x expected %x at LBA %x\n",
                  FileType, ExpectedType, RootLBA));
        return STATUS_FILE_CORRUPT_ERROR;
    }
    // build mappings for Data & AllocDescs
    FEExt.extLength = LBS;
    FEExt.extLocation = UDFPartLbaToPhys(Vcb, &(FELoc.extLocation) );
    if (FEExt.extLocation == LBA_OUT_OF_EXTENT)
        return STATUS_FILE_CORRUPT_ERROR;
    FileInfo->Dloc->FELoc.Mapping = UDFExtentToMapping(&FEExt);
    if (!(FileInfo->Dloc->FELoc.Mapping)) return STATUS_INSUFFICIENT_RESOURCES;
    // build mappings for AllocDescs
    if (!FileInfo->Dloc->AllocLoc.Mapping) {
        FileInfo->Dloc->AllocLoc.Mapping = UDFExtentToMapping(&FEExt);
        if (!(FileInfo->Dloc->AllocLoc.Mapping)) return STATUS_INSUFFICIENT_RESOURCES;
    }
    if (!NT_SUCCESS(status = UDFLoadExtInfo(IrpContext, Vcb, (PFILE_ENTRY)(FileInfo->Dloc->FileEntry), &FELoc,
                       &FileInfo->Dloc->DataLoc, &FileInfo->Dloc->AllocLoc)))
        return status;
    FileInfo->Dloc->FileEntryLen = (uint32)
    (FileInfo->Dloc->FELoc.Length = (FileInfo->Dloc->DataLoc.Offset) ? FileInfo->Dloc->DataLoc.Offset :
                                                          FileInfo->Dloc->AllocLoc.Offset);
init_tree_entry:
    // Keep FE buffer at SectorSize — FlushFE may write full sector for in-ICB mode
    // init DirIndex
    FileType = ((icbtag*)(FileInfo->Dloc->FileEntry + 1))->fileType;
    /* A stream pointer must not complete a successful open of a different
     * object kind and leave the caller to undo an ordinary open reference. */
    if ((ExpectedType && FileType != ExpectedType) ||
        (Parent && FileType != UDF_FILE_TYPE_STREAMDIR)) {
        UDFPrint(("UDFS: cached internal type %x expected %x at LBA %x\n",
                  FileType, ExpectedType, RootLBA));
        return STATUS_FILE_CORRUPT_ERROR;
    }
    if (FileType == UDF_FILE_TYPE_STREAMDIR) {
        LONG OldFlags;
        /* A cached Dloc may already have a canonical writer. Setting this
         * immutable role bit must not overwrite concurrent dirty flags. */
        do {
            OldFlags = FileInfo->Dloc->FE_Flags;
        } while (InterlockedCompareExchange((PLONG)&FileInfo->Dloc->FE_Flags,
                     OldFlags | UDF_FE_FLAG_IS_SDIR, OldFlags) != OldFlags);
    }
    if (FileType != UDF_FILE_TYPE_DIRECTORY && FileType != UDF_FILE_TYPE_STREAMDIR) {
        status = UDFCompleteFileInfoConstruction(Vcb, FileInfo);
        if (!NT_SUCCESS(status)) return status;
        return STATUS_SUCCESS;
    }
    // build index for directories
    if (!FileInfo->Dloc->DirIndex) {
        status = UDFIndexDirectory(IrpContext, Vcb, FileInfo);
        if (!NT_SUCCESS(status))
            return status;

    }
    status = UDFCompleteFileInfoConstruction(Vcb, FileInfo);
    if (!NT_SUCCESS(status)) return status;

    return status;
} // end UDFOpenRootFileWithParent__()

/* Preserve the existing parentless root/VAT/metadata/open-by-ID contract. */
NTSTATUS
UDFOpenRootFile__(PIRP_CONTEXT IrpContext, PVCB Vcb, lb_addr* RootLoc,
                   PUDF_FILE_INFO FileInfo)
{
    return UDFOpenRootFileWithParent__(IrpContext, Vcb, RootLoc, NULL, 0, FileInfo);
} // end UDFOpenRootFile__()

/*
    This routine frees all memory blocks referenced by given FileInfo
 */
uint32
UDFCleanUpFile__(PVCB Vcb, PUDF_FILE_INFO FileInfo)
{
    NTSTATUS CleanupStatus;
    return UDFCleanUpFileWithStatus(Vcb, FileInfo, &CleanupStatus);
}

static uint32
UDFCleanUpFileWithStatus(PVCB Vcb, PUDF_FILE_INFO FileInfo,
                         PNTSTATUS CleanupStatus)
{
    PUDF_DATALOC_INFO Dloc = FileInfo ? FileInfo->Dloc : NULL;
    uint32 Result = UDF_FREE_NOTHING;
    BOOLEAN VolumeAcquired = FALSE;
    *CleanupStatus = STATUS_DEVICE_BUSY;
    if (!Dloc)
        return UDFCleanUpFileInternal(Vcb, FileInfo, CleanupStatus);
    if (!NT_SUCCESS(Dloc->AllocationReleaseError)) {
        *CleanupStatus = Dloc->AllocationReleaseError;
        return UDF_FREE_NOTHING;
    }
    if (InterlockedCompareExchange(&Vcb->RetainedOpenCount, 0, 0)) {
        FileInfo->CleanupPending = TRUE;
        return UDF_FREE_NOTHING;
    }

    if (!ExIsResourceAcquiredExclusiveLite(&Vcb->VcbResource)) {
        if (ExAcquireResourceExclusiveLite(&Vcb->VcbResource, FALSE))
            VolumeAcquired = TRUE;
        else {
            UDFAcquireResourceExclusive(&Vcb->DlocResource2, TRUE);
            if (FileInfo->Constructing)
                FileInfo->ConstructionFailed = TRUE;
            FileInfo->CleanupPending = TRUE;
            UDFReleaseResource(&Vcb->DlocResource2);
            return UDF_FREE_NOTHING;
        }
    }
    UDFAcquireResourceExclusive(&Vcb->DlocResource2, TRUE);
    if (Dloc->CleanupOwner || Dloc->CreateConstructionOwner ||
        FileInfo->CreateConstructionParent || Dloc->StreamDeleteContextOwner || Dloc->StreamCreate) {
        /* A failed open retains its exact construction hold, reachable
         * from the Dloc ring, rather than stealing another cleanup claim. */
        if (FileInfo->Constructing)
            FileInfo->ConstructionFailed = TRUE;
        FileInfo->CleanupPending = TRUE;
        UDFReleaseResource(&Vcb->DlocResource2);
        if (VolumeAcquired) ExReleaseResourceLite(&Vcb->VcbResource);
        return UDF_FREE_NOTHING;
    }
    Dloc->CleanupOwner = PsGetCurrentThread();
    UDFReleaseResource(&Vcb->DlocResource2);
    _SEH2_TRY {
        if (FileInfo->Constructing)
            UDFDropFileInfoConstruction(Vcb, FileInfo);
        Result = UDFCleanUpFileInternal(Vcb, FileInfo, CleanupStatus);
        if (!(Result & UDF_FREE_FILEINFO))
            FileInfo->CleanupPending = TRUE;
    } _SEH2_FINALLY {
        if (_SEH2_AbnormalTermination() ||
            (*CleanupStatus != STATUS_SUCCESS && *CleanupStatus != STATUS_DEVICE_BUSY)) {
            /* Do not revive a partly destroyed generation after an
             * exception. Keep the claim and report the failure at flush. */
            Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
            InterlockedCompareExchange(&Vcb->MetadataWritebackError,
                _SEH2_AbnormalTermination() ? STATUS_UNSUCCESSFUL : *CleanupStatus, STATUS_SUCCESS);
        } else if (!(Result & UDF_FREE_DLOC)) {
            UDFAcquireResourceExclusive(&Vcb->DlocResource2, TRUE);
            Dloc->CleanupOwner = NULL;
            UDFReleaseResource(&Vcb->DlocResource2);
        }
        if (VolumeAcquired) ExReleaseResourceLite(&Vcb->VcbResource);
    } _SEH2_END;
    return Result;
}

uint32
UDFCleanUpFileForFcb(PVCB Vcb, PUDF_FILE_INFO FileInfo, PNTSTATUS CleanupStatus)
{
    *CleanupStatus = STATUS_INVALID_DEVICE_STATE;
    /* The indexed FCB owns this claim through the caller's decision. */
    if (!ExIsResourceAcquiredExclusiveLite(&Vcb->VcbResource) ||
        !FileInfo || !FileInfo->Dloc ||
        FileInfo->Dloc->CleanupOwner != PsGetCurrentThread())
        return UDF_FREE_NOTHING;
    return UDFCleanUpFileInternal(Vcb, FileInfo, CleanupStatus);
}

/* Each mount-owned internal FI contributes exactly one open reference.
 * A retry may flush/dispose it, but must not consume another owner's hold. */
NTSTATUS
UDFReleaseInternalFile(PIRP_CONTEXT IrpContext, PVCB Vcb,
                       PUDF_FILE_INFO* FileInfo)
{
    PUDF_FILE_INFO Owned;
    NTSTATUS Status = STATUS_SUCCESS, CleanupStatus = STATUS_DEVICE_BUSY;
    if (!FileInfo) return STATUS_INVALID_PARAMETER;
    Owned = *FileInfo;
    if (!Owned)
        return STATUS_SUCCESS;
    if (!Owned->InternalOpenReleased) {
        if (!Owned->Constructing && !Owned->ConstructionFailed && Owned->Dloc) {
            UDFAcquireResourceExclusive(&Vcb->DlocResource2, TRUE);
            _SEH2_TRY {
                if (!Owned->InternalOpenReleased) {
                    if (!Owned->RefCount) {
                        Status = STATUS_FILE_CORRUPT_ERROR;
                        Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
                    } else {
                        Status = UDFDereferenceFileLocked(Vcb, Owned);
                        if (Status == STATUS_SUCCESS)
                            Owned->InternalOpenReleased = TRUE;
                    }
                }
            } _SEH2_FINALLY {
                UDFReleaseResource(&Vcb->DlocResource2);
            } _SEH2_END;
            if (Status == STATUS_SUCCESS)
                Status = UDFCompleteFileClose(IrpContext, Vcb, Owned);
        }
        /* An incomplete/failed constructor has no ordinary-open token to
         * consume. The collector owns its actual construction hold; never
         * label it as an already released successful mount open. */
    } else if (!Owned->Constructing && !Owned->ConstructionFailed &&
               !Owned->RefCount && !Owned->OpenCount && !Owned->Fcb &&
               Owned->Dloc && !Owned->Dloc->DeleteCommitted && Owned->Dloc->FileEntry) {
        /* The first close already consumed the reference. Retry only its
         * metadata work, without calling close for a second decrement. */
        Status = UDFFlushFile__(IrpContext, Vcb, Owned, UDF_FLUSH_FLAGS_LITE);
    }
    if (!NT_SUCCESS(Status)) {
        /* A refused decrement retains the unspent slot. A metadata error
         * retains the consumed receipt; retry must not call Close again. */
        UDFRememberMetadataWritebackError(IrpContext, Vcb, Owned, Status);
        return Status;
    }
    if (UDFCleanUpFileWithStatus(Vcb, Owned, &CleanupStatus) & UDF_FREE_FILEINFO) {
        MyFreePool__(Owned);
        *FileInfo = NULL;
    } else if (NT_SUCCESS(Status)) {
        Status = CleanupStatus == STATUS_SUCCESS ? STATUS_DEVICE_BUSY : CleanupStatus;
    }
    return Status;
}

NTSTATUS
UDFReapFailedFileInfos(PIRP_CONTEXT IrpContext, PVCB Vcb)
{
    uint32 Index = 0;
    ASSERT_EXCLUSIVE_VCB(Vcb);
    if (InterlockedCompareExchange(&Vcb->RetainedOpenCount, 0, 0))
        return STATUS_FILE_CORRUPT_ERROR;
    while (Index < Vcb->DlocCount) {
        PUDF_DATALOC_INFO Dloc;
        PUDF_FILE_INFO First, FileInfo = NULL, Link;
        PFCB CanonicalFcb = NULL;
        UDFAcquireResourceExclusive(&Vcb->DlocResource, TRUE);
        UDFAcquireResourceExclusive(&Vcb->DlocResource2, TRUE);
        Dloc = Vcb->DlocList[Index].Dloc;
        if (Dloc && (First = Dloc->LinkedFileInfo)) {
            Link = First;
            do {
                if ((Link->ConstructionFailed || Link->CleanupPending) &&
                    !Link->InternalOpenReleased) {
                    FileInfo = Link;
                    CanonicalFcb = Dloc->CommonFcb;
                    break;
                }
                Link = Link->NextLinkedFile;
            } while (Link && Link != First);
        }
        UDFReleaseResource(&Vcb->DlocResource2);
        UDFReleaseResource(&Vcb->DlocResource);
        if (!FileInfo) {
            Index++;
            continue;
        }
        /* A failed constructor's exact hold/ring entry remains its retry
         * owner. Exclusive VCB excludes other constructors/disposers here.
         * Drain a shared canonical generation before dropping an alias;
         * never flush a failed initializer's half-built FE/mapping. */
        if (FileInfo->Fcb || FileInfo->OpenCount ||
            FileInfo->RefCount > (FileInfo->ConstructionReferenceOwned ? 1U : 0U)) {
            /* A normal live/cache owner will retry at its final teardown.
             * It is not a failed constructor and must not block every
             * otherwise valid flush while it still has open handles. */
            if (FileInfo->ConstructionFailed)
                return STATUS_DEVICE_BUSY;
            FileInfo->CleanupPending = FALSE;
            continue;
        }
        if (CanonicalFcb && !CanonicalFcb->FileInfo)
            return STATUS_FILE_CORRUPT_ERROR;
        if (!Dloc->DeleteCommitted && CanonicalFcb && CanonicalFcb->FileInfo != FileInfo) {
            IO_STATUS_BLOCK IoStatus;
            PFCB ParentFcb = CanonicalFcb->FileInfo->ParentFile ?
                CanonicalFcb->FileInfo->ParentFile->Fcb : NULL;
            BOOLEAN ParentAcquired = FALSE;
            IoStatus.Status = STATUS_SUCCESS;
            IoStatus.Information = 0;
            UDFAcquireFcbExclusive(IrpContext, CanonicalFcb, FALSE);
            _SEH2_TRY {
                if (ParentFcb) {
                    UDFAcquireFcbExclusive(IrpContext, ParentFcb, FALSE);
                    ParentAcquired = TRUE;
                }
                UDFFlushAFile(IrpContext, CanonicalFcb, NULL, &IoStatus,
                               UDF_FLUSH_FLAGS_LITE);
            } _SEH2_FINALLY {
                if (ParentAcquired)
                    UDFReleaseFcb(IrpContext, ParentFcb);
                UDFReleaseFcb(IrpContext, CanonicalFcb);
            } _SEH2_END;
            if (!NT_SUCCESS(IoStatus.Status))
                return IoStatus.Status;
        }
        if (!FileInfo->ConstructionFailed && !FileInfo->TemporaryRetired &&
            !FileInfo->Dloc->DeleteCommitted && FileInfo->Dloc->FileEntry) {
            NTSTATUS Status;
            PFCB ParentFcb = FileInfo->ParentFile ? FileInfo->ParentFile->Fcb : NULL;
            if (ParentFcb)
                UDFAcquireFcbExclusive(IrpContext, ParentFcb, FALSE);
            _SEH2_TRY {
                Status = UDFFlushFile__(IrpContext, Vcb, FileInfo,
                                         UDF_FLUSH_FLAGS_LITE);
            } _SEH2_FINALLY {
                if (ParentFcb)
                    UDFReleaseFcb(IrpContext, ParentFcb);
            } _SEH2_END;
            if (!NT_SUCCESS(Status))
                return Status;
        }
        {
            NTSTATUS CleanupStatus;
            if (!(UDFCleanUpFileWithStatus(Vcb, FileInfo, &CleanupStatus) & UDF_FREE_FILEINFO))
                return CleanupStatus == STATUS_SUCCESS ? STATUS_DEVICE_BUSY : CleanupStatus;
        }
        MyFreePool__(FileInfo);
        /* Cleanup can remove this Dloc or change its ring head. Reload the
         * same cache slot; each successful iteration removes one failed FI. */
    }
    return STATUS_SUCCESS;
}

/* The VCB excludes metadata lookup/reclamation; DlocResource2 excludes
 * new FI references while weak cache edges are checked and rebound. No
 * reference arithmetic is needed: an affected child has RefCount == 0.
 * A replacement is another FI for the SAME Dloc, not a different parent
 * namespace object. It need not represent the retiring FI's own name. */
static BOOLEAN
UDFRebindWeakChildParent(PUDF_FILE_INFO Child, PUDF_FILE_INFO OldParent,
                         PUDF_FILE_INFO NewParent, BOOLEAN Apply)
{
    PUDF_FILE_INFO Peer;
    PDIR_INDEX_ITEM ParentEntry;
    if (!Child || !Child->Dloc)
        return FALSE;
    ParentEntry = UDFDirIndex(Child->Dloc->DirIndex, 1);
    Peer = Child;
    do {
        BOOLEAN Affected;
        if (!Peer || Peer->Dloc != Child->Dloc)
            return FALSE;
        Affected = Peer->ParentFile == OldParent ||
                   (ParentEntry && ParentEntry->FileInfo == OldParent);
        if (Affected) {
            if (!NewParent || NewParent == OldParent ||
                NewParent->Dloc != OldParent->Dloc ||
                Peer->RefCount || Peer->OpenCount || Peer->Constructing ||
                Child->Dloc->CleanupOwner || Child->Dloc->StreamDeleteContextOwner || Child->Dloc->StreamCreate ||
                (Child->Dloc->FE_Flags & UDF_FE_FLAG_UNDER_INIT))
                return FALSE;
            if (Apply && Peer->ParentFile == OldParent)
                Peer->ParentFile = NewParent;
        }
        Peer = Peer->NextLinkedFile;
    } while (Peer != Child);
    if (Apply && ParentEntry && ParentEntry->FileInfo == OldParent)
        ParentEntry->FileInfo = NewParent;
    return TRUE;
}

static BOOLEAN
UDFRebindWeakChildren(PVCB Vcb, PUDF_FILE_INFO FileInfo,
                      PUDF_FILE_INFO Parallel)
{
    PUDF_DATALOC_INFO Dloc = FileInfo->Dloc;
    PUDF_FILE_INFO Replacement = Parallel, Peer;
    PDIR_INDEX_ITEM Entry;
    uint_di Index;
    ULONG Pass;
    BOOLEAN Success = TRUE;
    ASSERT(ExIsResourceAcquiredExclusiveLite(&Vcb->VcbResource));
    UDFAcquireResourceExclusive(&Vcb->DlocResource2, TRUE);
    _SEH2_TRY {
        Peer = FileInfo->NextLinkedFile;
        while (!Replacement && Peer && Peer != FileInfo) {
            if (Peer->Dloc == Dloc && !Peer->Constructing &&
                !Peer->ConstructionFailed && !Peer->TemporaryRetired)
                Replacement = Peer;
            Peer = Peer->NextLinkedFile;
        }
        /* Check the entire set before changing a weak pointer. The second
         * pass is allocation/I/O-free under the same lifetime lock. */
        for (Pass = 0; Pass != 2 && Success; Pass++) {
            for (Index = 2; (Entry = UDFDirIndex(Dloc->DirIndex, Index)); Index++) {
                if (Entry->FileInfo && !UDFRebindWeakChildParent(
                        Entry->FileInfo, FileInfo, Replacement, (BOOLEAN)Pass)) {
                    Success = FALSE;
                    break;
                }
            }
            if (Success && Dloc->SDirInfo && !UDFRebindWeakChildParent(
                    Dloc->SDirInfo, FileInfo, Replacement, (BOOLEAN)Pass))
                Success = FALSE;
        }
    } _SEH2_FINALLY {
        UDFReleaseResource(&Vcb->DlocResource2);
    } _SEH2_END;
    return Success;
}

static uint32
UDFCleanupFailure(PVCB Vcb, PNTSTATUS CleanupStatus, NTSTATUS Status)
{
    if (Status == STATUS_SUCCESS) Status = STATUS_DEVICE_BUSY;
    if (Status != STATUS_DEVICE_BUSY) {
        if (NT_SUCCESS(Status)) Status = STATUS_IO_DEVICE_ERROR;
        Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
        InterlockedCompareExchange(&Vcb->MetadataWritebackError, Status, STATUS_SUCCESS);
    }
    *CleanupStatus = Status;
    return UDF_FREE_NOTHING;
}

static uint32
UDFCleanUpFileInternal(PVCB Vcb, PUDF_FILE_INFO FileInfo, PNTSTATUS CleanupStatus)
{
    PUDF_DATALOC_INFO Dloc;
    PUDF_FILE_INFO Peer, ParFileInfo = NULL, Parent;
    PDIR_INDEX_ITEM Entry, ParentEntry = NULL;
    PFILE_IDENT_DESC Ident;
    PDIR_INDEX_HDR Index;
    BOOLEAN KeepDloc = FALSE, Linked = FALSE, SharedIdent = FALSE, FoundHead = FALSE;
    NTSTATUS Status;
    uint64 References = 0;
    uint_di i;
#ifdef UDF_TRACK_ONDISK_ALLOCATION
    uint32 LinkCount;
#endif

    *CleanupStatus = STATUS_DEVICE_BUSY;
    if (!FileInfo) {
        *CleanupStatus = STATUS_SUCCESS;
        return UDF_FREE_FILEINFO;
    }
    ValidateFileInfo(FileInfo);
    Dloc = FileInfo->Dloc;
    if (FileInfo->CreateConstructionParent || FileInfo->RetainedOpenReferences ||
        FileInfo->RefCount || FileInfo->OpenCount || FileInfo->Fcb || FileInfo->Constructing)
        return UDF_FREE_NOTHING;
    if (Dloc) {
        if (!NT_SUCCESS(Dloc->AllocationReleaseError))
            return UDFCleanupFailure(Vcb, CleanupStatus, Dloc->AllocationReleaseError);
        if (!NT_SUCCESS(Dloc->ZeroDataError))
            return UDFCleanupFailure(Vcb, CleanupStatus, Dloc->ZeroDataError);
        if (!NT_SUCCESS(Dloc->CacheSizeError))
            return UDFCleanupFailure(Vcb, CleanupStatus, Dloc->CacheSizeError);
        if (!NT_SUCCESS(Dloc->NamespaceTargetError))
            return UDFCleanupFailure(Vcb, CleanupStatus, Dloc->NamespaceTargetError);
        if (Dloc->AllocationReleaseActive || Dloc->CreateRollbackOwner ||
            Dloc->CreateConstructionOwner || Dloc->NamespaceScratch || Dloc->NamespaceTarget ||
            Dloc->NamespaceFileInfo || Dloc->NamespaceScratchOwner ||
            Dloc->NamespacePhase != UdfNamespaceNone ||
            Dloc->NamespaceTargetState != UdfNamespaceTargetNone ||
            Dloc->StreamDeleteOwner ||
            Dloc->StreamDeleteContextOwner || Dloc->StreamCreate || Dloc->RetiredExtents ||
            (Dloc->CommonFcb && Dloc->CommonFcb->FileInfo == FileInfo))
            return UDF_FREE_NOTHING;

        /* Check the actual ring before a peer search or mutation. Reciprocal
         * links exclude a cycle which omits this FI; reference totals have
         * no hidden contribution from an FCB or an invented cleanup open. */
        UDFAcquireResourceExclusive(&Vcb->DlocResource2, TRUE);
        _SEH2_TRY {
            Peer = FileInfo;
            do {
                if (!Peer || Peer->Dloc != Dloc || !Peer->NextLinkedFile || !Peer->PrevLinkedFile ||
                    Peer->NextLinkedFile->PrevLinkedFile != Peer ||
                    Peer->PrevLinkedFile->NextLinkedFile != Peer) {
                    *CleanupStatus = STATUS_FILE_CORRUPT_ERROR;
                    _SEH2_LEAVE;
                }
                if (Peer->RefCount > Dloc->LinkRefCount ||
                    References > Dloc->LinkRefCount - Peer->RefCount) {
                    *CleanupStatus = STATUS_FILE_CORRUPT_ERROR;
                    _SEH2_LEAVE;
                }
                References += Peer->RefCount;
                if (Peer == Dloc->LinkedFileInfo) FoundHead = TRUE;
                if (Peer != FileInfo && FileInfo->FileIdent && Peer->FileIdent == FileInfo->FileIdent)
                    SharedIdent = TRUE;
                Peer = Peer->NextLinkedFile;
            } while (Peer != FileInfo);
            if (!FoundHead || References != Dloc->LinkRefCount)
                *CleanupStatus = STATUS_FILE_CORRUPT_ERROR;
        } _SEH2_FINALLY {
            UDFReleaseResource(&Vcb->DlocResource2);
        } _SEH2_END;
        if (*CleanupStatus != STATUS_DEVICE_BUSY)
            return UDFCleanupFailure(Vcb, CleanupStatus, *CleanupStatus);
        ParFileInfo = UDFLocateAnyParallelFI(FileInfo);
        Linked = FileInfo->NextLinkedFile != FileInfo;
        KeepDloc = Dloc->LinkRefCount || Dloc->CommonFcb || Linked;
        if (KeepDloc && !Linked)
            return UDFCleanupFailure(Vcb, CleanupStatus, STATUS_FILE_CORRUPT_ERROR);
        if (!KeepDloc && (Dloc->FE_Flags & UDF_FE_FLAG_FREE_DEFERRED) && !Dloc->DeleteCommitted)
            return UDFCleanupFailure(Vcb, CleanupStatus, STATUS_FILE_CORRUPT_ERROR);
        if (FileInfo->TemporaryRetired &&
            (FileInfo->ParentFile || FileInfo->FileIdent || Linked || Dloc->CommonFcb ||
             Dloc->LinkRefCount || Dloc->DataLoc.Length || !Dloc->FELoc.Mapping ||
             Dloc->FELoc.Mapping[0].extLocation))
            return UDF_FREE_NOTHING;
        if (UDFDirectoryAppendPending(FileInfo) ||
            (Dloc->FE_Flags & UDF_FE_FLAG_CACHED_DATA_PENDING))
            return UDF_FREE_NOTHING;
        if (!FileInfo->ConstructionFailed && !FileInfo->TemporaryRetired &&
            !(Dloc->FE_Flags & UDF_FE_FLAG_FREE_DEFERRED)) {
            Entry = FileInfo->ParentFile && FileInfo->ParentFile->Dloc ?
                UDFDirIndex(FileInfo->ParentFile->Dloc->DirIndex, FileInfo->Index) : NULL;
            if ((Dloc->FE_Flags & UDF_FE_FLAG_FE_MODIFIED) || Dloc->DataLoc.Modified ||
                Dloc->AllocLoc.Modified || Dloc->FELoc.Modified ||
                (Dloc->DataLoc.Flags & EXTENT_FLAG_PREALLOCATED) ||
                (Entry && (Entry->FI_Flags & UDF_FI_FLAG_FI_MODIFIED)))
                return UDF_FREE_NOTHING;
        }
        if (KeepDloc && !UDFRebindWeakChildren(Vcb, FileInfo, ParFileInfo))
            return UDF_FREE_NOTHING;
        if (!KeepDloc && Dloc->DirIndex) {
            for (i = 2; (Entry = UDFDirIndex(Dloc->DirIndex, i)); i++) {
                PUDF_FILE_INFO Child = Entry->FileInfo;
                NTSTATUS ChildStatus;
                uint32 ChildResult;
                if (!Child) continue;
                if (!Child->Dloc || Child->NextLinkedFile != Child || Child->ParentFile != FileInfo ||
                    Child->Fcb || Child->RefCount || Child->OpenCount || Child->Dloc->CommonFcb ||
                    Child->Dloc->LinkRefCount || (Entry->FI_Flags & UDF_FI_FLAG_FI_MODIFIED) ||
                    (Child->Dloc->FE_Flags & (UDF_FE_FLAG_FE_MODIFIED | UDF_FE_FLAG_CACHED_DATA_PENDING)) ||
                    Child->Dloc->DataLoc.Modified || Child->Dloc->AllocLoc.Modified ||
                    Child->Dloc->FELoc.Modified || (Child->Dloc->DataLoc.Flags & EXTENT_FLAG_PREALLOCATED))
                    return UDF_FREE_NOTHING;
                ChildResult = UDFCleanUpFileWithStatus(Vcb, Child, &ChildStatus);
                if (!(ChildResult & UDF_FREE_FILEINFO))
                    return UDFCleanupFailure(Vcb, CleanupStatus, ChildStatus);
                MyFreePool__(Child);
                Entry->FileInfo = NULL;
            }
        }
        if (Dloc->SDirInfo && !KeepDloc)
            return UDF_FREE_NOTHING;
        if (!KeepDloc) {
            /* Distinct ownership must be established before any free. */
            if ((Dloc->DataLoc.Mapping && Dloc->DataLoc.Mapping == Dloc->AllocLoc.Mapping) ||
                (Dloc->DataLoc.Mapping && Dloc->DataLoc.Mapping == Dloc->FELoc.Mapping) ||
                (Dloc->AllocLoc.Mapping && Dloc->AllocLoc.Mapping == Dloc->FELoc.Mapping) ||
                (Dloc->FileEntry && ((PVOID)Dloc->FileEntry == (PVOID)Dloc->DataLoc.Mapping ||
                 (PVOID)Dloc->FileEntry == (PVOID)Dloc->AllocLoc.Mapping ||
                 (PVOID)Dloc->FileEntry == (PVOID)Dloc->FELoc.Mapping)))
                return UDFCleanupFailure(Vcb, CleanupStatus, STATUS_FILE_CORRUPT_ERROR);
            Status = UDFCheckDlocCleanup(Vcb, FileInfo, FALSE);
            if (Status != STATUS_SUCCESS) return UDFCleanupFailure(Vcb, CleanupStatus, Status);
            if (Dloc->FE_Flags & UDF_FE_FLAG_FREE_DEFERRED) {
                /* Still indexed and claimed during bitmap I/O. Do not
                 * forget the mappings, prefree receipt or dirty flags on
                 * a failed or uncertain allocation release. */
                Status = Dloc->DeletePrefree ? UDFReleasePrefreedAllocation(Vcb, NULL, FileInfo) :
                                               UDFFreeFileAllocation(Vcb, NULL, FileInfo);
                if (Status != STATUS_SUCCESS) return UDFCleanupFailure(Vcb, CleanupStatus, Status);
                Dloc->FE_Flags &= ~(UDF_FE_FLAG_FREE_DEFERRED | UDF_FE_FLAG_FE_MODIFIED);
            }
#ifdef UDF_DBG
            if (!FileInfo->ConstructionFailed && !FileInfo->TemporaryRetired) {
                ASSERT(!(Dloc->FE_Flags & UDF_FE_FLAG_FE_MODIFIED));
                ASSERT(!Dloc->DataLoc.Modified);
                ASSERT(!(Dloc->DataLoc.Flags & EXTENT_FLAG_PREALLOCATED));
                ASSERT(!Dloc->AllocLoc.Modified);
                ASSERT(!Dloc->FELoc.Modified);
            }
#endif
#ifdef UDF_TRACK_ONDISK_ALLOCATION
            /* Diagnostic bitmap reads happen while this generation still
             * has its indexed owner. Never reload a directory during GC. */
            if (!FileInfo->ConstructionFailed && Dloc->DataLoc.Mapping) {
                LinkCount = Dloc->FileEntry ? UDFGetFileLinkCount(FileInfo) : UDF_INVALID_LINK_COUNT;
                UDFCheckSpaceAllocation(Vcb, 0, Dloc->DataLoc.Mapping,
                    LinkCount && LinkCount != UDF_INVALID_LINK_COUNT ? AS_USED : AS_FREE);
            }
#endif
        }
    }

    /* Prepare every weak-edge decision before last-Dloc detach. A failed
     * initializer can have an unbound parent context with no directory row.
     * A reused/foreign row is never overwritten merely to destroy this FI. */
    Parent = FileInfo->ParentFile;
    if (Parent) {
        if (Parent == FileInfo || !Parent->Dloc)
            return UDFCleanupFailure(Vcb, CleanupStatus, STATUS_FILE_CORRUPT_ERROR);
        ParentEntry = UDFDirIndex(Parent->Dloc->DirIndex, FileInfo->Index);
        if (!ParentEntry && !FileInfo->ConstructionFailed && !UDFIsAStreamDir(FileInfo))
            return UDFCleanupFailure(Vcb, CleanupStatus, STATUS_FILE_CORRUPT_ERROR);
    }
    if (Dloc && !KeepDloc) {
        Status = UDFCheckDlocCleanup(Vcb, FileInfo, TRUE);
        if (Status != STATUS_SUCCESS) return UDFCleanupFailure(Vcb, CleanupStatus, Status);
    }

    /* From here only checked weak-edge stores and valid pool destruction:
     * no allocator, metadata/cache/device I/O or fallible status helper. */
    if (Dloc && KeepDloc && Linked) {
        Entry = UDFDirIndex(Dloc->DirIndex, 0);
        if (Entry && Entry->FileInfo == FileInfo) Entry->FileInfo = FileInfo->PrevLinkedFile;
        if (Dloc->LinkedFileInfo == FileInfo) Dloc->LinkedFileInfo = FileInfo->PrevLinkedFile;
        FileInfo->NextLinkedFile->PrevLinkedFile = FileInfo->PrevLinkedFile;
        FileInfo->PrevLinkedFile->NextLinkedFile = FileInfo->NextLinkedFile;
    }
    if (Parent) {
        if (Parent->Dloc->SDirInfo == FileInfo) {
            Parent->Dloc->SDirInfo = KeepDloc ? ParFileInfo : NULL;
            if (!KeepDloc) Parent->Dloc->FE_Flags &= ~UDF_FE_FLAG_HAS_DEL_SDIR;
        }
        if (ParentEntry && ParentEntry->FileInfo == FileInfo)
            ParentEntry->FileInfo = KeepDloc ? ParFileInfo : NULL;
    }
    if (FileInfo->ListPtr && FileInfo->ListPtr->FileInfo == FileInfo)
        FileInfo->ListPtr->FileInfo = NULL;
    Ident = FileInfo->FileIdent;
    FileInfo->FileIdent = NULL;
    FileInfo->ParentFile = NULL;
    FileInfo->ListPtr = NULL;
    FileInfo->NextLinkedFile = FileInfo->PrevLinkedFile = FileInfo;
    FileInfo->Dloc = NULL;
    if (!SharedIdent && Ident) MyFreePool__(Ident);
    if (Dloc && !KeepDloc) {
        if (Dloc->DataLoc.Mapping) MyFreePool__(Dloc->DataLoc.Mapping);
        if (Dloc->AllocLoc.Mapping) MyFreePool__(Dloc->AllocLoc.Mapping);
        if (Dloc->FELoc.Mapping) MyFreePool__(Dloc->FELoc.Mapping);
        if (Dloc->FileEntry) MyFreePool__(Dloc->FileEntry);
        Index = Dloc->DirIndex;
        if (Index) {
            for (i = 2; (Entry = UDFDirIndex(Index, i)); i++)
                if (Entry->FName.Buffer) MyFreePool__(Entry->FName.Buffer);
            UDFDirIndexFree(Index);
        }
        MyFreePool__(Dloc);
    }
    *CleanupStatus = STATUS_SUCCESS;
    return KeepDloc ? UDF_FREE_FILEINFO : (UDF_FREE_FILEINFO | UDF_FREE_DLOC);
} // end UDFCleanUpFileInternal()

/*
    This routine creates FileIdent record in destination directory &
    allocates FileEntry with in-ICB zero-sized data
    If it returns status != STATUS_SUCCESS caller should call UDFCleanUpFile__
    for returned pointer *WITHOUT* using UDFCloseFile__
 */
/* An appended name can reserve a deleted, reference-free FID before its
 * initial FE is written. This prevents later growth from exposing stale
 * bytes in the new slot. UNDER_INIT and then the existing FI/FCB owner
 * retain the FE; scalar/batched publication commits it before the valid FID.
 * Keep the immediate initial write when the reservation cannot be owned.
 */
static BOOLEAN
UDFMayDeferInitialFE(PIRP_CONTEXT IrpContext, PVCB Vcb,
    PUDF_FILE_INFO FileInfo, uint32 PartNum)
{
    PUDF_DATALOC_INFO dloc, parentDloc;
    PUDF_FILE_INFO parent;
    PFCB parentFcb;
    PDIR_INDEX_HDR header;
    PDIR_INDEX_ITEM index;
    uint32 frame, rel;

    if (!UDFMayDeferNewCreate(IrpContext, Vcb) ||
        FlagOn(IrpContext->Flags, IRP_CONTEXT_FLAG_WRITE_THROUGH) ||
        FlagOn(Vcb->VcbState, VCB_STATE_REMOVABLE_MEDIA) ||
        !FileInfo || !(dloc = FileInfo->Dloc) || FileInfo->Fcb ||
        !FileInfo->Constructing || FileInfo->ConstructionFailed ||
        FileInfo->RefCount || FileInfo->OpenCount || dloc->LinkRefCount ||
        dloc->CommonFcb ||
        !FlagOn(dloc->FE_Flags, UDF_FE_FLAG_UNDER_INIT) ||
        !FlagOn(dloc->FE_Flags, UDF_FE_FLAG_FE_MODIFIED) ||
        !dloc->FileEntry || !UDFCanStageFileEntry(Vcb, FileInfo) ||
        dloc->DataLoc.Length || dloc->DataLoc.Offset != dloc->FileEntryLen ||
        dloc->FileEntryLen > Vcb->SectorSize ||
        ((icbtag*)(dloc->FileEntry + 1))->fileType != UDF_FILE_TYPE_REGULAR ||
        (((icbtag*)(dloc->FileEntry + 1))->flags & ICB_FLAG_ALLOC_MASK) != ICB_FLAG_AD_IN_ICB ||
        FileInfo->NextLinkedFile != FileInfo || FileInfo->PrevLinkedFile != FileInfo ||
        !(parent = FileInfo->ParentFile) || !(parentDloc = parent->Dloc) ||
        !(parentFcb = parent->Fcb) || parentDloc->CommonFcb != parentFcb ||
        parentFcb->FileInfo != parent || parentFcb->FcbReference <= 0 ||
        !FlagOn(parentFcb->FcbState, FCB_STATE_IN_FCB_TABLE) ||
        !FlagOn(parentFcb->FcbState, UDF_FCB_DIRECTORY | UDF_FCB_ROOT_DIRECTORY) ||
        FlagOn(parentFcb->FcbState, UDF_FCB_DELETED | UDF_FCB_DELETE_ON_CLOSE) ||
        UDFIsAStreamDir(parent) || !(header = parentDloc->DirIndex) ||
        parentDloc->DataLoc.Length < 0 ||
        !parentDloc->DirectoryCommitKnown || parentDloc->DirectoryCommittedLength < 0 ||
        parentDloc->DirectoryCommittedLength > parentDloc->DataLoc.Length ||
        FileInfo->Index < 2 || !FileInfo->FileIdent ||
        FileInfo->FileIdentLen < sizeof(FILE_IDENT_DESC) ||
        FileInfo->FileIdentLen > Vcb->SectorSize || PartNum >= Vcb->PartitionMaps)
        return FALSE;
    if (parentDloc->DataLoc.Offset &&
        (!UDFCanStageFileEntry(Vcb, parent) ||
         parentDloc->DataLoc.Offset != parentDloc->FileEntryLen ||
         !parentDloc->FileEntry ||
         (((icbtag*)(parentDloc->FileEntry + 1))->flags & ICB_FLAG_ALLOC_MASK) != ICB_FLAG_AD_IN_ICB ||
         (uint64)parentDloc->FileEntryLen + (uint64)parentDloc->DataLoc.Length > Vcb->SectorSize))
        return FALSE;
    frame = FileInfo->Index >> UDF_DIR_INDEX_FRAME_SH;
    rel = FileInfo->Index & (UDF_DIR_INDEX_FRAME - 1);
    if (frame >= header->FrameCount ||
        (frame + 1 == header->FrameCount && rel >= header->LastFrameCount))
        return FALSE;
    index = UDFDirIndex(header, FileInfo->Index);
    return index && index->FileInfo == FileInfo &&
        FlagOn(index->FI_Flags, UDF_FI_FLAG_FI_MODIFIED) &&
        index->Length == FileInfo->FileIdentLen &&
        (uint64)index->Offset == (uint64)parentDloc->DataLoc.Length &&
        (uint64)index->Offset >= (uint64)parentDloc->DirectoryCommittedLength &&
        !(index->FileCharacteristics & (FILE_DELETED | FILE_PARENT | FILE_DIRECTORY | FILE_METADATA)) &&
        FileInfo->FileIdent->descTag.tagIdent == TID_FILE_IDENT_DESC &&
        FileInfo->FileIdent->fileCharacteristics == index->FileCharacteristics &&
        FileInfo->FileIdent->icb.extLocation.partitionReferenceNum == PartNum &&
        FileInfo->FileIdent->icb.extLocation.logicalBlockNum ==
            UDFPhysLbaToPart(Vcb, PartNum, dloc->FELoc.Mapping[0].extLocation);
}

static NTSTATUS
UDFReserveInitialFid(PIRP_CONTEXT IrpContext, PVCB Vcb,
    PUDF_FILE_INFO FileInfo, uint32 PartNum, PBOOLEAN Deferred)
{
    PUDF_FILE_INFO parent = FileInfo->ParentFile;
    PDIR_INDEX_ITEM index;
    PFILE_IDENT_DESC tombstone;
    SIZE_T written = 0;
    uint32 lba, parentPart;
    NTSTATUS status = STATUS_SUCCESS;

    *Deferred = FALSE;
    if (!UDFMayDeferInitialFE(IrpContext, Vcb, FileInfo, PartNum))
        return STATUS_SUCCESS;
    index = UDFDirIndex(parent->Dloc->DirIndex, FileInfo->Index);
    lba = UDFExtentOffsetToLba(Vcb, parent->Dloc->DataLoc.Mapping,
                              index->Offset, NULL, NULL, NULL, NULL);
    parentPart = UDFGetRefPartNumByPhysLba(Vcb,
        parent->Dloc->FELoc.Mapping[0].extLocation);
    if (parentPart >= Vcb->PartitionMaps)
        return STATUS_FILE_CORRUPT_ERROR;
    tombstone = MyAllocatePool__(NonPagedPool, FileInfo->FileIdentLen);
    if (!tombstone)
        return STATUS_SUCCESS; // No new state accepted; immediate FE fallback.
    _SEH2_TRY {
        RtlCopyMemory(tombstone, FileInfo->FileIdent, FileInfo->FileIdentLen);
        tombstone->fileCharacteristics = FILE_DELETED;
        RtlZeroMemory(&tombstone->icb, sizeof(tombstone->icb));
        UDFSetUpTag(Vcb, &tombstone->descTag, (uint16)FileInfo->FileIdentLen,
                    lba == LBA_OUT_OF_EXTENT ? 0 :
                        UDFPhysLbaToPart(Vcb, parentPart, lba), 0);
        status = UDFReserveDirectoryFid(IrpContext, Vcb, parent, index->Offset,
            FileInfo->FileIdentLen, (int8*)tombstone, &written, Deferred);
        if (NT_SUCCESS(status) && *Deferred && written != FileInfo->FileIdentLen) {
            *Deferred = FALSE;
            status = STATUS_IO_DEVICE_ERROR;
        }
    } _SEH2_FINALLY {
        MyFreePool__(tombstone);
    } _SEH2_END;
    return status;
}

/* The private FI is already indexed by its new Dloc. Pin the exact parent
 * before any weak namespace edge or initial FE/FID publication can escape.
 * This holds no successful-open reference to the constructor itself. */
static NTSTATUS
UDFOwnCreateConstruction(PVCB Vcb, PUDF_FILE_INFO FileInfo,
                         PUDF_FILE_INFO Parent)
{
    _SEH2_VOLATILE NTSTATUS Status;
    LONG Count;
    PUDF_DATALOC_INFO Dloc = FileInfo->Dloc;
    if (!Dloc || !FileInfo->Constructing || FileInfo->CreateConstructionParent)
        return STATUS_FILE_INVALID;
    Status = STATUS_SUCCESS;
    UDFAcquireResourceExclusive(&Vcb->DlocResource2, TRUE);
    _SEH2_TRY {
        if (Dloc->CreateConstructionOwner || Dloc->CleanupOwner ||
            Dloc->LinkedFileInfo != FileInfo || FileInfo->ConstructionFailed) {
            Status = STATUS_FILE_CORRUPT_ERROR;
            Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
            InterlockedCompareExchange(&Vcb->MetadataWritebackError,
                                        Status, STATUS_SUCCESS);
        } else {
            Dloc->CreateConstructionOwner = FileInfo;
            for (;;) {
                Count = InterlockedCompareExchange(&Vcb->PendingNamespaceCount, 0, 0);
                if (Count < 0 || Count >= MAXLONG - 1) {
                    Status = STATUS_FILE_CORRUPT_ERROR;
                    Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
                    break;
                }
                if (InterlockedCompareExchange(&Vcb->PendingNamespaceCount,
                                               Count + 1, Count) == Count) {
                    FileInfo->CreateConstructionCounted = TRUE;
                    break;
                }
            }
            if (Status == STATUS_SUCCESS) {
                /* DlocResource2 is recursive for this thread. The close
                 * barrier exists before reference admission can raise. */
                Status = UDFReferenceFileChecked(Vcb, Parent);
                if (Status == STATUS_SUCCESS)
                    FileInfo->CreateConstructionParent = Parent;
            }
        }
    } _SEH2_FINALLY {
        UDFReleaseResource(&Vcb->DlocResource2);
    } _SEH2_END;
    return Status;
}

/* No I/O/callback or second ordinary close belongs to this release. A
 * refused checked decrement leaves the exact parent token with the owner. */
static NTSTATUS
UDFReleaseCreateConstruction(PVCB Vcb, PUDF_FILE_INFO FileInfo)
{
    _SEH2_VOLATILE NTSTATUS Status = STATUS_FILE_CORRUPT_ERROR;
    PUDF_DATALOC_INFO Dloc = FileInfo->Dloc;
    UDFAcquireResourceExclusive(&Vcb->DlocResource2, TRUE);
    _SEH2_TRY {
        if (!Dloc || Dloc->CreateConstructionOwner != FileInfo ||
            !FileInfo->CreateConstructionCounted ||
            !NT_SUCCESS(FileInfo->CreateConstructionError) ||
            Vcb->PendingNamespaceCount <= 0 || Vcb->PendingNamespaceCount == MAXLONG)
            _SEH2_LEAVE;
        /* A returned parent-admission refusal acquired no parent token
         * and bound no index edge. Only that private early failure may
         * remove an owner with no parent; abnormal paths stay quarantined. */
        if (!FileInfo->CreateConstructionParent &&
            (!FileInfo->Constructing || FileInfo->ParentFile ||
             FileInfo->CreateConstructionOpenOwned))
            _SEH2_LEAVE;
        Status = FileInfo->CreateConstructionParent ?
            UDFDereferenceFileLocked(Vcb, FileInfo->CreateConstructionParent) : STATUS_SUCCESS;
        if (Status != STATUS_SUCCESS) _SEH2_LEAVE;
        FileInfo->CreateConstructionParent = NULL;
        FileInfo->CreateConstructionOpenOwned = FALSE; /* original open moves to caller */
        Dloc->CreateConstructionOwner = NULL;
        FileInfo->CreateConstructionCounted = FALSE;
        InterlockedDecrement(&Vcb->PendingNamespaceCount);
    } _SEH2_FINALLY {
        UDFReleaseResource(&Vcb->DlocResource2);
    } _SEH2_END;
    return Status;
}

/* Failure of initial publication is not proof that a tombstone reached
 * disk. Retain FI, allocation and parent; a new mount/recovery policy, not
 * the collector or another ordinary close, must decide their disposition. */
static VOID
UDFQuarantineCreateConstruction(PVCB Vcb, PUDF_FILE_INFO FileInfo, NTSTATUS Error)
{
    LONG Count;
    if (NT_SUCCESS(Error)) Error = STATUS_IO_DEVICE_ERROR;
    UDFAcquireResourceExclusive(&Vcb->DlocResource2, TRUE);
    _SEH2_TRY {
        if (!FileInfo->CreateConstructionCounted) {
            for (;;) {
                Count = InterlockedCompareExchange(&Vcb->PendingNamespaceCount, 0, 0);
                if (Count < 0 || Count >= MAXLONG - 1) {
                    /* Existing invalid/saturated inventory is itself a
                     * close barrier; never wrap it into an apparent zero. */
                    Error = STATUS_FILE_CORRUPT_ERROR;
                    break;
                }
                if (InterlockedCompareExchange(&Vcb->PendingNamespaceCount,
                                               Count + 1, Count) == Count) {
                    FileInfo->CreateConstructionCounted = TRUE;
                    break;
                }
            }
        }
        if (NT_SUCCESS(FileInfo->CreateConstructionError))
            FileInfo->CreateConstructionError = Error;
        FileInfo->ConstructionFailed = TRUE;
        FileInfo->CleanupPending = TRUE;
        Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
        InterlockedCompareExchange(&Vcb->MetadataWritebackError, Error, STATUS_SUCCESS);
    } _SEH2_FINALLY {
        UDFReleaseResource(&Vcb->DlocResource2);
    } _SEH2_END;
}

/* An indexed failed constructor is owned as a constructor, not as a new
 * successful ordinary open. In the early FE path ParentFile is still NULL:
 * no namespace edge or allocator operation requires a parent FI token.
 * A later defensive caller must retain an existing real parent contribution
 * or acquire an explicitly checked extra parent hold. Never copy counters. */
static VOID
UDFRetainFailedCreateConstruction(PVCB Vcb, PUDF_FILE_INFO FileInfo,
                                 PUDF_FILE_INFO Parent, NTSTATUS Error)
{
    PUDF_DATALOC_INFO Dloc = FileInfo->Dloc;
    NTSTATUS ParentStatus;
    if (!Dloc) {
        Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
        InterlockedCompareExchange(&Vcb->MetadataWritebackError,
            NT_SUCCESS(Error) ? STATUS_FILE_CORRUPT_ERROR : Error, STATUS_SUCCESS);
        return;
    }
    UDFAcquireResourceExclusive(&Vcb->DlocResource2, TRUE);
    _SEH2_TRY {
        if (!Dloc->CreateConstructionOwner)
            Dloc->CreateConstructionOwner = FileInfo;
        else if (Dloc->CreateConstructionOwner != FileInfo)
            Error = STATUS_FILE_CORRUPT_ERROR; /* do not replace another owner */
        /* Publish the lifetime/close barrier before a checked parent
         * admission or its resource acquisition can raise. */
        UDFQuarantineCreateConstruction(Vcb, FileInfo, Error);
        if (FileInfo->ParentFile && !FileInfo->RefCount &&
            !FileInfo->ConstructionReferenceOwned && !FileInfo->CreateConstructionParent) {
            if (FileInfo->ParentFile != Parent) {
                InterlockedCompareExchange(&Vcb->MetadataWritebackError,
                    STATUS_FILE_CORRUPT_ERROR, STATUS_SUCCESS);
                _SEH2_LEAVE;
            }
            _SEH2_TRY {
                ParentStatus = UDFReferenceFileChecked(Vcb, Parent);
                if (ParentStatus == STATUS_SUCCESS)
                    FileInfo->CreateConstructionParent = Parent;
            } _SEH2_EXCEPT(FsRtlIsNtstatusExpected(_SEH2_GetExceptionCode()) ?
                          EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
                ParentStatus = _SEH2_GetExceptionCode();
            } _SEH2_END;
            if (ParentStatus != STATUS_SUCCESS)
                InterlockedCompareExchange(&Vcb->MetadataWritebackError,
                                            ParentStatus, STATUS_SUCCESS);
        }
        /* A completed private stage can arrive here when scratch admission
         * was refused. Retain only its already acquired, exact open token. */
        if (!FileInfo->Constructing && !FileInfo->ConstructionReferenceOwned &&
            FileInfo->RefCount == 1 && Dloc->LinkRefCount == 1 && !FileInfo->Fcb)
            FileInfo->CreateConstructionOpenOwned = TRUE;
    } _SEH2_FINALLY {
        UDFReleaseResource(&Vcb->DlocResource2);
    } _SEH2_END;
}

static NTSTATUS
UDFCreateFileInternal(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
//    IN uint16 AllocMode, // short/long/ext/in-icb  // always in-ICB
    IN BOOLEAN IgnoreCase,
    IN PUNICODE_STRING _fn,
    IN uint32 ExtAttrSz,
    IN uint32 ImpUseLen,
    IN BOOLEAN Extended,
    IN BOOLEAN CreateNew,
 IN OUT PUDF_FILE_INFO DirInfo,
    OUT PUDF_FILE_INFO* _FileInfo,
    IN uint_di ExcludedIndex,
    IN BOOLEAN StageOnly,
    OUT PBOOLEAN FreshFileCreated OPTIONAL
    )
{
    uint32 l, d, AppendOffset = 0, PartNum;
    uint64 EntryCount, Placement;
    uint_di i, j;
    _SEH2_VOLATILE NTSTATUS status = STATUS_UNSUCCESSFUL;
    LONG_AD FEicb;
    UDF_DIR_SCAN_CONTEXT ScanContext;
    PDIR_INDEX_HDR hDirNdx;
    PDIR_INDEX_ITEM DirNdx;
    uint32 LBS;
    _SEH2_VOLATILE PUDF_FILE_INFO FileInfo = NULL;
    UNICODE_STRING PreparedName = {0};
    _SEH2_VOLATILE PWCHAR PreparedNameBuffer = NULL;
    BOOLEAN undel = FALSE;
//    BOOLEAN PackDir = FALSE;
    _SEH2_VOLATILE BOOLEAN FEAllocated = FALSE;
    _SEH2_VOLATILE BOOLEAN InitialPublicationStarted = FALSE;
    BOOLEAN NewNameAppend = FALSE;
    BOOLEAN InitialFEDeferred = FALSE;
    ULONGLONG NewUniqueId = 0;

    if (FreshFileCreated) *FreshFileCreated = FALSE;
    if (!_FileInfo) return STATUS_INVALID_PARAMETER;
    *_FileInfo = NULL;
    if (!IrpContext || !Vcb || !Vcb->Partitions || !DirInfo || !DirInfo->Dloc ||
        !DirInfo->Dloc->FELoc.Mapping || !_fn || !_fn->Buffer || !_fn->Length ||
        _fn->Length % sizeof(WCHAR) || _fn->Length > MAXUSHORT - sizeof(WCHAR) ||
        ImpUseLen > MAXUSHORT - 3 || Vcb->SectorSize < sizeof(FILE_IDENT_DESC) ||
        (Vcb->SectorSize & (Vcb->SectorSize - 1)))
        return STATUS_INVALID_PARAMETER;
    hDirNdx = DirInfo->Dloc->DirIndex;
    LBS = Vcb->SectorSize;
    ValidateFileInfo(DirInfo);
    PartNum = UDFGetRefPartNumByPhysLba(Vcb, DirInfo->Dloc->FELoc.Mapping[0].extLocation);
    if (PartNum >= Vcb->PartitionMaps || PartNum > MAXUSHORT)
        return STATUS_FILE_CORRUPT_ERROR;
    if (!hDirNdx) return STATUS_NOT_A_DIRECTORY;
    if (!hDirNdx->FrameCount || !hDirNdx->LastFrameCount ||
        hDirNdx->LastFrameCount > UDF_DIR_INDEX_FRAME)
        return STATUS_FILE_CORRUPT_ERROR;
    EntryCount = ((uint64)hDirNdx->FrameCount - 1) * UDF_DIR_INDEX_FRAME +
                  hDirNdx->LastFrameCount;
    if (EntryCount < 2 || EntryCount > MAXULONG)
        return STATUS_FILE_CORRUPT_ERROR;
    i = 0;

    _SEH2_TRY {

        // check if exists
        status = UDFFindFileExceptIndex(Vcb, IgnoreCase, FALSE, _fn, DirInfo, &i, ExcludedIndex);
        DirNdx = UDFDirIndex(hDirNdx,i);
        if (NT_SUCCESS(status)) {
            // file is a Cur(Parent)Dir
            if (i<2) try_return (status = STATUS_ACCESS_DENIED);
            // file deleted
            if (UDFIsDeleted(DirNdx)) {
                /* A stage never reuses an existing live or cached link,
                 * including an old deleted spelling of this name. */
                if (StageOnly)
                    goto CreateUndel;
                j=0;
                if (NT_SUCCESS(UDFFindFileExceptIndex(Vcb, IgnoreCase, TRUE, _fn, DirInfo, &j, ExcludedIndex))) {
                   i=j;
                   DirNdx = UDFDirIndex(hDirNdx,i);
                   goto CreateBothFound;
                }
                // we needn't allocating new FileIdent inside Dir stream
                // perform 'undel'
                if (DirNdx->FileInfo) {
    //                BrutePoint();
                    status = UDFPretendFileDeleted__(Vcb, DirNdx->FileInfo);
                    if (!NT_SUCCESS(status))
                        try_return (status = STATUS_FILE_DELETED);
                } else {
                    undel = TRUE;
                }
    //            BrutePoint();
                goto CreateUndel;
            }
CreateBothFound:
            // file already exists
            if (CreateNew) {
                try_return (status = STATUS_ACCESS_DENIED);
            }
            // try to open it
            BrutePoint();
            status = UDFOpenFile__(IrpContext, Vcb, IgnoreCase, TRUE, _fn, DirInfo, _FileInfo, &i);
    //        *_FileInfo = FileInfo; // OpenFile__ has already done it, so update it...
            DirNdx = UDFDirIndex(hDirNdx,i);
            FileInfo = *_FileInfo;
            if (!NT_SUCCESS(status)) {
                // :(( can't open....
                if (FileInfo && (UDFCleanUpFile__(Vcb, FileInfo) & UDF_FREE_FILEINFO)) {
                    MyFreePool__(FileInfo);
                    *_FileInfo = NULL;
                }
                BrutePoint();
                try_return (status);
            }
            /* Finding a live file is an open, not permission to truncate.
             * The caller applies its real disposition after access/share
             * admission and receives FreshFileCreated=FALSE. */
            try_return(status);
        }

CreateUndel:
        /* Reserve the ID before allocating or publishing a new FE/FID.
         * A failed journal write must leave the namespace untouched. */
        if (UDFIsAStreamDir(DirInfo))
            NewUniqueId = (ULONGLONG)UDFGetFileUID(DirInfo);
        else {
            status = UDFAllocateFileUID(IrpContext, Vcb, &NewUniqueId);
            if (!NT_SUCCESS(status))
                try_return(status);
        }

        // allocate FileInfo
        FileInfo = (PUDF_FILE_INFO)MyAllocatePoolTag__(UDF_FILE_INFO_MT,sizeof(UDF_FILE_INFO), MEM_FE_TAG);
        *_FileInfo = FileInfo;
        if (!FileInfo)
            try_return (status = STATUS_INSUFFICIENT_RESOURCES);
        ImpUseLen = (ImpUseLen + 3) & ~((uint32)3);

        RtlZeroMemory(FileInfo, sizeof(UDF_FILE_INFO));
        FileInfo->NamespaceStaged = StageOnly;
        // init horizontal links
        FileInfo->NextLinkedFile =
        FileInfo->PrevLinkedFile = FileInfo;
        // allocate space for FileEntry
        if (!NT_SUCCESS(status =
            UDFBuildFileEntry(IrpContext, Vcb, DirInfo, FileInfo, PartNum, ICB_FLAG_AD_IN_ICB, ExtAttrSz, Extended) )) {
            BrutePoint();
            try_return (status);
        }
        FEAllocated = TRUE;
        status = UDFOwnCreateConstruction(Vcb, FileInfo, DirInfo);
        if (status != STATUS_SUCCESS) try_return(status);
        FEicb.extLength = LBS;
        ASSERT(FileInfo->Dloc->FELoc.Mapping[0].extLocation);
        FEicb.extLocation.logicalBlockNum = UDFPhysLbaToPart(Vcb, PartNum, FileInfo->Dloc->FELoc.Mapping[0].extLocation);
        FEicb.extLocation.partitionReferenceNum = (uint16)PartNum;
        RtlZeroMemory(&(FEicb.impUse), sizeof(FEicb.impUse));

        if (!undel) {
            // build FileIdent
            if (!NT_SUCCESS(status =
                UDFBuildFileIdent(Vcb, _fn, &FEicb, ImpUseLen,
                        &(FileInfo->FileIdent), &(FileInfo->FileIdentLen)) ))
                try_return (status);
        } else {
            // read FileIdent
            if (!DirNdx || DirNdx->Length < sizeof(FILE_IDENT_DESC) ||
                DirNdx->Length > MAXUSHORT || DirInfo->Dloc->DataLoc.Length < 0 ||
                (uint64)DirNdx->Offset + DirNdx->Length > (uint64)DirInfo->Dloc->DataLoc.Length)
                try_return(status = STATUS_FILE_CORRUPT_ERROR);
            FileInfo->FileIdent = (PFILE_IDENT_DESC)MyAllocatePoolTag__(NonPagedPool, DirNdx->Length, MEM_FID_TAG);
            if (!(FileInfo->FileIdent)) try_return (status = STATUS_INSUFFICIENT_RESOURCES);
            FileInfo->FileIdentLen = DirNdx->Length;
            if (!NT_SUCCESS(status = UDFReadFile__(IrpContext, Vcb, DirInfo, DirNdx->Offset,
                                     DirNdx->Length, FALSE, (int8*)(FileInfo->FileIdent))))
                try_return (status);
            FileInfo->FileIdent->fileCharacteristics = 0;
            FileInfo->FileIdent->icb = FEicb;
            ImpUseLen = FileInfo->FileIdent->lengthOfImpUse;
            DirNdx->FileCharacteristics = 0;
        }

        // try to find suitable unused FileIdent in DirIndex
        l = FileInfo->FileIdentLen;
        if (undel) goto CrF__2;
        /* Own the replacement name before discarding an old deleted name
         * or changing the directory index's capacity. */
        PreparedName.MaximumLength = _fn->Length + sizeof(WCHAR);
        PreparedName.Length = _fn->Length;
        PreparedNameBuffer = (PWCHAR)MyAllocatePoolTag__(UDF_FILENAME_MT,
                                      PreparedName.MaximumLength, MEM_FNAMECPY_TAG);
        if (!PreparedNameBuffer) try_return(status = STATUS_INSUFFICIENT_RESOURCES);
        PreparedName.Buffer = PreparedNameBuffer;
        RtlCopyMemory(PreparedName.Buffer, _fn->Buffer, _fn->Length);
        PreparedName.Buffer[_fn->Length / sizeof(WCHAR)] = 0;
        // search for suitable unused (deleted) entry to reuse
        if (!StageOnly && UDFDirIndexInitScan(DirInfo, &ScanContext, 2)) {
            while((DirNdx = UDFDirIndexScan(&ScanContext, NULL))) {
                if ((DirNdx->Length == l) && UDFIsDeleted(DirNdx) &&
                   !DirNdx->FileInfo ) {
                    i = ScanContext.i;
                    goto CrF__1;
                }
            }
        }
        // no suitable deleted entry found, append new one
        if (EntryCount == MAXULONG)
            try_return(status = STATUS_FILE_TOO_LARGE);
        i = (uint_di)EntryCount;
        DirNdx = UDFDirIndex(hDirNdx, i - 1);
        if (!DirNdx || (uint64)DirNdx->Offset + DirNdx->Length > MAXULONG)
            try_return(status = STATUS_FILE_CORRUPT_ERROR);
        AppendOffset = DirNdx->Offset + DirNdx->Length;
        /* Placement includes the inline data offset only for sector-tag
         * alignment. Keep the logical directory offset itself separate. */
        Placement = (uint64)AppendOffset + l + DirInfo->Dloc->DataLoc.Offset;
        d = (uint32)((LBS - (Placement & (LBS - 1))) & (LBS - 1));
        if (d && d < sizeof(FILE_IDENT_DESC)) {
            uint32 IUl = FileInfo->FileIdent->lengthOfImpUse;
            uint32 FIl = FileInfo->FileIdent->lengthFileIdent;
            uint32 NewIUl;
            d = (d + 3) & ~((uint32)3);
            NewIUl = max(IUl + d, 32);
            d = NewIUl - IUl;
            if ((uint64)sizeof(FILE_IDENT_DESC) + IUl + FIl > l ||
                l > MAXUSHORT || NewIUl > MAXUSHORT || d > MAXUSHORT - l)
                try_return(status = STATUS_OBJECT_NAME_INVALID);
            if (!MyReallocPool__((PCHAR)FileInfo->FileIdent, l,
                                (PCHAR*)&FileInfo->FileIdent, l + d))
                try_return(status = STATUS_INSUFFICIENT_RESOURCES);
            l += d;
            RtlMoveMemory((int8*)(FileInfo->FileIdent + 1) + NewIUl,
                          (int8*)(FileInfo->FileIdent + 1) + IUl, FIl);
            RtlZeroMemory((int8*)(FileInfo->FileIdent + 1) + IUl, d);
            RtlZeroMemory((int8*)(FileInfo->FileIdent + 1) + NewIUl + FIl,
                          l - sizeof(FILE_IDENT_DESC) - NewIUl - FIl);
            if (!IUl)
                UDFSetEntityID_imp((EntityID*)(FileInfo->FileIdent + 1), UDF_ID_DEVELOPER);
            FileInfo->FileIdent->lengthOfImpUse = (uint16)NewIUl;
            FileInfo->FileIdentLen = l;
        }
        /* Index offsets/length are 32-bit. Refuse before any index mutation
         * rather than wrapping back into an earlier directory record. */
        if (l > MAXULONG - AppendOffset)
            try_return(status = STATUS_FILE_TOO_LARGE);
        status = UDFDirIndexGrow(&DirInfo->Dloc->DirIndex, 1);
        if (!NT_SUCCESS(status)) try_return(status);
        NewNameAppend = TRUE;
        hDirNdx = DirInfo->Dloc->DirIndex;
        DirNdx = UDFDirIndex(hDirNdx, i);
        DirNdx->Offset = AppendOffset;
        DirNdx->Length = l;
CrF__1:
        if (DirNdx->FName.Buffer) MyFreePool__(DirNdx->FName.Buffer);
        DirNdx->FName = PreparedName;
        PreparedNameBuffer = NULL; /* index owns it, including exception unwind */
CrF__2:
        // Reset all flags: stale FI_INTERNAL from deleted entry would
        // hide the new file from enumeration.
        DirNdx->FI_Flags = UDFBuildHashEntry(Vcb, &(DirNdx->FName), &(DirNdx->hashes), HASH_ALL)
                         | UDF_FI_FLAG_FI_MODIFIED;
        // we get here immediately when 'undel' occured
        FileInfo->Index = i;
        ASSERT(!DirNdx->FileInfo);
        /* A failed mapping allocation must still have the exact parent
         * context needed to detach this weak index pointer during unwind. */
        status = UDFSetFileInfoConstructionParent(Vcb, FileInfo, DirInfo);
        if (status != STATUS_SUCCESS) {
            /* Name/index preparation already ran. Do not free this FE
             * just because a refused transfer left ParentFile NULL. */
            UDFQuarantineCreateConstruction(Vcb, FileInfo, status);
            try_return(status);
        }
        DirNdx->FileInfo = FileInfo;
        DirNdx->FileEntryLoc = FEicb.extLocation;
        // mark file as 'deleted' for now
        DirNdx->FileCharacteristics = FILE_DELETED;
        FileInfo->FileIdent->fileCharacteristics |= FILE_DELETED;
        FileInfo->Dloc->DataLoc.Mapping = UDFExtentToMapping(&(FileInfo->Dloc->FELoc.Mapping[0]));
        if (!(FileInfo->Dloc->DataLoc.Mapping)) {
            /* No valid new FID was published; do not initiate tombstone I/O
             * using an incompletely initialized constructor. */
            try_return (status = STATUS_INSUFFICIENT_RESOURCES);
        }
        FileInfo->Dloc->DataLoc.Length = 0;
        FileInfo->Dloc->DataLoc.Offset = FileInfo->Dloc->FileEntryLen;
        // init FileEntry
        UDFSetFileUID(FileInfo, NewUniqueId);
        UDFSetFileSize(FileInfo, 0);
        UDFIncFileLinkCount(FileInfo); // increase to 1
        if (UDFIsAStreamDir(DirInfo))
            ((icbtag*)(FileInfo->Dloc->FileEntry+1))->flags |= ICB_FLAG_STREAM;
        UDFUpdateCreateTime(Vcb, FileInfo);
        UDFAttributesToUDF(UDFDirIndex(UDFGetDirIndexByFileInfo(FileInfo),FileInfo->Index),
                             FileInfo->Dloc->FileEntry, Vcb->DefaultAttr);
        FileInfo->Dloc->DataLoc.Mapping[0].extLength &= UDF_EXTENT_LENGTH_MASK;
        FileInfo->Dloc->DataLoc.Modified = TRUE;
        FileInfo->Dloc->FELoc.Mapping[0].extLength &= UDF_EXTENT_LENGTH_MASK;
        /* UDFBuildFileEntry zeroes its sector-sized buffer.  UDFFlushFE below
           writes the complete in-ICB FE sector, including its zero tail, so
           a separate zero-sector write here is immediately overwritten. */
#if 0
        if ((i >= 2) && (DirNdx->FName.Buffer[0] == L'.')) {
            BrutePoint();
        }
#endif

#ifdef UDF_CHECK_DISK_ALLOCATION
        if (  /*FileInfo->Fcb &&*/
             (Vcb->BitmapFcb ? UDFIsBitmapBitFree(Vcb, FileInfo->Dloc->FELoc.Mapping[0].extLocation - Vcb->Partitions[0].PartitionRoot) : UDFGetFreeBit(((uint32*)(Vcb->FSBM_Bitmap)), FileInfo->Dloc->FELoc.Mapping[0].extLocation - Vcb->Partitions[0].PartitionRoot))) {

            if (!FileInfo->FileIdent ||
               !(FileInfo->FileIdent->fileCharacteristics & FILE_DELETED)) {
                AdPrint(("Flushing to Discarded block %x\n", FileInfo->Dloc->FELoc.Mapping[0].extLocation));
                BrutePoint();
            }
        }
#endif // UDF_CHECK_DISK_ALLOCATION

        // make FileIdent valid
        FileInfo->FileIdent->fileCharacteristics = StageOnly ? FILE_DELETED : 0;
        DirNdx->FileCharacteristics = StageOnly ? FILE_DELETED : 0;
        /* A deferred initial FE remains owned and dirty until the existing
         * scalar or batch FE-before-FID publication boundary. No valid FID
         * may be emitted merely because this private buffer was initialized.
         * Reused slots and unsupported ownership retain the immediate write. */
        status = STATUS_SUCCESS;
        InitialPublicationStarted = TRUE;
        if (!StageOnly && NewNameAppend && !undel)
            status = UDFReserveInitialFid(IrpContext, Vcb, FileInfo,
                                          PartNum, &InitialFEDeferred);
        if (NT_SUCCESS(status) && InitialFEDeferred) {
            FileInfo->Dloc->FE_Flags |= UDF_FE_FLAG_NEW_UNPUBLISHED;
            UDF_METADATA_COUNT(Vcb,60);
        } else if (NT_SUCCESS(status)) {
            status = UDFFlushFE(IrpContext, Vcb, FileInfo, PartNum);
        }
        if (NT_SUCCESS(status) && !StageOnly && NewNameAppend && !undel &&
            !UDFIsAStreamDir(DirInfo) && UDFMayDeferNewCreate(IrpContext, Vcb))
        {
            FileInfo->Dloc->FE_Flags |= UDF_FE_FLAG_NEW_UNPUBLISHED;
            UDF_METADATA_COUNT(Vcb,30);
        }
        if (!NT_SUCCESS(status)) {
            /* Do not issue a second, unchecked tombstone write or infer
             * absence from RAM. FINALLY retains this exact generation. */
            try_return (status);
        }
        status = UDFCompleteFileInfoConstruction(Vcb, FileInfo);
        if (!NT_SUCCESS(status)) try_return(status);
        FileInfo->CreateConstructionOpenOwned = TRUE;
        if (StageOnly)
            hDirNdx->DelCount++; /* new reserved tombstone, no live file yet */
        else {
            if (undel) hDirNdx->DelCount--;
            if (!UDFIsAStreamDir(DirInfo)) UDFIncFileCounter(Vcb);
        }

        UDFCheckSpaceAllocation(Vcb, 0, FileInfo->Dloc->DataLoc.Mapping, AS_USED); // check if used
        status = UDFReleaseCreateConstruction(Vcb, FileInfo);
        if (status != STATUS_SUCCESS) try_return(status);
        if (FreshFileCreated) *FreshFileCreated = TRUE;
        try_return (status = STATUS_SUCCESS);

try_exit:   NOTHING;

    } _SEH2_FINALLY {
        BOOLEAN Abnormal = _SEH2_AbnormalTermination();
        if (PreparedNameBuffer) MyFreePool__(PreparedNameBuffer);
        if (FEAllocated && FileInfo && FileInfo->Dloc &&
            (FileInfo->CreateConstructionParent ||
             FileInfo->Dloc->CreateConstructionOwner == FileInfo) &&
            (Abnormal || !NT_SUCCESS(status))) {
            NTSTATUS ReleaseStatus = STATUS_DEVICE_BUSY;
            /* Before binding a weak parent/index edge or beginning media
             * publication, ordinary allocation failure remains private. */
            if (!Abnormal && !InitialPublicationStarted && !FileInfo->ParentFile)
                ReleaseStatus = UDFReleaseCreateConstruction(Vcb, FileInfo);
            if (ReleaseStatus != STATUS_SUCCESS)
                UDFQuarantineCreateConstruction(Vcb, FileInfo,
                    Abnormal ? STATUS_UNSUCCESSFUL : status);
        }
        if (Abnormal) {
            /* A possibly submitted write is not an ordinary allocation
             * failure. Preserve the generation for its caller's unwind. */
            if (FEAllocated && FileInfo->Dloc && !FileInfo->CreateConstructionParent &&
                !FileInfo->Dloc->CreateConstructionOwner) {
                FileInfo->ConstructionFailed = TRUE;
                FileInfo->CleanupPending = TRUE;
                Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
                InterlockedCompareExchange(&Vcb->MetadataWritebackError,
                                            STATUS_UNSUCCESSFUL, STATUS_SUCCESS);
            }
        } else if (!NT_SUCCESS(status)) {
            if (FEAllocated && FileInfo->Dloc && !FileInfo->CreateConstructionParent &&
                !FileInfo->Dloc->CreateConstructionOwner) {
                if (StageOnly && FileInfo->ParentFile) {
                    /* The tombstone write may have reached the medium.
                     * Its caller takes an explicit quarantine hold; keep
                     * allocation and all buffers reachable until then. */
                    FileInfo->ConstructionFailed = TRUE;
                    FileInfo->CleanupPending = TRUE;
                    Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
                    InterlockedCompareExchange(&Vcb->MetadataWritebackError,
                                                status, STATUS_SUCCESS);
                } else {
                    UDFDropFileInfoConstruction(Vcb, FileInfo);
                    /* The caller's failed-FI cleanup owns inner mappings and
                     * the FE buffer. Removing only the Dloc shell leaks them.
                     * Do not release allocation if construction still owns a
                     * reference or another generation unexpectedly joined. */
                    if (!FileInfo->RefCount && !FileInfo->Dloc->LinkRefCount &&
                        !FileInfo->Dloc->CommonFcb &&
                        FileInfo->NextLinkedFile == FileInfo && !FileInfo->OpenCount) {
                        UDFFreeFESpace(Vcb, DirInfo, &FileInfo->Dloc->FELoc);
                        if (FileInfo->Dloc->FELoc.Mapping)
                            FileInfo->Dloc->FELoc.Mapping[0].extLocation = 0;
                    } else {
                        FileInfo->CleanupPending = TRUE;
                        Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
                        InterlockedCompareExchange(&Vcb->MetadataWritebackError,
                                                    status, STATUS_SUCCESS);
                    }
                    if (StageOnly && NewNameAppend && !FileInfo->ParentFile &&
                        !FileInfo->RefCount && !FileInfo->Dloc->LinkRefCount &&
                        !FileInfo->Dloc->CommonFcb && !FileInfo->OpenCount &&
                        FileInfo->NextLinkedFile == FileInfo &&
                        UDFDirIndexGetLastIndex(DirInfo->Dloc->DirIndex) == i + 1) {
                        NTSTATUS RollbackStatus;
                        PDIR_INDEX_ITEM Entry = UDFDirIndex(DirInfo->Dloc->DirIndex, i);
                        if (Entry->FName.Buffer) MyFreePool__(Entry->FName.Buffer);
                        RtlZeroMemory(Entry, sizeof(*Entry));
                        RollbackStatus = UDFDirIndexTrunc(&DirInfo->Dloc->DirIndex, 1);
                        if (!NT_SUCCESS(RollbackStatus)) {
                            Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
                            InterlockedCompareExchange(&Vcb->MetadataWritebackError,
                                                        RollbackStatus, STATUS_SUCCESS);
                        }
                    }
                }
            }
        }
    } _SEH2_END
    return status;

} // end UDFCreateFileInternal()

NTSTATUS
UDFCreateFile__(PIRP_CONTEXT IrpContext, PVCB Vcb, BOOLEAN IgnoreCase,
    PUNICODE_STRING Name, uint32 ExtAttrSize, uint32 ImpUseLength,
    BOOLEAN Extended, BOOLEAN CreateNew, PUDF_FILE_INFO Directory,
    PUDF_FILE_INFO* FileInfo, PBOOLEAN FreshFileCreated)
{
    return UDFCreateFileInternal(IrpContext, Vcb, IgnoreCase, Name,
        ExtAttrSize, ImpUseLength, Extended, CreateNew, Directory,
        FileInfo, (uint_di)-1, FALSE, FreshFileCreated);
}


/*
    This routine reads data from file described by FileInfo
 */
/*__inline
NTSTATUS
UDFReadFile__(
    IN PVCB Vcb,
    IN PUDF_FILE_INFO FileInfo,
    IN int64 Offset,   // offset in extent
    IN SIZE_T Length,
    IN BOOLEAN Direct,
    OUT int8* Buffer,
    OUT PSIZE_T ReadBytes
    )
{
    ValidateFileInfo(FileInfo);

    return UDFReadExtent(Vcb, &(FileInfo->Dloc->DataLoc), Offset, Length, Direct, Buffer, ReadBytes);
} // end UDFReadFile__()*/

/*
    This routine zeros data in file described by FileInfo
 */
__inline
NTSTATUS
UDFZeroFile__(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN PUDF_FILE_INFO FileInfo,
    IN int64 Offset,   // offset in extent
    IN SIZE_T Length,
    IN BOOLEAN Direct,
    OUT PSIZE_T ReadBytes
    )
{
    ValidateFileInfo(FileInfo);

    return UDFZeroExtent__(IrpContext, Vcb, &(FileInfo->Dloc->DataLoc), Offset, Length, Direct, ReadBytes);
} // end UDFZeroFile__()*/

/*
    This routine makes sparse area in file described by FileInfo
 */
__inline
NTSTATUS
UDFSparseFile__(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN PUDF_FILE_INFO FileInfo,
    IN int64 Offset,   // offset in extent
    IN SIZE_T Length,
    IN BOOLEAN Direct,
    OUT PSIZE_T ReadBytes
    )
{
    ValidateFileInfo(FileInfo);

    return UDFSparseExtent__(IrpContext, Vcb, &(FileInfo->Dloc->DataLoc), Offset, Length, Direct, ReadBytes);
} // end UDFSparseFile__()*/

/*
    This routine fills tails of the last sector in extent with ZEROs
 */
NTSTATUS
UDFPadLastSector(PIRP_CONTEXT IrpContext, PVCB Vcb, PEXTENT_INFO ExtInfo)
{
    SIZE_T Available, ToWrite, Written;
    uint32 Lba, SectorOffset, Flags;
    uint64 Offset;
    NTSTATUS Status;
    if (!Vcb || !ExtInfo || !ExtInfo->Mapping || ExtInfo->Length <= 0 ||
        !Vcb->SectorSize || Vcb->SectorShift >= 30 ||
        Vcb->SectorSize != ((uint32)1 << Vcb->SectorShift) ||
        (uint64)ExtInfo->Length > (uint64)MAXLONGLONG - ExtInfo->Offset)
        return STATUS_INVALID_PARAMETER;
    Offset = (uint64)ExtInfo->Length + ExtInfo->Offset;
    if (!(Offset & (Vcb->SectorSize - 1))) return STATUS_SUCCESS;
    Lba = UDFExtentOffsetToLba(Vcb, ExtInfo->Mapping, (int64)Offset,
                              &SectorOffset, &Available, &Flags, NULL);
    if (Lba == LBA_OUT_OF_EXTENT || Flags > EXTENT_NOT_RECORDED_ALLOCATED ||
        SectorOffset >= Vcb->SectorSize || !Available)
        return STATUS_FILE_CORRUPT_ERROR;
    /* A mapping run may include many sectors. ZBuffer supplies a single
     * sector tail, not the whole remaining preallocated run. */
    ToWrite = Vcb->SectorSize - SectorOffset;
    if (Available < ToWrite || !Vcb->ZBuffer) return STATUS_FILE_CORRUPT_ERROR;
    Status = UDFWriteData(IrpContext, Vcb, TRUE,
        (((uint64)Lba) << Vcb->SectorShift) + SectorOffset,
        ToWrite, FALSE, Vcb->ZBuffer, &Written);
    if (NT_SUCCESS(Status) && Written != ToWrite) Status = STATUS_IO_DEVICE_ERROR;
    return Status;
} // UDFPadLastSector()

/*
    This routine updates AllocDesc sequence, FileIdent & FileEntry
    for given file
 */
/* Consume only the caller's exact acquired FI hold. No allocation, I/O
 * or exception-raising work may separate the ownership decision from these
 * three decrements. Writeback is a distinct, repeatable operation below. */
static NTSTATUS
UDFDereferenceFileLocked(PVCB Vcb, PUDF_FILE_INFO FileInfo)
{
    PUDF_DATALOC_INFO Dloc;
    PUDF_FILE_INFO Parent;
    NTSTATUS Status = STATUS_SUCCESS;
    if (!FileInfo)
        return STATUS_INVALID_PARAMETER;
    Dloc = FileInfo->Dloc;
    Parent = FileInfo->ParentFile;
    ASSERT(FileInfo->RefCount >= 1);
    if (!Dloc || FileInfo->Constructing || !FileInfo->RefCount ||
        !Dloc->LinkRefCount || (Parent && !Parent->OpenCount)) {
        Status = STATUS_FILE_CORRUPT_ERROR;
        Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
        InterlockedCompareExchange(&Vcb->MetadataWritebackError, Status, STATUS_SUCCESS);
    } else {
        InterlockedDecrement((PLONG)&FileInfo->RefCount);
        InterlockedDecrement((PLONG)&Dloc->LinkRefCount);
        if (Parent)
            InterlockedDecrement((PLONG)&Parent->OpenCount);
    }
    return Status;
}

NTSTATUS
UDFDereferenceFileChecked(PVCB Vcb, PUDF_FILE_INFO FileInfo)
{
    NTSTATUS Status;
    UDFAcquireResourceExclusive(&Vcb->DlocResource2, TRUE);
    _SEH2_TRY {
        Status = UDFDereferenceFileLocked(Vcb, FileInfo);
    } _SEH2_FINALLY {
        UDFReleaseResource(&Vcb->DlocResource2);
    } _SEH2_END;
    return Status;
}

/* Retain the caller's real ordinary-open token without consuming it or
 * manufacturing a replacement FI reference. DlocResource2 is held. */
static VOID
UDFRetainOrdinaryReferenceLocked(PVCB Vcb, PUDF_FILE_INFO FileInfo,
                                NTSTATUS Error)
{
    if (NT_SUCCESS(Error)) Error = STATUS_FILE_CORRUPT_ERROR;
    FileInfo->CleanupPending = TRUE;
    if (NT_SUCCESS(FileInfo->RetainedOpenError)) FileInfo->RetainedOpenError = Error;
    if (FileInfo->RetainedOpenReferences != MAXULONG)
        FileInfo->RetainedOpenReferences++;
    if (Vcb->RetainedOpenCount < 0)
        InterlockedExchange(&Vcb->RetainedOpenCount, MAXLONG);
    else if (Vcb->RetainedOpenCount < MAXLONG)
        InterlockedIncrement(&Vcb->RetainedOpenCount);
    Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
    InterlockedCompareExchange(&Vcb->MetadataWritebackError, Error, STATUS_SUCCESS);
}

static VOID
UDFRetainOrdinaryOpenReference(PVCB Vcb, PUDF_FILE_INFO* OpenReference,
                              NTSTATUS Error)
{
    PUDF_FILE_INFO FileInfo = *OpenReference;
    if (!FileInfo) return;
    UDFAcquireResourceExclusive(&Vcb->DlocResource2, TRUE);
    _SEH2_TRY {
        UDFRetainOrdinaryReferenceLocked(Vcb, FileInfo, Error);
        *OpenReference = NULL; /* the indexed retained-token inventory owns it */
    } _SEH2_FINALLY {
        UDFReleaseResource(&Vcb->DlocResource2);
    } _SEH2_END;
}

/* The failed request owns a successful open OR a partial constructor, never
 * a new metadata flush. Ordinary success consumes its FI/Dloc/exact-parent
 * contribution once and hands dirty metadata to the indexed collector. An
 * inconsistent decrement transfers the same token to explicit retained FI
 * inventory; it does not acquire a replacement or guess another owner. */
NTSTATUS
UDFReleaseCreateReference(PIRP_CONTEXT IrpContext, PVCB Vcb,
                           PUDF_FILE_INFO* OpenReference, BOOLEAN CloseLater)
{
    PUDF_FILE_INFO FileInfo;
    PFCB Fcb;
    NTSTATUS Status;
    if (!OpenReference) return STATUS_INVALID_PARAMETER;
    FileInfo = *OpenReference;
    if (!FileInfo) return STATUS_SUCCESS;
    if (!FileInfo->Dloc || FileInfo->Constructing || FileInfo->ConstructionFailed) {
        Status = UDFRetainFailedStreamOpen(Vcb, OpenReference);
        return NT_SUCCESS(Status) ? STATUS_FILE_INVALID : Status;
    }
    UDFAcquireResourceExclusive(&Vcb->DlocResource2, TRUE);
    _SEH2_TRY {
        if (CloseLater) FileInfo->CleanupPending = TRUE;
        Status = UDFDereferenceFileLocked(Vcb, FileInfo);
        if (!NT_SUCCESS(Status)) {
            UDFRetainOrdinaryReferenceLocked(Vcb, FileInfo, Status);
        }
        Fcb = FileInfo->Dloc->CommonFcb;
        *OpenReference = NULL; /* consumed or exact token transferred to FI */
    } _SEH2_FINALLY {
        UDFReleaseResource(&Vcb->DlocResource2);
    } _SEH2_END;
    if ((CloseLater || !NT_SUCCESS(Status)) && Fcb && !Fcb->FcbReference)
        UDFScheduleFcbTeardownRetry(IrpContext, Fcb);
    return Status;
}

static NTSTATUS
UDFCompleteFileCloseInternal(PIRP_CONTEXT IrpContext, PVCB Vcb,
                             PUDF_FILE_INFO FileInfo)
{
    PUDF_FILE_INFO DirInfo;
    NTSTATUS status;
    uint32 PartNum;
    if (!FileInfo)
        return STATUS_SUCCESS;
    if (FileInfo->Constructing || !FileInfo->Dloc)
        return STATUS_FILE_INVALID;
    if (FileInfo->Index < 2 && FileInfo->ParentFile && !UDFIsAStreamDir(FileInfo))
        return STATUS_SUCCESS;
    DirInfo = FileInfo->ParentFile;
    // If the file has gone (unlinked) we should return STATUS_SUCCESS here.
    if (!FileInfo->Dloc) return STATUS_SUCCESS;
    if (!NT_SUCCESS(FileInfo->Dloc->ZeroDataError) &&
        !UDFIsZeroDataRetry(FileInfo->Dloc))
        return FileInfo->Dloc->ZeroDataError;

    if (FileInfo->RefCount ||
       FileInfo->OpenCount ||
       !(FileInfo->Dloc->FELoc.Mapping)) return STATUS_SUCCESS;
    if (FileInfo->Dloc->DeleteCommitted && !UDFGetFileNameLinkCount(FileInfo))
        return STATUS_SUCCESS; /* no I/O and no clearing of cached-data ownership */
    if (Vcb->VcbState & VCB_STATE_VOLUME_READ_ONLY) {
        PUDF_DATALOC_INFO Dloc = FileInfo->Dloc;
        PDIR_INDEX_ITEM Entry = DirInfo ?
            UDFDirIndex(DirInfo->Dloc->DirIndex, FileInfo->Index) : NULL;
        /* Consume the close hold once, but do not issue another metadata
         * write after an earlier failure made the volume read-only. A clean
         * read-only open needs no work and can still close successfully. */
        if ((Dloc->FE_Flags & (UDF_FE_FLAG_FE_MODIFIED | UDF_FE_FLAG_CACHED_DATA_PENDING)) ||
            Dloc->DataLoc.Modified || Dloc->AllocLoc.Modified || Dloc->FELoc.Modified ||
            (Dloc->DataLoc.Flags & EXTENT_FLAG_PREALLOCATED) ||
            UDFDirectoryAppendPending(FileInfo) ||
            (Entry && (Entry->FI_Flags & UDF_FI_FLAG_FI_MODIFIED)))
            return STATUS_MEDIA_WRITE_PROTECTED;
        return STATUS_SUCCESS;
    }
    if (UDFDeferNewFileMetadata(IrpContext, Vcb, FileInfo))
        return STATUS_SUCCESS;
    /* Cleanup already consumed this FI reference. Its FILE_OBJECT still
     * holds the indexed FCB until IRP_CLOSE; do not immediately undo a
     * proved attribute-only cleanup exception. Final close/flush stay eager. */
    if (UDFCanRetainDirectoryAttributesClose(IrpContext, Vcb, FileInfo))
        return STATUS_SUCCESS;
    status = UDFFlushDirectoryAppend(IrpContext, Vcb, FileInfo);
    if (!NT_SUCCESS(status))
        return status;
//    ASSERT(FileInfo->Dloc->FELoc.Mapping[0].extLocation);
    PartNum = UDFGetRefPartNumByPhysLba(Vcb, FileInfo->Dloc->FELoc.Mapping[0].extLocation);
    if (PartNum == (uint32)-1) {
        UDFPrint(("  Is DELETED ?\n"));
        if (DirInfo) {
            PartNum = UDFGetRefPartNumByPhysLba(Vcb, DirInfo->Dloc->FELoc.Mapping[0].extLocation);
        } else {
            BrutePoint();
        }
    }
#ifdef UDF_CHECK_DISK_ALLOCATION
    if (  FileInfo->Fcb &&
         (Vcb->BitmapFcb ? UDFIsBitmapBitFree(Vcb, FileInfo->Dloc->FELoc.Mapping[0].extLocation - Vcb->Partitions[0].PartitionRoot) : UDFGetFreeBit(((uint32*)(Vcb->FSBM_Bitmap)), FileInfo->Dloc->FELoc.Mapping[0].extLocation - Vcb->Partitions[0].PartitionRoot))) {

        //ASSERT(FileInfo->Dloc->FELoc.Mapping[0].extLocation);
        if (UDFIsAStreamDir(FileInfo)) {
            if (!UDFIsSDirDeleted(FileInfo)) {
                UDFPrint(("  Not DELETED SDir\n"));
                BrutePoint();
            }
            ASSERT(!FileInfo->Dloc->FELoc.Modified);
        } else
        if (!FileInfo->FileIdent ||
           !(FileInfo->FileIdent->fileCharacteristics & FILE_DELETED)) {
            if (!FileInfo->FileIdent) {
                AdPrint(("  No FileIdent\n"));
            }
            if (FileInfo->FileIdent &&
               !(FileInfo->FileIdent->fileCharacteristics & FILE_DELETED))
                AdPrint(("  Not DELETED\n"));
            ASSERT(FileInfo->Dloc->FELoc.Mapping[0].extLocation);
            AdPrint(("Flushing to Discarded block %x\n", FileInfo->Dloc->FELoc.Mapping[0].extLocation));
            BrutePoint();
        } else {
            UDFCheckSpaceAllocation(Vcb, 0, FileInfo->Dloc->DataLoc.Mapping, AS_FREE); // check if free
            UDFCheckSpaceAllocation(Vcb, 0, FileInfo->Dloc->FELoc.Mapping, AS_FREE); // check if free
        }
    } else {
        if (!FileInfo->Dloc->FELoc.Mapping[0].extLocation ||
            (Vcb->BitmapFcb ? UDFIsBitmapBitFree(Vcb, FileInfo->Dloc->FELoc.Mapping[0].extLocation - Vcb->Partitions[0].PartitionRoot) : UDFGetFreeBit(((uint32*)(Vcb->FSBM_Bitmap)), FileInfo->Dloc->FELoc.Mapping[0].extLocation - Vcb->Partitions[0].PartitionRoot))) {
            UDFCheckSpaceAllocation(Vcb, 0, FileInfo->Dloc->DataLoc.Mapping, AS_FREE); // check if free
        } else {
            UDFCheckSpaceAllocation(Vcb, 0, FileInfo->Dloc->DataLoc.Mapping, AS_USED); // check if used
        }
    }
#endif // UDF_CHECK_DISK_ALLOCATION

    // we needn't flushing FE & Allocs untill all links are closed...
    if (!FileInfo->Dloc->LinkRefCount) {

        // flush pre-allocation charge for directories
        if (FileInfo->Dloc &&
           FileInfo->Dloc->DirIndex) {

            if (FileInfo->Dloc->DataLoc.Flags & EXTENT_FLAG_PREALLOCATED) {
                FileInfo->Dloc->DataLoc.Flags |= EXTENT_FLAG_CUT_PREALLOCATED;
                status = UDFResizeExtent(IrpContext, Vcb, FileInfo, PartNum, UDFGetFileSize(FileInfo), FALSE, &(FileInfo->Dloc->DataLoc));
                FileInfo->Dloc->DataLoc.Flags &= ~EXTENT_FLAG_CUT_PREALLOCATED;
                if (!NT_SUCCESS(status))
                    return status;
                FileInfo->Dloc->DataLoc.Flags &= ~EXTENT_FLAG_PREALLOCATED;
                AdPrint(("Dir pre-alloc truncated (Close)\n"));
                FileInfo->Dloc->DataLoc.Modified = TRUE;
            }
        }

        if (!NT_SUCCESS(status = UDFFlushFE(IrpContext, Vcb, FileInfo, PartNum))) {
            UDFPrint(("Error flushing FE\n"));
            /* A failed FE write is retryable.  Turning its FID into a
             * deletion tombstone would make the file disappear on remount. */
            return status;
        }
    }
    // ... but FI must be updated (if any)
    if (!NT_SUCCESS(status = UDFFlushFI(IrpContext, Vcb, FileInfo, PartNum))) {
        UDFPrint(("Error flushing FI\n"));
        return status;
    }
#ifdef UDF_DBG
//    ASSERT(FileInfo->Dloc->FELoc.Mapping[0].extLocation);
    if ((FileInfo->Dloc->FileEntry->descVersion != 2) &&
       (FileInfo->Dloc->FileEntry->descVersion != 3)) {
        ASSERT((Vcb->BitmapFcb ? UDFIsBitmapBitFree(Vcb, FileInfo->Dloc->FELoc.Mapping[0].extLocation - Vcb->Partitions[0].PartitionRoot) : UDFGetFreeBit(((uint32*)(Vcb->FSBM_Bitmap)), FileInfo->Dloc->FELoc.Mapping[0].extLocation - Vcb->Partitions[0].PartitionRoot)));
    }
#endif // UDF_DBG
    return STATUS_SUCCESS;
}

NTSTATUS
UDFCompleteFileClose(PIRP_CONTEXT IrpContext, PVCB Vcb,
                     PUDF_FILE_INFO FileInfo)
{
    NTSTATUS Status = STATUS_UNSUCCESSFUL;
    _SEH2_TRY {
        Status = UDFCompleteFileCloseInternal(IrpContext, Vcb, FileInfo);
    } _SEH2_FINALLY {
        if (_SEH2_AbnormalTermination()) {
            Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
            InterlockedCompareExchange(&Vcb->MetadataWritebackError,
                                        STATUS_UNSUCCESSFUL, STATUS_SUCCESS);
        }
    } _SEH2_END;
    UDFRememberMetadataWritebackError(IrpContext, Vcb, FileInfo, Status);
    return Status;
}

static NTSTATUS
UDFCloseFile__Internal(PIRP_CONTEXT IrpContext, PVCB Vcb,
                       PUDF_FILE_INFO FileInfo)
{
    NTSTATUS Status;
    ValidateFileInfo(FileInfo);
    if (!FileInfo)
        return STATUS_SUCCESS;
    if (FileInfo->Constructing) {
        UDFDropFileInfoConstruction(Vcb, FileInfo);
        return STATUS_FILE_INVALID;
    }
    if (!FileInfo->RefCount || !FileInfo->Dloc)
        return STATUS_FILE_INVALID;
    Status = UDFDereferenceFileChecked(Vcb, FileInfo);
    if (!NT_SUCCESS(Status))
        return Status;
    return UDFCompleteFileClose(IrpContext, Vcb, FileInfo);
} // end UDFCloseFile__Internal()

NTSTATUS
UDFCloseFile__(PIRP_CONTEXT IrpContext, PVCB Vcb, PUDF_FILE_INFO FileInfo)
{
    NTSTATUS status = UDFCloseFile__Internal(IrpContext, Vcb, FileInfo);
    UDFRememberMetadataWritebackError(IrpContext, Vcb, FileInfo, status);
    return status;
}

/*
    Helper function to handle parent FID updates
*/
static
NTSTATUS
UDFUpdateParentFID(PIRP_CONTEXT IrpContext, PVCB Vcb,
                   PUDF_FILE_INFO DirInfo, BOOLEAN WriteToMedia)
{
    NTSTATUS Status;
    PDIR_INDEX_ITEM Entry;
    PFILE_IDENT_DESC Fid;
    PUDF_FILE_INFO Parent = DirInfo->ParentFile;
    uint32 PartNum, ParentLba, ParentLbn;
    SIZE_T WrittenBytes = 0;
    int8* Buffer;
    if (!Parent || !Parent->Dloc || !Parent->Dloc->FELoc.Mapping ||
        !(Entry = UDFDirIndex(DirInfo->Dloc->DirIndex, 1)) ||
        Entry->Length < sizeof(FILE_IDENT_DESC) || Entry->Length > MAXUSHORT)
        return STATUS_FILE_CORRUPT_ERROR;
    ParentLba = Parent->Dloc->FELoc.Mapping[0].extLocation;
    PartNum = UDFGetRefPartNumByPhysLba(Vcb, ParentLba);
    ParentLbn = PartNum == (uint32)-1 ? (uint32)-1 :
        UDFPhysLbaToPart(Vcb, PartNum, ParentLba);
    if (!ParentLba || ParentLbn == (uint32)-1 || PartNum > MAXUSHORT)
        return STATUS_FILE_CORRUPT_ERROR;

    /* The FI already moved. Its borrowed '..' identity must follow that
     * parent even when the media update fails; the new FI/LCB holds the
     * parent, and the old parent must no longer be borrowed after its close. */
    Entry->FileInfo = Parent;
    Entry->FileEntryLoc.logicalBlockNum = ParentLbn;
    Entry->FileEntryLoc.partitionReferenceNum = (uint16)PartNum;
    if (!WriteToMedia)
        return STATUS_SUCCESS;
    Buffer = MyAllocatePool__(NonPagedPool, Entry->Length);
    if (!Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;
    _SEH2_TRY {
        Status = UDFReadFile__(IrpContext, Vcb, DirInfo, Entry->Offset,
                               Entry->Length, FALSE, Buffer);
        if (NT_SUCCESS(Status)) {
            Fid = (PFILE_IDENT_DESC)Buffer;
            if (!(Fid->fileCharacteristics & FILE_PARENT) ||
                Fid->descTag.tagIdent != TID_FILE_IDENT_DESC) {
                Status = STATUS_FILE_CORRUPT_ERROR;
            } else {
                Fid->icb.extLength = Vcb->SectorSize;
                Fid->icb.extLocation = Entry->FileEntryLoc;
                RtlZeroMemory(&Fid->icb.impUse, sizeof(Fid->icb.impUse));
                ((struct FidADImpUse*)&Fid->icb.impUse)->uniqueID =
                    (uint32)UDFGetFileUID(Parent);
                UDFSetUpTag(Vcb, &Fid->descTag, (uint16)Entry->Length,
                             Fid->descTag.tagLocation, 0);
                Status = UDFWriteFile__(IrpContext, Vcb, DirInfo, Entry->Offset,
                    Entry->Length, FALSE, Buffer, &WrittenBytes);
                if (NT_SUCCESS(Status) && WrittenBytes != Entry->Length)
                    Status = STATUS_IO_DEVICE_ERROR;
            }
        }
    } _SEH2_FINALLY {
        MyFreePool__(Buffer);
    } _SEH2_END;
    return Status;
}

/* A temporary FE is still a real allocated generation until the target
 * FID no longer points to it and that write passed a lower-device barrier.
 * The owner is explicit so an error cannot discard its mapping at last close. */
static NTSTATUS
UDFOwnNamespaceScratch(PVCB Vcb, PUDF_FILE_INFO Owner,
                       PUDF_FILE_INFO Namespace, PUDF_FILE_INFO Scratch)
{
    PUDF_DATALOC_INFO Dloc = Owner->Dloc;
    NTSTATUS Status = STATUS_FILE_CORRUPT_ERROR;
    LONG Count;
    UDFAcquireResourceExclusive(&Vcb->DlocResource2, TRUE);
    _SEH2_TRY {
        if (Dloc->NamespaceScratch == Scratch &&
            Scratch->Dloc->NamespaceScratchOwner == Dloc &&
            Dloc->NamespaceFileInfo == Namespace) {
            Status = STATUS_SUCCESS; /* an interrupted caller already transferred it */
            _SEH2_LEAVE;
        }
        if (Dloc->NamespaceScratch || Dloc->NamespaceTarget ||
            Dloc->NamespaceTargetState != UdfNamespaceTargetNone ||
            !NT_SUCCESS(Dloc->NamespaceTargetError) ||
            Dloc->NamespacePhase != UdfNamespaceNone || Scratch->Dloc->NamespaceScratchOwner ||
            Scratch->Dloc->CreateConstructionOwner || Scratch->Constructing ||
            Scratch->ConstructionReferenceOwned || Scratch->ConstructionFailed ||
            !Scratch->RefCount || !Scratch->ParentFile)
            _SEH2_LEAVE;
        for (;;) {
            Count = InterlockedCompareExchange(&Vcb->PendingNamespaceCount, 0, 0);
            if (Count < 0) _SEH2_LEAVE;
            if (Count >= MAXLONG - 1) {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                _SEH2_LEAVE;
            }
            if (InterlockedCompareExchange(&Vcb->PendingNamespaceCount,
                                           Count + 1, Count) == Count)
                break;
        }
        /* No allocation, I/O or callback between the count reservation and
         * transfer of the caller's existing successful stage reference. */
        Dloc->NamespaceScratch = Scratch;
        Dloc->NamespaceFileInfo = Namespace;
        Dloc->NamespacePhase = UdfNamespaceReserved;
        Scratch->Dloc->NamespaceScratchOwner = Dloc;
        Status = STATUS_SUCCESS;
    } _SEH2_FINALLY {
        UDFReleaseResource(&Vcb->DlocResource2);
    } _SEH2_END;
    return Status;
}

static NTSTATUS
UDFQuarantineNamespace(PVCB Vcb, NTSTATUS Status)
{
    ASSERT(!NT_SUCCESS(Status));
    Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
    InterlockedCompareExchange(&Vcb->MetadataWritebackError, Status, STATUS_SUCCESS);
    return Status;
}

/* Transfer the helper's one successful target open into the namespace
 * owner before its FID is detached. This generation reserves its own data
 * and FE until destination publication has passed a lower barrier. */
static NTSTATUS
UDFOwnNamespaceTarget(PVCB Vcb, PUDF_FILE_INFO Source, PUDF_FILE_INFO Target,
                      PUDF_FILE_INFO* OpenReference, PBOOLEAN TargetOwned)
{
    PUDF_DATALOC_INFO Owner = Source ? Source->Dloc : NULL;
    PUDF_DATALOC_INFO Dloc = Target ? Target->Dloc : NULL;
    NTSTATUS Status = STATUS_SHARING_VIOLATION;
    LONG Count;
    if (!OpenReference || !TargetOwned || !Owner || !Dloc)
        return STATUS_INVALID_PARAMETER;
    UDFAcquireResourceExclusive(&Vcb->DlocResource2, TRUE);
    _SEH2_TRY {
        if (Owner->NamespaceTarget == Target && Dloc->NamespaceScratchOwner == Owner &&
            Owner->NamespaceTargetState == UdfNamespaceTargetHeld &&
            Owner->NamespacePhase == UdfNamespaceTargetRetained &&
            !*OpenReference && *TargetOwned) {
            Status = STATUS_SUCCESS; /* this exact caller already transferred it */
            _SEH2_LEAVE;
        }
        if (*OpenReference != Target || *TargetOwned || Owner == Dloc ||
            Owner->NamespaceTarget || Owner->NamespaceTargetState != UdfNamespaceTargetNone ||
            !NT_SUCCESS(Owner->NamespaceTargetError) || !Owner->NamespaceScratch ||
            Owner->NamespacePhase != UdfNamespaceReserved || Dloc->CleanupOwner ||
            Dloc->CreateConstructionOwner || Dloc->CreateRollbackOwner ||
            Dloc->StreamDeleteOwner || Dloc->StreamDeleteContextOwner || Dloc->StreamCreate ||
            Dloc->NamespaceScratch || Dloc->NamespaceTarget || Dloc->NamespaceScratchOwner ||
            !Target->RefCount || !Target->ParentFile || Target->Constructing ||
            Target->ConstructionReferenceOwned || Target->ConstructionFailed)
            _SEH2_LEAVE;
        for (;;) {
            Count = InterlockedCompareExchange(&Vcb->PendingNamespaceCount, 0, 0);
            if (Count <= 0) {
                Status = STATUS_FILE_CORRUPT_ERROR; /* the scratch already owns one */
                _SEH2_LEAVE;
            }
            if (Count >= MAXLONG - 1) {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                _SEH2_LEAVE;
            }
            if (InterlockedCompareExchange(&Vcb->PendingNamespaceCount,
                                           Count + 1, Count) == Count)
                break;
        }
        /* The owner fields and the caller's exact-token receipt form one
         * callback-free group. A later exception cannot close that token
         * through the caller after it belongs to the namespace operation. */
        Owner->NamespaceTarget = Target;
        Owner->NamespaceTargetState = UdfNamespaceTargetHeld;
        Owner->NamespaceTargetError = STATUS_SUCCESS;
        Owner->NamespacePhase = UdfNamespaceTargetRetained;
        Dloc->NamespaceScratchOwner = Owner;
        *OpenReference = NULL;
        *TargetOwned = TRUE;
        Status = STATUS_SUCCESS;
    } _SEH2_FINALLY {
        UDFReleaseResource(&Vcb->DlocResource2);
    } _SEH2_END;
    return Status;
}

/* Called only after destination and source tombstones/FE writes passed the
 * final barrier. Existing live aliases still own their FCB/Dloc; only the
 * exact target hold is released here. */
static NTSTATUS
UDFRetireNamespaceTarget(PIRP_CONTEXT IrpContext, PVCB Vcb,
                         PUDF_DATALOC_INFO Owner)
{
    _SEH2_VOLATILE PUDF_FILE_INFO Target = NULL;
    _SEH2_VOLATILE PUDF_DATALOC_INFO Dloc = NULL;
    _SEH2_VOLATILE PFCB Fcb = NULL;
    _SEH2_VOLATILE BOOLEAN Acquired = FALSE, AttemptOwned = FALSE;
    _SEH2_VOLATILE BOOLEAN Drain = FALSE, ActiveRefusal = FALSE;
    _SEH2_VOLATILE NTSTATUS Status = STATUS_SUCCESS;
    ASSERT_EXCLUSIVE_VCB(Vcb);
    _SEH2_TRY {
        UDFAcquireResourceExclusive(&Vcb->DlocResource2, TRUE);
        _SEH2_TRY {
            Target = Owner->NamespaceTarget;
            if (!Target) {
                if (Owner->NamespaceTargetState != UdfNamespaceTargetNone ||
                    !NT_SUCCESS(Owner->NamespaceTargetError))
                    Status = STATUS_FILE_CORRUPT_ERROR;
                _SEH2_LEAVE;
            }
            if (Owner->NamespaceTargetState == UdfNamespaceTargetDraining ||
                Owner->NamespaceTargetState == UdfNamespaceTargetReleasing) {
                ActiveRefusal = TRUE;
                Status = STATUS_DEVICE_BUSY;
                _SEH2_LEAVE;
            }
            if (Owner->NamespaceTargetState == UdfNamespaceTargetFailed) {
                Status = NT_SUCCESS(Owner->NamespaceTargetError) ?
                    STATUS_FILE_CORRUPT_ERROR : Owner->NamespaceTargetError;
                _SEH2_LEAVE;
            }
            Dloc = Target->Dloc;
            if (!Dloc || Dloc->NamespaceScratchOwner != Owner || !Target->RefCount ||
                !Owner->NamespaceScratch || Owner->NamespacePhase != UdfNamespaceCommitted ||
                !Target->FileIdent || !(Target->FileIdent->fileCharacteristics & FILE_DELETED) ||
                (Owner->NamespaceTargetState != UdfNamespaceTargetHeld &&
                 Owner->NamespaceTargetState != UdfNamespaceTargetReady)) {
                Status = STATUS_FILE_CORRUPT_ERROR;
                _SEH2_LEAVE;
            }
            Fcb = Dloc->CommonFcb;
        } _SEH2_FINALLY {
            UDFReleaseResource(&Vcb->DlocResource2);
        } _SEH2_END;
        if (Status != STATUS_SUCCESS || !Target) _SEH2_LEAVE;
        if (Fcb) {
            if (!Fcb->Header.Resource ||
                (ExIsResourceAcquiredSharedLite(Fcb->Header.Resource) &&
                 !ExIsResourceAcquiredExclusiveLite(Fcb->Header.Resource))) {
                Status = STATUS_DEVICE_BUSY; /* do not upgrade a borrowed Main */
                _SEH2_LEAVE;
            }
            if (!UDFAcquireFcbExclusive(IrpContext, Fcb, TRUE)) {
                Status = STATUS_SHARING_VIOLATION;
                _SEH2_LEAVE;
            }
            Acquired = TRUE;
        }
        UDFAcquireResourceExclusive(&Vcb->DlocResource2, TRUE);
        _SEH2_TRY {
            if (Owner->NamespaceTarget != Target || Target->Dloc != Dloc ||
                Dloc->NamespaceScratchOwner != Owner ||
                (Owner->NamespaceTargetState != UdfNamespaceTargetHeld &&
                 Owner->NamespaceTargetState != UdfNamespaceTargetReady)) {
                Status = STATUS_FILE_CORRUPT_ERROR;
                _SEH2_LEAVE;
            }
            Drain = Owner->NamespaceTargetState == UdfNamespaceTargetHeld;
            Owner->NamespaceTargetState = Drain ? UdfNamespaceTargetDraining :
                                                  UdfNamespaceTargetReleasing;
            AttemptOwned = TRUE;
            /* Internal stream cleanup can acquire its base FI. Exclusive
             * VCB/Main and the source's retained target/count protect this
             * existing narrow window. Reentrant retirement is now BUSY. */
            if (Drain) Dloc->NamespaceScratchOwner = NULL;
        } _SEH2_FINALLY {
            UDFReleaseResource(&Vcb->DlocResource2);
        } _SEH2_END;
        if (Status != STATUS_SUCCESS) _SEH2_LEAVE;
        if (Drain) {
            _SEH2_TRY {
                if (!UDFGetFileNameLinkCount(Target) && UDFHasAStreamDir(Target))
                    Status = UDFDeleteAllStreams(IrpContext, Vcb, Target);
                if (Status == STATUS_SUCCESS) {
                    if (!UDFGetFileNameLinkCount(Target)) {
                        Dloc->DeleteCommitted = TRUE;
                        Dloc->FE_Flags |= UDF_FE_FLAG_FREE_DEFERRED;
                    } else {
                        Status = UDFFlushFile__(IrpContext, Vcb, Target, UDF_FLUSH_FLAGS_LITE);
                        if (Status == STATUS_SUCCESS)
                            Status = UDFBarrierFlushDevice(Vcb->TargetDeviceObject);
                    }
                }
            } _SEH2_FINALLY {
                BOOLEAN Abnormal = _SEH2_AbnormalTermination();
                UDFAcquireResourceExclusive(&Vcb->DlocResource2, TRUE);
                _SEH2_TRY {
                    if (Owner->NamespaceTarget != Target ||
                        Owner->NamespaceTargetState != UdfNamespaceTargetDraining ||
                        Dloc->NamespaceScratchOwner) {
                        if (Status == STATUS_SUCCESS) Status = STATUS_FILE_CORRUPT_ERROR;
                    } else {
                        Dloc->NamespaceScratchOwner = Owner;
                        Owner->NamespaceTargetState = Abnormal || Status != STATUS_SUCCESS ?
                            UdfNamespaceTargetFailed : UdfNamespaceTargetReleasing;
                    }
                } _SEH2_FINALLY {
                    UDFReleaseResource(&Vcb->DlocResource2);
                } _SEH2_END;
            } _SEH2_END;
            if (Status != STATUS_SUCCESS) _SEH2_LEAVE;
        }
        /* Acquire once before spending the token. No callback, I/O or second
         * resource acquisition separates the checked decrements from removal
         * of the exact owner/count. A refused decrement keeps a Ready owner. */
        UDFAcquireResourceExclusive(&Vcb->DlocResource2, TRUE);
        _SEH2_TRY {
            if (Owner->NamespaceTarget != Target ||
                Owner->NamespaceTargetState != UdfNamespaceTargetReleasing ||
                Dloc->NamespaceScratchOwner != Owner || !Owner->NamespaceScratch ||
                Vcb->PendingNamespaceCount <= 1 || Vcb->PendingNamespaceCount == MAXLONG) {
                Status = STATUS_FILE_CORRUPT_ERROR;
                _SEH2_LEAVE;
            }
            Status = UDFDereferenceFileLocked(Vcb, Target);
            if (Status != STATUS_SUCCESS) _SEH2_LEAVE;
            Owner->NamespaceTarget = NULL;
            Owner->NamespaceTargetState = UdfNamespaceTargetNone;
            Owner->NamespaceTargetError = STATUS_SUCCESS;
            Dloc->NamespaceScratchOwner = NULL;
            InterlockedDecrement(&Vcb->PendingNamespaceCount);
            AttemptOwned = FALSE; /* generic GC can now free Target */
        } _SEH2_FINALLY {
            UDFReleaseResource(&Vcb->DlocResource2);
        } _SEH2_END;
        if (Status != STATUS_SUCCESS) _SEH2_LEAVE;
        {
            NTSTATUS CleanupStatus;
            if (UDFCleanUpFileWithStatus(Vcb, Target, &CleanupStatus) & UDF_FREE_FILEINFO)
                MyFreePool__(Target);
            else if (CleanupStatus != STATUS_SUCCESS && CleanupStatus != STATUS_DEVICE_BUSY)
                Status = CleanupStatus;
        }
    } _SEH2_FINALLY {
        BOOLEAN Abnormal = _SEH2_AbnormalTermination();
        NTSTATUS Error = Abnormal ? STATUS_UNSUCCESSFUL : Status;
        if (Error != STATUS_SUCCESS && NT_SUCCESS(Error)) Error = STATUS_IO_DEVICE_ERROR;
        _SEH2_TRY {
            if (AttemptOwned || (Target && Error != STATUS_SUCCESS && !ActiveRefusal)) {
                UDFAcquireResourceExclusive(&Vcb->DlocResource2, TRUE);
                _SEH2_TRY {
                    if (Owner->NamespaceTarget == Target) {
                        if (AttemptOwned) {
                            if (!Dloc->NamespaceScratchOwner)
                                Dloc->NamespaceScratchOwner = Owner;
                            if (Dloc->NamespaceScratchOwner != Owner) {
                                Owner->NamespaceTargetState = UdfNamespaceTargetFailed;
                                if (Error == STATUS_SUCCESS) Error = STATUS_FILE_CORRUPT_ERROR;
                            } else if (Owner->NamespaceTargetState == UdfNamespaceTargetReleasing) {
                                Owner->NamespaceTargetState = UdfNamespaceTargetReady;
                            } else {
                                Owner->NamespaceTargetState = UdfNamespaceTargetFailed;
                            }
                        }
                        if (NT_SUCCESS(Owner->NamespaceTargetError))
                            Owner->NamespaceTargetError = Error == STATUS_SUCCESS ?
                                STATUS_FILE_CORRUPT_ERROR : Error;
                    } else if (AttemptOwned && Error == STATUS_SUCCESS) {
                        Error = STATUS_FILE_CORRUPT_ERROR;
                    }
                } _SEH2_FINALLY {
                    UDFReleaseResource(&Vcb->DlocResource2);
                } _SEH2_END;
            }
            if (Error != STATUS_SUCCESS && !ActiveRefusal)
                UDFQuarantineNamespace(Vcb, Error);
            if (!Abnormal && Error != STATUS_SUCCESS && NT_SUCCESS(Status))
                Status = Error;
        } _SEH2_FINALLY {
            if (Acquired) UDFReleaseFcb(IrpContext, Fcb);
        } _SEH2_END;
    } _SEH2_END;
    return Status;
}

/* A freshly created private FE has one sector and independently owned map
 * buffers. A general cached file/AED/stream is not eligible for this memory
 * destructor, even if some of its ordinary reference counts reached zero. */
static BOOLEAN
UDFScratchMapIsIcbSector(PVCB Vcb, PEXTENT_MAP Map, uint32 Lba)
{
    uint32 Bytes;
    return Map && UDFGetMappingByteCount(Map, &Bytes) == STATUS_SUCCESS &&
        Bytes == 2 * sizeof(EXTENT_MAP) && Map[0].extLocation == Lba &&
        (Map[0].extLength & UDF_EXTENT_LENGTH_MASK) == Vcb->SectorSize &&
        (Map[0].extLength >> 30) <= EXTENT_NOT_RECORDED_ALLOCATED &&
        !Map[1].extLength && !Map[1].extLocation;
}

static NTSTATUS
UDFCheckNamespaceScratch(PVCB Vcb, PUDF_DATALOC_INFO Owner,
                         PUDF_FILE_INFO Scratch)
{
    PUDF_DATALOC_INFO Dloc = Scratch ? Scratch->Dloc : NULL;
    PDIR_INDEX_ITEM Entry;
    uint32 Lba, Header;
    if (!Dloc || Dloc == Owner || Dloc->NamespaceScratchOwner != Owner ||
        Scratch->FileIdent || Scratch->Fcb || Scratch->ListPtr || Scratch->OpenCount ||
        Scratch->RefCount != 1 || Dloc->LinkRefCount != 1 || Dloc->CommonFcb ||
        !Scratch->ParentFile || !Scratch->ParentFile->Dloc || !Scratch->ParentFile->OpenCount ||
        Scratch->ParentFile->OpenCount > MAXLONG ||
        Scratch->NextLinkedFile != Scratch || Scratch->PrevLinkedFile != Scratch ||
        Dloc->LinkedFileInfo != Scratch || Scratch->Constructing || Scratch->DlocInitOwner ||
        Scratch->ConstructionFailed || Scratch->ConstructionReferenceOwned ||
        Scratch->ConstructionParent || Scratch->CreateConstructionParent ||
        Scratch->CreateConstructionCounted || Scratch->CreateConstructionOpenOwned ||
        Scratch->CreateRollbackCounted || Scratch->CreateRollbackPhase ||
        Scratch->RetainedOpenReferences || Scratch->InternalOpenReleased ||
        Scratch->TemporaryRetired || Scratch->NamespaceStaged ||
        Dloc->CleanupOwner || Dloc->AllocationReleaseActive ||
        !NT_SUCCESS(Dloc->AllocationReleaseError) || Dloc->CreateConstructionOwner || Dloc->CreateRollbackOwner ||
        Dloc->RetiredExtents || Dloc->DeletePrefree || Dloc->DeleteCommitted ||
        Dloc->NamespaceScratch || Dloc->NamespaceFileInfo || Dloc->NamespaceTarget ||
        Dloc->NamespacePhase != UdfNamespaceNone ||
        Dloc->NamespaceTargetState != UdfNamespaceTargetNone ||
        !NT_SUCCESS(Dloc->NamespaceTargetError) || Dloc->StreamDeleteOwner ||
        Dloc->StreamDeleteContextOwner || Dloc->StreamCreate ||
        Dloc->DirIndex || Dloc->SDirInfo || Dloc->DirectoryAppend || Dloc->DirectoryFECheckpoint ||
        !NT_SUCCESS(Dloc->ZeroDataError) || !NT_SUCCESS(Dloc->CacheSizeError) ||
        Dloc->ZeroDataAttempts || Dloc->ZeroDataCanRetry ||
        Dloc->DataLoc.Length || Dloc->DataLoc.Offset > Vcb->SectorSize ||
        Dloc->AllocLoc.Length < 0 || Dloc->AllocLoc.Offset > Vcb->SectorSize ||
        (uint64)Dloc->AllocLoc.Length > Vcb->SectorSize - Dloc->AllocLoc.Offset ||
        Dloc->FELoc.Offset || Dloc->FELoc.Length <= 0 || Dloc->FELoc.Length > Vcb->SectorSize ||
        !Dloc->FELoc.Mapping || !Dloc->FileEntry || Dloc->FileEntryCapacity < Vcb->SectorSize ||
        (Dloc->FE_Flags & (UDF_FE_FLAG_UNDER_INIT | UDF_FE_FLAG_FREE_DEFERRED |
            UDF_FE_FLAG_CACHED_DATA_PENDING | UDF_FE_FLAG_HAS_SDIR | UDF_FE_FLAG_IS_SDIR |
            UDF_FE_FLAG_HAS_DEL_SDIR | UDF_FE_FLAG_IS_DEL_SDIR)))
        return STATUS_FILE_CORRUPT_ERROR;
    Header = Dloc->FileEntry->tagIdent == TID_FILE_ENTRY ? sizeof(FILE_ENTRY) :
        Dloc->FileEntry->tagIdent == TID_EXTENDED_FILE_ENTRY ? sizeof(EXTENDED_FILE_ENTRY) : 0;
    Lba = Dloc->FELoc.Mapping[0].extLocation;
    if (!Header || Dloc->FileEntryLen < Header || Dloc->FileEntryLen > Vcb->SectorSize ||
        !Lba || Lba == LBA_OUT_OF_EXTENT || UDFGetFileSize(Scratch) ||
        (((icbtag*)(Dloc->FileEntry + 1))->flags & ICB_FLAG_ALLOC_MASK) != ICB_FLAG_AD_IN_ICB ||
        !UDFScratchMapIsIcbSector(Vcb, Dloc->FELoc.Mapping, Lba) ||
        !UDFScratchMapIsIcbSector(Vcb, Dloc->DataLoc.Mapping, Lba) ||
        (Dloc->AllocLoc.Mapping && !UDFScratchMapIsIcbSector(Vcb, Dloc->AllocLoc.Mapping, Lba)) ||
        Dloc->DataLoc.Mapping == Dloc->FELoc.Mapping ||
        (Dloc->AllocLoc.Mapping && (Dloc->AllocLoc.Mapping == Dloc->FELoc.Mapping ||
                                   Dloc->AllocLoc.Mapping == Dloc->DataLoc.Mapping)) ||
        (PVOID)Dloc->FileEntry == (PVOID)Dloc->FELoc.Mapping ||
        (PVOID)Dloc->FileEntry == (PVOID)Dloc->DataLoc.Mapping ||
        (PVOID)Dloc->FileEntry == (PVOID)Dloc->AllocLoc.Mapping)
        return STATUS_FILE_CORRUPT_ERROR;
    if (Scratch->Index >= 2) {
        Entry = UDFDirIndex(Scratch->ParentFile->Dloc->DirIndex, Scratch->Index);
        if (!Entry || Entry->FileInfo == Scratch)
            return STATUS_FILE_CORRUPT_ERROR; /* the namespace must already be detached */
    }
    return STATUS_SUCCESS;
}

/* Media absence is confirmed before entry. Move the exact FE allocation to
 * the existing source retirement ledger, retire the actual FI/index once,
 * and dispose only private memory. Never restore through generic partial GC. */
static NTSTATUS
UDFRetireNamespaceScratch(PIRP_CONTEXT IrpContext, PVCB Vcb,
                          PUDF_FILE_INFO Source)
{
    PUDF_DATALOC_INFO Owner = Source ? Source->Dloc : NULL;
    _SEH2_VOLATILE PUDF_FILE_INFO Scratch = NULL;
    _SEH2_VOLATILE PUDF_DATALOC_INFO Dloc = NULL;
    _SEH2_VOLATILE PUDF_EXTENT_RETIREMENT Retired = NULL;
    PUDF_EXTENT_RETIREMENT Prior;
    _SEH2_VOLATILE EXTENT_INFO Replacement = {0};
    _SEH2_VOLATILE BOOLEAN BitmapLocked = FALSE, IndexLocked = FALSE, ReferencesLocked = FALSE;
    _SEH2_VOLATILE BOOLEAN Detached = FALSE;
    _SEH2_VOLATILE NTSTATUS Status = STATUS_SUCCESS;
    LONG Slot, Remaining;
    uint32 Part, i, Lba, Bytes;
    ASSERT_EXCLUSIVE_VCB(Vcb);
    if (!Owner) return STATUS_FILE_INVALID;
    _SEH2_TRY {
    _SEH2_TRY {
        if (Owner->NamespaceTarget || Owner->NamespaceTargetState != UdfNamespaceTargetNone ||
            !NT_SUCCESS(Owner->NamespaceTargetError)) {
            Status = STATUS_INVALID_DEVICE_STATE;
            _SEH2_LEAVE;
        }
        Scratch = Owner->NamespaceScratch;
        if (!Scratch) {
            if (Owner->NamespaceFileInfo || Owner->NamespacePhase != UdfNamespaceNone) {
                Status = STATUS_FILE_CORRUPT_ERROR;
                _SEH2_LEAVE;
            }
            if (!Owner->RetiredExtents) _SEH2_LEAVE; /* no work or lower acquisition */
        }
        /* Actual callers own VCB/Main and have returned from their flushes.
         * Refuse an inherited lower claim: the later bitmap backend can do
         * I/O and must not run while a borrowed Dloc resource remains held.
         * Do not upgrade or release any acquisition owned by the caller. */
        if (ExIsResourceAcquiredSharedLite(&Vcb->BitMapResource1) ||
            ExIsResourceAcquiredExclusiveLite(&Vcb->BitMapResource1) ||
            ExIsResourceAcquiredSharedLite(&Vcb->DlocResource) ||
            ExIsResourceAcquiredExclusiveLite(&Vcb->DlocResource) ||
            ExIsResourceAcquiredSharedLite(&Vcb->DlocResource2) ||
            ExIsResourceAcquiredExclusiveLite(&Vcb->DlocResource2)) {
            Status = STATUS_DEVICE_BUSY;
            _SEH2_LEAVE;
        }
        if (!Scratch) {
            /* A prior memory retirement may have left only its exact range
             * receipt. Retrying that backend never decrements another FI. */
            Status = UDFAllocationFailureStatus(Vcb);
            if (Status == STATUS_SUCCESS)
                Status = FlagOn(Vcb->VcbState, VCB_STATE_VOLUME_READ_ONLY) ?
                    STATUS_MEDIA_WRITE_PROTECTED : UDFCommitExtentRetirements(IrpContext, Vcb, Source);
            _SEH2_LEAVE;
        }
        if (Owner->NamespacePhase != UdfNamespaceCommitted &&
            Owner->NamespacePhase != UdfNamespaceAbortCommitted) {
            Status = STATUS_INVALID_DEVICE_STATE;
            _SEH2_LEAVE;
        }
        Status = UDFCheckNamespaceScratch(Vcb, Owner, Scratch);
        if (Status != STATUS_SUCCESS) _SEH2_LEAVE;
        Dloc = Scratch->Dloc;
        Lba = Dloc->FELoc.Mapping[0].extLocation;
        Part = UDFGetRefPartNumByPhysLba(Vcb, Lba);
        Status = UDFPrepareExtentShrink(Vcb, Part, 0, FALSE, &Dloc->FELoc,
            (PEXTENT_INFO)&Replacement, (PUDF_EXTENT_RETIREMENT*)&Retired);
        if (Status != STATUS_SUCCESS) _SEH2_LEAVE;
        if (!Retired || Retired->Count != 1 || Retired->Ranges[0].extLocation != Lba ||
            (Retired->Ranges[0].extLength & UDF_EXTENT_LENGTH_MASK) != Vcb->SectorSize) {
            Status = STATUS_FILE_CORRUPT_ERROR;
            _SEH2_LEAVE;
        }
        _SEH2_TRY {
            if (!UDFAcquireResourceExclusive(&Vcb->BitMapResource1, TRUE)) {
                Status = STATUS_DEVICE_BUSY;
                _SEH2_LEAVE;
            }
            BitmapLocked = TRUE;
            if (!UDFAcquireResourceExclusive(&Vcb->DlocResource, TRUE)) {
                Status = STATUS_DEVICE_BUSY;
                _SEH2_LEAVE;
            }
            IndexLocked = TRUE;
            if (!UDFAcquireResourceExclusive(&Vcb->DlocResource2, TRUE)) {
                Status = STATUS_DEVICE_BUSY;
                _SEH2_LEAVE;
            }
            ReferencesLocked = TRUE;
            Status = UDFCheckNamespaceScratch(Vcb, Owner, Scratch);
            Slot = UDFFindDlocInMem(Vcb, Dloc);
            if (Status != STATUS_SUCCESS) _SEH2_LEAVE;
            if (Owner->NamespaceScratch != Scratch || Slot < 0 ||
                Vcb->DlocList[Slot].Lba != Lba || Vcb->PendingNamespaceCount <= 0 ||
                Vcb->PendingNamespaceCount == MAXLONG || Vcb->PendingExtentRetirements < 0) {
                Status = STATUS_FILE_CORRUPT_ERROR;
                _SEH2_LEAVE;
            }
            if (Vcb->PendingExtentRetirements >= MAXLONG - 1) {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                _SEH2_LEAVE;
            }
            Remaining = Vcb->PendingExtentRetirements;
            for (Prior = Owner->RetiredExtents; Prior; Prior = Prior->Next) {
                if (Remaining-- <= 0 || !Prior->Count || Prior->Count > Prior->Capacity ||
                    Prior->Completed > Prior->Count) {
                    Status = STATUS_FILE_CORRUPT_ERROR;
                    _SEH2_LEAVE;
                }
                for (i = Prior->Completed; i < Prior->Count; i++) {
                    Bytes = Prior->Ranges[i].extLength & UDF_EXTENT_LENGTH_MASK;
                    if ((uint64)Lba >= Prior->Ranges[i].extLocation &&
                        (uint64)Lba < (uint64)Prior->Ranges[i].extLocation +
                                       (((uint64)Bytes + Vcb->SectorSize - 1) >> Vcb->SectorShift)) {
                        Status = STATUS_FILE_CORRUPT_ERROR;
                        _SEH2_LEAVE;
                    }
                }
            }
            Status = UDFDereferenceFileLocked(Vcb, Scratch);
            if (Status != STATUS_SUCCESS) _SEH2_LEAVE;
            /* No fallible call after that consume: the exact allocation
             * owner/count is published before the namespace count retires. */
            Retired->Next = Owner->RetiredExtents;
            Owner->RetiredExtents = Retired;
            Retired = NULL;
            InterlockedIncrement(&Vcb->PendingExtentRetirements);
            RtlZeroMemory(&Vcb->DlocList[Slot], sizeof(Vcb->DlocList[Slot]));
            Vcb->DlocFreeHint = min(Vcb->DlocFreeHint, (ULONG)Slot);
            Owner->NamespaceScratch = NULL;
            Owner->NamespaceFileInfo = NULL;
            Owner->NamespacePhase = UdfNamespaceNone;
            Dloc->NamespaceScratchOwner = NULL;
            Scratch->Dloc = NULL;
            Scratch->ParentFile = NULL;
            Scratch->TemporaryRetired = TRUE;
            InterlockedDecrement(&Vcb->PendingNamespaceCount);
            Detached = TRUE;
        } _SEH2_FINALLY {
            if (ReferencesLocked) UDFReleaseResource(&Vcb->DlocResource2);
            if (IndexLocked) UDFReleaseResource(&Vcb->DlocResource);
            if (BitmapLocked) UDFReleaseResource(&Vcb->BitMapResource1);
        } _SEH2_END;
        if (Status != STATUS_SUCCESS) _SEH2_LEAVE;
        if (Detached) {
            /* Preflight proved distinct buffers and no other context owner.
             * Neither generic GC nor the bitmap backend sees this Dloc again. */
            MyFreePool__(Dloc->DataLoc.Mapping);
            if (Dloc->AllocLoc.Mapping) MyFreePool__(Dloc->AllocLoc.Mapping);
            MyFreePool__(Dloc->FELoc.Mapping);
            MyFreePool__(Dloc->FileEntry);
            MyFreePool__(Dloc);
            Dloc = NULL;
            MyFreePool__(Scratch);
            Scratch = NULL;
            Status = UDFAllocationFailureStatus(Vcb);
            if (Status == STATUS_SUCCESS)
                Status = FlagOn(Vcb->VcbState, VCB_STATE_VOLUME_READ_ONLY) ?
                    STATUS_MEDIA_WRITE_PROTECTED : UDFCommitExtentRetirements(IrpContext, Vcb, Source);
        }
    } _SEH2_EXCEPT(FsRtlIsNtstatusExpected(_SEH2_GetExceptionCode()) ?
                  EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        Status = _SEH2_GetExceptionCode();
    } _SEH2_END;
    } _SEH2_FINALLY {
        if (Replacement.Mapping) MyFreePool__(Replacement.Mapping);
        if (Retired) MyFreePool__(Retired); /* private, never published */
        if (_SEH2_AbnormalTermination())
            UDFQuarantineNamespace(Vcb, STATUS_UNSUCCESSFUL);
        else if (Status != STATUS_SUCCESS) {
            if (NT_SUCCESS(Status)) Status = STATUS_IO_DEVICE_ERROR;
            UDFQuarantineNamespace(Vcb, Status);
        }
    } _SEH2_END;
    return Status;
}

/* Rename/link conversion may retire only its own private, freshly created
 * FE generation. Other aliases, opens or cache owners forbid that shortcut. */
static BOOLEAN
UDFPrivateTemporaryFileInfo(PUDF_FILE_INFO FileInfo)
{
    return FileInfo && FileInfo->Dloc && !FileInfo->Constructing &&
        !FileInfo->ConstructionFailed && FileInfo->RefCount == 1 &&
        !FileInfo->OpenCount && !FileInfo->Fcb &&
        FileInfo->NextLinkedFile == FileInfo &&
        FileInfo->Dloc->LinkRefCount == 1 && !FileInfo->Dloc->CommonFcb &&
        !FileInfo->Dloc->CleanupOwner && !FileInfo->Dloc->CreateConstructionOwner &&
        !FileInfo->Dloc->CreateRollbackOwner && !FileInfo->CreateConstructionParent &&
        !FileInfo->Dloc->NamespaceScratch &&
        (!FileInfo->Dloc->NamespaceScratchOwner ||
         (FileInfo->NamespaceStaged &&
          FileInfo->Dloc->NamespaceScratchOwner->NamespaceScratch == FileInfo));
}

/* A staged FID is always deleted until its final ICB/name is ready.
 * Persist its complete directory span before attempting target unlink;
 * the stage's FE/parent reference remains held by Source.Dloc on error. */
static NTSTATUS
UDFPersistNamespaceStage(PIRP_CONTEXT IrpContext, PVCB Vcb,
                         PUDF_FILE_INFO Stage)
{
    NTSTATUS Status;
    if (!Stage || !Stage->NamespaceStaged || !Stage->ParentFile || !Stage->FileIdent)
        return STATUS_FILE_CORRUPT_ERROR;
    Status = UDFFlushFile__(IrpContext, Vcb, Stage, UDF_FLUSH_FLAGS_LITE);
    if (NT_SUCCESS(Status))
        Status = UDFFlushFile__(IrpContext, Vcb, Stage->ParentFile, UDF_FLUSH_FLAGS_LITE);
    if (NT_SUCCESS(Status))
        Status = UDFBarrierFlushDevice(Vcb->TargetDeviceObject);
    return Status;
}

static NTSTATUS
UDFPublishNamespaceStage(PVCB Vcb, PUDF_FILE_INFO Stage, uint8 Characteristics)
{
    PDIR_INDEX_HDR Header;
    PDIR_INDEX_ITEM Entry;
    if (!Stage || !Stage->NamespaceStaged || !Stage->ParentFile || !Stage->FileIdent ||
        (Characteristics & FILE_DELETED))
        return UDFQuarantineNamespace(Vcb, STATUS_FILE_CORRUPT_ERROR);
    Header = Stage->ParentFile->Dloc->DirIndex;
    Entry = Header ? UDFDirIndex(Header, Stage->Index) : NULL;
    if (!Entry || Entry->FileInfo != Stage || !Header->DelCount ||
        !(Entry->FileCharacteristics & FILE_DELETED) ||
        !(Stage->FileIdent->fileCharacteristics & FILE_DELETED))
        return UDFQuarantineNamespace(Vcb, STATUS_FILE_CORRUPT_ERROR);
    Entry->FileCharacteristics = Characteristics;
    Stage->FileIdent->fileCharacteristics = Characteristics;
    Entry->FI_Flags |= UDF_FI_FLAG_FI_MODIFIED;
    Stage->NamespaceStaged = FALSE;
    Header->DelCount--;
    if (!UDFIsAStreamDir(Stage->ParentFile)) {
        UDFChangeFileCounter(Vcb, !(Characteristics & FILE_DIRECTORY), TRUE);
        if (Characteristics & FILE_DIRECTORY)
            UDFIncFileLinkCount(Stage->ParentFile);
    }
    return STATUS_SUCCESS;
}

/* The submitted FID may or may not be on disk. Until its confirming
 * barrier, hide its private scratch FI from name lookup/enumeration while
 * retaining every allocation and reference. This is not an on-media undo. */
static VOID
UDFHideUncertainNamespaceStage(PVCB Vcb, PUDF_FILE_INFO Stage)
{
    PDIR_INDEX_HDR Header = Stage->ParentFile->Dloc->DirIndex;
    PDIR_INDEX_ITEM Entry = UDFDirIndex(Header, Stage->Index);
    if (!Stage->NamespaceStaged && Entry && Entry->FileInfo == Stage) {
        uint8 Characteristics = Entry->FileCharacteristics;
        Entry->FileCharacteristics |= FILE_DELETED;
        Stage->FileIdent->fileCharacteristics |= FILE_DELETED;
        Stage->NamespaceStaged = TRUE;
        Header->DelCount++;
        if (!UDFIsAStreamDir(Stage->ParentFile)) {
            UDFChangeFileCounter(Vcb, !(Characteristics & FILE_DIRECTORY), FALSE);
            if (Characteristics & FILE_DIRECTORY)
                UDFDecFileLinkCount(Stage->ParentFile);
        }
    }
}

/* Prepare the surviving source FE, then confirm its destination FID
 * before the caller is allowed to tombstone the original source name.
 * Convert exceptions to the same retained first-error path as NTSTATUS. */
static NTSTATUS
UDFCommitRenameDestination(PIRP_CONTEXT IrpContext, PVCB Vcb,
                           PUDF_FILE_INFO Source, PUDF_FILE_INFO Stage)
{
    PDIR_INDEX_ITEM SourceEntry, Entry;
    NTSTATUS Status = STATUS_SUCCESS;
    uint32 PartNum;
    SourceEntry = UDFDirIndex(Source->ParentFile->Dloc->DirIndex, Source->Index);
    Entry = UDFDirIndex(Stage->ParentFile->Dloc->DirIndex, Stage->Index);
    PartNum = UDFGetRefPartNumByPhysLba(Vcb, Source->Dloc->FELoc.Mapping[0].extLocation);
    if (!SourceEntry || !Entry || !Source->FileIdent || !Stage->FileIdent ||
        PartNum == (uint32)-1)
        return UDFQuarantineNamespace(Vcb, STATUS_FILE_CORRUPT_ERROR);
    _SEH2_TRY {
        Stage->FileIdent->icb = Source->FileIdent->icb;
        Stage->FileIdent->fileVersionNum = Source->FileIdent->fileVersionNum;
        Entry->FileEntryLoc = SourceEntry->FileEntryLoc;
        Source->Dloc->NamespacePhase = UdfNamespaceSourcePreparing;
        UDFIncFileLinkCount(Source);
        Status = UDFFlushFE(IrpContext, Vcb, Source, PartNum);
        if (NT_SUCCESS(Status))
            Status = UDFPublishNamespaceStage(Vcb, Stage,
                SourceEntry->FileCharacteristics & ~FILE_DELETED);
        if (NT_SUCCESS(Status)) {
            Source->Dloc->NamespacePhase = UdfNamespaceDestinationSubmitted;
            Status = UDFFlushFI(IrpContext, Vcb, Stage, PartNum);
        }
        if (NT_SUCCESS(Status))
            Status = UDFFlushFile__(IrpContext, Vcb, Stage->ParentFile, UDF_FLUSH_FLAGS_LITE);
        if (NT_SUCCESS(Status))
            Status = UDFBarrierFlushDevice(Vcb->TargetDeviceObject);
    } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
        Status = _SEH2_GetExceptionCode();
    } _SEH2_END;
    if (!NT_SUCCESS(Status)) {
        UDFHideUncertainNamespaceStage(Vcb, Stage);
        return UDFQuarantineNamespace(Vcb, Status);
    }
    Source->Dloc->NamespacePhase = UdfNamespaceDestinationCommitted;
    return STATUS_SUCCESS;
}

static NTSTATUS
UDFAbortNamespaceStage(PIRP_CONTEXT IrpContext, PVCB Vcb,
                       PUDF_FILE_INFO Source)
{
    PUDF_FILE_INFO Stage = Source->Dloc->NamespaceScratch;
    PDIR_INDEX_ITEM Entry;
    NTSTATUS Status;
    if (!Stage || !Stage->NamespaceStaged || !Stage->FileIdent || !Stage->ParentFile ||
        Stage->Dloc->NamespaceScratchOwner != Source->Dloc)
        return UDFQuarantineNamespace(Vcb, STATUS_FILE_CORRUPT_ERROR);
    Entry = UDFDirIndex(Stage->ParentFile->Dloc->DirIndex, Stage->Index);
    if (!Entry || Entry->FileInfo != Stage || !(Entry->FileCharacteristics & FILE_DELETED) ||
        !(Stage->FileIdent->fileCharacteristics & FILE_DELETED))
        return UDFQuarantineNamespace(Vcb, STATUS_FILE_CORRUPT_ERROR);
    /* A successful retry must leave a real tombstone, not an unwritten
     * hole in the enlarged directory. It is then safe to retire its FE. */
    Status = UDFPersistNamespaceStage(IrpContext, Vcb, Stage);
    if (Status != STATUS_SUCCESS)
        return UDFQuarantineNamespace(Vcb, NT_SUCCESS(Status) ? STATUS_IO_DEVICE_ERROR : Status);
    Source->Dloc->NamespacePhase = UdfNamespaceAbortCommitted;
    Entry->FileInfo = NULL;
    MyFreePool__(Stage->FileIdent);
    Stage->FileIdent = NULL;
    Stage->NamespaceStaged = FALSE;
    return UDFRetireNamespaceScratch(IrpContext, Vcb, Source);
}

/* Keep incomplete construction distinct from a completed ordinary open.
 * Neither path manufactures an FI reference or drops an early reader pin.
 * UNDER_INIT remains set on an incomplete FE. The enclosing exclusive VCB
 * and parent FCB resources protect this non-I/O ownership handoff. */
static VOID
UDFRetainFailedNamespaceStage(PVCB Vcb, PUDF_FILE_INFO Source,
                            PUDF_FILE_INFO Directory, PUDF_FILE_INFO Stage,
                            BOOLEAN HardLink, NTSTATUS Reason)
{
    NTSTATUS Status;
    if (Stage->CreateConstructionParent ||
        (Stage->Dloc && Stage->Dloc->CreateConstructionOwner)) {
        /* The constructor owner already holds its exact generation and
         * close barrier (a parent token only after binding). Do not invent
         * a successful FI open or install a second scratch owner. */
        UDFQuarantineCreateConstruction(Vcb, Stage, Reason);
        return;
    }
    if (Stage->Dloc->NamespaceScratchOwner) {
        /* The scratch admission may have finished before its caller set
         * StageOwned. Never increment its close count or overwrite it. */
        UDFQuarantineNamespace(Vcb, Reason);
        return;
    }
    if (Stage->Constructing || Stage->ConstructionReferenceOwned ||
        !Stage->RefCount || Stage->ConstructionFailed) {
        UDFRetainFailedCreateConstruction(Vcb, Stage, Directory, Reason);
        return;
    }
    Status = UDFOwnNamespaceScratch(Vcb, Source, HardLink ? Stage : Source, Stage);
    if (Status != STATUS_SUCCESS) {
        UDFRetainFailedCreateConstruction(Vcb, Stage, Directory, Reason);
        return;
    }
    Stage->ConstructionFailed = TRUE;
    Stage->CleanupPending = TRUE;
    UDFQuarantineNamespace(Vcb, Reason);
}

/* Prepare a hidden destination before removing an existing target. All
 * create-time FE/FID/name/index/mapping allocations precede that removal.
 * This does not make arbitrary later I/O/serialization failures atomic. */
static NTSTATUS
UDFStageNamespaceDestination(PIRP_CONTEXT IrpContext, PVCB Vcb,
    BOOLEAN IgnoreCase, BOOLEAN Replace, BOOLEAN HardLink,
    PUNICODE_STRING Name, PUDF_FILE_INFO Directory, PUDF_FILE_INFO Source,
    uint_di ExcludedSourceIndex, PUDF_FILE_INFO* Result, PBOOLEAN TargetRemoved)
{
    _SEH2_VOLATILE PUDF_FILE_INFO Target = NULL, Stage = NULL, TargetOpen = NULL;
    _SEH2_VOLATILE PFCB TargetFcb = NULL;
    _SEH2_VOLATILE BOOLEAN TargetAcquired = FALSE;
    _SEH2_VOLATILE BOOLEAN StageOwned = FALSE, TargetOwned = FALSE;
    _SEH2_VOLATILE NTSTATUS Status = STATUS_SUCCESS;
    NTSTATUS CloseStatus;
    uint_di Index = 0, ExcludedIndex = ExcludedSourceIndex;
    *Result = NULL;
    *TargetRemoved = FALSE;
    if (!Name || !Name->Buffer || !Name->Length || Name->Length % sizeof(WCHAR) ||
        Name->Length > MAXUSHORT - sizeof(WCHAR))
        return STATUS_OBJECT_NAME_INVALID;
    _SEH2_TRY {
        Status = UDFFindFile(Vcb, IgnoreCase, TRUE, Name, Directory, &Index);
        if (NT_SUCCESS(Status) && Index != ExcludedSourceIndex) {
            if (!Replace || Index < 2) {
                Status = STATUS_OBJECT_NAME_COLLISION;
                _SEH2_LEAVE;
            }
            Status = UDFOpenFile__(IrpContext, Vcb, IgnoreCase, TRUE, Name,
                                    Directory, (PUDF_FILE_INFO*)&Target, &Index);
            if (!NT_SUCCESS(Status))
                _SEH2_LEAVE;
            TargetOpen = Target; /* exact successful named-open token */
            if (Target->Dloc == Source->Dloc) {
                Status = HardLink ? STATUS_SUCCESS : STATUS_OBJECT_NAME_COLLISION;
                _SEH2_LEAVE;
            }
            TargetFcb = Target->Fcb;
            /* A parentless canonical by-ID alias is not a substitute for
             * ownership of this namespace link. Keep that case fail closed. */
            if (Target->Dloc->CommonFcb && Target->Dloc->CommonFcb != TargetFcb) {
                Status = STATUS_SHARING_VIOLATION;
                _SEH2_LEAVE;
            }
            if (TargetFcb) {
                if (!UDFAcquireFcbExclusive(IrpContext, TargetFcb, TRUE)) {
                    Status = STATUS_SHARING_VIOLATION;
                    _SEH2_LEAVE;
                }
                TargetAcquired = TRUE;
                if (TargetFcb->TeardownInProgress || !TargetFcb->FileInfo ||
                    TargetFcb->FileInfo->Dloc != Target->Dloc) {
                    Status = STATUS_FILE_INVALID;
                    _SEH2_LEAVE;
                }
            }
            if (TargetFcb && UDFIsLinkDeletePending(TargetFcb, Target)) {
                Status = STATUS_DELETE_PENDING;
                _SEH2_LEAVE;
            }
            if (TargetFcb && TargetFcb->ShareAccess.SharedDelete < TargetFcb->ShareAccess.OpenCount) {
                Status = STATUS_SHARING_VIOLATION;
                _SEH2_LEAVE;
            }
            Status = UDFDoesOSAllowFileToBeTargetForRename__(Target);
            if (!NT_SUCCESS(Status))
                _SEH2_LEAVE;
            ExcludedIndex = Index;
        } else if (!NT_SUCCESS(Status) && Status != STATUS_OBJECT_NAME_NOT_FOUND) {
            _SEH2_LEAVE;
        }
        Status = UDFCreateFileInternal(IrpContext, Vcb, IgnoreCase, Name,
            UDFGetFileEALength(Source), 0,
            Source->Dloc->FileEntry->tagIdent == TID_EXTENDED_FILE_ENTRY,
            TRUE, Directory, (PUDF_FILE_INFO*)&Stage, ExcludedIndex, TRUE, NULL);
        if (!NT_SUCCESS(Status))
            _SEH2_LEAVE;
        Status = UDFOwnNamespaceScratch(Vcb, Source, HardLink ? Stage : Source, Stage);
        if (Status != STATUS_SUCCESS) _SEH2_LEAVE;
        StageOwned = TRUE;
        if (!UDFPrivateTemporaryFileInfo(Stage)) {
            Status = UDFQuarantineNamespace(Vcb, STATUS_SHARING_VIOLATION);
            _SEH2_LEAVE;
        }
        Status = UDFPersistNamespaceStage(IrpContext, Vcb, Stage);
        if (!NT_SUCCESS(Status)) {
            Status = UDFQuarantineNamespace(Vcb, Status);
            _SEH2_LEAVE;
        }
        if (Target) {
            uint32 PartNum;
            /* A refused replacement leaves streams and their live owners
             * intact. Data/cache and FE writeback precede any namespace edit. */
            if (UDFStreamDirectoryHasLiveHandles(UDFHasAStreamDir(Target) ?
                                                Target->Dloc->SDirInfo : NULL)) {
                Status = STATUS_CANNOT_DELETE;
                _SEH2_LEAVE;
            }
            if (TargetFcb && TargetFcb->FcbNonpaged->SegmentObject.DataSectionObject) {
                IO_STATUS_BLOCK IoStatus;
                IoStatus.Status = STATUS_SUCCESS;
                IoStatus.Information = 0;
                CcFlushCache(&TargetFcb->FcbNonpaged->SegmentObject, NULL, 0, &IoStatus);
                Status = IoStatus.Status;
                if (!NT_SUCCESS(Status)) _SEH2_LEAVE;
                Target->Dloc->FE_Flags &= ~UDF_FE_FLAG_CACHED_DATA_PENDING;
            }
            Status = UDFFlushFile__(IrpContext, Vcb, Target, UDF_FLUSH_FLAGS_LITE);
            if (!NT_SUCCESS(Status)) _SEH2_LEAVE;
            Status = UDFOwnNamespaceTarget(Vcb, Source, Target,
                (PUDF_FILE_INFO*)&TargetOpen, (PBOOLEAN)&TargetOwned);
            if (!NT_SUCCESS(Status)) _SEH2_LEAVE;
            /* FALSE detaches only the name. The retained FI still reserves
             * every old FE/data block; no bitmap bit becomes reusable yet. */
            Status = UDFUnlinkFile__(IrpContext, Vcb, Target, FALSE);
            if (NT_SUCCESS(Status)) {
                *TargetRemoved = TRUE;
                Source->Dloc->NamespacePhase = UdfNamespaceTargetDetached;
                if (TargetFcb && !UDFGetFileNameLinkCount(Target))
                    TargetFcb->FcbState |= UDF_FCB_DELETED;
                PartNum = UDFGetRefPartNumByPhysLba(Vcb,
                    Target->Dloc->FELoc.Mapping[0].extLocation);
                Status = PartNum == (uint32)-1 ? STATUS_FILE_CORRUPT_ERROR :
                    UDFFlushFI(IrpContext, Vcb, Target, PartNum);
                if (NT_SUCCESS(Status))
                    Status = UDFBarrierFlushDevice(Vcb->TargetDeviceObject);
                if (NT_SUCCESS(Status)) {
                    Source->Dloc->NamespacePhase = UdfNamespaceTargetTombstoneCommitted;
                    UDFNoteCommittedUnlink(Vcb, TargetFcb, Target);
                }
            }
            if (!NT_SUCCESS(Status))
                Status = UDFQuarantineNamespace(Vcb, Status);
        }
    } _SEH2_FINALLY {
        BOOLEAN Abnormal = _SEH2_AbnormalTermination();
        _SEH2_TRY {
            if (Stage && Stage->Dloc && !StageOwned &&
                (Abnormal || (!NT_SUCCESS(Status) && Stage->ParentFile))) {
                UDFRetainFailedNamespaceStage(Vcb, Source, Directory, Stage, HardLink,
                    Abnormal ? STATUS_UNSUCCESSFUL : Status);
                StageOwned = TRUE;
            }
            if (TargetOpen) {
                CloseStatus = UDFReleaseCreateReference(IrpContext, Vcb,
                    (PUDF_FILE_INFO*)&TargetOpen, TRUE);
                if (NT_SUCCESS(CloseStatus) && !Abnormal && NT_SUCCESS(Status))
                    CloseStatus = UDFCompleteFileClose(IrpContext, Vcb, Target);
                if (!Abnormal && NT_SUCCESS(Status)) Status = CloseStatus;
            }
            if (Target && !TargetOpen && !TargetOwned && !Abnormal &&
                (UDFCleanUpFile__(Vcb, Target) & UDF_FREE_FILEINFO))
                MyFreePool__(Target);
            if (StageOwned && (Abnormal || !NT_SUCCESS(Status))) {
                if (Abnormal || TargetOwned || FlagOn(Vcb->VcbState, VCB_STATE_VOLUME_READ_ONLY))
                    UDFQuarantineNamespace(Vcb, Abnormal ? STATUS_UNSUCCESSFUL : Status);
                else
                    UDFAbortNamespaceStage(IrpContext, Vcb, Source);
            } else if (Stage && !StageOwned && !Abnormal) {
                /* A failed initializer has no successful open to close. */
                if (UDFCleanUpFile__(Vcb, Stage) & UDF_FREE_FILEINFO)
                    MyFreePool__(Stage);
            }
        } _SEH2_FINALLY {
            BOOLEAN CleanupAbnormal = Abnormal || _SEH2_AbnormalTermination();
            _SEH2_TRY {
                if (TargetOpen) {
                    /* Release can raise before entering its checked group.
                     * Keep that actual unspent ordinary token explicitly;
                     * a token consumed before a later callback is NULL. */
                    UDFRetainOrdinaryOpenReference(Vcb,
                        (PUDF_FILE_INFO*)&TargetOpen,
                        NT_SUCCESS(Status) ? STATUS_UNSUCCESSFUL : Status);
                }
                if (CleanupAbnormal) {
                    if (Stage && Stage->Dloc && !StageOwned) {
                        UDFRetainFailedNamespaceStage(Vcb, Source, Directory, Stage, HardLink,
                                                        STATUS_UNSUCCESSFUL);
                        StageOwned = TRUE;
                    }
                    if (StageOwned || *TargetRemoved)
                        UDFQuarantineNamespace(Vcb, STATUS_UNSUCCESSFUL);
                }
            } _SEH2_FINALLY {
                if (TargetAcquired)
                    UDFReleaseFcb(IrpContext, TargetFcb);
            } _SEH2_END;
        } _SEH2_END;
    } _SEH2_END;
    if (NT_SUCCESS(Status))
        *Result = Stage;
    return Status;
}

/* Transfer a freshly created link's old FE hold to a preallocated
 * private FI, then acquire exactly one hold on the surviving Dloc. The
 * temporary parent contribution remains owned by Scratch until retirement;
 * the new link contributes its own separate parent hold. No aggregate
 * LinkRefCount copying and no shell-only Dloc free is permitted. */
static NTSTATUS
UDFAdoptHardLinkScratch(PVCB Vcb, PUDF_FILE_INFO Source,
                       PUDF_FILE_INFO Link, PUDF_FILE_INFO Scratch)
{
    PUDF_DATALOC_INFO Old, New = Source->Dloc;
    NTSTATUS Status = STATUS_SHARING_VIOLATION;
    UDFAcquireResourceExclusive(&Vcb->DlocResource2, TRUE);
    if (!UDFPrivateTemporaryFileInfo(Link) || !Link->ParentFile ||
        New == Link->Dloc || New->CleanupOwner ||
        New->NamespaceScratch != Link || New->NamespaceFileInfo != Link ||
        Link->Dloc->NamespaceScratchOwner != New || !Link->NamespaceStaged ||
        New->NamespaceScratchOwner || Source->Constructing || Source->ConstructionFailed)
        goto out;
    if (New->LinkRefCount == MAXULONG || Link->ParentFile->OpenCount == MAXULONG) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto out;
    }
    Old = Link->Dloc;
    Scratch->Dloc = Old;
    Scratch->NextLinkedFile = Scratch->PrevLinkedFile = Scratch;
    Scratch->ParentFile = Link->ParentFile;
    Scratch->RefCount = 1; /* transferred from the fresh Link/Old pair */
    Old->LinkedFileInfo = Scratch;
    Link->Dloc = New;
    InterlockedIncrement((PLONG)&New->LinkRefCount); /* one exact hold */
    InterlockedIncrement((PLONG)&Link->ParentFile->OpenCount);
    UDFInsertLinkedFile(Link, Source);
    New->NamespaceScratch = Scratch;
    New->NamespaceFileInfo = Link;
    Old->NamespaceScratchOwner = New;
    /* PendingNamespaceCount was acquired by the stage; retain it. */
    Status = STATUS_SUCCESS;
out:
    UDFReleaseResource(&Vcb->DlocResource2);
    return Status;
}

/*
    This routine moves file from DirInfo1 to DirInfo2 & renames it to fn
 */
NTSTATUS
UDFRenameMoveFile__(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN BOOLEAN IgnoreCase,
    IN OUT BOOLEAN* Replace,        // replace if destination file exists
    IN PUNICODE_STRING fn,       // destination
    IN OUT PUDF_FILE_INFO DirInfo1,
    IN OUT PUDF_FILE_INFO DirInfo2,
    IN OUT PUDF_FILE_INFO FileInfo, // source (opened)
    OUT PBOOLEAN NamespaceChanged,
    OUT PBOOLEAN TargetRemoved
    )
{
    PUDF_FILE_INFO FileInfo2 = NULL;
    NTSTATUS status;
    NTSTATUS CompletionStatus = STATUS_SUCCESS;
    BOOLEAN ReplaceRequested = *Replace;
    PDIR_INDEX_ITEM DirNdx1;
    PDIR_INDEX_ITEM DirNdx2;
    uint_di i,j;
    uint32 PartNum;
    uint_di ExcludedSourceIndex = (uint_di)-1;
    uint32 NTAttr = 0;

    *NamespaceChanged = FALSE;
    *TargetRemoved = FALSE;
    *Replace = FALSE;

    if (Vcb->VcbState & VCB_STATE_VOLUME_READ_ONLY)
        return STATUS_ACCESS_DENIED;

    // validate FileInfo
    ValidateFileInfo(DirInfo1);
    ValidateFileInfo(DirInfo2);
    ValidateFileInfo(FileInfo);
    if (!NT_SUCCESS(FileInfo->Dloc->ZeroDataError))
        return FileInfo->Dloc->ZeroDataError;
    if (!FileInfo->ParentFile || !FileInfo->FileIdent ||
        (FileInfo->Index < 2 && !UDFIsAStreamDir(FileInfo)))
        return STATUS_INVALID_PARAMETER;
    if (FileInfo->Dloc->NamespaceScratch || FileInfo->Dloc->NamespaceTarget ||
        FileInfo->Dloc->NamespaceTargetState != UdfNamespaceTargetNone ||
        !NT_SUCCESS(FileInfo->Dloc->NamespaceTargetError) ||
        FileInfo->Dloc->NamespaceScratchOwner)
        return STATUS_SHARING_VIOLATION;
    if (UDFLocateAnyParallelFI(FileInfo))
        return STATUS_SHARING_VIOLATION; /* no partial per-link owner transfer */
    if (UDFGetFileLinkCount(FileInfo) >= UDF_MAX_LINK_COUNT)
        return STATUS_TOO_MANY_LINKS; /* the temporary second name needs one link */
    /* Commit the prior source generation before staging or replacing a
     * target. A source flush failure cannot remove that target name. */
    status = UDFFlushFile__(IrpContext, Vcb, FileInfo, UDF_FLUSH_FLAGS_LITE);
    if (!NT_SUCCESS(status))
        return status;


    i = j = 0;
    if (DirInfo1->Dloc == DirInfo2->Dloc) {
        if (NT_SUCCESS(status = UDFFindFile(Vcb, IgnoreCase, TRUE, fn, DirInfo2, &j)) &&
            j == FileInfo->Index) {
            PFILE_IDENT_DESC NewFid;
            uint32 NewFidLength;
            UNICODE_STRING NewIndexName;
            uint32 HeaderBytes;
            if (FileInfo->FileIdentLen < sizeof(FILE_IDENT_DESC))
                return STATUS_FILE_CORRUPT_ERROR;
            HeaderBytes = sizeof(FILE_IDENT_DESC) + FileInfo->FileIdent->lengthOfImpUse;
            DirNdx2 = UDFDirIndex(DirInfo2->Dloc->DirIndex, j);
            if (!DirNdx2 || FileInfo->FileIdentLen != DirNdx2->Length ||
                HeaderBytes > FileInfo->FileIdentLen ||
                FileInfo->FileIdent->lengthFileIdent > FileInfo->FileIdentLen - HeaderBytes)
                return STATUS_FILE_CORRUPT_ERROR;
            status = UDFBuildFileIdent(Vcb, fn, &FileInfo->FileIdent->icb,
                FileInfo->FileIdent->lengthOfImpUse, &NewFid, &NewFidLength);
            if (!NT_SUCCESS(status))
                return status;
            if (NewFidLength == DirNdx2->Length) {
                /* The serialized record, not its UTF-16 spelling, decides
                 * whether this directory slot can be reused in place. */
                RtlZeroMemory(&NewIndexName, sizeof(NewIndexName));
                NewIndexName.Buffer = MyAllocatePool__(NonPagedPool,
                                                        fn->Length + sizeof(WCHAR));
                if (!NewIndexName.Buffer) {
                    MyFreePool__(NewFid);
                    return STATUS_INSUFFICIENT_RESOURCES;
                }
                NewIndexName.Length = fn->Length;
                NewIndexName.MaximumLength = fn->Length + sizeof(WCHAR);
                RtlCopyMemory(NewIndexName.Buffer, fn->Buffer, fn->Length);
                NewIndexName.Buffer[fn->Length / sizeof(WCHAR)] = 0;
                NewFid->descTag = FileInfo->FileIdent->descTag;
                NewFid->fileVersionNum = FileInfo->FileIdent->fileVersionNum;
                NewFid->fileCharacteristics = FileInfo->FileIdent->fileCharacteristics;
                RtlCopyMemory(NewFid + 1, FileInfo->FileIdent + 1,
                               FileInfo->FileIdent->lengthOfImpUse);
                MyFreePool__(FileInfo->FileIdent);
                FileInfo->FileIdent = NewFid;
                FileInfo->FileIdentLen = NewFidLength;
                if (DirNdx2->FName.Buffer)
                    MyFreePool__(DirNdx2->FName.Buffer);
                DirNdx2->FName = NewIndexName;
                if (DirInfo1 != DirInfo2) {
                    InterlockedExchangeAdd((PLONG)&DirInfo2->OpenCount, FileInfo->RefCount);
                    InterlockedExchangeAdd((PLONG)&DirInfo1->OpenCount,
                                            -((LONG)FileInfo->RefCount));
                    FileInfo->ParentFile = DirInfo2;
                }
                DirNdx2->FI_Flags |= UDF_FI_FLAG_FI_MODIFIED;
                *NamespaceChanged = TRUE;
                UDFBuildHashEntry(Vcb, &DirNdx2->FName, &DirNdx2->hashes, HASH_ALL);
                return STATUS_SUCCESS;
            }
            MyFreePool__(NewFid);
            /* A different CS0 length (or a short-name alias) needs a new
             * record. Ignore only this source slot during collision search;
             * all other case-insensitive/8.3 collisions still reject it. */
            ExcludedSourceIndex = j;
        }
    }

    /* Allocate a complete deleted destination slot before target removal.
     * The helper returns an exact source-owned scratch generation. */
    status = UDFStageNamespaceDestination(IrpContext, Vcb, IgnoreCase,
        ReplaceRequested, FALSE, fn, DirInfo2, FileInfo,
        ExcludedSourceIndex, &FileInfo2, TargetRemoved);
    *Replace = *TargetRemoved;
    if (!NT_SUCCESS(status))
        return status;
    if (!FileInfo2 || !UDFPrivateTemporaryFileInfo(FileInfo2))
        return UDFQuarantineNamespace(Vcb, STATUS_FILE_CORRUPT_ERROR);
    // update pointers
    DirNdx1 = UDFDirIndex(DirInfo1->Dloc->DirIndex, i = FileInfo->Index);
    DirNdx2 = UDFDirIndex(DirInfo2->Dloc->DirIndex, j = FileInfo2->Index);


    NTAttr = UDFAttributesToNT(DirNdx1, FileInfo->Dloc->FileEntry);
    PartNum = UDFGetRefPartNumByPhysLba(Vcb,
        FileInfo->Dloc->FELoc.Mapping[0].extLocation);
    if (PartNum == (uint32)-1)
        return UDFQuarantineNamespace(Vcb, STATUS_FILE_CORRUPT_ERROR);
    status = UDFCommitRenameDestination(IrpContext, Vcb, FileInfo, FileInfo2);
    if (!NT_SUCCESS(status))
        return status;

    /* Only a confirmed destination permits removal of the old source.
     * Its unlink consumes the temporary second name's link-count increment. */
    status = UDFUnlinkFile__(IrpContext, Vcb, FileInfo, FALSE);
    if (!NT_SUCCESS(status)) {
        UDFHideUncertainNamespaceStage(Vcb, FileInfo2);
        return UDFQuarantineNamespace(Vcb, status);
    }

    // PHASE 1
    // drop all unnecessary info from FileInfo & flush FI

    DirNdx1->FileInfo = NULL;
    ASSERT(FileInfo->Dloc->FELoc.Mapping[0].extLocation);
    FileInfo->Dloc->NamespacePhase = UdfNamespaceSourceTombstoneSubmitted;
    _SEH2_TRY {
        status = UDFFlushFI(IrpContext, Vcb, FileInfo, PartNum);
        if (NT_SUCCESS(status))
            status = UDFFlushFile__(IrpContext, Vcb, DirInfo1, UDF_FLUSH_FLAGS_LITE);
        if (NT_SUCCESS(status))
            status = UDFBarrierFlushDevice(Vcb->TargetDeviceObject);
    } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
        status = _SEH2_GetExceptionCode();
    } _SEH2_END;
    if (!NT_SUCCESS(status))
        CompletionStatus = status;
    InterlockedExchangeAdd((PLONG)&(DirInfo1->OpenCount),
                          -((LONG)(FileInfo->RefCount)));
    // PHASE 2
    // copy all necessary info from FileInfo to FileInfo2

    /* Destination bytes already name the surviving source ICB. Keep
     * that valid FID when the old source buffer contains a tombstone. */
    MyFreePool__(FileInfo->FileIdent);
    FileInfo->FileIdent = NULL;

    // PHASE 3
    // copy all necessary info from FileInfo2 to FileInfo

    /* The confirmed destination now uses the surviving source FI. */
    DirNdx2->FileInfo = FileInfo;
    DirNdx2->FileCharacteristics = DirNdx1->FileCharacteristics & ~FILE_DELETED;
    DirNdx2->FileEntryLoc = DirNdx1->FileEntryLoc;
    DirNdx2->FI_Flags = (DirNdx2->FI_Flags & ~UDF_FI_FLAG_SYS_ATTR) | UDF_FI_FLAG_FI_MODIFIED;
    /* The scratch still owns its one parent contribution until retirement. */
    InterlockedExchangeAdd((PLONG)&(DirInfo2->OpenCount), FileInfo->RefCount);

    UDFAttributesToUDF(DirNdx2, FileInfo2->Dloc->FileEntry, NTAttr);

    FileInfo->Index = j;
    FileInfo->FileIdent = FileInfo2->FileIdent;
    FileInfo->FileIdentLen = FileInfo2->FileIdentLen;
    FileInfo2->FileIdent = NULL; /* ownership transferred to the source FI */
    FileInfo->ParentFile = DirInfo2;
    FileInfo->Dloc->NamespacePhase = UdfNamespaceIdentityMoved;
    *NamespaceChanged = TRUE;
    FileInfo->Dloc->FE_Flags |= UDF_FE_FLAG_FE_MODIFIED;

    ((icbtag*)(FileInfo->Dloc->FileEntry+1))->parentICBLocation =
        ((icbtag*)(FileInfo2->Dloc->FileEntry+1))->parentICBLocation;

    /* Stage publication already counted the new directory/name and its
     * parent link; source unlink removed the old ones exactly once. */
    if (UDFIsADirectory(FileInfo)) {

        if (DirInfo1 != DirInfo2) {
            _SEH2_TRY {
                status = UDFUpdateParentFID(IrpContext, Vcb, FileInfo,
                                             NT_SUCCESS(CompletionStatus));
            } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
                status = _SEH2_GetExceptionCode();
            } _SEH2_END;
            if (!NT_SUCCESS(status)) {
                if (NT_SUCCESS(CompletionStatus)) CompletionStatus = status;
                UDFPrint(("UDFRenameMoveFile__: Updating parent FID failed: %08lx\n", status));
            }
        }
    }

//    UDFUpdateModifyTime(Vcb, FileInfo);

    /* Publish the actual surviving FE/FID before its old scratch FE can
     * become allocatable. A failed write retains both generations and the
     * first error; the caller still applies the prepared in-memory prefix. */
    _SEH2_TRY {
        if (NT_SUCCESS(CompletionStatus))
            CompletionStatus = UDFFlushFile__(IrpContext, Vcb, FileInfo, 0);
        if (NT_SUCCESS(CompletionStatus))
            CompletionStatus = UDFBarrierFlushDevice(Vcb->TargetDeviceObject);
    } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
        if (NT_SUCCESS(CompletionStatus)) CompletionStatus = _SEH2_GetExceptionCode();
    } _SEH2_END;
    if (!NT_SUCCESS(CompletionStatus))
        return UDFQuarantineNamespace(Vcb, CompletionStatus);
    FileInfo->Dloc->NamespacePhase = UdfNamespaceCommitted;
    CompletionStatus = UDFRetireNamespaceTarget(IrpContext, Vcb, FileInfo->Dloc);
    if (NT_SUCCESS(CompletionStatus))
        CompletionStatus = UDFRetireNamespaceScratch(IrpContext, Vcb, FileInfo);
    (*Replace) = *TargetRemoved;
    return CompletionStatus;
} // end UDFRenameMoveFile__()

/*
    This routine transforms zero-sized file to directory
 */
NTSTATUS
UDFRecordDirectory__(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN OUT PUDF_FILE_INFO DirInfo
    )
{
    NTSTATUS Status;
    LONG_AD ParentIcb;
    PFILE_IDENT_DESC ParentFid = NULL;
    UNICODE_STRING ParentName;
    uint32 ChildPart, ParentPart, ChildLba, ParentLba, TagLoc, FidLength;
    SIZE_T Written = 0;
    PDIR_INDEX_ITEM DirNdx = NULL;
    BOOLEAN StreamDirectory;

    if (!DirInfo || !DirInfo->Dloc || !DirInfo->Dloc->FileEntry ||
        !DirInfo->Dloc->FELoc.Mapping || !DirInfo->ParentFile ||
        !DirInfo->ParentFile->Dloc || !DirInfo->ParentFile->Dloc->FileEntry ||
        !DirInfo->ParentFile->Dloc->FELoc.Mapping)
        return STATUS_INVALID_PARAMETER;
    if (UDFIsAStreamDir(DirInfo->ParentFile)) return STATUS_ACCESS_DENIED;
    if (UDFGetFileSize(DirInfo))
        return DirInfo->FileIdent && (DirInfo->FileIdent->fileCharacteristics & FILE_DIRECTORY) ?
               STATUS_FILE_IS_A_DIRECTORY : STATUS_NOT_A_DIRECTORY;
    if (DirInfo->Dloc->DirIndex) return STATUS_FILE_IS_A_DIRECTORY;
    if (UDFGetFileLinkCount(DirInfo->ParentFile) >= 0x7fff)
        return STATUS_INSUFFICIENT_RESOURCES;
    StreamDirectory = UDFIsAStreamDir(DirInfo);
    ChildLba = DirInfo->Dloc->FELoc.Mapping[0].extLocation;
    ParentLba = DirInfo->ParentFile->Dloc->FELoc.Mapping[0].extLocation;
    ChildPart = UDFGetRefPartNumByPhysLba(Vcb, ChildLba);
    ParentPart = UDFGetRefPartNumByPhysLba(Vcb, ParentLba);
    if (ChildPart >= Vcb->PartitionMaps || ParentPart >= Vcb->PartitionMaps || ParentPart > MAXUSHORT)
        return STATUS_FILE_CORRUPT_ERROR;
    TagLoc = UDFPhysLbaToPart(Vcb, ChildPart, ChildLba);
    RtlZeroMemory(&ParentIcb, sizeof(ParentIcb));
    ParentIcb.extLength = Vcb->SectorSize;
    ParentIcb.extLocation.logicalBlockNum = UDFPhysLbaToPart(Vcb, ParentPart, ParentLba);
    ParentIcb.extLocation.partitionReferenceNum = (uint16)ParentPart;
    if (TagLoc == LBA_OUT_OF_EXTENT || ParentIcb.extLocation.logicalBlockNum == LBA_OUT_OF_EXTENT)
        return STATUS_FILE_CORRUPT_ERROR;
    ((struct FidADImpUse*)&ParentIcb.impUse)->uniqueID = (uint32)UDFGetFileUID(DirInfo->ParentFile);
    ParentName.Buffer = (PWCH)L"";
    ParentName.Length = 0;
    ParentName.MaximumLength = sizeof(WCHAR);
    Status = UDFBuildFileIdent(Vcb, &ParentName, &ParentIcb, 0, &ParentFid, &FidLength);
    if (!NT_SUCCESS(Status)) return Status;
    _SEH2_TRY {
        ParentFid->fileCharacteristics |= FILE_PARENT;
        if (!StreamDirectory) ParentFid->fileCharacteristics |= FILE_DIRECTORY;
        UDFSetUpTag(Vcb, &ParentFid->descTag, (uint16)FidLength, TagLoc, 0);
        /* Preparation errors above change no link/count/type. Record the
         * intended identity before the first complete directory FE write. */
        if (DirInfo->FileIdent) {
            DirInfo->FileIdent->fileCharacteristics |= FILE_DIRECTORY;
            DirNdx = UDFDirIndex(UDFGetDirIndexByFileInfo(DirInfo), DirInfo->Index);
        }
        if (DirNdx) DirNdx->FileCharacteristics |= FILE_DIRECTORY;
        UDFIncFileLinkCount(DirInfo->ParentFile);
        ((icbtag*)(DirInfo->Dloc->FileEntry + 1))->fileType =
            StreamDirectory ? UDF_FILE_TYPE_STREAMDIR : UDF_FILE_TYPE_DIRECTORY;
        DirInfo->Dloc->FE_Flags |= UDF_FE_FLAG_FE_MODIFIED;
        /* Stream directories had a cancelling adjustment in the caller.
         * Exclude them here instead, including every early failure path. */
        if (!StreamDirectory) {
            UDFDecFileCounter(Vcb);
            UDFIncDirCounter(Vcb);
        }
        Status = UDFWriteFile__(IrpContext, Vcb, DirInfo, 0, FidLength,
                                 FALSE, (int8*)ParentFid, &Written);
        if (Status == STATUS_SUCCESS && Written != FidLength) Status = STATUS_IO_DEVICE_ERROR;
        if (Status != STATUS_SUCCESS) {
            if (NT_SUCCESS(Status)) Status = STATUS_IO_DEVICE_ERROR;
            _SEH2_LEAVE;
        }
        if (DirNdx) DirNdx->FileCharacteristics = DirInfo->FileIdent->fileCharacteristics;
        Status = UDFIndexDirectory(IrpContext, Vcb, DirInfo);
    } _SEH2_FINALLY {
        MyFreePool__(ParentFid);
    } _SEH2_END;
    return Status;
} // end UDFRecordDirectory__()

/* The dense physical bitmap is normally checkpointed after the namespace.
 * Truncation is the exception: before making an old extent allocatable in
 * RAM, persist FREE for exactly its released bits.  While the bitmap lock is
 * held, write and barrier the shortened FE.  A cut on either side of that FE
 * then has either an old owner with a referenced-free bit (repairable from a
 * complete graph), or no old owner.  Never publish unrelated RAM allocations
 * early by flushing the whole bitmap here. */
static NTSTATUS
UDFTruncatePrefreeRange(PIRP_CONTEXT IrpContext, PVCB Vcb,
                       uint32 BitmapLba, uint32 FirstLbn, uint32 Blocks,
                       int8* Sector, PBOOLEAN PossiblyModified)
{
    const uint32 SectorBytes = Vcb->SectorSize;
    const uint32 BitmapBytes = Vcb->FSBM_ByteCount;
    const uint32 DataOffset = sizeof(SPACE_BITMAP_DESC);
    NTSTATUS status;
    SIZE_T written;

    if (FirstLbn >= Vcb->FSBM_BitCount ||
        Blocks > Vcb->FSBM_BitCount - FirstLbn)
        return STATUS_FILE_CORRUPT_ERROR;
    while (Blocks) {
        uint32 byteOffset = FirstLbn >> 3;
        uint32 sectorOffset = (DataOffset + byteOffset) % SectorBytes;
        uint32 sectorIndex = (DataOffset + byteOffset) / SectorBytes;
        uint32 bits = min(Blocks, (SectorBytes - sectorOffset) * 8 -
                                  (FirstLbn & 7));
        uint32 j;
        BOOLEAN changed = FALSE;

        if (byteOffset >= BitmapBytes ||
            ((FirstLbn + bits - 1) >> 3) >= BitmapBytes)
            return STATUS_FILE_CORRUPT_ERROR;
        status = UDFReadSectors(IrpContext, Vcb, FALSE,
                                BitmapLba + sectorIndex, 1, FALSE, Sector);
        if (!NT_SUCCESS(status)) return status;
        for (j = 0; j < bits; ++j) {
            uint32 bit = FirstLbn + j;
            uint8* byte = (uint8*)Sector +
                ((DataOffset + (bit >> 3)) % SectorBytes);
            uint8 mask = (uint8)(1U << (bit & 7));
            if (!(*byte & mask)) {
                *byte |= mask;
                changed = TRUE;
            }
        }
        if (changed) {
            written = 0;
            *PossiblyModified = TRUE;
            status = UDFWriteSectors(IrpContext, Vcb, FALSE,
                                     BitmapLba + sectorIndex, 1, FALSE,
                                     Sector, &written);
            if (!NT_SUCCESS(status)) return status;
            if (written != SectorBytes) return STATUS_IO_DEVICE_ERROR;
        }
        FirstLbn += bits;
        Blocks -= bits;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS
UDFTruncatePrefreeMap(PIRP_CONTEXT IrpContext, PVCB Vcb,
                      uint32 BitmapLba, PEXTENT_MAP Map,
                      uint64 KeepBytes, uint32 FeLba, int8* Sector,
                      PBOOLEAN PossiblyModified)
{
    uint64 offset = 0;
    uint32 i;
    for (i = 0; Map && (Map[i].extLength & UDF_EXTENT_LENGTH_MASK); ++i) {
        uint32 bytes = Map[i].extLength & UDF_EXTENT_LENGTH_MASK;
        uint32 blocks = (bytes + Vcb->SectorSize - 1) >> Vcb->SectorShift;
        uint32 keep = 0;
        uint32 first;
        NTSTATUS status;

        if (KeepBytes > offset) {
            uint64 within = min((uint64)bytes, KeepBytes - offset);
            keep = (uint32)((within + Vcb->SectorSize - 1) >>
                            Vcb->SectorShift);
        }
        offset += bytes;
        if ((Map[i].extLength >> 30) == EXTENT_NOT_RECORDED_NOT_ALLOCATED)
            continue;
        if (keep >= blocks) continue;
        if (!Map[i].extLocation ||
            Map[i].extLocation < Vcb->Partitions[0].PartitionRoot)
            return STATUS_FILE_CORRUPT_ERROR;
        /* The FE can be the first entry of an in-ICB transition.  It is
         * never a released data or allocation-descriptor block. */
        first = Map[i].extLocation + keep;
        if (first == FeLba) {
            first++;
            keep++;
        }
        if (keep >= blocks) continue;
        if (first <= FeLba && FeLba < Map[i].extLocation + blocks)
            return STATUS_NOT_SUPPORTED;
        if (UDFPhysLbaToMetadata(Vcb, first) != LBA_OUT_OF_EXTENT ||
            UDFPhysLbaToMetadata(Vcb, Map[i].extLocation + blocks - 1) !=
                LBA_OUT_OF_EXTENT)
            return STATUS_NOT_SUPPORTED;
        status = UDFTruncatePrefreeRange(IrpContext, Vcb, BitmapLba,
                    first - Vcb->Partitions[0].PartitionRoot,
                    blocks - keep, Sector, PossiblyModified);
        if (!NT_SUCCESS(status)) return status;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS
UDFTruncatePrefree(PIRP_CONTEXT IrpContext, PVCB Vcb,
                   PUDF_FILE_INFO FileInfo, int64 NewLength,
                   PBOOLEAN PossiblyModified)
{
    uint32 bitmapLba, feLba, partNum;
    uint32 i;
    uint64 keep;
    uint16 ident;
    NTSTATUS status;
    int8* sector;
    PSPACE_BITMAP_DESC desc;
    BOOLEAN switchToIcb;

    if (Vcb->CDR_Mode || Vcb->BitmapFcb || !Vcb->FSBM_Bitmap ||
        !Vcb->FSBM_OldBitmap || Vcb->SectorSize != 512 ||
        !Vcb->PartitionMaps ||
        Vcb->Partitions[0].PartitionType == UDF_METADATA_MAP25 ||
        !FileInfo->Dloc->DataLoc.Mapping ||
        (FileInfo->Dloc->DataLoc.Flags & EXTENT_FLAG_PREALLOCATED))
        return STATUS_NOT_SUPPORTED;
    partNum = 0;
    feLba = FileInfo->Dloc->FELoc.Mapping[0].extLocation;
    bitmapLba = Vcb->Partitions[partNum].PartitionRoot +
                Vcb->Partitions[partNum].UspaceBitmap;
    sector = (int8*)MyAllocatePool__(NonPagedPool, Vcb->SectorSize);
    if (!sector) return STATUS_INSUFFICIENT_RESOURCES;
    status = UDFReadTagged(IrpContext, Vcb, sector, bitmapLba,
                           Vcb->Partitions[partNum].UspaceBitmap, &ident);
    if (!NT_SUCCESS(status)) goto done;
    desc = (PSPACE_BITMAP_DESC)sector;
    /* The descriptor's CRC may cover the two size fields, but it must not
     * cover bitmap payload bytes changed by these sector writes. */
    if (ident != TID_SPACE_BITMAP_DESC ||
        desc->descTag.descCRCLength >
            sizeof(SPACE_BITMAP_DESC) - sizeof(tag) ||
        desc->numOfBits != Vcb->FSBM_BitCount ||
        desc->numOfBytes < Vcb->FSBM_ByteCount) {
        status = STATUS_NOT_SUPPORTED;
        goto done;
    }

    switchToIcb =
        NewLength <= (int64)(Vcb->SectorSize - FileInfo->Dloc->FileEntryLen) &&
        ((((PFILE_ENTRY)FileInfo->Dloc->FileEntry)->icbTag.flags &
          ICB_FLAG_ALLOC_MASK) != ICB_FLAG_AD_IN_ICB);
    /* An external AD chain may be shortened by UDFBuildAllocDescs during
     * the FE flush, after this prefree scan.  Handle the all-released ICB
     * conversion, but refuse a partial shrink until its exact AD delta can
     * be included in the same transaction. */
    if (!switchToIcb && FileInfo->Dloc->AllocLoc.Mapping) {
        PEXTENT_MAP map = FileInfo->Dloc->AllocLoc.Mapping;
        for (i = 0; map[i].extLength & UDF_EXTENT_LENGTH_MASK; ++i) {
            if ((map[i].extLength >> 30) !=
                    EXTENT_NOT_RECORDED_NOT_ALLOCATED &&
                (map[i].extLocation != feLba ||
                 (map[i].extLength & UDF_EXTENT_LENGTH_MASK) >
                    Vcb->SectorSize)) {
                status = STATUS_NOT_SUPPORTED;
                goto done;
            }
        }
    }
    keep = switchToIcb ? 0 :
           (uint64)FileInfo->Dloc->DataLoc.Offset + (uint64)NewLength;
    status = UDFTruncatePrefreeMap(IrpContext, Vcb, bitmapLba,
                    FileInfo->Dloc->DataLoc.Mapping, keep, feLba,
                    sector, PossiblyModified);
    if (!NT_SUCCESS(status)) goto done;
    if (switchToIcb && FileInfo->Dloc->AllocLoc.Mapping)
        status = UDFTruncatePrefreeMap(IrpContext, Vcb, bitmapLba,
                    FileInfo->Dloc->AllocLoc.Mapping, 0, feLba,
                    sector, PossiblyModified);
done:
    MyFreePool__(sector);
    return status;
}

/* Prepare inline bytes and exact old data/AED owners before changing the
 * live FE mode or either map. The FE buffer address stays stable for callers
 * holding it under the existing file writer domain. */
static NTSTATUS
UDFResizeFileToInline(PIRP_CONTEXT IrpContext, PVCB Vcb,
                       PUDF_FILE_INFO FileInfo, int64 NewLength)
{
    PUDF_DATALOC_INFO Dloc = FileInfo->Dloc;
    EXTENT_INFO DataPlan, AllocPlan;
    PUDF_EXTENT_RETIREMENT DataRetired = NULL, AllocRetired = NULL, Retired;
    PEXTENT_MAP InlineMap = NULL, OldDataMap, OldAllocMap;
    int8* InlineFe = NULL;
    uint32 PartNum, DataPartNum, FeLba, Added, i, Copy;
    uint16 Ident;
    NTSTATUS Status = STATUS_SUCCESS;
    BOOLEAN Published = FALSE;

    RtlZeroMemory(&DataPlan, sizeof(DataPlan));
    RtlZeroMemory(&AllocPlan, sizeof(AllocPlan));
    Ident = Dloc->FileEntry ? Dloc->FileEntry->tagIdent : 0;
    if (!Dloc->FELoc.Mapping || Dloc->FELoc.Mapping[0].extLength != Vcb->SectorSize ||
        Dloc->FELoc.Mapping[1].extLength || !Dloc->DataLoc.Mapping || Dloc->DataLoc.Offset ||
        (Dloc->AllocLoc.Mapping && Dloc->AllocLoc.Mapping == Dloc->DataLoc.Mapping) ||
        (!Dloc->AllocLoc.Mapping && Dloc->AllocLoc.Length) ||
        (Ident != TID_FILE_ENTRY && Ident != TID_EXTENDED_FILE_ENTRY) ||
        Dloc->FileEntryLen < (Ident == TID_FILE_ENTRY ? sizeof(FILE_ENTRY) : sizeof(EXTENDED_FILE_ENTRY)) ||
        Dloc->FileEntryLen > Vcb->SectorSize || NewLength < 0 ||
        NewLength >= Dloc->DataLoc.Length ||
        (uint64)NewLength > Vcb->SectorSize - Dloc->FileEntryLen)
        return STATUS_FILE_CORRUPT_ERROR;
    FeLba = Dloc->FELoc.Mapping[0].extLocation;
    PartNum = UDFGetRefPartNumByPhysLba(Vcb, FeLba);
    if (PartNum >= Vcb->PartitionMaps) return STATUS_FILE_CORRUPT_ERROR;
    DataPartNum = PartNum;
    if (Vcb->MetadataFileInfo && PartNum == Vcb->MetadataRef && !UDFIsADirectory(FileInfo))
        DataPartNum = Vcb->MetadataPhysRef;

    _SEH2_TRY {
        InlineFe = (int8*)MyAllocatePool__(NonPagedPool, Vcb->SectorSize);
        InlineMap = (PEXTENT_MAP)MyAllocatePoolTag__(NonPagedPool,
                                                    2 * sizeof(EXTENT_MAP), MEM_EXTMAP_TAG);
        if (!InlineFe || !InlineMap) {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto inline_exit;
        }
        RtlCopyMemory(InlineFe, Dloc->FileEntry, Vcb->SectorSize);
        RtlZeroMemory(InlineFe + Dloc->FileEntryLen, Vcb->SectorSize - Dloc->FileEntryLen);
        if (NewLength) {
            Status = UDFReadExtent(IrpContext, Vcb, &Dloc->DataLoc, 0,
                (SIZE_T)NewLength, FALSE, InlineFe + Dloc->FileEntryLen);
            if (!NT_SUCCESS(Status)) goto inline_exit;
        }
        Status = UDFPrepareExtentShrink(Vcb, DataPartNum, 0, FALSE,
                                          &Dloc->DataLoc, &DataPlan, &DataRetired);
        if (!NT_SUCCESS(Status)) goto inline_exit;
        if (Dloc->AllocLoc.Mapping) {
            Status = UDFPrepareExtentShrink(Vcb, PartNum, 0, TRUE,
                                              &Dloc->AllocLoc, &AllocPlan, &AllocRetired);
            if (!NT_SUCCESS(Status)) goto inline_exit;
        }
        /* A malformed flattened map must not retire the owner FE itself.
         * Ordinary AD maps keep that first sector via their inline Offset. */
        for (Copy = 0; Copy < 2; Copy++) {
            Retired = Copy ? AllocRetired : DataRetired;
            if (!Retired) continue;
            for (i = 0; i < Retired->Count; i++) {
                uint64 End = (uint64)Retired->Ranges[i].extLocation +
                    ((Retired->Ranges[i].extLength & UDF_EXTENT_LENGTH_MASK) >> Vcb->SectorShift);
                if ((uint64)FeLba >= Retired->Ranges[i].extLocation && (uint64)FeLba < End) {
                    Status = STATUS_FILE_CORRUPT_ERROR;
                    goto inline_exit;
                }
            }
        }
        RtlZeroMemory(InlineMap, 2 * sizeof(EXTENT_MAP));
        InlineMap[0] = Dloc->FELoc.Mapping[0];
        ((PFILE_ENTRY)InlineFe)->icbTag.flags &= ~ICB_FLAG_ALLOC_MASK;
        ((PFILE_ENTRY)InlineFe)->icbTag.flags |= ICB_FLAG_AD_IN_ICB;
        Added = (DataRetired != NULL) + (AllocRetired != NULL);
        UDFAcquireResourceExclusive(&Vcb->BitMapResource1, TRUE);
        _SEH2_TRY {
            if (Vcb->PendingExtentRetirements < 0 ||
                Added > (ULONG)(MAXLONG - Vcb->PendingExtentRetirements)) {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                _SEH2_LEAVE;
            }
            if (DataRetired) {
                DataRetired->Next = Dloc->RetiredExtents;
                Dloc->RetiredExtents = DataRetired;
                DataRetired = NULL;
                InterlockedIncrement(&Vcb->PendingExtentRetirements);
            }
            if (AllocRetired) {
                AllocRetired->Next = Dloc->RetiredExtents;
                Dloc->RetiredExtents = AllocRetired;
                AllocRetired = NULL;
                InterlockedIncrement(&Vcb->PendingExtentRetirements);
            }
            OldDataMap = Dloc->DataLoc.Mapping;
            OldAllocMap = Dloc->AllocLoc.Mapping;
            Dloc->DataLoc.Mapping = InlineMap;
            InlineMap = NULL;
            Dloc->DataLoc.Offset = Dloc->FileEntryLen;
            Dloc->DataLoc.Length = NewLength;
            Dloc->DataLoc.Modified = TRUE;
            Dloc->DataLoc.Flags &= ~(EXTENT_FLAG_PREALLOCATED | EXTENT_FLAG_CUT_PREALLOCATED);
            Dloc->AllocLoc.Mapping = NULL;
            Dloc->AllocLoc.Length = 0;
            Dloc->AllocLoc.Offset = 0;
            Dloc->AllocLoc.Modified = TRUE;
            RtlCopyMemory(Dloc->FileEntry, InlineFe, Vcb->SectorSize);
            Dloc->FE_Flags |= UDF_FE_FLAG_FE_MODIFIED;
            if (FileInfo->Fcb) FileInfo->Fcb->FcbState |= UDF_FCB_EMBEDDED_DATA;
            Published = TRUE;
            MyFreePool__(OldDataMap);
            if (OldAllocMap) MyFreePool__(OldAllocMap);
        } _SEH2_FINALLY {
            UDFReleaseResource(&Vcb->BitMapResource1);
        } _SEH2_END;
inline_exit:
        NOTHING;
    } _SEH2_FINALLY {
        if (DataPlan.Mapping) MyFreePool__(DataPlan.Mapping);
        if (AllocPlan.Mapping) MyFreePool__(AllocPlan.Mapping);
        if (DataRetired) MyFreePool__(DataRetired);
        if (AllocRetired) MyFreePool__(AllocRetired);
        if (InlineMap) MyFreePool__(InlineMap);
        if (InlineFe) MyFreePool__(InlineFe);
        if (_SEH2_AbnormalTermination() && Published)
            Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
    } _SEH2_END;
    return Status;
}

/* Dispatch file resizing to private extent/inline plans. The public wrapper
 * below retains its bounded physical prefree protocol; old range owners are
 * released by FE/barrier retirement, not by this in-memory transition. */
static NTSTATUS
UDFResizeFileUnordered__(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN OUT PUDF_FILE_INFO FileInfo,
    IN int64 NewLength
    )
{
    SIZE_T WrittenBytes;
    NTSTATUS status;
    uint32 PartNum;

    UDFPrint(("UDFResizeFile__: FI %x, -> %I64x\n", FileInfo, NewLength));
    ValidateFileInfo(FileInfo);
//    ASSERT(FileInfo->RefCount >= 1);

    if (NewLength < 0 || FileInfo->Dloc->FileEntryLen > Vcb->SectorSize)
        return STATUS_INVALID_PARAMETER;
    if (NewLength != FileInfo->Dloc->DataLoc.Length &&
        (Vcb->VcbState & VCB_STATE_VOLUME_READ_ONLY))
        return STATUS_MEDIA_WRITE_PROTECTED;
    if (NewLength != FileInfo->Dloc->DataLoc.Length && (Vcb->Vat || Vcb->CDR_Mode))
        return STATUS_NOT_SUPPORTED;
    status = UDFFlushDirectoryAppend(IrpContext, Vcb, FileInfo);
    if (!NT_SUCCESS(status))
        return status;

    if ((NewLength >> Vcb->SectorShift) > Vcb->TotalAllocUnits) {
        UDFPrint(("STATUS_DISK_FULL\n"));
        return STATUS_DISK_FULL;
    }
    if (NewLength == FileInfo->Dloc->DataLoc.Length) return STATUS_SUCCESS;
    if (FileInfo->ParentFile && (FileInfo->Index >= 2)) {
        UDFDirIndex(FileInfo->ParentFile->Dloc->DirIndex,FileInfo->Index)->FI_Flags &= ~UDF_FI_FLAG_SYS_ATTR;
    }
    if (NewLength > FileInfo->Dloc->DataLoc.Length) {
        // grow file
        return UDFWriteFile__(IrpContext, Vcb, FileInfo, NewLength, 0, FALSE, NULL, &WrittenBytes);
    }
    // truncate file
    if (NewLength <= (Vcb->SectorSize - FileInfo->Dloc->FileEntryLen)) {
        // check if we are already in IN_ICB mode
        if ((((PFILE_ENTRY)(FileInfo->Dloc->FileEntry))->icbTag.flags & ICB_FLAG_ALLOC_MASK) != ICB_FLAG_AD_IN_ICB) {
            status = UDFResizeFileToInline(IrpContext, Vcb, FileInfo, NewLength);
        } else {
            // just modify Length field
            FileInfo->Dloc->DataLoc.Length = NewLength;
            status = STATUS_SUCCESS;
        }
    } else {
        // resize extent
        ASSERT(FileInfo->Dloc->FELoc.Mapping[0].extLocation);
        PartNum = UDFGetRefPartNumByPhysLba(Vcb, FileInfo->Dloc->FELoc.Mapping[0].extLocation);
        if (Vcb->MetadataFileInfo && PartNum == Vcb->MetadataRef &&
            !UDFIsADirectory(FileInfo))
            PartNum = Vcb->MetadataPhysRef;
        status = UDFResizeExtent(IrpContext, Vcb, FileInfo, PartNum, NewLength, FALSE, &FileInfo->Dloc->DataLoc);
        FileInfo->Dloc->DataLoc.Modified = TRUE;
        FileInfo->Dloc->AllocLoc.Modified = TRUE;
    }
    if (NT_SUCCESS(status)) {
        UDFSetFileSize(FileInfo, NewLength);
    }

    ASSERT(UDFGetFileSize(FileInfo) <= UDFGetExtentLength(FileInfo->Dloc->DataLoc.Mapping));

    return status;
} // end UDFResizeFile__()

NTSTATUS
UDFResizeFile__(PIRP_CONTEXT IrpContext, PVCB Vcb,
                PUDF_FILE_INFO FileInfo, int64 NewLength)
{
    NTSTATUS status;
    uint32 partNum;
    SIZE_T entries;
    PEXTENT_MAP released;
    BOOLEAN possiblyModified = FALSE;
    BOOLEAN txnInitialized = FALSE;

    if (!Vcb || !FileInfo || !FileInfo->Dloc || !FileInfo->Dloc->FileEntry ||
        !FileInfo->Dloc->DataLoc.Mapping || !FileInfo->Dloc->FELoc.Mapping ||
        !Vcb->SectorSize || Vcb->SectorShift >= 30 ||
        Vcb->SectorSize != ((uint32)1 << Vcb->SectorShift) || NewLength < 0)
        return STATUS_INVALID_PARAMETER;
    if (NewLength >= FileInfo->Dloc->DataLoc.Length ||
        UDFIsADirectory(FileInfo) ||
        (FileInfo->Dloc->FE_Flags & UDF_FE_FLAG_NEW_UNPUBLISHED))
        return UDFResizeFileUnordered__(IrpContext, Vcb, FileInfo, NewLength);

    /* No other allocator may see the freed RAM bits before the old FE is
     * durable.  This resource is recursively exclusive for calls below. */
    status = UDFDrainNewFileData(IrpContext, Vcb, FileInfo);
    if (!NT_SUCCESS(status)) return status;
    status = UDFOpenIntegrityForWrite(IrpContext, Vcb, FALSE);
    if (!NT_SUCCESS(status)) return status;
    entries = UDFGetMappingLength(FileInfo->Dloc->DataLoc.Mapping) /
              sizeof(EXTENT_MAP) +
              UDFGetMappingLength(FileInfo->Dloc->AllocLoc.Mapping) /
              sizeof(EXTENT_MAP) + 4;
    if (entries > 65536 || entries > ((SIZE_T)-1) / sizeof(EXTENT_MAP))
        return STATUS_NOT_SUPPORTED;
    released = (PEXTENT_MAP)MyAllocatePoolTag__(NonPagedPool,
                    entries * sizeof(EXTENT_MAP), MEM_EXTMAP_TAG);
    if (!released) return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(released, entries * sizeof(EXTENT_MAP));
    UDFAcquireResourceExclusive(&(Vcb->BitMapResource1), TRUE);
    _SEH2_TRY {
        if (Vcb->TruncateTxnOwner) {
            status = STATUS_NOT_SUPPORTED;
        } else {
            Vcb->TruncateTxnReleased = released;
            Vcb->TruncateTxnCapacity = (ULONG)(entries - 1);
            Vcb->TruncateTxnCount = 0;
            Vcb->TruncateTxnError = STATUS_SUCCESS;
            txnInitialized = TRUE;
            status = UDFTruncatePrefree(IrpContext, Vcb, FileInfo, NewLength,
                                        &possiblyModified);
            if (NT_SUCCESS(status)) {
                possiblyModified = TRUE;
                status = UDFBarrierFlushDevice(Vcb->TargetDeviceObject);
            }
            if (NT_SUCCESS(status)) {
                Vcb->TruncateTxnOwner = PsGetCurrentThread();
                status = UDFResizeFileUnordered__(IrpContext, Vcb,
                                                  FileInfo, NewLength);
            }
        }
        if (NT_SUCCESS(status) && !NT_SUCCESS(Vcb->TruncateTxnError))
            status = Vcb->TruncateTxnError;
        if (NT_SUCCESS(status)) {
            partNum = UDFGetRefPartNumByPhysLba(Vcb,
                        FileInfo->Dloc->FELoc.Mapping[0].extLocation);
            if (partNum == (uint32)-1)
                status = STATUS_FILE_CORRUPT_ERROR;
            else
                status = UDFFlushFE(IrpContext, Vcb, FileInfo, partNum);
        }
        if (NT_SUCCESS(status) && !NT_SUCCESS(Vcb->TruncateTxnError))
            status = Vcb->TruncateTxnError;
        if (NT_SUCCESS(status))
            status = UDFBarrierFlushDevice(Vcb->TargetDeviceObject);
        if (txnInitialized) {
            Vcb->TruncateTxnOwner = NULL;
            released[Vcb->TruncateTxnCount].extLength = 0;
            released[Vcb->TruncateTxnCount].extLocation = 0;
            if (NT_SUCCESS(status) && Vcb->TruncateTxnCount)
                UDFMarkSpaceAsXXX(Vcb, FileInfo->Dloc, released,
                                  AS_DISCARDED);
        }
    } _SEH2_FINALLY {
        if (_SEH2_AbnormalTermination() ||
            (!NT_SUCCESS(status) && possiblyModified))
            Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
        if (txnInitialized) {
            Vcb->TruncateTxnOwner = NULL;
            Vcb->TruncateTxnReleased = NULL;
            Vcb->TruncateTxnCapacity = 0;
            Vcb->TruncateTxnCount = 0;
        }
        UDFReleaseResource(&(Vcb->BitMapResource1));
        MyFreePool__(released);
    } _SEH2_END;
    return status;
}

/*
    This routine loads VAT.
 */
/* A decoded table owns its allocation independently of the live VCB.
 * Publish it only after the entire payload has passed bounds validation. */
typedef struct _UDF_VAT_IMAGE {
    uint32* Entries;
    uint32 Count;
    uint16 MinRead, MinWrite, MaxWrite;
    uint32 Files, Directories;
} UDF_VAT_IMAGE;

static NTSTATUS
UDFReadVATImage(PIRP_CONTEXT IrpContext, PVCB Vcb, PUDF_FILE_INFO FileInfo,
                uint16 MapType, uint32 Root, uint32 PartitionLength,
                UDF_VAT_IMAGE* Image)
{
    uint64 FileBytes;
    uint32 Offset, DataBytes, i;
    uint32* Entries = NULL;
    NTSTATUS Status;
    RtlZeroMemory(Image, sizeof(*Image));
    if (!FileInfo || !FileInfo->Dloc || !FileInfo->Dloc->FileEntry)
        return STATUS_FILE_CORRUPT_ERROR;
    FileBytes = UDFGetFileSize(FileInfo);
    if (FileBytes > MAXULONG || !PartitionLength || Root > Vcb->SessionEndLba)
        return STATUS_FILE_CORRUPT_ERROR;
    if (MapType == UDF_VIRTUAL_MAP15) {
        struct VirtualAllocationTable15 Trailer;
        if (FileBytes < sizeof(Trailer) ||
            ((icbtag*)(FileInfo->Dloc->FileEntry + 1))->fileType != UDF_FILE_TYPE_VAT15)
            return STATUS_FILE_CORRUPT_ERROR;
        DataBytes = (uint32)FileBytes - sizeof(Trailer);
        Status = UDFReadFile__(IrpContext, Vcb, FileInfo, DataBytes,
                               sizeof(Trailer), FALSE, (int8*)&Trailer);
        if (Status != STATUS_SUCCESS)
            return NT_SUCCESS(Status) ? STATUS_IO_DEVICE_ERROR : Status;
        if (RtlCompareMemory(Trailer.ident.ident, UDF_ID_ALLOC,
                             sizeof(UDF_ID_ALLOC) - 1) != sizeof(UDF_ID_ALLOC) - 1)
            return STATUS_FILE_CORRUPT_ERROR;
        Offset = 0;
        Image->MinRead = Image->MinWrite = Image->MaxWrite = 0x0150;
        Image->Files = Image->Directories = (uint32)-1;
    } else if (MapType == UDF_VIRTUAL_MAP20) {
        struct VirtualAllocationTable20 Header;
        if (FileBytes < sizeof(Header) ||
            ((icbtag*)(FileInfo->Dloc->FileEntry + 1))->fileType != UDF_FILE_TYPE_VAT20)
            return STATUS_FILE_CORRUPT_ERROR;
        Status = UDFReadFile__(IrpContext, Vcb, FileInfo, 0,
                               sizeof(Header), FALSE, (int8*)&Header);
        if (Status != STATUS_SUCCESS)
            return NT_SUCCESS(Status) ? STATUS_IO_DEVICE_ERROR : Status;
        if (Header.lengthHeader != sizeof(Header) + Header.lengthImpUse ||
            Header.lengthHeader > FileBytes || Header.reserved ||
            (Header.lengthImpUse &&
             (Header.lengthImpUse < 32 || (Header.lengthImpUse & 3))))
            return STATUS_FILE_CORRUPT_ERROR;
        if (Header.minReadRevision > 0x0201)
            return STATUS_NOT_SUPPORTED; /* no VAT 2.50 capability via its header */
        Offset = Header.lengthHeader;
        DataBytes = (uint32)FileBytes - Offset;
        Image->MinRead = Header.minReadRevision;
        Image->MinWrite = Header.minWriteRevision;
        Image->MaxWrite = Header.maxWriteRevision;
        Image->Files = Header.numFIDSFiles;
        Image->Directories = Header.numFIDSDirectories;
    } else {
        return STATUS_NOT_SUPPORTED;
    }
    if (DataBytes & (sizeof(uint32) - 1))
        return STATUS_FILE_CORRUPT_ERROR;
    Image->Count = DataBytes / sizeof(uint32);
    /* The physical partition length is an upper bound, not an allocation
     * request. No writer expands this immutable mounted table. */
    Entries = (uint32*)DbgAllocatePool(NonPagedPool, max(Image->Count, 1) * sizeof(uint32));
    if (!Entries)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlFillMemory(Entries, max(Image->Count, 1) * sizeof(uint32), 0xff);
    Status = STATUS_SUCCESS;
    _SEH2_TRY {
        if (DataBytes) {
            Status = UDFReadFile__(IrpContext, Vcb, FileInfo, Offset,
                                   DataBytes, FALSE, (int8*)Entries);
            if (Status != STATUS_SUCCESS) {
                if (NT_SUCCESS(Status)) Status = STATUS_IO_DEVICE_ERROR;
                _SEH2_LEAVE;
            }
        }
        for (i = 0; i < Image->Count; i++) {
            uint32 Physical = Entries[i];
            if (Physical != UDF_VAT_FREE_ENTRY &&
                (Physical >= PartitionLength ||
                 Physical > Vcb->SessionEndLba - Root)) {
                Status = STATUS_FILE_CORRUPT_ERROR;
                _SEH2_LEAVE;
            }
        }
        Image->Entries = Entries;
        Entries = NULL;
    } _SEH2_FINALLY {
        if (Entries) DbgFreePool(Entries);
    } _SEH2_END;
    return Status;
}

NTSTATUS
UDFLoadVAT(PIRP_CONTEXT IrpContext, PVCB Vcb, uint32 PartNdx)
{
    static const uint32 Delta[] = { 0, 2, 3, 5, 7 };
    UDF_VAT_IMAGE Image;
    lb_addr VatFELoc;
    uint32 i, Root, Candidate, PhysicalRef = LBA_OUT_OF_EXTENT;
    uint16 MapType;
    NTSTATUS Status = STATUS_FILE_CORRUPT_ERROR, CleanupStatus;
    PUDFPartMap Part;
    if (!Vcb->Partitions || PartNdx >= Vcb->PartitionMaps || PartNdx > MAXUSHORT)
        return STATUS_FILE_CORRUPT_ERROR;
    Part = &Vcb->Partitions[PartNdx];
    MapType = Part->PartitionType;
    if (MapType != UDF_VIRTUAL_MAP15 && MapType != UDF_VIRTUAL_MAP20)
        return STATUS_NOT_SUPPORTED;
    for (i = 0; i < Vcb->PartitionMaps; i++) {
        PUDFPartMap Physical = &Vcb->Partitions[i];
        if (Physical->PartitionType == UDF_TYPE1_MAP15 &&
            Physical->PartitionNum == Part->PartitionNum &&
            Physical->VolumeSeqNum == Part->VolumeSeqNum &&
            Physical->PartitionRoot == Part->PartitionRoot &&
            Physical->PartitionLen == Part->PartitionLen) {
            PhysicalRef = i;
            break;
        }
    }
    if (PhysicalRef == LBA_OUT_OF_EXTENT || PhysicalRef > MAXUSHORT)
        return STATUS_NOT_SUPPORTED;
    if (InterlockedCompareExchangePointer((PVOID volatile*)&Vcb->Vat, NULL, NULL)) {
        return Vcb->VatPartNdx == PartNdx && Vcb->VatPhysPartNdx == PhysicalRef &&
            Vcb->VatPartitionRoot == Part->PartitionRoot &&
            Vcb->VatPartitionLength == Part->PartitionLen &&
            Vcb->VatPartitionNumber == Part->PartitionNum &&
            Vcb->VatVolumeSequence == Part->VolumeSeqNum ? STATUS_SUCCESS : STATUS_WRONG_VOLUME;
    }
    if (Vcb->VatFileInfo)
        return STATUS_DEVICE_BUSY;
    Root = Part->PartitionRoot;
    if (!Part->PartitionLen || Root > Vcb->SessionEndLba)
        return STATUS_FILE_CORRUPT_ERROR;

    /* A VAT ICB is physical, even when the namespace ICBs it describes are
     * virtual. Use its explicit physical alias, never a global CDR toggle
     * that makes an unbound virtual partition look like physical space. */
    VatFELoc.partitionReferenceNum = (uint16)PhysicalRef;
    Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
    for (i = 0; i < RTL_NUMBER_OF(Delta); i++) {
        PUDF_FILE_INFO FileInfo;
        if (Vcb->SessionEndLba < Delta[i])
            continue;
        Candidate = Vcb->SessionEndLba - Delta[i];
        if (Candidate < Root || Candidate - Root >= Part->PartitionLen)
            continue;
        FileInfo = (PUDF_FILE_INFO)MyAllocatePoolTag__(UDF_FILE_INFO_MT,
                                      sizeof(*FileInfo), MEM_VATFINF_TAG);
        if (!FileInfo)
            return STATUS_INSUFFICIENT_RESOURCES;
        RtlZeroMemory(FileInfo, sizeof(*FileInfo));
        FileInfo->NextLinkedFile = FileInfo->PrevLinkedFile = FileInfo;
        Vcb->VatFileInfo = FileInfo;
        VatFELoc.logicalBlockNum = Candidate - Root;
        Status = UDFOpenRootFile__(IrpContext, Vcb, &VatFELoc, FileInfo);
        if (Status == STATUS_SUCCESS)
            Status = UDFReadVATImage(IrpContext, Vcb, FileInfo, MapType,
                                     Root, Part->PartitionLen, &Image);
        if (Status != STATUS_SUCCESS && NT_SUCCESS(Status))
            Status = STATUS_IO_DEVICE_ERROR;
        if (NT_SUCCESS(Status)) {
            Vcb->VatCapacity = max(Image.Count, 1);
            Vcb->VatPartNdx = PartNdx;
            Vcb->VatPhysPartNdx = PhysicalRef;
            Vcb->VatPartitionRoot = Root;
            Vcb->VatPartitionLength = Part->PartitionLen;
            Vcb->VatPartitionNumber = Part->PartitionNum;
            Vcb->VatVolumeSequence = Part->VolumeSeqNum;
            Vcb->InitVatCount = Vcb->VatCount = Image.Count;
            Vcb->minUDFReadRev = Image.MinRead;
            Vcb->minUDFWriteRev = Image.MinWrite;
            Vcb->maxUDFWriteRev = Image.MaxWrite;
            Vcb->numFiles = Image.Files;
            Vcb->numDirs = Image.Directories;
            Vcb->CDR_Mode = TRUE;
            /* Reader acquire observes the completed geometry/count tuple.
             * No live writer or namespace-free path mutates this generation. */
            InterlockedExchangePointer((PVOID volatile*)&Vcb->Vat, Image.Entries);
            UDFPrint(("UDFS: immutable VAT read-only view; sequential recording is not implemented\n"));
            return STATUS_SUCCESS;
        }
        UDFPrint(("UDFS: VAT candidate at %x rejected: %x\n", Candidate, Status));
        CleanupStatus = UDFReleaseInternalFile(IrpContext, Vcb, &Vcb->VatFileInfo);
        if (!NT_SUCCESS(CleanupStatus) || Vcb->VatFileInfo)
            return Status;
        if (Status == STATUS_INSUFFICIENT_RESOURCES || Status == STATUS_NOT_SUPPORTED)
            return Status;
    }
    return Status;
} // end UDFLoadVAT()

NTSTATUS
UDFOpenMetadataFile(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN uint32 PhysRef,
    IN uint32 FELbn,
    IN uint8 FileType,
    OUT PUDF_FILE_INFO* FileInfo
)
{
    NTSTATUS status = STATUS_UNSUCCESSFUL, ReleaseStatus;
    PUDF_FILE_INFO fi = NULL;
    lb_addr FELoc;

    if (!FileInfo) return STATUS_INVALID_PARAMETER;
    *FileInfo = NULL;
    if (!IrpContext || !Vcb || !Vcb->Partitions ||
        PhysRef >= Vcb->PartitionMaps || PhysRef > MAXUSHORT ||
        (FileType != UDF_FILE_TYPE_METADATA &&
         FileType != UDF_FILE_TYPE_METADATA_MIRROR &&
         FileType != UDF_FILE_TYPE_METADATA_BITMAP))
        return STATUS_INVALID_PARAMETER;
    fi = (PUDF_FILE_INFO)MyAllocatePoolTag__(UDF_FILE_INFO_MT,
                                            sizeof(UDF_FILE_INFO), MEM_VATFINF_TAG);
    if (!fi) return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(fi, sizeof(*fi));
    fi->NextLinkedFile = fi->PrevLinkedFile = fi;
    FELoc.partitionReferenceNum = (uint16)PhysRef;
    FELoc.logicalBlockNum = FELbn;

    _SEH2_TRY {
        _SEH2_TRY {
            status = UDFOpenRootFileWithParent__(IrpContext, Vcb, &FELoc,
                                                 NULL, FileType, fi);
            if (status != STATUS_SUCCESS && NT_SUCCESS(status)) status = STATUS_IO_DEVICE_ERROR;
        } _SEH2_EXCEPT(FsRtlIsNtstatusExpected(_SEH2_GetExceptionCode()) ?
                      EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
            status = _SEH2_GetExceptionCode();
        } _SEH2_END;
    } _SEH2_FINALLY {
        if (_SEH2_AbnormalTermination()) status = STATUS_FILE_CORRUPT_ERROR;
        if (status != STATUS_SUCCESS) {
            /* The type check precedes completion: this is a failed FI
             * constructor, never an ordinary open to close again. The
             * shared failure handoff retains its exact indexed hold and
             * keeps the caller's error output NULL even after an exception. */
            ReleaseStatus = UDFRetainFailedStreamOpen(Vcb, &fi);
            if (NT_SUCCESS(status) && !NT_SUCCESS(ReleaseStatus)) status = ReleaseStatus;
        }
    } _SEH2_END;
    if (status != STATUS_SUCCESS) return status;
    *FileInfo = fi;
    return STATUS_SUCCESS;
}

/* A named NonAlloc open owns a parent reference. Transfer checkpoint
 * access to a new parentless FI before closing that open, rather than
 * retaining a weak ParentFile into an already retired root/SystemSDir.
 * The Dloc is shared through the normal checked acquisition protocol. */
NTSTATUS
UDFHoldResidualNonAlloc(PVCB Vcb)
{
    PUDF_FILE_INFO Source = Vcb->NonAllocFileInfo, Hold;
    NTSTATUS Status;
    ASSERT_EXCLUSIVE_VCB(Vcb);
    if (Vcb->NonAllocCheckpointFileInfo || !Source)
        return STATUS_SUCCESS;
    if (Vcb->ResidualRetirementStarted || Source->InternalOpenReleased ||
        !Source->Dloc || !Source->RefCount || Source->Fcb ||
        Source->Dloc->CommonFcb || Source->Dloc->DirIndex ||
        Source->Dloc->DeleteCommitted)
        return STATUS_DEVICE_BUSY;
    Hold = (PUDF_FILE_INFO)MyAllocatePoolTag__(UDF_FILE_INFO_MT,
                                               sizeof(*Hold), MEM_FINF_TAG);
    if (!Hold)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Hold, sizeof(*Hold));
    Hold->Dloc = Source->Dloc;
    Status = UDFReferenceClonedFile(Vcb, Hold, Source);
    if (!NT_SUCCESS(Status)) {
        MyFreePool__(Hold); /* no reference or shared buffer was acquired */
        return Status;
    }
    Vcb->NonAllocCheckpointFileInfo = Hold;
    return STATUS_SUCCESS;
}

#define UDF_RESIDUAL_FILE_COUNT 5

static VOID
UDFResidualFileSlots(PVCB Vcb, PUDF_FILE_INFO** Slots)
{
    Slots[0] = &Vcb->NonAllocCheckpointFileInfo;
    Slots[1] = &Vcb->VatFileInfo;
    Slots[2] = &Vcb->MetadataBitmapFileInfo;
    Slots[3] = &Vcb->MetadataMirrorFileInfo;
    Slots[4] = &Vcb->MetadataFileInfo;
}

/* Both Dloc resources and the exclusive VCB are held. This is an exact
 * owner census, not permission to discard arbitrary clean cached FIs.
 * Only these five parentless, non-FCB internal opens may remain. */
static NTSTATUS
UDFCheckResidualFilesLocked(PVCB Vcb, BOOLEAN RequireClean)
{
    PUDF_FILE_INFO* Slots[UDF_RESIDUAL_FILE_COUNT];
    BOOLEAN Seen[UDF_RESIDUAL_FILE_COUNT] = { FALSE };
    uint32 i, j;
    NTSTATUS Status;
    if (Vcb->DlocCount && !Vcb->DlocList)
        return STATUS_FILE_CORRUPT_ERROR;
    if (Vcb->RootIndexFcb || Vcb->NonAllocFileInfo || Vcb->SysSDirFileInfo ||
        Vcb->UniqueIDMapFileInfo || Vcb->FcbTeardownCount ||
        Vcb->ClosedFcbRetryCount || Vcb->RetainedOpenCount || Vcb->PendingNamespaceCount ||
        Vcb->PendingDeleteCount || Vcb->RetainedDeleteCount ||
        Vcb->FailedZeroDataCount || Vcb->DeletePrefreeList)
        return STATUS_DEVICE_BUSY;
    Status = (NTSTATUS)InterlockedCompareExchange(&Vcb->MetadataWritebackError,
                                                  STATUS_SUCCESS, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return Status; /* reading this inventory must not consume its error */
    UDFResidualFileSlots(Vcb, Slots);
    for (i = 0; i < UDF_RESIDUAL_FILE_COUNT; i++) {
        PUDF_FILE_INFO FileInfo = *Slots[i];
        PUDF_DATALOC_INFO Dloc;
        uint32 Expected;
        if (!FileInfo)
            continue;
        Dloc = FileInfo->Dloc;
        Expected = FileInfo->InternalOpenReleased ? 0 : 1;
        if (!Dloc || !Dloc->FileEntry || !Dloc->FELoc.Mapping ||
            !Dloc->DataLoc.Mapping || FileInfo->ParentFile || FileInfo->Index ||
            FileInfo->FileIdent || FileInfo->ListPtr || FileInfo->Fcb ||
            FileInfo->OpenCount || FileInfo->RefCount != Expected ||
            (FileInfo->InternalOpenReleased && !Vcb->ResidualRetirementStarted) ||
            FileInfo->NextLinkedFile != FileInfo ||
            FileInfo->PrevLinkedFile != FileInfo ||
            Dloc->LinkedFileInfo != FileInfo || Dloc->LinkRefCount != Expected ||
            FileInfo->Constructing || FileInfo->ConstructionFailed ||
            FileInfo->ConstructionReferenceOwned || FileInfo->ConstructionParent ||
            FileInfo->TemporaryRetired || FileInfo->NamespaceStaged ||
            FileInfo->LinkDeleteCommitted || Dloc->CommonFcb ||
            Dloc->CleanupOwner || Dloc->AllocationReleaseActive ||
            !NT_SUCCESS(Dloc->AllocationReleaseError) || Dloc->DeleteCommitted || Dloc->DeletePrefree ||
            Dloc->DirIndex || Dloc->SDirInfo || Dloc->DirectoryAppend ||
            Dloc->DirectoryFECheckpoint || Dloc->NamespaceScratch ||
            Dloc->NamespaceTarget || Dloc->NamespaceFileInfo ||
            Dloc->NamespaceTargetState != UdfNamespaceTargetNone ||
            !NT_SUCCESS(Dloc->NamespaceTargetError) ||
            Dloc->NamespacePhase != UdfNamespaceNone ||
            Dloc->NamespaceScratchOwner || Dloc->StreamDeleteOwner ||
            Dloc->StreamDeleteContextOwner || Dloc->StreamCreate || !NT_SUCCESS(Dloc->ZeroDataError) ||
            (Dloc->FE_Flags & (UDF_FE_FLAG_UNDER_INIT | UDF_FE_FLAG_FREE_DEFERRED |
                UDF_FE_FLAG_NEW_UNPUBLISHED | UDF_FE_FLAG_CACHED_DATA_PENDING |
                UDF_FE_FLAG_HAS_SDIR | UDF_FE_FLAG_IS_SDIR |
                UDF_FE_FLAG_HAS_DEL_SDIR | UDF_FE_FLAG_IS_DEL_SDIR)))
            return STATUS_DEVICE_BUSY;
        if (RequireClean && (Dloc->RetiredExtents ||
            Dloc->DataLoc.Modified || Dloc->AllocLoc.Modified ||
            Dloc->FELoc.Modified || (Dloc->DataLoc.Flags & EXTENT_FLAG_PREALLOCATED) ||
            (Dloc->FE_Flags & (UDF_FE_FLAG_FE_MODIFIED | UDF_FE_FLAG_DIR_MODIFIED))))
            return STATUS_DEVICE_BUSY;
        for (j = 0; j < i; j++) {
            if (*Slots[j] && (*Slots[j])->Dloc == Dloc)
                return STATUS_FILE_CORRUPT_ERROR; /* not two independent owners */
        }
    }
    for (i = 0; i < Vcb->DlocCount; i++) {
        PUDF_DATALOC_INFO Dloc = Vcb->DlocList[i].Dloc;
        if (!Dloc)
            continue;
        for (j = 0; j < UDF_RESIDUAL_FILE_COUNT; j++) {
            if (*Slots[j] && (*Slots[j])->Dloc == Dloc)
                break;
        }
        if (j == UDF_RESIDUAL_FILE_COUNT)
            return STATUS_DEVICE_BUSY; /* an unaccounted lifetime, even if clean */
        if (Seen[j])
            return STATUS_FILE_CORRUPT_ERROR;
        Seen[j] = TRUE;
    }
    for (i = 0; i < UDF_RESIDUAL_FILE_COUNT; i++) {
        if (*Slots[i] && !Seen[i])
            return STATUS_FILE_CORRUPT_ERROR;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS
UDFCheckResidualFiles(PVCB Vcb, BOOLEAN RequireClean)
{
    NTSTATUS Status;
    UDFAcquireResourceExclusive(&Vcb->DlocResource, TRUE);
    UDFAcquireResourceExclusive(&Vcb->DlocResource2, TRUE);
    Status = UDFCheckResidualFilesLocked(Vcb, RequireClean);
    UDFReleaseResource(&Vcb->DlocResource2);
    UDFReleaseResource(&Vcb->DlocResource);
    return Status;
}

NTSTATUS
UDFCheckpointResidualFiles(PIRP_CONTEXT IrpContext, PVCB Vcb)
{
    PUDF_FILE_INFO* Slots[UDF_RESIDUAL_FILE_COUNT];
    NTSTATUS Status;
    BOOLEAN CloseDeferred = FALSE;
    uint32 i;
    ASSERT_EXCLUSIVE_VCB(Vcb);
    if (Vcb->ResidualCheckpointComplete && !Vcb->Modified &&
        !Vcb->BitmapModified && !Vcb->MetadataBitmapModified &&
        !Vcb->PreallocCache && !Vcb->PreallocCacheMaxSize)
        return UDFCheckResidualFiles(Vcb, TRUE);
    if (Vcb->ResidualRetirementStarted ||
        (Vcb->BitmapFcb && Vcb->BitmapCacheState != UdfBitmapCacheActive))
        return STATUS_FILE_CORRUPT_ERROR; /* never reopen a detached cache */
    Vcb->ResidualCheckpointComplete = FALSE;
    Status = UDFCheckResidualFiles(Vcb, FALSE);
    if (!NT_SUCCESS(Status))
        return Status;
    /* No new generation was produced after the previous checkpoint. This
     * also permits memory-only unwind of an unpublished, never-written
     * mount with no target device; do not invent a device flush there. */
    if (!Vcb->Modified && !Vcb->BitmapModified && !Vcb->MetadataBitmapModified &&
        !Vcb->PreallocCache && !Vcb->PreallocCacheMaxSize &&
        UDFCheckResidualFiles(Vcb, TRUE) == STATUS_SUCCESS) {
        Vcb->ResidualCheckpointComplete = TRUE;
        return STATUS_SUCCESS;
    }
    if (Vcb->VcbState & VCB_STATE_VOLUME_READ_ONLY) {
        /* Read-only closes can retire clean memory, not repair dirty media. */
        Status = UDFCheckResidualFiles(Vcb, TRUE);
        if (!NT_SUCCESS(Status))
            return Status;
        if (Vcb->Modified || Vcb->BitmapModified || Vcb->MetadataBitmapModified)
            return STATUS_MEDIA_WRITE_PROTECTED;
        if (Vcb->PreallocCache || Vcb->PreallocCacheMaxSize)
            return STATUS_DEVICE_BUSY;
    } else {
        if (!Vcb->TargetDeviceObject)
            return STATUS_DEVICE_NOT_CONNECTED;
        Status = UDFFlushAllCachedAllocations(Vcb, UDF_PREALLOC_CLASS_DIR);
        if (!NT_SUCCESS(Status))
            return Status;
        UDFResidualFileSlots(Vcb, Slots);
        for (i = 0; i < UDF_RESIDUAL_FILE_COUNT; i++) {
            if (*Slots[i]) {
                Status = UDFFlushFile__(IrpContext, Vcb, *Slots[i], 0);
                if (!NT_SUCCESS(Status))
                    return Status;
            }
        }
        Status = UDFFlushMetadataBitmap(IrpContext, Vcb);
        if (!NT_SUCCESS(Status))
            return Status;
        Status = UDFCheckResidualFiles(Vcb, TRUE);
        if (!NT_SUCCESS(Status))
            return Status;
        /* Do not let a metadata-only dirty flag bypass UDFUmount__'s gate. */
        if (!Vcb->Modified && (Vcb->BitmapModified || Vcb->MetadataBitmapModified))
            UDFSetModified(Vcb);
        Status = UDFUmount__(IrpContext, Vcb, &CloseDeferred);
        if (!NT_SUCCESS(Status))
            return Status;
        if (CloseDeferred)
            return STATUS_DEVICE_BUSY;
        Status = UDFCheckResidualFiles(Vcb, TRUE);
        if (!NT_SUCCESS(Status))
            return Status;
        if (Vcb->MetadataBitmapModified)
            return STATUS_DEVICE_BUSY;
        Status = UDFBarrierFlushDevice(Vcb->TargetDeviceObject);
        if (!NT_SUCCESS(Status))
            return Status;
        UDFPreClrModified(Vcb);
        UDFClrModified(Vcb);
    }
    Vcb->ResidualCheckpointComplete = TRUE;
    return STATUS_SUCCESS;
}

static VOID
UDFReleaseMetadataBuffers(PVCB Vcb)
{
    UDFInstallMetadataView(Vcb, NULL);
    if (Vcb->MetadataBitmap) {
        DbgFreePool(Vcb->MetadataBitmap);
        Vcb->MetadataBitmap = NULL;
    }
    if (Vcb->MetadataPendingAlloc) {
        DbgFreePool(Vcb->MetadataPendingAlloc);
        Vcb->MetadataPendingAlloc = NULL;
    }
    Vcb->MetadataBitmapByteCount = 0;
    Vcb->MetadataBitCount = 0;
    Vcb->MetadataFreeCount = 0;
    Vcb->MetadataHint = 0;
    Vcb->MetadataBitmapModified = FALSE;
}

/* This terminal phase owns only clean parentless internal opens. Detach
 * them from the Dloc index under its two resources, consume each acquired
 * hold once, and release memory. No generic cleanup, allocation freeing,
 * bitmap pinning (including debug checks), FE flush or device I/O here. */
NTSTATUS
UDFRetireResidualFiles(PVCB Vcb)
{
    PUDF_FILE_INFO* Slots[UDF_RESIDUAL_FILE_COUNT];
    NTSTATUS Status;
    uint32 i, j;
    ASSERT_EXCLUSIVE_VCB(Vcb);
    if (!Vcb->ResidualCheckpointComplete || Vcb->Modified || Vcb->BitmapModified ||
        Vcb->MetadataBitmapModified || Vcb->BitmapFcb ||
        Vcb->BitmapStreamFileObject || Vcb->BitmapCacheState != UdfBitmapCacheAbsent ||
        Vcb->PreallocCache || Vcb->PreallocCacheMaxSize)
        return STATUS_INVALID_DEVICE_STATE;
    UDFAcquireResourceExclusive(&Vcb->DlocResource, TRUE);
    UDFAcquireResourceExclusive(&Vcb->DlocResource2, TRUE);
    _SEH2_TRY {
        Status = UDFCheckResidualFilesLocked(Vcb, TRUE);
        if (!NT_SUCCESS(Status))
            _SEH2_LEAVE;
        Vcb->ResidualRetirementStarted = TRUE;
        UDFResidualFileSlots(Vcb, Slots);
        for (i = 0; i < UDF_RESIDUAL_FILE_COUNT; i++) {
            PUDF_FILE_INFO FileInfo = *Slots[i];
            if (FileInfo && !FileInfo->InternalOpenReleased) {
                Status = UDFDereferenceFileLocked(Vcb, FileInfo);
                if (!NT_SUCCESS(Status))
                    _SEH2_LEAVE;
                FileInfo->InternalOpenReleased = TRUE;
            }
        }
        for (i = 0; i < UDF_RESIDUAL_FILE_COUNT; i++) {
            PUDF_FILE_INFO FileInfo = *Slots[i];
            PUDF_DATALOC_INFO Dloc;
            if (!FileInfo)
                continue;
            Dloc = FileInfo->Dloc;
            for (j = 0; j < Vcb->DlocCount; j++) {
                if (Vcb->DlocList[j].Dloc == Dloc)
                    break;
            }
            /* Membership was proved before any destructive action. */
            NT_ASSERT(j < Vcb->DlocCount);
            if (j == Vcb->DlocCount) {
                Status = STATUS_FILE_CORRUPT_ERROR;
                _SEH2_LEAVE; /* keep the remaining consumed, indexed holds */
            }
            RtlZeroMemory(&Vcb->DlocList[j], sizeof(Vcb->DlocList[j]));
            Vcb->DlocFreeHint = min(Vcb->DlocFreeHint, j);
            *Slots[i] = NULL;
            if (Dloc->DataLoc.Mapping) MyFreePool__(Dloc->DataLoc.Mapping);
            if (Dloc->AllocLoc.Mapping) MyFreePool__(Dloc->AllocLoc.Mapping);
            if (Dloc->FELoc.Mapping) MyFreePool__(Dloc->FELoc.Mapping);
            MyFreePool__(Dloc->FileEntry);
            MyFreePool__(Dloc);
            MyFreePool__(FileInfo);
        }
        UDFReleaseMetadataBuffers(Vcb);
    } _SEH2_FINALLY {
        UDFReleaseResource(&Vcb->DlocResource2);
        UDFReleaseResource(&Vcb->DlocResource);
    } _SEH2_END;
    return Status;
}

NTSTATUS
UDFReleaseMetadata(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb
)
{
    PUDF_FILE_INFO* Files[3] = {
        &Vcb->MetadataBitmapFileInfo,
        &Vcb->MetadataMirrorFileInfo,
        &Vcb->MetadataFileInfo
    };
    NTSTATUS Status = STATUS_SUCCESS;
    uint32 i;
    for (i = 0; i < 3; i++) {
        NTSTATUS Current = UDFReleaseInternalFile(IrpContext, Vcb, Files[i]);
        if (NT_SUCCESS(Status)) Status = Current;
    }
    if (!NT_SUCCESS(Status))
        return Status;

    UDFReleaseMetadataBuffers(Vcb);
    return STATUS_SUCCESS;
}

NTSTATUS
UDFLoadMetadata(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN uint32 PartNdx
    )
{
    NTSTATUS status;
    PSPACE_BITMAP_DESC Sbd;
    PUDF_METADATA_VIEW View = NULL;
    uint32 i, len, n, PhysRef = (uint32)-1;

    if (Vcb->MetadataFileInfo) {
        View = UDFAcquireMetadataView(Vcb);
        if (!View) return STATUS_FILE_CORRUPT_ERROR;
        UDFReleaseMetadataView(View);
        return STATUS_SUCCESS;
    }

    for (i = 0; i < Vcb->PartitionMaps; i++) {
        if (
            (Vcb->Partitions[i].PartitionNum == Vcb->Partitions[PartNdx].PartitionNum) &&
            (
                (Vcb->Partitions[i].PartitionType == UDF_TYPE1_MAP15) ||
                (Vcb->Partitions[i].PartitionType == UDF_SPARABLE_MAP15)
            )
        ) {
            PhysRef = i;
            break;
        }
    }

    if (PhysRef == (uint32)-1) {
        UDFPrint(("UDFLoadMetadata: metadata map physical partition not found\n"));
        return STATUS_DISK_CORRUPT_ERROR;
    }

    Vcb->MetadataPhysRef = PhysRef;

    status = UDFOpenMetadataFile(IrpContext, Vcb, PhysRef, Vcb->MetadataFELoc,
                                UDF_FILE_TYPE_METADATA, &Vcb->MetadataFileInfo);
    
    if(!NT_SUCCESS(status)) {
        UDFPrint(("UDFLoadMetadata: failed to load metadata: %x\n", status));
        return status;
    }
    
    Vcb->Partitions[PartNdx].PartitionLen =
        (uint32)(UDFGetFileSize(Vcb->MetadataFileInfo) >> Vcb->SectorShift);

    status = UDFOpenMetadataFile(IrpContext, Vcb, PhysRef, Vcb->MetadataMirrorFELoc,
                                UDF_FILE_TYPE_METADATA_MIRROR, &Vcb->MetadataMirrorFileInfo);
    if ( !NT_SUCCESS(status)){
        UDFPrint(("UDFLoadMetadata: failed to load metadata mirror: %x\n", status));
        goto err_metadata;
    }

    if (Vcb->MetadataBitmapFELoc != 0xFFFFFFFF) {
        status = UDFOpenMetadataFile(IrpContext, Vcb, PhysRef, Vcb->MetadataBitmapFELoc,
                                    UDF_FILE_TYPE_METADATA_BITMAP, &Vcb->MetadataBitmapFileInfo);
        if(!NT_SUCCESS(status)) {
            UDFPrint(("UDFLoadMetadata: failed to load metadata bitmap: %x\n", status));
            goto err_metadata;
        }

        len = (uint32)UDFGetFileSize(Vcb->MetadataBitmapFileInfo);
        if (len < sizeof(SPACE_BITMAP_DESC)) {
            status = STATUS_DISK_CORRUPT_ERROR;
            goto err_metadata;
        }

        Sbd = (PSPACE_BITMAP_DESC)DbgAllocatePool(NonPagedPool, len);
        if (!Sbd) {
            status = STATUS_INSUFFICIENT_RESOURCES;
            goto err_metadata;
        }

        status = UDFReadFile__(IrpContext, Vcb, Vcb->MetadataBitmapFileInfo, 0, 
            len, FALSE, (int8*)Sbd);
        if (
            NT_SUCCESS(status) &&
            (
                (Sbd->descTag.tagIdent != TID_SPACE_BITMAP_DESC) ||
                (Sbd->numOfBytes > len - sizeof(SPACE_BITMAP_DESC)) ||
                (Sbd->numOfBits > Sbd->numOfBytes * 8)
            )
        ) {
            UDFPrint(("UDFLoadMetadata: corrupted metadata bitmap: tag %x, %x bits, %x bytes\n",
                Sbd->descTag.tagIdent, Sbd->numOfBits, Sbd->numOfBytes));
            status = STATUS_DISK_CORRUPT_ERROR;
        }

        if (!NT_SUCCESS(status)) {
            DbgFreePool(Sbd);
            goto err_metadata;
        }

        Vcb->MetadataBitmap = (PCHAR)DbgAllocatePool(NonPagedPool, Sbd->numOfBytes);
        if (!Vcb->MetadataBitmap) {
            DbgFreePool(Sbd);
            status = STATUS_INSUFFICIENT_RESOURCES;
            goto err_metadata;
        }
        Vcb->MetadataPendingAlloc = (PCHAR)DbgAllocatePool(NonPagedPool,
                                                            Sbd->numOfBytes);
        if (!Vcb->MetadataPendingAlloc) {
            DbgFreePool(Sbd);
            status = STATUS_INSUFFICIENT_RESOURCES;
            goto err_metadata;
        }
        RtlZeroMemory(Vcb->MetadataPendingAlloc, Sbd->numOfBytes);
        
        RtlCopyMemory(Vcb->MetadataBitmap, Sbd+1, Sbd->numOfBytes);
        Vcb->MetadataBitmapByteCount = Sbd->numOfBytes;
        Vcb->MetadataBitCount = min(Sbd->numOfBits, Vcb->Partitions[PartNdx].PartitionLen);
        
        DbgFreePool(Sbd);
        for (i = 0, n = 0; i < Vcb->MetadataBitCount; i++) {
            if (Vcb->MetadataBitmap[i >> 3] & (1 << (i & 7)))
                n++;
        }
        Vcb->MetadataFreeCount = n;
        Vcb->MetadataHint = 0;
    }

    status = UDFCreateMetadataView(Vcb,
        Vcb->MetadataFileInfo->Dloc->DataLoc.Mapping,
        Vcb->MetadataMirrorFileInfo->Dloc->DataLoc.Mapping, &View);
    if (!NT_SUCCESS(status))
        goto err_metadata;
    UDFInstallMetadataView(Vcb, View); /* before any metadata-partition FSD lookup */
    View = NULL;
    Vcb->MetadataBitmapModified = FALSE;
    UDFPrint(("UDFLoadMetadata: metadata partition: ref %x, %x blocks, %x free\n",
        PartNdx, Vcb->Partitions[PartNdx].PartitionLen, Vcb->MetadataFreeCount));
    
    return STATUS_SUCCESS;

err_metadata:
    UDFReleaseMetadata(IrpContext, Vcb);
    return status;
}

NTSTATUS
UDFFlushMetadataBitmap(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb
)
{
    NTSTATUS status;
    PSPACE_BITMAP_DESC Sbd;
    uint32 len;
    SIZE_T written = 0;

    if (!Vcb->MetadataBitmapModified)
        return STATUS_SUCCESS;

    if (!Vcb->MetadataBitmap || !Vcb->MetadataPendingAlloc ||
        !Vcb->MetadataBitmapFileInfo)
        return STATUS_FILE_CORRUPT_ERROR;

    len = (uint32)UDFGetFileSize(Vcb->MetadataBitmapFileInfo);
    if (len < sizeof(SPACE_BITMAP_DESC) ||
        Vcb->MetadataBitmapByteCount > len - sizeof(SPACE_BITMAP_DESC))
        return STATUS_FILE_CORRUPT_ERROR;

    /* An allocation or deletion may have made an FE/FID dirty before this
     * bitmap is serialized. Keep an OPEN integrity checkpoint durable while
     * its owners are drained to the lower device. */
    status = UDFOpenIntegrityForWrite(IrpContext, Vcb, FALSE);
    if (!NT_SUCCESS(status))
        return status;

    Sbd = (PSPACE_BITMAP_DESC)DbgAllocatePool(NonPagedPool, len);
    if (!Sbd)
        return STATUS_INSUFFICIENT_RESOURCES;

    /* Serialize the RAM copy and the disk-only prefree overlay under the
     * allocator lock.  Otherwise this whole-bitmap flush could silently
     * turn a prepared FREE bit back to USED before final FCB teardown. */
    UDFAcquireResourceExclusive(&(Vcb->BitMapResource1), TRUE);
    _SEH2_TRY {
        status = UDFReadFile__(IrpContext, Vcb,
                               Vcb->MetadataBitmapFileInfo,
                               0, len, FALSE, (int8*)Sbd);
        if (NT_SUCCESS(status)) {
            if (Sbd->descTag.tagIdent != TID_SPACE_BITMAP_DESC ||
                Sbd->numOfBytes > len - sizeof(SPACE_BITMAP_DESC) ||
                Sbd->numOfBits > Sbd->numOfBytes * 8) {
                status = STATUS_FILE_CORRUPT_ERROR;
            } else {
                uint32 descriptorLba = UDFExtentOffsetToLba(
                    Vcb, Vcb->MetadataBitmapFileInfo->Dloc->DataLoc.Mapping,
                    Vcb->MetadataBitmapFileInfo->Dloc->DataLoc.Offset,
                    NULL, NULL, NULL, NULL);
                if (descriptorLba == LBA_OUT_OF_EXTENT) {
                    status = STATUS_FILE_CORRUPT_ERROR;
                    goto done_metadata_bitmap;
                }
                Sbd->numOfBits = Vcb->MetadataBitCount;
                Sbd->numOfBytes = Vcb->MetadataBitmapByteCount;
                RtlCopyMemory(Sbd + 1, Vcb->MetadataBitmap,
                              Vcb->MetadataBitmapByteCount);
                status = UDFOverlayDeletePrefreeMetadata(
                    Vcb, (int8*)(Sbd + 1), Vcb->MetadataBitCount);
                if (!NT_SUCCESS(status))
                    goto done_metadata_bitmap;
                /* Publish allocation/free bits only after the preceding
                 * FE/FID owners are durable. UDFUmount__ runs later, so its
                 * pre-physical-bitmap barrier cannot order this write. */
                status = UDFBarrierFlushDevice(Vcb->TargetDeviceObject);
                if (!NT_SUCCESS(status))
                    goto done_metadata_bitmap;
                // ECMA 167 4/14.12: the CRC covers only the two bitmap length
                // fields.  The bitmap payload is mutable and excluded from it.
                UDFSetUpTag(Vcb, &Sbd->descTag,
                            sizeof(SPACE_BITMAP_DESC),
                            UDFPhysLbaToPart(Vcb, Vcb->MetadataPhysRef,
                                             descriptorLba), 0);
                status = UDFWriteFile__(IrpContext, Vcb,
                                        Vcb->MetadataBitmapFileInfo, 0, len,
                                        FALSE, (int8*)Sbd, &written);
                if (NT_SUCCESS(status) && written != len)
                    status = STATUS_IO_DEVICE_ERROR;
                // The bitmap may be embedded in its File Entry.  Flush that FE
                // before UDFUmount__ closes the LVID; closing the FileInfo later
                // must not reopen a volume whose metadata is already committed.
                if (NT_SUCCESS(status))
                    status = UDFFlushFile__(IrpContext, Vcb,
                                            Vcb->MetadataBitmapFileInfo, 0);
                /* Retire per-bit allocation provenance only after the
                 * serialized bitmap generation reaches the lower device.
                 * A successful cache/FE flush alone does not guarantee it. */
                if (NT_SUCCESS(status))
                    status = UDFBarrierFlushDevice(Vcb->TargetDeviceObject);
                if (NT_SUCCESS(status)) {
                    RtlZeroMemory(Vcb->MetadataPendingAlloc,
                                  Vcb->MetadataBitmapByteCount);
                    Vcb->MetadataBitmapModified = FALSE;
                }
            }
        }

done_metadata_bitmap:
        NOTHING;
    } _SEH2_FINALLY {
        UDFReleaseResource(&(Vcb->BitMapResource1));
        DbgFreePool(Sbd);
    } _SEH2_END;
    return status;
}

/*
    Reads Extended Attributes
    Caller should use UDFGetFileEALength to allocate Buffer of sufficient
    size
 *//*
NTSTATUS
UDFReadFileEA(
    IN PVCB Vcb,
    IN PDIR_INDEX FileDirNdx,
    OUT int8* Buffer
    )
{
    PFILE_ENTRY FileEntry;
    NTSTATUS status;

    if (FileDirNdx->FileInfo) {
        FileEntry = (PFILE_ENTRY)(FileDirNdx->FileInfo->Dloc->FileEntry);
    } else {
        FileEntry = (PFILE_ENTRY)MyAllocatePool__(NonPagedPool, Vcb->BlockSize);
        if (!FileEntry) return;
        if (!NT_SUCCESS(status = UDFReadFileEntry(Vcb, &(FileDirNdx->FileEntry), FileEntry, &Ident))) {
            MyFreePool__(FileEntry);
            return status;
        }
    }
}*/
/*
    This dumb routine checks if the file has been found is deleted
    It is introduced to make main modules FS-type independent
 */
/*BOOLEAN
UDFIsDeleted(
    IN PDIR_INDEX DirNdx
    )
{
    return (DirNdx->FileCharacteristics & FILE_DELETED);
} */

/*BOOLEAN
UDFIsADirectory(
    IN PUDF_FILE_INFO FileInfo
    )
{
    ValidateFileInfo(FileInfo);

    if (!FileInfo) return FALSE;
    if (FileInfo->Dloc->DirIndex) return TRUE;
    if (!(FileInfo->FileIdent)) return FALSE;
    return (FileInfo->FileIdent->fileCharacteristics & FILE_DIRECTORY);
} */
/*
    This routine calculates actual allocation size
 */
/*int64
UDFGetFileAllocationSize(
    IN PVCB Vcb,
    IN PUDF_FILE_INFO FileInfo
    )
{
    ValidateFileInfo(FileInfo);

    return UDFGetExtentLength(FileInfo->Dloc->DataLoc.Mapping);// +
//           UDFGetExtentLength(FileInfo->Dloc->FELoc.Mapping) +
//           UDFGetExtentLength(FileInfo->Dloc->AllocLoc.Mapping) - Vcb->BlockSize;
}*/

/*
    This routine checks if the directory is empty
 */
BOOLEAN
UDFIsDirEmpty(
    IN PDIR_INDEX_HDR hCurDirNdx
    )
{
    uint32 fc;
    uint_di i;
    PDIR_INDEX_ITEM CurDirNdx;
    // not empty
    for(i=2; (CurDirNdx = UDFDirIndex(hCurDirNdx,i)); i++) {
        fc = CurDirNdx->FileCharacteristics;
        if (!(fc & (FILE_PARENT | FILE_DELETED)) &&
           CurDirNdx->Length)
            return FALSE;
    }
    return TRUE;
} // end UDFIsDirEmpty()

/*
    Calc logicalBlocksRecorded from recorded extents of mapping (zero for in ICB data)
*/
static NTSTATUS
UDFSetRecordedBlocks(PVCB Vcb, PUDF_FILE_INFO FileInfo)
{
    PUDF_DATALOC_INFO Dloc = FileInfo->Dloc;
    uint32 Bytes, Count, i;
    uint64 Total = 0, Blocks, Previous;
    uint16 Mode = ((PFILE_ENTRY)Dloc->FileEntry)->icbTag.flags & ICB_FLAG_ALLOC_MASK;
    NTSTATUS Status;
    if (Mode != ICB_FLAG_AD_IN_ICB) {
        Status = UDFGetMappingByteCount(Dloc->DataLoc.Mapping, &Bytes);
        if (!NT_SUCCESS(Status)) return Status;
        Count = Bytes / sizeof(EXTENT_MAP) - 1;
        for (i = 0; i < Count; i++) {
            if ((Dloc->DataLoc.Mapping[i].extLength >> 30) != EXTENT_RECORDED_ALLOCATED)
                continue;
            Blocks = ((uint64)(Dloc->DataLoc.Mapping[i].extLength & UDF_EXTENT_LENGTH_MASK) +
                       Vcb->SectorSize - 1) >> Vcb->SectorShift;
            if (Total > (uint64)MAXLONGLONG - Blocks) return STATUS_FILE_CORRUPT_ERROR;
            Total += Blocks;
        }
    }
    if (Dloc->FileEntry->tagIdent == TID_FILE_ENTRY) {
        Previous = ((PFILE_ENTRY)Dloc->FileEntry)->logicalBlocksRecorded;
        ((PFILE_ENTRY)Dloc->FileEntry)->logicalBlocksRecorded = Total;
    } else {
        Previous = ((PEXTENDED_FILE_ENTRY)Dloc->FileEntry)->logicalBlocksRecorded;
        ((PEXTENDED_FILE_ENTRY)Dloc->FileEntry)->logicalBlocksRecorded = Total;
    }
    if (Previous != Total) Dloc->FE_Flags |= UDF_FE_FLAG_FE_MODIFIED;
    return STATUS_SUCCESS;
}

static
BOOLEAN
UDFAllocDescsStartInFEBlock(
    IN PVCB Vcb,
    IN PUDF_FILE_INFO FileInfo
)
{
    uint16 AllocMode;
    
    AllocMode = ((PFILE_ENTRY)(FileInfo->Dloc->FileEntry))->icbTag.flags & ICB_FLAG_ALLOC_MASK;

    return (
        FileInfo->Dloc->AllocLoc.Mapping &&
        AllocMode != ICB_FLAG_AD_IN_ICB) &&
           (FileInfo->Dloc->AllocLoc.Mapping[0].extLocation == FileInfo->Dloc->FELoc.Mapping[0].extLocation) &&
           (FileInfo->Dloc->AllocLoc.Offset == FileInfo->Dloc->FileEntryLen) &&
           (FileInfo->Dloc->AllocLoc.Offset < Vcb->SectorSize
    );
} 

static
BOOLEAN
UDFAllocDescsInFEBlock(
    IN PVCB Vcb,
    IN PUDF_FILE_INFO FileInfo
)
{
    return UDFAllocDescsStartInFEBlock(Vcb, FileInfo) &&
           ((FileInfo->Dloc->AllocLoc.Offset + FileInfo->Dloc->AllocLoc.Length) <= Vcb->SectorSize);
}

static
NTSTATUS
UDFSyncInIcbData(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN PUDF_FILE_INFO FileInfo
)
{
    uint32 FELen = FileInfo->Dloc->FileEntryLen;
    int8* FEBuf = (int8*)(FileInfo->Dloc->FileEntry);

    if (FELen + FileInfo->Dloc->DataLoc.Length 
        > Vcb->SectorSize)
        return STATUS_FILE_CORRUPT_ERROR;

    RtlZeroMemory(FEBuf + FELen, Vcb->SectorSize - FELen);
    if (!FileInfo->Dloc->DataLoc.Length)
        return STATUS_SUCCESS;

    return UDFReadExtent(IrpContext, Vcb, 
        &FileInfo->Dloc->DataLoc, 0, 
        (uint32)(FileInfo->Dloc->DataLoc.Length), 
        FALSE, FEBuf + FELen
    );
}

static NTSTATUS
UDFSerializeFEBlock(PVCB Vcb, PUDF_FILE_INFO FileInfo, uint32 PartNum, uint32* WriteLength)
{
    PUDF_DATALOC_INFO Dloc;
    uint32 Header, Tail, Lba, Location;
    uint64 Information;
    NTSTATUS Status;
    if (!WriteLength) return STATUS_INVALID_PARAMETER;
    *WriteLength = 0;
    Status = UDFValidateFileEntryBuffer(Vcb, FileInfo, &Header);
    if (!NT_SUCCESS(Status)) return Status;
    Dloc = FileInfo->Dloc;
    if (!Vcb->Partitions || PartNum >= Vcb->PartitionMaps ||
        Vcb->SectorSize > MAXUSHORT || !Dloc->FELoc.Mapping || Dloc->FELoc.Offset ||
        Dloc->FELoc.Length < 0 || (uint64)Dloc->FELoc.Length > Vcb->SectorSize ||
        Dloc->DataLoc.Length < 0 ||
        (Dloc->FELoc.Mapping[0].extLength & UDF_EXTENT_LENGTH_MASK) != Vcb->SectorSize ||
        (Dloc->FELoc.Mapping[0].extLength >> 30) > EXTENT_NOT_RECORDED_ALLOCATED ||
        Dloc->FELoc.Mapping[1].extLength)
        return STATUS_FILE_CORRUPT_ERROR;
    Lba = Dloc->FELoc.Mapping[0].extLocation;
    Location = UDFPhysLbaToPart(Vcb, PartNum, Lba);
    if (Location == LBA_OUT_OF_EXTENT || Location >= Vcb->Partitions[PartNum].PartitionLen)
        return STATUS_FILE_CORRUPT_ERROR;
    Status = UDFSetAllocDescLen(Vcb, FileInfo);
    if (!NT_SUCCESS(Status)) return Status;
    Status = UDFSetRecordedBlocks(Vcb, FileInfo);
    if (!NT_SUCCESS(Status)) return Status;
    if (Dloc->FileEntry->tagIdent == TID_FILE_ENTRY) {
        Information = ((PFILE_ENTRY)Dloc->FileEntry)->informationLength;
        Tail = ((PFILE_ENTRY)Dloc->FileEntry)->lengthAllocDescs;
    } else {
        Information = ((PEXTENDED_FILE_ENTRY)Dloc->FileEntry)->informationLength;
        Tail = ((PEXTENDED_FILE_ENTRY)Dloc->FileEntry)->lengthAllocDescs;
    }
    if (Information > MAXLONGLONG || Tail > Vcb->SectorSize - Header)
        return STATUS_FILE_CORRUPT_ERROR;
    if (Information != (uint64)Dloc->DataLoc.Length)
        UDFSetFileSize(FileInfo, Dloc->DataLoc.Length);
    RtlZeroMemory((int8*)Dloc->FileEntry + Header + Tail, Vcb->SectorSize - Header - Tail);
    UDFSetUpTag(Vcb, Dloc->FileEntry, (uint16)(Header + Tail), Location, 0);
    *WriteLength = Vcb->SectorSize;
    return STATUS_SUCCESS;
}

static
NTSTATUS
UDFWriteFEBlock(PIRP_CONTEXT IrpContext, PVCB Vcb,
    PUDF_FILE_INFO FileInfo, uint32 PartNum)
{
    PUDF_DATALOC_INFO Dloc = FileInfo->Dloc;
    uint32 WriteLen;
    SIZE_T WrittenBytes = 0;
    NTSTATUS status = UDFStreamCreateWriteStatus(FileInfo);
    if (status != STATUS_SUCCESS) return status;
    status = UDFFlushDirectoryAppend(IrpContext, Vcb, FileInfo);
    if (!NT_SUCCESS(status)) {
        UDFRememberMetadataWritebackError(IrpContext, Vcb, FileInfo, status);
        return status;
    }
    /* External maps require AD preparation and initialization ordering in
     * UDFFlushFEInternal. The direct inline-data caller cannot bypass it. */
    if ((((PFILE_ENTRY)Dloc->FileEntry)->icbTag.flags & ICB_FLAG_ALLOC_MASK) != ICB_FLAG_AD_IN_ICB &&
        (Dloc->DataLoc.Modified || Dloc->AllocLoc.Modified))
        return STATUS_DEVICE_BUSY;
    status = UDFSerializeFEBlock(Vcb, FileInfo, PartNum, &WriteLen);
    if (!NT_SUCCESS(status)) return status;
    /* A failed or short write may have replaced part of the old sector.
     * It must never leave an eligible old attribute-only checkpoint. */
    UDFDirectoryFEWriteStarting(Dloc);
    status = UDFWriteExtent(IrpContext, Vcb, &Dloc->FELoc,
        0, WriteLen, FALSE, (int8*)Dloc->FileEntry, &WrittenBytes);
    if (NT_SUCCESS(status) && WrittenBytes != WriteLen)
        status = STATUS_IO_DEVICE_ERROR;
    if (NT_SUCCESS(status)) {
        Dloc->DirectoryCommittedLength = Dloc->DataLoc.Length;
        Dloc->DirectoryCommitKnown = TRUE;
        UDFDirectoryAppendFECommitted(FileInfo);
        UDFDirectoryFEWriteCompleted(Vcb, FileInfo, WriteLen);
    }
    UDFRememberMetadataWritebackError(IrpContext, Vcb, FileInfo, status);
    return status;
}

static
NTSTATUS
UDFWriteFileData(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN PUDF_FILE_INFO FileInfo,
    IN int64 Offset,
    IN SIZE_T Length,
    IN BOOLEAN Direct,
    IN int8* Buffer,
    OUT PSIZE_T WrittenBytes
)
{
    NTSTATUS status;
    uint32 PartNum, Header;
    PUDF_DATALOC_INFO Dloc = FileInfo->Dloc;

    if (!WrittenBytes) return STATUS_INVALID_PARAMETER;
    *WrittenBytes = 0;
    if (Offset < 0 || (uint64)Length > (uint64)MAXLONGLONG - (uint64)Offset ||
        (Length && !Buffer))
        return STATUS_INVALID_PARAMETER;
    if ((((PFILE_ENTRY)Dloc->FileEntry)->icbTag.flags & ICB_FLAG_ALLOC_MASK) != ICB_FLAG_AD_IN_ICB)
        return UDFWriteExtent(IrpContext, Vcb, &Dloc->DataLoc, Offset, Length, Direct, Buffer, WrittenBytes);
    status = UDFValidateFileEntryBuffer(Vcb, FileInfo, &Header);
    if (!NT_SUCCESS(status)) return status;
    if (!Dloc->FELoc.Mapping || !Vcb->Partitions || !Vcb->PartitionMaps ||
        Dloc->DataLoc.Offset != Header || Dloc->DataLoc.Length < 0 ||
        (uint64)Dloc->DataLoc.Length > Vcb->SectorSize - Header ||
        (uint64)Offset > (uint64)Dloc->DataLoc.Length ||
        Length > (uint64)Dloc->DataLoc.Length - (uint64)Offset)
        return STATUS_FILE_CORRUPT_ERROR;
    PartNum = UDFGetRefPartNumByPhysLba(Vcb, Dloc->FELoc.Mapping[0].extLocation);
    if (PartNum >= Vcb->PartitionMaps) return STATUS_FILE_CORRUPT_ERROR;

    /* New File Entries are zero-initialized, and an opened File Entry was
       read as a complete sector.  Every in-ICB write through this routine
       copies its bytes into that same sector-sized buffer before the full
       FE write.  Re-reading the on-disk in-ICB payload here only duplicates
       the buffered bytes on rewritable media.  Keep the read for sequential
       media, where the next writable location may change between writes. */
    if (Vcb->CDR_Mode) {
        status = UDFSyncInIcbData(IrpContext, Vcb, FileInfo);
        if (!NT_SUCCESS(status))
            return status;
    }
    
    if (Length) {
        /* An in-memory payload change is dirty even when the following
         * full FE write fails. Do not lose it on a clean-flag overwrite. */
        Dloc->FE_Flags |= UDF_FE_FLAG_FE_MODIFIED;
        RtlCopyMemory((int8*)Dloc->FileEntry + Header + (SIZE_T)Offset, Buffer, Length);
    }

    if (UDFDeferNewFileMetadata(IrpContext, Vcb, FileInfo)) {
        Dloc->DataLoc.Modified = TRUE;
        Dloc->FE_Flags |= UDF_FE_FLAG_FE_MODIFIED;
        *WrittenBytes = Length;
        return STATUS_SUCCESS;
    }
    status = UDFWriteFEBlock(IrpContext, Vcb, FileInfo, PartNum);
    if (!NT_SUCCESS(status))
        UDFRememberMetadataWritebackError(IrpContext, Vcb, FileInfo, status);

    if (NT_SUCCESS(status)) {
        *WrittenBytes = Length;

        /* UDFWriteFEBlock committed the entire in-ICB FE sector, including
           its data and allocation length.  On rewritable media an otherwise
           unchanged FE need not be written again at the next cleanup flush.
           Keep dirty state when allocation/relocation or directory retagging
           is still pending.  Sequential media has different write ordering. */
        if (Length && !Vcb->CDR_Mode &&
            !Dloc->FELoc.Modified &&
            !Dloc->AllocLoc.Modified &&
            !Dloc->AllocLoc.Mapping &&
            (!Dloc->DirIndex ||
             (!!(Dloc->DirIndex->DIFlags & UDF_DI_FLAG_INIT_IN_ICB) ==
              !!Dloc->DataLoc.Offset))) {
            Dloc->DataLoc.Modified = FALSE;
            Dloc->FE_Flags &= ~UDF_FE_FLAG_FE_MODIFIED;
        }
    }
    
    return status;
}

/*
 */
/* Keep the first, FE-resident AD bytes private until the final complete
 * FE write. Only external AED bytes are submitted/barriered here. */
static NTSTATUS
UDFWriteAllocationDescriptorBytes(PIRP_CONTEXT IrpContext, PVCB Vcb,
    PUDF_FILE_INFO FileInfo, int8* Buffer, uint32 Capacity, uint16 AllocMode)
{
    PEXTENT_INFO Alloc = &FileInfo->Dloc->AllocLoc;
    uint32 Prefix = 0, Bytes, External;
    SIZE_T Written = 0;
    NTSTATUS Status;
    if (!Buffer)
        return AllocMode == ICB_FLAG_AD_IN_ICB ? STATUS_SUCCESS : STATUS_FILE_CORRUPT_ERROR;
    if (AllocMode == ICB_FLAG_AD_IN_ICB || Alloc->Length < 0 ||
        (uint64)Alloc->Length > Capacity || !Alloc->Mapping ||
        Alloc->Offset > Vcb->SectorSize)
        return STATUS_FILE_CORRUPT_ERROR;
    Bytes = (uint32)Alloc->Length;
    if (!Bytes && Alloc->Offset == Vcb->SectorSize &&
        FileInfo->Dloc->FileEntryLen == Vcb->SectorSize)
        return STATUS_SUCCESS;
    if (UDFAllocDescsInFEBlock(Vcb, FileInfo)) {
        if (Bytes > Vcb->SectorSize - Alloc->Offset)
            return STATUS_FILE_CORRUPT_ERROR;
        RtlCopyMemory((int8*)FileInfo->Dloc->FileEntry + Alloc->Offset, Buffer, Bytes);
        RtlZeroMemory((int8*)FileInfo->Dloc->FileEntry + Alloc->Offset + Bytes,
                       Vcb->SectorSize - Alloc->Offset - Bytes);
        return STATUS_SUCCESS;
    }
    if (Alloc->Mapping[0].extLocation == FileInfo->Dloc->FELoc.Mapping[0].extLocation &&
        !UDFAllocDescsStartInFEBlock(Vcb, FileInfo))
        return STATUS_FILE_CORRUPT_ERROR;
    if (UDFAllocDescsStartInFEBlock(Vcb, FileInfo)) {
        Prefix = min(Bytes, Vcb->SectorSize - Alloc->Offset);
        if (Alloc->Mapping[0].extLocation != FileInfo->Dloc->FELoc.Mapping[0].extLocation ||
            (Alloc->Mapping[0].extLength >> 30) > EXTENT_NOT_RECORDED_ALLOCATED ||
            (Alloc->Mapping[0].extLength & UDF_EXTENT_LENGTH_MASK) < Vcb->SectorSize)
            return STATUS_FILE_CORRUPT_ERROR;
    }
    External = Bytes - Prefix;
    if (!External) return STATUS_FILE_CORRUPT_ERROR;
    Status = UDFPadLastSector(IrpContext, Vcb, Alloc);
    if (!NT_SUCCESS(Status)) return Status;
    Status = UDFWriteExtent(IrpContext, Vcb, Alloc, Prefix, External,
                              FALSE, Buffer + Prefix, &Written);
    if (NT_SUCCESS(Status) && Written != External) Status = STATUS_IO_DEVICE_ERROR;
    if (!NT_SUCCESS(Status)) return Status;
    Status = UDFBarrierFlushDevice(Vcb->TargetDeviceObject);
    if (!NT_SUCCESS(Status)) return Status;
    if (Prefix)
        RtlCopyMemory((int8*)FileInfo->Dloc->FileEntry + Alloc->Offset, Buffer, Prefix);
    return STATUS_SUCCESS;
}

static NTSTATUS
UDFFlushFEInternal(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN PUDF_FILE_INFO FileInfo,
    IN uint32 PartNum,
    int8* StageBuffer,
    PBOOLEAN Staged
    )
{
    int8* NewAllocDescs = NULL;
    NTSTATUS status;
    uint16 AllocMode;
    uint32 lba, AllocCapacity = 0, SerializedLength;

    status = UDFStreamCreateWriteStatus(FileInfo);
    if (status != STATUS_SUCCESS) return status;

    if (!NT_SUCCESS(FileInfo->Dloc->ZeroDataError) &&
        !UDFIsZeroDataRetry(FileInfo->Dloc))
        return FileInfo->Dloc->ZeroDataError;

    AllocMode = ((PFILE_ENTRY)(FileInfo->Dloc->FileEntry))->icbTag.flags & ICB_FLAG_ALLOC_MASK;
#ifdef UDF_DBG
/*    if (UDFIsADirectory(FileInfo) && (UDFGetFileSize(FileInfo) < 0x28) &&
       !UDFIsDeleted(UDFDirIndex(DirInfo->Dloc->DirIndex, FileInfo->Index)) ) {
        BrutePoint();
    }*/
//    ASSERT(FileInfo->Dloc->FELoc.Mapping[0].extLocation);
    if (FileInfo->Dloc->FELoc.Offset) {
        BrutePoint();
    }
    if (FileInfo->Dloc->AllocLoc.Mapping) {
        ASSERT(AllocMode != ICB_FLAG_AD_IN_ICB);
    }
#endif // UDF_DBG
retry_flush_FE:
    /* Append payload and its recorded mapping must be committed before AD
     * serialization and before a longer directory FE becomes visible. */
    status = UDFFlushDirectoryAppend(IrpContext, Vcb, FileInfo);
    if (!NT_SUCCESS(status))
        return status;
    UDFPrint(("  FlushFE: %x\n", FileInfo->Dloc->FELoc.Mapping[0].extLocation));

    /* Retagging is meaningful only for directories.  It returns
     * STATUS_NOT_A_DIRECTORY for an ordinary file, and a real I/O failure
     * must stop this FE flush while its dirty state is still retained. */
    if (FileInfo->Dloc->DirIndex) {
        status = UDFReTagDirectory(IrpContext, Vcb, FileInfo);
        if (!NT_SUCCESS(status))
            return status;
    }
    if (FileInfo->Dloc->DataLoc.Modified ||
       FileInfo->Dloc->AllocLoc.Modified) {
        ASSERT(PartNum != (uint32)(-1));
        /* Recorded activation follows completed external data writes. Order
         * that initialized payload before serializing/publishing its new ADs;
         * a successful write request alone is not a lower cache barrier. */
        if (FileInfo->Dloc->DataLoc.Modified && !FileInfo->Dloc->DataLoc.Offset) {
            status = UDFBarrierFlushDevice(Vcb->TargetDeviceObject);
            if (!NT_SUCCESS(status)) return status;
        }
        // prepare new AllocDescs for flushing...
        if (!NT_SUCCESS(status = UDFBuildAllocDescs(IrpContext, Vcb, PartNum, FileInfo, &NewAllocDescs, &AllocCapacity))) {
            UDFPrint(("  FlushFE: UDFBuildAllocDescs() faliled (%x)\n", status));
            if (NewAllocDescs)
                MyFreePool__(NewAllocDescs);
            return status;
        }
#ifdef UDF_DBG
        ASSERT(UDFGetFileSize(FileInfo) <= UDFGetExtentLength(FileInfo->Dloc->DataLoc.Mapping));
        AllocMode = ((PFILE_ENTRY)(FileInfo->Dloc->FileEntry))->icbTag.flags & ICB_FLAG_ALLOC_MASK;
#endif // UDF_DBG
        // initiate update of lengthAllocDescs
        FileInfo->Dloc->FE_Flags |= UDF_FE_FLAG_FE_MODIFIED;
        _SEH2_TRY {
            AllocMode = ((PFILE_ENTRY)FileInfo->Dloc->FileEntry)->icbTag.flags & ICB_FLAG_ALLOC_MASK;
            status = UDFWriteAllocationDescriptorBytes(IrpContext, Vcb,
                                                        FileInfo, NewAllocDescs, AllocCapacity, AllocMode);
        } _SEH2_FINALLY {
            if (NewAllocDescs) MyFreePool__(NewAllocDescs);
            NewAllocDescs = NULL;
        } _SEH2_END;
        if (!NT_SUCCESS(status)) return status;
        FileInfo->Dloc->DataLoc.Modified = FALSE;
        FileInfo->Dloc->AllocLoc.Modified = FALSE;
    } else {
#if defined(UDF_DBG) && !defined(UDF_CHECK_UTIL)
        if (FileInfo->Dloc->DataLoc.Mapping[0].extLength == (uint32)(EXTENT_NOT_RECORDED_NOT_ALLOCATED << 30) &&
            FileInfo->Dloc->DataLoc.Mapping[0].extLocation == 0) {
                // This case occurs when DeletePending is set for directories. UDFUnlinkFile__ been called.
                ASSERT(UDFGetExtentLength(FileInfo->Dloc->DataLoc.Mapping) == 0);
        }
        else {
            ASSERT(UDFGetFileSize(FileInfo) <= UDFGetExtentLength(FileInfo->Dloc->DataLoc.Mapping));
        }
#endif // UDF_DBG
    }
/*    if (FileInfo->Fcb &&
       ((FileInfo->Dloc->FELoc.Mapping[0].extLocation > Vcb->SessionEndLba) ||
        (Vcb->BitmapFcb ? UDFIsBitmapBitFree(Vcb, FileInfo->Dloc->FELoc.Mapping[0].extLocation - Vcb->Partitions[0].PartitionRoot) : UDFGetFreeBit(((uint32*)(Vcb->FSBM_Bitmap)), FileInfo->Dloc->FELoc.Mapping[0].extLocation - Vcb->Partitions[0].PartitionRoot))) ) {
        BrutePoint();
    }*/
/*    if (FileInfo->Dloc->FELoc.Mapping[0].extLocation) {
        ASSERT( FileInfo->Dloc->FileEntry->tagLocation ==
               (FileInfo->Dloc->FELoc.Mapping[0].extLocation - 0x580));
    }*/
    if ((FileInfo->Dloc->FE_Flags & UDF_FE_FLAG_FE_MODIFIED) ||
        FileInfo->Dloc->FELoc.Modified || FileInfo->Dloc->RetiredExtents) {
        ASSERT(PartNum != (uint32)(-1));
        // UDF 2.50 File Entries may live in the metadata partition, whose
        // partition map is distinct from the physical partition map.
        ASSERT(PartNum < Vcb->PartitionMaps);
        if (PartNum == (uint32)(-1) || PartNum == (uint32)(-2)) {
            UDFPrint(("  bad PartNum: %d\n", PartNum));
        }
        if (PartNum >= Vcb->PartitionMaps)
            return STATUS_FILE_CORRUPT_ERROR;
        // flush FileEntry

        // if FE is located in remapped block, place it to reliable space
        lba = FileInfo->Dloc->FELoc.Mapping[0].extLocation;
        if (Vcb->BSBM_Bitmap) {
            if (lba < Vcb->Partitions[0].PartitionRoot ||
                lba - Vcb->Partitions[0].PartitionRoot >= Vcb->FSBM_BitCount)
                return STATUS_FILE_CORRUPT_ERROR;
            if (UDFGetBadBit((uint32*)(Vcb->BSBM_Bitmap), lba - Vcb->Partitions[0].PartitionRoot)) {
                AdPrint(("  bad block under FE @%x\n", lba));
                status = STATUS_DEVICE_DATA_ERROR;
                goto relocate_FE;
            }
        }

        AdPrint(("  setup tag: @%x\n", lba));
        ASSERT( lba );
        UDFPrint(("FELoc.Length = %x\n", FileInfo->Dloc->FELoc.Length));

        AllocMode = 
            ((PFILE_ENTRY)(FileInfo->Dloc->FileEntry))->icbTag.flags & 
            ICB_FLAG_ALLOC_MASK;
        /* Rewritable-media in-ICB writes update the same sector-sized FE
           buffer before committing the full FE block.  A separate disk read
           during flush only reloads those bytes; keep it for sequential
           media, where the writable location can move. */
        if (AllocMode == ICB_FLAG_AD_IN_ICB && Vcb->CDR_Mode) {

            status = UDFSyncInIcbData(IrpContext, Vcb, FileInfo);

            if (!NT_SUCCESS(status)) {
                UDFPrint(("  FlushFE: UDFSyncInIcbData failed with : %x\n", status));
                return status;
            }

        }
        if (StageBuffer && !FileInfo->Dloc->RetiredExtents &&
            UDFCanStageFileEntry(Vcb, FileInfo)) {
            status = UDFSerializeFEBlock(Vcb, FileInfo, PartNum, &SerializedLength);
            if (!NT_SUCCESS(status)) return status;
            if (SerializedLength == 512) {
                RtlCopyMemory(StageBuffer, FileInfo->Dloc->FileEntry, 512);
                *Staged = TRUE;
                return STATUS_SUCCESS;
            }
        }
        status = UDFWriteFEBlock(IrpContext, Vcb, FileInfo, PartNum);

        if (!NT_SUCCESS(status)) {
            UDFPrint(("  FlushFE: UDFWriteExtent(2) failed (%x)\n", status));
            if (status == STATUS_DEVICE_DATA_ERROR) {
relocate_FE:
                /* A parentless metadata/VAT/by-ID/checkpoint FI has no
                 * namespace FID that this branch can retarget. Likewise a
                 * multi-link FE needs all reverse references updated, not
                 * only the current name. Preserve the dirty generation
                 * and original error before allocating or freeing anything. */
                if (!FileInfo->ParentFile || FileInfo->Index < 2 ||
                    !FileInfo->FileIdent ||
                    UDFGetFileNameLinkCount(FileInfo) != 1 ||
                    !UDFDirIndex(UDFGetDirIndexByFileInfo(FileInfo), FileInfo->Index))
                    return STATUS_DEVICE_DATA_ERROR;
                /* Old AED/data retirements are rooted at the current FE.
                 * Moving that root before the parent FID commits would let
                 * the ordinary FE barrier release blocks still reachable
                 * through the old namespace. A loaded AD map's Length may
                 * describe only its FE prefix: inspect the actual map too.
                 * Preserve the failed generation until a complete ordered
                 * FE/FID relocation protocol exists for these states. */
                AllocMode = ((PFILE_ENTRY)FileInfo->Dloc->FileEntry)->icbTag.flags & ICB_FLAG_ALLOC_MASK;
                if (FileInfo->Dloc->RetiredExtents ||
                    (AllocMode != ICB_FLAG_AD_IN_ICB &&
                     (!FileInfo->Dloc->AllocLoc.Mapping ||
                      (FileInfo->Dloc->AllocLoc.Mapping[0].extLength & UDF_EXTENT_LENGTH_MASK) != Vcb->SectorSize ||
                      FileInfo->Dloc->AllocLoc.Mapping[1].extLength))) {
                    Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
                    UDFRememberMetadataWritebackError(IrpContext, Vcb, FileInfo, STATUS_DEVICE_DATA_ERROR);
                    return STATUS_DEVICE_DATA_ERROR;
                }
                UDFPrint(("  try to relocate\n"));

                EXTENT_INFO _FEExtInfo;
                // calculate the length required

                // allocate block for FE
                if (NT_SUCCESS(UDFAllocateFESpace(IrpContext, Vcb, FileInfo->ParentFile, PartNum, &_FEExtInfo, (uint32)(FileInfo->Dloc->FELoc.Length)) )) {
                    UDFPrint(("  relocate %x -> %x\n",
                        lba,
                        _FEExtInfo.Mapping[0].extLocation));

                    UDFMarkSpaceAsXXX(Vcb, 0, FileInfo->Dloc->FELoc.Mapping, AS_BAD);

                    UDFRelocateDloc(Vcb, FileInfo->Dloc, _FEExtInfo.Mapping[0].extLocation);
                    MyFreePool__(FileInfo->Dloc->FELoc.Mapping);
                    FileInfo->Dloc->FELoc.Mapping = _FEExtInfo.Mapping;

                    FileInfo->Dloc->FELoc.Modified = TRUE;
                    FileInfo->Dloc->FE_Flags |= UDF_FE_FLAG_FE_MODIFIED;

                    AllocMode = ((PFILE_ENTRY)(FileInfo->Dloc->FileEntry))->icbTag.flags & ICB_FLAG_ALLOC_MASK;
                    if (AllocMode == ICB_FLAG_AD_IN_ICB) {
                        UDFPrint(("  IN-ICB data lost\n"));
                        FileInfo->Dloc->DataLoc.Mapping[0].extLocation = _FEExtInfo.Mapping[0].extLocation;
                        FileInfo->Dloc->DataLoc.Modified = TRUE;
                    } else {
                        FileInfo->Dloc->AllocLoc.Mapping[0].extLocation = _FEExtInfo.Mapping[0].extLocation;
                        FileInfo->Dloc->AllocLoc.Modified = TRUE;
                    }

                    if (FileInfo->Index >= 2) {
                        PDIR_INDEX_ITEM DirNdx;
                        DirNdx = UDFDirIndex(UDFGetDirIndexByFileInfo(FileInfo), FileInfo->Index);
                        if (DirNdx) {
                            UDFPrint(("  update reference in FI\n"));
                            DirNdx->FileEntryLoc.logicalBlockNum =
                                FileInfo->FileIdent->icb.extLocation.logicalBlockNum =
                                UDFPhysLbaToPart(Vcb, PartNum, _FEExtInfo.Mapping[0].extLocation);
                            DirNdx->FI_Flags |= UDF_FI_FLAG_FI_MODIFIED;
                        }
                    }
                    // this will update
                    UDFPrint(("  retry flush...\n"));
                    goto retry_flush_FE;
                }
            }
            BrutePoint();
            return status;
        }
        FileInfo->Dloc->FE_Flags &= ~UDF_FE_FLAG_FE_MODIFIED;
        FileInfo->Dloc->FELoc.Modified = FALSE;
    } else {
        ASSERT((FileInfo->Dloc->FileEntry->descVersion == 2) ||
               (FileInfo->Dloc->FileEntry->descVersion == 3));
    }
#ifdef UDF_DBG
    if (FileInfo->Dloc->AllocLoc.Mapping) {
        ASSERT(AllocMode != ICB_FLAG_AD_IN_ICB);
    } else {
        ASSERT(AllocMode == ICB_FLAG_AD_IN_ICB);
    }
#endif // UDF_DBG
    /* Old allocation ownership survives every failed FE write/barrier.
     * Commit also covers a nonstaged call through UDFStageNewFileEntry. */
    return UDFCommitExtentRetirements(IrpContext, Vcb, FileInfo);
} // end UDFFlushFEInternal()

NTSTATUS
UDFFlushFE(PIRP_CONTEXT IrpContext, PVCB Vcb,
    PUDF_FILE_INFO FileInfo, uint32 PartNum)
{
    NTSTATUS status = UDFDrainNewFileData(IrpContext, Vcb, FileInfo);
    if (NT_SUCCESS(status))
        status = UDFFlushFEInternal(IrpContext, Vcb, FileInfo, PartNum, NULL, NULL);
    UDFRememberMetadataWritebackError(IrpContext, Vcb, FileInfo, status);
    return status;
}

NTSTATUS
UDFStageNewFileEntry(PIRP_CONTEXT IrpContext, PVCB Vcb,
    PUDF_FILE_INFO FileInfo, uint32 PartNum, int8* Buffer, PBOOLEAN Staged)
{
    *Staged = FALSE;
    return UDFFlushFEInternal(IrpContext, Vcb, FileInfo, PartNum, Buffer, Staged);
}

NTSTATUS
UDFFlushFI(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN PUDF_FILE_INFO FileInfo,
    IN uint32 PartNum
    )
{
    PUDF_FILE_INFO DirInfo = FileInfo->ParentFile;
    PDIR_INDEX_ITEM DirNdx;
    NTSTATUS status;
    SIZE_T WrittenBytes;
    // use WrittenBytes variable to store LBA of FI to be recorded
    #define lba   WrittenBytes

    if (FileInfo->Dloc && !NT_SUCCESS(FileInfo->Dloc->ZeroDataError) &&
        !UDFIsZeroDataRetry(FileInfo->Dloc))
        return FileInfo->Dloc->ZeroDataError;

//    ASSERT(FileInfo->Dloc->FELoc.Mapping[0].extLocation);
    // some files has no FI
    if (!DirInfo || UDFIsAStreamDir(FileInfo))
        return STATUS_SUCCESS;
    DirNdx = UDFDirIndex(DirInfo->Dloc->DirIndex, FileInfo->Index);
    if (FileInfo->NamespaceStaged &&
        (!FileInfo->FileIdent || !(DirNdx->FileCharacteristics & FILE_DELETED) ||
         !(FileInfo->FileIdent->fileCharacteristics & FILE_DELETED)))
        return STATUS_FILE_CORRUPT_ERROR; /* a private stage cannot publish */
//    ASSERT(FileInfo->FileIdent->lengthFileIdent < 0x80);
#ifdef UDF_DBG
    if (DirNdx->FileCharacteristics & FILE_DELETED) {
        ASSERT(FileInfo->FileIdent->fileCharacteristics & FILE_DELETED);
    }
#endif // UDF_DBG
    UDFPrint(("  FlushFI: offs %x\n", (ULONG)(DirNdx->Offset)));

    if ((DirNdx->FI_Flags & UDF_FI_FLAG_FI_MODIFIED)) {
        /* Test the FID we are about to publish, not stale characteristics
         * in its prior buffer. Deleted entries do not publish an FE. */
        if (!FileInfo->FileIdent)
            return STATUS_FILE_CORRUPT_ERROR;
        if ((FileInfo->Dloc->FE_Flags & UDF_FE_FLAG_NEW_UNPUBLISHED) &&
            !(DirNdx->FileCharacteristics & FILE_DELETED)) {
            status = UDFFlushFE(IrpContext, Vcb, FileInfo, PartNum);
            if (!NT_SUCCESS(status))
                return status;
        }
        // flush FileIdent
        ASSERT(PartNum != (uint32)(-1));
        FileInfo->FileIdent->fileCharacteristics = DirNdx->FileCharacteristics;
        lba = UDFExtentOffsetToLba(Vcb, DirInfo->Dloc->DataLoc.Mapping,
                                        DirNdx->Offset, NULL, NULL, NULL, NULL);
        AdPrint(("  FI lba %x\n", lba));
        // check if requested Offset is allocated
        if (lba == (uint32)LBA_OUT_OF_EXTENT) {
            // write 1 byte
            if (!NT_SUCCESS(status = UDFWriteFile__(IrpContext, Vcb, DirInfo, DirNdx->Offset, 1, FALSE, (int8*)(FileInfo->FileIdent), &WrittenBytes) )) {
                BrutePoint();
                return status;
            }
            lba = UDFExtentOffsetToLba(Vcb, DirInfo->Dloc->DataLoc.Mapping,
                                            DirNdx->Offset, NULL, NULL, NULL, NULL);
            AdPrint(("  allocated FI lba %x\n", lba));
            // check if requested Offset is allocated
            if (lba == (uint32)LBA_OUT_OF_EXTENT) {
                BrutePoint();
                return STATUS_UNSUCCESSFUL;
            }
        }
        // init structure
        UDFSetUpTag(
            Vcb, &(FileInfo->FileIdent->descTag), (uint16)(FileInfo->FileIdentLen), UDFPhysLbaToPart(Vcb, PartNum, lba),
            0);
        // record data
        {
            BOOLEAN publish =
                (FileInfo->Dloc->FE_Flags & UDF_FE_FLAG_NEW_UNPUBLISHED) &&
                !(FileInfo->FileIdent->fileCharacteristics & FILE_DELETED) &&
                (FlagOn(IrpContext->Flags, IRP_CONTEXT_FLAG_METADATA_WRITER) ||
                 IrpContext->MajorFunction == IRP_MJ_FLUSH_BUFFERS);
            BOOLEAN previous = FlagOn(IrpContext->Flags, IRP_CONTEXT_FLAG_METADATA_PUBLISH) != 0;
            if (publish)
                SetFlag(IrpContext->Flags, IRP_CONTEXT_FLAG_METADATA_PUBLISH);
            _SEH2_TRY {
                status = UDFWriteFile__(IrpContext, Vcb, DirInfo, DirNdx->Offset,
                    FileInfo->FileIdentLen, FALSE, (int8*)FileInfo->FileIdent, &WrittenBytes);
            } _SEH2_FINALLY {
                if (!previous)
                    ClearFlag(IrpContext->Flags, IRP_CONTEXT_FLAG_METADATA_PUBLISH);
            } _SEH2_END;
            if (NT_SUCCESS(status) && WrittenBytes != FileInfo->FileIdentLen)
                status = STATUS_IO_DEVICE_ERROR;
            if (!NT_SUCCESS(status))
                return status;
            if (!(FileInfo->FileIdent->fileCharacteristics & FILE_DELETED)) {
                if (FileInfo->Dloc->FE_Flags & UDF_FE_FLAG_NEW_UNPUBLISHED) {
                    UDF_METADATA_COUNT(Vcb,31);
#ifdef UDF_METADATA_BATCH_DIAGNOSTICS
                    if (IrpContext->MajorFunction == IRP_MJ_CREATE) UDF_METADATA_COUNT(Vcb,32);
                    else if (IrpContext->MajorFunction == IRP_MJ_CLEANUP) UDF_METADATA_COUNT(Vcb,33);
                    else if (IrpContext->MajorFunction == IRP_MJ_CLOSE) UDF_METADATA_COUNT(Vcb,34);
                    else UDF_METADATA_COUNT(Vcb,35);
#endif
                }
                FileInfo->Dloc->FE_Flags &= ~UDF_FE_FLAG_NEW_UNPUBLISHED;
            }
        }
        DirNdx->FI_Flags &= ~UDF_FI_FLAG_FI_MODIFIED;
    }

    return STATUS_SUCCESS;
} // end UDFFlushFI()

/*
    This routine updates AllocDesc sequence, FileIdent & FileEntry
    for given file
 */
static NTSTATUS
UDFFlushFile__Internal(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN PUDF_FILE_INFO FileInfo,
    IN ULONG FlushFlags
    )
{
    ValidateFileInfo(FileInfo);

    if (!FileInfo) return STATUS_SUCCESS;
    NTSTATUS status;
    uint32 PartNum;

    status = UDFFlushDirectoryAppend(IrpContext, Vcb, FileInfo);
    if (!NT_SUCCESS(status))
        return status;
    ASSERT(FileInfo->Dloc->FELoc.Mapping[0].extLocation);
    PartNum = UDFGetRefPartNumByPhysLba(Vcb, FileInfo->Dloc->FELoc.Mapping[0].extLocation);
    if (PartNum == (uint32)-1) {
        UDFPrint(("  Is DELETED ?\n"));
        if (FileInfo->ParentFile) {
            PartNum = UDFGetRefPartNumByPhysLba(Vcb, FileInfo->ParentFile->Dloc->FELoc.Mapping[0].extLocation);
        } else {
            BrutePoint();
        }
    }
#ifdef UDF_CHECK_DISK_ALLOCATION
    if ( FileInfo->Fcb &&
        (Vcb->BitmapFcb ? UDFIsBitmapBitFree(Vcb, FileInfo->Dloc->FELoc.Mapping[0].extLocation - Vcb->Partitions[0].PartitionRoot) : UDFGetFreeBit(((uint32*)(Vcb->FSBM_Bitmap)), FileInfo->Dloc->FELoc.Mapping[0].extLocation - Vcb->Partitions[0].PartitionRoot))) {

        if (UDFIsAStreamDir(FileInfo)) {
            if (!UDFIsSDirDeleted(FileInfo)) {
                UDFPrint(("  Not DELETED SDir\n"));
                BrutePoint();
            }
            ASSERT(!FileInfo->Dloc->FELoc.Modified);
        } else
        if (!FileInfo->FileIdent ||
           !(FileInfo->FileIdent->fileCharacteristics & FILE_DELETED)) {
            if (!FileInfo->FileIdent)
                AdPrint(("  No FileIdent\n"));
            if (FileInfo->FileIdent &&
               !(FileInfo->FileIdent->fileCharacteristics & FILE_DELETED))
                AdPrint(("  Not DELETED\n"));
            AdPrint(("Flushing to Discarded block %x\n", FileInfo->Dloc->FELoc.Mapping[0].extLocation));
            BrutePoint();
        }
    }
#endif // UDF_CHECK_DISK_ALLOCATION

    // flush FE and pre-allocation charge for directories
    if (FileInfo->Dloc &&
       FileInfo->Dloc->DirIndex) {
        // if Lite Flush is used, keep preallocations
        if (!(FlushFlags & UDF_FLUSH_FLAGS_LITE)) {
            if (FileInfo->Dloc->DataLoc.Flags & EXTENT_FLAG_PREALLOCATED) {
                FileInfo->Dloc->DataLoc.Flags |= EXTENT_FLAG_CUT_PREALLOCATED;
                status = UDFResizeExtent(IrpContext, Vcb, FileInfo, PartNum, UDFGetFileSize(FileInfo), FALSE, &(FileInfo->Dloc->DataLoc));
                FileInfo->Dloc->DataLoc.Flags &= ~EXTENT_FLAG_CUT_PREALLOCATED;
                if (!NT_SUCCESS(status))
                    return status;
                FileInfo->Dloc->DataLoc.Flags &= ~EXTENT_FLAG_PREALLOCATED;
                AdPrint(("Dir pre-alloc truncated (Flush)\n"));
                FileInfo->Dloc->DataLoc.Modified = TRUE;
            }
        }
    }
    // flush FE
    if (!NT_SUCCESS(status = UDFFlushFE(IrpContext, Vcb, FileInfo, PartNum))) {
        UDFPrint(("Error flushing FE\n"));
        /* Keep the FE and its FID dirty for a later flush.  The write
         * failure does not authorize deleting the file's directory entry. */
        return status;
    }
    if (!NT_SUCCESS(status = UDFFlushFI(IrpContext, Vcb, FileInfo, PartNum)))
        return status;

    ASSERT((FileInfo->Dloc->FileEntry->descVersion == 2) ||
           (FileInfo->Dloc->FileEntry->descVersion == 3));

    return STATUS_SUCCESS;
} // end UDFFlushFile__Internal()

NTSTATUS
UDFFlushFile__(PIRP_CONTEXT IrpContext, PVCB Vcb, PUDF_FILE_INFO FileInfo, ULONG FlushFlags)
{
    if (FileInfo && FileInfo->Dloc && !NT_SUCCESS(FileInfo->Dloc->ZeroDataError) &&
        !UDFIsZeroDataRetry(FileInfo->Dloc))
        return FileInfo->Dloc->ZeroDataError;
    NTSTATUS status = UDFFlushFile__Internal(IrpContext, Vcb, FileInfo, FlushFlags);
    UDFRememberMetadataWritebackError(IrpContext, Vcb, FileInfo, status);
    return status;
}

/*
    This routine compares 2 FileInfo structures
 */
BOOLEAN
UDFCompareFileInfo(
    IN PUDF_FILE_INFO f1,
    IN PUDF_FILE_INFO f2
    )
{
    uint_di i;
    PDIR_INDEX_HDR hDirIndex1;
    PDIR_INDEX_HDR hDirIndex2;
    PDIR_INDEX_ITEM DirIndex1;
    PDIR_INDEX_ITEM DirIndex2;

    if (!f1 || !f2) return FALSE;
    if (f1->Dloc->FileEntryLen != f2->Dloc->FileEntryLen) return FALSE;
//    if (f1->FileIdentLen != f2->FileIdentLen) return FALSE;
/*    if (f1->Dloc->DirIndex && !f2->Dloc->DirIndex) return FALSE;
    if (f2->Dloc->DirIndex && !f1->Dloc->DirIndex) return FALSE;
    if ((f1->Dloc->DirIndex) &&
       (f1->Dloc->DirIndex->LastFrameCount != f2->Dloc->DirIndex->LastFrameCount)) return FALSE;*/
    if (f1->Index != f2->Index) return FALSE;
    if (!(f1->Dloc->DataLoc.Mapping)) return FALSE;
    if (!(f2->Dloc->DataLoc.Mapping)) return FALSE;
    if (f1->Dloc->DataLoc.Mapping[0].extLocation != f2->Dloc->DataLoc.Mapping[0].extLocation) return FALSE;
    if (f1->Dloc->DataLoc.Mapping[0].extLength != f2->Dloc->DataLoc.Mapping[0].extLength) return FALSE;
//    if (f1-> != f2->) return FALSE;
//    if (f1-> != f2->) return FALSE;
//    if (f1-> != f2->) return FALSE;
    if (!(f1->Dloc->FileEntry)) return FALSE;
    if (!(f2->Dloc->FileEntry)) return FALSE;
    if (RtlCompareMemory(f1->Dloc->FileEntry, f2->Dloc->FileEntry, f2->Dloc->FileEntryLen) != f2->Dloc->FileEntryLen)
        return FALSE;
    if (!(hDirIndex1 = f1->Dloc->DirIndex)) return FALSE;
    if (!(hDirIndex2 = f2->Dloc->DirIndex)) return FALSE;

    for(i=2; (DirIndex1 = UDFDirIndex(hDirIndex1,i)) &&
             (DirIndex2 = UDFDirIndex(hDirIndex2,i)); i++) {
        if ( DirIndex1->FName.Buffer &&
           !DirIndex2->FName.Buffer)
            return FALSE;
        if ( DirIndex2->FName.Buffer &&
           !DirIndex1->FName.Buffer)
            return FALSE;
        if (!DirIndex2->FName.Buffer &&
           !DirIndex1->FName.Buffer)
            continue;
        if (RtlCompareUnicodeString(&(DirIndex1->FName),
                                   &(DirIndex2->FName),FALSE)) {
            return FALSE;
        }
//        if (DirIndex1[i].FileEntry != DirIndex2[i].FileEntry)
//            return FALSE;
        if (RtlCompareMemory(&(DirIndex1->FileEntryLoc),
                            &(DirIndex2->FileEntryLoc), sizeof(lb_addr)) != sizeof(lb_addr))
            return FALSE;
    }

    return TRUE;
} // end UDFCompareFileInfo()

/*
    This routine computes 32-bit hash based on CRC-32 from SSH
 */
uint32
__fastcall
crc32(
    IN uint8* s,
    IN uint32 len
    )
{
    uint32 i;
    uint32 crc32val = 0;

    for(i=0; i<len; i++, s++) {
        crc32val =
            crc32_tab[(crc32val ^ (*s)) & 0xff] ^ (crc32val >> 8);
    }
    return crc32val;
} // end crc32()

/*
    Calculate a 16-bit CRC checksum for unicode strings
    using ITU-T V.41 polynomial.

    The OSTA-UDF(tm) 1.50 standard states that using CRCs is mandatory.
    The polynomial used is: x^16 + x^12 + x^15 + 1
*/
uint16
__fastcall
UDFUnicodeCksum(
    PWCHAR s,
    uint32 n
    )
{
    uint16 Crc = 0;
    while (n--) {
        Crc = CrcTable[(Crc >> 8 ^ (*s >> 8)) & 0xff] ^ (Crc << 8);
        Crc = CrcTable[(Crc >> 8 ^ (*s++ & 0xff)) & 0xff] ^ (Crc << 8);
    }
    return Crc;
} // end UDFUnicodeCksum()

uint16
__fastcall
UDFUnicodeCksum150(
    PWCHAR s, // ECX
    uint32 n  // EDX
    )
{
    uint16 Crc = 0;
    uint16 Crc2 = 0;
    BOOLEAN Use16 = FALSE;
    while (n--) {
        if (!Use16) {
            if ((*s) & 0xff00) {
                Use16 = TRUE;
            } else {
                Crc2 = CrcTable[(Crc2 >> 8 ^ (*s >> 8)) & 0xff] ^ (Crc2 << 8);
            }
        }
        Crc = CrcTable[(Crc >> 8 ^ (*s >> 8)) & 0xff] ^ (Crc << 8);
        Crc = CrcTable[(Crc >> 8 ^ (*s++ & 0xff)) & 0xff] ^ (Crc << 8);
    }
    return Use16 ? Crc : Crc2;
} // end UDFUnicodeCksum150()

/*
    Calculate a 16-bit CRC checksum using ITU-T V.41 polynomial.

    The OSTA-UDF(tm) 1.50 standard states that using CRCs is mandatory.
    The polynomial used is: x^16 + x^12 + x^15 + 1
*/
uint16
__fastcall
UDFCrc(
    IN uint8 *Data,
    IN SIZE_T Size,
    IN uint16 Crc
    )
{
    while (Size--)
        Crc = CrcTable[(Crc >> 8 ^ *Data++) & 0xff] ^ (Crc << 8);
    return Crc;
} // end UDFCrc()

/*
    Read the first block of a tagged descriptor & check it.
*/
static unsigned int
UDFDescriptorCrcFast(const unsigned char *Bytes, unsigned int Length)
{
    return UDFCrc((uint8*)Bytes, Length, 0);
}

static BOOLEAN
UDFTaggedSectorValid(
    IN PVCB Vcb,
    IN const int8* Buf,
    IN uint32 Location
    )
{
    return UdfValidateDescriptorEx((const unsigned char*)Buf, Vcb->SectorSize,
                                    UDF_DESCRIPTOR_ANY_ID, Location,
                                    UDFDescriptorCrcFast) != 0;
}

NTSTATUS
UDFReadTagged(
    IN PIRP_CONTEXT IrpContext,
    PVCB Vcb,
    int8* Buf,
    uint32 Block,
    uint32 Location,
    uint16 *Ident
    )
{
    NTSTATUS RC;
    tag* PTag = (tag*)Buf;
//    icbtag* Icb = (icbtag*)(Buf+1);
    uint8 checksum;
    unsigned int i;
    int8* tb;
    uint32 MetadataLbn, MirrorLba;
    PUDF_METADATA_VIEW View = NULL;
    int8* MirrorBuf = NULL;
    NTSTATUS MirrorStatus;
    BOOLEAN DescriptorValid;

    // Read the block
    *Ident = 0;
    if (Block == 0xFFFFFFFF)
        return STATUS_FILE_CORRUPT_ERROR;

    _SEH2_TRY {
        View = UDFAcquireMetadataView(Vcb);
        RC = UDFReadSectors(IrpContext, Vcb, FALSE, Block, 1, FALSE, Buf);
        DescriptorValid = NT_SUCCESS(RC) && UDFTaggedSectorValid(Vcb, Buf, Location);
        if (!NT_SUCCESS(RC)) {
            UDFPrint(("UDF: Block=%x, Location=%x: read failed\n", Block, Location));
        }

        // A duplicated UDF 2.50 metadata partition is a recovery copy, not
        // merely reserved space.  On a bad primary descriptor, validate the
        // corresponding mirror sector before exposing it to the caller.
        if (View && (View->Flags & UDF_METADATA_DUPLICATED) && !DescriptorValid) {
            MetadataLbn = UDFMetadataViewPhysToLba(View, Block);
            if (MetadataLbn != LBA_OUT_OF_EXTENT) {
                MirrorLba = UDFMetadataViewLbaToPhys(View, MetadataLbn, TRUE, NULL);
                if (MirrorLba != LBA_OUT_OF_EXTENT && MirrorLba != Block) {
                    MirrorBuf = (int8*)DbgAllocatePool(NonPagedPool,
                                                       Vcb->SectorSize);
                    if (MirrorBuf) {
                        MirrorStatus = UDFReadSectors(IrpContext, Vcb, FALSE,
                                                      MirrorLba, 1, FALSE,
                                                      MirrorBuf);
                        if (NT_SUCCESS(MirrorStatus) &&
                            UDFTaggedSectorValid(Vcb, MirrorBuf, Location)) {
                            RtlCopyMemory(Buf, MirrorBuf, Vcb->SectorSize);
                            RC = STATUS_SUCCESS;
                            DescriptorValid = TRUE;
                            UDFPrint(("UDF: recovered metadata descriptor %x "
                                      "from mirror %x\n", Block, MirrorLba));
                        }
                        DbgFreePool(MirrorBuf);
                        MirrorBuf = NULL;
                    }
                }
            }
        }

        if (!NT_SUCCESS(RC))
            try_return(RC);

        *Ident = PTag->tagIdent;

        if (Location != PTag->tagLocation) {
            UDFPrint(("UDF: location mismatch block %x, tag %x != %x\n",
                Block, PTag->tagLocation, Location));
            try_return(RC = STATUS_FILE_CORRUPT_ERROR);
        }

        /* Verify the tag checksum */
        checksum = 0;
        tb = Buf;
        for (i=0; i<sizeof(tag); i++, tb++)
            checksum += (uint8)((i!=4) ? (*tb) : 0);

        if (checksum != PTag->tagChecksum) {
            UDFPrint(("UDF: tag checksum failed block %x\n", Block));
            try_return(RC = STATUS_CRC_ERROR);
        }

        // Verify the tag version
        if ((PTag->descVersion != 2) &&
           (PTag->descVersion != 3)) {
            UDFPrint(("UDF: Tag version 0x%04x != 0x0002 || 0x0003 block %x\n",
                (PTag->descVersion), Block));
            try_return(RC = STATUS_FILE_CORRUPT_ERROR);
        }

        // Verify the descriptor CRC
        if (DescriptorValid)
        {
            /*        UDFPrint(("Tag ID: %x, ver %x\t", PTag->tagIdent, PTag->descVersion ));
                    if ((i == TID_FILE_ENTRY) ||
                       (i == TID_EXTENDED_FILE_ENTRY)) {
                        UDFPrint(("StrategType: %x, ", Icb->strategyType ));
                        UDFPrint(("FileType: %x\t", Icb->fileType ));
                    }
                    UDFPrint(("\n"));*/
            try_return(RC = STATUS_SUCCESS);
        }
        UDFPrint(("UDF: Crc failure block %x: crc = %x, crclen = %x\n",
            Block, PTag->descCRC, PTag->descCRCLength));
        RC = STATUS_CRC_ERROR;

try_exit:    NOTHING;

    } _SEH2_FINALLY {
        if (MirrorBuf) DbgFreePool(MirrorBuf);
        UDFReleaseMetadataView(View);
    } _SEH2_END

    return RC;
} // end UDFReadTagged()

/*
    This routine creates hard link for the file from DirInfo1
    to DirInfo2 & names it as fn
 */
NTSTATUS
UDFHardLinkFile__(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN BOOLEAN IgnoreCase,
    IN OUT BOOLEAN* Replace,
    IN PUNICODE_STRING fn,
    IN OUT PUDF_FILE_INFO DirInfo1,
    IN OUT PUDF_FILE_INFO DirInfo2,
    IN OUT PUDF_FILE_INFO FileInfo,
    OUT PBOOLEAN TargetRemoved,
    OUT PBOOLEAN LinkCreated
    )
{
    _SEH2_VOLATILE PUDF_FILE_INFO Link = NULL;
    _SEH2_VOLATILE PUDF_FILE_INFO Scratch = NULL;
    _SEH2_VOLATILE BOOLEAN HoldOwned = FALSE;
    _SEH2_VOLATILE BOOLEAN Converted = FALSE;
    _SEH2_VOLATILE NTSTATUS Status = STATUS_SUCCESS;
    NTSTATUS CloseStatus;
    PDIR_INDEX_ITEM SourceEntry, LinkEntry;
    uint_di Index;
    BOOLEAN ReplaceRequested = *Replace;
    ULONGLONG NewFidUniqueId;

    *TargetRemoved = *LinkCreated = FALSE;
    *Replace = FALSE;
    if (Vcb->VcbState & VCB_STATE_VOLUME_READ_ONLY)
        return STATUS_ACCESS_DENIED;
    ValidateFileInfo(DirInfo1);
    ValidateFileInfo(DirInfo2);
    ValidateFileInfo(FileInfo);
    if (!FileInfo->ParentFile || !FileInfo->FileIdent || UDFIsADirectory(FileInfo))
        return STATUS_INVALID_PARAMETER;
    if (FileInfo->Dloc->NamespaceScratch || FileInfo->Dloc->NamespaceTarget ||
        FileInfo->Dloc->NamespaceTargetState != UdfNamespaceTargetNone ||
        !NT_SUCCESS(FileInfo->Dloc->NamespaceTargetError) ||
        FileInfo->Dloc->NamespaceScratchOwner)
        return STATUS_SHARING_VIOLATION;
    if (UDFGetFileLinkCount(FileInfo) >= UDF_MAX_LINK_COUNT)
        return STATUS_TOO_MANY_LINKS;
    if (FileInfo->Dloc->FE_Flags & UDF_FE_FLAG_NEW_UNPUBLISHED) {
        Status = UDFFlushFile__(IrpContext, Vcb, FileInfo, 0);
        if (!NT_SUCCESS(Status))
            return Status;
    }
    Index = 0;
    if (DirInfo1->Dloc == DirInfo2->Dloc &&
        NT_SUCCESS(UDFFindFile(Vcb, IgnoreCase, TRUE, fn, DirInfo2, &Index)) &&
        Index == FileInfo->Index)
        return STATUS_OBJECT_NAME_COLLISION;

    /* Reserve the extra owner before any target can be removed. */
    Scratch = MyAllocatePoolTag__(UDF_FILE_INFO_MT, sizeof(UDF_FILE_INFO), MEM_FE_TAG);
    if (!Scratch)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Scratch, sizeof(*Scratch));
    _SEH2_TRY {
        Status = UDFAllocateFileUID(IrpContext, Vcb, &NewFidUniqueId);
        if (!NT_SUCCESS(Status))
            _SEH2_LEAVE;

        Status = UDFStageNamespaceDestination(IrpContext, Vcb, IgnoreCase,
            ReplaceRequested, TRUE, fn, DirInfo2, FileInfo, (uint_di)-1,
            (PUDF_FILE_INFO*)&Link, TargetRemoved);
        *Replace = *TargetRemoved;
        if (!NT_SUCCESS(Status) || !Link)
            _SEH2_LEAVE; /* same-FE replacement is a no-op */
        /* The stage owns the original temporary FI hold until adoption.
         * A failed conversion retains that owner instead of closing it. */
        Status = UDFAdoptHardLinkScratch(Vcb, FileInfo, Link, Scratch);
        if (!NT_SUCCESS(Status)) {
            /* The prepared stage retains its exact FI/parent hold. */
            HoldOwned = FALSE;
            Status = UDFQuarantineNamespace(Vcb, Status);
            _SEH2_LEAVE;
        }
        Scratch = NULL; /* now owned by Source.Dloc, never by this stack */
        Converted = TRUE;
        HoldOwned = TRUE; /* newly acquired exact Link/source-Dloc hold */
        SourceEntry = UDFDirIndex(DirInfo1->Dloc->DirIndex, FileInfo->Index);
        LinkEntry = UDFDirIndex(DirInfo2->Dloc->DirIndex, Link->Index);
        Link->FileIdent->icb = FileInfo->FileIdent->icb;
        Link->FileIdent->fileVersionNum = FileInfo->FileIdent->fileVersionNum;
        ((struct FidADImpUse*)&Link->FileIdent->icb.impUse)->uniqueID = (uint32)NewFidUniqueId;
        LinkEntry->FileEntryLoc = SourceEntry->FileEntryLoc;
        LinkEntry->FI_Flags = (LinkEntry->FI_Flags & ~UDF_FI_FLAG_SYS_ATTR) |
            UDF_FI_FLAG_FI_MODIFIED | UDF_FI_FLAG_LINKED;
        Status = UDFPublishNamespaceStage(Vcb, Link,
            SourceEntry->FileCharacteristics & ~FILE_DELETED);
        if (!NT_SUCCESS(Status))
            _SEH2_LEAVE;
        SourceEntry->FI_Flags |= UDF_FI_FLAG_LINKED;
        FileInfo->Dloc->FE_Flags |= UDF_FE_FLAG_FE_MODIFIED;
        UDFIncFileLinkCount(FileInfo);
        *LinkCreated = TRUE;

        /* Only successful FE/FID publication plus the lower barrier lets
         * the old FE become allocatable. Retire all its inner buffers. */
        FileInfo->Dloc->NamespacePhase = UdfNamespaceDestinationSubmitted;
        Status = UDFFlushFile__(IrpContext, Vcb, Link, 0);
        if (NT_SUCCESS(Status))
            Status = UDFBarrierFlushDevice(Vcb->TargetDeviceObject);
        if (NT_SUCCESS(Status)) {
            FileInfo->Dloc->NamespacePhase = UdfNamespaceCommitted;
            Status = UDFRetireNamespaceTarget(IrpContext, Vcb, FileInfo->Dloc);
        }
        if (NT_SUCCESS(Status))
            Status = UDFRetireNamespaceScratch(IrpContext, Vcb, FileInfo);
        if (!NT_SUCCESS(Status))
            Status = UDFQuarantineNamespace(Vcb, Status);
    } _SEH2_FINALLY {
        BOOLEAN Abnormal = _SEH2_AbnormalTermination();
        if (Abnormal && (Converted || *TargetRemoved || Vcb->PendingNamespaceCount))
            UDFQuarantineNamespace(Vcb, STATUS_UNSUCCESSFUL);
        _SEH2_TRY {
            if (Link && HoldOwned) {
                /* No second decrement if late close writeback raises. */
                HoldOwned = FALSE;
                CloseStatus = UDFDereferenceFileChecked(Vcb, Link);
                if (NT_SUCCESS(CloseStatus) && !Abnormal && NT_SUCCESS(Status))
                    CloseStatus = UDFCompleteFileClose(IrpContext, Vcb, Link);
                if (!Abnormal && NT_SUCCESS(Status)) Status = CloseStatus;
            }
            if (Link && !Abnormal && (UDFCleanUpFile__(Vcb, Link) & UDF_FREE_FILEINFO))
                MyFreePool__(Link);
        } _SEH2_FINALLY {
            if (Scratch)
                MyFreePool__(Scratch);
        } _SEH2_END;
    } _SEH2_END;
    return Status;
} // end UDFHardLinkFile__()

/*
    This internal constructor allocates an in-ICB zero-sized FE but leaves
    it UNDER_INIT/Constructing even on success. The stream-create owner must
    bind its exact parent before UDFCompleteFileInfoConstruction. A failed
    constructor does not own a successful-open reference to close.
 */
NTSTATUS
UDFCreateRootFile__(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
//    IN uint16 AllocMode, // short/long/ext/in-icb  // always in-ICB
    IN uint32 PartNum,
    IN uint32 ExtAttrSz,
    IN uint32 ImpUseLen,
    IN BOOLEAN Extended,
    OUT PUDF_FILE_INFO* _FileInfo
    )
{
    NTSTATUS status;
    LONG_AD FEicb;
    PUDF_FILE_INFO FileInfo;
    *_FileInfo = NULL;
    SIZE_T ReadBytes;
    ULONGLONG NewUniqueId;

    status = UDFAllocateFileUID(IrpContext, Vcb, &NewUniqueId);
    if (!NT_SUCCESS(status))
        return status;

    FileInfo = (PUDF_FILE_INFO)MyAllocatePoolTag__(UDF_FILE_INFO_MT,sizeof(UDF_FILE_INFO), MEM_FINF_TAG);
    *_FileInfo = FileInfo;
    if (!FileInfo)
        return STATUS_INSUFFICIENT_RESOURCES;
    ImpUseLen = (ImpUseLen + 3) & ~((uint16)3);

    RtlZeroMemory(FileInfo, sizeof(UDF_FILE_INFO));
    // init horizontal links
    FileInfo->NextLinkedFile =
    FileInfo->PrevLinkedFile = FileInfo;
    // allocate space for FileEntry
    if (!NT_SUCCESS(status =
        UDFBuildFileEntry(IrpContext, Vcb, NULL, FileInfo, PartNum, ICB_FLAG_AD_IN_ICB, ExtAttrSz, Extended) ))
        return status;
    FEicb.extLength = Vcb->SectorSize;
    FEicb.extLocation.logicalBlockNum = UDFPhysLbaToPart(Vcb, PartNum, FileInfo->Dloc->FELoc.Mapping[0].extLocation);
    FEicb.extLocation.partitionReferenceNum = (uint16)PartNum;
    RtlZeroMemory(&(FEicb.impUse), sizeof(FEicb.impUse));

    FileInfo->Dloc->DataLoc.Mapping = UDFExtentToMapping(&(FileInfo->Dloc->FELoc.Mapping[0]));
    if (!(FileInfo->Dloc->DataLoc.Mapping)) return STATUS_INSUFFICIENT_RESOURCES;
    FileInfo->Dloc->DataLoc.Length = 0;
    FileInfo->Dloc->DataLoc.Offset = FileInfo->Dloc->FileEntryLen;
    // init FileEntry
    UDFSetFileUID(FileInfo, NewUniqueId);
    UDFSetFileSize(FileInfo, 0);
    UDFIncFileLinkCount(FileInfo); // increase to 1
    UDFUpdateCreateTime(Vcb, FileInfo);
    // zero sector for FileEntry
    FileInfo->Dloc->DataLoc.Mapping[0].extLength &= UDF_EXTENT_LENGTH_MASK;
    FileInfo->Dloc->FELoc.Mapping[0].extLength &= UDF_EXTENT_LENGTH_MASK;
    status = UDFWriteData(IrpContext, Vcb, TRUE, ((int64)(FileInfo->Dloc->FELoc.Mapping[0].extLocation)) << Vcb->SectorShift, Vcb->SectorSize, FALSE, Vcb->ZBuffer, &ReadBytes);
    if (status == STATUS_SUCCESS && ReadBytes != Vcb->SectorSize)
        status = STATUS_IO_DEVICE_ERROR;
    if (status != STATUS_SUCCESS)
        return NT_SUCCESS(status) ? STATUS_IO_DEVICE_ERROR : status;

    /* The sole stream-directory caller must bind its parent and operation
     * owner before publishing the first successful FI reference. */
    return STATUS_SUCCESS;
} // end UDFCreateRootFile__()

/* Reserve a lifetime owner before any child allocation or I/O. Existing
 * Main/VCB writer ownership serializes the base representation; DlocResource2
 * serializes the exact FI hold and the global no-CLOSED count. */
static NTSTATUS
UDFReserveStreamCreate(PVCB Vcb, PUDF_FILE_INFO BaseFile,
                        PUDF_STREAM_CREATE_CONTEXT* Result)
{
    PUDF_DATALOC_INFO Dloc = BaseFile->Dloc;
    PUDF_STREAM_CREATE_CONTEXT Context;
    NTSTATUS Status = STATUS_SUCCESS;
    *Result = NULL;
    Context = (PUDF_STREAM_CREATE_CONTEXT)MyAllocatePool__(NonPagedPool, sizeof(*Context));
    if (!Context) return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Context, sizeof(*Context));
    UDFAcquireResourceExclusive(&Vcb->DlocResource2, TRUE);
    _SEH2_TRY {
        if (Dloc->StreamCreate) {
            Status = NT_SUCCESS(Dloc->StreamCreate->FirstError) ? STATUS_DEVICE_BUSY :
                     Dloc->StreamCreate->FirstError;
            _SEH2_LEAVE;
        }
        if (Dloc->CleanupOwner || Dloc->StreamDeleteOwner || Dloc->StreamDeleteContextOwner ||
            Dloc->NamespaceScratch || Dloc->NamespaceTarget || Dloc->NamespaceScratchOwner ||
            Dloc->DeleteCommitted || Vcb->PendingNamespaceCount < 0 ||
            Vcb->PendingNamespaceCount == MAXLONG || BaseFile->RefCount >= MAXLONG ||
            Dloc->LinkRefCount >= MAXLONG ||
            (BaseFile->ParentFile && BaseFile->ParentFile->OpenCount >= MAXLONG)) {
            Status = STATUS_DEVICE_BUSY;
            _SEH2_LEAVE;
        }
        Status = UDFReferenceFileChecked(Vcb, BaseFile);
        if (!NT_SUCCESS(Status)) _SEH2_LEAVE;
        Context->BaseFile = BaseFile;
        Context->Thread = PsGetCurrentThread();
        Context->Phase = UdfStreamCreateReserved;
        Dloc->StreamCreate = Context;
        InterlockedIncrement(&Vcb->PendingNamespaceCount);
        *Result = Context;
        Context = NULL;
    } _SEH2_FINALLY {
        UDFReleaseResource(&Vcb->DlocResource2);
        if (Context) MyFreePool__(Context);
    } _SEH2_END;
    return Status;
}

/* Non-I/O transfer. A successful child open goes to the caller exactly once;
 * an empty failed attempt can release just its extra base hold. Never close
 * or unlink a possibly written child from a finally/unwind path. */
static NTSTATUS
UDFFinishStreamCreate(PVCB Vcb, PUDF_STREAM_CREATE_CONTEXT* OwnedContext,
                       PUDF_FILE_INFO* ChildResult)
{
    PUDF_STREAM_CREATE_CONTEXT Context = *OwnedContext;
    PUDF_FILE_INFO BaseFile = Context->BaseFile, Child = Context->Child;
    PUDF_DATALOC_INFO Dloc = BaseFile->Dloc;
    NTSTATUS Status;
    UDFAcquireResourceExclusive(&Vcb->DlocResource2, TRUE);
    _SEH2_TRY {
        if (Dloc->StreamCreate != Context || Vcb->PendingNamespaceCount <= 0 ||
            (Child && (Context->Phase != UdfStreamCreateCommitted || !ChildResult ||
                       !Child->Dloc || Child->Dloc->StreamCreate != Context ||
                       Child->Constructing || !Child->RefCount))) {
            Status = STATUS_FILE_CORRUPT_ERROR;
            _SEH2_LEAVE;
        }
        Status = UDFDereferenceFileLocked(Vcb, BaseFile);
        if (!NT_SUCCESS(Status)) _SEH2_LEAVE;
        if (Child) Child->Dloc->StreamCreate = NULL;
        Dloc->StreamCreate = NULL;
        InterlockedDecrement(&Vcb->PendingNamespaceCount);
        Context->BaseFile = Context->Child = NULL;
        *OwnedContext = NULL;
        if (ChildResult) *ChildResult = Child;
    } _SEH2_FINALLY {
        UDFReleaseResource(&Vcb->DlocResource2);
    } _SEH2_END;
    if (!*OwnedContext) MyFreePool__(Context);
    return Status;
}

/* Child FE -> barrier -> base streamDirectoryICB FE -> barrier. A failed
 * generation retains its exact constructor/open/base owners and first error;
 * it is not converted to a successful destructive best-effort unlink. */
NTSTATUS
UDFCreateStreamDir__(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN PUDF_FILE_INFO FileInfo,
    OUT PUDF_FILE_INFO* _SDirInfo
    )
{
    PUDF_DATALOC_INFO Dloc;
    PUDF_STREAM_CREATE_CONTEXT Context = NULL;
    PUDF_FILE_INFO Child;
    PEXTENDED_FILE_ENTRY BaseEntry;
    PFCB Fcb;
    uint32 PartNum, Lbn, Header;
    NTSTATUS Status, ReleaseStatus;

    if (!_SDirInfo) return STATUS_INVALID_PARAMETER;
    *_SDirInfo = NULL;
    if (!IrpContext || !Vcb || !FileInfo || !(Dloc = FileInfo->Dloc))
        return STATUS_INVALID_PARAMETER;
    if (!UDFIsStreamsSupported(Vcb)) return STATUS_NOT_SUPPORTED;
    if (Dloc->StreamCreate)
        return NT_SUCCESS(Dloc->StreamCreate->FirstError) ? STATUS_DEVICE_BUSY : Dloc->StreamCreate->FirstError;
    if (Vcb->VcbState & VCB_STATE_VOLUME_READ_ONLY) return STATUS_MEDIA_WRITE_PROTECTED;
    if ((FileInfo->ParentFile && UDFIsAStreamDir(FileInfo->ParentFile)) || UDFHasAStreamDir(FileInfo))
        return STATUS_FILE_DELETED;
    if (Dloc->SDirInfo) return STATUS_ACCESS_DENIED;
    Fcb = Dloc->CommonFcb;
    if (Fcb) {
        if (!Fcb->FcbNonpaged || !Fcb->FileInfo || Fcb->FileInfo->Dloc != Dloc ||
            !ExIsResourceAcquiredExclusiveLite(&Fcb->FcbNonpaged->FcbResource) ||
            ExIsResourceAcquiredSharedLite(&Fcb->FcbNonpaged->FcbPagingIoResource))
            return STATUS_INVALID_DEVICE_STATE;
    } else if (FileInfo->Fcb || !ExIsResourceAcquiredExclusiveLite(&Vcb->VcbResource)) {
        return STATUS_INVALID_DEVICE_STATE;
    }
    if (!Dloc->FileEntry || Dloc->FileEntry->tagIdent != TID_EXTENDED_FILE_ENTRY) {
        Status = UDFConvertFEToExtended(IrpContext, Vcb, FileInfo);
        if (!NT_SUCCESS(Status)) return Status;
    }
    Status = UDFValidateFileEntryBuffer(Vcb, FileInfo, &Header);
    if (!NT_SUCCESS(Status)) return Status;
    BaseEntry = (PEXTENDED_FILE_ENTRY)Dloc->FileEntry;
    if (BaseEntry->streamDirectoryICB.extLength || !Dloc->FELoc.Mapping || !Vcb->Partitions)
        return STATUS_FILE_CORRUPT_ERROR;
    PartNum = UDFGetRefPartNumByPhysLba(Vcb, Dloc->FELoc.Mapping[0].extLocation);
    if (PartNum >= Vcb->PartitionMaps || PartNum > MAXUSHORT)
        return STATUS_FILE_CORRUPT_ERROR;
    Status = UDFReserveStreamCreate(Vcb, FileInfo, &Context);
    if (!NT_SUCCESS(Status)) return Status;
    _SEH2_TRY {
    _SEH2_TRY {
        /* Constructor output belongs to Context even if allocation or I/O
         * fails/raises before an ordinary child open has been completed. */
        Status = UDFCreateRootFile__(IrpContext, Vcb, PartNum, 0, 0, TRUE, &Context->Child);
        Child = Context->Child;
        if (Child && Child->Dloc) {
            UDFAcquireResourceExclusive(&Vcb->DlocResource2, TRUE);
            Child->Dloc->StreamCreate = Context;
            UDFReleaseResource(&Vcb->DlocResource2);
        }
        if (!NT_SUCCESS(Status)) goto stream_create_exit;
        if (!Child || !Child->Dloc || !Child->Dloc->FileEntry ||
            Child->Dloc->FileEntry->tagIdent != TID_EXTENDED_FILE_ENTRY) {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto stream_create_exit;
        }
        Context->Phase = UdfStreamCreateConstructed;
        UDFAcquireResourceExclusive(&Vcb->DlocResource2, TRUE);
        _SEH2_TRY {
            if (FileInfo->OpenCount >= MAXLONG || !Child->Constructing ||
                Child->ConstructionReferenceOwned || Child->RefCount) {
                Status = STATUS_FILE_CORRUPT_ERROR;
                _SEH2_LEAVE;
            }
            Status = UDFSetFileInfoConstructionParent(Vcb, Child, FileInfo);
            if (Status != STATUS_SUCCESS) _SEH2_LEAVE;
            /* This first successful child reference acquires the exact
             * parent OpenCount before any directory payload can be written. */
            Status = UDFCompleteFileInfoConstruction(Vcb, Child);
            if (NT_SUCCESS(Status)) Context->Phase = UdfStreamCreateChildOpen;
        } _SEH2_FINALLY {
            UDFReleaseResource(&Vcb->DlocResource2);
        } _SEH2_END;
        if (!NT_SUCCESS(Status)) goto stream_create_exit;
        Child->Dloc->FE_Flags |= UDF_FE_FLAG_FE_MODIFIED | UDF_FE_FLAG_IS_SDIR;
        ((PEXTENDED_FILE_ENTRY)Child->Dloc->FileEntry)->icbTag.fileType = UDF_FILE_TYPE_STREAMDIR;
        ((PEXTENDED_FILE_ENTRY)Child->Dloc->FileEntry)->uniqueID = BaseEntry->uniqueID;
        UDFDecFileLinkCount(Child); /* stream directories have no ordinary name link */
        Status = UDFRecordDirectory__(IrpContext, Vcb, Child);
        if (!NT_SUCCESS(Status)) goto stream_create_exit;
        Status = UDFFlushFE(IrpContext, Vcb, Child, PartNum);
        if (NT_SUCCESS(Status)) Status = UDFBarrierFlushDevice(Vcb->TargetDeviceObject);
        if (!NT_SUCCESS(Status)) goto stream_create_exit;
        Context->Phase = UdfStreamCreateChildDurable;
        Lbn = UDFPhysLbaToPart(Vcb, PartNum, Child->Dloc->FELoc.Mapping[0].extLocation);
        if (Lbn == LBA_OUT_OF_EXTENT) {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto stream_create_exit;
        }
        RtlZeroMemory(&BaseEntry->streamDirectoryICB, sizeof(BaseEntry->streamDirectoryICB));
        BaseEntry->streamDirectoryICB.extLength = Vcb->SectorSize;
        BaseEntry->streamDirectoryICB.extLocation.partitionReferenceNum = (uint16)PartNum;
        BaseEntry->streamDirectoryICB.extLocation.logicalBlockNum = Lbn;
        Dloc->SDirInfo = Child;
        Dloc->FE_Flags &= ~UDF_FE_FLAG_HAS_DEL_SDIR;
        Dloc->FE_Flags |= UDF_FE_FLAG_FE_MODIFIED | UDF_FE_FLAG_HAS_SDIR;
        Context->Phase = UdfStreamCreateBasePrepared;
        Status = UDFFlushFE(IrpContext, Vcb, FileInfo, PartNum);
        if (NT_SUCCESS(Status)) Status = UDFBarrierFlushDevice(Vcb->TargetDeviceObject);
        if (!NT_SUCCESS(Status)) goto stream_create_exit;
        Context->Phase = UdfStreamCreateCommitted;
        Status = UDFFinishStreamCreate(Vcb, &Context, _SDirInfo);
stream_create_exit:
        if (Status != STATUS_SUCCESS && NT_SUCCESS(Status)) Status = STATUS_IO_DEVICE_ERROR;
    } _SEH2_EXCEPT(FsRtlIsNtstatusExpected(_SEH2_GetExceptionCode()) ?
                  EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        Status = _SEH2_GetExceptionCode();
    } _SEH2_END;
    } _SEH2_FINALLY {
        if (_SEH2_AbnormalTermination()) Status = STATUS_FILE_CORRUPT_ERROR;
        if (Context) {
            Child = Context->Child;
            if (Child && !Child->Dloc) {
                /* The root constructor's unbound shell has no inner
                 * allocations or successful-open hold to close. */
                MyFreePool__(Child);
                Context->Child = NULL;
            }
            if (!Context->Child) {
                ReleaseStatus = UDFFinishStreamCreate(Vcb, &Context, NULL);
                if (NT_SUCCESS(Status) && !NT_SUCCESS(ReleaseStatus)) Status = ReleaseStatus;
            }
            if (Context) {
                UDFAcquireResourceExclusive(&Vcb->DlocResource2, TRUE);
                if (Context->Child && Context->Child->Dloc)
                    Context->Child->Dloc->StreamCreate = Context;
                if (NT_SUCCESS(Context->FirstError))
                    Context->FirstError = NT_SUCCESS(Status) ? STATUS_FILE_CORRUPT_ERROR : Status;
                Context->Thread = NULL;
                UDFReleaseResource(&Vcb->DlocResource2);
                Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
                InterlockedCompareExchange(&Vcb->MetadataWritebackError,
                                            Context->FirstError, STATUS_SUCCESS);
                if (NT_SUCCESS(Status)) Status = Context->FirstError;
            }
        }
    } _SEH2_END;
    return Status;
} // end UDFCreateStreamDir__()

/*
    This routine opens Stream Directory associated with file specified
 */
NTSTATUS
UDFOpenStreamDir__(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN PUDF_FILE_INFO FileInfo,    // file containing stream-dir
    OUT PUDF_FILE_INFO* _SDirInfo  // this is to be filled & doesn't contain
                                   // any pointers
    )
{
    NTSTATUS status, ReleaseStatus;
    BOOLEAN ClonedReference = FALSE;
    PUDF_FILE_INFO SDirInfo;
    PUDF_FILE_INFO ParSDirInfo;
    uint16 Ident;
    uint32 Header;

    if (!_SDirInfo) return STATUS_INVALID_PARAMETER;
    *_SDirInfo = NULL;
    if (!IrpContext || !Vcb || !FileInfo || !FileInfo->Dloc)
        return STATUS_INVALID_PARAMETER;
    status = UDFValidateFileEntryBuffer(Vcb, FileInfo, &Header);
    if (!NT_SUCCESS(status)) return status;
    ValidateFileInfo(FileInfo);
    if (FileInfo->Dloc->StreamCreate)
        return NT_SUCCESS(FileInfo->Dloc->StreamCreate->FirstError) ? STATUS_DEVICE_BUSY :
               FileInfo->Dloc->StreamCreate->FirstError;
    // check if this file has ExtendedFileEntry
    if ((Ident = FileInfo->Dloc->FileEntry->tagIdent) != TID_EXTENDED_FILE_ENTRY) {
        return STATUS_NOT_FOUND;
    }
    if ((SDirInfo = FileInfo->Dloc->SDirInfo)) {
        // it is already opened. Good...

        // check if we have Deleted SDir
        if (FileInfo->Dloc->SDirInfo &&
           UDFIsSDirDeleted(FileInfo->Dloc->SDirInfo))
            return STATUS_FILE_DELETED;
        // All right. Look for parallel SDir (if any)
        if (SDirInfo->ParentFile != FileInfo) {
            ParSDirInfo = UDFLocateParallelFI(FileInfo, 0, SDirInfo);
            if (!ParSDirInfo || ParSDirInfo->ParentFile != FileInfo) {
                SDirInfo = (PUDF_FILE_INFO)MyAllocatePoolTag__(UDF_FILE_INFO_MT,sizeof(UDF_FILE_INFO), MEM_SDFINF_TAG);
                *_SDirInfo = SDirInfo;
                if (!SDirInfo) return STATUS_INSUFFICIENT_RESOURCES;
                RtlCopyMemory(SDirInfo, FileInfo->Dloc->SDirInfo, sizeof(UDF_FILE_INFO));
    //          SDirInfo->NextLinkedFile = FileInfo->Dloc->SDirInfo->NextLinkedFile; // is already done
                SDirInfo->RefCount = 0;
                SDirInfo->OpenCount = 0;
                SDirInfo->ListPtr = NULL;
                SDirInfo->ParentFile = FileInfo;
                SDirInfo->Fcb = NULL;
                status = UDFReferenceClonedFile(Vcb, SDirInfo, FileInfo->Dloc->SDirInfo);
                if (!NT_SUCCESS(status)) {
                    MyFreePool__(SDirInfo);
                    *_SDirInfo = NULL;
                    return status;
                }
                ClonedReference = TRUE;
            } else {
                SDirInfo = ParSDirInfo;
            }
        }
        if (!ClonedReference) {
            status = UDFReferenceFileChecked(Vcb, SDirInfo);
            if (!NT_SUCCESS(status)) return status;
        }
        *_SDirInfo = SDirInfo;
        return STATUS_SUCCESS;
    }
    // normal open
    if (!((PEXTENDED_FILE_ENTRY)(FileInfo->Dloc->FileEntry))->streamDirectoryICB.extLength)
        return STATUS_NOT_FOUND;
    SDirInfo = (PUDF_FILE_INFO)MyAllocatePoolTag__(UDF_FILE_INFO_MT,sizeof(UDF_FILE_INFO), MEM_SDFINF_TAG);
    if (!SDirInfo) return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(SDirInfo, sizeof(*SDirInfo));
    status = STATUS_UNSUCCESSFUL;
    _SEH2_TRY {
        _SEH2_TRY {
            status = UDFOpenRootFileWithParent__(IrpContext, Vcb,
                &((PEXTENDED_FILE_ENTRY)FileInfo->Dloc->FileEntry)->streamDirectoryICB.extLocation,
                FileInfo, UDF_FILE_TYPE_STREAMDIR, SDirInfo);
            if (status != STATUS_SUCCESS && NT_SUCCESS(status)) status = STATUS_IO_DEVICE_ERROR;
        } _SEH2_EXCEPT(FsRtlIsNtstatusExpected(_SEH2_GetExceptionCode()) ?
                      EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
            status = _SEH2_GetExceptionCode();
        } _SEH2_END;
    } _SEH2_FINALLY {
        if (_SEH2_AbnormalTermination()) status = STATUS_FILE_CORRUPT_ERROR;
        if (status != STATUS_SUCCESS) {
            /* These failures precede constructor completion; no ordinary
             * open is closed twice or silently dropped by a caller which
             * ignores its failure output. Preserve the exact constructor
             * hold/parent for the existing failed-generation collector. */
            ReleaseStatus = UDFRetainFailedStreamOpen(Vcb, &SDirInfo);
            if (NT_SUCCESS(status) && !NT_SUCCESS(ReleaseStatus)) status = ReleaseStatus;
        }
    } _SEH2_END;
    if (status != STATUS_SUCCESS) return status;
    /* The successful constructor already owns its exact parent count and
     * indexed with that context. Publish only the weak cache link here. */
    FileInfo->Dloc->FE_Flags |= UDF_FE_FLAG_HAS_SDIR;
    FileInfo->Dloc->SDirInfo = SDirInfo;
    *_SDirInfo = SDirInfo;

    return STATUS_SUCCESS;
} // end UDFOpenStreamDir__()

/*
    Sequential VAT checkpoint contract; the append backend is unavailable.
 */
NTSTATUS
UDFRecordVAT(IN PIRP_CONTEXT IrpContext, IN PVCB Vcb)
{
    UNREFERENCED_PARAMETER(IrpContext);
    UNREFERENCED_PARAMETER(Vcb);
    /* The removed legacy path detached/freed the live translation before
     * allocation/I/O succeeded, predicted packet addresses without a writer
     * advancing NWA, and could report success with no durable VAT. Keep the
     * table, FE and all mappings intact until a real append/checkpoint
     * backend implements that complete protocol. This is no clean close. */
    UDFPrint(("UDFS: sequential VAT recording requires an append/checkpoint backend\n"));
    return STATUS_NOT_SUPPORTED;
} // end UDFRecordVAT()

/*
    Sequential VAT completion contract; no current writer invokes it.
 */
NTSTATUS
UDFUpdateVAT(IN void* _Vcb, IN uint32 Lba, IN uint32* RelocTab, IN uint32 BCount)
{
    UNREFERENCED_PARAMETER(_Vcb);
    UNREFERENCED_PARAMETER(Lba);
    UNREFERENCED_PARAMETER(RelocTab);
    UNREFERENCED_PARAMETER(BCount);
    /* No current I/O completion calls this callback. It cannot acknowledge
     * or mutate a translation without owning an actual completed append. */
    return STATUS_NOT_SUPPORTED;
} // end UDFUpdateVAT()

/*
    This routine converts file's FE to extended form.
    It is needed for stream creation.
 */
NTSTATUS
UDFConvertFEToExtended(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN PUDF_FILE_INFO FileInfo
    )
{
    PUDF_DATALOC_INFO Dloc;
    PFILE_ENTRY FileEntry;
    PEXTENDED_FILE_ENTRY ExFileEntry = NULL;
    PFCB Fcb;
    EXTENT_INFO DataPlan, AllocPlan, Borrowed;
    PUDF_ALLOCATION_FAILURE Lease = NULL;
    PEXTENT_MAP OldDataMap = NULL;
    uint32 Header, NewHeader, PartNum, DataPartNum, Bytes;
    uint64 NewHeaderBytes, Cover;
    uint16 Mode;
    SIZE_T Written = 0;
    NTSTATUS Status = STATUS_SUCCESS, LeaseStatus;
    BOOLEAN PagingAcquired = FALSE, Promote = FALSE;
    BOOLEAN LiveChanged = FALSE, Installed = FALSE;

    if (!IrpContext || !Vcb || !FileInfo || !(Dloc = FileInfo->Dloc))
        return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(&DataPlan, sizeof(DataPlan));
    RtlZeroMemory(&AllocPlan, sizeof(AllocPlan));
    RtlZeroMemory(&Borrowed, sizeof(Borrowed));
    /* The create path owns the canonical Main resource. An internal FI
     * without an FCB requires the exclusive volume writer domain instead.
     * Do not silently upgrade shared locks or enter Cc under Paging. */
    Fcb = Dloc->CommonFcb;
    if (Fcb) {
        if ((FileInfo->Fcb && FileInfo->Fcb != Fcb) || !Fcb->FcbNonpaged || !Fcb->FileInfo || Fcb->FileInfo->Dloc != Dloc ||
            Fcb->Header.PagingIoResource != &Fcb->FcbNonpaged->FcbPagingIoResource ||
            !ExIsResourceAcquiredExclusiveLite(&Fcb->FcbNonpaged->FcbResource) ||
            ExIsResourceAcquiredSharedLite(&Fcb->FcbNonpaged->FcbPagingIoResource))
            return STATUS_INVALID_DEVICE_STATE;
    } else if (FileInfo->Fcb || !ExIsResourceAcquiredExclusiveLite(&Vcb->VcbResource)) {
        return STATUS_INVALID_DEVICE_STATE;
    }
    _SEH2_TRY {
    _SEH2_TRY {
        Status = UDFDrainNewFileData(IrpContext, Vcb, FileInfo);
        if (!NT_SUCCESS(Status)) goto conversion_exit;
        if (Fcb) {
            UDFAcquirePagingIoExclusive(IrpContext, Fcb);
            PagingAcquired = TRUE;
        }
        Status = UDFFlushDirectoryAppend(IrpContext, Vcb, FileInfo);
        if (!NT_SUCCESS(Status)) goto conversion_exit;
        Status = UDFValidateFileEntryBuffer(Vcb, FileInfo, &Header);
        if (!NT_SUCCESS(Status)) goto conversion_exit;
        if (Dloc->FileEntry->tagIdent == TID_EXTENDED_FILE_ENTRY)
            goto conversion_exit;
        if (Vcb->Vat || Vcb->CDR_Mode || (Vcb->VcbState & VCB_STATE_VOLUME_READ_ONLY)) {
            Status = STATUS_MEDIA_WRITE_PROTECTED;
            goto conversion_exit;
        }
        if (!NT_SUCCESS(Dloc->ZeroDataError)) {
            Status = Dloc->ZeroDataError;
            goto conversion_exit;
        }
        FileEntry = (PFILE_ENTRY)Dloc->FileEntry;
        Mode = FileEntry->icbTag.flags & ICB_FLAG_ALLOC_MASK;
        if (!Dloc->FELoc.Mapping || Dloc->FELoc.Offset ||
            Dloc->FELoc.Mapping[0].extLength != Vcb->SectorSize ||
            Dloc->FELoc.Mapping[1].extLength || !Dloc->DataLoc.Mapping ||
            Dloc->DataLoc.Mapping == Dloc->FELoc.Mapping ||
            Dloc->DataLoc.Length < 0 ||
            FileEntry->informationLength != (uint64)Dloc->DataLoc.Length ||
            FileEntry->lengthAllocDescs > Vcb->SectorSize - Header) {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto conversion_exit;
        }
        NewHeaderBytes = (uint64)Header - sizeof(FILE_ENTRY) + sizeof(EXTENDED_FILE_ENTRY);
        if (NewHeaderBytes > Vcb->SectorSize) {
            Status = STATUS_NOT_SUPPORTED; /* one-sector FE/EFE profile */
            goto conversion_exit;
        }
        NewHeader = (uint32)NewHeaderBytes;
        if (!Vcb->Partitions || !Vcb->PartitionMaps) {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto conversion_exit;
        }
        PartNum = UDFGetRefPartNumByPhysLba(Vcb, Dloc->FELoc.Mapping[0].extLocation);
        if (PartNum >= Vcb->PartitionMaps) {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto conversion_exit;
        }
        DataPartNum = PartNum;
        if (Vcb->MetadataFileInfo && PartNum == Vcb->MetadataRef && !UDFIsADirectory(FileInfo))
            DataPartNum = Vcb->MetadataPhysRef;
        if (DataPartNum >= Vcb->PartitionMaps) {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto conversion_exit;
        }
        if (Mode == ICB_FLAG_AD_IN_ICB) {
            if (Dloc->DataLoc.Offset != Header ||
                (uint64)Dloc->DataLoc.Length > Vcb->SectorSize - Header ||
                Dloc->DataLoc.Mapping[0].extLocation != Dloc->FELoc.Mapping[0].extLocation ||
                Dloc->DataLoc.Mapping[0].extLength != Vcb->SectorSize ||
                Dloc->DataLoc.Mapping[1].extLength || Dloc->AllocLoc.Mapping ||
                Dloc->AllocLoc.Length || Dloc->AllocLoc.Offset) {
                Status = STATUS_FILE_CORRUPT_ERROR;
                goto conversion_exit;
            }
            Promote = (uint64)Dloc->DataLoc.Length > Vcb->SectorSize - NewHeader;
        } else if (Mode == ICB_FLAG_AD_SHORT || Mode == ICB_FLAG_AD_LONG) {
            if (Dloc->DataLoc.Offset || !Dloc->AllocLoc.Mapping ||
                Dloc->AllocLoc.Mapping == Dloc->DataLoc.Mapping ||
                Dloc->AllocLoc.Mapping == Dloc->FELoc.Mapping ||
                Dloc->AllocLoc.Offset != Header || Dloc->AllocLoc.Length < 0 ||
                Dloc->AllocLoc.Mapping[0].extLocation != Dloc->FELoc.Mapping[0].extLocation ||
                (Dloc->AllocLoc.Mapping[0].extLength & UDF_EXTENT_LENGTH_MASK) < Vcb->SectorSize ||
                (Dloc->AllocLoc.Mapping[0].extLength >> 30) > EXTENT_NOT_RECORDED_ALLOCATED) {
                Status = STATUS_FILE_CORRUPT_ERROR;
                goto conversion_exit;
            }
        } else {
            Status = STATUS_NOT_SUPPORTED;
            goto conversion_exit;
        }
        ExFileEntry = (PEXTENDED_FILE_ENTRY)MyAllocatePoolTag__(NonPagedPool,
                                                           Vcb->SectorSize, MEM_XFE_TAG);
        if (!ExFileEntry) {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto conversion_exit;
        }
        RtlZeroMemory(ExFileEntry, Vcb->SectorSize);
        ExFileEntry->descTag = FileEntry->descTag;
        ExFileEntry->descTag.tagIdent = TID_EXTENDED_FILE_ENTRY;
        ExFileEntry->descTag.descCRC = ExFileEntry->descTag.descCRCLength = 0;
        ExFileEntry->descTag.tagChecksum = 0; /* regenerated at FE serialization */
        ExFileEntry->icbTag = FileEntry->icbTag;
        ExFileEntry->uid = FileEntry->uid;
        ExFileEntry->gid = FileEntry->gid;
        ExFileEntry->permissions = FileEntry->permissions;
        ExFileEntry->fileLinkCount = FileEntry->fileLinkCount;
        ExFileEntry->recordFormat = FileEntry->recordFormat;
        ExFileEntry->recordDisplayAttr = FileEntry->recordDisplayAttr;
        ExFileEntry->recordLength = FileEntry->recordLength;
        ExFileEntry->informationLength = FileEntry->informationLength;
        /* A plain FE cannot already own a stream directory. Preserve the
         * existing FE creation-time fallback (its modification time). */
        ExFileEntry->objectSize = FileEntry->informationLength;
        ExFileEntry->logicalBlocksRecorded = FileEntry->logicalBlocksRecorded;
        ExFileEntry->accessTime = FileEntry->accessTime;
        ExFileEntry->modificationTime = FileEntry->modificationTime;
        ExFileEntry->createTime = FileEntry->modificationTime;
        ExFileEntry->attrTime = FileEntry->attrTime;
        ExFileEntry->checkpoint = FileEntry->checkpoint;
        ExFileEntry->extendedAttrICB = FileEntry->extendedAttrICB;
        ExFileEntry->impIdent = FileEntry->impIdent;
        ExFileEntry->uniqueID = FileEntry->uniqueID;
        ExFileEntry->lengthExtendedAttr = FileEntry->lengthExtendedAttr;
        RtlCopyMemory(ExFileEntry + 1, FileEntry + 1, FileEntry->lengthExtendedAttr);
        if (Mode == ICB_FLAG_AD_IN_ICB && !Promote) {
            ExFileEntry->lengthAllocDescs = (uint32)Dloc->DataLoc.Length;
            RtlCopyMemory((int8*)ExFileEntry + NewHeader,
                          (int8*)FileEntry + Header, (SIZE_T)Dloc->DataLoc.Length);
        } else if (Promote) {
            /* The lease owns an immutable borrowed map. Copy it before
             * WriteExtent can replace the recorded/unrecorded work map. */
            Cover = ((uint64)Dloc->DataLoc.Length + Vcb->SectorSize - 1) &
                    ~((uint64)Vcb->SectorSize - 1);
            Status = UDFAllocExtentLease(IrpContext, Vcb, DataPartNum, (int64)Cover,
                UDFPartStart(Vcb, DataPartNum), UDFPartEnd(Vcb, DataPartNum),
                &Borrowed, EXTENT_FLAG_ALLOC_STD, &Lease);
            if (!NT_SUCCESS(Status)) goto conversion_exit;
            if (!Lease || !Borrowed.Mapping ||
                (uint64)UDFGetExtentLength(Borrowed.Mapping) != Cover) {
                Status = STATUS_FILE_CORRUPT_ERROR;
                goto conversion_exit;
            }
            Status = UDFGetMappingByteCount(Borrowed.Mapping, &Bytes);
            if (!NT_SUCCESS(Status)) goto conversion_exit;
            DataPlan.Mapping = (PEXTENT_MAP)MyAllocatePoolTag__(NonPagedPool, Bytes, MEM_EXTMAP_TAG);
            AllocPlan.Mapping = (PEXTENT_MAP)MyAllocatePoolTag__(NonPagedPool,
                                                          2 * sizeof(EXTENT_MAP), MEM_EXTMAP_TAG);
            if (!DataPlan.Mapping || !AllocPlan.Mapping) {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                goto conversion_exit;
            }
            RtlCopyMemory(DataPlan.Mapping, Borrowed.Mapping, Bytes);
            DataPlan.Length = Dloc->DataLoc.Length;
            DataPlan.Modified = TRUE;
            RtlZeroMemory(AllocPlan.Mapping, 2 * sizeof(EXTENT_MAP));
            AllocPlan.Mapping[0] = Dloc->FELoc.Mapping[0];
            AllocPlan.Offset = NewHeader;
            AllocPlan.Modified = TRUE;
            Status = UDFWriteExtent(IrpContext, Vcb, &DataPlan, 0,
                (SIZE_T)Dloc->DataLoc.Length, FALSE, (int8*)FileEntry + Header, &Written);
            if (NT_SUCCESS(Status) && Written != (SIZE_T)Dloc->DataLoc.Length)
                Status = STATUS_IO_DEVICE_ERROR;
            if (!NT_SUCCESS(Status)) goto conversion_exit;
            ExFileEntry->icbTag.flags &= ~ICB_FLAG_ALLOC_MASK;
            ExFileEntry->icbTag.flags |= (PartNum == DataPartNum) ? ICB_FLAG_AD_SHORT : ICB_FLAG_AD_LONG;
            Status = UDFPublishAllocationLease(Vcb, Lease);
            if (!NT_SUCCESS(Status)) goto conversion_exit;
        }
        /* No fallible byte/map preparation remains. Keep the sector buffer
         * address stable; raw FE readers under the writer domain cannot be
         * left pointing at freed storage. No old physical block is released. */
        LiveChanged = TRUE;
        Dloc->FE_Flags |= UDF_FE_FLAG_FE_MODIFIED;
        if (Promote) {
            OldDataMap = Dloc->DataLoc.Mapping;
            Dloc->DataLoc = DataPlan;
            DataPlan.Mapping = NULL;
            Dloc->AllocLoc = AllocPlan;
            AllocPlan.Mapping = NULL;
        } else if (Mode == ICB_FLAG_AD_IN_ICB) {
            Dloc->DataLoc.Offset = NewHeader;
            Dloc->DataLoc.Modified = TRUE;
        } else {
            /* Keep every existing AED owner. The counted builder rebuilds
             * the shorter FE prefix and resizes/retires its old chain later. */
            Dloc->AllocLoc.Offset = NewHeader;
            Dloc->AllocLoc.Modified = TRUE;
            Dloc->DataLoc.Modified = TRUE;
        }
        RtlCopyMemory(Dloc->FileEntry, ExFileEntry, Vcb->SectorSize);
        Dloc->FileEntryLen = NewHeader;
        Dloc->FELoc.Length = NewHeader;
        Dloc->FELoc.Modified = TRUE;
        if (Fcb) {
            if (Dloc->DataLoc.Offset)
                Fcb->FcbState |= UDF_FCB_EMBEDDED_DATA;
            else
                Fcb->FcbState &= ~UDF_FCB_EMBEDDED_DATA;
        }
        if (Vcb->minUDFReadRev < 0x0200) Vcb->minUDFReadRev = 0x0200;
        Installed = TRUE;
conversion_exit:
        NOTHING;
    } _SEH2_EXCEPT(FsRtlIsNtstatusExpected(_SEH2_GetExceptionCode()) ?
                  EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        Status = _SEH2_GetExceptionCode();
    } _SEH2_END;
    } _SEH2_FINALLY {
        if (_SEH2_AbnormalTermination()) Status = STATUS_FILE_CORRUPT_ERROR;
        if (Lease) {
            if (Installed && !_SEH2_AbnormalTermination())
                LeaseStatus = UDFCommitAllocationLease(Vcb, &Lease);
            else
                LeaseStatus = UDFAbortAllocationLease(Vcb, &Lease, Status);
            if (NT_SUCCESS(Status) && !NT_SUCCESS(LeaseStatus)) Status = LeaseStatus;
            if (Lease) {
                Lease->Uncertain = TRUE;
                UDFAbortAllocationLease(Vcb, &Lease, Status);
            }
        }
        if (OldDataMap) MyFreePool__(OldDataMap);
        if (DataPlan.Mapping) MyFreePool__(DataPlan.Mapping);
        if (AllocPlan.Mapping) MyFreePool__(AllocPlan.Mapping);
        if (ExFileEntry) MyFreePool__(ExFileEntry);
        if (LiveChanged && !NT_SUCCESS(Status)) {
            Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
            UDFRememberMetadataWritebackError(IrpContext, Vcb, FileInfo, Status);
        }
        if (PagingAcquired) UDFReleasePagingIo(IrpContext, Fcb);
    } _SEH2_END;
    return Status;
} // end UDFConvertFEToExtended()

/*
    This routine makes file almost unavailable for external callers.
    The only way to access Hidden with this routine file is OpenByIndex.
    It is usefull calling this routine to pretend file to be deleted,
    for ex. when we have UDFCleanUp__() or smth. like this in progress,
    but we want to create file with the same name.
 */
NTSTATUS
UDFPretendFileDeleted__(
    IN PVCB Vcb,
    IN PUDF_FILE_INFO FileInfo
    )
{
    AdPrint(("UDFPretendFileDeleted__:\n"));

    NTSTATUS RC;
    PDIR_INDEX_HDR hDirNdx = UDFGetDirIndexByFileInfo(FileInfo);
    if (!hDirNdx) return STATUS_CANNOT_DELETE;
    PDIR_INDEX_ITEM DirNdx = UDFDirIndex(hDirNdx, FileInfo->Index);
    if (!DirNdx) return STATUS_CANNOT_DELETE;


    // we can't hide file that is not marked as deleted
    RC = UDFDoesOSAllowFilePretendDeleted__(FileInfo);
    if (!NT_SUCCESS(RC))
        return RC;

    AdPrint(("UDFPretendFileDeleted__: set UDF_FI_FLAG_FI_INTERNAL\n"));

    DirNdx->FI_Flags |= UDF_FI_FLAG_FI_INTERNAL;
    if (DirNdx->FName.Buffer) {
        MyFreePool__(DirNdx->FName.Buffer);
        DirNdx->FName.Buffer = NULL;
        DirNdx->FName.Length =
        DirNdx->FName.MaximumLength = 0;
    }
    return STATUS_SUCCESS;
} // end UDFPretendFileDeleted__()
