////////////////////////////////////////////////////////////////////
// Copyright (C) Alexander Telyatnikov, Ivan Keliukh, Yegor Anchishkin, SKIF Software, 1999-2013. Kiev, Ukraine
// All rights reserved
// This file was released under the GPLv2 on June 2015.
////////////////////////////////////////////////////////////////////
/*
 Module Name: Phys_lib.cpp

 Execution: Kernel mode only

 Description:

   Contains code that implement read/write operations for physical device
*/

#include "phys_lib.h"

#define MSF_TO_LBA(Minutes,Seconds,Frames) \
                (ULONG)((60 * 75 * (Minutes)) + (75 * (Seconds)) + ((Frames) - 150))

#define TrkInfo_Dat_Mask    0x0F
#define TrkInfo_Dat_Mode1   0x01
#define TrkInfo_Dat_Mode2   0x02
#define TrkInfo_Dat_XA      0x02
#define TrkInfo_Dat_DDCD    0x02
#define TrkInfo_Dat_unknown 0x0F
#define TrkInfo_Dat_Unknown TrkInfo_Dat_unknown
#define TrkInfo_FP          0x10
#define TrkInfo_Packet      0x20
#define TrkInfo_Blank       0x40
#define TrkInfo_RT          0x80


#define Trk_QSubChan_Type_Audio            0x00
#define Trk_QSubChan_Type_AllowCpy         0x02
#define Trk_QSubChan_Type_Data             0x04
#define Trk_QSubChan_Type_IncrData         0x05
#define Trk_QSubChan_Type_Mask             0x0d

#define WParam_TrkMode_Mask             Trk_QSubChan_Type_Mask           //0x0d
#define WParam_TrkMode_Data             Trk_QSubChan_Type_Data           //0x04
#define WParam_TrkMode_IncrData         Trk_QSubChan_Type_IncrData       //0x05
#define TrkInfo_Trk_unknown 0x0F


#define TrkInfo_Trk_XA      (Trk_QSubChan_Type_Audio | Trk_QSubChan_Type_AllowCpy)

#define TocControl_TrkMode_Mask           WParam_TrkMode_Mask
#define TocControl_TrkMode_Data           WParam_TrkMode_Data
#define TocControl_TrkMode_IncrData       WParam_TrkMode_IncrData


#define DEFAULT_LAST_LBA_FP_CD  276159
#define TOC_LastTrack_ID        0xAA
#define MediaType_UnknownSize_CDRW 0x20

#define UDF_MAX_SECTOR_IO_BYTES  0x100000 // 1 MB which is a multiple of every sector size

// Local functions:

uint32
UDFFixFPAddress(
    IN PVCB           Vcb,               // Volume control block from this DevObj
    IN uint32         Lba
    );

NTSTATUS
UDFReallocTrackMap(
    IN PVCB Vcb,
    IN uint32 TrackNum
    )
{
    if (Vcb->TrackMap) {
        MyFreePool__(Vcb->TrackMap);
        Vcb->TrackMap = NULL;
    }
    Vcb->TrackMap = (PUDFTrackMap)
        MyAllocatePool__(NonPagedPool, TrackNum*sizeof(UDFTrackMap));
    if (!Vcb->TrackMap) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Vcb->TrackMap,TrackNum*sizeof(UDFTrackMap));
    return STATUS_SUCCESS;
} // end UDFReallocTrackMap()

/*************************************************************************
*
*  Function: UDFSendSectorIoChunk()
*
*  Description:
*    Build and send a single read/write IRP (capped at the chunk size) to the target device.
*    Waits for completion synchronously.  Handles MDL and IRP cleanup.
*
*************************************************************************/

static
NTSTATUS
UDFSendSectorIoChunk(
    IN PIRP_CONTEXT IrpContext,
    IN PDEVICE_OBJECT TargetDeviceObject,
    IN PVOID Buffer,
    IN ULONG ByteCount,
    IN LONGLONG Offset,
    IN BOOLEAN IsWrite
    )
{
    KEVENT Event;
    LARGE_INTEGER ROffset;
    PIRP Irp;
    PIO_STACK_LOCATION IrpSp;
    NTSTATUS Status;

    PAGED_CODE();

    ROffset.QuadPart = Offset;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    Irp = IoBuildAsynchronousFsdRequest(
        IsWrite ? IRP_MJ_WRITE : IRP_MJ_READ,
        TargetDeviceObject,
        Buffer,
        ByteCount,
        &ROffset,
        NULL);

    if (!Irp) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    IoSetCompletionRoutine(Irp,
                           &UDFHijackCompletionRoutine,
                           &Event,
                           TRUE,
                           TRUE,
                           TRUE);

    IrpSp = IoGetNextIrpStackLocation(Irp);

    SetFlag(IrpSp->Flags, SL_OVERRIDE_VERIFY_VOLUME);

    if (IsWrite && FlagOn(IrpContext->Flags, IRP_CONTEXT_FLAG_WRITE_THROUGH)) {
        SetFlag(IrpSp->Flags, SL_WRITE_THROUGH);
    }

    Status = IoCallDriver(TargetDeviceObject, Irp);

    if (Status == STATUS_PENDING) {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = Irp->IoStatus.Status;
    }

    /* A successful IRP with fewer bytes than requested did not transfer the
     * complete sector range.  Do not report that metadata was persisted. */
    if (NT_SUCCESS(Status) &&
        (!NT_SUCCESS(Irp->IoStatus.Status) ||
         Irp->IoStatus.Information != ByteCount)) {
        Status = NT_SUCCESS(Irp->IoStatus.Status) ?
                 STATUS_IO_DEVICE_ERROR : Irp->IoStatus.Status;
    }

    if (Status == STATUS_PENDING)
        Status = STATUS_IO_DEVICE_ERROR; /* completion did not supply a final status */
    UDFRecordLowerIoError(IrpContext, Status);

    //  Free MDL chain.

    while (Irp->MdlAddress != NULL) {
        PMDL NextMdl = Irp->MdlAddress->Next;
        MmUnlockPages(Irp->MdlAddress);
        IoFreeMdl(Irp->MdlAddress);
        Irp->MdlAddress = NextMdl;
    }

    IoFreeIrp(Irp);

    return Status;
} // end UDFSendSectorIoChunk()

/*************************************************************************
*
*  Function: UDFSendSectorIo()
*
*  Description:
*    Build and send a read/write IRP to the target device as a series of chunks.
*    Waits for completion synchronously.  Handles MDL and IRP cleanup.
*
*    Sending as chunks because, the Mdl is allocated with IoAllocateMdl by 
*    IoBuildAsynchronousFsdRequest, which fails above ~64 MB on x86 and ~32 MB 
*    on amd64 on ReactOS and NT 5.x.
*
*************************************************************************/

static
NTSTATUS
UDFSendSectorIo(
    IN PIRP_CONTEXT IrpContext,
    IN PDEVICE_OBJECT TargetDeviceObject,
    IN PVOID Buffer,
    IN ULONG ByteCount,
    IN LONGLONG Offset,
    IN BOOLEAN IsWrite
    )
{
    PCHAR BufPtr = Buffer;
    ULONG ChunkLen = 0;
    NTSTATUS Status = STATUS_SUCCESS;

    while (ByteCount > 0) {
        ChunkLen = min(ByteCount, UDF_MAX_SECTOR_IO_BYTES);
        Status = UDFSendSectorIoChunk(IrpContext, TargetDeviceObject, BufPtr, ChunkLen, Offset, IsWrite);
        
        if (!NT_SUCCESS(Status)) {

            return Status;
        }
        
        BufPtr += ChunkLen;
        Offset += ChunkLen;
        ByteCount -= ChunkLen;
    }

    return Status;
} // end UDFSendSectorIo()


/* Descriptor locations expressed in media block addresses must not pass
 * through the mapping table they are themselves used to load or verify. */
NTSTATUS
UDFReadMediaSectors(PIRP_CONTEXT IrpContext, PVCB Vcb, uint32 Lba,
                    uint32 Blocks, PVOID Buffer)
{
    NTSTATUS Status;
    if (!Blocks)
        return STATUS_SUCCESS;
    if (!Buffer || !Vcb->TargetDeviceObject || !Vcb->PhysicalLastLbaKnown ||
        !Vcb->SectorSize || Vcb->SectorShift >= 30 ||
        Vcb->SectorSize != (1UL << Vcb->SectorShift) ||
        Blocks > (MAXULONG >> Vcb->SectorShift))
        return STATUS_INVALID_PARAMETER;
    if (Lba > Vcb->PhysicalLastLba ||
        Blocks > (uint64)Vcb->PhysicalLastLba - Lba + 1)
        return STATUS_FILE_CORRUPT_ERROR;
    if (Vcb->VcbState & UDF_VCB_FLAGS_DEAD)
        return STATUS_NO_SUCH_DEVICE;
    Status = UDFSendSectorIo(IrpContext, Vcb->TargetDeviceObject, Buffer,
                 Blocks << Vcb->SectorShift, (uint64)Lba << Vcb->SectorShift, FALSE);
    if (NT_SUCCESS(Status))
        Vcb->VcbState &= ~UDF_VCB_LAST_WRITE;
    return Status;
}

/*************************************************************************
*
*  Function: UDFReadWriteSectors()
*
*  Description:
*    Single entry point for all metadata sector I/O (read and write).
*    Handles sparing table relocation, fixed-packet address translation,
*    bad sector bitmap checks. Sequential append writes require a backend
*    that is not implemented by this direct sector writer.
*
*    For file data I/O, use UDFNonCachedIo() in deviosup.cpp instead.
*
*************************************************************************/

static NTSTATUS
UDFReadWriteSectorsRaw(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN LONGLONG StartingOffset,
    IN ULONG ByteCount,
    IN BOOLEAN ReturnError,
    IN PVOID Buffer,
    IN BOOLEAN IsWrite
    )
{
    NTSTATUS RC = STATUS_SUCCESS;
    PEXTENT_MAP RelocExtent;
    PEXTENT_MAP RelocExtent_saved = NULL;

    uint32 Lba, BCount;
    uint32 rLba;

    if (!ByteCount)
        return STATUS_SUCCESS;
    if (!Buffer || StartingOffset < 0 || !Vcb->SectorSize || Vcb->SectorShift >= 30 ||
        Vcb->SectorSize != (1UL << Vcb->SectorShift) ||
        (StartingOffset & (Vcb->SectorSize - 1)) ||
        (ByteCount & (Vcb->SectorSize - 1)) ||
        ((uint64)StartingOffset >> Vcb->SectorShift) > MAXULONG)
        return STATUS_INVALID_PARAMETER;
    Lba = (uint32)((uint64)StartingOffset >> Vcb->SectorShift);
    BCount = ByteCount >> Vcb->SectorShift;
    ASSERT(Buffer);

    if (Vcb->VcbState & UDF_VCB_FLAGS_DEAD) {
        return STATUS_NO_SUCH_DEVICE;
    }

    if (IsWrite && (Vcb->CDR_Mode ||
        FlagOn(Vcb->VcbState, VCB_STATE_PACKET_RUNOUT_FIXUP)))
        return STATUS_NOT_SUPPORTED;

    //
    //  Input is already a physical volume address; apply device sparing only.
    //  Never redirect a write to an
    //  NWA that this backend neither advances nor commits into a VAT.
    //

    RC = UDFMapVolumeSectorSpan(Vcb, Lba, BCount, &RelocExtent);
    if (!NT_SUCCESS(RC)) {
        if (!ReturnError) ExRaiseStatus(RC);
        return RC;
    }
    RelocExtent_saved = RelocExtent == UDF_NO_EXTENT_MAP ? NULL : RelocExtent;
    rLba = Lba;

    _SEH2_TRY {

        if (RelocExtent == UDF_NO_EXTENT_MAP) {

            //
            //  No relocation — single contiguous I/O.
            //

            if (!IsWrite) {

                // The full recorded span was validated before any I/O.

                RC = UDFPrepareForReadOperation(IrpContext, Vcb, rLba, BCount);
                if (!NT_SUCCESS(RC)) try_return(RC);

                rLba = UDFFixFPAddress(Vcb, rLba);
                if (rLba == LBA_OUT_OF_EXTENT)
                    try_return(RC = STATUS_FILE_CORRUPT_ERROR);

                RC = UDFSendSectorIo(IrpContext,
                         Vcb->TargetDeviceObject, Buffer, ByteCount,
                         ((uint64)rLba) << Vcb->SectorShift, FALSE);

                Vcb->VcbState &= ~UDF_VCB_LAST_WRITE;

            } else {

                RC = UDFPrepareForWriteOperation(Vcb, rLba, BCount);
                if (!NT_SUCCESS(RC)) try_return(RC);

                RC = UDFSendSectorIo(IrpContext,
                         Vcb->TargetDeviceObject, Buffer, ByteCount,
                         ((uint64)rLba) << Vcb->SectorShift, TRUE);
            }

            try_return(RC);
        }

        //
        //  Sectors were relocated — walk the relocation map.
        //

        RelocExtent_saved = RelocExtent;

        for (; RelocExtent->extLength; RelocExtent++) {

            rLba = RelocExtent->extLocation;
            uint32 FragLength = RelocExtent->extLength;
            uint32 FragBCount = FragLength >> Vcb->SectorShift;

            if (!IsWrite) {

                RC = UDFPrepareForReadOperation(IrpContext, Vcb, rLba, FragBCount);
                if (!NT_SUCCESS(RC)) break;

                rLba = UDFFixFPAddress(Vcb, rLba);
                if (rLba == LBA_OUT_OF_EXTENT) {
                    RC = STATUS_FILE_CORRUPT_ERROR;
                    break;
                }

                RC = UDFSendSectorIo(IrpContext,
                         Vcb->TargetDeviceObject, Buffer, FragLength,
                         ((uint64)rLba) << Vcb->SectorShift, FALSE);

                Vcb->VcbState &= ~UDF_VCB_LAST_WRITE;

            } else {

                RC = UDFPrepareForWriteOperation(Vcb, rLba, FragBCount);
                if (!NT_SUCCESS(RC)) break;

                RC = UDFSendSectorIo(IrpContext,
                         Vcb->TargetDeviceObject, Buffer, FragLength,
                         ((uint64)rLba) << Vcb->SectorShift, TRUE);
            }

            if (!NT_SUCCESS(RC)) break;
            Buffer = (PUCHAR)Buffer + FragLength;
        }

try_exit: NOTHING;
    } _SEH2_FINALLY {
        if (RelocExtent_saved) {
            MyFreePool__(RelocExtent_saved);
        }
    } _SEH2_END;

    if (!NT_SUCCESS(RC) && !ReturnError) {
        ExRaiseStatus(RC);
    }

    return RC;
} // end UDFReadWriteSectorsRaw()

NTSTATUS
UDFReadWriteSectors(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN LONGLONG StartingOffset,
    IN ULONG ByteCount,
    IN BOOLEAN ReturnError,
    IN PVOID Buffer,
    IN BOOLEAN IsWrite
    )
{
    NTSTATUS Status = STATUS_SUCCESS;
    PUDF_METADATA_VIEW View = NULL;
    uint32 FirstLba, Count, i, MetadataLbn, MirrorLba;

    _SEH2_TRY {
        View = UDFAcquireMetadataView(Vcb);
        if (IsWrite && (Vcb->MetadataFlags & UDF_METADATA_DUPLICATED) &&
            (Vcb->MetadataFileInfo || Vcb->MetadataMirrorFileInfo) && !View) {
            Status = STATUS_FILE_CORRUPT_ERROR;
            _SEH2_LEAVE; /* no primary write without the required mirror authority */
        }

        if (IsWrite && View && (View->Flags & UDF_METADATA_DUPLICATED) &&
            View->MirrorBlocks < View->MainBlocks) {
            Status = STATUS_FILE_CORRUPT_ERROR;
            _SEH2_LEAVE; /* a known incomplete mirror cannot authorize writes */
        }

        /* Hold the same independent generation across primary and mirror I/O.
         * Growth may retire the Dloc buffers while these arrays remain live. */
        Status = UDFReadWriteSectorsRaw(IrpContext, Vcb, StartingOffset,
                                        ByteCount, TRUE, Buffer, IsWrite);
        if (!NT_SUCCESS(Status) || !IsWrite || !ByteCount || !View ||
            !(View->Flags & UDF_METADATA_DUPLICATED))
            _SEH2_LEAVE;

        /* Raw validated alignment, geometry and the complete physical span
         * before its first I/O. Therefore these casts/additions stay bounded. */
        FirstLba = (uint32)((uint64)StartingOffset >> View->SectorShift);
        Count = ByteCount >> View->SectorShift;
        for (i = 0; i < Count; i++) {
            MetadataLbn = UDFMetadataViewPhysToLba(View, FirstLba + i);
            if (MetadataLbn == LBA_OUT_OF_EXTENT)
                continue;
            MirrorLba = UDFMetadataViewLbaToPhys(View, MetadataLbn, TRUE, NULL);
            if (MirrorLba == LBA_OUT_OF_EXTENT) {
                Status = STATUS_FILE_CORRUPT_ERROR;
                break;
            }
            Status = UDFReadWriteSectorsRaw(IrpContext, Vcb,
                ((LONGLONG)MirrorLba) << View->SectorShift,
                View->SectorSize, TRUE,
                (PUCHAR)Buffer + (((SIZE_T)i) << View->SectorShift), TRUE);
            if (!NT_SUCCESS(Status))
                break;
        }
    } _SEH2_FINALLY {
        UDFReleaseMetadataView(View);
        if (_SEH2_AbnormalTermination() && IsWrite)
            Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
    } _SEH2_END;

    if (!NT_SUCCESS(Status)) {
        if (IsWrite) Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
        if (!ReturnError) ExRaiseStatus(Status);
    }
    return Status;
} // end UDFReadWriteSectors()

/*
    Check the physical write's overlap with the known bad-block bitmap.
    This routine does not configure an optical writer, advance NWA or
    implement packet formatting/append completion.
 */
NTSTATUS
UDFPrepareForWriteOperation(
    IN PVCB Vcb,
    IN uint32 Lba,
    IN uint32 BCount
    )
{
    if (!BCount)
        return STATUS_SUCCESS;
    if ((uint64)Lba + BCount > (uint64)MAXULONG + 1)
        return STATUS_INVALID_PARAMETER;
    if (Vcb->BSBM_Bitmap) {
        uint64 First, End, Root;
        uint32 i, LastBit;
        if (!Vcb->Partitions || !Vcb->PartitionMaps)
            return STATUS_FILE_CORRUPT_ERROR;
        Root = Vcb->Partitions[0].PartitionRoot;
        First = max((uint64)Lba, Root);
        End = min((uint64)Lba + BCount, Root + Vcb->FSBM_BitCount);
        /* LVID/VDS sectors can precede this partition. Only its overlapping
         * physical range has bad-bitmap indices; never underflow at Lba-Root. */
        if (First < End) {
            LastBit = (uint32)(End - Root);
            for (i = (uint32)(First - Root); i < LastBit; i++) {
                if (!(((const uint8*)Vcb->BSBM_Bitmap)[i >> 3] & (1U << (i & 7))))
                    continue;
                UDFPrint(("W: Known BB @ %#x\n", Lba));
                //return STATUS_FT_WRITE_RECOVERY; // this shall not be treated as error and
                                                   // we shall get IO request to BAD block
                return STATUS_DEVICE_DATA_ERROR;
            }
        }
    }

    Vcb->VcbState |= UDF_VCB_LAST_WRITE;

    return STATUS_SUCCESS;
} // end UDFPrepareForWriteOperation()

/*
    use standard way to determine disk layout (ReadTOC cmd)
 */
NTSTATUS
UDFDetermineVolumeLayout(
    PIRP_CONTEXT IrpContext,
    PDEVICE_OBJECT DeviceObject,
    PVCB Vcb,
    PULONG SessionStartLba,
    PULONG SessionEndLba
    )
{
    NTSTATUS Status;
    CDROM_TOC_LARGE* toc = NULL;
    CDROM_TOC_SESSION_DATA* LastSes = NULL;
    ULONG LocalTrackCount;
    ULONG TocEntry;
    void* TempBuffer = NULL;
    ULONG OldTrkNum;
    ULONG TrkNum;
    SIZE_T i, len;

    *SessionStartLba = 0;
    *SessionEndLba = 0;

    //// Only process CD-ROM devices

    //if (DeviceObject->DeviceType != FILE_DEVICE_CD_ROM) {

    //    return STATUS_SUCCESS;
    //}

    toc = (CDROM_TOC_LARGE*)MyAllocatePool__(NonPagedPool, sizeof(CDROM_TOC_LARGE));
    LastSes = (CDROM_TOC_SESSION_DATA*)MyAllocatePool__(NonPagedPool, sizeof(CDROM_TOC_SESSION_DATA));

    _SEH2_TRY {

        if (!toc || !LastSes) {
            try_return (Status = STATUS_INSUFFICIENT_RESOURCES);
        }

        RtlZeroMemory(toc, sizeof(CDROM_TOC_LARGE));

        CDROM_READ_TOC_EX Command;

        RtlZeroMemory(&Command, sizeof(Command));

        Status = UDFPerformDevIoCtrl(IOCTL_CDROM_READ_TOC_EX,
                                DeviceObject,
                                &Command,
                                sizeof(Command),
                                toc,
                                sizeof(CDROM_TOC_LARGE),
                                TRUE,
                                NULL);

        if (!NT_SUCCESS(Status) && 
            (Status != STATUS_INSUFFICIENT_RESOURCES)) {

            // try using the MSF mode
            Command.Msf = 1;

            Status = UDFPerformDevIoCtrl(IOCTL_CDROM_READ_TOC_EX,
                                    DeviceObject,
                                    &Command,
                                    sizeof(Command),
                                    toc,
                                    sizeof(CDROM_TOC_LARGE),
                                    TRUE,
                                    NULL);
        }

        if (Status == STATUS_INSUFFICIENT_RESOURCES) {

            UDFRaiseStatus(IrpContext, Status);
        }

        // If even standard read toc does not work, then use default values
        if (!NT_SUCCESS(Status)) {

            Status = UDFReallocTrackMap(Vcb, 2);
            if (!NT_SUCCESS(Status)) {
                try_return(Status);
            }

            Vcb->LastSession=1;
            Vcb->FirstTrackNum=1;
            Vcb->LastTrackNum=1;
            Vcb->TrackMap[1].FirstLba = Vcb->SessionStartLba;
            Vcb->TrackMap[1].LastLba = Vcb->SessionEndLba;
            Vcb->TrackMap[1].PacketSize = PACKETSIZE_UDF;

            if (DeviceObject->DeviceType == FILE_DEVICE_DISK) {

                try_return(Status = STATUS_SUCCESS);
            }

            Vcb->TrackMap[1].DataParam = TrkInfo_Dat_XA | TrkInfo_FP | TrkInfo_Packet;
            Vcb->TrackMap[1].TrackParam = TrkInfo_Trk_XA;
            Vcb->TrackMap[1].NWA = 0xffffffff;
            Vcb->NWA = DEFAULT_LAST_LBA_FP_CD + 7 + 1;
            try_return(Status = STATUS_SUCCESS);
        }

        LocalTrackCount = toc->LastTrack - toc->FirstTrack + 1;

        // Get out if there is an immediate problem with the TOC.

        if (toc->LastTrack - toc->FirstTrack >= MAXIMUM_NUMBER_TRACKS_LARGE) {
            try_return(Status = STATUS_DISK_CORRUPT_ERROR);
        }

        Vcb->LastTrackNum = toc->LastTrack;
        Vcb->FirstTrackNum = toc->FirstTrack;
        // some devices report LastTrackNum=0 for full disks
        Vcb->LastTrackNum = max(Vcb->LastTrackNum, Vcb->FirstTrackNum);

        Status = UDFReallocTrackMap(Vcb, MAXIMUM_NUMBER_TRACKS_LARGE+1);

        if (!NT_SUCCESS(Status)) {
            BrutePoint();
            try_return(Status);
        }
        // find 1st and last session
        Status = UDFPerformDevIoCtrl(IOCTL_CDROM_GET_LAST_SESSION,
            DeviceObject,
            NULL,
            0,
            LastSes,
            sizeof(CDROM_TOC_SESSION_DATA),
            TRUE,
            NULL);

        if (NT_SUCCESS(Status) &&
            LastSes->FirstCompleteSession != LastSes->LastCompleteSession) {

            SwapCopyUchar4(SessionStartLba, &LastSes->TrackData[0].Address);

            // Validate: SessionStartLba must be greater than SessionEndLba

            if (*SessionEndLba <= *SessionStartLba) {

                *SessionStartLba = 0;
                *SessionEndLba = 0;
            }

            TrkNum = LastSes->TrackData[0].TrackNumber;

            *SessionStartLba = 0;
            SwapCopyUchar4(SessionStartLba, &LastSes->TrackData[0].Address);

            Vcb->LastSession = LastSes->FirstCompleteSession;
            for(TocEntry=0;TocEntry<LocalTrackCount + 1;TocEntry++) {
                if (toc->TrackData[TocEntry].TrackNumber == TrkNum) {
                    Vcb->TrackMap[TrkNum].Session = Vcb->LastSession;
                }
            }
        }

        OldTrkNum = 0;
        // Scan toc for first & last LBA
        for(TocEntry=0;TocEntry<LocalTrackCount + 1;TocEntry++) {
#define TempMSF toc->TrackData[TocEntry].Address
            TrkNum = toc->TrackData[TocEntry].TrackNumber;
#ifdef UDF_DBG
            if (TrkNum >= MAXIMUM_NUMBER_TRACKS_LARGE &&
                TrkNum != TOC_LastTrack_ID) {
                UDFPrint(("UDFUseStandard: Array out of bounds\n"));
                BrutePoint();
                try_return(Status = STATUS_SUCCESS);
            }
            UDFPrint(("Track N %d (0x%x) first LBA %ld (%lx) \n",TrkNum,TrkNum,
                MSF_TO_LBA(TempMSF[1],TempMSF[2],TempMSF[3]),
                MSF_TO_LBA(TempMSF[1],TempMSF[2],TempMSF[3])));
#endif // UDF_DBG
            if (TOC_LastTrack_ID == TrkNum) {

                if (Command.Msf) {
                    *SessionEndLba = MSF_TO_LBA(TempMSF[1],TempMSF[2],TempMSF[3]) - 1;
                }
                else {
                    // The non-MSF (LBA) mode
                    *SessionEndLba = 0;
                    SwapCopyUchar4(SessionEndLba, &toc->TrackData[TocEntry].Address);
                    if (*SessionEndLba) {
                        *SessionEndLba -= 1;
                    }
                }

                Vcb->TrackMap[OldTrkNum].LastLba = *SessionEndLba;
                UDFPrint(("UDFUseStandard: Last track entry, break TOC scan\n"));
                break;
            } else {
                Vcb->TrackMap[TrkNum].FirstLba = MSF_TO_LBA(TempMSF[1],TempMSF[2],TempMSF[3]);
                if (Vcb->TrackMap[TrkNum].FirstLba & 0x80000000)
                    Vcb->TrackMap[TrkNum].FirstLba = 0;
                if (TrkNum) {
                    if (TOC_LastTrack_ID == OldTrkNum) {
                        UDFPrint(("UDFUseStandard: Wrong previous track number\n"));
                        BrutePoint();
                    } else {
                        Vcb->TrackMap[OldTrkNum].LastLba = Vcb->TrackMap[TrkNum].FirstLba-1;
                    }
                }
            }
            // check track type
            switch(toc->TrackData[TocEntry].Control & TocControl_TrkMode_Mask) {
            case TocControl_TrkMode_Data:
            case TocControl_TrkMode_IncrData:
                Vcb->TrackMap[TrkNum].DataParam = TrkInfo_Dat_XA;
                Vcb->TrackMap[TrkNum].TrackParam = TrkInfo_Trk_XA;
                break;
            default:
                Vcb->TrackMap[TrkNum].DataParam = TrkInfo_Dat_unknown;
                Vcb->TrackMap[TrkNum].TrackParam = TrkInfo_Trk_unknown;
            }
            OldTrkNum = TrkNum;
#undef TempMSF
        }

        TrkNum = Vcb->LastTrackNum;
        Status = STATUS_SUCCESS;
        // find last _valid_ track
        for(;TrkNum;TrkNum--) {
            if ((Vcb->TrackMap[TrkNum].DataParam  != TrkInfo_Dat_unknown) &&
               (Vcb->TrackMap[TrkNum].TrackParam != TrkInfo_Trk_unknown)) {
                Status = STATUS_UNSUCCESSFUL;
                Vcb->LastTrackNum = TrkNum;
                break;
            }
        }
        // no valid tracks...
        if (!TrkNum) {
            UDFPrint(("UDFUseStandard: no valid tracks...\n"));
            try_return(Status = STATUS_UNRECOGNIZED_VOLUME);
        }
        i = 0;

        // Check for last Variable Packet(VP) track. Some last sectors may belong to Link-data &
        // be unreadable. We should forget about them, because UDF needs
        // last _readable_ sector.

        TempBuffer = MyAllocatePool__(NonPagedPool, Vcb->SectorSize);

        if (!TempBuffer) { 
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        while(!NT_SUCCESS(Status) && (i<8)) {
            Status = UDFReadWriteSectors(IrpContext, Vcb,
                       ((LONGLONG)(Vcb->TrackMap[TrkNum].LastLba-i)) << Vcb->SectorShift,
                       Vcb->SectorSize, TRUE, TempBuffer, FALSE);
            i++;
        }
        if (NT_SUCCESS(Status)) {
            *SessionEndLba = Vcb->TrackMap[TrkNum].LastLba-i+1;
/*            if (i) {
                Vcb->TrackMap[TrkNum].PacketSize = PACKETSIZE_UDF;
                Vcb->TrackMap[TrkNum].;
            }*/
        } else {

            // Check for Fixed Packet(FP) track. READ_TOC reports actual track length, but
            // Link-data is hidden & unreadable for us. So, available track
            // length may be less than actual. Here we assume that Packet-size
            // is PACKETSIZE_UDF.
            i = 0;
            len = Vcb->TrackMap[TrkNum].LastLba - Vcb->TrackMap[TrkNum].FirstLba + 1;
            len = (uint32)(((int64)len*PACKETSIZE_UDF) / (PACKETSIZE_UDF+7));

            while(!NT_SUCCESS(Status) && (i<9)) {
                Status = UDFReadWriteSectors(IrpContext, Vcb,
                           ((LONGLONG)(Vcb->TrackMap[TrkNum].FirstLba-i+len)) << Vcb->SectorShift,
                           Vcb->SectorSize, TRUE, TempBuffer, FALSE);
                i++;
            }
            if (NT_SUCCESS(Status)) {
                *SessionEndLba =
                Vcb->TrackMap[TrkNum].LastLba = Vcb->TrackMap[TrkNum].FirstLba-i+len+1;
                Vcb->TrackMap[TrkNum].PacketSize = PACKETSIZE_UDF;
//                Vcb->TrackMap[TrkNum].;
            } else
            if (Status == STATUS_INVALID_DEVICE_REQUEST) {
                // wrap return code from Audio-disk
                Status = STATUS_SUCCESS;
            }
        }

#ifdef UDF_CDRW_EMULATION_ON_ROM
        Vcb->NWA = *SessionEndLba+7+1;
#else
        Vcb->NWA = *SessionEndLba+7+1;
#endif //UDF_CDRW_EMULATION_ON_ROM

try_exit: NOTHING;
    } _SEH2_FINALLY {
        if (toc) MyFreePool__(toc);
        if (LastSes) MyFreePool__(LastSes);
        if (TempBuffer) MyFreePool__(TempBuffer);
    } _SEH2_END;

    return Status;
} // end UDFUseStandard()

/*
    Get block size (for read operation)
 */
static
NTSTATUS
UDFGetBlockSize(IN PDEVICE_OBJECT DeviceObject, IN PVCB Vcb)
{
    NTSTATUS Status;
    IO_STATUS_BLOCK IoStatus;
    DISK_GEOMETRY_EX Geometry;
    GET_LENGTH_INFORMATION Length;
    uint64 Bytes, Sectors;

    Vcb->PhysicalLastLbaKnown = FALSE;
    if (!Vcb->SectorSize || Vcb->SectorShift >= 30 ||
        Vcb->SectorSize != (1UL << Vcb->SectorShift))
        return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(&IoStatus, sizeof(IoStatus));
    if (DeviceObject->DeviceType == FILE_DEVICE_DISK) {
        RtlZeroMemory(&Length, sizeof(Length));
        Status = UDFPerformDevIoCtrl(IOCTL_DISK_GET_LENGTH_INFO, DeviceObject,
                    NULL, 0, &Length, sizeof(Length), TRUE, &IoStatus);
        if (Status != STATUS_SUCCESS)
            return NT_SUCCESS(Status) ? STATUS_IO_DEVICE_ERROR : Status;
        if (IoStatus.Information != sizeof(Length) || Length.Length.QuadPart <= 0)
            return STATUS_IO_DEVICE_ERROR;
        Bytes = (uint64)Length.Length.QuadPart;
    } else {
        RtlZeroMemory(&Geometry, sizeof(Geometry));
        Status = UDFPerformDevIoCtrl(IOCTL_CDROM_GET_DRIVE_GEOMETRY_EX,
                    DeviceObject, NULL, 0, &Geometry, sizeof(Geometry), TRUE, &IoStatus);
        if (Status != STATUS_SUCCESS)
            return NT_SUCCESS(Status) ? STATUS_IO_DEVICE_ERROR : Status;
        if (IoStatus.Information < (ULONG_PTR)FIELD_OFFSET(DISK_GEOMETRY_EX, DiskSize) +
                                      sizeof(Geometry.DiskSize) ||
            IoStatus.Information > sizeof(Geometry) || Geometry.DiskSize.QuadPart <= 0 ||
            Geometry.Geometry.BytesPerSector != Vcb->SectorSize)
            return STATUS_IO_DEVICE_ERROR;
        Bytes = (uint64)Geometry.DiskSize.QuadPart;
    }
    if (Bytes & (Vcb->SectorSize - 1))
        return STATUS_UNRECOGNIZED_VOLUME;
    Sectors = Bytes >> Vcb->SectorShift;
    /* The driver reserves UINT32_MAX as an invalid LBA. Reject an
     * unrepresentable device instead of truncating its size modulo 2^32. */
    if (!Sectors || Sectors > MAXULONG)
        return STATUS_NOT_SUPPORTED;
    Vcb->SessionEndLba = (uint32)Sectors - 1;
    Vcb->PhysicalLastLba = Vcb->SessionEndLba;
    Vcb->PhysicalLastLbaKnown = TRUE;
    return STATUS_SUCCESS;
} // end UDFGetBlockSize()

/* Limit an I/O run before the next fixed-packet link/runout gap. The
 * caller uses the returned physical start only for this bounded run. */
NTSTATUS
UDFGetFixedPacketRun(PVCB Vcb, uint32 Lba, uint32 Requested,
                     uint32* Physical, uint32* Blocks)
{
    uint32 Track, Packet, Count;
    uint64 DiskStart;
    if (!Requested || (uint64)Lba + Requested > (uint64)MAXULONG + 1)
        return STATUS_INVALID_PARAMETER;
    *Physical = Lba;
    *Blocks = Requested;
    if (!FlagOn(Vcb->VcbState, VCB_STATE_PACKET_RUNOUT_FIXUP))
        return STATUS_SUCCESS;
    if (Lba < 0x20) {
        *Blocks = min(Requested, 0x20 - Lba);
        return STATUS_SUCCESS;
    }
    if (!Vcb->TrackMap || Vcb->FirstTrackNum > Vcb->LastTrackNum)
        return STATUS_FILE_CORRUPT_ERROR;
    Track = Vcb->LastReadTrack;
    if (Track < Vcb->FirstTrackNum || Track > Vcb->LastTrackNum ||
        !Vcb->TrackMap[Track].PacketSize) {
        if (Vcb->FirstTrackNum != Vcb->LastTrackNum)
            return STATUS_NOT_SUPPORTED; /* no guessed track for this namespace */
        Track = Vcb->FirstTrackNum;
    }
    if (Vcb->TrackMap[Track].FirstLba || Vcb->TrackMap[Track].TrackFPOffset ||
        Vcb->TrackMap[Track].PacketFPOffset)
        return STATUS_NOT_SUPPORTED; /* zero-origin conversion only */
    Packet = Vcb->TrackMap[Track].PacketSize;
    if (!Packet || Packet > MAXULONG - 7)
        return STATUS_FILE_CORRUPT_ERROR;
    Count = min(Requested, Packet - Lba % Packet);
    DiskStart = (uint64)(Lba / Packet) * ((uint64)Packet + 7) + Lba % Packet;
    if (DiskStart > MAXULONG || Count > (uint64)MAXULONG - DiskStart + 1)
        return STATUS_FILE_CORRUPT_ERROR;
    if (!Vcb->PhysicalLastLbaKnown)
        return STATUS_NOT_SUPPORTED; /* no unproved physical capacity */
    if (DiskStart > Vcb->PhysicalLastLba ||
        Count > (uint64)Vcb->PhysicalLastLba - DiskStart + 1)
        return STATUS_FILE_CORRUPT_ERROR;
    *Physical = (uint32)DiskStart;
    *Blocks = Count;
    return STATUS_SUCCESS;
}

uint32
UDFFixFPAddress(IN PVCB Vcb, IN uint32 Lba)
{
    uint32 Physical, Blocks;
    if (!NT_SUCCESS(UDFGetFixedPacketRun(Vcb, Lba, 1, &Physical, &Blocks)))
        return LBA_OUT_OF_EXTENT;
    return Physical;
} // end UDFFixFPAddress()

/*
    detect device driver & try to read disk layout (use all methods)
 */
NTSTATUS
UDFGetDiskInfo(
    IN PIRP_CONTEXT IrpContext,
    IN PDEVICE_OBJECT DeviceObject,      // the target device object
    IN PVCB           Vcb                // Volume control block from this DevObj
    )
{
    NTSTATUS        RC = STATUS_UNRECOGNIZED_VOLUME;

    UDFPrint(("UDFGetDiskInfo\n"));

    _SEH2_TRY {
        RC = UDFGetBlockSize(DeviceObject, Vcb);

        if (!NT_SUCCESS(RC)) {
            try_return(RC);
        }

        ULONG SessionStart;
        ULONG SessionEnd;

        RC = UDFDetermineVolumeLayout(IrpContext, DeviceObject, Vcb, &SessionStart, &SessionEnd);

        if (!NT_SUCCESS(RC)) {
            try_return(RC);
        }

        Vcb->SessionStartLba = SessionStart;
        if (SessionEnd) {
            Vcb->SessionEndLba = SessionEnd;
        }

try_exit:   NOTHING;

    } _SEH2_FINALLY {

        if (Vcb->TrackMap) {
            if (Vcb->TrackMap[Vcb->LastTrackNum].LastLba > Vcb->NWA) {
                if (Vcb->NWA) {
                    if (Vcb->TrackMap[Vcb->LastTrackNum].DataParam & TrkInfo_FP) {
                        Vcb->SessionEndLba = Vcb->NWA-1;
                    } else {
                        Vcb->SessionEndLba = Vcb->NWA-7-1;
                    }
                }
            } else {
                if ((Vcb->LastTrackNum > 1) &&
                   (Vcb->TrackMap[Vcb->LastTrackNum-1].FirstLba >= Vcb->TrackMap[Vcb->LastTrackNum-1].LastLba)) {
                    Vcb->SessionEndLba = Vcb->TrackMap[Vcb->LastTrackNum-1].LastLba;
                }
            }
        }

        /* This direct writer has no fixed-packet write/commit backend.
         * Do not expose a writable mount based only on its read fixup. */
        if (FlagOn(Vcb->VcbState, VCB_STATE_PACKET_RUNOUT_FIXUP))
            Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
        if (Vcb->VcbState & VCB_STATE_VOLUME_READ_ONLY) {
            if (!Vcb->BlankCD && Vcb->MediaType != MediaType_UnknownSize_CDRW) {
                UDFPrint(("UDFGetDiskInfo: R/O+!Blank+!RW -> !RAW\n"));
                Vcb->VcbState &= ~UDF_VCB_FLAGS_RAW_DISK;
            } else {
                UDFPrint(("UDFGetDiskInfo: Blank or RW\n"));
            }
        }

        UDFPrint(("UDF: ------------------------------------------\n"));
        UDFPrint(("UDF: Media characteristics\n"));
        UDFPrint(("UDF: Last session: %d\n",Vcb->LastSession));
        UDFPrint(("UDF: First track: %d, Last track: %d\n",Vcb->FirstTrackNum, Vcb->LastTrackNum));
        UDFPrint(("UDF: Session start LBA: %x\n",Vcb->SessionStartLba));
        UDFPrint(("UDF: Session end LBA: %x\n",Vcb->SessionEndLba));
        UDFPrint(("UDF: NWA: %x\n",Vcb->NWA));
        UDFPrint(("UDF: Flags: %s%s\n",
                 Vcb->VcbState & UDF_VCB_FLAGS_RAW_DISK ? "RAW " : "",
                 Vcb->VcbState & VCB_STATE_VOLUME_READ_ONLY ? "R/O " : "WR "
                 ));
        UDFPrint(("UDF: ------------------------------------------\n"));

    } _SEH2_END;

    UDFPrint(("UDFGetDiskInfo: %x\n", RC));
    return(RC);

} // end UDFGetDiskInfo()

NTSTATUS
UDFPrepareForReadOperation(PIRP_CONTEXT IrpContext, PVCB Vcb, uint32 Lba, uint32 BCount)
{
    UNREFERENCED_PARAMETER(IrpContext);
    if (!BCount)
        return STATUS_SUCCESS;
    if ((uint64)Lba + BCount > (uint64)MAXULONG + 1)
        return STATUS_INVALID_PARAMETER;
    if (Vcb->FsDeviceType != FILE_DEVICE_CD_ROM_FILE_SYSTEM) {
        Vcb->VcbState &= ~UDF_VCB_LAST_WRITE;
        return STATUS_SUCCESS;
    }
    if (Vcb->BSBM_Bitmap) {
        uint64 First, End, Root;
        uint32 Bit, Last;
        if (!Vcb->Partitions || !Vcb->PartitionMaps)
            return STATUS_FILE_CORRUPT_ERROR;
        Root = Vcb->Partitions[0].PartitionRoot;
        First = max((uint64)Lba, Root);
        End = min((uint64)Lba + BCount, Root + Vcb->FSBM_BitCount);
        if (First < End) {
            Last = (uint32)(End - Root);
            for (Bit = (uint32)(First - Root); Bit < Last; Bit++) {
                if (((const uint8*)Vcb->BSBM_Bitmap)[Bit >> 3] & (1U << (Bit & 7)))
                    return STATUS_DEVICE_DATA_ERROR;
            }
        }
    }
    return STATUS_SUCCESS;
} // end UDFPrepareForReadOperation()

/* Physical volume byte offsets only. Check the original request before any
 * narrowing/multiplication or OPEN-LVID write; a wrapped sector count must
 * never become a successful zero/smaller transfer. */
static NTSTATUS
UDFCheckVolumeByteSpan(PVCB Vcb, int64 Offset, uint64 Length)
{
    uint64 Last;
    if (!Length)
        return STATUS_SUCCESS;
    if (Offset < 0 || !Vcb->SectorSize || Vcb->SectorShift >= 30 ||
        Vcb->SectorSize != (1UL << Vcb->SectorShift) ||
        ((uint64)Offset >> Vcb->SectorShift) >= LBA_OUT_OF_EXTENT)
        return STATUS_INVALID_PARAMETER;
    if (Length > MAXULONG)
        return STATUS_NOT_SUPPORTED; /* this lower batch interface is ULONG-sized */
    Last = (uint64)Offset + Length - 1; /* bounded start + ULONG cannot overflow */
    if ((Last >> Vcb->SectorShift) >= LBA_OUT_OF_EXTENT ||
        (Last >> Vcb->SectorShift) > Vcb->SessionEndLba)
        return STATUS_FILE_CORRUPT_ERROR;
    if (Vcb->VcbState & UDF_VCB_FLAGS_DEAD)
        return STATUS_NO_SUCH_DEVICE;
    return STATUS_SUCCESS;
}

/*
    This routine reads physical sectors.
 */
NTSTATUS
UDFReadSectors(PIRP_CONTEXT IrpContext, PVCB Vcb, BOOLEAN Translate,
                 uint32 Lba, uint32 BCount, BOOLEAN Direct, int8* Buffer)
{
    NTSTATUS Status;
    uint32 Bytes;
    UNREFERENCED_PARAMETER(Translate); /* partition translation is already complete */
    UNREFERENCED_PARAMETER(Direct);
    if (!BCount)
        return STATUS_SUCCESS;
    if (!Buffer || !Vcb->SectorSize || Vcb->SectorShift >= 30 ||
        Vcb->SectorSize != (1UL << Vcb->SectorShift) ||
        BCount > (MAXULONG >> Vcb->SectorShift))
        return STATUS_INVALID_PARAMETER;
    Bytes = BCount << Vcb->SectorShift;
    Status = UDFCheckVolumeByteSpan(Vcb, (int64)Lba << Vcb->SectorShift, Bytes);
    if (!NT_SUCCESS(Status))
        return Status;
    return UDFReadWriteSectors(IrpContext, Vcb, (int64)Lba << Vcb->SectorShift,
                               Bytes, TRUE, Buffer, FALSE);
} // end UDFReadSectors()

/*
    This routine reads data inside a single physical sector (sub-sector read).
 */
static
NTSTATUS
UDFReadInSector(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN uint32 Lba,
    IN uint32 Offset,
    IN uint32 Length,
    OUT int8* Buffer
    )
{
    int8* tmp_buff;
    NTSTATUS status;

    if (!Buffer || Offset >= Vcb->SectorSize || Length > Vcb->SectorSize - Offset)
        return STATUS_INVALID_PARAMETER;
    tmp_buff = (int8*)MyAllocatePool__(NonPagedPool, Vcb->SectorSize);
    if (!tmp_buff) return STATUS_INSUFFICIENT_RESOURCES;

    status = UDFReadWriteSectors(IrpContext, Vcb,
                 ((LONGLONG)Lba) << Vcb->SectorShift,
                 Vcb->SectorSize,
                 TRUE, tmp_buff, FALSE);
    if (NT_SUCCESS(status)) {
        RtlCopyMemory(Buffer, tmp_buff + Offset, Length);
    }

    MyFreePool__(tmp_buff);
    return status;
} // end UDFReadInSector()

/*
    This routine reads data at unaligned offset & length.
 */
NTSTATUS
UDFReadData(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN BOOLEAN Translate,
    IN int64 Offset,
    IN uint32 Length,
    IN BOOLEAN Direct,
    OUT int8* Buffer
    )
{
    uint32 i, l, Lba, BS = Vcb->SectorSize;
    uint32 BSh = Vcb->SectorShift;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(Translate); /* Offset is a physical volume byte offset */
    if (!Length) return STATUS_SUCCESS;
    if (!Buffer) return STATUS_INVALID_PARAMETER;
    status = UDFCheckVolumeByteSpan(Vcb, Offset, Length);
    if (!NT_SUCCESS(status)) return status;

    // Read tail of the 1st sector if Offset is not sector-aligned.
    Lba = (uint32)(Offset >> BSh);
    if ((i = (uint32)(Offset & (BS - 1)))) {
        l = min(BS - i, Length);
        status = UDFReadInSector(IrpContext, Vcb, Lba, i, l, Buffer);
        if (!NT_SUCCESS(status)) return status;
        Length -= l;
        if (!Length) return STATUS_SUCCESS;
        Lba++;
        Buffer += l;
    }

    // Read sector-aligned middle part.
    if (Length >= BS) {
        uint32 AlignedBytes = (Length >> BSh) << BSh;
        status = UDFReadWriteSectors(IrpContext, Vcb,
                     ((LONGLONG)Lba) << BSh,
                     AlignedBytes,
                     TRUE, Buffer, FALSE);
        if (!NT_SUCCESS(status)) return status;
        Buffer += AlignedBytes;
        Lba += AlignedBytes >> BSh;
        Length -= AlignedBytes;
    }

    // Read head of the last sector.
    if (!Length) return STATUS_SUCCESS;
    return UDFReadInSector(IrpContext, Vcb, Lba, 0, Length, Buffer);
} // end UDFReadData()

NTSTATUS
UDFBarrierFlushDevice(IN PDEVICE_OBJECT TargetDeviceObject)
{
    IO_STATUS_BLOCK iosb;
    KEVENT event;
    PIRP irp;
    NTSTATUS status;

    iosb.Status = STATUS_UNSUCCESSFUL;
    iosb.Information = 0;
    KeInitializeEvent(&event, NotificationEvent, FALSE);
    irp = IoBuildSynchronousFsdRequest(IRP_MJ_FLUSH_BUFFERS,
                                       TargetDeviceObject, NULL, 0, NULL,
                                       &event, &iosb);
    if (!irp)
        return STATUS_INSUFFICIENT_RESOURCES;
    status = IoCallDriver(TargetDeviceObject, irp);
    if (status == STATUS_PENDING) {
        KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
        status = iosb.Status;
    } else if (NT_SUCCESS(status)) {
        status = iosb.Status;
    }
    if (status == STATUS_PENDING)
        status = STATUS_IO_DEVICE_ERROR;
    UDFRecordLowerIoError(NULL, status);
    return status;
}

NTSTATUS
UDFOpenIntegrityForWrite(IN PIRP_CONTEXT IrpContext, IN PVCB Vcb,
                         IN BOOLEAN Direct)
{
    NTSTATUS status = UDFAllocationFailureStatus(Vcb);
    BOOLEAN Opening = FALSE;

    if (!NT_SUCCESS(status))
        return status;
    if (Vcb->CDR_Mode || FlagOn(Vcb->VcbState, VCB_STATE_PACKET_RUNOUT_FIXUP))
        return STATUS_NOT_SUPPORTED;
    if (Vcb->VcbState & VCB_STATE_VOLUME_READ_ONLY)
        return STATUS_MEDIA_WRITE_PROTECTED;
    if (Vcb->Modified && Vcb->IntegrityType != INTEGRITY_TYPE_CLOSE &&
        (ExIsResourceAcquiredExclusiveLite(&Vcb->BitMapResource1) ||
         !InterlockedCompareExchange(&Vcb->IntegrityOpenPending, 0, 0)))
        return (Vcb->VcbState & VCB_STATE_VOLUME_READ_ONLY) ?
               STATUS_MEDIA_WRITE_PROTECTED : STATUS_SUCCESS;
    /* A bitmap paging write may already own this resource. The fast path
     * above remains recursive; a first OPEN/barrier owns Pending until its
     * finally, including raised lower I/O errors. */
    UDFAcquireResourceExclusive(&Vcb->BitMapResource1, TRUE);
    _SEH2_TRY {
        status = UDFAllocationFailureStatus(Vcb);
        if (NT_SUCCESS(status) && (Vcb->VcbState & VCB_STATE_VOLUME_READ_ONLY))
            status = STATUS_MEDIA_WRITE_PROTECTED;
        if (NT_SUCCESS(status) && (!Vcb->Modified || Vcb->IntegrityType == INTEGRITY_TYPE_CLOSE)) {
            Opening = TRUE;
            InterlockedExchange(&Vcb->IntegrityOpenPending, 1);
            UDFSetModified(Vcb);
            if (Vcb->LVid && !Direct) {
                status = UDFUpdateLogicalVolInt(IrpContext, Vcb, FALSE);
                if (NT_SUCCESS(status))
                    status = UDFBarrierFlushDevice(Vcb->TargetDeviceObject);
            }
        }
    } _SEH2_FINALLY {
        if (Opening) {
            if (_SEH2_AbnormalTermination() || !NT_SUCCESS(status))
                Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
            InterlockedExchange(&Vcb->IntegrityOpenPending, 0);
        }
        UDFReleaseResource(&Vcb->BitMapResource1);
    } _SEH2_END;
    return status;
}

/*
    This routine writes physical sectors.
    Marks volume as modified and updates LVID if needed.
 */
NTSTATUS
UDFWriteSectors(PIRP_CONTEXT IrpContext, PVCB Vcb, BOOLEAN Translate,
                 uint32 Lba, uint32 BCount, BOOLEAN Direct, int8* Buffer,
                 PSIZE_T WrittenBytes)
{
    NTSTATUS Status;
    uint32 Bytes;
    UNREFERENCED_PARAMETER(Translate);
    if (!WrittenBytes)
        return STATUS_INVALID_PARAMETER;
    *WrittenBytes = 0;
    if (!BCount)
        return STATUS_SUCCESS;
    if (!Buffer || !Vcb->SectorSize || Vcb->SectorShift >= 30 ||
        Vcb->SectorSize != (1UL << Vcb->SectorShift) ||
        BCount > (MAXULONG >> Vcb->SectorShift))
        return STATUS_INVALID_PARAMETER;
    Bytes = BCount << Vcb->SectorShift;
    Status = UDFCheckVolumeByteSpan(Vcb, (int64)Lba << Vcb->SectorShift, Bytes);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = UDFOpenIntegrityForWrite(IrpContext, Vcb, Direct);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = UDFReadWriteSectors(IrpContext, Vcb, (int64)Lba << Vcb->SectorShift,
                                Bytes, TRUE, Buffer, TRUE);
    if (Status == STATUS_SUCCESS)
        *WrittenBytes = Bytes;
    return Status;
} // end UDFWriteSectors()

/*
    This routine writes data inside a single physical sector (read-modify-write).
 */
static
NTSTATUS
UDFWriteInSector(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN uint32 Lba,
    IN uint32 Offset,
    IN uint32 Length,
    IN int8* Buffer,
    OUT PSIZE_T WrittenBytes
    )
{
    int8* tmp_buff;
    NTSTATUS status;

    status = UDFOpenIntegrityForWrite(IrpContext, Vcb, FALSE);
    if (!NT_SUCCESS(status)) {
        *WrittenBytes = 0;
        return status;
    }

    if (Vcb->CDR_Mode) {
        if (Vcb->SessionEndLba < Lba)
            Vcb->SessionEndLba = Lba;
    }

    *WrittenBytes = 0;

    tmp_buff = (int8*)MyAllocatePool__(NonPagedPool, Vcb->SectorSize);
    if (!tmp_buff) {
        BrutePoint();
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // Read-modify-write cycle.
    status = UDFReadWriteSectors(IrpContext, Vcb,
                 ((LONGLONG)Lba) << Vcb->SectorShift,
                 Vcb->SectorSize,
                 TRUE, tmp_buff, FALSE);
    if (!NT_SUCCESS(status)) goto done;

    /* Equal primary bytes do not establish that an independent metadata
       mirror committed this generation. A failed or short completion can
       follow a completed primary write, before the paired mirror write.
       Keep the paired writer on retry even when this primary slice matches.
       In CDR_Mode, equal writes still advance the next writable address. */
    if (!Vcb->CDR_Mode &&
        (!(Vcb->MetadataFlags & UDF_METADATA_DUPLICATED) ||
         UDFPhysLbaToMetadata(Vcb, Lba) == LBA_OUT_OF_EXTENT) &&
        RtlCompareMemory(tmp_buff + Offset, Buffer, Length) == Length) {
        *WrittenBytes = Length;
        goto done;
    }

    RtlCopyMemory(tmp_buff + Offset, Buffer, Length);

    status = UDFReadWriteSectors(IrpContext, Vcb,
                 ((LONGLONG)Lba) << Vcb->SectorShift,
                 Vcb->SectorSize,
                 TRUE, tmp_buff, TRUE);
    if (NT_SUCCESS(status))
        *WrittenBytes = Length;

done:
    MyFreePool__(tmp_buff);

    ASSERT(NT_SUCCESS(status));
    if (!NT_SUCCESS(status)) {
        UDFPrint(("UDFWriteInSector() for LBA %x failed\n", Lba));
    }

    return status;
} // end UDFWriteInSector()

/*
    This routine writes data at unaligned offset & length.
 */
NTSTATUS
UDFWriteData(
    PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN BOOLEAN Translate,
    IN int64 Offset,
    IN SIZE_T Length,
    IN BOOLEAN Direct,
    IN int8* Buffer,
    OUT PSIZE_T WrittenBytes
    )
{
    uint32 i, l, Lba, BS = Vcb->SectorSize;
    uint32 BSh = Vcb->SectorShift;
    NTSTATUS status;
    SIZE_T _WrittenBytes;

    UNREFERENCED_PARAMETER(Translate);
    if (!WrittenBytes) return STATUS_INVALID_PARAMETER;
    *WrittenBytes = 0;
    if (!Length) return STATUS_SUCCESS;
    if (!Buffer) return STATUS_INVALID_PARAMETER;
    status = UDFCheckVolumeByteSpan(Vcb, Offset, Length);
    if (!NT_SUCCESS(status)) return status;

    status = UDFOpenIntegrityForWrite(IrpContext, Vcb, Direct);
    if (!NT_SUCCESS(status))
        return status;


    // Write tail of the 1st sector if Offset is not sector-aligned.
    Lba = (uint32)(Offset >> BSh);
    if ((i = ((uint32)Offset & (BS - 1)))) {
        l = min(BS - i, (uint32)Length);
        status = UDFWriteInSector(IrpContext, Vcb, Lba, i, l, Buffer, WrittenBytes);
        if (!NT_SUCCESS(status)) return status;
        Length -= l;
        if (!Length) return STATUS_SUCCESS;
        Lba++;
        Buffer += l;
    }

    // Write sector-aligned middle part.
    if (Length >= BS) {
        uint32 AlignedBytes = ((uint32)(Length >> BSh)) << BSh;
        status = UDFReadWriteSectors(IrpContext, Vcb,
                     ((LONGLONG)Lba) << BSh,
                     AlignedBytes,
                     TRUE, Buffer, TRUE);

        _WrittenBytes = NT_SUCCESS(status) ? AlignedBytes : 0;
        *WrittenBytes += _WrittenBytes;
        if (!NT_SUCCESS(status)) return status;

        Lba += AlignedBytes >> BSh;
        Buffer += AlignedBytes;
        Length -= AlignedBytes;
    }

    if (!Length) return STATUS_SUCCESS;

    status = UDFWriteInSector(IrpContext, Vcb, Lba, 0, (uint32)Length, Buffer, &_WrittenBytes);
    *WrittenBytes += _WrittenBytes;

    return status;
} // end UDFWriteData()
