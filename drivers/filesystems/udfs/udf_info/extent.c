////////////////////////////////////////////////////////////////////
// Copyright (C) Alexander Telyatnikov, Ivan Keliukh, Yegor Anchishkin, SKIF Software, 1999-2013. Kiev, Ukraine
// All rights reserved
// This file was released under the GPLv2 on June 2015.
////////////////////////////////////////////////////////////////////
/*
        Module name:

   extent.cpp

        Abstract:

   This file contains filesystem-specific routines
   responsible for extent & mapping management

*/

#include "udf.h"
#include <udf_descriptor.h>

#define         UDF_BUG_CHECK_ID                UDF_FILE_UDF_INFO_EXTENT

/*
    This routine converts offset in extent to Lba & returns offset in the 1st
    sector & bytes before end of block.
    Here we assume no references to AllocDescs
 */
uint32
UDFExtentOffsetToLba(
    IN PVCB Vcb,
    IN PEXTENT_MAP Extent,   // Extent array
    IN int64 Offset,      // offset in extent
    OUT uint32* SectorOffset,
    OUT PSIZE_T AvailLength,  // available data in this block
    OUT uint32* Flags,
    OUT uint32* Index
    )
{
    uint32 j = 0, Length, Type, LastType = EXTENT_NOT_RECORDED_NOT_ALLOCATED;
    uint64 Remaining, Physical;

    if (SectorOffset) *SectorOffset = 0;
    if (AvailLength) *AvailLength = 0;
    if (Flags) *Flags = EXTENT_NOT_RECORDED_NOT_ALLOCATED;
    if (Index) *Index = MAXULONG;
    if (!Vcb || !Extent || Offset < 0 || Vcb->SectorShift >= 32 ||
        !Vcb->SectorSize || Vcb->SectorSize != ((uint32)1 << Vcb->SectorShift))
        return LBA_OUT_OF_EXTENT;

    /* The mapping is an owned, terminated internal array. File offsets are
     * byte counts, not 32-bit block numbers or the low word of Offset. */
    Remaining = (uint64)Offset;
    for (;;) {
        Length = Extent->extLength & UDF_EXTENT_LENGTH_MASK;
        if (!Length) {
            /* Resize uses the last valid index/type when growing beyond EOF.
             * An empty map has no predecessor, including a freed-FE marker
             * whose length is zero but whose old type bits remain set. */
            if (j) {
                if (Index) *Index = j - 1;
                if (Flags) *Flags = LastType;
            }
            return LBA_OUT_OF_EXTENT;
        }
        Type = Extent->extLength >> 30;
        if (Type > EXTENT_NOT_RECORDED_NOT_ALLOCATED)
            return LBA_OUT_OF_EXTENT;
        if (Type != EXTENT_NOT_RECORDED_NOT_ALLOCATED &&
            (uint64)Extent->extLocation +
                (((uint64)Length + Vcb->SectorSize - 1) >> Vcb->SectorShift) > LBA_OUT_OF_EXTENT)
            return LBA_OUT_OF_EXTENT;
        if (Remaining < Length) {
            Physical = (uint64)Extent->extLocation + (Remaining >> Vcb->SectorShift);
            if (Type != EXTENT_NOT_RECORDED_NOT_ALLOCATED && Physical >= LBA_OUT_OF_EXTENT)
                return LBA_OUT_OF_EXTENT;
            if (SectorOffset) *SectorOffset = (uint32)Remaining & (Vcb->SectorSize - 1);
            if (AvailLength) *AvailLength = Length - (uint32)Remaining;
            if (Flags) *Flags = Type;
            if (Index) *Index = j;
            /* A hole has no physical sector, regardless of its file offset. */
            return Type == EXTENT_NOT_RECORDED_NOT_ALLOCATED ? 0 : (uint32)Physical;
        }
        if (Length & (Vcb->SectorSize - 1)) {
            /* A short final run has an EOF too. It cannot be followed by
             * another run with a fractional logical sector boundary. */
            if (!(Extent[1].extLength & UDF_EXTENT_LENGTH_MASK)) {
                if (Index) *Index = j;
                if (Flags) *Flags = Type;
            }
            return LBA_OUT_OF_EXTENT;
        }
        if (j == MAXULONG - 1)
            return LBA_OUT_OF_EXTENT;
        Remaining -= Length;
        LastType = Type;
        j++;
        Extent++;
    }
} // end UDFExtentOffsetToLba()

uint32
UDFNextExtentToLba(
    IN PVCB Vcb,
    IN PEXTENT_MAP Extent,   // Extent array
    OUT PSIZE_T AvailLength,  // available data in this block
    OUT uint32* Flags,
    OUT uint32* Index
    )
{
    return UDFExtentOffsetToLba(Vcb, Extent, 0, NULL, AvailLength, Flags, Index);
} // end UDFNextExtentToLba()

/*
    This routine locates frag containing specified Lba in extent
 */
ULONG
UDFLocateLbaInExtent(
    IN PVCB Vcb,
    IN PEXTENT_MAP Extent,   // Extent array
    IN lba_t lba
    )
{
    uint32 Length, i = 0;
    uint64 Blocks;

    if (!Vcb || !Extent || Vcb->SectorShift >= 32 || !Vcb->SectorSize ||
        Vcb->SectorSize != ((uint32)1 << Vcb->SectorShift))
        return LBA_OUT_OF_EXTENT;
    while ((Length = Extent->extLength & UDF_EXTENT_LENGTH_MASK) != 0) {
        Blocks = ((uint64)Length + Vcb->SectorSize - 1) >> Vcb->SectorShift;
        if ((Extent->extLength >> 30) < EXTENT_NOT_RECORDED_NOT_ALLOCATED &&
            lba >= Extent->extLocation &&
            (uint64)lba - Extent->extLocation < Blocks) {
            return i;
        }
        if (i == MAXULONG - 1)
            return LBA_OUT_OF_EXTENT;
        i++; //frag offset
        Extent++;
    }
    return LBA_OUT_OF_EXTENT;// index of item in extent, containing out Lba
} // end UDFLocateLbaInExtent()

/*
    This routine calculates total length of specified extent.
    Here we assume no references to AllocDescs
 */
int64
UDFGetExtentLength(
    IN PEXTENT_MAP Extent   // Extent array
    )
{
    int64 Total = 0;
    uint32 Count = 0, Length;
    const uint32 MaxCount = (MAXULONG - MY_HEAP_ALIGN) / sizeof(EXTENT_MAP);
    if (!Extent)
        return 0;
    while ((Length = Extent->extLength & UDF_EXTENT_LENGTH_MASK) != 0) {
        if ((Extent->extLength >> 30) > EXTENT_NOT_RECORDED_NOT_ALLOCATED ||
            Count >= MaxCount - 1 || (uint64)Total + Length > MAXLONGLONG)
            ExRaiseStatus(STATUS_FILE_CORRUPT_ERROR);
        Total += Length;
        Count++;
        Extent++;
    }
    return Total;
} // UDFGetExtentLength()

/*
    This routine appends Zero-terminator to single Extent-entry.
    Such operation makes it compatible with other internal routines
 */
PEXTENT_MAP
__fastcall
UDFExtentToMapping_(
    IN PEXTENT_AD Extent
#ifdef UDF_TRACK_EXTENT_TO_MAPPING
   ,IN ULONG src,
    IN ULONG line
#endif //UDF_TRACK_EXTENT_TO_MAPPING
    )
{
    PEXTENT_MAP Map;

#ifdef UDF_TRACK_EXTENT_TO_MAPPING
#define UDF_EXT_MAP_MULT 4
#else //UDF_TRACK_EXTENT_TO_MAPPING
#define UDF_EXT_MAP_MULT 2
#endif //UDF_TRACK_EXTENT_TO_MAPPING

    Map = (PEXTENT_MAP)MyAllocatePoolTag__(NonPagedPool , UDF_EXT_MAP_MULT *
                                                       sizeof(EXTENT_MAP), MEM_EXTMAP_TAG);
    if (!Map) return NULL;
    RtlZeroMemory((int8*)(Map+1), sizeof(EXTENT_MAP));
    Map[0].extLength = Extent->extLength;
    Map[0].extLocation = Extent->extLocation;
#ifdef UDF_TRACK_EXTENT_TO_MAPPING
    Map[2].extLength = src;
    Map[2].extLocation = line;
#endif //UDF_TRACK_EXTENT_TO_MAPPING
    return Map;
} // end UDFExtentToMapping()

/*
    This routine calculates file mapping length (in bytes) including
    ZERO-terminator
 */
uint32
UDFGetMappingLength(
    IN PEXTENT_MAP Extent
    )
{
    uint32 Bytes;
    NTSTATUS Status;
    if (!Extent)
        return 0;
    Status = UDFGetMappingByteCount(Extent, &Bytes);
    if (!NT_SUCCESS(Status))
        ExRaiseStatus(Status);
    return Bytes;
} // end UDFGetMappingLength()

/* Internal maps are owned and terminated; count bytes before allocation
 * arithmetic, including the allocator's alignment headroom. */
NTSTATUS
UDFGetMappingByteCount(PEXTENT_MAP Mapping, uint32* Bytes)
{
    uint32 Count = 0;
    const uint32 MaxCount = (MAXULONG - MY_HEAP_ALIGN) / sizeof(EXTENT_MAP);
    if (!Mapping || !Bytes)
        return STATUS_INVALID_PARAMETER;
    *Bytes = 0;
    while (Mapping[Count].extLength & UDF_EXTENT_LENGTH_MASK) {
        if ((Mapping[Count].extLength >> 30) > EXTENT_NOT_RECORDED_NOT_ALLOCATED ||
            Count >= MaxCount - 1)
            return STATUS_FILE_CORRUPT_ERROR;
        Count++;
    }
    *Bytes = (Count + 1) * sizeof(EXTENT_MAP);
    return STATUS_SUCCESS;
}

/* Append is a private copy/commit operation. In particular, allocation
 * failure must not masquerade as a successful unchanged mapping. Neither
 * the destination nor the tail is consumed on failure; the caller still
 * owns the tail's physical allocation and must retain or roll it back. */
NTSTATUS
__fastcall
UDFAppendMapping(PEXTENT_MAP* Mapping, PEXTENT_MAP Tail)
{
    PEXTENT_MAP NewMap;
    uint32 OldBytes, TailBytes, NewBytes;
    uint64 Total;
    NTSTATUS Status;

    if (!Mapping || !*Mapping || !Tail || *Mapping == Tail)
        return STATUS_INVALID_PARAMETER;
    Status = UDFGetMappingByteCount(*Mapping, &OldBytes);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = UDFGetMappingByteCount(Tail, &TailBytes);
    if (!NT_SUCCESS(Status))
        return Status;
    if (TailBytes == sizeof(EXTENT_MAP))
        return STATUS_SUCCESS;
    Total = (uint64)OldBytes + TailBytes - sizeof(EXTENT_MAP);
    if (Total > MAXULONG - MY_HEAP_ALIGN)
        return STATUS_INSUFFICIENT_RESOURCES;
    NewBytes = (uint32)Total;
    NewMap = (PEXTENT_MAP)MyAllocatePoolTag__(NonPagedPool, NewBytes, MEM_EXTMAP_TAG);
    if (!NewMap)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlCopyMemory(NewMap, *Mapping, OldBytes - sizeof(EXTENT_MAP));
    RtlCopyMemory((PCHAR)NewMap + OldBytes - sizeof(EXTENT_MAP), Tail, TailBytes);
    MyFreePool__(*Mapping);
    *Mapping = NewMap;
    return STATUS_SUCCESS;
}

/* Release only the private list wrappers after every underlying receipt is
 * committed to a live map or aborted/retained by the VCB lease inventory. */
NTSTATUS
UDFCompletePrivateExtent(PVCB Vcb, PUDF_PRIVATE_ALLOCATION* Claims,
                         BOOLEAN Transferred, NTSTATUS FirstError)
{
    PUDF_PRIVATE_ALLOCATION Claim, Next;
    NTSTATUS Status = FirstError, ReleaseStatus;
    if (!Claims) return STATUS_INVALID_PARAMETER;
    for (Claim = *Claims; Claim; Claim = Next) {
        Next = Claim->Next;
        if (Claim->Lease) {
            ReleaseStatus = Transferred ? UDFCommitAllocationLease(Vcb, &Claim->Lease) :
                UDFAbortAllocationLease(Vcb, &Claim->Lease, FirstError);
            if (NT_SUCCESS(Status) && !NT_SUCCESS(ReleaseStatus)) Status = ReleaseStatus;
            if (Claim->Lease) {
                /* The allocator's active/failed VCB inventory owns the
                 * receipt independently of this caller-private wrapper. */
                Claim->Lease->Uncertain = TRUE;
                ReleaseStatus = UDFAbortAllocationLease(Vcb, &Claim->Lease, Status);
                if (NT_SUCCESS(Status) && !NT_SUCCESS(ReleaseStatus)) Status = ReleaseStatus;
                if (Claim->Lease) {
                    Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
                    InterlockedCompareExchange(&Vcb->FailedAllocationError,
                                                STATUS_FILE_CORRUPT_ERROR, STATUS_SUCCESS);
                }
            }
        }
        MyFreePool__(Claim);
    }
    *Claims = NULL;
    return Status;
}

NTSTATUS
UDFPublishPrivateExtent(PVCB Vcb, PUDF_PRIVATE_ALLOCATION Claims)
{
    PUDF_PRIVATE_ALLOCATION Claim;
    NTSTATUS Status;
    for (Claim = Claims; Claim; Claim = Claim->Next) {
        Status = UDFPublishAllocationLease(Vcb, Claim->Lease);
        if (!NT_SUCCESS(Status)) return Status;
    }
    return STATUS_SUCCESS;
}

/* Respect the allocator's per-attempt extent-length limit without imposing
 * that limit on an entire file/AED work allocation. No borrowed lease map is
 * ever passed to a function which can resize/record/free a work map. */
NTSTATUS
UDFPreparePrivateExtent(PIRP_CONTEXT IrpContext, PVCB Vcb, uint32 PartRef,
    int64 Length, uint8 Flags, PEXTENT_INFO Extent, PUDF_PRIVATE_ALLOCATION* Claims)
{
    EXTENT_INFO Candidate, Borrowed;
    PUDF_PRIVATE_ALLOCATION Pending = NULL, Claim;
    uint64 Cover, Done = 0;
    uint32 Need, Maximum;
    NTSTATUS Status = STATUS_SUCCESS;

    if (!Extent || !Claims) return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(Extent, sizeof(*Extent));
    *Claims = NULL;
    RtlZeroMemory(&Candidate, sizeof(Candidate));
    if (!IrpContext || !Vcb || !Vcb->Partitions || PartRef >= Vcb->PartitionMaps ||
        Length <= 0 || !Vcb->SectorSize || Vcb->SectorShift >= 30 ||
        Vcb->SectorSize != ((uint32)1 << Vcb->SectorShift) ||
        (uint64)Length > (uint64)MAXLONGLONG - (Vcb->SectorSize - 1))
        return STATUS_INVALID_PARAMETER;
    Maximum = ALIGN_DOWN_BY(UDF_EXTENT_LENGTH_MASK, Vcb->SectorSize);
    Cover = ((uint64)Length + Vcb->SectorSize - 1) & ~((uint64)Vcb->SectorSize - 1);
    _SEH2_TRY {
    _SEH2_TRY {
        Candidate.Mapping = (PEXTENT_MAP)MyAllocatePoolTag__(NonPagedPool,
                                                         sizeof(EXTENT_MAP), MEM_EXTMAP_TAG);
        if (!Candidate.Mapping) { Status = STATUS_INSUFFICIENT_RESOURCES; goto private_exit; }
        RtlZeroMemory(Candidate.Mapping, sizeof(EXTENT_MAP));
        while (Done < Cover) {
            Claim = (PUDF_PRIVATE_ALLOCATION)MyAllocatePool__(NonPagedPool, sizeof(*Claim));
            if (!Claim) { Status = STATUS_INSUFFICIENT_RESOURCES; goto private_exit; }
            RtlZeroMemory(Claim, sizeof(*Claim));
            Claim->Next = Pending;
            Pending = Claim;
            Need = (uint32)min(Cover - Done, (uint64)Maximum);
            RtlZeroMemory(&Borrowed, sizeof(Borrowed));
            Status = UDFAllocExtentLease(IrpContext, Vcb, PartRef, Need,
                UDFPartStart(Vcb, PartRef), UDFPartEnd(Vcb, PartRef), &Borrowed, Flags, &Claim->Lease);
            if (!NT_SUCCESS(Status)) goto private_exit;
            if (!Claim->Lease || !Borrowed.Mapping ||
                (uint64)UDFGetExtentLength(Borrowed.Mapping) != Need) {
                Status = STATUS_FILE_CORRUPT_ERROR;
                goto private_exit;
            }
            Status = UDFAppendMapping(&Candidate.Mapping, Borrowed.Mapping);
            if (!NT_SUCCESS(Status)) goto private_exit;
            Done += Need;
        }
        Candidate.Length = Length;
        Candidate.Modified = TRUE;
        Candidate.Flags = Flags;
        *Extent = Candidate;
        Candidate.Mapping = NULL;
        *Claims = Pending;
        Pending = NULL;
private_exit:
        NOTHING;
    } _SEH2_EXCEPT(FsRtlIsNtstatusExpected(_SEH2_GetExceptionCode()) ?
                  EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        Status = _SEH2_GetExceptionCode();
    } _SEH2_END;
    } _SEH2_FINALLY {
        if (_SEH2_AbnormalTermination()) Status = STATUS_FILE_CORRUPT_ERROR;
        Status = UDFCompletePrivateExtent(Vcb, &Pending, FALSE, Status);
        if (Candidate.Mapping) MyFreePool__(Candidate.Mapping);
    } _SEH2_END;
    return Status;
}

/* Counted private maps are published only after the entire AD chain is valid.
 * Growth never consumes the old allocation on failure. There is no recursive
 * AED call and no merge into the caller's live AllocLoc during parsing. */
typedef struct _UDF_AD_BUILD {
    PEXTENT_MAP Map;
    uint32 Count, Capacity;
} UDF_AD_BUILD;

typedef struct _UDF_AED_KEY {
    uint32 Lbn;
    uint16 Partition;
} UDF_AED_KEY;

static NTSTATUS
UDFAppendParsedAd(UDF_AD_BUILD* Build, uint32 Location, uint32 Length)
{
    if (Build->Count == Build->Capacity) {
        uint32 Limit = MAXULONG / sizeof(EXTENT_MAP) - 1;
        uint32 Capacity;
        PEXTENT_MAP Map;
        if (Build->Count >= Limit)
            return STATUS_NOT_SUPPORTED;
        Capacity = Build->Capacity ?
            (Build->Capacity > Limit / 2 ? Limit : Build->Capacity * 2) : 8;
        Map = (PEXTENT_MAP)MyAllocatePoolTag__(NonPagedPool,
                                  (Capacity + 1) * sizeof(*Map), MEM_EXTMAP_TAG);
        if (!Map)
            return STATUS_INSUFFICIENT_RESOURCES;
        if (Build->Count)
            RtlCopyMemory(Map, Build->Map, Build->Count * sizeof(*Map));
        if (Build->Map) MyFreePool__(Build->Map);
        Build->Map = Map;
        Build->Capacity = Capacity;
    }
    Build->Map[Build->Count].extLocation = Location;
    Build->Map[Build->Count++].extLength = Length;
    Build->Map[Build->Count].extLocation = 0;
    Build->Map[Build->Count].extLength = 0;
    return STATUS_SUCCESS;
}

/* Keep the descriptor's partition identity until its range has been checked.
 * Metadata extents are split by that identity, never guessed by searching for
 * a coincident numeric physical address after flattening mixed long_ads.
 * Virtual descriptor slots resolve once in UDFPartLbaToPhys; the resulting
 * flat map is physical and lower I/O never reapplies VAT by numeric range. */
static NTSTATUS
UDFAppendAdRange(PVCB Vcb, UDF_AD_BUILD* Build, lb_addr Address,
                  uint32 Length, uint32 Type, BOOLEAN Descriptor)
{
    uint64 Rounded = ((uint64)Length + Vcb->SectorSize - 1) &
                     ~((uint64)Vcb->SectorSize - 1);
    uint32 Remaining, Part = Address.partitionReferenceNum;
    uint32 MaxRun = UDF_EXTENT_LENGTH_MASK & ~(Vcb->SectorSize - 1);
    NTSTATUS Status;
    BOOLEAN Virtual;
    if (!Rounded || Rounded > MAXULONG || !MaxRun)
        return STATUS_FILE_CORRUPT_ERROR;
    /* A final max-length AD can round to 1 GiB, which needs two internal
     * runs; do not turn its length bit into an allocation-type flag. */
    if (Type == EXTENT_NOT_RECORDED_NOT_ALLOCATED) {
        Remaining = (uint32)Rounded;
        while (Remaining) {
            uint32 Piece = min(Remaining, MaxRun);
            Status = UDFAppendParsedAd(Build, 0, Piece | (Type << 30));
            if (!NT_SUCCESS(Status)) return Status;
            Remaining -= Piece;
        }
        return STATUS_SUCCESS;
    }
    if (!Vcb->Partitions || Part >= Vcb->PartitionMaps)
        return STATUS_FILE_CORRUPT_ERROR;
    Virtual = Vcb->Partitions[Part].PartitionType == UDF_VIRTUAL_MAP15 ||
              Vcb->Partitions[Part].PartitionType == UDF_VIRTUAL_MAP20;
    /* UDF virtual extents identify descriptors, not file/directory data.
     * Virtual size is the VAT count, not the physical partition length. */
    if (Virtual) {
        if (!Descriptor || Rounded > Vcb->SectorSize)
            return STATUS_FILE_CORRUPT_ERROR;
    } else if (Address.logicalBlockNum >= Vcb->Partitions[Part].PartitionLen ||
        (Rounded >> Vcb->SectorShift) > Vcb->Partitions[Part].PartitionLen - Address.logicalBlockNum) {
        return STATUS_FILE_CORRUPT_ERROR;
    }
    Remaining = (uint32)Rounded;
    while (Remaining) {
        uint32 Location, Piece = min(Remaining, MaxRun);
        if (Vcb->MetadataFileInfo && Part == Vcb->MetadataRef) {
            SIZE_T Available = 0;
            Location = UDFMetadataLbaToPhys(Vcb, Address.logicalBlockNum, &Available);
            if (!Available || (Available & (Vcb->SectorSize - 1)))
                return STATUS_FILE_CORRUPT_ERROR;
            Piece = (uint32)min((uint64)Piece, (uint64)Available);
        } else {
            if (!Virtual && (uint64)Vcb->Partitions[Part].PartitionRoot + Address.logicalBlockNum >=
                LBA_OUT_OF_EXTENT)
                return STATUS_NOT_SUPPORTED;
            Location = UDFPartLbaToPhys(Vcb, &Address);
        }
        if (Location == LBA_OUT_OF_EXTENT || Location > Vcb->SessionEndLba ||
            (Piece >> Vcb->SectorShift) > (uint64)Vcb->SessionEndLba - Location + 1)
            return STATUS_FILE_CORRUPT_ERROR;
        Status = UDFAppendParsedAd(Build, Location, Piece | (Type << 30));
        if (!NT_SUCCESS(Status))
            return Status;
        Remaining -= Piece;
        if (Remaining)
            Address.logicalBlockNum += Piece >> Vcb->SectorShift;
    }
    return STATUS_SUCCESS;
}

/* FE/EFE buffers are one logical block in this caller profile. No I/O, caller
 * mapping publication or descriptor-sized allocation precedes these bounds.
 * A legal AED fits one logical block (UDF 2.01/2.50 section 2.3.11). */
NTSTATUS
UDFReadMappingFromXEntry(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN const LONG_AD* FileIcb,
    IN const tag* XEntry,
    IN OUT PEXTENT_INFO DataInfo,
    IN OUT PEXTENT_INFO AllocLoc
    )
{
    UDF_AD_BUILD Data = {0}, Alloc = {0};
    UDF_AED_KEY* Seen = NULL;
    PUCHAR Sector = NULL;
    const uint8* Descs;
    uint32 Offset, Length, InitialLength, Mode, Part, Width, Depth = 0;
    uint64 Information, LogicalBytes = 0;
    NTSTATUS Status = STATUS_FILE_CORRUPT_ERROR;
    BOOLEAN Embedded;
    if (!FileIcb || !XEntry || !DataInfo || !AllocLoc || DataInfo == AllocLoc ||
        DataInfo->Mapping || !Vcb->Partitions ||
        FileIcb->extLocation.partitionReferenceNum >= Vcb->PartitionMaps)
        return STATUS_INVALID_PARAMETER;
    if (Vcb->SectorSize < 512 || Vcb->SectorShift >= 30 ||
        Vcb->SectorSize != (1UL << Vcb->SectorShift) ||
        Vcb->SectorSize > 0x10000)
        return STATUS_NOT_SUPPORTED;
    if (XEntry->tagIdent == TID_FILE_ENTRY) {
        const FILE_ENTRY* Entry = (const FILE_ENTRY*)XEntry;
        if (Vcb->SectorSize < sizeof(*Entry) ||
            Entry->lengthExtendedAttr > Vcb->SectorSize - sizeof(*Entry))
            return STATUS_FILE_CORRUPT_ERROR;
        Offset = sizeof(*Entry) + Entry->lengthExtendedAttr;
        Length = Entry->lengthAllocDescs;
        Information = Entry->informationLength;
        Mode = Entry->icbTag.flags & ICB_FLAG_ALLOC_MASK;
    } else if (XEntry->tagIdent == TID_EXTENDED_FILE_ENTRY) {
        const EXTENDED_FILE_ENTRY* Entry = (const EXTENDED_FILE_ENTRY*)XEntry;
        if (Vcb->SectorSize < sizeof(*Entry) ||
            Entry->lengthExtendedAttr > Vcb->SectorSize - sizeof(*Entry))
            return STATUS_FILE_CORRUPT_ERROR;
        Offset = sizeof(*Entry) + Entry->lengthExtendedAttr;
        Length = Entry->lengthAllocDescs;
        Information = Entry->informationLength;
        Mode = Entry->icbTag.flags & ICB_FLAG_ALLOC_MASK;
    } else {
        return STATUS_FILE_CORRUPT_ERROR;
    }
    if (Length > Vcb->SectorSize - Offset || (Information >> 63))
        return STATUS_FILE_CORRUPT_ERROR;
    Embedded = Mode == ICB_FLAG_AD_IN_ICB;
    if (!Embedded && Mode != ICB_FLAG_AD_SHORT && Mode != ICB_FLAG_AD_LONG)
        return STATUS_NOT_SUPPORTED; /* no fallthrough treating unknown mode as embedded */
    if (Embedded && Information > Length)
        return STATUS_FILE_CORRUPT_ERROR;
    Part = FileIcb->extLocation.partitionReferenceNum;
    Width = Mode == ICB_FLAG_AD_SHORT ? sizeof(SHORT_AD) : sizeof(LONG_AD);
    InitialLength = Length;
    Descs = (const uint8*)XEntry + Offset;
    _SEH2_TRY {
        Status = UDFAppendAdRange(Vcb, Embedded ? &Data : &Alloc,
                         FileIcb->extLocation, Vcb->SectorSize, EXTENT_RECORDED_ALLOCATED, TRUE);
        if (!NT_SUCCESS(Status))
            _SEH2_LEAVE;
        if (!Embedded) {
            BOOLEAN More;
            do {
                uint32 i, Count;
                More = FALSE;
                if (Length % Width) {
                    Status = STATUS_FILE_CORRUPT_ERROR;
                    _SEH2_LEAVE;
                }
                Count = Length / Width;
                for (i = 0; i < Count; i++) {
                    lb_addr Address;
                    uint32 ExtLength = UdfDescriptorRead32(Descs + i * Width);
                    uint32 Type = ExtLength >> 30;
                    uint32 Bytes = ExtLength & UDF_EXTENT_LENGTH_MASK;
                    if (Mode == ICB_FLAG_AD_SHORT) {
                        Address.partitionReferenceNum = (uint16)Part;
                        Address.logicalBlockNum = UdfDescriptorRead32(Descs + i * Width + 4);
                    } else {
                        Address.logicalBlockNum = UdfDescriptorRead32(Descs + i * Width + 4);
                        Address.partitionReferenceNum = (uint16)UdfDescriptorRead16(Descs + i * Width + 8);
                    }
                    if (!Bytes) {
                        if (Type) {
                            Status = STATUS_FILE_CORRUPT_ERROR;
                            _SEH2_LEAVE;
                        }
                        break; /* legacy zero terminator: its location is not dereferenced */
                    }
                    if (Type == EXTENT_NEXT_EXTENT_ALLOCDESC) {
                        uint32 j, Location;
                        uint16 Ident;
                        PALLOC_EXT_DESC Next;
                        if (i + 1 != Count || Bytes < sizeof(ALLOC_EXT_DESC) ||
                            Bytes > Vcb->SectorSize || Address.partitionReferenceNum >= Vcb->PartitionMaps ||
                            ((Vcb->Partitions[Address.partitionReferenceNum].PartitionType != UDF_VIRTUAL_MAP15 &&
                              Vcb->Partitions[Address.partitionReferenceNum].PartitionType != UDF_VIRTUAL_MAP20) &&
                             Address.logicalBlockNum >= Vcb->Partitions[Address.partitionReferenceNum].PartitionLen)) {
                            Status = STATUS_FILE_CORRUPT_ERROR;
                            _SEH2_LEAVE;
                        }
                        if (Depth >= ALLOC_DESC_MAX_RECURSE) {
                            Status = STATUS_NOT_SUPPORTED;
                            _SEH2_LEAVE;
                        }
                        if (!Seen) {
                            Seen = (UDF_AED_KEY*)MyAllocatePoolTag__(NonPagedPool,
                                      ALLOC_DESC_MAX_RECURSE * sizeof(*Seen), MEM_ALLOCDESC_TAG);
                            Sector = (PUCHAR)MyAllocatePoolTag__(NonPagedPool,
                                                        Vcb->SectorSize, MEM_ALLOCDESC_TAG);
                            if (!Seen || !Sector) {
                                Status = STATUS_INSUFFICIENT_RESOURCES;
                                _SEH2_LEAVE;
                            }
                        }
                        for (j = 0; j < Depth; j++) {
                            if (Seen[j].Partition == Address.partitionReferenceNum &&
                                Seen[j].Lbn == Address.logicalBlockNum) {
                                Status = STATUS_FILE_CORRUPT_ERROR;
                                _SEH2_LEAVE;
                            }
                        }
                        Seen[Depth].Partition = Address.partitionReferenceNum;
                        Seen[Depth++].Lbn = Address.logicalBlockNum;
                        if ((Vcb->Partitions[Address.partitionReferenceNum].PartitionType == UDF_TYPE1_MAP15 ||
                             Vcb->Partitions[Address.partitionReferenceNum].PartitionType == UDF_SPARABLE_MAP15) &&
                            (uint64)Vcb->Partitions[Address.partitionReferenceNum].PartitionRoot +
                                Address.logicalBlockNum >= LBA_OUT_OF_EXTENT) {
                            Status = STATUS_NOT_SUPPORTED;
                            _SEH2_LEAVE;
                        }
                        Location = UDFPartLbaToPhys(Vcb, &Address);
                        Status = UDFReadTagged(IrpContext, Vcb, (int8*)Sector,
                                                Location, Address.logicalBlockNum, &Ident);
                        if (Status != STATUS_SUCCESS) {
                            if (NT_SUCCESS(Status)) Status = STATUS_IO_DEVICE_ERROR;
                            _SEH2_LEAVE;
                        }
                        Next = (PALLOC_EXT_DESC)Sector;
                        if (Ident != TID_ALLOC_EXTENT_DESC ||
                            Next->lengthAllocDescs > Bytes - sizeof(*Next) ||
                            Next->lengthAllocDescs % Width) {
                            Status = STATUS_FILE_CORRUPT_ERROR;
                            _SEH2_LEAVE;
                        }
                        /* UDFReadTagged authenticates full returned bytes/tag/CRC,
                         * including the allowed header-only AED CRC dialect. */
                        Status = UDFAppendAdRange(Vcb, &Alloc, Address,
                                                 Vcb->SectorSize, EXTENT_RECORDED_ALLOCATED, TRUE);
                        if (!NT_SUCCESS(Status))
                            _SEH2_LEAVE;
                        Descs = (const uint8*)(Next + 1);
                        Length = Next->lengthAllocDescs;
                        More = TRUE;
                        break;
                    }
                    /* Short/long AD information is its declared byte length,
                     * not sector padding. After the file body only aligned,
                     * allocated/unrecorded file-tail extents are permitted. */
                    if ((LogicalBytes >= Information &&
                         (Type != EXTENT_NOT_RECORDED_ALLOCATED || (Bytes & (Vcb->SectorSize - 1)))) ||
                        (LogicalBytes < Information && Information - LogicalBytes > Bytes &&
                         (Bytes & (Vcb->SectorSize - 1)))) {
                        Status = STATUS_FILE_CORRUPT_ERROR;
                        _SEH2_LEAVE;
                    }
                    Status = UDFAppendAdRange(Vcb, &Data, Address, Bytes, Type, FALSE);
                    if (!NT_SUCCESS(Status))
                        _SEH2_LEAVE;
                    LogicalBytes += Bytes;
                }
            } while (More);
            if (Information > LogicalBytes) {
                Status = STATUS_FILE_CORRUPT_ERROR;
                _SEH2_LEAVE;
            }
            if (!Data.Map) {
                Data.Map = (PEXTENT_MAP)MyAllocatePoolTag__(NonPagedPool,
                                                   sizeof(EXTENT_MAP), MEM_EXTMAP_TAG);
                if (!Data.Map) {
                    Status = STATUS_INSUFFICIENT_RESOURCES;
                    _SEH2_LEAVE;
                }
                RtlZeroMemory(Data.Map, sizeof(EXTENT_MAP));
            }
        }
        /* Only this final section transfers ownership. A read/CRC/cycle/OOM
         * refusal above leaves the caller's old mapping and offsets intact. */
        if (AllocLoc->Mapping) MyFreePool__(AllocLoc->Mapping);
        AllocLoc->Mapping = Alloc.Map;
        Alloc.Map = NULL;
        AllocLoc->Offset = Embedded ? 0 : Offset;
        AllocLoc->Length = Embedded ? 0 : InitialLength;
        DataInfo->Mapping = Data.Map;
        Data.Map = NULL;
        DataInfo->Offset = Embedded ? Offset : 0;
        DataInfo->Length = (int64)Information;
        DataInfo->Modified = FALSE;
        Status = STATUS_SUCCESS;
    } _SEH2_FINALLY {
        if (Data.Map) MyFreePool__(Data.Map);
        if (Alloc.Map) MyFreePool__(Alloc.Map);
        if (Sector) MyFreePool__(Sector);
        if (Seen) MyFreePool__(Seen);
    } _SEH2_END;
    return Status;
} // end UDFReadMappingFromXEntry()


/*
    This routine builds data for AllocDesc sequence for specified
    extent
 */
/* Encode a data run or chain link while its address domain is explicit.
 * Sparse positions are zero, never a translation of placeholder PSN zero. */
static NTSTATUS
UDFStoreAllocationDescriptor(PVCB Vcb, uint32 PartNum, BOOLEAN LongAds,
                              uint32 Length, uint32 Location, int8* Destination)
{
    uint32 Type = Length >> 30, Bytes = Length & UDF_EXTENT_LENGTH_MASK;
    uint32 Reference = PartNum, Lbn = 0;
    uint64 Blocks, End;
    if (!Bytes || PartNum >= Vcb->PartitionMaps || PartNum > MAXUSHORT)
        return STATUS_FILE_CORRUPT_ERROR;
    if (Type != EXTENT_NOT_RECORDED_NOT_ALLOCATED) {
        Blocks = ((uint64)Bytes + Vcb->SectorSize - 1) >> Vcb->SectorShift;
        End = (uint64)Location + Blocks;
        if (!Blocks || End > LBA_OUT_OF_EXTENT)
            return STATUS_FILE_CORRUPT_ERROR;
        if (LongAds && Type != EXTENT_NEXT_EXTENT_ALLOCDESC)
            Reference = UDFGetRefPartNumByPhysLba(Vcb, Location);
        if (Reference >= Vcb->PartitionMaps || Reference > MAXUSHORT)
            return STATUS_FILE_CORRUPT_ERROR;
        Lbn = UDFPhysLbaToPart(Vcb, Reference, Location);
        if (Lbn == LBA_OUT_OF_EXTENT ||
            (uint64)Lbn + Blocks > Vcb->Partitions[Reference].PartitionLen ||
            UDFPhysLbaToPart(Vcb, Reference, (uint32)(End - 1)) != (uint64)Lbn + Blocks - 1)
            return STATUS_FILE_CORRUPT_ERROR;
    }
    if (LongAds) {
        PLONG_AD Ad = (PLONG_AD)Destination;
        RtlZeroMemory(Ad, sizeof(*Ad));
        Ad->extLength = Length;
        Ad->extLocation.logicalBlockNum = Lbn;
        Ad->extLocation.partitionReferenceNum = (uint16)Reference;
    } else {
        PSHORT_AD Ad = (PSHORT_AD)Destination;
        Ad->extLength = Length;
        Ad->extPosition = Lbn;
    }
    return STATUS_SUCCESS;
}

/* One counted layout for short and long ADs. All sizes are planned wide
 * before narrowing; every emitted block has a proved buffer window. */
static BOOLEAN
UDFRetirementOverlapsMap(PVCB Vcb, PEXTENT_MAP Map, uint32 Lba, uint32 Blocks);

/* Ordinary lookup/IO shares the canonical Main domain; a new cold parser
 * owns UNDER_INIT until the complete FE/AED mapping exists. Metadata growth
 * has no ordinary FCB, but its reserved FIs are owned by BitMapResource1. */
static BOOLEAN
UDFAllocationDescriptorWriterHeld(PVCB Vcb, PUDF_FILE_INFO FileInfo)
{
    PUDF_DATALOC_INFO Dloc = FileInfo->Dloc;
    PFCB Fcb = Dloc->CommonFcb;
    if (Fcb) {
        /* Paging reads also hold Main shared, but need not take VCB. A
         * volume-exclusive caller still needs the canonical Main writer. */
        return Fcb->FcbNonpaged && Fcb->FileInfo && Fcb->FileInfo->Dloc == Dloc &&
            ExIsResourceAcquiredExclusiveLite(&Fcb->FcbNonpaged->FcbResource);
    }
    if (FileInfo->Fcb) return FALSE;
    if (ExIsResourceAcquiredExclusiveLite(&Vcb->VcbResource)) return TRUE;
    if (FileInfo->Constructing && FileInfo->DlocInitOwner &&
        (Dloc->FE_Flags & UDF_FE_FLAG_UNDER_INIT)) return TRUE;
    return !Fcb && (FileInfo == Vcb->MetadataFileInfo ||
                   FileInfo == Vcb->MetadataMirrorFileInfo || FileInfo == Vcb->MetadataBitmapFileInfo) &&
        ExIsResourceAcquiredExclusiveLite(&Vcb->BitMapResource1);
}

static NTSTATUS
UDFBuildAllocationDescriptorSequence(PIRP_CONTEXT IrpContext, PVCB Vcb,
    uint32 PartNum, BOOLEAN LongAds, uint32 InitSz, PUDF_FILE_INFO FileInfo,
    int8** Buffer, uint32* Capacity)
{
    PUDF_DATALOC_INFO Dloc;
    PEXTENT_MAP Mapping, PreviousMap = NULL;
    PEXTENT_INFO Alloc;
    EXTENT_INFO Candidate, OldPrefix, Tail;
    PUDF_EXTENT_RETIREMENT Retired = NULL, Prior;
    PUDF_PRIVATE_ALLOCATION Claims = NULL, Claim;
    int8* Flat = NULL;
    int8* Encoded = NULL;
    uint32 MapBytes, Count, Entries = 0, i, LastRelevant = MAXULONG;
    uint32 Width = LongAds ? sizeof(LONG_AD) : sizeof(SHORT_AD);
    uint32 FirstSlots, AedSlots, Remaining, Take, Source = 0, Slots, Payload, Offset;
    uint32 RawLength, Type, EncodedLength, LinkLba, SectorOffset, LinkLocation;
    uint32 CandidateBytes, Blocks, j;
    LONG Pending;
    uint64 Total = 0, Position = 0, FlatBytes, EncodedBytes;
    uint64 Information, NextOffset, Cover, TailBytes;
    SIZE_T Available;
    BOOLEAN First = TRUE, More, Installed = FALSE;
    PALLOC_EXT_DESC Aed;
    NTSTATUS Status, ReleaseStatus;

    if (!Buffer || !Capacity) return STATUS_INVALID_PARAMETER;
    *Buffer = NULL;
    *Capacity = 0;
    RtlZeroMemory(&Candidate, sizeof(Candidate));
    RtlZeroMemory(&OldPrefix, sizeof(OldPrefix));
    RtlZeroMemory(&Tail, sizeof(Tail));
    if (!Vcb || !FileInfo || !(Dloc = FileInfo->Dloc) || !Dloc->FileEntry ||
        Dloc->FileEntryCapacity < Vcb->SectorSize || !Dloc->FELoc.Mapping ||
        !Dloc->DataLoc.Mapping || !Vcb->Partitions || PartNum >= Vcb->PartitionMaps ||
        !Vcb->SectorSize || Vcb->SectorShift >= 30 ||
        Vcb->SectorSize != ((uint32)1 << Vcb->SectorShift) || Vcb->SectorSize > MAXUSHORT ||
        Dloc->FileEntryLen > Vcb->SectorSize || InitSz != Vcb->SectorSize - Dloc->FileEntryLen ||
        Dloc->DataLoc.Offset || Dloc->DataLoc.Length < 0 ||
        Vcb->SectorSize < sizeof(ALLOC_EXT_DESC) + 2 * Width)
        return STATUS_FILE_CORRUPT_ERROR;
    if (Vcb->Vat || Vcb->CDR_Mode || (Vcb->VcbState & VCB_STATE_VOLUME_READ_ONLY))
        return STATUS_MEDIA_WRITE_PROTECTED;
    Status = UDFValidateFileEntryBuffer(Vcb, FileInfo, &RawLength);
    if (!NT_SUCCESS(Status)) return Status;
    if (!UDFAllocationDescriptorWriterHeld(Vcb, FileInfo))
        return STATUS_INVALID_DEVICE_STATE;
    Mapping = Dloc->DataLoc.Mapping;
    Alloc = &Dloc->AllocLoc;
    if (Dloc->FELoc.Offset ||
        (Dloc->FELoc.Mapping[0].extLength & UDF_EXTENT_LENGTH_MASK) != Vcb->SectorSize ||
        (Dloc->FELoc.Mapping[0].extLength >> 30) > EXTENT_NOT_RECORDED_ALLOCATED ||
        Dloc->FELoc.Mapping[1].extLength)
        return STATUS_FILE_CORRUPT_ERROR;
    Information = (uint64)Dloc->DataLoc.Length;
    Status = UDFGetMappingByteCount(Mapping, &MapBytes);
    if (!NT_SUCCESS(Status)) return Status;
    Count = MapBytes / sizeof(EXTENT_MAP) - 1;
    for (i = 0; i < Count; i++) {
        RawLength = Mapping[i].extLength & UDF_EXTENT_LENGTH_MASK;
        Type = Mapping[i].extLength >> 30;
        if (Total < Information || Type != EXTENT_NOT_RECORDED_NOT_ALLOCATED)
            LastRelevant = i;
        if (Total >= Information && Type != EXTENT_NOT_RECORDED_NOT_ALLOCATED &&
            (Type != EXTENT_NOT_RECORDED_ALLOCATED || (RawLength & (Vcb->SectorSize - 1))))
            return STATUS_NOT_SUPPORTED;
        if (Total < Information && Information - Total > RawLength &&
            (RawLength & (Vcb->SectorSize - 1)))
            return STATUS_FILE_CORRUPT_ERROR;
        Total += RawLength;
        if (Total > MAXLONGLONG) return STATUS_FILE_CORRUPT_ERROR;
    }
    if (Information > Total) return STATUS_FILE_CORRUPT_ERROR;
    FlatBytes = max((uint64)1, (uint64)Count * Width);
    if (FlatBytes > MAXULONG - MY_HEAP_ALIGN) return STATUS_INSUFFICIENT_RESOURCES;
    _SEH2_TRY {
    _SEH2_TRY {
        Flat = (int8*)MyAllocatePoolTag__(NonPagedPool, FlatBytes, MEM_SHAD_TAG);
        if (!Flat) { Status = STATUS_INSUFFICIENT_RESOURCES; goto sequence_exit; }
        for (i = 0; i < Count; i++) {
            RawLength = Mapping[i].extLength & UDF_EXTENT_LENGTH_MASK;
            Type = Mapping[i].extLength >> 30;
            if (Position >= Information && Type == EXTENT_NOT_RECORDED_NOT_ALLOCATED) {
                Position += RawLength; /* unused holes own no physical allocation */
                continue;
            }
            EncodedLength = RawLength;
            if (i == LastRelevant && Position < Information && Information - Position <= RawLength)
                EncodedLength -= (uint32)((Position + RawLength - Information) & (Vcb->SectorSize - 1));
            if (!EncodedLength) { Status = STATUS_FILE_CORRUPT_ERROR; goto sequence_exit; }
            Status = UDFStoreAllocationDescriptor(Vcb, PartNum, LongAds,
                EncodedLength | (Type << 30), Mapping[i].extLocation, Flat + (SIZE_T)Entries * Width);
            if (!NT_SUCCESS(Status)) goto sequence_exit;
            Entries++;
            Position += RawLength;
        }
        FirstSlots = InitSz / Width;
        AedSlots = (Vcb->SectorSize - sizeof(ALLOC_EXT_DESC)) / Width;
        if (Entries <= FirstSlots) {
            EncodedBytes = (uint64)Entries * Width;
        } else {
#ifndef UDF_ALLOW_FRAG_AD
            Status = STATUS_NOT_SUPPORTED;
            goto sequence_exit;
#else
            uint64 AedBlocks = 1, LastEntries;
            if (!FirstSlots || AedSlots < 2) { Status = STATUS_NOT_SUPPORTED; goto sequence_exit; }
            Remaining = Entries - (FirstSlots - 1);
            if (Remaining > AedSlots)
                AedBlocks += ((uint64)Remaining - AedSlots + AedSlots - 2) / (AedSlots - 1);
            if (AedBlocks > ALLOC_DESC_MAX_RECURSE) {
                Status = STATUS_NOT_SUPPORTED; /* same chain-depth profile as the reader */
                goto sequence_exit;
            }
            LastEntries = Remaining - (AedBlocks - 1) * (AedSlots - 1);
            EncodedBytes = (uint64)InitSz + (AedBlocks - 1) * Vcb->SectorSize +
                           sizeof(ALLOC_EXT_DESC) + LastEntries * Width;
#endif
        }
        if (EncodedBytes > MAXULONG - MY_HEAP_ALIGN ||
            EncodedBytes > (uint64)MAXLONGLONG - Dloc->FileEntryLen) {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto sequence_exit;
        }
        Encoded = (int8*)MyAllocatePoolTag__(NonPagedPool, max((uint64)1, EncodedBytes), MEM_SHAD_TAG);
        if (!Encoded) { Status = STATUS_INSUFFICIENT_RESOURCES; goto sequence_exit; }
        RtlZeroMemory(Encoded, max((uint64)1, EncodedBytes));
        /* Never resize/reuse an AED still reachable from the old FE.
         * Plan the old exact tail retirement without freeing it, and build
         * an independent replacement chain rooted at the same FE sector. */
        if (Alloc->Mapping) {
            if (Alloc->Mapping == Dloc->DataLoc.Mapping || Alloc->Mapping == Dloc->FELoc.Mapping ||
                Alloc->Offset != Dloc->FileEntryLen ||
                Alloc->Mapping[0].extLocation != Dloc->FELoc.Mapping[0].extLocation ||
                (Alloc->Mapping[0].extLength & UDF_EXTENT_LENGTH_MASK) < Vcb->SectorSize ||
                (Alloc->Mapping[0].extLength >> 30) > EXTENT_NOT_RECORDED_ALLOCATED) {
                Status = STATUS_FILE_CORRUPT_ERROR;
                goto sequence_exit;
            }
            Status = UDFPrepareExtentShrink(Vcb, PartNum, 0, TRUE,
                                              Alloc, &OldPrefix, &Retired);
            if (!NT_SUCCESS(Status)) goto sequence_exit;
        } else if (Alloc->Offset || Alloc->Length) {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto sequence_exit;
        }
        Candidate.Mapping = (PEXTENT_MAP)MyAllocatePoolTag__(NonPagedPool,
                                                          2 * sizeof(EXTENT_MAP), MEM_EXTMAP_TAG);
        if (!Candidate.Mapping) { Status = STATUS_INSUFFICIENT_RESOURCES; goto sequence_exit; }
        RtlZeroMemory(Candidate.Mapping, 2 * sizeof(EXTENT_MAP));
        Candidate.Mapping[0] = Dloc->FELoc.Mapping[0];
        Candidate.Offset = Dloc->FileEntryLen;
        Candidate.Length = (int64)EncodedBytes;
        Candidate.Modified = TRUE;
        TailBytes = (((uint64)Candidate.Offset + EncodedBytes + Vcb->SectorSize - 1) &
                      ~((uint64)Vcb->SectorSize - 1)) - Vcb->SectorSize;
        if (TailBytes) {
            Status = UDFPreparePrivateExtent(IrpContext, Vcb, PartNum, (int64)TailBytes,
                                              EXTENT_FLAG_ALLOC_STD, &Tail, &Claims);
            if (!NT_SUCCESS(Status)) goto sequence_exit;
            Status = UDFAppendMapping(&Candidate.Mapping, Tail.Mapping);
            if (!NT_SUCCESS(Status)) goto sequence_exit;
        }
        Remaining = Entries;
        Slots = FirstSlots;
        Offset = 0;
        while (TRUE) {
            More = Remaining > Slots;
            Take = More ? Slots - 1 : Remaining;
            Payload = (Take + (More ? 1 : 0)) * Width;
            Cover = (uint64)Offset + (First ? 0 : sizeof(ALLOC_EXT_DESC)) + Payload;
            if (Cover > EncodedBytes) { Status = STATUS_FILE_CORRUPT_ERROR; goto sequence_exit; }
            if (!First) {
                Aed = (PALLOC_EXT_DESC)(Encoded + Offset);
                Aed->descTag.tagIdent = TID_ALLOC_EXTENT_DESC;
                Aed->previousAllocExtLocation = 0;
                Aed->lengthAllocDescs = Payload;
                Offset += sizeof(ALLOC_EXT_DESC);
            } else {
                Aed = NULL;
            }
            if (Take) RtlCopyMemory(Encoded + Offset, Flat + (SIZE_T)Source * Width, (SIZE_T)Take * Width);
            Source += Take;
            Remaining -= Take;
            Offset += Take * Width;
            if (More) {
                NextOffset = ((uint64)Dloc->FileEntryLen + Offset + Width + Vcb->SectorSize - 1) &
                             ~((uint64)Vcb->SectorSize - 1);
                if (NextOffset < Dloc->FileEntryLen || NextOffset - Dloc->FileEntryLen >= EncodedBytes) {
                    Status = STATUS_FILE_CORRUPT_ERROR;
                    goto sequence_exit;
                }
                LinkLba = UDFExtentOffsetToLba(Vcb, Candidate.Mapping, (int64)NextOffset,
                                               &SectorOffset, &Available, &Type, NULL);
                if (LinkLba == LBA_OUT_OF_EXTENT || SectorOffset || Available < Vcb->SectorSize ||
                    Type > EXTENT_NOT_RECORDED_ALLOCATED) {
                    Status = STATUS_FILE_CORRUPT_ERROR;
                    goto sequence_exit;
                }
                Status = UDFStoreAllocationDescriptor(Vcb, PartNum, LongAds,
                    Vcb->SectorSize | ((uint32)EXTENT_NEXT_EXTENT_ALLOCDESC << 30), LinkLba, Encoded + Offset);
                if (!NT_SUCCESS(Status)) goto sequence_exit;
            }
            if (Aed) {
                LinkLba = UDFExtentOffsetToLba(Vcb, Candidate.Mapping,
                    (int64)((uint64)Dloc->FileEntryLen + (uint64)((int8*)Aed - Encoded)),
                    &SectorOffset, &Available, &Type, NULL);
                if (LinkLba == LBA_OUT_OF_EXTENT || SectorOffset || Available < Vcb->SectorSize ||
                    Type > EXTENT_NOT_RECORDED_ALLOCATED) {
                    Status = STATUS_FILE_CORRUPT_ERROR;
                    goto sequence_exit;
                }
                LinkLocation = UDFPhysLbaToPart(Vcb, PartNum, LinkLba);
                if (LinkLocation == LBA_OUT_OF_EXTENT) {
                    Status = STATUS_FILE_CORRUPT_ERROR;
                    goto sequence_exit;
                }
                UDFSetUpTag(Vcb, &Aed->descTag,
                    (uint16)(sizeof(ALLOC_EXT_DESC) + (Vcb->UdfRevision >= 0x0201 ? Payload : 0)),
                    LinkLocation, 0);
            }
            if (!More) break;
            Offset = (uint32)(NextOffset - Dloc->FileEntryLen);
            Slots = AedSlots;
            First = FALSE;
        }
        if (Source != Entries || Offset != EncodedBytes) {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto sequence_exit;
        }
        /* The writer domain excludes old in-memory map readers; a new
         * cold parser must first own UNDER_INIT and cannot race this Dloc.
         * Check the allocator's offered tail against all still-live owners
         * before submitting any AED bytes. A conflicting bitmap claim is
         * uncertain, not a private free that may release someone else's data. */
        Status = UDFGetMappingByteCount(Candidate.Mapping, &CandidateBytes);
        if (!NT_SUCCESS(Status)) goto sequence_exit;
        UDFAcquireResourceExclusive(&Vcb->BitMapResource1, TRUE);
        _SEH2_TRY {
            if (Vcb->PendingExtentRetirements < 0 ||
                (Retired && Vcb->PendingExtentRetirements == MAXLONG)) {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                _SEH2_LEAVE;
            }
            for (i = 1; i + 1 < CandidateBytes / sizeof(EXTENT_MAP); i++) {
                uint32 Lba = Candidate.Mapping[i].extLocation;
                uint64 End;
                Blocks = (Candidate.Mapping[i].extLength & UDF_EXTENT_LENGTH_MASK) >> Vcb->SectorShift;
                End = (uint64)Lba + Blocks;
                if (!Blocks || End > LBA_OUT_OF_EXTENT ||
                    UDFRetirementOverlapsMap(Vcb, Dloc->DataLoc.Mapping, Lba, Blocks) ||
                    UDFRetirementOverlapsMap(Vcb, Dloc->AllocLoc.Mapping, Lba, Blocks) ||
                    UDFRetirementOverlapsMap(Vcb, Dloc->FELoc.Mapping, Lba, Blocks)) {
                    Status = STATUS_FILE_CORRUPT_ERROR;
                    break;
                }
                Pending = Vcb->PendingExtentRetirements;
                for (Prior = Dloc->RetiredExtents; Prior; Prior = Prior->Next) {
                    if (Pending-- <= 0 || Prior->Count > Prior->Capacity ||
                        Prior->Completed > Prior->Count) {
                        Status = STATUS_FILE_CORRUPT_ERROR;
                        break;
                    }
                    for (j = Prior->Completed; j < Prior->Count; j++) {
                        uint64 OldEnd = (uint64)Prior->Ranges[j].extLocation +
                            ((Prior->Ranges[j].extLength & UDF_EXTENT_LENGTH_MASK) >> Vcb->SectorShift);
                        if ((uint64)Lba < OldEnd && (uint64)Prior->Ranges[j].extLocation < End) {
                            Status = STATUS_FILE_CORRUPT_ERROR;
                            break;
                        }
                    }
                    if (!NT_SUCCESS(Status)) break;
                }
                if (!NT_SUCCESS(Status)) break;
            }
            if (!NT_SUCCESS(Status)) {
                for (Claim = Claims; Claim; Claim = Claim->Next)
                    if (Claim->Lease) Claim->Lease->Uncertain = TRUE;
                Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
                _SEH2_LEAVE;
            }
            Status = UDFPublishPrivateExtent(Vcb, Claims);
            if (!NT_SUCCESS(Status)) _SEH2_LEAVE;
            if (Retired) {
                Retired->Next = Dloc->RetiredExtents;
                Dloc->RetiredExtents = Retired;
                Retired = NULL;
                InterlockedIncrement(&Vcb->PendingExtentRetirements);
            }
            PreviousMap = Alloc->Mapping;
            *Alloc = Candidate;
            Candidate.Mapping = NULL;
            Dloc->FE_Flags |= UDF_FE_FLAG_FE_MODIFIED;
            Installed = TRUE;
        } _SEH2_FINALLY {
            UDFReleaseResource(&Vcb->BitMapResource1);
        } _SEH2_END;
        if (!NT_SUCCESS(Status)) goto sequence_exit;
        *Buffer = Encoded;
        Encoded = NULL;
        *Capacity = (uint32)max((uint64)1, EncodedBytes);
        Status = STATUS_SUCCESS;
sequence_exit:
        NOTHING;
    } _SEH2_EXCEPT(FsRtlIsNtstatusExpected(_SEH2_GetExceptionCode()) ?
                  EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        Status = _SEH2_GetExceptionCode();
    } _SEH2_END;
    } _SEH2_FINALLY {
        if (_SEH2_AbnormalTermination()) Status = STATUS_FILE_CORRUPT_ERROR;
        ReleaseStatus = UDFCompletePrivateExtent(Vcb, &Claims,
            Installed && !_SEH2_AbnormalTermination(), Status);
        if (NT_SUCCESS(Status) && !NT_SUCCESS(ReleaseStatus)) Status = ReleaseStatus;
        if (Flat) MyFreePool__(Flat);
        if (Encoded) MyFreePool__(Encoded);
        if (Candidate.Mapping) MyFreePool__(Candidate.Mapping);
        if (OldPrefix.Mapping) MyFreePool__(OldPrefix.Mapping);
        if (Tail.Mapping) MyFreePool__(Tail.Mapping);
        if (Retired) MyFreePool__(Retired);
        if (PreviousMap) MyFreePool__(PreviousMap);
        if (Installed && !NT_SUCCESS(Status)) {
            Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
            UDFRememberMetadataWritebackError(IrpContext, Vcb, FileInfo, Status);
        }
    } _SEH2_END;
    return Status;
}

NTSTATUS
UDFBuildShortAllocDescs(PIRP_CONTEXT IrpContext, PVCB Vcb, uint32 PartNum,
    int8** Buffer, uint32 InitSz, PUDF_FILE_INFO FileInfo, uint32* Capacity)
{
    return UDFBuildAllocationDescriptorSequence(IrpContext, Vcb, PartNum,
                                                  FALSE, InitSz, FileInfo, Buffer, Capacity);
}

NTSTATUS
UDFBuildLongAllocDescs(PIRP_CONTEXT IrpContext, PVCB Vcb, uint32 PartNum,
    int8** Buffer, uint32 InitSz, PUDF_FILE_INFO FileInfo, uint32* Capacity)
{
    return UDFBuildAllocationDescriptorSequence(IrpContext, Vcb, PartNum,
                                                  TRUE, InitSz, FileInfo, Buffer, Capacity);
} // end counted short/long AD builders

/*
    This routine builds data for AllocDesc sequence for specified
    extent
 */
/*NTSTATUS
UDFBuildExtAllocDescs(
    IN PVCB Vcb,
    IN uint32 PartNum,
    OUT int8** Buff,  // data for AllocLoc
    IN uint32 InitSz,
 IN OUT PUDF_FILE_INFO FileInfo
    )
{
    uint32 i, j;
    uint32 len=0, ac, len2;
    uint32 TagLoc, prevTagLoc;
    uint32 LBS = Vcb->LBlockSize;
    uint32 LBSh = Vcb->BlockSizeBits;
    PEXTENT_MAP Extent = FileInfo->Dloc->DataLoc.Mapping;
    PEXTENT_INFO AllocExtent = &(FileInfo->Dloc->AllocLoc);
    PEXT_AD Alloc, saved_Alloc;
    uint32 BufOffs;
    uint32 ExtOffs = AllocExtent->Offset;
    uint32 NewLen;
    NTSTATUS status;
    uint32 TagLen = 0;
    tag* Tag = NULL;

    ValidateFileInfo(FileInfo);
    // calculate length
    for(len=0; Extent[len].extLength; len++);
    Alloc = (PEXT_AD)MyAllocatePool__(NonPagedPool, (len+1)*sizeof(EXT_AD));
    if (!Alloc) return STATUS_INSUFFICIENT_RESOURCES;
    // fill contiguous AllocDesc buffer (decribing UserData)
    for(i=0;i<len;i++) {
        Alloc[i].extLength =
        Alloc[i].recordedLength =
        Alloc[i].informationLength = Extent[i].extLength;
        Alloc[i].extLocation.logicalBlockNum = UDFPhysLbaToPart(Vcb, PartNum, Extent[i].extLocation);
        Alloc[i].extLocation.partitionReferenceNum = (uint16)PartNum;
    }
    RtlZeroMemory(&(Alloc[i]), sizeof(EXT_AD));
    j = len*sizeof(EXT_AD); // required space
    len = InitSz;            // space available in 1st block

    // Ok. Let's init AllocLoc
    if (!(FileInfo->Dloc->AllocLoc.Mapping)) {
        FileInfo->Dloc->AllocLoc.Mapping = (PEXTENT_MAP)MyAllocatePool__(NonPagedPool, 2 * sizeof(EXTENT_MAP));
        if (!(FileInfo->Dloc->AllocLoc.Mapping)) {
            MyFreePool__(Alloc);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        // allocation descriptors are located in the same sector as FileEntry
        // (at least their 1st part), just after it
        FileInfo->Dloc->AllocLoc.Mapping[0] = FileInfo->Dloc->FELoc.Mapping[0];
        FileInfo->Dloc->AllocLoc.Offset = FileInfo->Dloc->FileEntryLen;
        FileInfo->Dloc->AllocLoc.Length = 0;
        // set terminator
        FileInfo->Dloc->AllocLoc.Mapping[1].extLength =
        FileInfo->Dloc->AllocLoc.Mapping[1].extLocation = 0;
    }

    if (j <= len) {
        // we needn't allocating additional blocks to store AllocDescs
        RtlCopyMemory(*Buff, (int8*)Alloc, j);
        NewLen = j;
        MyFreePool__(Alloc);
    } else {
        BufOffs = 0;
        TagLoc = prevTagLoc = 0;
        // calculate the space available for EXT_ADs in each block
        ac = (LBS - (sizeof(ALLOC_EXT_DESC) + sizeof(EXT_AD))) & ~(sizeof(EXT_AD)-1);
        // calculate actual AllocSequence length (in LBlocks)
        len -= sizeof(EXT_AD);
        NewLen = ( ((j - len + ac - 1) / ac) << LBSh) + len + sizeof(EXT_AD);
        MyFreePool__(*Buff);
        (*Buff) = (int8*)MyAllocatePool__(NonPagedPool, NewLen);
        if (UDFGetExtentLength(AllocExtent->Mapping) < NewLen) {
            status = UDFResizeExtent(Vcb, PartNum, NewLen, TRUE, AllocExtent);
            if (!NT_SUCCESS(status)) {
                MyFreePool__(Alloc);
                return status;
            }
        }
        RtlZeroMemory(*Buff, NewLen);
        NewLen = 0; // recorded length
        saved_Alloc = Alloc;
        len2 = len + sizeof(EXT_AD);
        // fill buffer sector by sector (adding links at the end of each one)
        while(TRUE) {

            // j - remained AllocDescs length (in bytes)
            // len - bytes available for in AllocDescs each block

            // leave space for terminator or pointer to next part of sequence
            if (j == len2) {
                // if we have only 1 EXT_AD that we can fit in last sector
                // we shall do it instead of recording link & allocating new block
                len =
                TagLen = len2;
            }
            RtlCopyMemory( (*Buff)+BufOffs, (int8*)Alloc, len);
            Alloc = (PEXT_AD)((int8*)Alloc + len);
            j -= len;
            BufOffs += len;
            if (Tag) {
                // Set up Tag for AllocDesc
                Tag->tagIdent = TID_ALLOC_EXTENT_DESC;
                UDFSetUpTag(Vcb, Tag, (uint16)TagLen, TagLoc);
                prevTagLoc = TagLoc;
            }
            if (!j) {
                // terminate loop
                NewLen = BufOffs;
                break;
            }
            len = ac;
            if (j <= (len + sizeof(EXT_AD)))
                len = j - sizeof(EXT_AD);
            len2 = len + sizeof(EXT_AD);
            // we have more than 1 EXT_AD that we can't fit in current block
            // so we shall set up pointer to the next block
            ((PEXT_AD)((*Buff)+BufOffs))->extLength =
            ((PEXT_AD)((*Buff)+BufOffs))->recordedLength = LBS;
            ((PEXT_AD)((*Buff)+BufOffs))->informationLength = len2 |
                (((uint32)EXTENT_NEXT_EXTENT_ALLOCDESC) << 30) ;
            ((PEXT_AD)((*Buff)+BufOffs))->extLocation.logicalBlockNum = TagLoc =
                UDFPhysLbaToPart(Vcb, PartNum,
                    UDFExtentOffsetToLba(Vcb, AllocExtent->Mapping, ExtOffs + BufOffs + 2*sizeof(EXT_AD)-1, NULL, NULL, NULL, NULL) );
            ((PEXT_AD)((*Buff)+BufOffs))->extLocation.partitionReferenceNum = (uint16)PartNum;
            BufOffs = (BufOffs + 2*sizeof(EXT_AD) - 1) & ~(sizeof(EXT_AD)-1) ;
            // init AllocDesc
            ( (PALLOC_EXT_DESC) ((*Buff)+BufOffs))->lengthAllocDescs = len2;
            ( (PALLOC_EXT_DESC) ((*Buff)+BufOffs))->previousAllocExtLocation = prevTagLoc;
            Tag = (tag*)((*Buff)+BufOffs);
            TagLen = len2;
            BufOffs += sizeof(ALLOC_EXT_DESC);
        }
        MyFreePool__(saved_Alloc);
    }
    status = UDFResizeExtent(Vcb, PartNum, NewLen, TRUE, AllocExtent);
    return status;
} // end UDFBuildExtAllocDescs()*/


/* Private extent growth/shrink owns typed allocator leases and exact old
 * generation retirements. The former ParentLocation-only cache has no
 * producer/consumer in this source: retaining its get/store/append API would
 * allow untyped or still-live ranges to become reusable. Keep checkpoint's
 * empty-cache contract, but never guess the domain/owner of a stray entry. */
NTSTATUS
UDFFlushAllCachedAllocations(
    IN PVCB Vcb,
    IN uint32 AllocClass
    )
{
    NTSTATUS Status = STATUS_SUCCESS;
    BOOLEAN CacheLocked = FALSE;
    if (AllocClass != UDF_PREALLOC_CLASS_DIR)
        return STATUS_INVALID_PARAMETER;
    UDFAcquireResourceExclusive(&Vcb->BitMapResource1, TRUE);
    _SEH2_TRY {
        UDFAcquireResourceExclusive(&Vcb->PreallocResource, TRUE);
        CacheLocked = TRUE;
        if (Vcb->PreallocCache || Vcb->PreallocCacheMaxSize) {
            /* Preserve the inventory. No current source path creates it,
             * and its obsolete coordinates prove no bitmap ownership. */
            Status = STATUS_FILE_CORRUPT_ERROR;
            Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
            InterlockedCompareExchange(&Vcb->MetadataWritebackError,
                                        Status, STATUS_SUCCESS);
        }
    } _SEH2_FINALLY {
        if (CacheLocked) UDFReleaseResource(&Vcb->PreallocResource);
        UDFReleaseResource(&Vcb->BitMapResource1);
    } _SEH2_END;
    return Status;
} // end UDFFlushAllCachedAllocations()

/*
    This routine allocates space for FE of the file being created.
    Allocates a single block from the partition bitmap.
 */
static NTSTATUS
UDFAllocateFESpaceInternal(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN PUDF_FILE_INFO DirInfo,
    IN uint32 PartNum,
    IN PEXTENT_INFO FEExtInfo,
    IN uint32 Len,
    OUT PUDF_ALLOCATION_FAILURE* Lease OPTIONAL
    )
{
    uint32 start, end, length, preferred;
    NTSTATUS status;
    UNREFERENCED_PARAMETER(DirInfo);

    if (Lease) *Lease = NULL;
    if (!Vcb || !Vcb->Partitions || PartNum >= Vcb->PartitionMaps ||
        !FEExtInfo || !Len || !Vcb->SectorSize)
        return STATUS_INVALID_PARAMETER;
    if (Vcb->MetadataFileInfo && PartNum == Vcb->MetadataRef && !Lease)
        return UDFAllocMetadataExtent(IrpContext, Vcb, Len, FEExtInfo);

    start = UDFPartStart(Vcb, PartNum);
    end = UDFPartEnd(Vcb, PartNum);
    length = end - start;
    /* This is placement, not reserved space: every FE still consumes the
     * ordinary partition bitmap. Keep data's sector hint out of the FE run.
     * A full preferred range falls back to the entire partition. */
    if (!Vcb->MetadataFileInfo && Vcb->UdfRevision == 0x201 &&
        Vcb->PartitionMaps == 1 && PartNum == 0 && !Vcb->CDR_Mode &&
        Vcb->SectorSize == 512 && Len > 0 && Len <= Vcb->SectorSize &&
        Vcb->TargetDeviceObject &&
        Vcb->TargetDeviceObject->DeviceType == FILE_DEVICE_DISK &&
        end > start && length >= 16384 && Vcb->FSBM_BitCount >= length &&
        Vcb->FeSectorHint != MAXULONG) {
        preferred = end - length / 4;
        status = Lease ?
            UDFAllocExtentLease(IrpContext, Vcb, PartNum, Len, preferred, end,
                FEExtInfo, EXTENT_FLAG_VERIFY | EXTENT_FLAG_FE_LOCALITY, Lease) :
            UDFAllocFreeExtent(IrpContext, Vcb, PartNum, Len, preferred, end,
                FEExtInfo, EXTENT_FLAG_VERIFY | EXTENT_FLAG_FE_LOCALITY);
        if (status != STATUS_DISK_FULL)
            return status;
    }
    return Lease ?
        UDFAllocExtentLease(IrpContext, Vcb, PartNum, Len, start, end,
                            FEExtInfo, EXTENT_FLAG_VERIFY, Lease) :
        UDFAllocFreeExtent(IrpContext, Vcb, PartNum, Len, start, end,
                            FEExtInfo, EXTENT_FLAG_VERIFY);
}

NTSTATUS
UDFAllocateFESpace(PIRP_CONTEXT IrpContext, PVCB Vcb, PUDF_FILE_INFO DirInfo,
                   uint32 PartNum, PEXTENT_INFO FEExtInfo, uint32 Len)
{
    return UDFAllocateFESpaceInternal(IrpContext, Vcb, DirInfo, PartNum,
                                     FEExtInfo, Len, NULL);
}

/* The returned map is borrowed from the VCB lease. A constructor must
 * publish an independent work map before committing these bitmap claims. */
NTSTATUS
UDFAllocateFESpaceLease(PIRP_CONTEXT IrpContext, PVCB Vcb,
    PUDF_FILE_INFO DirInfo, uint32 PartNum, PEXTENT_INFO FEExtInfo,
    uint32 Len, PUDF_ALLOCATION_FAILURE* Lease)
{
    if (!Lease) return STATUS_INVALID_PARAMETER;
    return UDFAllocateFESpaceInternal(IrpContext, Vcb, DirInfo, PartNum,
                                     FEExtInfo, Len, Lease);
}

/*
    This routine frees space allocated for FE.
 */
void
UDFFreeFESpace(
    IN PVCB Vcb,
    IN PUDF_FILE_INFO DirInfo,
    IN PEXTENT_INFO FEExtInfo
    )
{
    UNREFERENCED_PARAMETER(DirInfo);
    UDFMarkSpaceAsXXX(Vcb, 0, FEExtInfo->Mapping, AS_DISCARDED); // free
    FEExtInfo->Mapping[0].extLocation = 0;
    FEExtInfo->Mapping[0].extLength = (EXTENT_NOT_RECORDED_NOT_ALLOCATED << 30);
} // end UDFFreeFESpace()

/*
    This routine rebuilds mapping on write attempts to Alloc-Not-Rec area.
    Here we assume that required area lays in a single frag.
 */
NTSTATUS
UDFMarkAllocatedAsRecorded(
    IN PVCB Vcb,
    IN int64 Offset,
    IN uint32 Length,
    IN PEXTENT_INFO ExtInfo   // Extent array
    )
{
    uint32 Index, Lba, Type, OldBytes, Prefix, Suffix, Rounded, j;
    SIZE_T Available;
    uint64 Cover, AllocationBytes;
    PEXTENT_MAP OldMap, NewMap;
    NTSTATUS Status;

    if (!Vcb || !ExtInfo || !ExtInfo->Mapping || Offset < 0 || !Vcb->SectorSize)
        return STATUS_INVALID_PARAMETER;
    if (!Length)
        return STATUS_SUCCESS;
    OldMap = ExtInfo->Mapping;
    Lba = UDFExtentOffsetToLba(Vcb, OldMap,
        Offset & ~((int64)Vcb->SectorSize - 1), NULL, &Available, &Type, &Index);
    Cover = ((uint64)Offset & (Vcb->SectorSize - 1)) + Length;
    Cover = (Cover + Vcb->SectorSize - 1) & ~((uint64)Vcb->SectorSize - 1);
    if (Lba == LBA_OUT_OF_EXTENT || Index == MAXULONG || !Cover || Cover > Available)
        return STATUS_INVALID_PARAMETER;
    if (Type == EXTENT_RECORDED_ALLOCATED)
        return STATUS_SUCCESS;
    if (Type != EXTENT_NOT_RECORDED_ALLOCATED)
        return STATUS_INVALID_PARAMETER;
    if (Vcb->Vat || Vcb->CDR_Mode || (Vcb->VcbState & VCB_STATE_VOLUME_READ_ONLY))
        return STATUS_MEDIA_WRITE_PROTECTED;
    Rounded = (uint32)Cover;
    Prefix = (Lba - OldMap[Index].extLocation) << Vcb->SectorShift;
    Suffix = (OldMap[Index].extLength & UDF_EXTENT_LENGTH_MASK) - Prefix - Rounded;
    if (!Prefix && !Suffix) {
        OldMap[Index].extLength = Rounded;
        ExtInfo->Modified = TRUE;
        UDFPackMapping(Vcb, ExtInfo);
        return STATUS_SUCCESS;
    }
    Status = UDFGetMappingByteCount(OldMap, &OldBytes);
    if (!NT_SUCCESS(Status))
        return Status;
    AllocationBytes = (uint64)OldBytes +
        ((Prefix ? 1 : 0) + (Suffix ? 1 : 0)) * sizeof(EXTENT_MAP);
    if (AllocationBytes > MAXULONG - MY_HEAP_ALIGN)
        return STATUS_INSUFFICIENT_RESOURCES;
    NewMap = (PEXTENT_MAP)MyAllocatePoolTag__(NonPagedPool,
                         (uint32)AllocationBytes, MEM_EXTMAP_TAG);
    if (!NewMap)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlCopyMemory(NewMap, OldMap, Index * sizeof(EXTENT_MAP));
    j = Index;
    if (Prefix) {
        NewMap[j].extLocation = OldMap[Index].extLocation;
        NewMap[j++].extLength = Prefix | (EXTENT_NOT_RECORDED_ALLOCATED << 30);
    }
    NewMap[j].extLocation = Lba;
    NewMap[j++].extLength = Rounded;
    if (Suffix) {
        NewMap[j].extLocation = Lba + (Rounded >> Vcb->SectorShift);
        NewMap[j++].extLength = Suffix | (EXTENT_NOT_RECORDED_ALLOCATED << 30);
    }
    RtlCopyMemory(&NewMap[j], &OldMap[Index + 1],
                  OldBytes - (Index + 1) * sizeof(EXTENT_MAP));
    ExtInfo->Mapping = NewMap;
    ExtInfo->Modified = TRUE;
    MyFreePool__(OldMap);
    UDFPackMapping(Vcb, ExtInfo);
    return STATUS_SUCCESS;
} // end UDFMarkAllocatedAsRecorded()

/*
    This routine rebuilds mapping on write attempts to Not-Alloc-Not-Rec area.
    Here we assume that required area lays in a single frag.
 */
NTSTATUS
UDFMarkNotAllocatedAsAllocated(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN int64 Offset,
    IN uint32 Length,
    IN PEXTENT_INFO ExtInfo   // Extent array
    )
{
    uint32 Index, Lba, Type, OldBytes, TailBytes, Prefix, Suffix, j, k;
    uint32 PartNum = LBA_OUT_OF_EXTENT, Derived;
    uint64 Blocks, End;
    SIZE_T Available;
    uint64 Cover, AllocationBytes;
    PEXTENT_MAP OldMap, NewMap = NULL;
    PUDF_ALLOCATION_FAILURE Lease = NULL;
    BOOLEAN Transferred = FALSE;
    NTSTATUS LeaseStatus;
    EXTENT_INFO Tail;
    NTSTATUS Status;

    if (!Vcb || !ExtInfo || !ExtInfo->Mapping || Offset < 0 || !Vcb->SectorSize ||
        !Vcb->Partitions || !Vcb->PartitionMaps || Vcb->SectorShift >= 30 ||
        Vcb->SectorSize != ((uint32)1 << Vcb->SectorShift))
        return STATUS_INVALID_PARAMETER;
    if (!Length)
        return STATUS_SUCCESS;
    OldMap = ExtInfo->Mapping;
    Lba = UDFExtentOffsetToLba(Vcb, OldMap,
        Offset & ~((int64)Vcb->SectorSize - 1), NULL, &Available, &Type, &Index);
    Cover = (((uint64)Offset & (Vcb->SectorSize - 1)) + Length + Vcb->SectorSize - 1) &
             ~((uint64)Vcb->SectorSize - 1);
    if (Lba == LBA_OUT_OF_EXTENT || Index == MAXULONG || !Cover || Cover > Available)
        return STATUS_INVALID_PARAMETER;
    if (Type != EXTENT_NOT_RECORDED_NOT_ALLOCATED)
        return STATUS_SUCCESS;
    if (Vcb->VcbState & VCB_STATE_VOLUME_READ_ONLY)
        return STATUS_MEDIA_WRITE_PROTECTED;
    Status = UDFGetMappingByteCount(OldMap, &OldBytes);
    if (!NT_SUCCESS(Status))
        return Status;
    Prefix = (OldMap[Index].extLength & UDF_EXTENT_LENGTH_MASK) - (uint32)Available;
    Suffix = (uint32)Available - (uint32)Cover;
    /* A PSN cannot identify a hole's partition. Infer the domain from a
     * real run, never from the hole's zero/placeholder address. A wholly
     * sparse map on a metadata volume needs explicit typed owner input. */
    for (k = 0; k + 1 < OldBytes / sizeof(EXTENT_MAP); k++) {
        if ((OldMap[k].extLength >> 30) == EXTENT_NOT_RECORDED_NOT_ALLOCATED)
            continue;
        Blocks = ((uint64)(OldMap[k].extLength & UDF_EXTENT_LENGTH_MASK) +
                   Vcb->SectorSize - 1) >> Vcb->SectorShift;
        End = (uint64)OldMap[k].extLocation + Blocks;
        if (!Blocks || End > LBA_OUT_OF_EXTENT)
            return STATUS_FILE_CORRUPT_ERROR;
        Derived = UDFGetRefPartNumByPhysLba(Vcb, OldMap[k].extLocation);
        if (Derived >= Vcb->PartitionMaps ||
            UDFGetRefPartNumByPhysLba(Vcb, (uint32)(End - 1)) != Derived ||
            (PartNum != LBA_OUT_OF_EXTENT && PartNum != Derived))
            return STATUS_NOT_SUPPORTED;
        PartNum = Derived;
    }
    if (PartNum == LBA_OUT_OF_EXTENT && !Vcb->MetadataFileInfo) {
        for (k = 0; k < Vcb->PartitionMaps; k++) {
            if (Vcb->Partitions[k].PartitionType != UDF_TYPE1_MAP15 &&
                Vcb->Partitions[k].PartitionType != UDF_SPARABLE_MAP15)
                continue;
            if (PartNum != LBA_OUT_OF_EXTENT &&
                (Vcb->Partitions[k].PartitionRoot != Vcb->Partitions[PartNum].PartitionRoot ||
                 Vcb->Partitions[k].PartitionLen != Vcb->Partitions[PartNum].PartitionLen ||
                 Vcb->Partitions[k].PartitionNum != Vcb->Partitions[PartNum].PartitionNum ||
                 Vcb->Partitions[k].VolumeSeqNum != Vcb->Partitions[PartNum].VolumeSeqNum))
                return STATUS_NOT_SUPPORTED;
            PartNum = k;
        }
    }
    if (PartNum == LBA_OUT_OF_EXTENT || Vcb->Vat || Vcb->CDR_Mode)
        return STATUS_NOT_SUPPORTED;
    RtlZeroMemory(&Tail, sizeof(Tail));
    _SEH2_TRY {
    _SEH2_TRY {
    Status = UDFAllocExtentLease(IrpContext, Vcb, PartNum, (int64)Cover,
        UDFPartStart(Vcb, PartNum), UDFPartEnd(Vcb, PartNum), &Tail, ExtInfo->Flags, &Lease);
    if (!NT_SUCCESS(Status)) goto split_exit;
    if (!Lease || !Tail.Mapping) {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto split_exit;
    }
    Status = UDFGetMappingByteCount(Tail.Mapping, &TailBytes);
    if (!NT_SUCCESS(Status))
        goto split_exit;
    if ((uint64)UDFGetExtentLength(Tail.Mapping) != Cover) {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto split_exit;
    }
    AllocationBytes = (uint64)OldBytes + TailBytes - 2 * sizeof(EXTENT_MAP) +
                     ((Prefix ? 1 : 0) + (Suffix ? 1 : 0)) * sizeof(EXTENT_MAP);
    if (AllocationBytes > MAXULONG - MY_HEAP_ALIGN) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto split_exit;
    }
    NewMap = (PEXTENT_MAP)MyAllocatePoolTag__(NonPagedPool,
                         (uint32)AllocationBytes, MEM_EXTMAP_TAG);
    if (!NewMap) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto split_exit;
    }
    RtlCopyMemory(NewMap, OldMap, Index * sizeof(EXTENT_MAP));
    j = Index;
    if (Prefix) {
        NewMap[j].extLocation = 0;
        NewMap[j++].extLength = Prefix | (EXTENT_NOT_RECORDED_NOT_ALLOCATED << 30);
    }
    RtlCopyMemory(&NewMap[j], Tail.Mapping, TailBytes - sizeof(EXTENT_MAP));
    j += TailBytes / sizeof(EXTENT_MAP) - 1;
    if (Suffix) {
        NewMap[j].extLocation = 0;
        NewMap[j++].extLength = Suffix | (EXTENT_NOT_RECORDED_NOT_ALLOCATED << 30);
    }
    RtlCopyMemory(&NewMap[j], &OldMap[Index + 1],
                  OldBytes - (Index + 1) * sizeof(EXTENT_MAP));
    Status = UDFPublishAllocationLease(Vcb, Lease);
    if (!NT_SUCCESS(Status)) goto split_exit;
    ExtInfo->Mapping = NewMap;
    ExtInfo->Modified = TRUE;
    NewMap = NULL;
    Transferred = TRUE;
    MyFreePool__(OldMap);
split_exit:
    NOTHING;
    } _SEH2_EXCEPT(FsRtlIsNtstatusExpected(_SEH2_GetExceptionCode()) ?
                  EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        Status = _SEH2_GetExceptionCode();
    } _SEH2_END;
    } _SEH2_FINALLY {
        if (_SEH2_AbnormalTermination()) {
            Status = STATUS_FILE_CORRUPT_ERROR;
            if (Transferred) Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
        }
        if (Lease) {
            if (Transferred && !_SEH2_AbnormalTermination())
                LeaseStatus = UDFCommitAllocationLease(Vcb, &Lease);
            else
                LeaseStatus = UDFAbortAllocationLease(Vcb, &Lease, Status);
            if (NT_SUCCESS(Status) && !NT_SUCCESS(LeaseStatus)) Status = LeaseStatus;
            if (Lease) {
                Lease->Uncertain = TRUE;
                UDFAbortAllocationLease(Vcb, &Lease, Status);
            }
        }
        if (NewMap) MyFreePool__(NewMap);
    } _SEH2_END;
    return Status;
} // end UDFMarkNotAllocatedAsAllocated()

//#if 0
/*
    This routine rebuilds mapping on write zero attempts to
    Alloc-Not-Rec area.
    Here we assume that required area lays in a single frag.
 */
NTSTATUS
UDFMarkAllocatedAsNotXXX(
    IN PVCB Vcb,
    IN int64 Offset,
    IN uint32 Length,
    IN PEXTENT_INFO ExtInfo,   // Extent array
    IN BOOLEAN Deallocate
    )
{
    uint32 Index, Lba, Type, Target, OldBytes, Prefix, Suffix, j;
    SIZE_T Available;
    uint64 AllocationBytes;
    PEXTENT_MAP OldMap, NewMap;
    NTSTATUS Status;

    if (!Vcb || !ExtInfo || !ExtInfo->Mapping || Offset < 0 || !Vcb->SectorSize ||
        (Offset & (Vcb->SectorSize - 1)) || (Length & (Vcb->SectorSize - 1)))
        return STATUS_INVALID_PARAMETER;
    if (!Length)
        return STATUS_SUCCESS;
    OldMap = ExtInfo->Mapping;
    Lba = UDFExtentOffsetToLba(Vcb, OldMap, Offset, NULL, &Available, &Type, &Index);
    if (Lba == LBA_OUT_OF_EXTENT || Index == MAXULONG || Length > Available)
        return STATUS_INVALID_PARAMETER;
    Target = Deallocate ? EXTENT_NOT_RECORDED_NOT_ALLOCATED : EXTENT_NOT_RECORDED_ALLOCATED;
    if (Type == Target || Type == EXTENT_NOT_RECORDED_NOT_ALLOCATED)
        return STATUS_SUCCESS;
    if (Vcb->Vat || Vcb->CDR_Mode || (Vcb->VcbState & VCB_STATE_VOLUME_READ_ONLY))
        return STATUS_MEDIA_WRITE_PROTECTED;
    /* Releasing a live allocation requires the outer FE/barrier transaction.
     * Do not free bitmap bits before the replacement FE is durable. Metadata
     * holes need a separate typed retirement protocol, not physical bits. */
    if (Deallocate) {
        if (Vcb->TruncateTxnOwner != PsGetCurrentThread() || Vcb->Vat || Vcb->CDR_Mode ||
            (Vcb->MetadataFileInfo && UDFPhysLbaToMetadata(Vcb, Lba) != LBA_OUT_OF_EXTENT))
            return STATUS_NOT_SUPPORTED;
        if (!NT_SUCCESS(Vcb->TruncateTxnError))
            return Vcb->TruncateTxnError;
        if (!Vcb->TruncateTxnReleased || Vcb->TruncateTxnCount >= Vcb->TruncateTxnCapacity)
            return STATUS_INSUFFICIENT_RESOURCES;
    }
    Status = UDFGetMappingByteCount(OldMap, &OldBytes);
    if (!NT_SUCCESS(Status))
        return Status;
    Prefix = (OldMap[Index].extLength & UDF_EXTENT_LENGTH_MASK) - (uint32)Available;
    Suffix = (uint32)Available - Length;
    AllocationBytes = (uint64)OldBytes +
        ((Prefix ? 1 : 0) + (Suffix ? 1 : 0)) * sizeof(EXTENT_MAP);
    if (AllocationBytes > MAXULONG - MY_HEAP_ALIGN)
        return STATUS_INSUFFICIENT_RESOURCES;
    NewMap = (PEXTENT_MAP)MyAllocatePoolTag__(NonPagedPool,
                         (uint32)AllocationBytes, MEM_EXTMAP_TAG);
    if (!NewMap)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlCopyMemory(NewMap, OldMap, Index * sizeof(EXTENT_MAP));
    j = Index;
    if (Prefix) {
        NewMap[j].extLocation = OldMap[Index].extLocation;
        NewMap[j++].extLength = Prefix | (Type << 30);
    }
    NewMap[j].extLocation = Deallocate ? 0 : Lba;
    NewMap[j++].extLength = Length | (Target << 30);
    if (Suffix) {
        NewMap[j].extLocation = Lba + (Length >> Vcb->SectorShift);
        NewMap[j++].extLength = Suffix | (Type << 30);
    }
    RtlCopyMemory(&NewMap[j], &OldMap[Index + 1],
                  OldBytes - (Index + 1) * sizeof(EXTENT_MAP));
    if (Deallocate) {
        PEXTENT_MAP Released = &Vcb->TruncateTxnReleased[Vcb->TruncateTxnCount++];
        Released->extLocation = Lba;
        Released->extLength = Length;
    }
    ExtInfo->Mapping = NewMap;
    ExtInfo->Modified = TRUE;
    MyFreePool__(OldMap);
    UDFPackMapping(Vcb, ExtInfo);
    return STATUS_SUCCESS;
} // end UDFMarkAllocatedAsNotXXX()
//#endif //0

/*
    This routine resizes extent & updates associated mapping
 */
/* Each successful tail remains a typed allocator lease until its copy is
 * part of the complete replacement. The list is an attempt owner, distinct
 * from the VCB active-lease linkage and from the live file mapping. */
typedef struct _UDF_RESIZE_CLAIM {
    struct _UDF_RESIZE_CLAIM* Next;
    PUDF_ALLOCATION_FAILURE Lease;
} UDF_RESIZE_CLAIM, *PUDF_RESIZE_CLAIM;

static NTSTATUS
UDFGrowExtentPrivate(PIRP_CONTEXT IrpContext, PVCB Vcb, uint32 PartNum,
                     int64 Length, BOOLEAN AlwaysInIcb, PEXTENT_INFO ExtInfo)
{
    EXTENT_INFO Candidate, Tail;
    PEXTENT_MAP Previous;
    PUDF_RESIZE_CLAIM Claims = NULL, Claim, Next;
    uint32 Bytes, Count, i, LastLength, RoundedLength, MaxGrow;
    uint64 Target, Capacity, TailBytes, Need;
    NTSTATUS Status = STATUS_SUCCESS, LeaseStatus;
    BOOLEAN Transferred = FALSE;

    if (!Vcb || !ExtInfo || !Vcb->Partitions || PartNum >= Vcb->PartitionMaps ||
        !Vcb->SectorSize || Vcb->SectorShift >= 30 ||
        Vcb->SectorSize != ((uint32)1 << Vcb->SectorShift) ||
        Length < 0 || ExtInfo->Length < 0 || Length <= ExtInfo->Length ||
        (uint64)Length > (uint64)MAXLONGLONG - ExtInfo->Offset)
        return STATUS_INVALID_PARAMETER;
    Candidate = *ExtInfo;
    Candidate.Mapping = NULL;
    MaxGrow = ALIGN_DOWN_BY(UDF_EXTENT_LENGTH_MASK, Vcb->SectorSize);

    _SEH2_TRY {
    _SEH2_TRY {
        Bytes = sizeof(EXTENT_MAP);
        if (ExtInfo->Mapping) {
            Status = UDFGetMappingByteCount(ExtInfo->Mapping, &Bytes);
            if (!NT_SUCCESS(Status)) goto growth_exit;
        }
        Candidate.Mapping = (PEXTENT_MAP)MyAllocatePoolTag__(NonPagedPool, Bytes, MEM_EXTMAP_TAG);
        if (!Candidate.Mapping) {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto growth_exit;
        }
        if (ExtInfo->Mapping)
            RtlCopyMemory(Candidate.Mapping, ExtInfo->Mapping, Bytes);
        else
            RtlZeroMemory(Candidate.Mapping, Bytes);
        Count = Bytes / sizeof(EXTENT_MAP) - 1;
        if (Candidate.Offset) {
            if (Candidate.Offset > Vcb->SectorSize || !Count ||
                (Candidate.Mapping[0].extLength & UDF_EXTENT_LENGTH_MASK) < Vcb->SectorSize ||
                (!AlwaysInIcb && (Candidate.Mapping[0].extLength & UDF_EXTENT_LENGTH_MASK) != Vcb->SectorSize) ||
                ((Candidate.Mapping[0].extLength >> 30) != EXTENT_RECORDED_ALLOCATED &&
                 (!AlwaysInIcb || (Candidate.Mapping[0].extLength >> 30) != EXTENT_NOT_RECORDED_ALLOCATED))) {
                Status = STATUS_FILE_CORRUPT_ERROR;
                goto growth_exit;
            }
            if ((uint64)Length + Candidate.Offset > Vcb->SectorSize && !AlwaysInIcb) {
                /* Data promotion copies the inline payload in its caller.
                 * The FE sector itself is never an allocation to release. */
                if (Count != 1) {
                    Status = STATUS_FILE_CORRUPT_ERROR;
                    goto growth_exit;
                }
                RtlZeroMemory(Candidate.Mapping, Bytes);
                Count = 0;
                Candidate.Offset = 0;
            }
        }
        Target = (uint64)Length + Candidate.Offset;
        if (Target > (uint64)MAXLONGLONG - (Vcb->SectorSize - 1)) {
            Status = STATUS_INVALID_PARAMETER;
            goto growth_exit;
        }
        Target = (Target + Vcb->SectorSize - 1) & ~((uint64)Vcb->SectorSize - 1);
        /* Every nonfinal run begins at a sector boundary. The existing
         * final partial run already owns its physical final sector; round
         * that private descriptor without claiming that block a second time. */
        for (i = 0; i + 1 < Count; i++) {
            if (Candidate.Mapping[i].extLength & (Vcb->SectorSize - 1)) {
                Status = STATUS_NOT_SUPPORTED;
                goto growth_exit;
            }
        }
        if (Count) {
            LastLength = Candidate.Mapping[Count - 1].extLength & UDF_EXTENT_LENGTH_MASK;
            RoundedLength = (LastLength + Vcb->SectorSize - 1) & ~(Vcb->SectorSize - 1);
            if (RoundedLength > MaxGrow) {
                Status = STATUS_NOT_SUPPORTED;
                goto growth_exit;
            }
            Candidate.Mapping[Count - 1].extLength = RoundedLength |
                (Candidate.Mapping[Count - 1].extLength & UDF_EXTENT_FLAG_MASK);
        }
        Capacity = UDFGetExtentLength(Candidate.Mapping);
        while (Capacity < Target) {
            Need = min(Target - Capacity, (uint64)MaxGrow);
            Claim = (PUDF_RESIZE_CLAIM)MyAllocatePool__(NonPagedPool, sizeof(*Claim));
            if (!Claim) {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                goto growth_exit;
            }
            RtlZeroMemory(Claim, sizeof(*Claim));
            Claim->Next = Claims;
            Claims = Claim; /* owns any result even if the allocator raises */
            RtlZeroMemory(&Tail, sizeof(Tail));
            Status = UDFAllocExtentLease(IrpContext, Vcb, PartNum, (int64)Need,
                UDFPartStart(Vcb, PartNum), UDFPartEnd(Vcb, PartNum),
                &Tail, Candidate.Flags, &Claim->Lease);
            if (!NT_SUCCESS(Status)) goto growth_exit;
            if (!Claim->Lease || !Tail.Mapping) {
                Status = STATUS_FILE_CORRUPT_ERROR;
                goto growth_exit;
            }
            TailBytes = UDFGetExtentLength(Tail.Mapping);
            if (TailBytes < Need || TailBytes > (uint64)MAXLONGLONG - Capacity) {
                Status = STATUS_FILE_CORRUPT_ERROR;
                goto growth_exit;
            }
            Status = UDFAppendMapping(&Candidate.Mapping, Tail.Mapping);
            if (!NT_SUCCESS(Status)) goto growth_exit;
            Capacity += TailBytes;
        }
        Candidate.Length = Length;
        Candidate.Modified = TRUE;
        for (Claim = Claims; Claim; Claim = Claim->Next) {
            Status = UDFPublishAllocationLease(Vcb, Claim->Lease);
            if (!NT_SUCCESS(Status)) goto growth_exit;
        }
        /* No claim or fallible map allocation remains. Publish the complete
         * RAM shape once. The retained old physical prefix stays allocated. */
        Previous = ExtInfo->Mapping;
        *ExtInfo = Candidate;
        Candidate.Mapping = NULL;
        Transferred = TRUE;
        if (Previous) MyFreePool__(Previous);

growth_exit:
        NOTHING;
    } _SEH2_EXCEPT(FsRtlIsNtstatusExpected(_SEH2_GetExceptionCode()) ?
                  EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        Status = _SEH2_GetExceptionCode();
    } _SEH2_END;
    } _SEH2_FINALLY {
        if (_SEH2_AbnormalTermination()) {
            Status = STATUS_FILE_CORRUPT_ERROR;
            Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
        }
        for (Claim = Claims; Claim; Claim = Next) {
            Next = Claim->Next;
            if (Claim->Lease) {
                if (Transferred && !_SEH2_AbnormalTermination())
                    LeaseStatus = UDFCommitAllocationLease(Vcb, &Claim->Lease);
                else
                    LeaseStatus = UDFAbortAllocationLease(Vcb, &Claim->Lease, Status);
                if (NT_SUCCESS(Status) && !NT_SUCCESS(LeaseStatus)) Status = LeaseStatus;
                if (Claim->Lease) {
                    Claim->Lease->Uncertain = TRUE;
                    UDFAbortAllocationLease(Vcb, &Claim->Lease, Status);
                }
            }
            MyFreePool__(Claim);
        }
        if (Candidate.Mapping) MyFreePool__(Candidate.Mapping);
    } _SEH2_END;
    return Status;
}

/* Build both the kept map and the exact retired allocation list before
 * changing any live field. No bitmap is freed by this planner. */
NTSTATUS
UDFPrepareExtentShrink(PVCB Vcb, uint32 PartNum, int64 Length,
    BOOLEAN AlwaysInIcb, PEXTENT_INFO ExtInfo, PEXTENT_INFO Replacement,
    PUDF_EXTENT_RETIREMENT* Deferred)
{
    EXTENT_INFO Candidate;
    PUDF_EXTENT_RETIREMENT Retired = NULL;
    uint32 Bytes, Count, i, Used = 0, Kind, RunBytes, OldBlocks, KeepBlocks, KeepBytes;
    uint32 MaxGrow;
    uint64 Target, Offset = 0, Remaining, AllocBytes;
    NTSTATUS Status;

    if (!Replacement || !Deferred || Replacement == ExtInfo) return STATUS_INVALID_PARAMETER;
    Replacement->Mapping = NULL;
    *Deferred = NULL;
    if (!Vcb || !ExtInfo || !Vcb->Partitions || PartNum >= Vcb->PartitionMaps ||
        !Vcb->SectorSize || Vcb->SectorShift >= 30 ||
        Vcb->SectorSize != ((uint32)1 << Vcb->SectorShift) ||
        Length < 0 || ExtInfo->Length < 0 || Length > ExtInfo->Length)
        return STATUS_INVALID_PARAMETER;
    if (Vcb->Partitions[PartNum].PartitionType != UDF_TYPE1_MAP15 &&
        Vcb->Partitions[PartNum].PartitionType != UDF_SPARABLE_MAP15 &&
        Vcb->Partitions[PartNum].PartitionType != UDF_METADATA_MAP25)
        return STATUS_NOT_SUPPORTED;
    Candidate = *ExtInfo;
    Candidate.Mapping = NULL;
    MaxGrow = ALIGN_DOWN_BY(UDF_EXTENT_LENGTH_MASK, Vcb->SectorSize);
    Status = UDFGetMappingByteCount(ExtInfo->Mapping, &Bytes);
    if (!NT_SUCCESS(Status)) return Status;
    Count = Bytes / sizeof(EXTENT_MAP) - 1;
    if (!Count || (uint64)Length > (uint64)MAXLONGLONG - Candidate.Offset)
        return STATUS_FILE_CORRUPT_ERROR;
    if (Candidate.Offset && !AlwaysInIcb) {
        /* An inline data FE never has a separately releasable data tail. */
        if (Count != 1 || Candidate.Offset > Vcb->SectorSize ||
            (ExtInfo->Mapping[0].extLength & UDF_EXTENT_LENGTH_MASK) != Vcb->SectorSize ||
            (uint64)Length + Candidate.Offset > Vcb->SectorSize)
            return STATUS_FILE_CORRUPT_ERROR;
    }
    Candidate.Mapping = NULL;
    Target = (uint64)Length + Candidate.Offset;
    if (Target > (uint64)MAXLONGLONG - (Vcb->SectorSize - 1))
        return STATUS_INVALID_PARAMETER;
    Target = (Target + Vcb->SectorSize - 1) & ~((uint64)Vcb->SectorSize - 1);
    AllocBytes = (uint64)FIELD_OFFSET(UDF_EXTENT_RETIREMENT, Ranges) + Bytes;
    if (AllocBytes > MAXULONG - MY_HEAP_ALIGN)
        return STATUS_INSUFFICIENT_RESOURCES;
    _SEH2_TRY {
        Candidate.Mapping = (PEXTENT_MAP)MyAllocatePoolTag__(NonPagedPool,
                                      max(Bytes, 2 * sizeof(EXTENT_MAP)), MEM_EXTMAP_TAG);
        Retired = (PUDF_EXTENT_RETIREMENT)MyAllocatePool__(NonPagedPool, AllocBytes);
        if (!Candidate.Mapping || !Retired) {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto shrink_exit;
        }
        RtlZeroMemory(Candidate.Mapping, max(Bytes, 2 * sizeof(EXTENT_MAP)));
        RtlZeroMemory(Retired, AllocBytes);
        Retired->Capacity = Count;
        Retired->PartRef = PartNum;
        Retired->PartitionRoot = Vcb->Partitions[PartNum].PartitionRoot;
        Retired->PartitionLength = Vcb->Partitions[PartNum].PartitionLen;
        Retired->Metadata = Vcb->Partitions[PartNum].PartitionType == UDF_METADATA_MAP25;
        for (i = 0; i < Count; i++) {
            RunBytes = ExtInfo->Mapping[i].extLength & UDF_EXTENT_LENGTH_MASK;
            Kind = ExtInfo->Mapping[i].extLength >> 30;
            if ((i + 1 < Count && (RunBytes & (Vcb->SectorSize - 1))) ||
                RunBytes > MaxGrow) {
                Status = STATUS_NOT_SUPPORTED;
                goto shrink_exit;
            }
            OldBlocks = (RunBytes + Vcb->SectorSize - 1) >> Vcb->SectorShift;
            Remaining = Target > Offset ? min((uint64)RunBytes, Target - Offset) : 0;
            KeepBlocks = (uint32)((Remaining + Vcb->SectorSize - 1) >> Vcb->SectorShift);
            KeepBytes = KeepBlocks << Vcb->SectorShift;
            if (KeepBlocks) {
                Candidate.Mapping[Used] = ExtInfo->Mapping[i];
                Candidate.Mapping[Used++].extLength = KeepBytes | (Kind << 30);
            }
            if (KeepBlocks < OldBlocks && Kind != EXTENT_NOT_RECORDED_NOT_ALLOCATED) {
                if ((uint64)ExtInfo->Mapping[i].extLocation + OldBlocks > LBA_OUT_OF_EXTENT) {
                    Status = STATUS_FILE_CORRUPT_ERROR;
                    goto shrink_exit;
                }
                Retired->Ranges[Retired->Count].extLocation =
                    ExtInfo->Mapping[i].extLocation + KeepBlocks;
                Retired->Ranges[Retired->Count++].extLength =
                    ((OldBlocks - KeepBlocks) << Vcb->SectorShift) | (Kind << 30);
            }
            Offset += RunBytes;
        }
        if (Offset > (uint64)MAXLONGLONG - (Vcb->SectorSize - 1) ||
            Target > ((Offset + Vcb->SectorSize - 1) & ~((uint64)Vcb->SectorSize - 1))) {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto shrink_exit;
        }
        if (!Used) {
            Candidate.Mapping[0].extLength = Vcb->SectorSize |
                ((uint32)EXTENT_NOT_RECORDED_NOT_ALLOCATED << 30);
        }
        Candidate.Length = Length;
        Candidate.Modified = TRUE;
        *Replacement = Candidate;
        Candidate.Mapping = NULL;
        if (Retired->Count) {
            *Deferred = Retired;
            Retired = NULL;
        }
        Status = STATUS_SUCCESS;
shrink_exit:
        NOTHING;
    } _SEH2_FINALLY {
        if (Candidate.Mapping) MyFreePool__(Candidate.Mapping);
        if (Retired) MyFreePool__(Retired);
    } _SEH2_END;
    return Status;
}

static NTSTATUS
UDFShrinkExtentPrivate(PVCB Vcb, PUDF_FILE_INFO FileInfo, uint32 PartNum,
                       int64 Length, BOOLEAN AlwaysInIcb, PEXTENT_INFO ExtInfo)
{
    EXTENT_INFO Candidate;
    PUDF_EXTENT_RETIREMENT Retired = NULL;
    PEXTENT_MAP Previous;
    NTSTATUS Status;
    BOOLEAN Published = FALSE;
    RtlZeroMemory(&Candidate, sizeof(Candidate));
    Status = UDFPrepareExtentShrink(Vcb, PartNum, Length, AlwaysInIcb,
                                     ExtInfo, &Candidate, &Retired);
    if (!NT_SUCCESS(Status)) return Status;
    UDFAcquireResourceExclusive(&Vcb->BitMapResource1, TRUE);
    _SEH2_TRY {
        if (Retired) {
            if (Vcb->PendingExtentRetirements == MAXLONG) {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                _SEH2_LEAVE;
            }
            Retired->Next = FileInfo->Dloc->RetiredExtents;
            FileInfo->Dloc->RetiredExtents = Retired;
            InterlockedIncrement(&Vcb->PendingExtentRetirements);
            Retired = NULL;
        }
        Previous = ExtInfo->Mapping;
        *ExtInfo = Candidate;
        Candidate.Mapping = NULL;
        FileInfo->Dloc->FE_Flags |= UDF_FE_FLAG_FE_MODIFIED;
        Published = TRUE;
        if (Previous) MyFreePool__(Previous);
    } _SEH2_FINALLY {
        if (Candidate.Mapping) MyFreePool__(Candidate.Mapping);
        if (Retired) MyFreePool__(Retired);
        if (_SEH2_AbnormalTermination() && Published)
            Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
        UDFReleaseResource(&Vcb->BitMapResource1);
    } _SEH2_END;
    return Status;
}

static BOOLEAN
UDFRetirementOverlapsMap(PVCB Vcb, PEXTENT_MAP Map, uint32 Lba, uint32 Blocks)
{
    uint32 Bytes, Count, i;
    uint64 End = (uint64)Lba + Blocks, MapEnd;
    if (!Map) return FALSE;
    if (!NT_SUCCESS(UDFGetMappingByteCount(Map, &Bytes))) return TRUE;
    Count = Bytes / sizeof(EXTENT_MAP) - 1;
    for (i = 0; i < Count; i++) {
        if ((Map[i].extLength >> 30) == EXTENT_NOT_RECORDED_NOT_ALLOCATED)
            continue;
        MapEnd = (uint64)Map[i].extLocation +
            (((uint64)(Map[i].extLength & UDF_EXTENT_LENGTH_MASK) +
              Vcb->SectorSize - 1) >> Vcb->SectorShift);
        if ((uint64)Lba < MapEnd && (uint64)Map[i].extLocation < End)
            return TRUE;
    }
    return FALSE;
}

NTSTATUS
UDFCommitExtentRetirements(PIRP_CONTEXT IrpContext, PVCB Vcb, PUDF_FILE_INFO FileInfo)
{
    PUDF_DATALOC_INFO Dloc = FileInfo->Dloc;
    PUDF_EXTENT_RETIREMENT Retired;
    PUDF_METADATA_VIEW View = NULL;
    PUDFPartMap Part;
    EXTENT_MAP One[2];
    uint32 i, j, Length, Blocks, Lba, Lbn;
    NTSTATUS Status = STATUS_SUCCESS;

    UNREFERENCED_PARAMETER(IrpContext);
    if (!Dloc->RetiredExtents) return STATUS_SUCCESS;
    if (Dloc->DataLoc.Modified || Dloc->AllocLoc.Modified || Dloc->FELoc.Modified ||
        (Dloc->FE_Flags & UDF_FE_FLAG_FE_MODIFIED))
        return STATUS_DEVICE_BUSY; /* this API never substitutes for writing FE */
    UDFAcquireResourceExclusive(&Vcb->BitMapResource1, TRUE);
    _SEH2_TRY {
    _SEH2_TRY {
        View = UDFAcquireMetadataView(Vcb);
        Status = UDFBarrierFlushDevice(Vcb->TargetDeviceObject);
        if (!NT_SUCCESS(Status)) goto retirement_exit;
        while ((Retired = Dloc->RetiredExtents) != NULL) {
            if (!Retired->Count || Retired->Count > Retired->Capacity ||
                Retired->Completed > Retired->Count ||
                Retired->PartRef >= Vcb->PartitionMaps ||
                Vcb->PendingExtentRetirements <= 0) {
                Status = STATUS_FILE_CORRUPT_ERROR;
                goto retirement_exit;
            }
            if (Retired->Uncertain) {
                Status = NT_SUCCESS(Retired->Error) ? STATUS_FILE_CORRUPT_ERROR : Retired->Error;
                goto retirement_exit; /* no duplicate accounting after partial mutation */
            }
            Part = &Vcb->Partitions[Retired->PartRef];
            if (Part->PartitionRoot != Retired->PartitionRoot ||
                (Retired->Metadata ? Part->PartitionLen < Retired->PartitionLength :
                                     Part->PartitionLen != Retired->PartitionLength) ||
                (Retired->Metadata ? Part->PartitionType != UDF_METADATA_MAP25 :
                 (Part->PartitionType != UDF_TYPE1_MAP15 && Part->PartitionType != UDF_SPARABLE_MAP15))) {
                Status = STATUS_FILE_CORRUPT_ERROR;
                goto retirement_exit;
            }
            for (i = Retired->Completed; i < Retired->Count; i++) {
                One[0] = Retired->Ranges[i];
                One[1].extLength = One[1].extLocation = 0;
                Lba = One[0].extLocation;
                Length = One[0].extLength & UDF_EXTENT_LENGTH_MASK;
                Blocks = Length >> Vcb->SectorShift;
                if (!Blocks || (Length & (Vcb->SectorSize - 1)) ||
                    (One[0].extLength >> 30) > EXTENT_NOT_RECORDED_ALLOCATED ||
                    (uint64)Lba + Blocks > LBA_OUT_OF_EXTENT ||
                    UDFRetirementOverlapsMap(Vcb, Dloc->DataLoc.Mapping, Lba, Blocks) ||
                    UDFRetirementOverlapsMap(Vcb, Dloc->AllocLoc.Mapping, Lba, Blocks) ||
                    UDFRetirementOverlapsMap(Vcb, Dloc->FELoc.Mapping, Lba, Blocks)) {
                    Status = STATUS_FILE_CORRUPT_ERROR;
                    goto retirement_exit;
                }
                if (Retired->Metadata) {
                    Lbn = UDFMetadataViewPhysToLba(View, Lba);
                    if (Retired->PartRef != Vcb->MetadataRef || !View ||
                        !Vcb->MetadataBitmap || !Vcb->MetadataPendingAlloc ||
                        Lbn >= Vcb->MetadataBitCount || Blocks > Vcb->MetadataBitCount - Lbn ||
                        ((uint64)Vcb->MetadataBitCount + 7) / 8 > Vcb->MetadataBitmapByteCount ||
                        Vcb->MetadataFreeCount > Vcb->MetadataBitCount - Blocks) {
                        Status = STATUS_FILE_CORRUPT_ERROR;
                        goto retirement_exit;
                    }
                    for (j = 0; j < Blocks; j++) {
                        if ((Vcb->MetadataBitmap[(Lbn + j) >> 3] & (1 << ((Lbn + j) & 7))) ||
                            UDFMetadataViewLbaToPhys(View, Lbn + j, FALSE, NULL) != Lba + j) {
                            Status = STATUS_FILE_CORRUPT_ERROR;
                            goto retirement_exit;
                        }
                    }
                    Retired->Uncertain = TRUE;
                    for (j = 0; j < Blocks; j++) {
                        Vcb->MetadataBitmap[(Lbn + j) >> 3] |= (uint8)(1 << ((Lbn + j) & 7));
                        Vcb->MetadataPendingAlloc[(Lbn + j) >> 3] &= ~(1 << ((Lbn + j) & 7));
                    }
                    Vcb->MetadataFreeCount += Blocks;
                    Vcb->MetadataHint = min(Vcb->MetadataHint, Lbn);
                    Vcb->MetadataBitmapModified = TRUE;
                    UDFSetModified(Vcb);
                } else {
                    if (!Vcb->PartitionMaps ||
                        Part->PartitionRoot != Vcb->Partitions[0].PartitionRoot ||
                        Part->PartitionLen > Vcb->FSBM_BitCount ||
                        Lba < Part->PartitionRoot || Lba - Part->PartitionRoot >= Part->PartitionLen ||
                        Blocks > Part->PartitionLen - (Lba - Part->PartitionRoot) ||
                        (Vcb->MetadataFileInfo && !View)) {
                        Status = STATUS_FILE_CORRUPT_ERROR;
                        goto retirement_exit;
                    }
                    /* No physical free may cross either metadata payload. */
                    if (View) {
                        const EXTENT_MAP* Map;
                        uint32 Copy, Count;
                        for (Copy = 0; Copy < 2; Copy++) {
                            Map = Copy ? View->MirrorMap : View->MainMap;
                            Count = Copy ? View->MirrorCount : View->MainCount;
                            for (j = 0; j < Count; j++) {
                                uint64 End = (uint64)Map[j].extLocation +
                                    ((Map[j].extLength & UDF_EXTENT_LENGTH_MASK) >> View->SectorShift);
                                if ((uint64)Lba < End &&
                                    (uint64)Map[j].extLocation < (uint64)Lba + Blocks) {
                                    Status = STATUS_FILE_CORRUPT_ERROR;
                                    goto retirement_exit;
                                }
                            }
                        }
                    }
                    Retired->Uncertain = TRUE;
                    /* The replacement FE is already durable. Bypass any
                     * unrelated ambient truncate ledger using a stack copy. */
                    UDFMarkSpaceAsXXXNoProtect(Vcb, 0, One,
                        AS_DISCARDED | AS_PHYSICAL | AS_PRIVATE_ROLLBACK);
                }
                Retired->Uncertain = FALSE;
                Retired->Error = STATUS_SUCCESS;
                Retired->Completed++;
            }
            Dloc->RetiredExtents = Retired->Next;
            InterlockedDecrement(&Vcb->PendingExtentRetirements);
            MyFreePool__(Retired);
        }
retirement_exit:
        NOTHING;
    } _SEH2_EXCEPT(FsRtlIsNtstatusExpected(_SEH2_GetExceptionCode()) ?
                  EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        Status = _SEH2_GetExceptionCode();
        if (Dloc->RetiredExtents && Dloc->RetiredExtents->Uncertain)
            Dloc->RetiredExtents->Error = Status;
    } _SEH2_END;
    } _SEH2_FINALLY {
        if (_SEH2_AbnormalTermination() || !NT_SUCCESS(Status))
            Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
        UDFReleaseMetadataView(View);
        UDFUnpinBitmapPage(Vcb);
        UDFReleaseResource(&Vcb->BitMapResource1);
    } _SEH2_END;
    return Status;
}

NTSTATUS
UDFResizeExtent(PIRP_CONTEXT IrpContext, PVCB Vcb, PUDF_FILE_INFO FileInfo,
                 uint32 PartNum, int64 Length, BOOLEAN AlwaysInIcb, PEXTENT_INFO ExtInfo)
{
    if (!Vcb || !FileInfo || !FileInfo->Dloc || !ExtInfo ||
        !Vcb->Partitions || PartNum >= Vcb->PartitionMaps ||
        !Vcb->SectorSize || Vcb->SectorShift >= 30 ||
        Vcb->SectorSize != ((uint32)1 << Vcb->SectorShift) ||
        (ExtInfo != &FileInfo->Dloc->DataLoc && ExtInfo != &FileInfo->Dloc->AllocLoc) ||
        (AlwaysInIcb && ExtInfo != &FileInfo->Dloc->AllocLoc) ||
        Length < 0 || ExtInfo->Length < 0)
        return STATUS_INVALID_PARAMETER;
    if (Length == ExtInfo->Length && !(ExtInfo->Flags & EXTENT_FLAG_CUT_PREALLOCATED))
        return STATUS_SUCCESS;
    if (Vcb->VcbState & VCB_STATE_VOLUME_READ_ONLY)
        return STATUS_MEDIA_WRITE_PROTECTED;
    if (Vcb->Vat || Vcb->CDR_Mode)
        return STATUS_NOT_SUPPORTED;
    if (Length > ExtInfo->Length)
        return UDFGrowExtentPrivate(IrpContext, Vcb, PartNum, Length, AlwaysInIcb, ExtInfo);
    return UDFShrinkExtentPrivate(Vcb, FileInfo, PartNum, Length, AlwaysInIcb, ExtInfo);
} // end UDFResizeExtent()


/*
    This routine (re)builds AllocDescs data for all allocation modes except
    in-ICB & resizes associated extent (FileInfo->Dloc->AllocLoc) for
    already allocated user data extent (FileInfo->Dloc->DataLoc).
    AllocMode in FileEntry pointed by FileInfo must be already initialized.
 */
NTSTATUS
UDFBuildAllocDescs(PIRP_CONTEXT IrpContext, PVCB Vcb, uint32 PartNum,
    PUDF_FILE_INFO FileInfo, int8** AllocData, uint32* Capacity)
{
    uint16 Mode;
    uint32 Initial, Header;
    NTSTATUS Status;
    if (!AllocData || !Capacity) return STATUS_INVALID_PARAMETER;
    *AllocData = NULL;
    *Capacity = 0;
    if (!Vcb || !FileInfo || !FileInfo->Dloc || !FileInfo->Dloc->FileEntry ||
        FileInfo->Dloc->FileEntryLen > Vcb->SectorSize)
        return STATUS_FILE_CORRUPT_ERROR;
    Status = UDFValidateFileEntryBuffer(Vcb, FileInfo, &Header);
    if (!NT_SUCCESS(Status)) return Status;
    Initial = Vcb->SectorSize - Header;
    Mode = ((PFILE_ENTRY)FileInfo->Dloc->FileEntry)->icbTag.flags & ICB_FLAG_ALLOC_MASK;
    if (Mode == ICB_FLAG_AD_IN_ICB)
        return FileInfo->Dloc->AllocLoc.Mapping ? STATUS_FILE_CORRUPT_ERROR : STATUS_SUCCESS;
    if (Mode == ICB_FLAG_AD_SHORT)
        return UDFBuildShortAllocDescs(IrpContext, Vcb, PartNum, AllocData, Initial, FileInfo, Capacity);
    if (Mode == ICB_FLAG_AD_LONG)
        return UDFBuildLongAllocDescs(IrpContext, Vcb, PartNum, AllocData, Initial, FileInfo, Capacity);
    return STATUS_NOT_SUPPORTED;
} // end UDFBuildAllocDescs()

/*
    This routine discards file's allocation
 */
static VOID
UDFFreeFileAllocationBody(
    IN PVCB Vcb,
    IN PUDF_FILE_INFO DirInfo,
    IN PUDF_FILE_INFO FileInfo
    )
{
    if (FileInfo->Dloc->DataLoc.Offset) {
        // in-ICB data
        if (FileInfo->Dloc->DataLoc.Mapping) {
            ASSERT(FileInfo->Dloc->FELoc.Mapping[0].extLocation ==
                   FileInfo->Dloc->DataLoc.Mapping[0].extLocation);
            UDFMarkSpaceAsXXX(Vcb, FileInfo->Dloc, &(FileInfo->Dloc->DataLoc.Mapping[1]), AS_DISCARDED); // free
            FileInfo->Dloc->DataLoc.Mapping[1].extLocation =
            FileInfo->Dloc->DataLoc.Mapping[1].extLength = 0;
            FileInfo->Dloc->DataLoc.Mapping[0].extLocation = 0;
            FileInfo->Dloc->DataLoc.Mapping[0].extLength = EXTENT_NOT_RECORDED_NOT_ALLOCATED << 30;
        }
        if (FileInfo->Dloc->AllocLoc.Mapping) {
            ASSERT(FileInfo->Dloc->FELoc.Mapping[0].extLocation ==
                   FileInfo->Dloc->AllocLoc.Mapping[0].extLocation);
            UDFMarkSpaceAsXXX(Vcb, FileInfo->Dloc, &(FileInfo->Dloc->AllocLoc.Mapping[1]), AS_DISCARDED); // free
            FileInfo->Dloc->AllocLoc.Mapping[1].extLocation =
            FileInfo->Dloc->AllocLoc.Mapping[1].extLength = 0;
            FileInfo->Dloc->AllocLoc.Mapping[0].extLocation = 0;
            FileInfo->Dloc->AllocLoc.Mapping[0].extLength = EXTENT_NOT_RECORDED_NOT_ALLOCATED << 30;
        }
        UDFFreeFESpace(Vcb, DirInfo, &(FileInfo->Dloc->FELoc));
    } else {
        if (FileInfo->Dloc->AllocLoc.Mapping) {
            ASSERT(FileInfo->Dloc->FELoc.Mapping[0].extLocation ==
                   FileInfo->Dloc->AllocLoc.Mapping[0].extLocation);
            UDFMarkSpaceAsXXX(Vcb, FileInfo->Dloc, &(FileInfo->Dloc->AllocLoc.Mapping[1]), AS_DISCARDED); // free
            FileInfo->Dloc->AllocLoc.Mapping[1].extLocation =
            FileInfo->Dloc->AllocLoc.Mapping[1].extLength = 0;
            FileInfo->Dloc->AllocLoc.Mapping[0].extLocation = 0;
            FileInfo->Dloc->AllocLoc.Mapping[0].extLength = EXTENT_NOT_RECORDED_NOT_ALLOCATED << 30;
        }
        UDFFreeFESpace(Vcb, DirInfo, &(FileInfo->Dloc->FELoc));
        UDFMarkSpaceAsXXX(Vcb, FileInfo->Dloc, FileInfo->Dloc->DataLoc.Mapping, AS_DISCARDED); // free
    }
}

static NTSTATUS
UDFCheckReleasedAllocationMap(PEXTENT_MAP Mapping)
{
    uint32 Bytes, i;
    NTSTATUS Status;
    if (!Mapping) return STATUS_SUCCESS;
    Status = UDFGetMappingByteCount(Mapping, &Bytes);
    if (Status != STATUS_SUCCESS) return Status;
    for (i = 0; i + 1 < Bytes / sizeof(*Mapping); i++)
        if ((Mapping[i].extLength >> 30) != EXTENT_NOT_RECORDED_NOT_ALLOCATED)
            return STATUS_IO_DEVICE_ERROR;
    return STATUS_SUCCESS;
}

/* Only a confirmed final deletion with no remaining FI/cache/FCB owner can
 * retire these maps. Keep its Dloc indexed/claimed through every fallible
 * bitmap operation. An uncertain release is never replayed as a fresh free. */
NTSTATUS
UDFFreeFileAllocation(PVCB Vcb, PUDF_FILE_INFO DirInfo, PUDF_FILE_INFO FileInfo)
{
    PUDF_DATALOC_INFO Dloc = FileInfo ? FileInfo->Dloc : NULL;
    _SEH2_VOLATILE NTSTATUS Status = STATUS_SUCCESS;
    _SEH2_VOLATILE BOOLEAN Locked = FALSE, Active = FALSE;
    if (!Dloc || !ExIsResourceAcquiredExclusiveLite(&Vcb->VcbResource) ||
        Dloc->CleanupOwner != PsGetCurrentThread() || !Dloc->DeleteCommitted ||
        FileInfo->RefCount || FileInfo->OpenCount || FileInfo->Fcb ||
        Dloc->CommonFcb || Dloc->LinkRefCount || !Dloc->FELoc.Mapping)
        return STATUS_FILE_CORRUPT_ERROR;
    if (Dloc->AllocationReleaseActive) return STATUS_DEVICE_BUSY;
    if (!NT_SUCCESS(Dloc->AllocationReleaseError)) return Dloc->AllocationReleaseError;
    if (Vcb->Vat || Vcb->CDR_Mode) return STATUS_NOT_SUPPORTED;
    if (Vcb->TruncateTxnOwner == PsGetCurrentThread())
        return STATUS_DEVICE_BUSY; /* never donate this delete to another truncate ledger */
    _SEH2_TRY {
    _SEH2_TRY {
        UDFAcquireResourceExclusive(&Vcb->BitMapResource1, TRUE);
        Locked = TRUE;
        Status = UDFAllocationFailureStatus(Vcb);
        if (Status == STATUS_SUCCESS && (Vcb->VcbState & VCB_STATE_VOLUME_READ_ONLY))
            Status = STATUS_MEDIA_WRITE_PROTECTED;
        if (Status != STATUS_SUCCESS) _SEH2_LEAVE;
        Dloc->AllocationReleaseActive = TRUE;
        Active = TRUE;
        UDFFreeFileAllocationBody(Vcb, DirInfo, FileInfo);
        Status = UDFCheckReleasedAllocationMap(Dloc->DataLoc.Mapping);
        if (Status == STATUS_SUCCESS) Status = UDFCheckReleasedAllocationMap(Dloc->AllocLoc.Mapping);
        if (Status == STATUS_SUCCESS) Status = UDFCheckReleasedAllocationMap(Dloc->FELoc.Mapping);
        if (Status == STATUS_SUCCESS) {
            Dloc->DataLoc.Flags &= ~(EXTENT_FLAG_PREALLOCATED | EXTENT_FLAG_CUT_PREALLOCATED);
            Dloc->DataLoc.Modified = Dloc->AllocLoc.Modified = Dloc->FELoc.Modified = FALSE;
        }
    } _SEH2_EXCEPT(FsRtlIsNtstatusExpected(_SEH2_GetExceptionCode()) ?
                  EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        Status = _SEH2_GetExceptionCode();
    } _SEH2_END;
    } _SEH2_FINALLY {
        if (Active) {
            if (_SEH2_AbnormalTermination()) Status = STATUS_UNSUCCESSFUL;
            if (Status != STATUS_SUCCESS) {
                if (NT_SUCCESS(Status)) Status = STATUS_IO_DEVICE_ERROR;
                InterlockedCompareExchange(&Dloc->AllocationReleaseError, Status, STATUS_SUCCESS);
                Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
                InterlockedCompareExchange(&Vcb->MetadataWritebackError, Status, STATUS_SUCCESS);
            }
            Dloc->AllocationReleaseActive = FALSE;
        }
        if (Locked) UDFReleaseResource(&Vcb->BitMapResource1);
    } _SEH2_END;
    return Status;
}

/*
    This routine packs physically sequential extents into single one
 */
static BOOLEAN
UDFCanMergeMappingRuns(PVCB Vcb, const EXTENT_MAP* Left, const EXTENT_MAP* Right)
{
    uint32 LeftLength = Left->extLength & UDF_EXTENT_LENGTH_MASK;
    uint32 RightLength = Right->extLength & UDF_EXTENT_LENGTH_MASK;
    uint32 Type = Left->extLength >> 30;
    uint32 Limit = ALIGN_DOWN_BY(UDF_EXTENT_LENGTH_MASK, Vcb->SectorSize);
    if (!LeftLength || !RightLength || Type > EXTENT_NOT_RECORDED_NOT_ALLOCATED ||
        Type != (Right->extLength >> 30) ||
        (LeftLength & (Vcb->SectorSize - 1)) ||
        (uint64)LeftLength + RightLength > Limit)
        return FALSE;
    return Type == EXTENT_NOT_RECORDED_NOT_ALLOCATED ||
        (uint64)Left->extLocation + (LeftLength >> Vcb->SectorShift) == Right->extLocation;
}

void
__fastcall
UDFPackMapping(IN PVCB Vcb, IN PEXTENT_INFO ExtInfo)
{
    PEXTENT_MAP OldMap, NewMap;
    EXTENT_MAP Run;
    uint32 OldSize, NewSize, Count, Runs, i, j;

    if (!Vcb || !ExtInfo || !ExtInfo->Mapping || !Vcb->SectorSize ||
        Vcb->SectorShift >= 32 || Vcb->SectorSize != ((uint32)1 << Vcb->SectorShift))
        return;
    OldMap = ExtInfo->Mapping;
    if (!NT_SUCCESS(UDFGetMappingByteCount(OldMap, &OldSize)))
        return; /* Optional packing never consumes an invalid/unknown owner. */
    Count = OldSize / sizeof(EXTENT_MAP) - 1;
    if (Count < 2)
        return;
    Runs = 1;
    Run = OldMap[0];
    for (i = 1; i < Count; i++) {
        if (UDFCanMergeMappingRuns(Vcb, &Run, &OldMap[i])) {
            Run.extLength += OldMap[i].extLength & UDF_EXTENT_LENGTH_MASK;
        } else {
            Run = OldMap[i];
            Runs++;
        }
    }
    NewSize = (Runs + 1) * sizeof(EXTENT_MAP);
    if (NewSize == OldSize ||
        (OldSize - NewSize <= PACK_MAPPING_THRESHOLD && NewSize >= PACK_MAPPING_THRESHOLD))
        return;
    NewMap = (PEXTENT_MAP)MyAllocatePoolTag__(NonPagedPool, NewSize, MEM_EXTMAP_TAG);
    if (!NewMap)
        return; /* The original map remains fully owned on OOM. */
    j = 0;
    NewMap[0] = OldMap[0];
    for (i = 1; i < Count; i++) {
        if (UDFCanMergeMappingRuns(Vcb, &NewMap[j], &OldMap[i]))
            NewMap[j].extLength += OldMap[i].extLength & UDF_EXTENT_LENGTH_MASK;
        else
            NewMap[++j] = OldMap[i];
    }
    NewMap[j + 1].extLength = NewMap[j + 1].extLocation = 0;
    ExtInfo->Mapping = NewMap;
    MyFreePool__(OldMap);
} // end UDFPackMapping()

/*
    This routine expands mapping to 'frag-per-LBlock' state
 */
NTSTATUS
__fastcall
UDFUnPackMapping(
    IN PVCB Vcb,
    IN PEXTENT_INFO ExtInfo   // Extent array
    )
{
    PEXTENT_MAP Mapping, NewMapping;
    uint32 Bytes, Count, i, j = 0, Length, Chunk, Type;
    uint64 Entries = 0, Blocks, Physical;
    NTSTATUS Status;

    if (!Vcb || !ExtInfo || !ExtInfo->Mapping || Vcb->SectorShift >= 32 ||
        !Vcb->SectorSize || Vcb->SectorSize != ((uint32)1 << Vcb->SectorShift))
        return STATUS_INVALID_PARAMETER;
    Mapping = ExtInfo->Mapping;
    Status = UDFGetMappingByteCount(Mapping, &Bytes);
    if (!NT_SUCCESS(Status))
        return Status;
    Count = Bytes / sizeof(EXTENT_MAP) - 1;
    for (i = 0; i < Count; i++) {
        Length = Mapping[i].extLength & UDF_EXTENT_LENGTH_MASK;
        Blocks = ((uint64)Length + Vcb->SectorSize - 1) >> Vcb->SectorShift;
        Type = Mapping[i].extLength >> 30;
        if (Type != EXTENT_NOT_RECORDED_NOT_ALLOCATED &&
            (uint64)Mapping[i].extLocation + Blocks > LBA_OUT_OF_EXTENT)
            return STATUS_FILE_CORRUPT_ERROR;
        if (i + 1 < Count && (Length & (Vcb->SectorSize - 1)))
            return STATUS_FILE_CORRUPT_ERROR;
        Entries += Blocks;
        if (Entries + 1 > (MAXULONG - MY_HEAP_ALIGN) / sizeof(EXTENT_MAP))
            return STATUS_INSUFFICIENT_RESOURCES;
    }
    NewMapping = (PEXTENT_MAP)MyAllocatePoolTag__(NonPagedPool,
                            (Entries + 1) * sizeof(EXTENT_MAP), MEM_EXTMAP_TAG);
    if (!NewMapping)
        return STATUS_INSUFFICIENT_RESOURCES;
    for (i = 0; i < Count; i++) {
        Length = Mapping[i].extLength & UDF_EXTENT_LENGTH_MASK;
        Type = Mapping[i].extLength >> 30;
        Physical = Mapping[i].extLocation;
        while (Length) {
            Chunk = min(Length, Vcb->SectorSize);
            NewMapping[j].extLength = Chunk | (Type << 30);
            NewMapping[j++].extLocation =
                Type == EXTENT_NOT_RECORDED_NOT_ALLOCATED ? 0 : (uint32)Physical;
            Length -= Chunk;
            Physical++;
        }
    }
    NewMapping[j].extLength = NewMapping[j].extLocation = 0;
    ExtInfo->Mapping = NewMapping;
    MyFreePool__(Mapping);
    return STATUS_SUCCESS;
} // end UDFUnPackMapping()

/*
    Relocate a part of extent that starts from relative (inside extent)
    block number 'ExtBlock' and has length of 'BC' blocks to continuous
    run which starts at block 'Lba'
 */
NTSTATUS
UDFRelocateExtent(
    IN PVCB Vcb,
    IN PEXTENT_INFO ExtInfo,
    IN uint32 ExtBlock,
    IN uint32 Lba,
    IN uint32 BC
    )
{
    return STATUS_ACCESS_DENIED;
}

/*
    This routine reads cached data only.
 */
/*NTSTATUS
UDFReadExtentCached(
    IN PVCB Vcb,
    IN PEXTENT_INFO ExtInfo,   // Extent array
    IN int64 Offset,      // offset in extent
    IN uint32 Length,
    OUT int8* Buffer,
    OUT PSIZE_T ReadBytes
    )
{
    (*ReadBytes) = 0;
    if (!ExtInfo || !ExtInfo->Mapping) return STATUS_INVALID_PARAMETER;

    PEXTENT_MAP Extent = ExtInfo->Mapping;   // Extent array
    uint32 to_read, Lba, sect_offs, flags;
    SIZE_T _ReadBytes;
    NTSTATUS status;
    // prevent reading out of data space
    if (Offset > ExtInfo->Length) return STATUS_END_OF_FILE;
    if (Offset+Length > ExtInfo->Length) Length = (uint32)(ExtInfo->Length - Offset);
    Offset += ExtInfo->Offset;               // used for in-ICB data
    // read maximal possible part of each frag of extent
    while(((LONG)Length) > 0) {
        Lba = UDFExtentOffsetToLba(Vcb, Extent, Offset, &sect_offs, &to_read, &flags, NULL);
        // EOF check
        if (Lba == LBA_OUT_OF_EXTENT) return STATUS_END_OF_FILE;
        // check for reading tail
        to_read = (to_read < Length) ?
                   to_read : Length;
        if (flags == EXTENT_RECORDED_ALLOCATED) {
            status = UDFReadDataCached(Vcb, TRUE, ( ((uint64)Lba) << Vcb->BlockSizeBits) + sect_offs, to_read, Buffer, &_ReadBytes);
            (*ReadBytes) += _ReadBytes;
        } else {
            RtlZeroMemory(Buffer, to_read);
            (*ReadBytes) += to_read;
            status = STATUS_SUCCESS;
        }
        if (!NT_SUCCESS(status)) return status;
        // prepare for reading next frag...
        Buffer += to_read;
        Offset += to_read;
        Length -= to_read;
    }
    return STATUS_SUCCESS;
} // end UDFReadExtentCached()*/

/*
    This routine reads data at any offset from specified extent.
 */
NTSTATUS
UDFReadExtent(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN PEXTENT_INFO ExtInfo, // Extent array
    IN int64 Offset,      // offset in extent
    IN SIZE_T Length,
    IN BOOLEAN Direct,
    OUT int8* Buffer
    )
{
    PEXTENT_MAP Extent;
    SIZE_T ToRead;
    uint32 Lba, SectorOffset, Flags, Index;
    NTSTATUS Status;

    if (!Vcb || !ExtInfo || !ExtInfo->Mapping || Offset < 0 || (Length && !Buffer))
        return STATUS_INVALID_PARAMETER;
    if (ExtInfo->Length < 0)
        return STATUS_FILE_CORRUPT_ERROR;
    if (Offset > ExtInfo->Length)
        return STATUS_END_OF_FILE;
    if ((uint64)Length > (uint64)(ExtInfo->Length - Offset))
        Length = (SIZE_T)(ExtInfo->Length - Offset);
    if (!Length)
        return STATUS_SUCCESS;
    if ((uint64)Offset + ExtInfo->Offset > MAXLONGLONG)
        return STATUS_FILE_CORRUPT_ERROR;
    Offset += ExtInfo->Offset;
    Extent = ExtInfo->Mapping;
    Lba = UDFExtentOffsetToLba(Vcb, Extent, Offset,
                              &SectorOffset, &ToRead, &Flags, &Index);
    while (Length) {
        /* EOF was already bounded by informationLength. A missing run here
         * is corrupt mapping, not a successful short/zero-filled read. */
        if (Lba == LBA_OUT_OF_EXTENT || Index == MAXULONG || !ToRead ||
            Flags > EXTENT_NOT_RECORDED_NOT_ALLOCATED)
            return STATUS_FILE_CORRUPT_ERROR;
        Extent += Index + 1;
        ToRead = min(ToRead, Length);
        if (Flags == EXTENT_RECORDED_ALLOCATED) {
            Status = UDFReadData(IrpContext, Vcb, TRUE,
                (((uint64)Lba) << Vcb->SectorShift) + SectorOffset,
                ToRead, Direct, Buffer);
            if (!NT_SUCCESS(Status))
                return Status;
        } else {
            RtlZeroMemory(Buffer, ToRead);
        }
        Length -= ToRead;
        if (!Length)
            break;
        Buffer += ToRead;
        Lba = UDFNextExtentToLba(Vcb, Extent, &ToRead, &Flags, &Index);
        SectorOffset = 0;
    }
    return STATUS_SUCCESS;
} // end UDFReadExtent()

/*
    This routine reads and builds mapping for
    specified amount of data at any offset from specified extent.
    Size of output buffer is limited by *_SubExtInfoSz
 */
NTSTATUS
UDFReadExtentLocation(
    IN PVCB Vcb,
    IN PEXTENT_INFO ExtInfo,      // Extent array
    IN int64 Offset,              // offset in extent to start SubExtent from
    OUT PEXTENT_MAP* _SubExtInfo, // SubExtent mapping array
 IN OUT uint32* _SubExtInfoSz,    // IN:  maximum number of fragments to get
                                  // OUT: actually obtained fragments
    OUT int64* _NextOffset        // offset, caller can start from to continue
    )
{
    PEXTENT_MAP Extent, SubExtent;
    SIZE_T Available;
    uint32 Lba, SectorOffset, Flags, Index, Count = 0, Capacity;
    int64 Remaining, NextOffset;
    NTSTATUS Status;

    if (!_SubExtInfo || !_SubExtInfoSz || !_NextOffset)
        return STATUS_INVALID_PARAMETER;
    Capacity = *_SubExtInfoSz;
    *_SubExtInfo = NULL;
    *_SubExtInfoSz = 0;
    *_NextOffset = Offset;
    if (!Vcb || !ExtInfo || !ExtInfo->Mapping || Offset < 0 ||
        !Vcb->SectorSize || Vcb->SectorShift >= 32)
        return STATUS_INVALID_PARAMETER;
    /* A byte inside an FE has no sector-aligned retrieval mapping. Do not
     * expose the descriptor sector as if it were an ordinary data cluster. */
    if (ExtInfo->Offset)
        return STATUS_NOT_SUPPORTED;
    if (Offset & (Vcb->SectorSize - 1))
        return STATUS_INVALID_PARAMETER;
    if (ExtInfo->Length < 0)
        return STATUS_FILE_CORRUPT_ERROR;
    if (Offset >= ExtInfo->Length)
        return STATUS_END_OF_FILE;
    if (!Capacity)
        return STATUS_BUFFER_TOO_SMALL;
    if (Capacity > (MAXULONG - MY_HEAP_ALIGN) / sizeof(EXTENT_MAP) - 1)
        return STATUS_INVALID_PARAMETER;
    SubExtent = (PEXTENT_MAP)MyAllocatePoolTag__(NonPagedPool,
                       (Capacity + 1) * sizeof(EXTENT_MAP), MEM_EXTMAP_TAG);
    if (!SubExtent)
        return STATUS_INSUFFICIENT_RESOURCES;

    Extent = ExtInfo->Mapping;
    Remaining = ExtInfo->Length - Offset;
    NextOffset = Offset;
    Lba = UDFExtentOffsetToLba(Vcb, Extent, Offset,
                             &SectorOffset, &Available, &Flags, &Index);
    while (Remaining && Count < Capacity) {
        if (Lba == LBA_OUT_OF_EXTENT || Index == MAXULONG || !Available ||
            SectorOffset || Flags > EXTENT_NOT_RECORDED_NOT_ALLOCATED) {
            MyFreePool__(SubExtent);
            return STATUS_FILE_CORRUPT_ERROR;
        }
        Extent += Index + 1;
        if ((uint64)Available > (uint64)Remaining)
            Available = (SIZE_T)Remaining;
        /* Keep type bits in extLength. Bit 31 of a real physical LBA is
         * address data, never an unrecorded/sparse flag. This is a counted
         * private output map, plus a terminator, committed only on success
         * or a deliberate BUFFER_OVERFLOW carrying complete entries. */
        SubExtent[Count].extLength = (uint32)Available | (Flags << 30);
        SubExtent[Count].extLocation =
            Flags == EXTENT_NOT_RECORDED_NOT_ALLOCATED ? 0 : Lba;
        Count++;
        NextOffset += Available;
        Remaining -= Available;
        if (!Remaining || Count == Capacity)
            break;
        Lba = UDFNextExtentToLba(Vcb, Extent, &Available, &Flags, &Index);
        SectorOffset = 0;
    }
    SubExtent[Count].extLength = SubExtent[Count].extLocation = 0;
    Status = Remaining ? STATUS_BUFFER_OVERFLOW : STATUS_SUCCESS;
    *_SubExtInfo = SubExtent;
    *_SubExtInfoSz = Count;
    *_NextOffset = NextOffset;
    return Status;
} // end UDFReadExtentLocation()

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4035)               // re-enable below
#endif

uint32
UDFGetZeroLength(
    IN int8* Buffer,
    IN uint32 Length
    )
{
    uint32 i;
    Length /= sizeof(uint32);
    for(i=0; i<Length; i++) {
        if ( ((uint32*)Buffer)[i] )
            break;
    }
    return Length*sizeof(uint32);
}

#ifdef _MSC_VER
#pragma warning(pop) // re-enable warning #4035
#endif

/*
    This routine writes data at any offset to specified extent.
 */
NTSTATUS
UDFWriteExtent(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN PEXTENT_INFO ExtInfo,
    IN int64 Offset,
    IN SIZE_T Length,
    IN BOOLEAN Direct,
    IN int8* Buffer,
    OUT PSIZE_T WrittenBytes
    )
{
    uint32 Lba, SectorOffset, Flags, BaseLba, BaseFlags;
    SIZE_T ToWrite, Written, PaddingWritten, Available;
    uint64 Cover;
    NTSTATUS Status;
    BOOLEAN Activate;
    int8* SectorBuffer;

    if (WrittenBytes) *WrittenBytes = 0;
    if (!Vcb || !ExtInfo || !ExtInfo->Mapping || !WrittenBytes || Offset < 0 ||
        !Vcb->SectorSize || Vcb->SectorShift >= 30 ||
        Vcb->SectorSize != ((uint32)1 << Vcb->SectorShift) ||
        (Length && !Buffer) || (uint64)Offset + ExtInfo->Offset > MAXLONGLONG ||
        (uint64)Length > MAXLONGLONG - ((uint64)Offset + ExtInfo->Offset))
        return STATUS_INVALID_PARAMETER;
    Offset += ExtInfo->Offset;
    while (Length) {
        Lba = UDFExtentOffsetToLba(Vcb, ExtInfo->Mapping, Offset,
                                  &SectorOffset, &ToWrite, &Flags, NULL);
        if (Lba == LBA_OUT_OF_EXTENT) return STATUS_END_OF_FILE;
        if (!ToWrite || Flags > EXTENT_NOT_RECORDED_NOT_ALLOCATED)
            return STATUS_FILE_CORRUPT_ERROR;
        ToWrite = min(ToWrite, Length);
        if (Flags == EXTENT_NOT_RECORDED_NOT_ALLOCATED) {
            Status = UDFMarkNotAllocatedAsAllocated(IrpContext, Vcb, Offset, ToWrite, ExtInfo);
            if (!NT_SUCCESS(Status)) return Status;
            Lba = UDFExtentOffsetToLba(Vcb, ExtInfo->Mapping, Offset,
                                      &SectorOffset, &ToWrite, &Flags, NULL);
            if (Lba == LBA_OUT_OF_EXTENT || !ToWrite ||
                Flags >= EXTENT_NOT_RECORDED_NOT_ALLOCATED)
                return STATUS_FILE_CORRUPT_ERROR;
            ToWrite = min(ToWrite, Length);
        }
        Activate = Flags == EXTENT_NOT_RECORDED_ALLOCATED;
        if (Activate) {
            /* Keep the descriptor unrecorded until every requested byte and
             * both partial-sector tails have completed. A failed/short write
             * remains logically zero and cannot publish old allocation bytes. */
            if (Vcb->Vat || Vcb->CDR_Mode || (Vcb->VcbState & VCB_STATE_VOLUME_READ_ONLY))
                return STATUS_MEDIA_WRITE_PROTECTED;
            if (SectorOffset >= Vcb->SectorSize || ToWrite > UDF_EXTENT_LENGTH_MASK)
                return STATUS_FILE_CORRUPT_ERROR;
            Cover = ((uint64)SectorOffset + ToWrite + Vcb->SectorSize - 1) &
                    ~((uint64)Vcb->SectorSize - 1);
            BaseLba = UDFExtentOffsetToLba(Vcb, ExtInfo->Mapping,
                Offset & ~((int64)Vcb->SectorSize - 1), NULL, &Available, &BaseFlags, NULL);
            if (BaseLba != Lba || BaseFlags != EXTENT_NOT_RECORDED_ALLOCATED || Cover > Available)
                return STATUS_FILE_CORRUPT_ERROR;
            if (SectorOffset + ToWrite <= Vcb->SectorSize &&
                (SectorOffset || ToWrite < Vcb->SectorSize)) {
                SectorBuffer = (int8*)MyAllocatePool__(NonPagedPool, Vcb->SectorSize);
                if (SectorBuffer) {
                    _SEH2_TRY {
                        RtlZeroMemory(SectorBuffer, Vcb->SectorSize);
                        RtlCopyMemory(SectorBuffer + SectorOffset, Buffer, ToWrite);
                        Status = UDFWriteData(IrpContext, Vcb, TRUE,
                            ((uint64)Lba) << Vcb->SectorShift, Vcb->SectorSize,
                            Direct, SectorBuffer, &Written);
                    } _SEH2_FINALLY {
                        MyFreePool__(SectorBuffer);
                    } _SEH2_END;
                    if (!NT_SUCCESS(Status)) return Status;
                    if (Written != Vcb->SectorSize) return STATUS_IO_DEVICE_ERROR;
                    goto activate_written_range;
                }
            }
            /* Scratch OOM retains the older bounded padding fallback. Check
             * exact completed bytes before allowing a recorded transition. */
            if (SectorOffset || SectorOffset + ToWrite < Vcb->SectorSize) {
                Status = UDFWriteData(IrpContext, Vcb, TRUE,
                    ((uint64)Lba) << Vcb->SectorShift, Vcb->SectorSize,
                    Direct, Vcb->ZBuffer, &PaddingWritten);
                if (!NT_SUCCESS(Status)) return Status;
                if (PaddingWritten != Vcb->SectorSize) return STATUS_IO_DEVICE_ERROR;
            }
            if (SectorOffset + ToWrite > Vcb->SectorSize &&
                ((SectorOffset + ToWrite) & (Vcb->SectorSize - 1))) {
                uint64 LastSector = ((((uint64)Lba) << Vcb->SectorShift) +
                    SectorOffset + ToWrite) & ~((uint64)Vcb->SectorSize - 1);
                Status = UDFWriteData(IrpContext, Vcb, TRUE, LastSector,
                    Vcb->SectorSize, Direct, Vcb->ZBuffer, &PaddingWritten);
                if (!NT_SUCCESS(Status)) return Status;
                if (PaddingWritten != Vcb->SectorSize) return STATUS_IO_DEVICE_ERROR;
            }
        }
        Status = UDFWriteData(IrpContext, Vcb, TRUE,
            (((uint64)Lba) << Vcb->SectorShift) + SectorOffset,
            ToWrite, Direct, Buffer, &Written);
        if (Written > ToWrite) return STATUS_IO_DEVICE_ERROR;
        if (!Activate) *WrittenBytes += Written;
        if (!NT_SUCCESS(Status)) return Status;
        if (Written != ToWrite) return STATUS_IO_DEVICE_ERROR;
        if (Activate) {
activate_written_range:
            Status = UDFMarkAllocatedAsRecorded(Vcb, Offset, (uint32)ToWrite, ExtInfo);
            if (!NT_SUCCESS(Status)) return Status;
            *WrittenBytes += ToWrite;
        }
        Buffer += ToWrite;
        Offset += ToWrite;
        Length -= ToWrite;
    }
    return STATUS_SUCCESS;
} // end UDFWriteExtent()

//#if 0
/*
    This routine zeroes/deallocates data at any offset to specified extent.
 */
NTSTATUS
UDFZeroExtent(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN PEXTENT_INFO ExtInfo,   // Extent array
    IN int64 Offset,           // offset in extent
    IN SIZE_T Length,
    IN BOOLEAN Deallocate,     // deallocate frag or just mark as unrecorded
    IN BOOLEAN Direct,         // setting this flag delays flushing of given
                               // data to indefinite term
    OUT PSIZE_T WrittenBytes
    )
{
    uint32 Lba, SectorOffset, Flags;
    SIZE_T Available, Count, Completed, SectorSize;
    int64 MapOffset;
    NTSTATUS Status;

    if (WrittenBytes) *WrittenBytes = 0;
    if (!Vcb || !ExtInfo || !ExtInfo->Mapping || !WrittenBytes || Offset < 0 ||
        (uint64)Offset + ExtInfo->Offset > MAXLONGLONG ||
        (uint64)Length > MAXLONGLONG - ((uint64)Offset + ExtInfo->Offset))
        return STATUS_INVALID_PARAMETER;
    MapOffset = Offset + ExtInfo->Offset;
    SectorSize = Vcb->SectorSize;
    while (Length) {
        Lba = UDFExtentOffsetToLba(Vcb, ExtInfo->Mapping, MapOffset,
                                  &SectorOffset, &Available, &Flags, NULL);
        if (Lba == LBA_OUT_OF_EXTENT)
            return STATUS_END_OF_FILE;
        if (!Available || Flags > EXTENT_NOT_RECORDED_NOT_ALLOCATED)
            return STATUS_FILE_CORRUPT_ERROR;
        Count = min(Available, Length);
        if (Flags == EXTENT_NOT_RECORDED_NOT_ALLOCATED) {
            /* A hole is already zero and owns no sectors to release. */
        } else if (SectorOffset || Count < SectorSize) {
            Count = min(Count, SectorSize - SectorOffset);
            if (Flags == EXTENT_RECORDED_ALLOCATED) {
                Completed = 0;
                Status = UDFWriteData(IrpContext, Vcb, TRUE,
                    (((uint64)Lba) << Vcb->SectorShift) + SectorOffset,
                    Count, Direct, Vcb->ZBuffer, &Completed);
                if (Completed > Count)
                    return STATUS_IO_DEVICE_ERROR;
                *WrittenBytes += Completed;
                if (!NT_SUCCESS(Status))
                    return Status;
                if (Completed != Count)
                    return STATUS_IO_DEVICE_ERROR;
                MapOffset += Count;
                Length -= Count;
                continue;
            }
            /* An unrecorded partial sector already reads as zero. Do not
             * deallocate neighboring bytes outside the requested window. */
        } else {
            Count &= ~(SectorSize - 1);
            if (Deallocate || Flags == EXTENT_RECORDED_ALLOCATED) {
                Status = UDFMarkAllocatedAsNotXXX(Vcb, MapOffset,
                                                  (uint32)Count, ExtInfo, Deallocate);
                if (!NT_SUCCESS(Status))
                    return Status;
            }
        }
        *WrittenBytes += Count;
        MapOffset += Count;
        Length -= Count;
        /* Reload after each mutation; never replace the requested remainder
         * with all bytes available in the next mapping fragment. */
    }
    return STATUS_SUCCESS;
} // end UDFZeroExtent()
//#endif //0
