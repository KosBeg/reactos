////////////////////////////////////////////////////////////////////
// Copyright (C) Alexander Telyatnikov, Ivan Keliukh, Yegor Anchishkin, SKIF Software, 1999-2013. Kiev, Ukraine
// All rights reserved
// This file was released under the GPLv2 on June 2015.
////////////////////////////////////////////////////////////////////
/*
        Module name:

   remap.cpp

        Abstract:

   This file contains filesystem-specific routines
   responsible for disk space management

*/

#include "udf.h"

#define         UDF_BUG_CHECK_ID                UDF_FILE_UDF_INFO_REMAP

/* A read probe is not a defect-repair transaction. Report its exact failure;
 * do not silently accept allocation failure or mutate the allocation bitmap.
 * The caller either commits a verified extent or rolls back its own claim. */
NTSTATUS
__fastcall
UDFCheckArea(
    PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN lba_t LBA,
    IN uint32 BCount
    )
{
    uint8* Buffer;
    uint32 Capacity, Done = 0;
    NTSTATUS Status = STATUS_SUCCESS;
    if (!BCount)
        return STATUS_SUCCESS;
    if (!Vcb->SectorSize || Vcb->SectorShift >= 30 ||
        Vcb->SectorSize != (1UL << Vcb->SectorShift) ||
        LBA > Vcb->SessionEndLba ||
        BCount > (uint64)Vcb->SessionEndLba - LBA + 1)
        return STATUS_INVALID_PARAMETER;
    if (Vcb->SectorSize > 0x10000)
        return STATUS_NOT_SUPPORTED;
    Capacity = min(BCount, 0x10000UL >> Vcb->SectorShift);
    Buffer = (uint8*)DbgAllocatePoolWithTag(NonPagedPool,
                                            Capacity << Vcb->SectorShift, 'bNWD');
    if (!Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;
    _SEH2_TRY {
        while (Done < BCount) {
            uint32 Blocks = min(BCount - Done, Capacity);
            Status = UDFReadWriteSectors(IrpContext, Vcb,
                ((LONGLONG)LBA + Done) << Vcb->SectorShift,
                Blocks << Vcb->SectorShift, TRUE, Buffer, FALSE);
            if (Status != STATUS_SUCCESS) {
                if (NT_SUCCESS(Status)) Status = STATUS_IO_DEVICE_ERROR;
                _SEH2_LEAVE;
            }
            Done += Blocks;
        }
    } _SEH2_FINALLY {
        DbgFreePool(Buffer);
    } _SEH2_END;
    return Status;
} // end UDFCheckArea()

/*
    This routine remaps sectors from bad packet
 */
NTSTATUS
UDFRemapPacket(IN PIRP_CONTEXT IrpContext, IN PVCB Vcb,
                IN uint32 Lba, IN BOOLEAN RemapSpared)
{
    UNREFERENCED_PARAMETER(IrpContext);
    UNREFERENCED_PARAMETER(Vcb);
    UNREFERENCED_PARAMETER(Lba);
    UNREFERENCED_PARAMETER(RemapSpared);
    /* The old path changed a mapping without copying the complete packet
     * or committing its replicas. Existing loaded maps remain usable; new
     * automatic defect replacement requires that separate complete backend. */
    return STATUS_NOT_SUPPORTED;
} // end UDFRemapPacket()

/* Input is already a volume physical sector. Only persistent device-defect
 * and fixed-packet media boundaries remain below typed partition mapping. */
static NTSTATUS
UDFMapVolumeSectorRun(PVCB Vcb, uint32 Lba, uint32 Limit,
                       uint32* Physical, uint32* Blocks)
{
    uint32 i, Length = Limit, Target = Lba, DiskStart;
    NTSTATUS Status;
    if (Vcb->SparingTable) {
        uint32 Packet = Vcb->SparingBlockSize;
        BOOLEAN Found = FALSE;
        if (!InterlockedCompareExchange(&Vcb->SparingReady, 0, 0) || !Packet ||
            Vcb->SparingCount > Vcb->SparingCapacity || Vcb->SparingCapacity > MAXUSHORT)
            return STATUS_FILE_CORRUPT_ERROR;
        for (i = 0; i < Vcb->SparingCount; i++) {
            uint32 Original = Vcb->SparingTable[i].origLocation;
            uint32 Replacement = Vcb->SparingTable[i].mappedLocation;
            if (Original >= SPARING_LOC_CORRUPTED)
                continue;
            if ((uint64)Original + Packet > (uint64)MAXULONG + 1)
                return STATUS_FILE_CORRUPT_ERROR;
            if (Lba >= Original && Lba - Original < Packet) {
                uint32 Delta = Lba - Original;
                if (Found || (uint64)Replacement + Delta > MAXULONG)
                    return STATUS_FILE_CORRUPT_ERROR;
                Found = TRUE;
                Target = Replacement + Delta;
                Length = min(Length, Packet - Delta);
            } else if (Original > Lba) {
                Length = min(Length, Original - Lba);
            }
        }
    }
    Status = UDFGetFixedPacketRun(Vcb, Target, Length, &DiskStart, &Length);
    if (!NT_SUCCESS(Status))
        return Status;
    if (!Length || Target > Vcb->SessionEndLba ||
        Length > (uint64)Vcb->SessionEndLba - Target + 1)
        return STATUS_FILE_CORRUPT_ERROR;
    *Physical = Target;
    *Blocks = Length;
    return STATUS_SUCCESS;
}

/* Status and allocation ownership are separate. A nonidentity result is
 * one owned, terminated map; identity uses UDF_NO_EXTENT_MAP. Validate the
 * full request before publishing either result, never Lba+BlockCount+1.
 * Requires a live VCB. Sparing originals are published once after partition
 * binding and remain immutable until final VCB disposal; readers never walk
 * the staged relative array. VAT is resolved earlier by partition identity. */
NTSTATUS
UDFMapVolumeSectorSpan(PVCB Vcb, uint32 Lba, uint32 BlockCount, PEXTENT_MAP* Result)
{
    PEXTENT_MAP Extent = NULL;
    uint32 MaxRun, Pass, Count = 0, Capacity = 0;
    NTSTATUS Status = STATUS_SUCCESS;
    *Result = NULL;
    if (!BlockCount) {
        *Result = UDF_NO_EXTENT_MAP;
        return STATUS_SUCCESS;
    }
    if (!Vcb->SectorSize || Vcb->SectorShift >= 30 ||
        Vcb->SectorSize != (1UL << Vcb->SectorShift) ||
        (uint64)Lba + BlockCount > (uint64)MAXULONG + 1 ||
        BlockCount > (MAXULONG >> Vcb->SectorShift))
        return STATUS_INVALID_PARAMETER;
    /* A table is staged before the partition descriptor supplies its root.
     * Only mount's physical descriptor reads exist then. No namespace/cache
     * user may consume a partially converted relative/physical array. */
    if (Vcb->SparingTable && !InterlockedCompareExchange(&Vcb->SparingReady, 0, 0)) {
        if (Vcb->RootIndexFcb || Vcb->VcbCondition == VcbMounted ||
            FlagOn(Vcb->VcbState, VCB_STATE_PACKET_RUNOUT_FIXUP))
            return STATUS_INVALID_DEVICE_STATE;
        if (Lba > Vcb->SessionEndLba ||
            BlockCount > (uint64)Vcb->SessionEndLba - Lba + 1)
            return STATUS_FILE_CORRUPT_ERROR;
        *Result = UDF_NO_EXTENT_MAP;
        return STATUS_SUCCESS;
    }
    /* Plain HDD I/O retains a constant-time no-allocation path. */
    if (!Vcb->SparingTable &&
        !FlagOn(Vcb->VcbState, VCB_STATE_PACKET_RUNOUT_FIXUP)) {
        if (Lba > Vcb->SessionEndLba ||
            BlockCount > (uint64)Vcb->SessionEndLba - Lba + 1)
            return STATUS_FILE_CORRUPT_ERROR;
        *Result = UDF_NO_EXTENT_MAP;
        return STATUS_SUCCESS;
    }
    MaxRun = UDF_EXTENT_LENGTH_MASK >> Vcb->SectorShift;
    _SEH2_TRY {
        for (Pass = 0; Pass < 2; Pass++) {
            uint64 Cursor = Lba;
            uint32 Remaining = BlockCount, Last = 0, LastBlocks = 0;
            BOOLEAN Identity = TRUE;
            Count = 0;
            while (Remaining) {
                uint32 Physical, Blocks;
                Status = UDFMapVolumeSectorRun(Vcb, (uint32)Cursor,
                    min(Remaining, MaxRun), &Physical, &Blocks);
                if (!NT_SUCCESS(Status))
                    _SEH2_LEAVE;
                if (!Blocks || Blocks > Remaining || Blocks > MaxRun) {
                    Status = STATUS_FILE_CORRUPT_ERROR;
                    _SEH2_LEAVE;
                }
                if (Physical != Cursor)
                    Identity = FALSE;
                if (Count && !FlagOn(Vcb->VcbState, VCB_STATE_PACKET_RUNOUT_FIXUP) &&
                    (uint64)Last + LastBlocks == Physical &&
                    Blocks <= MaxRun - LastBlocks) {
                    LastBlocks += Blocks;
                    if (Pass)
                        Extent[Count - 1].extLength = LastBlocks << Vcb->SectorShift;
                } else {
                    if (Pass) {
                        if (Count >= Capacity) {
                            Status = STATUS_FILE_CORRUPT_ERROR;
                            _SEH2_LEAVE;
                        }
                        Extent[Count].extLocation = Physical;
                        Extent[Count].extLength = Blocks << Vcb->SectorShift;
                    }
                    Last = Physical;
                    LastBlocks = Blocks;
                    Count++;
                }
                Cursor += Blocks;
                Remaining -= Blocks;
            }
            if (!Pass) {
                if (FlagOn(Vcb->VcbState, VCB_STATE_PACKET_RUNOUT_FIXUP) && Count > 1)
                    Identity = FALSE; /* preserve packet boundaries in the output */
                if (Identity) {
                    *Result = UDF_NO_EXTENT_MAP;
                    _SEH2_LEAVE;
                }
                if (Count >= MAXULONG / sizeof(EXTENT_MAP)) {
                    Status = STATUS_INSUFFICIENT_RESOURCES;
                    _SEH2_LEAVE;
                }
                Capacity = Count;
                Extent = (PEXTENT_MAP)MyAllocatePoolTag__(NonPagedPool,
                    (Capacity + 1) * sizeof(EXTENT_MAP), MEM_EXTMAP_TAG);
                if (!Extent) {
                    Status = STATUS_INSUFFICIENT_RESOURCES;
                    _SEH2_LEAVE;
                }
            } else {
                if (Count != Capacity) {
                    Status = STATUS_FILE_CORRUPT_ERROR;
                    _SEH2_LEAVE;
                }
                Extent[Count].extLocation = Extent[Count].extLength = 0;
                *Result = Extent;
                Extent = NULL;
            }
        }
    } _SEH2_FINALLY {
        if (Extent) MyFreePool__(Extent);
    } _SEH2_END;
    return Status;
} // UDFMapVolumeSectorSpan()

