////////////////////////////////////////////////////////////////////
// Copyright (C) Alexander Telyatnikov, Ivan Keliukh, Yegor Anchishkin, SKIF Software, 1999-2013. Kiev, Ukraine
// All rights reserved
// This file was released under the GPLv2 on June 2015.
////////////////////////////////////////////////////////////////////
/*
        Module name:

   alloc.cpp

        Abstract:

   This file contains filesystem-specific routines
   responsible for disk space management

*/

#include "udf.h"

#define         UDF_BUG_CHECK_ID                UDF_FILE_UDF_INFO_ALLOC

static const int8 bit_count_tab[] = {
    0, 1, 1, 2, 1, 2, 2, 3,   1, 2, 2, 3, 2, 3, 3, 4,
    1, 2, 2, 3, 2, 3, 3, 4,   2, 3, 3, 4, 3, 4, 4, 5,
    1, 2, 2, 3, 2, 3, 3, 4,   2, 3, 3, 4, 3, 4, 4, 5,
    2, 3, 3, 4, 3, 4, 4, 5,   3, 4, 4, 5, 4, 5, 5, 6,
    1, 2, 2, 3, 2, 3, 3, 4,   2, 3, 3, 4, 3, 4, 4, 5,
    2, 3, 3, 4, 3, 4, 4, 5,   3, 4, 4, 5, 4, 5, 5, 6,
    2, 3, 3, 4, 3, 4, 4, 5,   3, 4, 4, 5, 4, 5, 5, 6,
    3, 4, 4, 5, 4, 5, 5, 6,   4, 5, 5, 6, 5, 6, 6, 7,

    1, 2, 2, 3, 2, 3, 3, 4,   2, 3, 3, 4, 3, 4, 4, 5,
    2, 3, 3, 4, 3, 4, 4, 5,   3, 4, 4, 5, 4, 5, 5, 6,
    2, 3, 3, 4, 3, 4, 4, 5,   3, 4, 4, 5, 4, 5, 5, 6,
    3, 4, 4, 5, 4, 5, 5, 6,   4, 5, 5, 6, 5, 6, 6, 7,
    2, 3, 3, 4, 3, 4, 4, 5,   3, 4, 4, 5, 4, 5, 5, 6,
    3, 4, 4, 5, 4, 5, 5, 6,   4, 5, 5, 6, 5, 6, 6, 7,
    3, 4, 4, 5, 4, 5, 5, 6,   4, 5, 5, 6, 5, 6, 6, 7,
    4, 5, 5, 6, 5, 6, 6, 7,   5, 6, 6, 7, 6, 7, 7, 8
};

/*
    RtlFindNextForwardRunSet — not exported by ReactOS ntoskrnl,
    but statically linked into MS udfs.sys. Same algorithm here.
    Finds the next run of set (1) bits starting at or after FromIndex.
 */
static
ULONG
UDFBitmapFindNextRunSet(
    IN PRTL_BITMAP BitMapHeader,
    IN ULONG FromIndex,
    OUT PULONG StartingRunIndex
    )
{
    ULONG Size = BitMapHeader->SizeOfBitMap;
    PULONG Buf = BitMapHeader->Buffer;
    ULONG Idx = FromIndex;

    if (Size == 0) {
        *StartingRunIndex = Idx;
        return 0;
    }

    ULONG LastWord = (Size - 1) >> 5;

    // Phase 1: skip clear bits, find first set bit
    // Skip partial first word
    ULONG Word = Idx >> 5;
    if ((Idx & 31) && Word <= LastWord) {
        // Mask off bits below Idx within this word
        ULONG Val = Buf[Word] & ~((1UL << (Idx & 31)) - 1);
        if (Val) {
            // Found a set bit — count trailing zeros
            Idx = (Word << 5);
            while (!(Val & 1)) { Val >>= 1; Idx++; }
            goto FoundStart;
        }
        Idx = (Word + 1) << 5;
        Word++;
    }
    // Skip whole zero words
    while (Word <= LastWord && Buf[Word] == 0) { Word++; Idx += 32; }
    // Scan within the non-zero word
    if (Word <= LastWord) {
        ULONG Val = Buf[Word];
        Idx = Word << 5;
        while (!(Val & 1)) { Val >>= 1; Idx++; }
    }

FoundStart:
    if (Idx >= Size) {
        *StartingRunIndex = Size;
        return 0;
    }
    *StartingRunIndex = Idx;

    // Phase 2: count consecutive set bits
    ULONG End = Idx;
    Word = End >> 5;
    // Check partial first word
    if ((End & 31) && Word <= LastWord) {
        ULONG Val = Buf[Word] >> (End & 31);
        while ((End & 31) && (Val & 1)) { Val >>= 1; End++; }
        if (End & 31) goto Done;  // Run ended mid-word
        Word++;
    }
    // Skip whole 0xFFFFFFFF words
    while (Word <= LastWord && Buf[Word] == 0xFFFFFFFF) { Word++; End += 32; }
    // Scan partial word at run end
    if (Word <= LastWord) {
        ULONG Val = Buf[Word];
        while ((Val & 1)) { Val >>= 1; End++; }
    }

Done:
    if (End > Size) End = Size;
    return End - Idx;
}

/*
    Pin the bitmap cache page containing the given LBN (BitIndex).
    Initializes RTL_BITMAP on the pinned data for bounds-safe access.
    Stores result in Vcb->BitmapRtl, BitmapPageStartLbn, BitmapPageBitCount.
 */
VOID
UDFPinBitmapPage(
    IN PVCB Vcb,
    IN ULONG Lbn
    )
{
    uint64 StreamOffset, AllocationEnd, BitmapEnd, DataStart, DataEnd;
    uint32 PinBase, PinLength, StartLbn, BitCount;
    SIZE_T BufferOffset, WordBytes;
    LARGE_INTEGER Offset;
    PVOID Buffer = NULL;

    if (!Vcb->BitmapFcb || !Vcb->BitmapStreamFileObject ||
        Vcb->BitmapCacheState != UdfBitmapCacheActive || !Vcb->BitmapCacheInitialized)
        ExRaiseStatus(STATUS_FILE_CLOSED);
    if (Lbn >= Vcb->FSBM_BitCount ||
        ((uint64)Vcb->FSBM_BitCount + 7) / 8 > Vcb->FSBM_ByteCount)
        ExRaiseStatus(STATUS_DISK_CORRUPT_ERROR);
    StreamOffset = (uint64)Vcb->BitmapDataOffset + (Lbn >> 3);
    BitmapEnd = (uint64)Vcb->BitmapDataOffset + Vcb->FSBM_ByteCount;
    AllocationEnd = Vcb->BitmapFcb->Header.AllocationSize.QuadPart;
    if (AllocationEnd > MAXULONG || BitmapEnd > AllocationEnd || StreamOffset >= BitmapEnd)
        ExRaiseStatus(STATUS_DISK_CORRUPT_ERROR);
    PinBase = (uint32)StreamOffset & ~(BITMAP_PIN_GRANULARITY - 1);
    if (Vcb->BitmapBcb && Vcb->BitmapPinnedOffset == PinBase &&
        Vcb->BitmapPinnedData && Vcb->BitmapRtl.Buffer &&
        Lbn >= Vcb->BitmapPageStartLbn &&
        Lbn - Vcb->BitmapPageStartLbn < Vcb->BitmapPageBitCount)
        return;
    UDFUnpinBitmapPage(Vcb);
    PinLength = (uint32)(min((uint64)PinBase + BITMAP_PIN_GRANULARITY, AllocationEnd) - PinBase);
    if (!PinLength)
        ExRaiseStatus(STATUS_DISK_CORRUPT_ERROR);
    Offset.QuadPart = PinBase;
    if (!CcPinRead(Vcb->BitmapStreamFileObject, &Offset, PinLength, TRUE,
                   &Vcb->BitmapBcb, &Buffer) || !Buffer || !Vcb->BitmapBcb)
        ExRaiseStatus(STATUS_INSUFFICIENT_RESOURCES);
    Vcb->BitmapPinnedData = (PUCHAR)Buffer;
    Vcb->BitmapPinnedOffset = PinBase;
    Vcb->BitmapPinnedLength = PinLength;
    DataStart = max((uint64)Vcb->BitmapDataOffset, PinBase);
    DataEnd = min((uint64)PinBase + PinLength, BitmapEnd);
    if (DataEnd <= DataStart || StreamOffset < DataStart || StreamOffset >= DataEnd ||
        (DataStart - Vcb->BitmapDataOffset) * 8 >= Vcb->FSBM_BitCount)
        ExRaiseStatus(STATUS_DISK_CORRUPT_ERROR);
    StartLbn = (uint32)((DataStart - Vcb->BitmapDataOffset) * 8);
    BitCount = (uint32)min((DataEnd - DataStart) * 8,
                          (uint64)Vcb->FSBM_BitCount - StartLbn);
    BufferOffset = (SIZE_T)(DataStart - PinBase);
    /* RTL bitmap operations may inspect the final ULONG. Its storage must
     * exist in the actual pin, even when the declared last byte is partial. */
    WordBytes = (SIZE_T)(((uint64)BitCount + 31) / 32) * sizeof(ULONG);
    if (!BitCount || BufferOffset > PinLength || WordBytes > PinLength - BufferOffset ||
        (((ULONG_PTR)Vcb->BitmapPinnedData + BufferOffset) & (sizeof(ULONG) - 1)))
        ExRaiseStatus(STATUS_DISK_CORRUPT_ERROR);
    RtlInitializeBitMap(&Vcb->BitmapRtl,
        (PULONG)(Vcb->BitmapPinnedData + BufferOffset), BitCount);
    Vcb->BitmapPageStartLbn = StartLbn;
    Vcb->BitmapPageBitCount = BitCount;
}

VOID
UDFUnpinBitmapPage(
    IN PVCB Vcb
    )
{
    if (Vcb->BitmapBcb) {
        CcUnpinData(Vcb->BitmapBcb);
        Vcb->BitmapBcb = NULL;
    }
    Vcb->BitmapPinnedData = NULL;
    Vcb->BitmapPinnedOffset = Vcb->BitmapPinnedLength = 0;
    Vcb->BitmapPageStartLbn = Vcb->BitmapPageBitCount = 0;
    RtlZeroMemory(&Vcb->BitmapRtl, sizeof(Vcb->BitmapRtl));
}

VOID
UDFDirtyBitmapPage(
    IN PVCB Vcb
    )
{
    if (Vcb->BitmapCacheState != UdfBitmapCacheActive || !Vcb->BitmapBcb ||
        !Vcb->BitmapPinnedData || !Vcb->BitmapRtl.Buffer || !Vcb->BitmapPageBitCount)
        ExRaiseStatus(STATUS_FILE_CLOSED);
    CcSetDirtyPinnedData(Vcb->BitmapBcb, NULL);
}

/*
    Check if a single bitmap bit is free (1 = free in UDF).
    Pins the relevant page internally.
 */
BOOLEAN
UDFIsBitmapBitFree(
    IN PVCB Vcb,
    IN ULONG Lbn
    )
{
    BOOLEAN IsFree = FALSE;
    ULONG Index;
    _SEH2_TRY {
        UDFPinBitmapPage(Vcb, Lbn);
        if (Lbn < Vcb->BitmapPageStartLbn ||
            Lbn - Vcb->BitmapPageStartLbn >= Vcb->BitmapPageBitCount)
            ExRaiseStatus(STATUS_DISK_CORRUPT_ERROR);
        Index = Lbn - Vcb->BitmapPageStartLbn;
        IsFree = RtlCheckBit(&Vcb->BitmapRtl, Index) ? TRUE : FALSE;
    } _SEH2_FINALLY {
        /* A BCB belongs to the pinning thread, also on a raised error. */
        UDFUnpinBitmapPage(Vcb);
    } _SEH2_END;
    return IsFree;
}

/*
    Count consecutive free (set) bits starting from Start LBN, up to Limit.
    Uses RTL_BITMAP across pinned page boundaries.
 */
SIZE_T
UDFGetCachedBitmapLen(
    IN PVCB Vcb,
    IN ULONG Start,
    IN ULONG Limit
    )
{
    SIZE_T Total = 0;
    ULONG Position = Start, Index, RunStart, RunLength, Maximum;
    if (Start > Limit || Limit > Vcb->FSBM_BitCount)
        ExRaiseStatus(STATUS_INVALID_PARAMETER);
    if (Start == Limit)
        return 0;
    _SEH2_TRY {
        while (Position < Limit) {
            UDFPinBitmapPage(Vcb, Position);
            if (Position < Vcb->BitmapPageStartLbn ||
                Position - Vcb->BitmapPageStartLbn >= Vcb->BitmapPageBitCount)
                ExRaiseStatus(STATUS_DISK_CORRUPT_ERROR);
            Index = Position - Vcb->BitmapPageStartLbn;
            Maximum = min(Vcb->BitmapPageBitCount - Index, Limit - Position);
            if (!Maximum)
                ExRaiseStatus(STATUS_DISK_CORRUPT_ERROR);
            RunLength = UDFBitmapFindNextRunSet(&Vcb->BitmapRtl, Index, &RunStart);
            if (!RunLength || RunStart != Index)
                break;
            RunLength = min(RunLength, Maximum);
            Total += RunLength;
            Position += RunLength;
            if (RunLength < Maximum)
                break;
        }
    } _SEH2_FINALLY {
        UDFUnpinBitmapPage(Vcb);
    } _SEH2_END;
    return Total;
}

static
ULONG
UDFCountFreeBits(
    IN PRTL_BITMAP Bitmap,
    IN ULONG Start,
    IN ULONG Count
)
{
    ULONG i = 0, n = 0;

    if (Start > Bitmap->SizeOfBitMap || Count > Bitmap->SizeOfBitMap - Start)
        ExRaiseStatus(STATUS_DISK_CORRUPT_ERROR);
    if (!Count)
        return 0;

    if (RtlAreBitsClear(Bitmap, Start, Count))
        return 0;

    if (RtlAreBitsSet(Bitmap, Start, Count)) 
        return Count;
    
    for (i = 0; i < Count; i++) {
        if (RtlCheckBit(Bitmap, Start + i)) 
            n++;
    }

    return n;
}

/*
    This routine converts physical address to logical in specified partition
 */
/* A physical or metadata partition has a checked numeric inverse. A virtual
 * map does not: its physical descriptor tag and forward VAT membership are
 * needed to recover an ICB identity (UDFResolvePhysicalIcbAddress). */
uint32
UDFPhysLbaToPart(PVCB Vcb, uint32 RefPartNum, uint32 Addr)
{
    PUDFPartMap Part;
    if (!Vcb->Partitions || RefPartNum >= Vcb->PartitionMaps)
        return LBA_OUT_OF_EXTENT;
    Part = &Vcb->Partitions[RefPartNum];
    if (Part->PartitionType == UDF_METADATA_MAP25)
        return Vcb->MetadataFileInfo ? UDFPhysLbaToMetadata(Vcb, Addr) : LBA_OUT_OF_EXTENT;
    if (Part->PartitionType != UDF_TYPE1_MAP15 && Part->PartitionType != UDF_SPARABLE_MAP15)
        return LBA_OUT_OF_EXTENT;
    if (Addr < Part->PartitionRoot || Addr - Part->PartitionRoot >= Part->PartitionLen)
        return LBA_OUT_OF_EXTENT;
    return Addr - Part->PartitionRoot;
}

/* Resolve exactly once while the partition reference is still available.
 * All flattened EXTENT_MAP entries and raw sector callers consume volume
 * physical sectors. They must never guess a virtual domain from an LBA. */
uint32
__fastcall
UDFPartLbaToPhys(PVCB Vcb, lb_addr* Addr)
{
    PUDFPartMap Part;
    uint32 Index, Value;
    uint64 Physical;
    BOOLEAN Virtual;
    if (!Addr || !Vcb->Partitions || Addr->partitionReferenceNum >= Vcb->PartitionMaps)
        return LBA_OUT_OF_EXTENT;
    Index = Addr->partitionReferenceNum;
    Part = &Vcb->Partitions[Index];
    Virtual = Part->PartitionType == UDF_VIRTUAL_MAP15 || Part->PartitionType == UDF_VIRTUAL_MAP20;
    if (!Part->PartitionLen || (!Virtual && Addr->logicalBlockNum >= Part->PartitionLen))
        return LBA_OUT_OF_EXTENT;
    if (Part->PartitionType == UDF_METADATA_MAP25) {
        if (!Vcb->MetadataFileInfo)
            return LBA_OUT_OF_EXTENT;
        return UDFMetadataLbaToPhys(Vcb, Addr->logicalBlockNum, NULL);
    }
    Value = Addr->logicalBlockNum;
    if (Virtual) {
        uint32* Vat = (uint32*)InterlockedCompareExchangePointer(
                                        (PVOID volatile*)&Vcb->Vat, NULL, NULL);
        if (!Vat || Index != Vcb->VatPartNdx || !Vcb->VatCapacity ||
            Part->PartitionRoot != Vcb->VatPartitionRoot ||
            Part->PartitionLen != Vcb->VatPartitionLength ||
            Part->PartitionNum != Vcb->VatPartitionNumber ||
            Part->VolumeSeqNum != Vcb->VatVolumeSequence ||
            Vcb->VatCapacity > MAXULONG / sizeof(uint32) ||
            Vcb->VatCount > Vcb->VatCapacity || Value >= Vcb->VatCount)
            return LBA_OUT_OF_EXTENT;
        Value = Vat[Value];
        if (Value == UDF_VAT_FREE_ENTRY || Value >= Part->PartitionLen)
            return LBA_OUT_OF_EXTENT;
    } else if (Part->PartitionType != UDF_TYPE1_MAP15 &&
               Part->PartitionType != UDF_SPARABLE_MAP15) {
        return LBA_OUT_OF_EXTENT;
    }
    Physical = (uint64)Part->PartitionRoot + Value;
    if (Physical >= LBA_OUT_OF_EXTENT || Physical > Vcb->SessionEndLba)
        return LBA_OUT_OF_EXTENT;
    return (uint32)Physical;
} // end UDFPartLbaToPhys()

/* These are owned internal maps, not untrusted on-disk byte arrays. Bound
 * their sizes before copying and reject holes/nonrecorded or out-of-partition
 * spans. All subsequent view walks use the saved counts, never a sentinel. */
static NTSTATUS
UDFMetadataViewMapSize(PVCB Vcb, PEXTENT_MAP Map, uint32* Bytes,
                       uint32* Count, uint32* Blocks)
{
    NTSTATUS Status;
    uint32 i, Length, RunBlocks;
    uint64 Total = 0;
    PUDFPartMap Part = &Vcb->Partitions[Vcb->MetadataPhysRef];

    Status = UDFGetMappingByteCount(Map, Bytes);
    if (!NT_SUCCESS(Status)) return Status;
    *Count = *Bytes / sizeof(EXTENT_MAP) - 1;
    if (!*Count || Map[*Count].extLength)
        return STATUS_FILE_CORRUPT_ERROR;
    for (i = 0; i < *Count; i++) {
        Length = Map[i].extLength & UDF_EXTENT_LENGTH_MASK;
        RunBlocks = Length >> Vcb->SectorShift;
        if ((Map[i].extLength >> 30) != EXTENT_RECORDED_ALLOCATED ||
            (Length & (Vcb->SectorSize - 1)) || !RunBlocks ||
            Map[i].extLocation < Part->PartitionRoot ||
            Map[i].extLocation - Part->PartitionRoot >= Part->PartitionLen ||
            RunBlocks > Part->PartitionLen - (Map[i].extLocation - Part->PartitionRoot) ||
            Map[i].extLocation > Vcb->SessionEndLba ||
            (uint64)RunBlocks > (uint64)Vcb->SessionEndLba - Map[i].extLocation + 1 ||
            (uint64)Map[i].extLocation + RunBlocks > LBA_OUT_OF_EXTENT)
            return STATUS_FILE_CORRUPT_ERROR;
        Total += RunBlocks;
        if (Total > MAXULONG) return STATUS_FILE_CORRUPT_ERROR;
    }
    *Blocks = (uint32)Total;
    return STATUS_SUCCESS;
}

NTSTATUS
UDFCreateMetadataView(PVCB Vcb, PEXTENT_MAP MainMap,
                       PEXTENT_MAP MirrorMap, PUDF_METADATA_VIEW* View)
{
    NTSTATUS Status;
    uint32 MainBytes, MirrorBytes, MainCount, MirrorCount, MainBlocks, MirrorBlocks;
    uint64 Bytes;
    PUDF_METADATA_VIEW NewView;
    if (!View) return STATUS_INVALID_PARAMETER;
    *View = NULL;
    if (!Vcb || !Vcb->Partitions || Vcb->MetadataPhysRef >= Vcb->PartitionMaps ||
        !Vcb->SectorSize || Vcb->SectorShift >= 30 ||
        Vcb->SectorSize != ((uint32)1 << Vcb->SectorShift))
        return STATUS_FILE_CORRUPT_ERROR;
    Status = UDFMetadataViewMapSize(Vcb, MainMap, &MainBytes, &MainCount, &MainBlocks);
    if (!NT_SUCCESS(Status)) return Status;
    Status = UDFMetadataViewMapSize(Vcb, MirrorMap, &MirrorBytes, &MirrorCount, &MirrorBlocks);
    if (!NT_SUCCESS(Status)) return Status;
    Bytes = (uint64)sizeof(*NewView) + MainBytes + MirrorBytes;
    if (Bytes > MAXULONG - MY_HEAP_ALIGN)
        return STATUS_INSUFFICIENT_RESOURCES;
    NewView = (PUDF_METADATA_VIEW)MyAllocatePoolTag__(NonPagedPool, Bytes, MEM_EXTMAP_TAG);
    if (!NewView) return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(NewView, sizeof(*NewView));
    NewView->References = 1;
    NewView->SectorSize = Vcb->SectorSize;
    NewView->SectorShift = Vcb->SectorShift;
    NewView->MainCount = MainCount;
    NewView->MirrorCount = MirrorCount;
    NewView->MainBlocks = MainBlocks;
    NewView->MirrorBlocks = MirrorBlocks;
    NewView->Flags = Vcb->MetadataFlags;
    NewView->MainMap = (PEXTENT_MAP)(NewView + 1);
    NewView->MirrorMap = (PEXTENT_MAP)((PUCHAR)(NewView + 1) + MainBytes);
    RtlCopyMemory((PVOID)NewView->MainMap, MainMap, MainBytes);
    RtlCopyMemory((PVOID)NewView->MirrorMap, MirrorMap, MirrorBytes);
    *View = NewView;
    return STATUS_SUCCESS;
}

PUDF_METADATA_VIEW
UDFAcquireMetadataView(PVCB Vcb)
{
    KIRQL OldIrql;
    PUDF_METADATA_VIEW View;
    if (!Vcb) return NULL;
    KeAcquireSpinLock(&Vcb->MetadataViewLock, &OldIrql);
    View = Vcb->MetadataView;
    /* The publication reference cannot disappear while the spin lock is
     * held. Releases on other CPUs remain interlocked outside this lock. */
    if (View && View->References > 0 && View->References < MAXLONG)
        InterlockedIncrement(&View->References);
    else
        View = NULL;
    KeReleaseSpinLock(&Vcb->MetadataViewLock, OldIrql);
    return View;
}

VOID
UDFReleaseMetadataView(PUDF_METADATA_VIEW View)
{
    LONG References;
    if (!View) return;
    References = InterlockedDecrement(&View->References);
    NT_ASSERT(References >= 0);
    if (!References) MyFreePool__(View);
}

VOID
UDFInstallMetadataView(PVCB Vcb, PUDF_METADATA_VIEW View)
{
    KIRQL OldIrql;
    PUDF_METADATA_VIEW Previous;
    KeAcquireSpinLock(&Vcb->MetadataViewLock, &OldIrql);
    Previous = Vcb->MetadataView;
    Vcb->MetadataView = View;
    KeReleaseSpinLock(&Vcb->MetadataViewLock, OldIrql);
    /* Pool disposal, mapping walks and lower I/O never occur under spin. */
    UDFReleaseMetadataView(Previous);
}

uint32
UDFMetadataViewLbaToPhys(const UDF_METADATA_VIEW* View, uint32 Lbn,
                         BOOLEAN Mirror, PSIZE_T AvailLength)
{
    const EXTENT_MAP* Map;
    uint32 i, Count, Blocks;
    if (AvailLength) *AvailLength = 0;
    if (!View || Lbn >= (Mirror ? View->MirrorBlocks : View->MainBlocks))
        return LBA_OUT_OF_EXTENT;
    Map = Mirror ? View->MirrorMap : View->MainMap;
    Count = Mirror ? View->MirrorCount : View->MainCount;
    for (i = 0; i < Count; i++) {
        Blocks = (Map[i].extLength & UDF_EXTENT_LENGTH_MASK) >> View->SectorShift;
        if (Lbn < Blocks) {
            if (AvailLength) *AvailLength = ((SIZE_T)(Blocks - Lbn)) << View->SectorShift;
            return Map[i].extLocation + Lbn;
        }
        Lbn -= Blocks;
    }
    return LBA_OUT_OF_EXTENT;
}

uint32
UDFMetadataViewPhysToLba(const UDF_METADATA_VIEW* View, uint32 Lba)
{
    uint32 i, Blocks, Offset = 0;
    if (!View) return LBA_OUT_OF_EXTENT;
    for (i = 0; i < View->MainCount; i++) {
        Blocks = (View->MainMap[i].extLength & UDF_EXTENT_LENGTH_MASK) >> View->SectorShift;
        if (Lba >= View->MainMap[i].extLocation &&
            Lba - View->MainMap[i].extLocation < Blocks)
            return Offset + (Lba - View->MainMap[i].extLocation);
        Offset += Blocks;
    }
    return LBA_OUT_OF_EXTENT;
}

uint32
__fastcall
UDFMetadataLbaToPhys(PVCB Vcb, uint32 Lbn, PSIZE_T AvailLength)
{
    PUDF_METADATA_VIEW View = UDFAcquireMetadataView(Vcb);
    uint32 Result = UDFMetadataViewLbaToPhys(View, Lbn, FALSE, AvailLength);
    UDFReleaseMetadataView(View);
    return Result;
}

uint32
__fastcall
UDFPhysLbaToMetadata(PVCB Vcb, uint32 Lba)
{
    PUDF_METADATA_VIEW View = UDFAcquireMetadataView(Vcb);
    uint32 Result = UDFMetadataViewPhysToLba(View, Lba);
    UDFReleaseMetadataView(View);
    return Result;
}

typedef struct _UDF_METADATA_GROWTH_PROFILE {
    ULONGLONG ScanTicks;
    ULONGLONG AllocateTicks;
    ULONGLONG ZeroTicks;
    ULONGLONG CommitTicks;
    uint32 ScanCalls;
    uint32 AllocateCalls;
    uint32 AlignmentRejects;
} UDF_METADATA_GROWTH_PROFILE;

/* Search free runs once instead of allocating and rolling back each
   overlapping alignment window on a large physical partition.  Keep the
   bitmap lock across discovery and reservation so the selected run cannot
   be taken by another allocation. */
static NTSTATUS
UDFAllocAlignedMetadataRun(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN uint32 Blocks,
    IN uint32 PhysStart,
    IN uint32 PhysEnd,
    IN uint32 Align,
    OUT PEXTENT_INFO ExtInfo,
    OUT uint32* AllocStart,
    OUT PUDF_ALLOCATION_FAILURE* Lease,
    IN OUT UDF_METADATA_GROWTH_PROFILE* Profile
    )
{
    uint32 searchStart, runStart, runLength, candidate, remainder;
    NTSTATUS status = STATUS_DISK_FULL;
    LARGE_INTEGER phaseStart;

    *Lease = NULL;
    ExtInfo->Mapping = NULL;
    ExtInfo->Length = 0;
    ExtInfo->Offset = 0;
    if (!Blocks || !Align || PhysEnd <= PhysStart || Blocks > PhysEnd - PhysStart)
        return status;

    UDFAcquireResourceExclusive(&(Vcb->BitMapResource1), TRUE);
    _SEH2_TRY {
        searchStart = PhysStart;
        while (searchStart <= PhysEnd - Blocks) {
            phaseStart = KeQueryPerformanceCounter(NULL);
            runStart = (uint32)UDFFindMinSuitableExtent(Vcb, Blocks,
                          searchStart, PhysEnd, &runLength,
                          EXTENT_FLAG_IGNORE_HINT);
            Profile->ScanTicks += KeQueryPerformanceCounter(NULL).QuadPart -
                                  phaseStart.QuadPart;
            Profile->ScanCalls++;
            if (runLength < Blocks)
                break;
            if (runStart < searchStart || runStart >= PhysEnd ||
                runLength > PhysEnd - runStart) {
                status = STATUS_FILE_CORRUPT_ERROR;
                break;
            }

            candidate = runStart;
            remainder = (candidate - PhysStart) % Align;
            if (remainder && Align - remainder > runLength - Blocks) {
                Profile->AlignmentRejects++;
                searchStart = runStart + runLength;
                continue;
            }
            if (remainder)
                candidate += Align - remainder;
            phaseStart = KeQueryPerformanceCounter(NULL);
            status = UDFAllocPhysicalExtentLease(IrpContext, Vcb, Vcb->MetadataPhysRef,
                         ((int64)Blocks) << Vcb->SectorShift,
                         candidate, candidate + Blocks, ExtInfo,
                         EXTENT_FLAG_VERIFY, Lease);
            Profile->AllocateTicks += KeQueryPerformanceCounter(NULL).QuadPart -
                                      phaseStart.QuadPart;
            Profile->AllocateCalls++;
            if (NT_SUCCESS(status)) {
                *AllocStart = candidate;
                break;
            }
            /* A failed typed allocation returns no mapping; any uncertain
             * partial claims already have their independent VCB receipt. */
            if (status != STATUS_DISK_FULL)
                break;
            searchStart = runStart + runLength;
        }
    } _SEH2_FINALLY {
        UDFUnpinBitmapPage(Vcb);
        UDFReleaseResource(&Vcb->BitMapResource1);
    } _SEH2_END;
    return status;
}

static NTSTATUS
UDFGrowMetadataFileCore(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN uint32 RequestedBlocks
    )
{
    uint32 unit, align, grow, preferredGrow, minGrow, maxGrow, nextGrow, fallbacks = 0;
    uint32 oldBlocks, newBlocks, oldMapBytes;
    uint32 newBitmapBytes, physStart, physEnd, candidate, mirrorCandidate, i;
    uint32 mainMapBytes, mirrorMapBytes;
    BOOLEAN duplicate;
    PCHAR newBitmap = NULL;
    PCHAR newPendingAlloc = NULL;
    int8* zeroBuffer = NULL;
    PEXTENT_MAP mainMap = NULL, mirrorMap = NULL;
    PEXTENT_MAP oldMainMap, oldMirrorMap;
    EXTENT_INFO physical, physicalMirror;
    SIZE_T written;
    NTSTATUS status = STATUS_DISK_FULL;
    UDF_METADATA_GROWTH_PROFILE profile;
    LARGE_INTEGER frequency, growthStart, phaseStart;
    BOOLEAN profileStarted = FALSE, commitStarted = FALSE;
    PUDF_ALLOCATION_FAILURE mainLease = NULL, mirrorLease = NULL;
    PUDF_METADATA_VIEW newView = NULL;
    NTSTATUS leaseStatus;

    if (!Vcb->MetadataFileInfo || !Vcb->MetadataMirrorFileInfo ||
        !Vcb->MetadataBitmapFileInfo || !Vcb->MetadataPendingAlloc || !Vcb->MetadataBitmap ||
        Vcb->MetadataRef >= Vcb->PartitionMaps || Vcb->MetadataPhysRef >= Vcb->PartitionMaps)
        return STATUS_NOT_SUPPORTED;

    if (!Vcb->SectorSize || Vcb->SectorShift >= 32 ||
        Vcb->SectorSize != ((uint32)1 << Vcb->SectorShift) ||
        ((uint64)Vcb->MetadataBitCount + 7) / 8 > Vcb->MetadataBitmapByteCount ||
        Vcb->MetadataFreeCount > Vcb->MetadataBitCount)
        return STATUS_FILE_CORRUPT_ERROR;
    duplicate = (Vcb->MetadataFlags & UDF_METADATA_DUPLICATED) != 0;
    oldBlocks = Vcb->MetadataBitCount;
    if (oldBlocks != Vcb->Partitions[Vcb->MetadataRef].PartitionLen ||
        oldBlocks != (uint32)(UDFGetFileSize(Vcb->MetadataFileInfo) >> Vcb->SectorShift) ||
        oldBlocks != (uint32)(UDFGetFileSize(Vcb->MetadataMirrorFileInfo) >> Vcb->SectorShift))
        return STATUS_FILE_CORRUPT_ERROR;

    oldMainMap = Vcb->MetadataFileInfo->Dloc->DataLoc.Mapping;
    oldMirrorMap = Vcb->MetadataMirrorFileInfo->Dloc->DataLoc.Mapping;
    mainMapBytes = UDFGetMappingLength(oldMainMap);
    mirrorMapBytes = UDFGetMappingLength(oldMirrorMap);
    if (!mainMapBytes || mirrorMapBytes != mainMapBytes)
        return STATUS_NOT_SUPPORTED;
    for (i = 0; i < mainMapBytes / sizeof(EXTENT_MAP); i++) {
        if (oldMainMap[i].extLength != oldMirrorMap[i].extLength ||
            (duplicate && oldMainMap[i].extLength &&
             oldMainMap[i].extLocation == oldMirrorMap[i].extLocation) ||
            (!duplicate &&
             oldMainMap[i].extLocation != oldMirrorMap[i].extLocation))
            return STATUS_NOT_SUPPORTED;
    }

    unit = Vcb->MetadataAllocUnit;
    align = Vcb->MetadataAlignUnit;
    if (!unit || (unit % 32) || !align)
        return STATUS_FILE_CORRUPT_ERROR;
    // A fixed 512-block increment exhausts the Metadata File Entry's short
    // AD space on large directories long before the physical partition fills.
    // Prefer geometric growth to bound AD count, but a fragmented disk may
    // have only smaller aligned runs. Fall back before reporting ENOSPC.
    if (!RequestedBlocks)
        return STATUS_INVALID_PARAMETER;
    if (RequestedBlocks > MAXULONG - (unit - 1))
        return STATUS_DISK_FULL;
    grow = max(RequestedBlocks, max((uint32)512, oldBlocks / 2));
    if (grow > MAXULONG - (unit - 1))
        return STATUS_DISK_FULL;
    minGrow = ((RequestedBlocks + unit - 1) / unit) * unit;
    grow = ((grow + unit - 1) / unit) * unit;
    /* A metadata-file AD stores the byte length in 30 bits. Keep each
     * geometric increment representable as one allocation-unit-aligned AD;
     * the physical allocator cannot safely split this particular mapping. */
    maxGrow = (UDF_EXTENT_LENGTH_MASK >> Vcb->SectorShift);
    maxGrow -= maxGrow % unit;
    if (!maxGrow || minGrow > maxGrow)
        return STATUS_NOT_SUPPORTED;
    if (grow > maxGrow)
        grow = maxGrow;
    preferredGrow = grow;
    if (oldBlocks > MAXULONG - grow)
        return STATUS_DISK_FULL;
    newBlocks = oldBlocks + grow;
    newBitmapBytes = (newBlocks >> 3) + ((newBlocks & 7) != 0);
    newBitmapBytes = max(newBitmapBytes, Vcb->MetadataBitmapByteCount);

    _SEH2_TRY {
    _SEH2_TRY {
    newBitmap = (PCHAR)MyAllocatePool__(NonPagedPool, newBitmapBytes);
    newPendingAlloc = (PCHAR)MyAllocatePool__(NonPagedPool, newBitmapBytes);
    zeroBuffer = (int8*)MyAllocatePool__(NonPagedPool, (uint64)Vcb->SectorSize * 8);
    mainMap = (PEXTENT_MAP)MyAllocatePoolTag__(NonPagedPool,
                mainMapBytes + sizeof(EXTENT_MAP), MEM_EXTMAP_TAG);
    mirrorMap = (PEXTENT_MAP)MyAllocatePoolTag__(NonPagedPool,
                mirrorMapBytes + sizeof(EXTENT_MAP), MEM_EXTMAP_TAG);
    if (!newBitmap || !newPendingAlloc || !zeroBuffer || !mainMap || !mirrorMap) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto cleanup;
    }
    RtlFillMemory(newBitmap, newBitmapBytes, 0xff);
    RtlZeroMemory(newPendingAlloc, newBitmapBytes);
    RtlCopyMemory(newBitmap, Vcb->MetadataBitmap, Vcb->MetadataBitmapByteCount);
    for (i = oldBlocks; i < newBlocks; i++)
        newBitmap[i >> 3] |= (uint8)(1 << (i & 7));
    RtlZeroMemory(zeroBuffer, Vcb->SectorSize * 8);

    physical.Mapping = NULL;
    physicalMirror.Mapping = NULL;
    candidate = mirrorCandidate = 0;
    RtlZeroMemory(&profile, sizeof(profile));
    growthStart = KeQueryPerformanceCounter(&frequency);
    profileStarted = TRUE;
    physStart = UDFPartStart(Vcb, Vcb->MetadataPhysRef);
    physEnd = UDFPartEnd(Vcb, Vcb->MetadataPhysRef);
    for (;;) {
        status = UDFAllocAlignedMetadataRun(IrpContext, Vcb, grow,
                     physStart, physEnd, align, &physical, &candidate, &mainLease, &profile);
        if (!NT_SUCCESS(status)) {
            if (physical.Mapping) {
                status = STATUS_FILE_CORRUPT_ERROR;
                goto release_physical;
            }
            if (status == STATUS_DISK_FULL)
                goto reduce_growth;
            goto cleanup;
        }
        if (!physical.Mapping) {
            status = STATUS_DISK_FULL;
            goto reduce_growth;
        }
        if (physical.Mapping[0].extLocation != candidate ||
            physical.Mapping[1].extLength) {
            status = STATUS_FILE_CORRUPT_ERROR;
            goto release_physical;
        }

        // Duplicated metadata requires a second, disjoint physical allocation.
        // Its recorded AD has the same logical length as the primary AD.
        mirrorCandidate = candidate;
        if (duplicate) {
            status = UDFAllocAlignedMetadataRun(IrpContext, Vcb, grow,
                         physStart, physEnd, align, &physicalMirror,
                         &mirrorCandidate, &mirrorLease, &profile);
            if (!NT_SUCCESS(status)) {
                if (status == STATUS_DISK_FULL)
                    goto release_and_reduce_growth;
                goto release_physical;
            }
            if (!physicalMirror.Mapping) {
                status = STATUS_DISK_FULL;
                goto release_and_reduce_growth;
            }
            if (physicalMirror.Mapping[0].extLocation != mirrorCandidate ||
                physicalMirror.Mapping[1].extLength ||
                ((uint64)mirrorCandidate < (uint64)candidate + grow &&
                 (uint64)candidate < (uint64)mirrorCandidate + grow)) {
                status = STATUS_FILE_CORRUPT_ERROR;
                goto release_physical;
            }
        }
        break;

release_and_reduce_growth:
        leaseStatus = UDFAbortAllocationLease(Vcb, &mirrorLease, status);
        if (NT_SUCCESS(leaseStatus))
            leaseStatus = UDFAbortAllocationLease(Vcb, &mainLease, status);
        physical.Mapping = physicalMirror.Mapping = NULL; /* borrowed views */
        if (!NT_SUCCESS(leaseStatus) || !NT_SUCCESS(UDFAllocationFailureStatus(Vcb)))
            goto cleanup;
reduce_growth:
        if (grow <= minGrow)
            goto cleanup;
        nextGrow = max(minGrow, (grow / 2 / unit) * unit);
        if (nextGrow >= grow) {
            status = STATUS_DISK_FULL;
            goto cleanup;
        }
        grow = nextGrow;
        fallbacks++;
    }
    newBlocks = oldBlocks + grow;
    newBitmapBytes = max((newBlocks >> 3) + ((newBlocks & 7) != 0),
                         Vcb->MetadataBitmapByteCount);

    // Metadata File ADs must describe aligned, recorded allocation units.
    phaseStart = KeQueryPerformanceCounter(NULL);
    for (i = 0; i < grow; i += 8) {
        uint32 chunk = min((uint32)8, grow - i);
        status = UDFWriteSectors(IrpContext, Vcb, TRUE, candidate + i,
                                 chunk, FALSE, zeroBuffer, &written);
        if (!NT_SUCCESS(status) || written != ((SIZE_T)chunk << Vcb->SectorShift)) {
            if (NT_SUCCESS(status)) status = STATUS_IO_DEVICE_ERROR;
            profile.ZeroTicks += KeQueryPerformanceCounter(NULL).QuadPart -
                                 phaseStart.QuadPart;
            goto release_physical;
        }
        if (duplicate) {
            status = UDFWriteSectors(IrpContext, Vcb, TRUE, mirrorCandidate + i,
                                     chunk, FALSE, zeroBuffer, &written);
            if (!NT_SUCCESS(status) || written != ((SIZE_T)chunk << Vcb->SectorShift)) {
                if (NT_SUCCESS(status)) status = STATUS_IO_DEVICE_ERROR;
                profile.ZeroTicks += KeQueryPerformanceCounter(NULL).QuadPart -
                                     phaseStart.QuadPart;
                goto release_physical;
            }
        }
    }
    profile.ZeroTicks += KeQueryPerformanceCounter(NULL).QuadPart -
                         phaseStart.QuadPart;

    RtlCopyMemory(mainMap, oldMainMap, mainMapBytes - sizeof(EXTENT_MAP));
    RtlCopyMemory(mirrorMap, oldMirrorMap, mirrorMapBytes - sizeof(EXTENT_MAP));
    oldMapBytes = mainMapBytes - sizeof(EXTENT_MAP);
    mainMap[oldMapBytes / sizeof(EXTENT_MAP)].extLocation = candidate;
    mainMap[oldMapBytes / sizeof(EXTENT_MAP)].extLength = grow << Vcb->SectorShift;
    mirrorMap[oldMapBytes / sizeof(EXTENT_MAP)] =
        mainMap[oldMapBytes / sizeof(EXTENT_MAP)];
    mirrorMap[oldMapBytes / sizeof(EXTENT_MAP)].extLocation = mirrorCandidate;
    RtlZeroMemory((int8*)mainMap + mainMapBytes, sizeof(EXTENT_MAP));
    RtlZeroMemory((int8*)mirrorMap + mirrorMapBytes, sizeof(EXTENT_MAP));

    /* Allocate both independent reader maps before any new FE can become
     * visible. No reader may retain a pointer into either mutable Dloc map. */
    status = UDFCreateMetadataView(Vcb, mainMap, mirrorMap, &newView);
    if (!NT_SUCCESS(status))
        goto cleanup;

    status = UDFPublishAllocationLease(Vcb, mainLease);
    if (NT_SUCCESS(status) && mirrorLease)
        status = UDFPublishAllocationLease(Vcb, mirrorLease);
    if (!NT_SUCCESS(status))
        goto cleanup;
    Vcb->MetadataFileInfo->Dloc->DataLoc.Mapping = mainMap;
    Vcb->MetadataMirrorFileInfo->Dloc->DataLoc.Mapping = mirrorMap;
    Vcb->MetadataFileInfo->Dloc->DataLoc.Length =
        Vcb->MetadataMirrorFileInfo->Dloc->DataLoc.Length =
        ((int64)newBlocks) << Vcb->SectorShift;
    Vcb->MetadataFileInfo->Dloc->DataLoc.Modified = TRUE;
    Vcb->MetadataFileInfo->Dloc->AllocLoc.Modified = TRUE;
    Vcb->MetadataMirrorFileInfo->Dloc->DataLoc.Modified = TRUE;
    Vcb->MetadataMirrorFileInfo->Dloc->AllocLoc.Modified = TRUE;
    UDFSetFileSize(Vcb->MetadataFileInfo, ((int64)newBlocks) << Vcb->SectorShift);
    UDFSetFileSize(Vcb->MetadataMirrorFileInfo, ((int64)newBlocks) << Vcb->SectorShift);
    MyFreePool__(oldMainMap);
    MyFreePool__(oldMirrorMap);
    mainMap = mirrorMap = NULL;
    physical.Mapping = physicalMirror.Mapping = NULL; /* leases own buffers */

    phaseStart = KeQueryPerformanceCounter(NULL);
    commitStarted = TRUE;
    status = UDFFlushFile__(IrpContext, Vcb, Vcb->MetadataFileInfo, 0);
    if (!NT_SUCCESS(status)) goto failed_after_commit;
    status = UDFFlushFile__(IrpContext, Vcb, Vcb->MetadataMirrorFileInfo, 0);
    if (!NT_SUCCESS(status)) goto failed_after_commit;
    status = UDFResizeFile__(IrpContext, Vcb, Vcb->MetadataBitmapFileInfo,
                             sizeof(SPACE_BITMAP_DESC) + newBitmapBytes);
    if (!NT_SUCCESS(status)) goto failed_after_commit;

    /* Preserve allocations made while the new metadata extents were zeroed
     * and committed. Both the active and pending bitmaps share this lock. */
    if (Vcb->MetadataBitCount != oldBlocks ||
        !Vcb->MetadataBitmap || !Vcb->MetadataPendingAlloc ||
        Vcb->MetadataBitmapByteCount > newBitmapBytes) {
        status = STATUS_FILE_CORRUPT_ERROR;
        goto failed_after_commit;
    }
    RtlCopyMemory(newBitmap, Vcb->MetadataBitmap,
                  Vcb->MetadataBitmapByteCount);
    if (oldBlocks & 7)
        newBitmap[oldBlocks >> 3] |= (uint8)(0xffU << (oldBlocks & 7));
    RtlCopyMemory(newPendingAlloc, Vcb->MetadataPendingAlloc,
                  Vcb->MetadataBitmapByteCount);
    DbgFreePool(Vcb->MetadataPendingAlloc);
    Vcb->MetadataPendingAlloc = newPendingAlloc;
    newPendingAlloc = NULL;
    DbgFreePool(Vcb->MetadataBitmap);
    Vcb->MetadataBitmap = newBitmap;
    newBitmap = NULL;
    Vcb->MetadataBitmapByteCount = newBitmapBytes;
    Vcb->MetadataBitCount = newBlocks;
    Vcb->MetadataFreeCount += grow;
    Vcb->MetadataHint = oldBlocks;
    Vcb->Partitions[Vcb->MetadataRef].PartitionLen = newBlocks;
    Vcb->MetadataBitmapModified = TRUE;
    UDFSetModified(Vcb);
    status = UDFFlushMetadataBitmap(IrpContext, Vcb);
    if (!NT_SUCCESS(status)) goto failed_after_commit;
    status = UDFBarrierFlushDevice(Vcb->TargetDeviceObject);
    if (!NT_SUCCESS(status)) goto failed_after_commit;
    /* Only a complete generation becomes addressable by new readers.
     * Failure above keeps the previous prefix view and retained leases. */
    UDFInstallMetadataView(Vcb, newView);
    newView = NULL;
    goto cleanup;

release_physical:
    /* These raw payload writes did not authorize freeing a live FE.
     * The finally uses private abort or retained published ownership. */
    goto cleanup;
failed_after_commit:
    if (!NT_SUCCESS(status))
        Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
cleanup:
    NOTHING;
    } _SEH2_EXCEPT(FsRtlIsNtstatusExpected(_SEH2_GetExceptionCode()) ?
                  EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        status = _SEH2_GetExceptionCode();
    } _SEH2_END;
    } _SEH2_FINALLY {
        if (_SEH2_AbnormalTermination()) {
            status = STATUS_FILE_CORRUPT_ERROR; /* diagnostic; exception still propagates */
            Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
            if (mainLease) mainLease->Uncertain = TRUE;
            if (mirrorLease) mirrorLease->Uncertain = TRUE;
        }
        if (mainLease) {
            if (!_SEH2_AbnormalTermination() && NT_SUCCESS(status))
                leaseStatus = UDFCommitAllocationLease(Vcb, &mainLease);
            else
                leaseStatus = UDFAbortAllocationLease(Vcb, &mainLease, status);
            if (NT_SUCCESS(status) && !NT_SUCCESS(leaseStatus)) status = leaseStatus;
            if (mainLease) {
                mainLease->Uncertain = TRUE;
                UDFAbortAllocationLease(Vcb, &mainLease, status);
            }
        }
        if (mirrorLease) {
            if (!_SEH2_AbnormalTermination() && NT_SUCCESS(status))
                leaseStatus = UDFCommitAllocationLease(Vcb, &mirrorLease);
            else
                leaseStatus = UDFAbortAllocationLease(Vcb, &mirrorLease, status);
            if (NT_SUCCESS(status) && !NT_SUCCESS(leaseStatus)) status = leaseStatus;
            if (mirrorLease) {
                mirrorLease->Uncertain = TRUE;
                UDFAbortAllocationLease(Vcb, &mirrorLease, status);
            }
        }
    if (profileStarted) {
        LARGE_INTEGER end = KeQueryPerformanceCounter(NULL);
        if (commitStarted)
            profile.CommitTicks = end.QuadPart - phaseStart.QuadPart;
        /* One bounded line per metadata growth. Ticks and frequency avoid
         * discarding sub-millisecond scan and allocation times. */
        UDFPrint(("UDF metadata growth req=%x preferred=%x grow=%x fallback=%x status=%x main=%x mirror=%x scans=%x allocs=%x align_reject=%x ticks(scan/alloc/zero/commit/total)=%I64x/%I64x/%I64x/%I64x/%I64x freq=%I64x\n",
                  RequestedBlocks, preferredGrow, grow, fallbacks, status,
                  candidate, mirrorCandidate,
                  profile.ScanCalls, profile.AllocateCalls,
                  profile.AlignmentRejects, profile.ScanTicks,
                  profile.AllocateTicks, profile.ZeroTicks,
                  profile.CommitTicks, end.QuadPart - growthStart.QuadPart,
                  frequency.QuadPart));
    }
    if (newBitmap) DbgFreePool(newBitmap);
    if (newPendingAlloc) DbgFreePool(newPendingAlloc);
    if (zeroBuffer) DbgFreePool(zeroBuffer);
    if (mainMap) MyFreePool__(mainMap);
    if (mirrorMap) MyFreePool__(mirrorMap);
    UDFReleaseMetadataView(newView);
    } _SEH2_END;
    return status;
}
/* One growth transaction owns the bitmap domain through snapshot,
 * reservation, payload initialization and FE/bitmap publication. Lower
 * metadata-file writes use physical mappings; their already-OPEN bitmap
 * paging callbacks do not reacquire this resource on a different thread. */
static NTSTATUS
UDFGrowMetadataFile(PIRP_CONTEXT IrpContext, PVCB Vcb, uint32 RequestedBlocks)
{
    NTSTATUS Status;
    UDFAcquireResourceExclusive(&Vcb->BitMapResource1, TRUE);
    _SEH2_TRY {
        Status = UDFAllocationFailureStatus(Vcb);
        if (NT_SUCCESS(Status))
            Status = UDFGrowMetadataFileCore(IrpContext, Vcb, RequestedBlocks);
    } _SEH2_FINALLY {
        UDFUnpinBitmapPage(Vcb);
        UDFReleaseResource(&Vcb->BitMapResource1);
    } _SEH2_END;
    return Status;
}

static VOID UDFRollbackPrivateMetadataClaims(PVCB Vcb, PUDF_ALLOCATION_FAILURE Receipt);
static VOID UDFRetainFailedAllocation(PVCB Vcb, PUDF_ALLOCATION_FAILURE Receipt);

static NTSTATUS
UDFAllocMetadataExtentInternal(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN int64 Length,
    OUT PEXTENT_INFO ExtInfo,
    OUT PUDF_ALLOCATION_FAILURE* Lease
    )
{
    uint32 blocks, bit, i, phys, pass, first;
    uint64 Rounded;
    BOOLEAN GrowthAttempted = FALSE;
    EXTENT_AD Extent;
    PEXTENT_MAP Mapping = NULL;
    NTSTATUS Status = STATUS_UNSUCCESSFUL;
    PUDF_ALLOCATION_FAILURE Receipt;

    if (Lease) *Lease = NULL;
    if (!ExtInfo)
        return STATUS_INVALID_PARAMETER;
    ExtInfo->Mapping = NULL;
    ExtInfo->Offset = 0;
    ExtInfo->Length = 0;
    ExtInfo->Modified = FALSE;
    if (!Vcb || Length <= 0 || !Vcb->SectorSize || Vcb->SectorShift >= 32 ||
        Vcb->SectorSize != ((uint32)1 << Vcb->SectorShift))
        return STATUS_INVALID_PARAMETER;
    Rounded = ((uint64)Length + Vcb->SectorSize - 1) & ~((uint64)Vcb->SectorSize - 1);
    if (Rounded > ALIGN_DOWN_BY(UDF_EXTENT_LENGTH_MASK, Vcb->SectorSize))
        return STATUS_INVALID_PARAMETER;
    blocks = (uint32)(Rounded >> Vcb->SectorShift);
    if (!Vcb->Partitions || Vcb->MetadataRef >= Vcb->PartitionMaps ||
        Vcb->Partitions[Vcb->MetadataRef].PartitionType != UDF_METADATA_MAP25)
        return STATUS_NOT_SUPPORTED;
    Receipt = (PUDF_ALLOCATION_FAILURE)MyAllocatePool__(NonPagedPool, sizeof(*Receipt));
    if (!Receipt) return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Receipt, sizeof(*Receipt));
    Receipt->Metadata = TRUE;
    Receipt->PartRef = Vcb->MetadataRef;
    Receipt->PartitionRoot = Vcb->Partitions[Vcb->MetadataRef].PartitionRoot;
    Receipt->PartitionLength = Vcb->Partitions[Vcb->MetadataRef].PartitionLen;
    Receipt->SourceId = UDF_BUG_CHECK_ID;
    Receipt->SourceLine = __LINE__;
    UDFAcquireResourceExclusive(&Vcb->BitMapResource1, TRUE);
    _SEH2_TRY {
    for (;;) {
        Status = UDFAllocationFailureStatus(Vcb);
        if (!NT_SUCCESS(Status))
            goto metadata_allocation_exit;
        if (Vcb->Vat || Vcb->CDR_Mode)
            { Status = STATUS_NOT_SUPPORTED; goto metadata_allocation_exit; }
        if (Vcb->VcbState & VCB_STATE_VOLUME_READ_ONLY)
            { Status = STATUS_MEDIA_WRITE_PROTECTED; goto metadata_allocation_exit; }
        /* The direct FE allocator also calls here. OPEN must precede RAM
         * bitmap mutation, not just the first later FE/FID write. */
        Status = UDFOpenIntegrityForWrite(IrpContext, Vcb, FALSE);
        if (!NT_SUCCESS(Status))
            goto metadata_allocation_exit;
        _SEH2_TRY {
            Status = UDFAllocationFailureStatus(Vcb);
            if (!NT_SUCCESS(Status))
                _SEH2_LEAVE;
            if (Vcb->VcbState & VCB_STATE_VOLUME_READ_ONLY) {
                Status = STATUS_MEDIA_WRITE_PROTECTED;
                _SEH2_LEAVE;
            }
            if (!Vcb->MetadataFileInfo || !Vcb->MetadataBitmap || !Vcb->MetadataPendingAlloc) {
                Status = STATUS_NOT_SUPPORTED;
                _SEH2_LEAVE;
            }
            if (((uint64)Vcb->MetadataBitCount + 7) / 8 > Vcb->MetadataBitmapByteCount ||
                Vcb->MetadataFreeCount > Vcb->MetadataBitCount) {
                Status = STATUS_FILE_CORRUPT_ERROR;
                _SEH2_LEAVE;
            }
            if (Lease && Vcb->ActiveAllocationLeaseCount == MAXLONG) {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                _SEH2_LEAVE;
            }
            Status = STATUS_DISK_FULL;
            first = Vcb->MetadataHint < Vcb->MetadataBitCount ? Vcb->MetadataHint : 0;
            for (pass = 0; pass < 2 && blocks <= Vcb->MetadataBitCount; pass++) {
                for (bit = pass ? 0 : first;
                     bit <= Vcb->MetadataBitCount - blocks && (pass == 0 || bit < first); bit++) {
                    phys = UDFMetadataLbaToPhys(Vcb, bit, NULL);
                    if (phys == LBA_OUT_OF_EXTENT || (uint64)phys + blocks > LBA_OUT_OF_EXTENT)
                        continue;
                    for (i = 0; i < blocks; i++) {
                        if (!(Vcb->MetadataBitmap[(bit + i) >> 3] & (1 << ((bit + i) & 7))) ||
                            UDFMetadataLbaToPhys(Vcb, bit + i, NULL) != phys + i)
                            break;
                    }
                    if (i != blocks)
                        continue;
                    if (Vcb->MetadataFreeCount < blocks) {
                        Status = STATUS_FILE_CORRUPT_ERROR;
                        _SEH2_LEAVE;
                    }
                    Extent.extLocation = phys;
                    Extent.extLength = (uint32)Rounded | (EXTENT_NOT_RECORDED_ALLOCATED << 30);
                    Mapping = UDFExtentToMapping(&Extent);
                    if (!Mapping) {
                        Status = STATUS_INSUFFICIENT_RESOURCES;
                        _SEH2_LEAVE;
                    }
                    Receipt->Mapping = Mapping;
                    Receipt->MappingBytes = 2 * sizeof(EXTENT_MAP);
                    Receipt->MetadataFirstBit = bit;
                    Receipt->MetadataBitCount = blocks;
                    /* Growth may have appended a new stable metadata prefix. */
                    Receipt->PartitionLength = Vcb->Partitions[Vcb->MetadataRef].PartitionLen;
                    Mapping = NULL;
                    Receipt->ClaimInProgress = TRUE;
                    for (i = 0; i < blocks; i++) {
                        Vcb->MetadataBitmap[(bit + i) >> 3] &= ~(1 << ((bit + i) & 7));
                        Vcb->MetadataPendingAlloc[(bit + i) >> 3] |= (uint8)(1 << ((bit + i) & 7));
                    }
                    Vcb->MetadataFreeCount -= blocks;
                    Vcb->MetadataHint = bit + blocks;
                    if (Vcb->MetadataHint >= Vcb->MetadataBitCount)
                        Vcb->MetadataHint = 0;
                    Vcb->MetadataBitmapModified = TRUE;
                    UDFSetModified(Vcb);
                    Receipt->ClaimInProgress = FALSE;
                    Receipt->HasClaims = TRUE;
                    ExtInfo->Mapping = Receipt->Mapping;
                    if (Lease) {
                        Receipt->Leased = TRUE;
                        Receipt->Next = Vcb->ActiveAllocationLeases;
                        Vcb->ActiveAllocationLeases = Receipt;
                        InterlockedIncrement(&Vcb->ActiveAllocationLeaseCount);
                        *Lease = Receipt;
                        Receipt = NULL; /* VCB inventory owns the borrowed map */
                    } else {
                        Receipt->Mapping = NULL; /* caller owns the successful map */
                        Receipt->HasClaims = FALSE;
                    }
                    ExtInfo->Length = Length;
                    ExtInfo->Modified = TRUE;
                    Status = STATUS_SUCCESS;
                    _SEH2_LEAVE;
                }
            }
        } _SEH2_FINALLY {
            if (Mapping) {
                MyFreePool__(Mapping);
                Mapping = NULL;
            }
        } _SEH2_END;
        if (Status != STATUS_DISK_FULL || GrowthAttempted)
            goto metadata_allocation_exit;
        Status = UDFGrowMetadataFile(IrpContext, Vcb, blocks);
        if (!NT_SUCCESS(Status))
            goto metadata_allocation_exit;
        GrowthAttempted = TRUE;
    }

metadata_allocation_exit:
        NOTHING;
    } _SEH2_FINALLY {
        if (Receipt) {
            /* Normal success either transferred the map or linked a lease.
             * An interrupted resident claim cannot prove counter completion;
             * keep its receipt without attempting another fallible rollback
             * from this resource's finally block. */
            if (Receipt->HasClaims || Receipt->ClaimInProgress) {
                Receipt->Uncertain = TRUE;
                Receipt->FirstError = (_SEH2_AbnormalTermination() || NT_SUCCESS(Status)) ?
                                      STATUS_FILE_CORRUPT_ERROR : Status;
            }
            if (Receipt->Uncertain || Receipt->HasClaims) {
                UDFRetainFailedAllocation(Vcb, Receipt);
            } else {
                if (Receipt->Mapping) MyFreePool__(Receipt->Mapping);
                MyFreePool__(Receipt);
            }
        }
        UDFUnpinBitmapPage(Vcb);
        UDFReleaseResource(&Vcb->BitMapResource1);
    } _SEH2_END;
    return Status;
}

NTSTATUS
UDFAllocMetadataExtent(PIRP_CONTEXT IrpContext, PVCB Vcb,
                        int64 Length, PEXTENT_INFO ExtInfo)
{
    return UDFAllocMetadataExtentInternal(IrpContext, Vcb, Length, ExtInfo, NULL);
}

/*
    This routine returns physycal Lba for partition-relative addr
    No partition bounds check is performed.
    This routine only checks if requested partition exists.
    It is introduced for 'Adaptec DirectCD' compatibility,
    because it uses negative values as extent terminator (against standard)
 */
/*uint32
__fastcall
UDFPartLbaToPhysCompat(
  IN PVCB Vcb,
  IN lb_addr* Addr
  )
{
    uint32 i, a;
    if (Addr->partitionReferenceNum >= Vcb->PartitionMaps) return LBA_NOT_ALLOCATED;
    // walk through partition maps & transform relative address
    // to physical
    for(i=Addr->partitionReferenceNum; i<Vcb->PartitionMaps; i++) {
        if (Vcb->Partitions[i].PartitionNum == Addr->partitionReferenceNum) {
            a = Vcb->Partitions[i].PartitionRoot + Addr->logicalBlockNum;
            if (a > Vcb->LastPossibleLBA) {
                BrutePoint();
            }
            return a;
        }
    }
    a = Vcb->Partitions[i-1].PartitionRoot + Addr->logicalBlockNum;
    if (a > Vcb->LastPossibleLBA) {
        BrutePoint();
    }
    return a;
} // end UDFPartLbaToPhysCompat()*/


/*
    This routine looks for the partition containing given physical sector
 */
uint32
__fastcall
UDFGetRefPartNumByPhysLba(
    IN PVCB Vcb,
    IN uint32 Lba
    )
{
    uint32 i, root;
    PUDFPartMap pm = Vcb->Partitions;
    if (!pm)
        return LBA_OUT_OF_EXTENT;

    if (
        Vcb->MetadataFileInfo &&
        (UDFPhysLbaToMetadata(Vcb, Lba) != LBA_OUT_OF_EXTENT)
    )
        return Vcb->MetadataRef;

    for (i = 0; i < Vcb->PartitionMaps; i++, pm++) {

        if ((pm->PartitionType != UDF_SPARABLE_MAP15) &&
            (pm->PartitionType != UDF_TYPE1_MAP15)
        ) {
            continue;
        }

        root = pm->PartitionRoot;
        if (Lba >= root && Lba - root < pm->PartitionLen) {
            return i;
        }
    }
    return LBA_OUT_OF_EXTENT; // Lba doesn't belong to any partition
} // end UDFGetPartNumByPhysLba()

/*
    Very simple routine. It walks through the Partition Maps & returns
    the 1st Lba of the 1st suitable one
 */
uint32
__fastcall
UDFPartStart(
    PVCB Vcb,
    uint32 RefPartNum
    )
{
    if (RefPartNum == (uint32)-1) 
        return 0;

    if (RefPartNum == (uint32)-2) 
        RefPartNum = UDFGetLastPhysPartRef(Vcb);
    
    if(RefPartNum >= Vcb->PartitionMaps)
        return 0;

    return Vcb->Partitions[RefPartNum].PartitionRoot;
} // end UDFPartStart(

/*
   This routine does almost the same as previous.
   The only difference is changing First Lba to Last one...
 */
uint32
__fastcall
UDFPartEnd(
    PVCB Vcb,
    uint32 RefPartNum
    )
{
    if (RefPartNum == (uint32)-1)
        return Vcb->SessionEndLba;

    if (RefPartNum == (uint32)-2)
        RefPartNum = UDFGetLastPhysPartRef(Vcb);
    
    if(RefPartNum >= Vcb->PartitionMaps)
        RefPartNum = Vcb->PartitionMaps-1;

    return (
        Vcb->Partitions[RefPartNum].PartitionRoot +
        Vcb->Partitions[RefPartNum].PartitionLen
    );
} // end UDFPartEnd()

/*
    Very simple routine. It walks through the Partition Maps & returns
    the 1st Lba of the 1st suitable one
 */
uint32
__fastcall
UDFPartLen(
    PVCB Vcb, 
    uint32 RefPartNum
    )
{
    if (RefPartNum == (uint32)-1) 
        return Vcb->SessionEndLba;

    if (RefPartNum == (uint32)-2)
        RefPartNum = UDFGetLastPhysPartRef(Vcb);

    if(RefPartNum >= Vcb->PartitionMaps)
        RefPartNum = Vcb->PartitionMaps-1;
 
    return Vcb->Partitions[RefPartNum].PartitionLen;
} // end UDFPartLen()

uint32
__fastcall
UDFGetLastPhysPartRef(
    PVCB Vcb
)
{
    uint32 m;

    for (m = Vcb->PartitionMaps; m > 0; m--) {

        if ((Vcb->Partitions[m-1].PartitionType == UDF_TYPE1_MAP15) ||
            (Vcb->Partitions[m-1].PartitionType == UDF_SPARABLE_MAP15)
        ) {
            return m-1;
        }
    }

    return 0;
}

/*
    This routine returns length of bit-chain starting from Offs bit in
    array Bitmap. Bitmap scan is limited with Lim.
 */
SIZE_T
UDFGetBitmapLen(
    uint32* Bitmap,
    SIZE_T Offs,
    SIZE_T Lim          // NOT included
    )
{
    ASSERT(Offs <= Lim);
    if (Offs >= Lim) {
        return 0;//(Offs == Lim);
    }

    BOOLEAN bit = UDFGetBit(Bitmap, Offs);
    SIZE_T i=Offs>>5;
    SIZE_T len=0;
    uint8 j=(uint8)(Offs&31);
    uint8 lLim=(uint8)(Lim&31);

    Lim = Lim>>5;

    ASSERT((bit == 0) || (bit == 1));

    uint32 a;

    a = Bitmap[i] >> j;

    while(i<=Lim) {

        while( j < ((i<Lim) ? 32 : lLim) ) {
            if ( ((BOOLEAN)(a&1)) != bit)
                return len;
            len++;
            a>>=1;
            j++;
        }
        j=0;
While_3:
        i++;
        if (i > Lim) break;
        a = Bitmap[i];

        if (i<Lim) {
            if ((bit && (a==0xffffffff)) ||
               (!bit && !a)) {
                len+=32;
                goto While_3;
            }
        }
    }
    return len;
} // end UDFGetBitmapLen()

/*
 */
SIZE_T
UDFFindMinSuitableExtent(
    IN PVCB Vcb,
    IN uint32 LengthInBlocks,
    IN uint32 SearchStartPSN,  
    IN uint32 ExclusiveSearchLimPSN,    
    OUT uint32* MaxExtLen,
    IN uint8  AllocFlags
)
{
    SIZE_T i, len;
    ULONG pass;
    SIZE_T best_lba=0, best_len=0;
    SIZE_T max_lba=0, max_len=0;
    uint32 partRoot = Vcb->Partitions[0].PartitionRoot, lbnStart = SearchStartPSN - partRoot;
    uint32 lbnLim = ExclusiveSearchLimPSN - partRoot;
    uint32 hint = (AllocFlags & EXTENT_FLAG_FE_LOCALITY) ?
                  Vcb->FeSectorHint : Vcb->SectorHint;

    UDF_CHECK_BITMAP_RESOURCE(Vcb);

    if (LengthInBlocks > (uint32)(UDF_EXTENT_LENGTH_MASK >> Vcb->SectorShift)) {
        LengthInBlocks = (UDF_EXTENT_LENGTH_MASK >> Vcb->SectorShift);
    }

#define _bitmap_freebit(off) (Vcb->BitmapFcb ? UDFIsBitmapBitFree(Vcb, hint - off) : UDFGetFreeBit((uint32*)Vcb->FSBM_Bitmap, hint - off))
    /* The metadata-growth caller must traverse physical runs in increasing
     * order. Wrapping from SectorHint can select an unalignable later run,
     * then skip an earlier suitable run when it advances SearchStart. */
    if ((AllocFlags & EXTENT_FLAG_IGNORE_HINT) ||
        hint < lbnStart || hint >= lbnLim) {
        hint = lbnStart;
    } else if (
        hint > lbnStart && _bitmap_freebit(0)) {
        while ( hint > lbnStart && _bitmap_freebit(1)) {
            hint--;
        }
    }
#undef _bitmap_freebit

    for (pass = 0; pass < 2; pass++) {
        uint32 scanStart = pass ? lbnStart : hint;
        uint32 scanLim = pass ? hint : lbnLim;

        if (scanStart >= scanLim)
            continue;

        i = scanStart;
        if (Vcb->BitmapFcb) {
            ULONG CurrentRunStart = 0, CurrentRunLength = 0;
            ULONG CurrentLbn = (ULONG)scanStart;
        
            while (CurrentLbn < scanLim) {
                UDFPinBitmapPage(Vcb, CurrentLbn);
            
                ULONG runStartIndex;
                ULONG pageStart = Vcb->BitmapPageStartLbn;
                ULONG pageBits = Vcb->BitmapPageBitCount;
                ULONG fromIndex = CurrentLbn - pageStart;
                ULONG runLen = UDFBitmapFindNextRunSet(&Vcb->BitmapRtl, fromIndex, &runStartIndex);
            
                ULONG runStartLbn = runStartIndex + pageStart;
                ULONG pageEnd = min(pageStart + pageBits, scanLim);

                if (runStartLbn >= scanLim)
                    runLen = 0;
                else if (runLen > pageEnd - runStartLbn)
                    runLen = pageEnd - runStartLbn;
            
                if (CurrentRunLength != 0) {
                    if (runLen == 0 || runStartLbn != CurrentLbn) {
                        if (CurrentRunLength >= LengthInBlocks) {
                            if (!best_len || (best_len > CurrentRunLength)) {
                                best_lba = CurrentRunStart;
                                best_len = CurrentRunLength;
                            }
                        } else if(max_len < CurrentRunLength) {
                            max_lba = CurrentRunStart;
                            max_len = CurrentRunLength;
                        }
                        if (best_len || (Vcb->CDR_Mode && max_len)) break;
                        CurrentRunLength = runLen;
                        CurrentRunStart = runStartLbn;
                    } else {
                        CurrentRunLength += runLen;
                    }
                } else {
                     if(runLen != 0) {
                        CurrentRunLength = runLen;
                        CurrentRunStart = runStartLbn;
                    }
                }
            
                if (CurrentRunLength >= LengthInBlocks) {
                    best_len = CurrentRunLength;
                    best_lba = CurrentRunStart;
                    break;
                }
            
                if (best_len) break;
            
                if (runLen == 0)
                    CurrentLbn = pageEnd;
                else
                    CurrentLbn = CurrentRunStart + CurrentRunLength;
            
                if (CurrentLbn > scanLim)
                    CurrentLbn = scanLim;
            }
        
            if (CurrentRunLength != 0 && !best_len) {
                if (CurrentRunLength >= LengthInBlocks) {
                    if (!best_len || (best_len > CurrentRunLength)) {
                        best_lba = CurrentRunStart;
                        best_len = CurrentRunLength;
                    }
                } else if (max_len < CurrentRunLength) {
                    max_lba = CurrentRunStart;
                    max_len = CurrentRunLength;
                }
            }
        } else {
            while (i < scanLim) {

                ASSERT(i <= scanLim);
                
                len = UDFGetBitmapLen((uint32*)(Vcb->FSBM_Bitmap), i, scanLim);
                
                if (UDFGetFreeBit((uint32*)(Vcb->FSBM_Bitmap), i)) {
                    if (len >= LengthInBlocks) {
                        if (!best_len || (best_len > len)) {
                            best_lba = i;
                            best_len = len;
                        }
                        if(len == LengthInBlocks) break;
                    } else {
                        if (max_len < len) {
                            max_lba = i;
                            max_len = len;
                        }
                    }
                    if (best_len || (Vcb->CDR_Mode && max_len)) break;
                }
                i += len;
            }
        }
        if (best_len || (Vcb->CDR_Mode && max_len))
            break;
    }
    
    UDFUnpinBitmapPage(Vcb);
    
    if (!best_len && !max_len) {
        UDFPrint(("UDFFindMinSuitableExtent: Couldn't find free space lbnStart=%x lbnLim=%x Length=%x BitCount=%x\n",
            (ULONG)lbnStart, (ULONG)lbnLim, LengthInBlocks, Vcb->FSBM_BitCount));
    }

    if (best_len) {
        *MaxExtLen = best_len;
        return best_lba + partRoot;
    }

    *MaxExtLen = max_len;

    return max_lba + partRoot;
}

#ifdef UDF_CHECK_DISK_ALLOCATION
/*
    This routine checks space described by Mapping as Used/Freed (optionaly)
 */
void
UDFCheckSpaceAllocation_(
    IN PVCB Vcb,
    IN PEXTENT_MAP Map,
    IN uint32 asXXX
#ifdef UDF_TRACK_ONDISK_ALLOCATION
   ,IN uint32 FE_lba,
    IN uint32 BugCheckId,
    IN uint32 Line
#endif //UDF_TRACK_ONDISK_ALLOCATION
    )
{
    uint32 i=0;
    uint32 lba, j, len, BS, BSh;
    BOOLEAN asUsed = (asXXX == AS_USED);

    if (!Map) return;

    BS = Vcb->BlockSize;
    BSh = Vcb->BlockSizeBits;

    UDFAcquireResourceExclusive(&(Vcb->BitMapResource1),TRUE);
    // walk through all frags in data area specified
#ifdef UDF_TRACK_ONDISK_ALLOCATION
    AdPrint(("ChkAlloc:Map:%x:File:%x:Line:%d\n",
        Map,
        BugCheckId,
        Line
        ));
#endif //UDF_TRACK_ONDISK_ALLOCATION
    while(Map[i].extLength & UDF_EXTENT_LENGTH_MASK) {

#ifdef UDF_TRACK_ONDISK_ALLOCATION
        AdPrint(("ChkAlloc:%x:%s:%x:@:%x:(%x):File:%x:Line:%d\n",
            FE_lba,
            asUsed ? "U" : "F",
            (Map[i].extLength & UDF_EXTENT_LENGTH_MASK) >> BSh,
            Map[i].extLocation,
            (Map[i].extLength >> 30),
            BugCheckId,
            Line
            ));
#endif //UDF_TRACK_ONDISK_ALLOCATION
        if (asUsed) {
            UDFCheckUsedBitOwner(Vcb, (Map[i].extLength & UDF_EXTENT_LENGTH_MASK) >> BSh, FE_lba);
        } else {
            UDFCheckFreeBitOwner(Vcb, (Map[i].extLength & UDF_EXTENT_LENGTH_MASK) >> BSh);
        }

        if ((Map[i].extLength >> 30) == EXTENT_NOT_RECORDED_NOT_ALLOCATED) {
            // skip unallocated frags
//            ASSERT(!(Map[i].extLength & UDF_EXTENT_LENGTH_MASK));
            ASSERT(!Map[i].extLocation);
            i++;
            continue;
        } else {
//            ASSERT(!(Map[i].extLength & UDF_EXTENT_LENGTH_MASK));
            ASSERT(Map[i].extLocation);
        }

#ifdef UDF_CHECK_EXTENT_SIZE_ALIGNMENT
        ASSERT(!(Map[i].extLength & (BS-1)));
#endif //UDF_CHECK_EXTENT_SIZE_ALIGNMENT
        len = ((Map[i].extLength & UDF_EXTENT_LENGTH_MASK)+BS-1) >> BSh;
        lba = Map[i].extLocation;
        if (lba < Vcb->Partitions[0].PartitionRoot) {
            ASSERT(FALSE);
            i++;
            continue;
        }
        // The bitmap counts partition-relative blocks, not physical sectors.
        uint32 lbn = lba - Vcb->Partitions[0].PartitionRoot;
        if (lbn >= Vcb->FSBM_BitCount) {
            ASSERT(FALSE);
            i++;
            continue;
        }
        if (len > Vcb->FSBM_BitCount - lbn)
            len = Vcb->FSBM_BitCount - lbn;

        // mark frag as XXX (see asUsed parameter)
        if (asUsed) {

            ASSERT(len);
            for(j=0;j<len;j++) {
                if (lbn+j >= Vcb->FSBM_BitCount) {
                    BrutePoint();
                    AdPrint(("USED Mapping covers block(s) beyond bitmap @%x\n",lba+j));
                    break;
                }
                if (Vcb->BitmapFcb ? UDFIsBitmapBitFree(Vcb, lbn+j) : !UDFGetUsedBit(Vcb->FSBM_Bitmap, lbn+j)) {
                    BrutePoint();
                    AdPrint(("USED Mapping covers FREE block(s) @%x\n",lba+j));
                    break;
                }
            }

        } else {

            ASSERT(len);
            for(j=0;j<len;j++) {
                if (lbn+j >= Vcb->FSBM_BitCount) {
                    BrutePoint();
                    AdPrint(("USED Mapping covers block(s) beyond bitmap @%x\n",lba+j));
                    break;
                }
                if (Vcb->BitmapFcb ? !UDFIsBitmapBitFree(Vcb, lbn+j) : !UDFGetFreeBit(Vcb->FSBM_Bitmap, lbn+j)) {
                    BrutePoint();
                    AdPrint(("FREE Mapping covers USED block(s) @%x\n",lba+j));
                    break;
                }
            }
        }

        i++;
    }
    UDFReleaseResource(&(Vcb->BitMapResource1));
} // end UDFCheckSpaceAllocation_()
#endif //UDF_CHECK_DISK_ALLOCATION

void
UDFMarkBadSpaceAsUsed(
    IN PVCB Vcb,
    IN lba_t lba,
    IN ULONG len
    )
{
    uint32 j;
#define BIT_C   (sizeof(Vcb->BSBM_Bitmap[0])*8)
    len = (lba+len+BIT_C-1)/BIT_C;
    if (Vcb->BSBM_Bitmap) {
        if (Vcb->BitmapFcb) {
            // Per-page: AND bad-block mask into pinned bitmap data
            for(j=lba/BIT_C; j<len; j++) {
                if (Vcb->BSBM_Bitmap[j]) {
                    UDFPinBitmapPage(Vcb, j * BIT_C);
                    ULONG byteOff = (j * BIT_C - Vcb->BitmapPageStartLbn) / 8;
                    // Access raw pinned data for bad-block masking
                    PUCHAR rawData = (PUCHAR)Vcb->BitmapRtl.Buffer;
                    Vcb->FreeAllocUnits -= bit_count_tab[rawData[byteOff] & (UCHAR)Vcb->BSBM_Bitmap[j]];
                    rawData[byteOff] &= ~Vcb->BSBM_Bitmap[j];
                    UDFDirtyBitmapPage(Vcb);
                }
            }
        } else {
            for(j=lba/BIT_C; j<len; j++) {
                Vcb->FSBM_Bitmap[j] &= ~Vcb->BSBM_Bitmap[j];
            }
        }
    }
#undef BIT_C
    UDFUnpinBitmapPage(Vcb);
} // UDFMarkBadSpaceAsUsed()

/*
    This routine marks space described by Mapping as Used/Freed (optionaly)
 */
void
UDFMarkSpaceAsXXXNoProtect_(
    IN PVCB Vcb,
    IN PEXTENT_MAP Map,
    IN uint32 asXXX
#ifdef UDF_TRACK_ONDISK_ALLOCATION
   ,IN uint32 FE_lba,
    IN uint32 BugCheckId,
    IN uint32 Line
#endif //UDF_TRACK_ONDISK_ALLOCATION
    )
{
    uint32 i=0;
    uint32 lba, j, len, BS, BSh;
    BOOLEAN asUsed = (asXXX & (AS_USED | AS_BAD)) != 0;
    BOOLEAN physicalMarked = FALSE;
    BOOLEAN metadataFileMap;
#ifdef UDF_TRACK_ONDISK_ALLOCATION
    BOOLEAN bit_before, bit_after;
#endif //UDF_TRACK_ONDISK_ALLOCATION

    UDF_CHECK_BITMAP_RESOURCE(Vcb);

    if (!Map) return;
    /* This backend has no sequential append/VAT commit transaction.
     * Keep the mounted translation and bitmap immutable even on an
     * unexpected internal free/mark path. Never rewrite VAT by PSN index. */
    if (Vcb->Vat || Vcb->CDR_Mode) {
        Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
        return;
    }

    if ((asXXX & AS_PRIVATE_ROLLBACK) &&
        (((asXXX & (AS_DISCARDED | AS_PHYSICAL)) != (AS_DISCARDED | AS_PHYSICAL)) ||
         (asXXX & (AS_USED | AS_BAD))))
        ExRaiseStatus(STATUS_INVALID_PARAMETER);
    _SEH2_TRY {
        BS = Vcb->SectorSize;
        BSh = Vcb->SectorShift;
        metadataFileMap = (asXXX & AS_PHYSICAL) || (Vcb->MetadataFileInfo &&
            Map == Vcb->MetadataFileInfo->Dloc->DataLoc.Mapping);
        if (Vcb->MetadataMirrorFileInfo &&
            Map == Vcb->MetadataMirrorFileInfo->Dloc->DataLoc.Mapping)
            metadataFileMap = TRUE;
        uint32 partRoot = Vcb->Partitions[0].PartitionRoot;
        // walk through all frags in data area specified
        while(Map[i].extLength & UDF_EXTENT_LENGTH_MASK) {
            if ((Map[i].extLength >> 30) > EXTENT_NOT_RECORDED_NOT_ALLOCATED)
                ExRaiseStatus(STATUS_FILE_CORRUPT_ERROR);
            if ((Map[i].extLength >> 30) == EXTENT_NOT_RECORDED_NOT_ALLOCATED) {
                // skip unallocated frags
                i++;
                continue;
            }
            ASSERT(Map[i].extLocation);

    #ifdef UDF_TRACK_ONDISK_ALLOCATION
            AdPrint(("Alloc:%x:%s:%x:@:%x:File:%x:Line:%d\n",
                FE_lba,
                asUsed ? ((asXXX & AS_BAD) ? "B" : "U") : "F",
                (Map[i].extLength & UDF_EXTENT_LENGTH_MASK) >> Vcb->BlockSizeBits,
                Map[i].extLocation,
                BugCheckId,
                Line
                ));
    #endif //UDF_TRACK_ONDISK_ALLOCATION

    #ifdef UDF_DBG
    #ifdef UDF_CHECK_EXTENT_SIZE_ALIGNMENT
            ASSERT(!(Map[i].extLength & (BS-1)));
    #endif //UDF_CHECK_EXTENT_SIZE_ALIGNMENT
    //        len = ((Map[i].extLength & UDF_EXTENT_LENGTH_MASK)+BS-1) >> BSh;
    #else // UDF_DBG
    //        len = (Map[i].extLength & UDF_EXTENT_LENGTH_MASK) >> BSh;
    #endif // UDF_DBG
            len = (uint32)(((uint64)(Map[i].extLength & UDF_EXTENT_LENGTH_MASK) + BS - 1) >> BSh);
            if (!len || ((uint64)len << BSh) > UDF_EXTENT_LENGTH_MASK)
                ExRaiseStatus(STATUS_FILE_CORRUPT_ERROR);
            lba = Map[i].extLocation;

            // These sectors are already reserved by the physical partition's
            // bitmap as part of the Metadata File.  Track child ICBs and directory
            // extents in the Metadata Bitmap instead; never release their physical
            // blocks to ordinary file data.
            if (Vcb->MetadataBitmap && !metadataFileMap &&
                !(asXXX & AS_BAD)) {
                if (!Vcb->MetadataPendingAlloc) {
                    Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
                    ExRaiseStatus(STATUS_FILE_CORRUPT_ERROR);
                }
                uint32 metadataLbn = UDFPhysLbaToMetadata(Vcb, lba);
                if (metadataLbn != LBA_OUT_OF_EXTENT) {
                    if (metadataLbn >= Vcb->MetadataBitCount ||
                        len > Vcb->MetadataBitCount - metadataLbn)
                        ExRaiseStatus(STATUS_FILE_CORRUPT_ERROR);
                    for (j = 0; j < len; j++) {
                        if (UDFMetadataLbaToPhys(Vcb, metadataLbn + j, NULL) != lba + j)
                            break;
                    }
                    if (j != len)
                        ExRaiseStatus(STATUS_NOT_SUPPORTED);
                    if (j == len) {
                        if ((asXXX & AS_DISCARDED) &&
                            Vcb->TruncateTxnOwner == PsGetCurrentThread()) {
                            /* This transaction only prepared physical bits.
                             * Leave unexpected metadata bits reserved and make
                             * the outer resize fail closed. */
                            Vcb->TruncateTxnError = STATUS_NOT_SUPPORTED;
                            Map[i].extLength = (len << BSh) |
                                (EXTENT_NOT_RECORDED_NOT_ALLOCATED << 30);
                            Map[i].extLocation = 0;
                            i++;
                            continue;
                        }
                        for (j = 0; j < len; j++) {
                            uint32 bit = metadataLbn + j;
                            uint8 mask = (uint8)(1 << (bit & 7));
                            BOOLEAN wasFree = (Vcb->MetadataBitmap[bit >> 3] & mask) != 0;
                            if (asUsed && wasFree) {
                                Vcb->MetadataBitmap[bit >> 3] &= ~mask;
                                Vcb->MetadataPendingAlloc[bit >> 3] |= mask;
                                Vcb->MetadataFreeCount--;
                            } else if (!asUsed && !wasFree) {
                                Vcb->MetadataBitmap[bit >> 3] |= mask;
                                Vcb->MetadataFreeCount++;
                                if (bit < Vcb->MetadataHint)
                                    Vcb->MetadataHint = bit;
                            }
                            if (!asUsed)
                                Vcb->MetadataPendingAlloc[bit >> 3] &= ~mask;
                        }
                        Vcb->MetadataBitmapModified = TRUE;
                        UDFSetModified(Vcb);
                        if (!asUsed) {
                            Map[i].extLength = (len << BSh) |
                                (EXTENT_NOT_RECORDED_NOT_ALLOCATED << 30);
                            Map[i].extLocation = 0;
                        }
                        i++;
                        continue;
                    }
                }
            }

            if (lba < partRoot)
                ExRaiseStatus(STATUS_FILE_CORRUPT_ERROR);
            // Validate the same partition-relative range that is marked below.
            // Comparing the physical LBA with the bitmap's bit count leaves the
            // partition's final PartitionRoot blocks free after allocation.
            uint32 lbn = lba - partRoot;
            if (lbn >= Vcb->FSBM_BitCount || len > Vcb->FSBM_BitCount - lbn ||
                (!Vcb->BitmapFcb && !Vcb->FSBM_Bitmap) ||
                ((asXXX & AS_BAD) && !Vcb->BSBM_Bitmap))
                ExRaiseStatus(STATUS_FILE_CORRUPT_ERROR);

            if ((asXXX & AS_DISCARDED) && !(asXXX & AS_PRIVATE_ROLLBACK) &&
                Vcb->TruncateTxnOwner == PsGetCurrentThread()) {
                /* The shortened FE has not passed its device barrier yet.
                 * Neither the RAM bitmap nor the lower TRIM may expose these
                 * sectors, including to an allocation made by this thread
                 * while serializing the new FE. */
                if (Vcb->TruncateTxnReleased && len &&
                    Vcb->TruncateTxnCount < Vcb->TruncateTxnCapacity) {
                    PEXTENT_MAP entry = &Vcb->TruncateTxnReleased[
                        Vcb->TruncateTxnCount++];
                    entry->extLocation = lba;
                    entry->extLength = len << BSh;
                } else {
                    Vcb->TruncateTxnError = STATUS_INSUFFICIENT_RESOURCES;
                }
                Map[i].extLength = (len << BSh) |
                    (EXTENT_NOT_RECORDED_NOT_ALLOCATED << 30);
                Map[i].extLocation = 0;
                i++;
                continue;
            }

            if (!physicalMarked) {
                Vcb->BitmapModified = TRUE;
                UDFSetModified(Vcb);
                physicalMarked = TRUE;
            }

            if (asUsed) {
                Vcb->SectorHint = lbn + len;
                if (Vcb->SectorHint >= Vcb->FSBM_BitCount) {
                    Vcb->SectorHint = 0;
                }
            } else if (lbn < Vcb->SectorHint) {
                Vcb->SectorHint = lbn;
            }

            // mark frag as XXX (see asUsed parameter)
            if (asUsed) {
                ASSERT(len);
                if (Vcb->BitmapFcb) {
                    // Per-page: clear bits (used = 0) across page boundaries
                    ULONG remaining = len;
                    ULONG pos = lbn;
                    while (remaining > 0) {
                        UDFPinBitmapPage(Vcb, pos);
                        if (pos < Vcb->BitmapPageStartLbn ||
                            pos - Vcb->BitmapPageStartLbn >= Vcb->BitmapPageBitCount)
                            ExRaiseStatus(STATUS_DISK_CORRUPT_ERROR);
                        ULONG localIdx = pos - Vcb->BitmapPageStartLbn;
                        ULONG bitsInPage = min(remaining, Vcb->BitmapPageBitCount - localIdx);
                        if (!bitsInPage)
                            ExRaiseStatus(STATUS_DISK_CORRUPT_ERROR);
                        Vcb->FreeAllocUnits -= UDFCountFreeBits(&Vcb->BitmapRtl, localIdx, bitsInPage);
                        RtlClearBits(&Vcb->BitmapRtl, localIdx, bitsInPage);
                        UDFDirtyBitmapPage(Vcb);
                        pos += bitsInPage;
                        remaining -= bitsInPage;
                    }
                } else {
                    UDFSetUsedBits(Vcb->FSBM_Bitmap, lbn, len);
                }

            } else {
                ASSERT(len);
                if (Vcb->BitmapFcb) {
                    // Per-page: set bits (free = 1) across page boundaries
                    ULONG remaining = len;
                    ULONG pos = lbn;
                    while (remaining > 0) {
                        UDFPinBitmapPage(Vcb, pos);
                        if (pos < Vcb->BitmapPageStartLbn ||
                            pos - Vcb->BitmapPageStartLbn >= Vcb->BitmapPageBitCount)
                            ExRaiseStatus(STATUS_DISK_CORRUPT_ERROR);
                        ULONG localIdx = pos - Vcb->BitmapPageStartLbn;
                        ULONG bitsInPage = min(remaining, Vcb->BitmapPageBitCount - localIdx);
                        if (!bitsInPage)
                            ExRaiseStatus(STATUS_DISK_CORRUPT_ERROR);
                        Vcb->FreeAllocUnits += bitsInPage - UDFCountFreeBits(&Vcb->BitmapRtl, localIdx, bitsInPage);
                        RtlSetBits(&Vcb->BitmapRtl, localIdx, bitsInPage);
                        UDFDirtyBitmapPage(Vcb);
                        pos += bitsInPage;
                        remaining -= bitsInPage;
                    }
                } else {
                    UDFSetFreeBits(Vcb->FSBM_Bitmap, lbn, len);
                }
                if (asXXX & AS_BAD) {
                    UDFSetBits(Vcb->BSBM_Bitmap, lbn, len);
                }
                UDFMarkBadSpaceAsUsed(Vcb, lbn, len);

                /* A filesystem free does not repair a physical defect. Keep
                 * persistent sparing translations across delete/reallocation. */
                Map[i].extLength = (len << BSh) | (EXTENT_NOT_RECORDED_NOT_ALLOCATED << 30);
                Map[i].extLocation = 0;
            }

            i++;
        }

    } _SEH2_FINALLY {
        UDFUnpinBitmapPage(Vcb);
    } _SEH2_END;
} // end UDFMarkSpaceAsXXXNoProtect_()

/*
    This routine marks space described by Mapping as Used/Freed (optionaly)
    It protects data with sync Resource
 */
void
UDFMarkSpaceAsXXX_(
    IN PVCB Vcb,
    IN PEXTENT_MAP Map,
    IN uint32 asXXX
#ifdef UDF_TRACK_ONDISK_ALLOCATION
   ,IN uint32 FE_lba,
    IN uint32 BugCheckId,
    IN uint32 Line
#endif //UDF_TRACK_ONDISK_ALLOCATION
    )
{
    if (!Map) return;
    if (!Map[0].extLength) {
#ifdef UDF_DBG
        ASSERT(!Map[0].extLocation);
#endif // UDF_DBG
        return;
    }

    UDFAcquireResourceExclusive(&(Vcb->BitMapResource1), TRUE);
    _SEH2_TRY {
#ifdef UDF_TRACK_ONDISK_ALLOCATION
        UDFMarkSpaceAsXXXNoProtect_(Vcb, Map, asXXX, FE_lba, BugCheckId, Line);
#else //UDF_TRACK_ONDISK_ALLOCATION
        UDFMarkSpaceAsXXXNoProtect_(Vcb, Map, asXXX);
#endif //UDF_TRACK_ONDISK_ALLOCATION
    } _SEH2_FINALLY {
        UDFReleaseResource(&(Vcb->BitMapResource1));
    } _SEH2_END;

} // end UDFMarkSpaceAsXXX_()

/* An unresolved receipt is a separate owner from FI/FCB and from an
 * ordinary allocator ENOSPC. The original request retains FirstError;
 * later admission reports the rollback fault if that made the state unknown. */
NTSTATUS
UDFAllocationFailureStatus(PVCB Vcb)
{
    NTSTATUS Status = (NTSTATUS)InterlockedCompareExchange(
        &Vcb->FailedAllocationError, STATUS_SUCCESS, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return Status;
    return InterlockedCompareExchange(&Vcb->FailedAllocationCount, 0, 0) ?
           STATUS_FILE_CORRUPT_ERROR : STATUS_SUCCESS;
}

/* Caller owns BitMapResource1. Never feed the receipt itself to the legacy
 * free helper: it turns freed coordinates into holes, including on a partial
 * failure. A stack copy also explicitly identifies physical/private ownership
 * and cannot be absorbed by an unrelated live-file truncate transaction. */
static VOID
UDFRollbackPrivatePhysicalClaims(PVCB Vcb, PUDF_ALLOCATION_FAILURE Receipt)
{
    uint32 i, Count, Length;
    uint64 Blocks;
    EXTENT_MAP One[2];
    if (Receipt->Metadata || !Receipt->Mapping || Receipt->MappingBytes < 2 * sizeof(EXTENT_MAP) ||
        Receipt->MappingBytes % sizeof(EXTENT_MAP) || Receipt->PartRef >= Vcb->PartitionMaps ||
        Vcb->Partitions[Receipt->PartRef].PartitionRoot != Receipt->PartitionRoot ||
        Vcb->Partitions[Receipt->PartRef].PartitionLen != Receipt->PartitionLength)
        ExRaiseStatus(STATUS_FILE_CORRUPT_ERROR);
    Count = Receipt->MappingBytes / sizeof(EXTENT_MAP) - 1;
    One[1].extLength = One[1].extLocation = 0;
    for (i = 0; i < Count; i++) {
        One[0] = Receipt->Mapping[i];
        Length = One[0].extLength & UDF_EXTENT_LENGTH_MASK;
        Blocks = Length >> Vcb->SectorShift;
        if (!Length || (Length & (Vcb->SectorSize - 1)) ||
            (One[0].extLength >> 30) != EXTENT_NOT_RECORDED_ALLOCATED ||
            One[0].extLocation < Receipt->PartitionRoot ||
            (uint64)One[0].extLocation - Receipt->PartitionRoot >= Receipt->PartitionLength ||
            Blocks > Receipt->PartitionLength - (One[0].extLocation - Receipt->PartitionRoot))
            ExRaiseStatus(STATUS_FILE_CORRUPT_ERROR);
        UDFMarkSpaceAsXXXNoProtect(Vcb, 0, One,
            AS_DISCARDED | AS_PHYSICAL | AS_PRIVATE_ROLLBACK);
    }
    Receipt->HasClaims = FALSE;
}

/* A metadata child claim owns logical metadata bitmap bits. Its physical
 * bytes remain owned by the metadata file regardless of this rollback. All
 * bounds, current allocation bits and stable-prefix addresses are checked
 * before the first resident bitmap mutation. Never call the physical marker. */
static VOID
UDFRollbackPrivateMetadataClaims(PVCB Vcb, PUDF_ALLOCATION_FAILURE Receipt)
{
    uint32 i, First = Receipt->MetadataFirstBit, Blocks = Receipt->MetadataBitCount;
    PUDF_METADATA_VIEW View = UDFAcquireMetadataView(Vcb);
    BOOLEAN Valid = FALSE;
    if (View) {
        if (Receipt->Metadata && Receipt->PartRef == Vcb->MetadataRef &&
            Receipt->PartRef < Vcb->PartitionMaps &&
            Vcb->Partitions[Receipt->PartRef].PartitionType == UDF_METADATA_MAP25 &&
            Receipt->PartitionRoot == Vcb->Partitions[Receipt->PartRef].PartitionRoot &&
            Receipt->PartitionLength <= Vcb->Partitions[Receipt->PartRef].PartitionLen &&
            Vcb->MetadataBitmap && Vcb->MetadataPendingAlloc && Blocks &&
            First < Vcb->MetadataBitCount && Blocks <= Vcb->MetadataBitCount - First &&
            ((uint64)Vcb->MetadataBitCount + 7) / 8 <= Vcb->MetadataBitmapByteCount &&
            Vcb->MetadataFreeCount <= Vcb->MetadataBitCount - Blocks &&
            Receipt->Mapping && Receipt->MappingBytes == 2 * sizeof(EXTENT_MAP) &&
            !Receipt->Mapping[1].extLength &&
            (Receipt->Mapping[0].extLength >> 30) == EXTENT_NOT_RECORDED_ALLOCATED &&
            (Receipt->Mapping[0].extLength & UDF_EXTENT_LENGTH_MASK) ==
                ((uint64)Blocks << Vcb->SectorShift)) {
            Valid = TRUE;
            for (i = 0; i < Blocks; i++) {
                if ((Vcb->MetadataBitmap[(First + i) >> 3] & (1 << ((First + i) & 7))) ||
                    UDFMetadataViewLbaToPhys(View, First + i, FALSE, NULL) !=
                        (uint64)Receipt->Mapping[0].extLocation + i) {
                    Valid = FALSE;
                    break;
                }
            }
        }
        UDFReleaseMetadataView(View);
    }
    if (!Valid) ExRaiseStatus(STATUS_FILE_CORRUPT_ERROR);
    for (i = 0; i < Blocks; i++) {
        Vcb->MetadataBitmap[(First + i) >> 3] |= (uint8)(1 << ((First + i) & 7));
        Vcb->MetadataPendingAlloc[(First + i) >> 3] &= ~(1 << ((First + i) & 7));
    }
    Vcb->MetadataFreeCount += Blocks;
    Vcb->MetadataHint = min(Vcb->MetadataHint, First);
    Vcb->MetadataBitmapModified = TRUE;
    UDFSetModified(Vcb);
    Receipt->HasClaims = FALSE;
}

static VOID
UDFRetainFailedAllocation(PVCB Vcb, PUDF_ALLOCATION_FAILURE Receipt)
{
    NTSTATUS Error;
    if (NT_SUCCESS(Receipt->FirstError))
        Receipt->FirstError = STATUS_FILE_CORRUPT_ERROR;
    Error = NT_SUCCESS(Receipt->RollbackError) ? Receipt->FirstError : Receipt->RollbackError;
    Receipt->Next = Vcb->FailedAllocations;
    Vcb->FailedAllocations = Receipt;
    Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
    InterlockedIncrement(&Vcb->FailedAllocationCount);
    InterlockedCompareExchange(&Vcb->FailedAllocationError, Error, STATUS_SUCCESS);
}

/* Membership is protected by BitMapResource1. An active lease is a VCB
 * owner even while its caller's private work map is not published. */
static PUDF_ALLOCATION_FAILURE*
UDFFindActiveAllocationLease(PVCB Vcb, PUDF_ALLOCATION_FAILURE Lease)
{
    PUDF_ALLOCATION_FAILURE* Link = &Vcb->ActiveAllocationLeases;
    LONG Remaining = Vcb->ActiveAllocationLeaseCount;
    while (*Link && Remaining-- > 0) {
        if (*Link == Lease)
            return Link;
        Link = &(*Link)->Next;
    }
    return NULL;
}

NTSTATUS
UDFPublishAllocationLease(PVCB Vcb, PUDF_ALLOCATION_FAILURE Lease)
{
    NTSTATUS Status = STATUS_FILE_CORRUPT_ERROR;
    UDFAcquireResourceExclusive(&Vcb->BitMapResource1, TRUE);
    _SEH2_TRY {
        if (Lease && UDFFindActiveAllocationLease(Vcb, Lease)) {
            Lease->Published = TRUE; /* before the first visible field/I/O */
            Status = STATUS_SUCCESS;
        }
    } _SEH2_FINALLY {
        UDFReleaseResource(&Vcb->BitMapResource1);
    } _SEH2_END;
    return Status;
}

NTSTATUS
UDFCommitAllocationLease(PVCB Vcb, PUDF_ALLOCATION_FAILURE* Lease)
{
    PUDF_ALLOCATION_FAILURE Node, *Link;
    NTSTATUS Status = STATUS_FILE_CORRUPT_ERROR;
    if (!Lease || !*Lease)
        return STATUS_INVALID_PARAMETER;
    Node = *Lease;
    UDFAcquireResourceExclusive(&Vcb->BitMapResource1, TRUE);
    _SEH2_TRY {
        Link = UDFFindActiveAllocationLease(Vcb, Node);
        if (Link && Node->Published) {
            /* A live, separately allocated mapping now owns these blocks.
             * Only bookkeeping buffers are retired; no bitmap change. */
            *Link = Node->Next;
            InterlockedDecrement(&Vcb->ActiveAllocationLeaseCount);
            MyFreePool__(Node->Mapping);
            MyFreePool__(Node);
            *Lease = NULL;
            Status = STATUS_SUCCESS;
        }
    } _SEH2_FINALLY {
        UDFReleaseResource(&Vcb->BitMapResource1);
    } _SEH2_END;
    return Status;
}

NTSTATUS
UDFAbortAllocationLease(PVCB Vcb, PUDF_ALLOCATION_FAILURE* Lease, NTSTATUS FirstError)
{
    PUDF_ALLOCATION_FAILURE Node, *Link;
    NTSTATUS Status = STATUS_SUCCESS;
    BOOLEAN Located = FALSE;
    if (!Lease || !*Lease)
        return STATUS_SUCCESS;
    Node = *Lease;
    UDFAcquireResourceExclusive(&Vcb->BitMapResource1, TRUE);
    _SEH2_TRY {
        Link = UDFFindActiveAllocationLease(Vcb, Node);
        if (!Link) {
            Status = STATUS_FILE_CORRUPT_ERROR;
            _SEH2_LEAVE;
        }
        Located = TRUE;
        Node->FirstError = NT_SUCCESS(FirstError) ? STATUS_FILE_CORRUPT_ERROR : FirstError;
        if (Node->Published || Node->Uncertain) {
            /* Once FE publication can have happened, freeing the blocks is
             * unsafe even when the final on-disk generation is unknown. */
            Node->Uncertain = TRUE;
            Status = Node->FirstError;
        } else {
            _SEH2_TRY {
                if (Node->Metadata)
                    UDFRollbackPrivateMetadataClaims(Vcb, Node);
                else
                    UDFRollbackPrivatePhysicalClaims(Vcb, Node);
            } _SEH2_EXCEPT(FsRtlIsNtstatusExpected(_SEH2_GetExceptionCode()) ?
                          EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
                Status = _SEH2_GetExceptionCode();
                Node->RollbackError = Status;
                Node->Uncertain = TRUE;
            } _SEH2_END;
        }
    } _SEH2_FINALLY {
        if (Located) {
            if (_SEH2_AbnormalTermination()) {
                Node->Uncertain = TRUE;
                Node->RollbackError = STATUS_FILE_CORRUPT_ERROR;
            }
            Link = UDFFindActiveAllocationLease(Vcb, Node);
            if (Link) {
                *Link = Node->Next;
                InterlockedDecrement(&Vcb->ActiveAllocationLeaseCount);
                Node->Leased = FALSE;
                if (Node->Uncertain || Node->HasClaims)
                    UDFRetainFailedAllocation(Vcb, Node);
                else {
                    MyFreePool__(Node->Mapping);
                    MyFreePool__(Node);
                }
                *Lease = NULL;
            }
        }
        UDFUnpinBitmapPage(Vcb);
        UDFReleaseResource(&Vcb->BitMapResource1);
    } _SEH2_END;
    return Status;
}

static NTSTATUS
UDFAllocatePhysicalAttempt(PIRP_CONTEXT IrpContext, PVCB Vcb,
    uint32 Blocks, uint32 SearchStart, uint32 SearchLim, uint8 AllocFlags,
    PUDF_ALLOCATION_FAILURE Receipt)
{
    uint32 Remaining = Blocks, Found, Take, Location;
    uint32 DataHint = Vcb->SectorHint;
    BOOLEAN FeLocality = (AllocFlags & EXTENT_FLAG_FE_LOCALITY) != 0;
    PEXTENT_MAP Tail = NULL;
    EXTENT_MAP One[2];
    NTSTATUS Status;

    One[1].extLength = One[1].extLocation = 0;
    if (Blocks > SearchLim - SearchStart)
        goto no_space;
    while (Remaining) {
        Found = 0;
        Location = UDFFindMinSuitableExtent(Vcb, Remaining, SearchStart,
                                             SearchLim, &Found, AllocFlags);
        if (!Found)
            goto no_space;
        Take = min(Found, Remaining);
        if (!Location || Location < SearchStart || Location >= SearchLim ||
            Take > SearchLim - Location)
            return STATUS_FILE_CORRUPT_ERROR;
        One[0].extLocation = Location;
        One[0].extLength = (Take << Vcb->SectorShift) | (EXTENT_NOT_RECORDED_ALLOCATED << 30);
        if (Vcb->TargetDeviceObject->DeviceType != FILE_DEVICE_DISK &&
            (AllocFlags & EXTENT_FLAG_VERIFY)) {
            Status = UDFCheckArea(IrpContext, Vcb, Location, Take);
            if (!NT_SUCCESS(Status))
                return Status;
        }
        if (!Receipt->Mapping) {
#ifdef UDF_TRACK_EXTENT_TO_MAPPING
            Receipt->Mapping = UDFExtentToMapping_(&One[0], Receipt->SourceId, Receipt->SourceLine);
#else
            Receipt->Mapping = UDFExtentToMapping(&One[0]);
#endif
            if (!Receipt->Mapping)
                return STATUS_INSUFFICIENT_RESOURCES;
            Receipt->MappingBytes = 2 * sizeof(EXTENT_MAP);
        } else {
#ifdef UDF_TRACK_EXTENT_TO_MAPPING
            Tail = UDFExtentToMapping_(&One[0], Receipt->SourceId, Receipt->SourceLine);
#else
            Tail = UDFExtentToMapping(&One[0]);
#endif
            if (!Tail)
                return STATUS_INSUFFICIENT_RESOURCES;
            _SEH2_TRY {
                Status = UDFAppendMapping(&Receipt->Mapping, Tail);
            } _SEH2_FINALLY {
                MyFreePool__(Tail);
                Tail = NULL;
            } _SEH2_END;
            if (!NT_SUCCESS(Status))
                return Status;
            Receipt->MappingBytes += sizeof(EXTENT_MAP);
        }
        /* Own exact immutable coordinates before any page/counter changes. */
        Receipt->HasClaims = TRUE;
        Receipt->ClaimInProgress = TRUE;
        UDFMarkSpaceAsXXXNoProtect(Vcb, 0, One, AS_USED | AS_PHYSICAL);
        Receipt->ClaimInProgress = FALSE;
        if (FeLocality) {
            uint32 Next = Location - Receipt->PartitionRoot + 1;
            Vcb->FeSectorHint = Next < SearchLim - Receipt->PartitionRoot ?
                               Next : SearchStart - Receipt->PartitionRoot;
            Vcb->SectorHint = DataHint;
        }
        Remaining -= Take;
    }
    return STATUS_SUCCESS;

no_space:
    if (FeLocality)
        Vcb->FeSectorHint = MAXULONG;
    else
        UDFPrint(("UDF BM: DISK_FULL remaining=%x start=%x limit=%x\n",
                  Remaining, SearchStart, SearchLim));
    return STATUS_DISK_FULL;
}

/* Typed allocation: address equality cannot choose a metadata partition.
 * The receipt is allocated before the first claim and consumed exactly once
 * by success, a complete rollback, or the persistent VCB failure inventory. */
static NTSTATUS
UDFAllocFreeExtentInternal_(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN uint32 PartRef,
    IN int64 Length,
    IN uint32 SearchStart,
    IN uint32 SearchLim,
    OUT PEXTENT_INFO ExtInfo,
    IN uint8 AllocFlags,
    OUT PUDF_ALLOCATION_FAILURE* Lease
#ifdef UDF_TRACK_ALLOC_FREE_EXTENT
   ,IN uint32 src,
    IN uint32 line
#endif
    )
{
    PUDF_ALLOCATION_FAILURE Receipt;
    PUDFPartMap Part;
    uint64 End, Rounded;
    uint32 Blocks;
    BOOLEAN Locked = FALSE;
    NTSTATUS Status = STATUS_UNSUCCESSFUL;
    if (Lease) *Lease = NULL;
    if (!ExtInfo)
        return STATUS_INVALID_PARAMETER;
    ExtInfo->Mapping = NULL;
    ExtInfo->Length = 0;
    ExtInfo->Offset = 0;
    ExtInfo->Modified = FALSE;
    ExtInfo->Flags = AllocFlags;
    if (!Vcb || !Vcb->Partitions || PartRef >= Vcb->PartitionMaps || Length < 0 ||
        !Vcb->SectorSize || Vcb->SectorShift >= 32 ||
        Vcb->SectorSize != ((uint32)1 << Vcb->SectorShift) || !Vcb->TargetDeviceObject)
        return STATUS_INVALID_PARAMETER;
    Status = UDFAllocationFailureStatus(Vcb);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Vcb->CDR_Mode || Vcb->Vat)
        return STATUS_NOT_SUPPORTED;
    if (Vcb->VcbState & VCB_STATE_VOLUME_READ_ONLY)
        return STATUS_MEDIA_WRITE_PROTECTED;
    if (!Length)
        return STATUS_SUCCESS;
    Rounded = ((uint64)Length + Vcb->SectorSize - 1) & ~((uint64)Vcb->SectorSize - 1);
    if (Rounded > ALIGN_DOWN_BY(UDF_EXTENT_LENGTH_MASK, Vcb->SectorSize))
        return STATUS_INVALID_PARAMETER;
    Part = &Vcb->Partitions[PartRef];
    End = (uint64)Part->PartitionRoot + Part->PartitionLen;
    if (End > MAXULONG || SearchStart < Part->PartitionRoot ||
        SearchLim < SearchStart || SearchLim > End)
        return STATUS_INVALID_PARAMETER;
    if (Part->PartitionType == UDF_METADATA_MAP25) {
        if (!Vcb->MetadataFileInfo || PartRef != Vcb->MetadataRef ||
            SearchStart != Part->PartitionRoot || SearchLim != End)
            return STATUS_NOT_SUPPORTED;
        return UDFAllocMetadataExtentInternal(IrpContext, Vcb, Length, ExtInfo, Lease);
    }
    if (Part->PartitionType != UDF_TYPE1_MAP15 && Part->PartitionType != UDF_SPARABLE_MAP15)
        return STATUS_NOT_SUPPORTED;
    Blocks = (uint32)(Rounded >> Vcb->SectorShift);
    if ((AllocFlags & EXTENT_FLAG_FE_LOCALITY) && (Blocks != 1 || Length > Vcb->SectorSize))
        return STATUS_INVALID_PARAMETER;
    Receipt = (PUDF_ALLOCATION_FAILURE)MyAllocatePool__(NonPagedPool, sizeof(*Receipt));
    if (!Receipt)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Receipt, sizeof(*Receipt));
    Receipt->PartRef = PartRef;
    Receipt->PartitionRoot = Part->PartitionRoot;
    Receipt->PartitionLength = Part->PartitionLen;
#ifdef UDF_TRACK_ALLOC_FREE_EXTENT
    Receipt->SourceId = src;
    Receipt->SourceLine = line;
#else
    Receipt->SourceId = UDF_BUG_CHECK_ID;
    Receipt->SourceLine = __LINE__;
#endif
    _SEH2_TRY {
        UDFAcquireResourceExclusive(&Vcb->BitMapResource1, TRUE);
        Locked = TRUE;
        _SEH2_TRY {
            /* A prior claimant may have failed while this thread waited. */
            Status = UDFAllocationFailureStatus(Vcb);
            if (NT_SUCCESS(Status) && (Vcb->VcbState & VCB_STATE_VOLUME_READ_ONLY))
                Status = STATUS_MEDIA_WRITE_PROTECTED;
            if (NT_SUCCESS(Status) && Lease && Vcb->ActiveAllocationLeaseCount == MAXLONG)
                Status = STATUS_INSUFFICIENT_RESOURCES;
            if (NT_SUCCESS(Status) && ((!Vcb->BitmapFcb && !Vcb->FSBM_Bitmap) ||
                Part->PartitionLen > Vcb->FSBM_BitCount ||
                Vcb->Partitions[0].PartitionRoot != Part->PartitionRoot))
                Status = STATUS_FILE_CORRUPT_ERROR;
            if (NT_SUCCESS(Status))
                Status = UDFOpenIntegrityForWrite(IrpContext, Vcb, FALSE);
            if (NT_SUCCESS(Status))
                Status = UDFAllocatePhysicalAttempt(IrpContext, Vcb, Blocks,
                                  SearchStart, SearchLim, AllocFlags, Receipt);
        } _SEH2_EXCEPT(FsRtlIsNtstatusExpected(_SEH2_GetExceptionCode()) ?
                      EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
            Status = _SEH2_GetExceptionCode();
            Receipt->Uncertain = Receipt->ClaimInProgress;
        } _SEH2_END;
        Receipt->FirstError = Status;
        if (!NT_SUCCESS(Status) && Receipt->HasClaims && !Receipt->Uncertain) {
            _SEH2_TRY {
                UDFRollbackPrivatePhysicalClaims(Vcb, Receipt);
            } _SEH2_EXCEPT(FsRtlIsNtstatusExpected(_SEH2_GetExceptionCode()) ?
                          EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
                Receipt->RollbackError = _SEH2_GetExceptionCode();
                Receipt->Uncertain = TRUE;
            } _SEH2_END;
        }
        if (NT_SUCCESS(Status)) {
            ExtInfo->Mapping = Receipt->Mapping;
            ExtInfo->Length = Length;
            ExtInfo->Modified = TRUE;
            if (Lease) {
                Receipt->Leased = TRUE;
                Receipt->Next = Vcb->ActiveAllocationLeases;
                Vcb->ActiveAllocationLeases = Receipt;
                InterlockedIncrement(&Vcb->ActiveAllocationLeaseCount);
                *Lease = Receipt;
                Receipt = NULL;
            } else {
                Receipt->Mapping = NULL;
                Receipt->HasClaims = FALSE;
            }
        }
    } _SEH2_FINALLY {
        if (Locked) {
            if (Receipt && _SEH2_AbnormalTermination() && Receipt->HasClaims)
                Receipt->Uncertain = TRUE;
            if (Receipt && Receipt->HasClaims && Receipt->Mapping) {
                /* No I/O or allocation in this retention path. Never drop
                 * an unknown owner merely because its caller is unwinding. */
                if (NT_SUCCESS(Receipt->FirstError))
                    Receipt->FirstError = NT_SUCCESS(Status) ? STATUS_FILE_CORRUPT_ERROR : Status;
                UDFRetainFailedAllocation(Vcb, Receipt);
                Receipt = NULL;
            }
            UDFUnpinBitmapPage(Vcb);
            UDFReleaseResource(&Vcb->BitMapResource1);
        }
        if (Receipt) {
            if (Receipt->Mapping) MyFreePool__(Receipt->Mapping);
            MyFreePool__(Receipt);
        }
    } _SEH2_END;
    return Status;
} // end UDFAllocFreeExtent_()

NTSTATUS
UDFAllocFreeExtent_(PIRP_CONTEXT IrpContext, PVCB Vcb, uint32 PartRef,
    int64 Length, uint32 SearchStart, uint32 SearchLim, PEXTENT_INFO ExtInfo,
    uint8 AllocFlags
#ifdef UDF_TRACK_ALLOC_FREE_EXTENT
    , uint32 src, uint32 line
#endif
    )
{
    return UDFAllocFreeExtentInternal_(IrpContext, Vcb, PartRef, Length,
        SearchStart, SearchLim, ExtInfo, AllocFlags, NULL
#ifdef UDF_TRACK_ALLOC_FREE_EXTENT
        , src, line
#endif
        );
}

NTSTATUS
UDFAllocExtentLease(PIRP_CONTEXT IrpContext, PVCB Vcb, uint32 PartRef,
    int64 Length, uint32 SearchStart, uint32 SearchLim, PEXTENT_INFO ExtInfo,
    uint8 AllocFlags, PUDF_ALLOCATION_FAILURE* Lease)
{
    if (Lease) *Lease = NULL;
    if (ExtInfo) {
        ExtInfo->Mapping = NULL;
        ExtInfo->Length = 0;
        ExtInfo->Offset = 0;
        ExtInfo->Modified = FALSE;
    }
    if (!Lease || Length <= 0)
        return STATUS_INVALID_PARAMETER;
    return UDFAllocFreeExtentInternal_(IrpContext, Vcb, PartRef, Length,
        SearchStart, SearchLim, ExtInfo, AllocFlags, Lease
#ifdef UDF_TRACK_ALLOC_FREE_EXTENT
        , UDF_BUG_CHECK_ID, __LINE__
#endif
        );
}

NTSTATUS
UDFAllocPhysicalExtentLease(PIRP_CONTEXT IrpContext, PVCB Vcb, uint32 PartRef,
    int64 Length, uint32 SearchStart, uint32 SearchLim, PEXTENT_INFO ExtInfo,
    uint8 AllocFlags, PUDF_ALLOCATION_FAILURE* Lease)
{
    if (Lease) *Lease = NULL;
    if (ExtInfo) {
        ExtInfo->Mapping = NULL;
        ExtInfo->Length = 0;
        ExtInfo->Offset = 0;
        ExtInfo->Modified = FALSE;
    }
    if (!Lease || Length <= 0)
        return STATUS_INVALID_PARAMETER;
    if (!Vcb || !Vcb->Partitions || PartRef >= Vcb->PartitionMaps ||
        Vcb->Partitions[PartRef].PartitionType == UDF_METADATA_MAP25)
        return STATUS_NOT_SUPPORTED;
    return UDFAllocFreeExtentInternal_(IrpContext, Vcb, PartRef, Length,
        SearchStart, SearchLim, ExtInfo, AllocFlags, Lease
#ifdef UDF_TRACK_ALLOC_FREE_EXTENT
        , UDF_BUG_CHECK_ID, __LINE__
#endif
        );
}

/*
    Returns block-count
 */
/* Count only the declared bits, without a padded ULONG read beyond the
 * final bitmap byte. Set bits represent free blocks in this bitmap. */
static uint32
UDFCountBitmapBytes(const UCHAR* Bitmap, uint32 BitCount)
{
    uint32 Count = 0, i, Bytes = BitCount >> 3;
    for (i = 0; i < Bytes; i++)
        Count += bit_count_tab[Bitmap[i]];
    if (BitCount & 7)
        Count += bit_count_tab[Bitmap[Bytes] & ((1U << (BitCount & 7)) - 1)];
    return Count;
}

uint32
__fastcall
UDFGetPartFreeSpace(IN PVCB Vcb, IN uint32 partNum)
{
    uint32 Count = 0, Position = 0, Bits, Start;
    SIZE_T BufferOffset, RequiredBytes;
    const UCHAR* Bitmap;
    BOOLEAN Unpin = FALSE;

    if (!Vcb || partNum >= Vcb->PartitionMaps)
        ExRaiseStatus(STATUS_INVALID_PARAMETER);
    /* Pin state is shared even for readers. Pair all fallible pin/count
     * operations with cleanup; an incomplete scan is never a free-space
     * value that a caller may serialize into a CLOSED LVID. */
    UDFAcquireResourceExclusive(&Vcb->BitMapResource1, TRUE);
    _SEH2_TRY {
        if (Vcb->MetadataFileInfo && partNum == Vcb->MetadataRef) {
            Count = Vcb->MetadataFreeCount;
        } else if (Vcb->BitmapFcb) {
            Unpin = TRUE;
            while (Position < Vcb->FSBM_BitCount) {
                UDFPinBitmapPage(Vcb, Position);
                Bits = Vcb->BitmapPageBitCount;
                Start = Vcb->BitmapPageStartLbn;
                Bitmap = (const UCHAR*)Vcb->BitmapRtl.Buffer;
                if (!Bits || Start != Position || Bits > Vcb->FSBM_BitCount - Position ||
                    !Vcb->BitmapPinnedData || !Bitmap ||
                    (ULONG_PTR)Bitmap < (ULONG_PTR)Vcb->BitmapPinnedData)
                    ExRaiseStatus(STATUS_DISK_CORRUPT_ERROR);
                BufferOffset = (ULONG_PTR)Bitmap - (ULONG_PTR)Vcb->BitmapPinnedData;
                RequiredBytes = ((uint64)Bits + 7) >> 3;
                if (BufferOffset > Vcb->BitmapPinnedLength ||
                    RequiredBytes > Vcb->BitmapPinnedLength - BufferOffset ||
                    Vcb->BitmapRtl.SizeOfBitMap != Bits)
                    ExRaiseStatus(STATUS_DISK_CORRUPT_ERROR);
                Count += UDFCountBitmapBytes(Bitmap, Bits);
                Position += Bits;
            }
        } else if (Vcb->FSBM_Bitmap) {
            RequiredBytes = ((uint64)Vcb->FSBM_BitCount + 7) >> 3;
            if (RequiredBytes > Vcb->FSBM_ByteCount)
                ExRaiseStatus(STATUS_DISK_CORRUPT_ERROR);
            Count = UDFCountBitmapBytes((const UCHAR*)Vcb->FSBM_Bitmap, Vcb->FSBM_BitCount);
        }
        /* Read-only optical media may have no free-space bitmap. It cannot
         * advertise allocatable blocks, so retain a zero count in that case. */
    } _SEH2_FINALLY {
        if (Unpin) UDFUnpinBitmapPage(Vcb);
        UDFReleaseResource(&Vcb->BitMapResource1);
    } _SEH2_END;
    return Count;
} // end UDFGetPartFreeSpace()

int64
__fastcall
UDFGetFreeSpace(PVCB Vcb)
{
    uint32 i;
    if (Vcb->CDR_Mode || Vcb->Vat)
        return 0; /* no sequential allocation/commit backend */
    /* All accepted physical aliases share one bitmap. Count that owner
     * once, never once per partition-map reference. */
    for (i = 0; i < Vcb->PartitionMaps; i++) {
        if (Vcb->Partitions[i].PartitionType == UDF_TYPE1_MAP15 ||
            Vcb->Partitions[i].PartitionType == UDF_SPARABLE_MAP15)
            return UDFGetPartFreeSpace(Vcb, i);
    }
    return 0;
} // end UDFGetFreeSpace()

/*
    Returns block-count
 */
int64
UDFGetTotalSpace(PVCB Vcb)
{
    uint32 i;
    for (i = 0; i < Vcb->PartitionMaps; i++) {
        if (Vcb->Partitions[i].PartitionType == UDF_TYPE1_MAP15 ||
            Vcb->Partitions[i].PartitionType == UDF_SPARABLE_MAP15)
            return Vcb->Partitions[i].PartitionLen;
    }
    return 0;
} // end UDFGetTotalSpace()
