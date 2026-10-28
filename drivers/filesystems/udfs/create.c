////////////////////////////////////////////////////////////////////
// Copyright (C) Alexander Telyatnikov, Ivan Keliukh, Yegor Anchishkin, SKIF Software, 1999-2013. Kiev, Ukraine
// All rights reserved
// This file was released under the GPLv2 on June 2015.
////////////////////////////////////////////////////////////////////
/*************************************************************************
*
* File: Create.cpp
*
* Module: UDF File System Driver (Kernel mode execution only)
*
* Description:
*   Contains code to handle the "Create"/"Open" dispatch entry point.
*
*************************************************************************/

#include            "udffs.h"

#define IsFileObjectReadOnly(FO) (!((FO)->WriteAccess | (FO)->DeleteAccess))

// define the file specific bug-check id
#define         UDF_BUG_CHECK_ID                UDF_FILE_CREATE

#define         MEM_USOBJ_TAG                   "US_Obj"

_When_(RelatedTypeOfOpen != UnopenedFileObject, _At_(RelatedCcb, _In_))
_When_(RelatedTypeOfOpen == UnopenedFileObject, _At_(RelatedCcb, _In_opt_))
_When_(RelatedTypeOfOpen != UnopenedFileObject, _At_(RelatedFileName, _In_))
_When_(RelatedTypeOfOpen == UnopenedFileObject, _At_(RelatedFileName, _In_opt_))
NTSTATUS
UDFNormalizeFileNames(
    _Inout_ PIRP_CONTEXT IrpContext,
    _In_ PVCB Vcb,
    _In_ BOOLEAN OpenByFileId,
    _In_ TYPE_OF_OPEN RelatedTypeOfOpen,
    PCCB RelatedCcb,
    PUNICODE_STRING RelatedFileName,
    _Inout_ PUNICODE_STRING FileName,
    _Inout_ PUNICODE_STRING RemainingName
    );

_Requires_lock_held_(_Global_critical_region_)
NTSTATUS
UDFCompleteFcbOpen(
    _In_ PIRP_CONTEXT IrpContext,
    _In_ PIO_STACK_LOCATION IrpSp,
    _In_ PVCB Vcb,
    _Inout_ PFCB *CurrentFcb,
    _In_opt_ PUDF_FILE_INFO OpenFileInfo,
    _In_ TYPE_OF_OPEN TypeOfOpen,
    _In_ ULONG UserCcbFlags,
    _In_ ULONG CreateDisposition,
    _In_ BOOLEAN VcbLocked,
    _In_ ULONG DesiredInformation
    );

NTSTATUS
UDFOpenObjectByFileId(
    _In_ PIRP_CONTEXT IrpContext,
    _In_ PIO_STACK_LOCATION IrpSp,
    _In_ PVCB Vcb,
    _Inout_ PFCB *CurrentFcb
    );

/* These ICBs are internal allocation authorities, not ordinary files even
 * if a user supplies their physical ID or a malformed named FID points at
 * one. Only the internal loader/growth paths may mutate their mappings. */
static BOOLEAN
UDFIsReservedMetadataFile(PUDF_FILE_INFO FileInfo)
{
    UCHAR Type;
    if (!FileInfo || !FileInfo->Dloc || !FileInfo->Dloc->FileEntry)
        return TRUE;
    Type = ((PFILE_ENTRY)FileInfo->Dloc->FileEntry)->icbTag.fileType;
    return Type == UDF_FILE_TYPE_METADATA ||
           Type == UDF_FILE_TYPE_METADATA_MIRROR ||
           Type == UDF_FILE_TYPE_METADATA_BITMAP;
}

NTSTATUS
UDFSupersedeOrOverwriteFile(
    IN PIRP_CONTEXT IrpContext,
    IN PFILE_OBJECT FileObject,
    IN PVCB Vcb,
    IN PFCB Fcb,
    IN PUDF_FILE_INFO FileInfo,
    IN LONGLONG AllocationSize,
    IN ULONG FileAttributes,
    IN BOOLEAN Supersede
    )
{
    NTSTATUS RC;
    ULONG NewFileAttributes;

    if (!NT_SUCCESS(FileInfo->Dloc->ZeroDataError))
        return FileInfo->Dloc->ZeroDataError;

    /* Preserve the prior cached generation before truncating or replacing
     * its allocation. Cc must run before PagingIoResource is acquired. */
    RC = UDFDrainNewFileData(IrpContext, Vcb, FileInfo);
    if (!NT_SUCCESS(RC))
        return RC;

    /* Dropping streams can flush the owning EFE. Keep it outside the base
     * PagingIoResource, and refuse an undeletable stream before destroying
     * the main payload. Recheck truncation under PagingIoResource below. */
    if (!UDFIsAStream(FileInfo) && !UDFIsAStreamDir(FileInfo) &&
        UDFHasAStreamDir(FileInfo)) {
        if (!MmCanFileBeTruncated(&Fcb->FcbNonpaged->SegmentObject, &UdfData.UDFLargeZero))
            return STATUS_USER_MAPPED_FILE;
        RC = UDFDeleteAllStreams(IrpContext, Vcb, FileInfo);
        if (!NT_SUCCESS(RC))
            return RC;
    }

    UDFAcquirePagingIoExclusive(IrpContext, Fcb);

    _SEH2_TRY {

        if (!MmCanFileBeTruncated(&Fcb->FcbNonpaged->SegmentObject, &UdfData.UDFLargeZero)) {

            AdPrint(("    Can't truncate. File is mapped\n"));
            try_return(RC = STATUS_USER_MAPPED_FILE);
        }

        // Truncate file to zero
        RC = UDFResizeFile__(IrpContext, Vcb, FileInfo, 0);

        if (!NT_SUCCESS(RC)) {

            AdPrint(("    Error during resize operation\n"));
            try_return(RC);
        }

        // Set file sizes
        Fcb->Header.AllocationSize.QuadPart = UDFSysGetAllocSize(Vcb, AllocationSize);
        Fcb->Header.FileSize.QuadPart = 0;
        Fcb->Header.ValidDataLength.QuadPart = 0;
        Fcb->FcbState &= ~UDF_FCB_DELAY_CLOSE;

        MmPrint(("    CcSetFileSizes()\n"));
        CcSetFileSizes(FileObject, (PCC_FILE_SIZES)&Fcb->Header.AllocationSize);
        Fcb->NtReqFCBFlags |= UDF_NTREQ_FCB_MODIFIED;

        // Set attributes
        NewFileAttributes = FileAttributes | FILE_ATTRIBUTE_ARCHIVE;
        if (!Supersede) {
            // For Overwrite, combine with current attributes from FCB cache
            NewFileAttributes |= Fcb->FileAttributes;
        }
        // Write back to DirIndex
        UDFAttributesToUDF(UDFDirIndex(UDFGetDirIndexByFileInfo(FileInfo), FileInfo->Index),
                           FileInfo->Dloc->FileEntry, NewFileAttributes);
        // Update FCB cache
        Fcb->FileAttributes = NewFileAttributes;


try_exit: NOTHING;

    } _SEH2_FINALLY {

        UDFReleasePagingIo(IrpContext, Fcb);
    } _SEH2_END;

    return RC;
} // end UDFSupersedeOrOverwriteFile()


/*************************************************************************
*
* Function: UDFOpenExistingFcb()
*
* Description:
*   Open an existing FCB. Checks access rights and share access,
*   creates the CCB via UDFCompleteFcbOpen, and handles post-open
*   actions (MmFlushImageSection, cache purge, supersede/overwrite).
*   Caller must validate file/directory type and create disposition
*   before calling this function.
*
* Expected Interrupt Level (for execution) :
*
*  IRQL_PASSIVE_LEVEL
*
* Return Value: STATUS_SUCCESS/Error
*
*************************************************************************/
/* A failed CREATE cannot depend on the I/O manager sending cleanup/close
 * for the rejected FILE_OBJECT. Preserve the already acquired CCB/FI/LCB
 * and handle-count owners before invoking a fallible cache callback. */
static VOID
UDFNoteCreateAbortError(PVCB Vcb, PCCB Ccb, NTSTATUS Status)
{
    if (Status == STATUS_SUCCESS || Status == STATUS_DEVICE_BUSY ||
        Status == STATUS_INSUFFICIENT_RESOURCES) return;
    if (NT_SUCCESS(Status)) Status = STATUS_IO_DEVICE_ERROR;
    if (NT_SUCCESS(Ccb->AbortFirstError)) Ccb->AbortFirstError = Status;
    Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
    InterlockedCompareExchange(&Vcb->MetadataWritebackError, Status, STATUS_SUCCESS);
}

/* Main exclusive; one bounded attempt, without waiting for Cache Manager.
 * FALSE from CcUninitializeCacheMap is not an I/O error. The nonpaged event
 * remains alive through asynchronous completion or an uncertain callback. */
static NTSTATUS
UDFResumeCreateAbort(PIRP_CONTEXT IrpContext, PCCB Ccb)
{
    PFCB Fcb = Ccb->Fcb;
    PVCB Vcb = Fcb->Vcb;
    PFILE_OBJECT FileObject = Ccb->FileObject;
    _SEH2_VOLATILE NTSTATUS Status = STATUS_SUCCESS;
    _SEH2_VOLATILE BOOLEAN Ready = FALSE;
    ASSERT_EXCLUSIVE_FCB(Fcb);
    if (!(Ccb->AbortFlags & UDF_CCB_ABORT_LINKED) ||
        (!FileObject && !(Ccb->AbortFlags & UDF_CCB_ABORT_DETACHED)))
        return STATUS_FILE_CORRUPT_ERROR;
    if (Ccb->AbortFlags & UDF_CCB_ABORT_RUNNING) return STATUS_DEVICE_BUSY;
    Ccb->AbortFlags |= UDF_CCB_ABORT_RUNNING;
    _SEH2_TRY {
        _SEH2_TRY {
            Status = UDFDiscardDeleteOnClose(IrpContext, Ccb, FALSE);
            if (Status != STATUS_SUCCESS) _SEH2_LEAVE;
            if (!(Ccb->AbortFlags & UDF_CCB_ABORT_CACHE_DONE)) {
                if (!(Ccb->AbortFlags & UDF_CCB_ABORT_CACHE_STARTED)) {
                    if (FileObject->PrivateCacheMap) {
                        /* A failed CREATE's I/O manager may invalidate its
                         * device binding. A reference to the body does not
                         * preserve that binding for a new cache invocation. */
                        if (!FileObject->DeviceObject || !FileObject->SectionObjectPointer) {
                            Status = STATUS_FILE_INVALID;
                            _SEH2_LEAVE;
                        }
                        Ccb->AbortCacheEvent = MyAllocatePool__(NonPagedPool,
                                                    sizeof(CACHE_UNINITIALIZE_EVENT));
                        if (!Ccb->AbortCacheEvent) {
                            Status = STATUS_INSUFFICIENT_RESOURCES;
                            _SEH2_LEAVE;
                        }
                        Ccb->AbortCacheEvent->Next = NULL;
                        KeInitializeEvent(&Ccb->AbortCacheEvent->Event, NotificationEvent, FALSE);
                        Ccb->AbortFlags |= UDF_CCB_ABORT_CACHE_STARTED;
                        /* Never repeat an uncertain invocation against the
                         * same cache map, even if it raises before returning. */
                        CcUninitializeCacheMap(FileObject, NULL, Ccb->AbortCacheEvent);
                    } else Ccb->AbortFlags |= UDF_CCB_ABORT_CACHE_DONE;
                }
                if (Ccb->AbortFlags & UDF_CCB_ABORT_CACHE_STARTED) {
                    if (!Ccb->AbortCacheEvent) {
                        Status = STATUS_FILE_CORRUPT_ERROR;
                        _SEH2_LEAVE;
                    }
                    if (!KeReadStateEvent(&Ccb->AbortCacheEvent->Event)) {
                        Status = STATUS_DEVICE_BUSY;
                        _SEH2_LEAVE;
                    }
                    if (FileObject->PrivateCacheMap) {
                        Status = STATUS_PURGE_FAILED;
                        _SEH2_LEAVE;
                    }
                    Ccb->AbortFlags |= UDF_CCB_ABORT_CACHE_DONE;
                }
            }
            if (!(Ccb->AbortFlags & UDF_CCB_ABORT_COUNTS_DONE)) {
                if (Ccb->AbortFlags & UDF_CCB_ABORT_COUNTS_STARTED) {
                    Status = STATUS_FILE_CORRUPT_ERROR; /* ambiguous, never replay */
                    _SEH2_LEAVE;
                }
                UDFLockVcb(IrpContext, Vcb);
                _SEH2_TRY {
                    if (!Fcb->FcbReference || !Fcb->FcbUserReference ||
                        !Vcb->VcbReference || !Vcb->VcbUserReference ||
                        (!FlagOn(Ccb->Flags, UDF_CCB_CLEANED) &&
                         (!Fcb->FcbCleanup || !Vcb->VcbCleanup ||
                          (Ccb->Lcb && !Ccb->Lcb->Reference) ||
                          (FlagOn(Ccb->AbortFileObjectFlags, FO_CACHE_SUPPORTED) &&
                           !Fcb->CachedOpenHandleCount)))) {
                        Status = STATUS_FILE_CORRUPT_ERROR;
                        _SEH2_LEAVE;
                    }
                    if (!FlagOn(Ccb->Flags, UDF_CCB_CLEANED)) {
                        /* Retire the exact accepted counter receipt. A
                         * rejected query may have used a temporary/dummy FO;
                         * never retain that address or fabricate an object
                         * for IoRemoveShareAccess after CREATE returned. */
                        if (Fcb->ShareAccess.OpenCount < Ccb->OpenShareAccess.OpenCount ||
                            Fcb->ShareAccess.Readers < Ccb->OpenShareAccess.Readers ||
                            Fcb->ShareAccess.Writers < Ccb->OpenShareAccess.Writers ||
                            Fcb->ShareAccess.Deleters < Ccb->OpenShareAccess.Deleters ||
                            Fcb->ShareAccess.SharedRead < Ccb->OpenShareAccess.SharedRead ||
                            Fcb->ShareAccess.SharedWrite < Ccb->OpenShareAccess.SharedWrite ||
                            Fcb->ShareAccess.SharedDelete < Ccb->OpenShareAccess.SharedDelete) {
                            Status = STATUS_FILE_CORRUPT_ERROR;
                            _SEH2_LEAVE;
                        }
                        Ccb->AbortFlags |= UDF_CCB_ABORT_COUNTS_STARTED;
                        Fcb->ShareAccess.OpenCount -= Ccb->OpenShareAccess.OpenCount;
                        Fcb->ShareAccess.Readers -= Ccb->OpenShareAccess.Readers;
                        Fcb->ShareAccess.Writers -= Ccb->OpenShareAccess.Writers;
                        Fcb->ShareAccess.Deleters -= Ccb->OpenShareAccess.Deleters;
                        Fcb->ShareAccess.SharedRead -= Ccb->OpenShareAccess.SharedRead;
                        Fcb->ShareAccess.SharedWrite -= Ccb->OpenShareAccess.SharedWrite;
                        Fcb->ShareAccess.SharedDelete -= Ccb->OpenShareAccess.SharedDelete;
                        UDFDecrementCleanupCounts(IrpContext, Fcb);
                        if (Ccb->Lcb) InterlockedDecrement((PLONG)&Ccb->Lcb->Reference);
                        if (FlagOn(Ccb->AbortFileObjectFlags, FO_CACHE_SUPPORTED))
                            Fcb->CachedOpenHandleCount--;
                    }
                    Ccb->AbortFlags |= UDF_CCB_ABORT_COUNTS_STARTED;
                    Fcb->FcbReference--;
                    Fcb->FcbUserReference--;
                    Vcb->VcbReference--;
                    Vcb->VcbUserReference--;
                    Ccb->AbortFlags |= UDF_CCB_ABORT_COUNTS_DONE;
                } _SEH2_FINALLY {
                    UDFUnlockVcb(IrpContext, Vcb);
                } _SEH2_END;
                if (Status != STATUS_SUCCESS) _SEH2_LEAVE;
            }
            if (!(Ccb->AbortFlags & UDF_CCB_ABORT_DETACHED)) {
                PFCB BoundFcb;
                PCCB BoundCcb;
                UDFDecodeFileObject(FileObject, &BoundFcb, &BoundCcb);
                if (BoundFcb != Fcb || BoundCcb != Ccb) {
                    Status = STATUS_FILE_CORRUPT_ERROR;
                    _SEH2_LEAVE;
                }
                UDFSetFileObject(FileObject, UnopenedFileObject, NULL, NULL);
                FileObject->SectionObjectPointer = NULL;
                ClearFlag(FileObject->Flags, FO_CACHE_SUPPORTED);
                Ccb->AbortFlags |= UDF_CCB_ABORT_DETACHED;
            }
            /* A refused decrement may transfer to R464's exact FI ledger.
             * A NULL pointer is its receipt, even if the status is failure. */
            Status = UDFReleaseCreateReference(IrpContext, Vcb, &Ccb->FileInfo, TRUE);
            if (Ccb->FileInfo) _SEH2_LEAVE;
            Ready = TRUE;
        } _SEH2_EXCEPT(FsRtlIsNtstatusExpected(_SEH2_GetExceptionCode()) ?
                      EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
            Status = _SEH2_GetExceptionCode();
            if (Status == STATUS_CANT_WAIT) Status = STATUS_DEVICE_BUSY;
        } _SEH2_END;
    } _SEH2_FINALLY {
        Ccb->AbortFlags &= ~UDF_CCB_ABORT_RUNNING;
        if (_SEH2_AbnormalTermination())
            UDFNoteCreateAbortError(Vcb, Ccb, STATUS_FILE_CORRUPT_ERROR);
    } _SEH2_END;
    if (Status != STATUS_SUCCESS && NT_SUCCESS(Status)) Status = STATUS_IO_DEVICE_ERROR;
    if (!Ready && Status == STATUS_SUCCESS) Status = STATUS_DEVICE_BUSY;
    UDFNoteCreateAbortError(Vcb, Ccb, Status);
    if (!Ready) return Status;

    /* Every cache callback is complete, the FILE_OBJECT is detached, and
     * the FI token was consumed/transferred. No fallible I/O follows. */
    if (Ccb->AbortCacheEvent) {
        MyFreePool__(Ccb->AbortCacheEvent);
        Ccb->AbortCacheEvent = NULL;
    }
    UDFLockVcb(IrpContext, Vcb);
    _SEH2_TRY {
        RemoveEntryList(&Ccb->AbortLinks);
        if (Ccb->AbortFlags & UDF_CCB_ABORT_COUNTED) {
            if (Vcb->AbortedOpenCount > 0 && Vcb->AbortedOpenCount != MAXLONG)
                InterlockedDecrement(&Vcb->AbortedOpenCount);
            else {
                InterlockedExchange(&Vcb->AbortedOpenCount, MAXLONG);
                Status = STATUS_FILE_CORRUPT_ERROR;
                UDFNoteCreateAbortError(Vcb, Ccb, Status);
            }
        }
        Ccb->AbortFlags &= ~(UDF_CCB_ABORT_LINKED | UDF_CCB_ABORT_COUNTED);
    } _SEH2_FINALLY {
        UDFUnlockVcb(IrpContext, Vcb);
    } _SEH2_END;
    Ccb->FileObject = NULL;
    UDFDeleteCcb(Ccb);
    if (!Fcb->FcbReference) UDFScheduleFcbTeardownRetry(IrpContext, Fcb);
    if (FileObject) ObDereferenceObject(FileObject); /* cached abort's owned FO only */
    return Status;
}

static NTSTATUS
UDFUnwindCompletedOpen(PIRP_CONTEXT IrpContext, PFILE_OBJECT FileObject,
                        PUDF_FILE_INFO* LocalOpenReference)
{
    PFCB Fcb;
    PCCB Ccb;
    TYPE_OF_OPEN Type;
    if (!FileObject) return STATUS_SUCCESS;
    Type = UDFDecodeFileObject(FileObject, &Fcb, &Ccb);
    if (Type <= StreamFileOpen || !Fcb || !Ccb) return STATUS_SUCCESS;
    ASSERT_EXCLUSIVE_FCB(Fcb);
    if (!(Ccb->AbortFlags & UDF_CCB_ABORT_LINKED)) {
        /* No allocation or callback before ownership is reachable. The
         * existing open counts keep FCB/VCB alive; queue guards persist if
         * a later phase has already consumed those counts. */
        Ccb->AbortFileObjectFlags = FileObject->Flags;
        if (FileObject->PrivateCacheMap) {
            /* Cache Manager already requires a referenceable real object.
             * Keep only this cache-bearing object's body for its completion
             * event; do not extend the lifetime of ordinary dummy queries. */
            ObReferenceObject(FileObject);
        } else {
            Ccb->AbortFlags |= UDF_CCB_ABORT_CACHE_DONE | UDF_CCB_ABORT_DETACHED;
            UDFSetFileObject(FileObject, UnopenedFileObject, NULL, NULL);
            FileObject->SectionObjectPointer = NULL;
            ClearFlag(FileObject->Flags, FO_CACHE_SUPPORTED);
            Ccb->FileObject = NULL; /* no address beyond this CREATE lifetime */
        }
        UDFLockVcb(IrpContext, Fcb->Vcb);
        InsertTailList(&Fcb->AbortedOpenQueue, &Ccb->AbortLinks);
        Ccb->AbortFlags |= UDF_CCB_ABORT_LINKED;
        if (Fcb->Vcb->AbortedOpenCount >= 0 && Fcb->Vcb->AbortedOpenCount < MAXLONG) {
            InterlockedIncrement(&Fcb->Vcb->AbortedOpenCount);
            Ccb->AbortFlags |= UDF_CCB_ABORT_COUNTED;
        } else {
            InterlockedExchange(&Fcb->Vcb->AbortedOpenCount, MAXLONG);
            UDFNoteCreateAbortError(Fcb->Vcb, Ccb, STATUS_FILE_CORRUPT_ERROR);
        }
        UDFUnlockVcb(IrpContext, Fcb->Vcb);
    } else {
        if (LocalOpenReference && *LocalOpenReference == Ccb->FileInfo)
            *LocalOpenReference = NULL;
        return NT_SUCCESS(Ccb->AbortFirstError) ? STATUS_DEVICE_BUSY : Ccb->AbortFirstError;
    }
    if (LocalOpenReference && *LocalOpenReference == Ccb->FileInfo)
        *LocalOpenReference = NULL; /* same token now belongs to abort inventory */
    return UDFResumeCreateAbort(IrpContext, Ccb);
}

/* One explicit volume-flush pass over a pinned FCB snapshot. No generic
 * table restart pointer or table/VCB mutex is held across cache callbacks. */
NTSTATUS
UDFRetryVolumeCreateAborts(PIRP_CONTEXT IrpContext, PVCB Vcb)
{
    PFCB* Files;
    PFCB Fcb;
    PVOID Restart = NULL;
    ULONG Capacity, Index;
    _SEH2_VOLATILE ULONG Count = 0;
    _SEH2_VOLATILE BOOLEAN TableLocked = FALSE, VcbLocked = FALSE;
    _SEH2_VOLATILE NTSTATUS Status = STATUS_SUCCESS;
    ASSERT_EXCLUSIVE_VCB(Vcb);
    if (!InterlockedCompareExchange(&Vcb->AbortedOpenCount, 0, 0)) return STATUS_SUCCESS;
    UDFLockFcbTable(IrpContext, Vcb);
    Capacity = RtlNumberGenericTableElements(&Vcb->FcbTable);
    UDFUnlockFcbTable(IrpContext, Vcb);
    if (Capacity >= MAXULONG / sizeof(PFCB)) return STATUS_INSUFFICIENT_RESOURCES;
    Capacity++; /* the volume DASD may not be indexed */
    Files = MyAllocatePool__(NonPagedPool, Capacity * sizeof(PFCB));
    if (!Files) return STATUS_INSUFFICIENT_RESOURCES;
    _SEH2_TRY {
        UDFLockFcbTable(IrpContext, Vcb); TableLocked = TRUE;
        UDFLockVcb(IrpContext, Vcb); VcbLocked = TRUE;
        Fcb = Vcb->VolumeDasdFcb;
        for (;;) {
            if (Fcb && !IsListEmpty(&Fcb->AbortedOpenQueue)) {
                if (Count == Capacity || Fcb->TeardownInProgress ||
                    Fcb->FcbReference == MAXULONG || Vcb->VcbReference == MAXULONG) {
                    Status = STATUS_FILE_CORRUPT_ERROR;
                    break;
                }
                Fcb->FcbReference++; Vcb->VcbReference++;
                Files[Count++] = Fcb;
            }
            do { Fcb = UDFGetNextFcb(IrpContext, Vcb, &Restart); }
            while (Fcb && Fcb == Vcb->VolumeDasdFcb);
            if (!Fcb) break;
        }
        VcbLocked = FALSE; UDFUnlockVcb(IrpContext, Vcb);
        TableLocked = FALSE; UDFUnlockFcbTable(IrpContext, Vcb);
        for (Index = 0; Status == STATUS_SUCCESS && Index < Count; Index++) {
            Fcb = Files[Index];
            if (!UDFAcquireFcbExclusive(IrpContext, Fcb, TRUE)) {
                Status = STATUS_DEVICE_BUSY;
                break;
            }
            _SEH2_TRY {
                while (!IsListEmpty(&Fcb->AbortedOpenQueue)) {
                    PCCB Ccb = CONTAINING_RECORD(Fcb->AbortedOpenQueue.Flink, struct CCB, AbortLinks);
                    Status = UDFResumeCreateAbort(IrpContext, Ccb);
                    if (Status != STATUS_SUCCESS) break;
                }
            } _SEH2_FINALLY {
                UDFReleaseFcb(IrpContext, Fcb);
            } _SEH2_END;
        }
    } _SEH2_FINALLY {
        if (VcbLocked) UDFUnlockVcb(IrpContext, Vcb);
        if (TableLocked) UDFUnlockFcbTable(IrpContext, Vcb);
        UDFLockVcb(IrpContext, Vcb);
        _SEH2_TRY {
            for (Index = 0; Index < Count; Index++) {
                Fcb = Files[Index];
                ASSERT(Fcb->FcbReference && Vcb->VcbReference);
                Fcb->FcbReference--; Vcb->VcbReference--;
                if (!Fcb->FcbReference && FlagOn(Fcb->FcbState, FCB_STATE_IN_FCB_TABLE) &&
                    !Fcb->TeardownRetryPending) {
                    Fcb->TeardownRetryPending = TRUE;
                    InterlockedIncrement(&Vcb->ClosedFcbRetryCount);
                }
            }
        } _SEH2_FINALLY {
            UDFUnlockVcb(IrpContext, Vcb);
            MyFreePool__(Files);
        } _SEH2_END;
    } _SEH2_END;
    if (Status == STATUS_SUCCESS && InterlockedCompareExchange(&Vcb->AbortedOpenCount, 0, 0))
        Status = STATUS_DEVICE_BUSY;
    return Status;
}

/* Coherence work is fallible admission, not a post-success open side
 * effect. CREATE already holds VCB; a parent Main may be held too. Take
 * child Main only without waiting or upgrading a borrowed shared hold. */
static NTSTATUS
UDFPrepareNoncachedOpen(PIRP_CONTEXT IrpContext, PVCB Vcb,
                        PFCB Fcb, PUDF_FILE_INFO FileInfo)
{
    _SEH2_VOLATILE NTSTATUS Status = STATUS_SUCCESS;
    BOOLEAN MainAcquired = FALSE;
    if (!ExIsResourceAcquiredExclusiveLite(&Fcb->FcbNonpaged->FcbResource)) {
        if (ExIsResourceAcquiredSharedLite(&Fcb->FcbNonpaged->FcbResource) ||
            !UDFAcquireFcbExclusive(IrpContext, Fcb, TRUE))
            return STATUS_SHARING_VIOLATION;
        MainAcquired = TRUE;
    }
    _SEH2_TRY {
        _SEH2_TRY {
            /* Recheck after acquisition. A new cached open is permitted,
             * but it must not have a dirty generation purged beneath it. */
            if (!Fcb->CachedOpenHandleCount &&
                Fcb->FcbNonpaged->SegmentObject.DataSectionObject) {
                IO_STATUS_BLOCK IoStatus;
                IoStatus.Status = STATUS_SUCCESS;
                IoStatus.Information = 0;
                CcFlushCache(&Fcb->FcbNonpaged->SegmentObject, NULL, 0, &IoStatus);
                Status = IoStatus.Status;
                if (Status != STATUS_SUCCESS) {
                    if (NT_SUCCESS(Status)) Status = STATUS_IO_DEVICE_ERROR;
                    UDFRememberMetadataWritebackError(IrpContext, Vcb, FileInfo, Status);
                } else if (!CcPurgeCacheSection(&Fcb->FcbNonpaged->SegmentObject,
                                                NULL, 0, FALSE)) {
                    Status = STATUS_PURGE_FAILED;
                }
            }
        } _SEH2_EXCEPT(FsRtlIsNtstatusExpected(_SEH2_GetExceptionCode()) ?
                      EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
            Status = _SEH2_GetExceptionCode();
            UDFRememberMetadataWritebackError(IrpContext, Vcb, FileInfo, Status);
        } _SEH2_END;
    } _SEH2_FINALLY {
        if (MainAcquired) UDFReleaseFcb(IrpContext, Fcb);
    } _SEH2_END;
    return Status;
}

/* A failed fresh CREATE transfers a real ordinary FI hold to this Dloc
 * owner. It does not re-use a CCB's accepted hold or infer creation from a
 * successful open. Publication is non-I/O; retry belongs to a volume flush. */
#define UDF_CREATE_ROLLBACK_NONE       0
#define UDF_CREATE_ROLLBACK_READY      1
#define UDF_CREATE_ROLLBACK_UNLINK     2
#define UDF_CREATE_ROLLBACK_COMMITTED  3
#define UDF_CREATE_ROLLBACK_RELEASE    4
#define UDF_CREATE_ROLLBACK_UNCERTAIN  5
#define UDF_CREATE_ROLLBACK_FLUSH      6

static NTSTATUS
UDFQueueCreateRollback(PIRP_CONTEXT IrpContext, PFILE_OBJECT FileObject,
    PUDF_FILE_INFO* OpenReference, NTSTATUS CreateStatus)
{
    PUDF_FILE_INFO FileInfo = *OpenReference;
    PVCB Vcb = IrpContext->Vcb;
    PUDF_DATALOC_INFO Dloc;
    PFCB BoundFcb = NULL;
    PCCB Ccb = NULL;
    TYPE_OF_OPEN Type;
    _SEH2_VOLATILE PUDF_FILE_INFO Extra = NULL;
    _SEH2_VOLATILE NTSTATUS Status = STATUS_SUCCESS;
    LONG Count;
    if (!FileInfo || !(Dloc = FileInfo->Dloc) || FileInfo->Constructing ||
        FileInfo->ConstructionFailed || !FileInfo->RefCount ||
        !FileInfo->ParentFile || !FileInfo->ParentFile->Dloc)
        return STATUS_FILE_INVALID;
    Type = UDFDecodeFileObject(FileObject, &BoundFcb, &Ccb);
    if (Type > StreamFileOpen) {
        if (!Ccb || !BoundFcb || !BoundFcb->FileInfo || Ccb->FileInfo != FileInfo ||
            BoundFcb->FileInfo->Dloc != Dloc)
            return STATUS_FILE_CORRUPT_ERROR;
        /* CCB already accepted the original reference. Reserve a separate
         * checked hold before its existing abort path consumes that token. */
        Status = UDFReferenceFileChecked(Vcb, FileInfo);
        if (Status != STATUS_SUCCESS) return Status;
        Extra = FileInfo;
    }
    _SEH2_TRY {
        UDFAcquireResourceExclusive(&Vcb->DlocResource2, TRUE);
        _SEH2_TRY {
            if (Dloc->CreateRollbackOwner || FileInfo->CreateRollbackPhase ||
                FileInfo->CreateRollbackCounted || Dloc->CleanupOwner ||
                Dloc->NamespaceScratchOwner || Dloc->StreamDeleteContextOwner ||
                Dloc->StreamCreate || !FileInfo->RefCount || Vcb->CreateRollbackCount < 0 ||
                Vcb->CreateRollbackCount >= MAXLONG - 1) {
                Status = STATUS_FILE_CORRUPT_ERROR;
                _SEH2_LEAVE;
            }
            for (;;) {
                Count = InterlockedCompareExchange(&Vcb->PendingNamespaceCount, 0, 0);
                if (Count < 0 || Count >= MAXLONG - 1) {
                    Status = STATUS_FILE_CORRUPT_ERROR;
                    _SEH2_LEAVE;
                }
                if (InterlockedCompareExchange(&Vcb->PendingNamespaceCount,
                                               Count + 1, Count) == Count)
                    break;
            }
            if (Status != STATUS_SUCCESS) _SEH2_LEAVE;
            FileInfo->CreateFailureStatus = CreateStatus;
            FileInfo->CreateRollbackFirstError = STATUS_SUCCESS;
            FileInfo->CreateRollbackLastError = STATUS_SUCCESS;
            FileInfo->CreateRollbackTouched = FALSE;
            FileInfo->CreateRollbackCounted = TRUE;
            FileInfo->CreateRollbackPhase = UDF_CREATE_ROLLBACK_READY;
            Dloc->CreateRollbackOwner = FileInfo;
            InsertTailList(&Vcb->CreateRollbackQueue, &FileInfo->CreateRollbackLinks);
            InterlockedIncrement(&Vcb->CreateRollbackCount);
            /* One callback-free handoff. With a CCB, Extra is the new owner;
             * otherwise the caller's original ordinary hold moves here. */
            Extra = NULL;
            *OpenReference = NULL;
        } _SEH2_FINALLY {
            UDFReleaseResource(&Vcb->DlocResource2);
        } _SEH2_END;
    } _SEH2_FINALLY {
        if (Extra)
            UDFReleaseCreateReference(IrpContext, Vcb,
                                       (PUDF_FILE_INFO*)&Extra, FALSE);
        if (_SEH2_AbnormalTermination() || Status == STATUS_FILE_CORRUPT_ERROR) {
            Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
            InterlockedCompareExchange(&Vcb->MetadataWritebackError,
                                        STATUS_FILE_CORRUPT_ERROR, STATUS_SUCCESS);
        }
    } _SEH2_END;
    return Status;
}

static NTSTATUS
UDFResumeCreateRollback(PIRP_CONTEXT IrpContext, PVCB Vcb,
                         PUDF_FILE_INFO FileInfo)
{
    PUDF_DATALOC_INFO Dloc = FileInfo->Dloc;
    PFCB Fcb = Dloc->CommonFcb;
    PFCB Parent = FileInfo->ParentFile ? FileInfo->ParentFile->Fcb : NULL;
    _SEH2_VOLATILE BOOLEAN MainHeld = FALSE, ParentHeld = FALSE;
    BOOLEAN WasWriter = BooleanFlagOn(IrpContext->Flags, IRP_CONTEXT_FLAG_METADATA_WRITER);
    _SEH2_VOLATILE NTSTATUS Status = STATUS_DEVICE_BUSY;
    ASSERT_EXCLUSIVE_VCB(Vcb);
    if (Dloc->CreateRollbackOwner != FileInfo || !FileInfo->CreateRollbackCounted)
        return STATUS_FILE_CORRUPT_ERROR;
    if (FileInfo->CreateRollbackPhase == UDF_CREATE_ROLLBACK_FLUSH ||
        FileInfo->CreateRollbackPhase == UDF_CREATE_ROLLBACK_UNLINK ||
        FileInfo->CreateRollbackPhase == UDF_CREATE_ROLLBACK_RELEASE)
        return STATUS_DEVICE_BUSY; /* a recursive flush cannot replay this owner */
    if (FileInfo->CreateRollbackPhase != UDF_CREATE_ROLLBACK_READY &&
        FileInfo->CreateRollbackPhase != UDF_CREATE_ROLLBACK_COMMITTED)
        return NT_SUCCESS(FileInfo->CreateRollbackFirstError) ?
               STATUS_FILE_CORRUPT_ERROR : FileInfo->CreateRollbackFirstError;
    if (FileInfo->CreateRollbackPhase == UDF_CREATE_ROLLBACK_READY &&
        FlagOn(Vcb->VcbState, VCB_STATE_VOLUME_READ_ONLY))
        return NT_SUCCESS(FileInfo->CreateRollbackFirstError) ?
               FileInfo->CreateFailureStatus : FileInfo->CreateRollbackFirstError;
    if ((Fcb && (!Fcb->Header.Resource || Fcb->TeardownInProgress)) ||
        (Parent && (!Parent->Header.Resource || Parent->TeardownInProgress)))
        return STATUS_DEVICE_BUSY;
    if (Fcb && !ExIsResourceAcquiredExclusiveLite(Fcb->Header.Resource)) {
        if (ExIsResourceAcquiredSharedLite(Fcb->Header.Resource) ||
            !UDFAcquireFcbExclusive(IrpContext, Fcb, TRUE))
            return STATUS_DEVICE_BUSY;
        MainHeld = TRUE;
    }
    SetFlag(IrpContext->Flags, IRP_CONTEXT_FLAG_METADATA_WRITER);
    _SEH2_TRY {
        _SEH2_TRY {
            if (Parent && Parent != Fcb &&
                !ExIsResourceAcquiredExclusiveLite(Parent->Header.Resource)) {
                if (ExIsResourceAcquiredSharedLite(Parent->Header.Resource) ||
                    !UDFAcquireFcbExclusive(IrpContext, Parent, TRUE))
                    _SEH2_LEAVE;
                ParentHeld = TRUE;
            }
            if (FileInfo->CreateRollbackPhase == UDF_CREATE_ROLLBACK_READY) {
                if (!Parent || !FileInfo->FileIdent) {
                    Status = STATUS_FILE_CORRUPT_ERROR;
                    _SEH2_LEAVE;
                }
                /* The old failed CCB/cache owner must finish separately.
                 * A mapping, another handle or another exact FI is not ours
                 * to close merely to make rollback appear successful. */
                if (FileInfo->RefCount != 1 || FileInfo->OpenCount ||
                    (Fcb && (Fcb->FcbCleanup || !IsListEmpty(&Fcb->AbortedOpenQueue) ||
                     !MmCanFileBeTruncated(&Fcb->FcbNonpaged->SegmentObject,
                                           &UdfData.UDFLargeZero))))
                    _SEH2_LEAVE;
                FileInfo->CreateRollbackPhase = UDF_CREATE_ROLLBACK_FLUSH;
                Status = UDFFlushFile__(IrpContext, Vcb, FileInfo, 0);
                FileInfo->CreateRollbackPhase = UDF_CREATE_ROLLBACK_READY;
                if (Status != STATUS_SUCCESS) _SEH2_LEAVE;
                FileInfo->CreateRollbackPhase = UDF_CREATE_ROLLBACK_UNLINK;
                FileInfo->CreateRollbackTouched = FALSE;
                Status = UDFUnlinkFileEx__(IrpContext, Vcb, FileInfo, TRUE,
                                           &FileInfo->CreateRollbackTouched);
                if (Status != STATUS_SUCCESS) {
                    FileInfo->CreateRollbackPhase = FileInfo->CreateRollbackTouched ?
                        UDF_CREATE_ROLLBACK_UNCERTAIN : UDF_CREATE_ROLLBACK_READY;
                    if (FileInfo->CreateRollbackTouched)
                        Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
                    _SEH2_LEAVE;
                }
                FileInfo->CreateRollbackPhase = UDF_CREATE_ROLLBACK_COMMITTED;
            }
            /* Unlink has a durable receipt. Retry only reference retirement,
             * never the namespace operation or an already spent close. */
            UDFAcquireResourceExclusive(&Vcb->DlocResource2, TRUE);
            if (Dloc->CreateRollbackOwner != FileInfo || FileInfo->RefCount != 1 ||
                !FileInfo->CreateRollbackCounted || Vcb->PendingNamespaceCount <= 0 ||
                Vcb->PendingNamespaceCount == MAXLONG || Vcb->CreateRollbackCount <= 0 ||
                Vcb->CreateRollbackCount == MAXLONG) {
                Status = STATUS_FILE_CORRUPT_ERROR;
            } else {
                FileInfo->CreateRollbackPhase = UDF_CREATE_ROLLBACK_RELEASE;
                Status = STATUS_SUCCESS;
            }
            UDFReleaseResource(&Vcb->DlocResource2);
            if (Status != STATUS_SUCCESS) _SEH2_LEAVE;
            Status = UDFDereferenceFileChecked(Vcb, FileInfo);
            if (Status != STATUS_SUCCESS) {
                FileInfo->CreateRollbackPhase = UDF_CREATE_ROLLBACK_COMMITTED;
                _SEH2_LEAVE; /* returned refusal consumed no reference */
            }
            UDFAcquireResourceExclusive(&Vcb->DlocResource2, TRUE);
            RemoveEntryList(&FileInfo->CreateRollbackLinks);
            InitializeListHead(&FileInfo->CreateRollbackLinks);
            Dloc->CreateRollbackOwner = NULL;
            FileInfo->CreateRollbackPhase = UDF_CREATE_ROLLBACK_NONE;
            FileInfo->CreateRollbackCounted = FALSE;
            FileInfo->CleanupPending = TRUE;
            InterlockedDecrement(&Vcb->PendingNamespaceCount);
            InterlockedDecrement(&Vcb->CreateRollbackCount);
            UDFReleaseResource(&Vcb->DlocResource2);
        } _SEH2_EXCEPT(FsRtlIsNtstatusExpected(_SEH2_GetExceptionCode()) ?
                      EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
            Status = _SEH2_GetExceptionCode();
            if (FileInfo->CreateRollbackPhase == UDF_CREATE_ROLLBACK_FLUSH)
                FileInfo->CreateRollbackPhase = UDF_CREATE_ROLLBACK_READY;
            else if (FileInfo->CreateRollbackPhase == UDF_CREATE_ROLLBACK_UNLINK ||
                FileInfo->CreateRollbackPhase == UDF_CREATE_ROLLBACK_RELEASE) {
                FileInfo->CreateRollbackPhase = UDF_CREATE_ROLLBACK_UNCERTAIN;
                Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
            }
        } _SEH2_END;
    } _SEH2_FINALLY {
        if (_SEH2_AbnormalTermination()) {
            FileInfo->CreateRollbackPhase = UDF_CREATE_ROLLBACK_UNCERTAIN;
            Status = STATUS_FILE_CORRUPT_ERROR;
            Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
        }
        if (Status != STATUS_SUCCESS && NT_SUCCESS(Status)) Status = STATUS_IO_DEVICE_ERROR;
        FileInfo->CreateRollbackLastError = Status;
        if (Status != STATUS_SUCCESS) {
            if (Status != STATUS_DEVICE_BUSY) {
                if (NT_SUCCESS(FileInfo->CreateRollbackFirstError))
                    FileInfo->CreateRollbackFirstError = Status;
                InterlockedCompareExchange(&Vcb->MetadataWritebackError,
                                            Status, STATUS_SUCCESS);
            }
        }
        if (ParentHeld) UDFReleaseFcb(IrpContext, Parent);
        if (MainHeld) UDFReleaseFcb(IrpContext, Fcb);
        if (!WasWriter) ClearFlag(IrpContext->Flags, IRP_CONTEXT_FLAG_METADATA_WRITER);
    } _SEH2_END;
    return Status;
}

/* Only active inline owners are visited. Fetch the head anew after each
 * attempt; never keep a list restart pointer across unlink/cache callbacks.
 * The original count bounds the pass if a recursive request adds work. */
NTSTATUS
UDFRetryVolumeCreateRollbacks(PIRP_CONTEXT IrpContext, PVCB Vcb)
{
    LONG Index, Capacity;
    NTSTATUS Status;
    PUDF_FILE_INFO FileInfo;
    ASSERT_EXCLUSIVE_VCB(Vcb);
    Capacity = InterlockedCompareExchange(&Vcb->CreateRollbackCount, 0, 0);
    if (Capacity < 0 || Capacity == MAXLONG) {
        Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
        InterlockedCompareExchange(&Vcb->MetadataWritebackError,
                                    STATUS_FILE_CORRUPT_ERROR, STATUS_SUCCESS);
        return STATUS_FILE_CORRUPT_ERROR;
    }
    if (!Capacity) {
        BOOLEAN Empty;
        UDFAcquireResourceExclusive(&Vcb->DlocResource2, TRUE);
        Empty = IsListEmpty(&Vcb->CreateRollbackQueue);
        UDFReleaseResource(&Vcb->DlocResource2);
        if (Empty) return STATUS_SUCCESS;
        Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
        InterlockedCompareExchange(&Vcb->MetadataWritebackError,
                                    STATUS_FILE_CORRUPT_ERROR, STATUS_SUCCESS);
        return STATUS_FILE_CORRUPT_ERROR;
    }
    for (Index = 0; Index < Capacity; Index++) {
        UDFAcquireResourceExclusive(&Vcb->DlocResource2, TRUE);
        FileInfo = IsListEmpty(&Vcb->CreateRollbackQueue) ? NULL :
            CONTAINING_RECORD(Vcb->CreateRollbackQueue.Flink,
                              UDF_FILE_INFO, CreateRollbackLinks);
        UDFReleaseResource(&Vcb->DlocResource2);
        if (!FileInfo) {
            Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
            InterlockedCompareExchange(&Vcb->MetadataWritebackError,
                                        STATUS_FILE_CORRUPT_ERROR, STATUS_SUCCESS);
            return STATUS_FILE_CORRUPT_ERROR;
        }
        Status = UDFResumeCreateRollback(IrpContext, Vcb, FileInfo);
        if (Status != STATUS_SUCCESS) {
            if (Status == STATUS_FILE_CORRUPT_ERROR) {
                Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
                InterlockedCompareExchange(&Vcb->MetadataWritebackError,
                                            Status, STATUS_SUCCESS);
            }
            return Status;
        }
    }
    return InterlockedCompareExchange(&Vcb->CreateRollbackCount, 0, 0) ?
           STATUS_DEVICE_BUSY : STATUS_SUCCESS;
}


NTSTATUS
UDFOpenExistingFcb(
    IN PIRP_CONTEXT IrpContext,
    IN PIO_STACK_LOCATION IrpSp,
    IN PVCB Vcb,
    IN PFILE_OBJECT FileObject,
    IN PUDF_FILE_INFO NewFileInfo,
    IN OUT PFCB *PtrNewFcb,
    IN BOOLEAN IgnoreCase,
    IN PFILE_OBJECT RelatedFileObject OPTIONAL,
    IN PFCB RelatedFcb OPTIONAL,
    IN ULONG CreateDisposition
    )
{
    NTSTATUS Status;
    ULONG CcbFlags = 0;
    TYPE_OF_OPEN TypeOfOpen;
    ULONG DesiredInformation;
    USHORT TmpFileAttributes;
    PDIR_INDEX_ITEM OpenDirectoryEntry;

    ULONG Options = IrpSp->Parameters.Create.Options;
    USHORT FileAttributes = (USHORT)(IrpSp->Parameters.Create.FileAttributes & ~FILE_ATTRIBUTE_NORMAL);
    LONGLONG AllocationSize = IrpContext->Irp->Overlay.AllocationSize.QuadPart;
    ACCESS_MASK DesiredAccess = IrpSp->Parameters.Create.SecurityContext->DesiredAccess;
    USHORT ShareAccess = IrpSp->Parameters.Create.ShareAccess;
    PACCESS_STATE AccessState = IrpSp->Parameters.Create.SecurityContext->AccessState;
    BOOLEAN DeleteOnClose = BooleanFlagOn(Options, FILE_DELETE_ON_CLOSE);

    PAGED_CODE();
    ASSERT(*PtrNewFcb);
    if (UDFIsReservedMetadataFile(NewFileInfo))
        return STATUS_ACCESS_DENIED;

    if (UDFIsADirectory(NewFileInfo)) {
        if (FlagOn(Options, FILE_NON_DIRECTORY_FILE) ||
            CreateDisposition == FILE_SUPERSEDE ||
            CreateDisposition == FILE_OVERWRITE ||
            CreateDisposition == FILE_OVERWRITE_IF)
            return STATUS_FILE_IS_A_DIRECTORY;
    } else if (FlagOn(Options, FILE_DIRECTORY_FILE)) {
        return STATUS_NOT_A_DIRECTORY;
    }

    // Check share access BEFORE creating CCB/incrementing counts.
    Status = UDFCheckAccessRights(FileObject, AccessState, *PtrNewFcb, NULL, DesiredAccess, ShareAccess);
    if (!NT_SUCCESS(Status)) {
        AdPrint(("    Access/Share access check failed\n"));
        return Status;
    }

    if (DeleteOnClose && ((*PtrNewFcb)->FcbState & UDF_FCB_READ_ONLY)) {
        AdPrint(("    Can't delete Read-Only file\n"));
        return STATUS_CANNOT_DELETE;
    }

    /* Report a held named stream before accepting FILE_DELETE_ON_CLOSE.
     * Otherwise cleanup can refuse the delete after this CREATE has already
     * reported success to its caller.  VCB is held shared here, while stream
     * namespace changes require it exclusive.  A live stream holds the
     * shared Dloc's SDirInfo; the cleanup preflight remains the final guard. */
    if (DeleteOnClose &&
        UDFHasAStreamDir((*PtrNewFcb)->FileInfo) &&
        UDFStreamDirectoryHasLiveHandles(
            (*PtrNewFcb)->FileInfo->Dloc->SDirInfo)) {
        return STATUS_CANNOT_DELETE;
    }

    // Determine type of open based on FCB type
    if (UDFIsADirectory((*PtrNewFcb)->FileInfo)) {
        TypeOfOpen = UserDirectoryOpen;
    } else {
        TypeOfOpen = UserFileOpen;
    }

    // Set CCB flags
    if (IgnoreCase) {
        SetFlag(CcbFlags, CCB_FLAG_IGNORE_CASE);
    }

    // Determine Information based on CreateDisposition
    switch (CreateDisposition) {
    case FILE_SUPERSEDE:
        DesiredInformation = FILE_SUPERSEDED;
        break;
    case FILE_CREATE:
        DesiredInformation = FILE_CREATED;
        break;
    case FILE_OVERWRITE:
    case FILE_OVERWRITE_IF:
        DesiredInformation = FILE_OVERWRITTEN;
        break;
    default:
        DesiredInformation = FILE_OPENED;
        break;
    }

    /* Refuse a failed cache flush/purge before any successful CCB/share
     * count publication. Later direct I/O still enforces per-request cache
     * coherence; this admission does not replace that obligation. */
    if (TypeOfOpen == UserFileOpen && FlagOn(Options, FILE_NO_INTERMEDIATE_BUFFERING) &&
        !(*PtrNewFcb)->CachedOpenHandleCount &&
        (*PtrNewFcb)->FcbNonpaged->SegmentObject.DataSectionObject) {
        Status = UDFPrepareNoncachedOpen(IrpContext, Vcb, *PtrNewFcb, NewFileInfo);
        if (Status != STATUS_SUCCESS) return Status;
    }

    // Create CCB, set share access, increment open counts
    Status = UDFCompleteFcbOpen(IrpContext,
                                IrpSp,
                                Vcb,
                                PtrNewFcb,
                                NewFileInfo,
                                TypeOfOpen,
                                CcbFlags,
                                CreateDisposition,
                                FALSE,               // VcbLocked
                                DesiredInformation);
    if (!NT_SUCCESS(Status)) return Status;

    (*PtrNewFcb)->Header.IsFastIoPossible = UDFIsFastIoPossible(*PtrNewFcb);

    if (!UDFIsADirectory(NewFileInfo)) {
        //  If the user wants 'write access' access to the file make sure there
        //  is not a process mapping this file as an image.  Any attempt to
        //  delete the file will be stopped in fileinfo.cpp
        //
        //  If the user wants to delete on close, we must check at this
        //  point though.
        if ((DesiredAccess & FILE_WRITE_DATA) || DeleteOnClose) {

            MmPrint(("    MmFlushImageSection();\n"));

            if (!MmFlushImageSection(&(*PtrNewFcb)->FcbNonpaged->SegmentObject, MmFlushForWrite)) {

                Status = DeleteOnClose ? STATUS_CANNOT_DELETE :
                                              STATUS_SHARING_VIOLATION;
                AdPrint(("    File is mapped or deletion in progress\n"));
                return Status;
            }
        }

    }

    if (DeleteOnClose && UDFIsADirectory(NewFileInfo) && !UDFIsDirEmpty__(NewFileInfo)) {
        AdPrint(("    Directory in not empry\n"));
        return STATUS_DIRECTORY_NOT_EMPTY;
    }

    // FID attributes belong to this name, not the canonical/by-ID FI.
    OpenDirectoryEntry = UDFDirIndex(UDFGetDirIndexByFileInfo(NewFileInfo),
                                      NewFileInfo->Index);
    TmpFileAttributes = (USHORT)(OpenDirectoryEntry ?
        UDFAttributesToNT(OpenDirectoryEntry, NewFileInfo->Dloc->FileEntry) :
        (*PtrNewFcb)->FileAttributes);

    // Reject DeleteOnClose on readonly files
    if (DeleteOnClose &&
       (TmpFileAttributes & FILE_ATTRIBUTE_READONLY)) {
        AdPrint(("    Read-only file with DeleteOnClose not allowed\n"));
        return STATUS_CANNOT_DELETE;
    }

    // If a supersede or overwrite was requested, do so now ...
    if ((CreateDisposition == FILE_SUPERSEDE) ||
       (CreateDisposition == FILE_OVERWRITE) ||
       (CreateDisposition == FILE_OVERWRITE_IF)) {

        ASSERT(!UDFIsADirectory(NewFileInfo));

        if (CreateDisposition == FILE_SUPERSEDE) {
            BOOLEAN RestoreRO = FALSE;

            // NT wants us to allow Supersede on RO files
            if ((*PtrNewFcb)->FcbState & UDF_FCB_READ_ONLY) {
                RestoreRO = TRUE;
                (*PtrNewFcb)->FcbState &= ~UDF_FCB_READ_ONLY;
            }
            Status = UDFCheckAccessRights(NULL, NULL, *PtrNewFcb, NULL, DELETE, 0);
            if (RestoreRO) {
                (*PtrNewFcb)->FcbState |= UDF_FCB_READ_ONLY;
            }
            if (!NT_SUCCESS(Status)) {
                AdPrint(("    Can't supersede. DELETE permission required\n"));
                return Status;
            }
        } else {
            Status = UDFCheckAccessRights(NULL, NULL, *PtrNewFcb, NULL,
                        FILE_WRITE_DATA | FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES, 0);
            if (!NT_SUCCESS(Status)) {
                AdPrint(("    Can't overwrite. Permission denied\n"));
                return Status;
            }
        }
        // Existing & requested System and Hidden bits must match
        if ( (TmpFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) &
            (FileAttributes ^ (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) ) {
            AdPrint(("    The Hidden and/or System bits do not match\n"));
            return STATUS_ACCESS_DENIED;
        }

        // Truncate file and set attributes
        Status = UDFSupersedeOrOverwriteFile(
            IrpContext,
            FileObject,
            Vcb,
            *PtrNewFcb,
            NewFileInfo,
            AllocationSize,
            FileAttributes,
            (BOOLEAN)(CreateDisposition == FILE_SUPERSEDE)
        );
        if (!NT_SUCCESS(Status)) {
            return Status;
        }

        // notify changes
        UDFNotifyReportChange( IrpContext, Vcb, NewFileInfo->Fcb,
                                   FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_SIZE,
                                   FILE_ACTION_MODIFIED,
                                   NULL, FileObject);

        // Update parent object
        if ((Vcb->CompatFlags & UDF_VCB_IC_UPDATE_DIR_WRITE) &&
           RelatedFcb &&
           RelatedFileObject &&
           (RelatedFcb->FileInfo == NewFileInfo->ParentFile)) {
                RelatedFileObject->Flags |= (FO_FILE_MODIFIED | FO_FILE_SIZE_CHANGED);
        }
    }

    // Newly created file — set attributes, sizes, and send notification
    if (CreateDisposition == FILE_CREATE) {

        // NT requires ARCHIVE on files
        if (!UDFIsADirectory(NewFileInfo))
            FileAttributes |= FILE_ATTRIBUTE_ARCHIVE;

        UDFAttributesToUDF(UDFDirIndex(UDFGetDirIndexByFileInfo(NewFileInfo), NewFileInfo->Index),
                           NewFileInfo->Dloc->FileEntry, FileAttributes);
        (*PtrNewFcb)->FileAttributes = FileAttributes;
        if (FileAttributes & FILE_ATTRIBUTE_READONLY)
            (*PtrNewFcb)->FcbState |= UDF_FCB_READ_ONLY;

        (*PtrNewFcb)->Header.FileSize.QuadPart =
        (*PtrNewFcb)->Header.ValidDataLength.QuadPart = 0;
        if (AllocationSize) {
            (*PtrNewFcb)->Header.AllocationSize.QuadPart = AllocationSize;
            MmPrint(("    CcIsFileCached()\n"));
            if (CcIsFileCached(FileObject)) {
                 MmPrint(("    CcSetFileSizes()\n"));
                 BrutePoint();
                 CcSetFileSizes(FileObject, (PCC_FILE_SIZES)&(*PtrNewFcb)->Header.AllocationSize);
                 (*PtrNewFcb)->NtReqFCBFlags |= UDF_NTREQ_FCB_MODIFIED;
            }
        }

        if (UDFIsAStream(NewFileInfo)) {
            UDFNotifyReportChange( IrpContext, Vcb, *PtrNewFcb,
                                       FILE_NOTIFY_CHANGE_STREAM_NAME,
                                       FILE_ACTION_ADDED_STREAM,
                                       NULL, FileObject);
        } else {
            UDFNotifyReportChange( IrpContext, Vcb, *PtrNewFcb,
                                       UDFIsADirectory(NewFileInfo) ? FILE_NOTIFY_CHANGE_DIR_NAME : FILE_NOTIFY_CHANGE_FILE_NAME,
                                       FILE_ACTION_ADDED,
                                       NULL, FileObject);
        }
    }

    // Update parent object
    if ((Vcb->CompatFlags & UDF_VCB_IC_UPDATE_DIR_READ) &&
       RelatedFcb &&
       RelatedFileObject &&
       (RelatedFcb->FileInfo == NewFileInfo->ParentFile)) {
            RelatedFileObject->Flags |= FO_FILE_FAST_IO_READ;
    }

    return STATUS_SUCCESS;

} // end UDFOpenExistingFcb()


/*************************************************************************
*
* Function: UDFOpenObjectFromDirContext()
*
* Description:
*   Open a file or directory found via directory search (UDFFindDirEntry).
*   Creates FileInfo from disk, creates or finds FCB, acquires FCB lock,
*   creates LCB for intermediate path components.
*
*   When PerformUserOpen is TRUE, also completes the open for the user
*   (access check, CCB creation, supersede/overwrite handling).
*   When FALSE, only sets up internal structures for path traversal.
*
* Expected Interrupt Level (for execution) :
*
*  IRQL_PASSIVE_LEVEL
*
* Return Value: STATUS_SUCCESS/Error
*
*************************************************************************/
/* Current/Previous name resources actually owned by this request. Keep
 * both FCB identities pinned while changing lock order, and publish each
 * acquired/released resource before any next fallible step. */
static NTSTATUS
UDFMoveCreateFcb(PIRP_CONTEXT IrpContext, PVCB Vcb, PFCB Child,
                 PFCB* Current, PFCB* Previous)
{
    PFCB Parent = *Current, Extra;
    BOOLEAN ParentPinned = FALSE, ChildPinned = FALSE;
    _SEH2_VOLATILE NTSTATUS Status = STATUS_SUCCESS;
    if (!Child || !Child->FcbNonpaged || Child->Vcb != Vcb ||
        (Parent && Parent->Vcb != Vcb)) return STATUS_FILE_INVALID;
    if (Child == Parent) return STATUS_SUCCESS;
    UDF_CHECK_PAGING_IO_RESOURCE(Child);
    if (ExIsResourceAcquiredSharedLite(&Child->FcbNonpaged->FcbResource) &&
        !ExIsResourceAcquiredExclusiveLite(&Child->FcbNonpaged->FcbResource))
        return STATUS_SHARING_VIOLATION;
    if (UDFAcquireFcbExclusive(IrpContext, Child, TRUE)) {
        Extra = *Previous;
        *Previous = NULL;
        if (Extra && Extra != Parent) UDFReleaseFcb(IrpContext, Extra);
        *Previous = Parent;
        *Current = Child;
        return STATUS_SUCCESS;
    }
    if (!BooleanFlagOn(IrpContext->Flags, IRP_CONTEXT_FLAG_WAIT))
        return STATUS_DEVICE_BUSY; /* no retry token after FI admission */
    UDFLockVcb(IrpContext, Vcb);
    if (Child->TeardownInProgress || (Parent && Parent->TeardownInProgress))
        Status = STATUS_SHARING_VIOLATION;
    else if (Child->FcbReference == MAXULONG || (Parent && Parent->FcbReference == MAXULONG))
        Status = STATUS_INSUFFICIENT_RESOURCES;
    else {
        Child->FcbReference++;
        ChildPinned = TRUE;
        if (Parent) { Parent->FcbReference++; ParentPinned = TRUE; }
    }
    UDFUnlockVcb(IrpContext, Vcb);
    if (Status != STATUS_SUCCESS) return Status;
    _SEH2_TRY {
        Extra = *Previous;
        *Previous = NULL;
        if (Extra && Extra != Parent) UDFReleaseFcb(IrpContext, Extra);
        *Current = NULL;
        if (Parent) UDFReleaseFcb(IrpContext, Parent);
        if (!UDFAcquireFcbExclusive(IrpContext, Child, FALSE)) {
            Status = STATUS_SHARING_VIOLATION;
            _SEH2_LEAVE;
        }
        *Current = Child;
        if (Parent) {
            UDF_CHECK_PAGING_IO_RESOURCE(Parent);
            /* A waiting upward reacquire can invert another CREATE's
             * parent/child handoff. Keep the acquired child and refuse this
             * attempt if the old parent cannot be reacquired immediately. */
            if (!UDFAcquireFcbExclusive(IrpContext, Parent, TRUE)) {
                Status = STATUS_DEVICE_BUSY;
                _SEH2_LEAVE;
            }
            *Previous = Parent;
        }
    } _SEH2_FINALLY {
        BOOLEAN Failed = _SEH2_AbnormalTermination() || Status != STATUS_SUCCESS;
        UDFLockVcb(IrpContext, Vcb);
        _SEH2_TRY {
            if (ChildPinned) { ASSERT(Child->FcbReference); Child->FcbReference--; }
            if (ParentPinned) { ASSERT(Parent->FcbReference); Parent->FcbReference--; }
            if (Failed && !Child->FcbReference &&
                FlagOn(Child->FcbState, FCB_STATE_IN_FCB_TABLE) && !Child->TeardownRetryPending) {
                Child->TeardownRetryPending = TRUE;
                InterlockedIncrement(&Vcb->ClosedFcbRetryCount);
            }
            if (Failed && Parent && !Parent->FcbReference &&
                FlagOn(Parent->FcbState, FCB_STATE_IN_FCB_TABLE) && !Parent->TeardownRetryPending) {
                Parent->TeardownRetryPending = TRUE;
                InterlockedIncrement(&Vcb->ClosedFcbRetryCount);
            }
        } _SEH2_FINALLY {
            UDFUnlockVcb(IrpContext, Vcb);
        } _SEH2_END;
    } _SEH2_END;
    return Status;
}

/* Teardown only the FCB whose resource slot is actually owned, not a stale
 * last successful path pointer. The previous FCB remains independently
 * pinned through an ancestor walk and through raised cleanup failures. */
static NTSTATUS
UDFTeardownCreateFcb(PIRP_CONTEXT IrpContext, PVCB Vcb,
                     PFCB* Current, PFCB* Previous)
{
    PFCB Target = *Current, Parent = *Previous;
    BOOLEAN ParentPinned = FALSE;
    BOOLEAN WasWriter = BooleanFlagOn(IrpContext->Flags, IRP_CONTEXT_FLAG_METADATA_WRITER);
    _SEH2_VOLATILE BOOLEAN Removed = FALSE;
    _SEH2_VOLATILE NTSTATUS Status = STATUS_SUCCESS;
    if (!Target || Target == Vcb->RootIndexFcb ||
        !ExIsResourceAcquiredExclusiveLite(&Target->FcbNonpaged->FcbResource))
        return STATUS_SUCCESS;
    if (Parent && Parent != Target) {
        UDFLockVcb(IrpContext, Vcb);
        if (Parent->FcbReference == MAXULONG) Status = STATUS_INSUFFICIENT_RESOURCES;
        else { Parent->FcbReference++; ParentPinned = TRUE; }
        UDFUnlockVcb(IrpContext, Vcb);
        if (Status != STATUS_SUCCESS) {
            UDFScheduleFcbTeardownRetry(IrpContext, Target);
            return Status;
        }
    }
    SetFlag(IrpContext->Flags, IRP_CONTEXT_FLAG_METADATA_WRITER);
    _SEH2_TRY {
        _SEH2_TRY {
            Status = UDFTeardownStructuresEx(IrpContext, Target, FALSE, TRUE, (PBOOLEAN)&Removed);
        } _SEH2_EXCEPT(FsRtlIsNtstatusExpected(_SEH2_GetExceptionCode()) ?
                      EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
            Status = _SEH2_GetExceptionCode();
        } _SEH2_END;
    } _SEH2_FINALLY {
        if (Removed) {
            if (*Previous == Target) *Previous = NULL;
            *Current = NULL; /* teardown already consumed this acquisition */
        }
        if (ParentPinned) {
            UDFLockVcb(IrpContext, Vcb);
            ASSERT(Parent->FcbReference);
            Parent->FcbReference--;
            UDFUnlockVcb(IrpContext, Vcb);
            if (!Parent->FcbReference) UDFScheduleFcbTeardownRetry(IrpContext, Parent);
        }
        if (!WasWriter) ClearFlag(IrpContext->Flags, IRP_CONTEXT_FLAG_METADATA_WRITER);
    } _SEH2_END;
    if (Status != STATUS_SUCCESS && Status != STATUS_DEVICE_BUSY)
        InterlockedCompareExchange(&Vcb->MetadataWritebackError, Status, STATUS_SUCCESS);
    return Status;
}

NTSTATUS
UDFOpenObjectFromDirContext(
    IN PIRP_CONTEXT IrpContext,
    IN PIO_STACK_LOCATION IrpSp,
    IN PVCB Vcb,
    IN PDIR_ENUM_CONTEXT DirContext OPTIONAL,
    IN PUDF_FILE_INFO RelatedFileInfo,
    IN OUT PFCB *CurrentFcb,
    IN OUT PFCB *PreviousFcb,
    IN BOOLEAN IgnoreCase,
    IN BOOLEAN PerformUserOpen,
    IN ULONG CreateDisposition,
    IN ULONG RemainingNameLength,
    IN PFILE_OBJECT RelatedFileObject OPTIONAL,
    IN PFCB RelatedFcb OPTIONAL,
    IN PUDF_FILE_INFO ExistingFileInfo OPTIONAL,
    OUT PUDF_FILE_INFO *OutFileInfo,
    OUT PFCB *OutFcb
    )
{
    _SEH2_VOLATILE NTSTATUS Status = STATUS_UNSUCCESSFUL;
    _SEH2_VOLATILE PUDF_FILE_INFO NewFileInfo = NULL;
    _SEH2_VOLATILE PFCB PtrNewFcb = NULL;

    PAGED_CODE();

    *OutFileInfo = NULL;
    *OutFcb = NULL;

    if (ExistingFileInfo) {
        // Use pre-created FileInfo (e.g., from UDFCreateFile__).
        // Skip disk read — FileInfo is already open with valid RefCount.
        NewFileInfo = ExistingFileInfo;
        Status = STATUS_SUCCESS;
    } else {
        // Step 1: Open FileInfo from directory context (read from disk or find cached)
        ASSERT(DirContext);
        _SEH2_TRY {
            _SEH2_TRY {
                Status = UDFOpenFileInfoFromDirContext(IrpContext, Vcb, DirContext,
                                       TRUE, (PUDF_FILE_INFO*)&NewFileInfo);
                if (Status != STATUS_SUCCESS && NT_SUCCESS(Status)) Status = STATUS_IO_DEVICE_ERROR;
            } _SEH2_EXCEPT(FsRtlIsNtstatusExpected(_SEH2_GetExceptionCode()) ?
                          EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
                Status = _SEH2_GetExceptionCode();
            } _SEH2_END;
            if (Status == STATUS_CANT_WAIT) Status = STATUS_DEVICE_BUSY;
        } _SEH2_FINALLY {
            if (_SEH2_AbnormalTermination()) Status = STATUS_FILE_CORRUPT_ERROR;
            if (Status != STATUS_SUCCESS) {
                /* No successful open was returned. Keep its exact failed
                 * constructor reachable even when this wrapper's caller has
                 * no partial output, or an exception skips the return. */
                UDFRetainFailedStreamOpen(Vcb, (PUDF_FILE_INFO*)&NewFileInfo);
            }
        } _SEH2_END;
        if (Status == STATUS_FILE_DELETED) {
            AdPrint(("    File deleted\n"));
            return STATUS_ACCESS_DENIED;
        }
        if (Status == STATUS_SHARING_PAUSED) {
            AdPrint(("    Dloc is being initialized\n"));
            BrutePoint();
            return STATUS_SHARING_VIOLATION;
        }
        if (!NT_SUCCESS(Status)) {
            return Status;
        }
    }

    /* From here a successful temporary open belongs to the caller's output,
     * even if an expected/abnormal FCB/prefix/CCB setup step fails. */
    *OutFileInfo = NewFileInfo;
    _SEH2_TRY {
        if (!(PtrNewFcb = NewFileInfo->Fcb)) {
            Status = UDFFirstOpenFile(IrpContext, Vcb,
                            (PFCB*)&PtrNewFcb, RelatedFileInfo, NewFileInfo);
            if (Status != STATUS_SUCCESS) _SEH2_LEAVE;
        } else if (!(PtrNewFcb->FcbState & UDF_FCB_VALID) || PtrNewFcb->TeardownInProgress) {
            Status = STATUS_ACCESS_DENIED;
            _SEH2_LEAVE;
        }
        *OutFcb = PtrNewFcb;
        Status = UDFMoveCreateFcb(IrpContext, Vcb, PtrNewFcb, CurrentFcb, PreviousFcb);
        if (Status != STATUS_SUCCESS) _SEH2_LEAVE;
        if (PtrNewFcb->FileInfo == NewFileInfo && NewFileInfo->ParentFile == RelatedFileInfo)
            PtrNewFcb->ParentFcb = RelatedFileInfo->Fcb;
        /* Logical self/parent traversal is not a new namespace child link. */
        if (RemainingNameLength && *PreviousFcb && RelatedFileInfo &&
            NewFileInfo->ParentFile == RelatedFileInfo &&
            (NewFileInfo->Index >= 2 || UDFIsAStreamDir(NewFileInfo))) {
            PLCB Prefix;
            Status = UDFAcquirePrefix(IrpContext, RelatedFileInfo->Fcb, PtrNewFcb,
                        NewFileInfo, NULL, NULL, NULL, NewFileInfo->Index, &Prefix);
            if (Status != STATUS_SUCCESS) _SEH2_LEAVE;
            if (*PreviousFcb && *PreviousFcb != *CurrentFcb) {
                PFCB Parent = *PreviousFcb;
                *PreviousFcb = *CurrentFcb;
                UDFReleaseFcb(IrpContext, Parent);
            }
        }
        /* Access/share/overwrite/cache/CCB work needs child Main already
         * held. No fallible lock/prefix work follows a successful CCB. */
        if (PerformUserOpen) {
            Status = UDFOpenExistingFcb(IrpContext, IrpSp, Vcb,
                IrpSp->FileObject, NewFileInfo, (PFCB*)&PtrNewFcb,
                IgnoreCase, RelatedFileObject, RelatedFcb, CreateDisposition);
            *OutFcb = PtrNewFcb;
        }
    } _SEH2_EXCEPT(FsRtlIsNtstatusExpected(_SEH2_GetExceptionCode()) ?
                  EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        if (NT_SUCCESS(Status)) Status = _SEH2_GetExceptionCode();
    } _SEH2_END;
    if (Status == STATUS_CANT_WAIT) Status = STATUS_DEVICE_BUSY;
    if (Status != STATUS_SUCCESS && NT_SUCCESS(Status)) Status = STATUS_IO_DEVICE_ERROR;
    return Status;

} // end UDFOpenObjectFromDirContext()


/*************************************************************************
*
* Function: UDFOpenObjectByFileId()
*
* Description:
*   Open a file or directory by its 64-bit FileId.
*
*   The FileId is extracted from FileObject->FileName.Buffer and validated.
*   We look up or create an FCB using the FileId, verify file/directory
*   compatibility with CreateOptions, acquire the FCB, check access,
*   and complete the open.
*
* Expected Interrupt Level (for execution) :
*
*  IRQL_PASSIVE_LEVEL
*
* Return Value: STATUS_SUCCESS/Error
*
*************************************************************************/
NTSTATUS
UDFOpenObjectByFileId(
    _In_ PIRP_CONTEXT IrpContext,
    _In_ PIO_STACK_LOCATION IrpSp,
    _In_ PVCB Vcb,
    _Inout_ PFCB *CurrentFcb
    )
{
    _SEH2_VOLATILE NTSTATUS Status = STATUS_ACCESS_DENIED;

    _SEH2_VOLATILE BOOLEAN UnlockFcbTable = FALSE;
    _SEH2_VOLATILE BOOLEAN FcbExisted = FALSE;
    _SEH2_VOLATILE BOOLEAN NewFcbVcbLocked = FALSE;
    _SEH2_VOLATILE PUDF_FILE_INFO OpenFileInfoReference = NULL;
    _SEH2_VOLATILE PUDF_FILE_INFO UnattachedFileInfo = NULL;

    NODE_TYPE_CODE NodeTypeCode;
    TYPE_OF_OPEN TypeOfOpen;

    FILE_ID FileId;

    _SEH2_VOLATILE PFCB NextFcb = NULL;

    PAGED_CODE();

    ASSERT_VCB(Vcb);

    //
    // Extract the FileId from the FileObject.
    //
    // Our FileId format (UDFGetNTFileId):
    //   LowPart  = physicalLBA - PartitionRoot
    //   HighPart = (uint32)Vcb pointer
    //
    // For directories, UDFFirstOpenFile also sets FID_DIR_MASK (bit 31)
    // in HighPart, but on 32-bit systems this is a no-op since kernel
    // addresses already have bit 31 set.
    //
    // Directory enumeration reports the ICB address of each entry, setting
    // FID_DIR_MASK for directories.  Older callers may still supply an ID
    // without that bit, so the FCB lookup below accepts either form.
    //

    if (!IrpSp->FileObject->FileName.Buffer ||
        IrpSp->FileObject->FileName.Length != sizeof(FILE_ID))
        return STATUS_INVALID_PARAMETER;
    RtlCopyMemory(&FileId, IrpSp->FileObject->FileName.Buffer, sizeof(FILE_ID));
    if (((ULONG)FileId.HighPart ^ (ULONG)(UINT_PTR)Vcb) & ~((ULONG)FID_DIR_MASK))
        return STATUS_INVALID_PARAMETER;

    //
    // Use a try-finally to facilitate cleanup.
    //

    _SEH2_TRY {

        //
        // Acquire the FcbTable and look for an existing FCB.
        //
        // The stored FileId for directories may have FID_DIR_MASK set
        // while the incoming FileId from enumeration does not. Try both
        // variants to find the existing FCB.
        //

        UDFLockFcbTable(IrpContext, Vcb);
        UnlockFcbTable = TRUE;

        NextFcb = UDFLookupFcbTable(IrpContext, Vcb, FileId);

        if (!NextFcb) {

            //
            // Try with FID_DIR_MASK toggled — handles the case where the
            // stored FCB FileId has the directory bit set but the incoming
            // FileId from enumeration does not (or vice versa).
            //

            FILE_ID AltFileId = FileId;
            AltFileId.HighPart ^= FID_DIR_MASK;
            NextFcb = UDFLookupFcbTable(IrpContext, Vcb, AltFileId);
        }

        if (NextFcb) {

            //
            // Found an existing FCB. Determine type from the FCB itself.
            //

            FcbExisted = TRUE;
            if (NextFcb->TeardownInProgress ||
                !(NextFcb->FcbState & UDF_FCB_VALID) ||
                !NextFcb->FileInfo || !NextFcb->FileInfo->Dloc ||
                FlagOn(NextFcb->FcbState, UDF_FCB_DELETED | UDF_FCB_NOT_FOUND_ON_MEDIA))
                try_return(Status = STATUS_SHARING_VIOLATION);
            if (UDFIsReservedMetadataFile(NextFcb->FileInfo))
                try_return(Status = STATUS_ACCESS_DENIED);

        } else {

            //
            // FCB not in table. We need to open the file from disk to
            // determine its type and create the FCB.
            //

            if (!FlagOn(IrpContext->Flags, IRP_CONTEXT_FLAG_WAIT)) {

                UDFRaiseStatus(IrpContext, STATUS_CANT_WAIT);
            }

            //
            // Convert FileId back to physical LBA.
            // UDFGetNTFileId stores: LowPart = physLBA - PartitionRoot
            //
            uint64 WidePhysical = (uint64)(uint32)FileId.LowPart + UDFPartStart(Vcb, -2);
            lb_addr FileLoc;
            if (WidePhysical >= LBA_OUT_OF_EXTENT || WidePhysical > Vcb->SessionEndLba)
                try_return(Status = STATUS_INVALID_PARAMETER);

            /* A VAT inverse may read the physical tag. No FCB-table mutex
             * may cover that I/O, allocation or constructor operation. */
            UDFUnlockFcbTable(IrpContext, Vcb);
            UnlockFcbTable = FALSE;
            Status = UDFResolvePhysicalIcbAddress(IrpContext, Vcb, (uint32)WidePhysical, &FileLoc);
            if (!NT_SUCCESS(Status))
                try_return(Status);

            PUDF_FILE_INFO FileInfo = (PUDF_FILE_INFO)
                MyAllocatePoolTag__(UDF_FILE_INFO_MT, sizeof(UDF_FILE_INFO), MEM_FINF_TAG);
            UnattachedFileInfo = FileInfo;

            if (!FileInfo) {
                try_return(Status = STATUS_INSUFFICIENT_RESOURCES);
            }

            /* Even an exception before constructor admission leaves a
             * recognizable unbound shell, not uninitialized owner fields. */
            RtlZeroMemory(FileInfo, sizeof(*FileInfo));
            FileInfo->NextLinkedFile = FileInfo->PrevLinkedFile = FileInfo;
            Status = UDFOpenRootFile__(IrpContext, Vcb, &FileLoc, FileInfo);

            if (!NT_SUCCESS(Status))
                try_return(Status);
            if (UDFIsReservedMetadataFile(FileInfo))
                try_return(Status = STATUS_ACCESS_DENIED);

            //
            // Now we know the file type from the on-disk data.
            // Determine NodeTypeCode and build the FileId that matches
            // what UDFFirstOpenFile would store (with FID_DIR_MASK for dirs).
            //
            BOOLEAN IsDirectory = UDFIsADirectory(FileInfo);
            FILE_ID StoredFileId = FileId;

            if (IsDirectory) {
                NodeTypeCode = UDF_NODE_TYPE_INDEX;
                SetFlag(StoredFileId.HighPart, FID_DIR_MASK);
            } else {
                NodeTypeCode = UDF_NODE_TYPE_DATA;
                // Don't modify HighPart for files — bit 31 may be part of the
                // Vcb pointer (kernel address), not a flag we control.
                // UDFFirstOpenFile leaves HighPart unmodified for files.
            }

            //
            // Create the FCB with the correct type and FileId.
            //
            UDFLockFcbTable(IrpContext, Vcb);
            UnlockFcbTable = TRUE;
            NextFcb = UDFCreateFcb(IrpContext, StoredFileId, NodeTypeCode, (PBOOLEAN)&FcbExisted);

            if (!NextFcb)
                try_return(Status = STATUS_INSUFFICIENT_RESOURCES);

            if (FcbExisted) {
                if (NextFcb->TeardownInProgress ||
                    !(NextFcb->FcbState & UDF_FCB_VALID) || !NextFcb->FileInfo ||
                    NextFcb->FileInfo->Dloc != FileInfo->Dloc ||
                    FlagOn(NextFcb->FcbState, UDF_FCB_DELETED | UDF_FCB_NOT_FOUND_ON_MEDIA))
                    try_return(Status = STATUS_SHARING_VIOLATION);
                /* The constructor hold prevents the indexed teardown gate
                 * from claiming this Dloc. Dispose of this anonymous alias
                 * in FINALLY after releasing the table mutex. */
            } else {

                //
                // Link FCB to FileInfo and initialize it.
                //

                /* A table miss does not release another admission's
                 * canonical Dloc identity. Never overwrite that owner. */
                if (!FileInfo->Dloc || FileInfo->Dloc->CommonFcb)
                    try_return(Status = STATUS_SHARING_VIOLATION);
                NextFcb->FileInfo = FileInfo;
                FileInfo->Fcb = NextFcb;

                if (Vcb->RootIndexFcb) {
                    NextFcb->ParentFcb = Vcb->RootIndexFcb;
                }

                if (FileInfo->Dloc) {
                    FileInfo->Dloc->CommonFcb = NextFcb;
                }

                //
                // Initialize FCB fields from the file info.
                // VcbMutex is needed by UDFInitializeFCB (ASSERT_LOCKED_VCB).
                //

                UDFLockVcb(IrpContext, Vcb);
                NewFcbVcbLocked = TRUE;

                Status = UDFInitializeFCB(NextFcb, Vcb,
                             IsDirectory ? UDF_FCB_DIRECTORY : 0);

                if (!NT_SUCCESS(Status)) {
                    UDFUnlockVcb(IrpContext, Vcb);
                    NewFcbVcbLocked = FALSE;
                    NextFcb->FileInfo = NULL;
                    FileInfo->Fcb = NULL;
                    if (FileInfo->Dloc && FileInfo->Dloc->CommonFcb == NextFcb)
                        FileInfo->Dloc->CommonFcb = NULL;
                    try_return(Status);
                }

                /* Match named-open initialization: inline data must use the
                 * extent walker, including Cache Manager paging requests. */
                if (!IsDirectory &&
                    ((((PFILE_ENTRY)FileInfo->Dloc->FileEntry)->icbTag.flags &
                      ICB_FLAG_ALLOC_MASK) == ICB_FLAG_AD_IN_ICB)) {
                    NextFcb->FcbState |= UDF_FCB_EMBEDDED_DATA;
                }

                // Set file times
                UDFGetFileXTime(FileInfo,
                    &NextFcb->CreationTime.QuadPart,
                    &NextFcb->LastAccessTime.QuadPart,
                    &NextFcb->ChangeTime.QuadPart,
                    &NextFcb->LastWriteTime.QuadPart);

                // Set file sizes
                NextFcb->Header.AllocationSize.QuadPart =
                    UDFSysGetAllocSize(Vcb, FileInfo->Dloc->DataLoc.Length);
                NextFcb->Header.FileSize.QuadPart =
                NextFcb->Header.ValidDataLength.QuadPart = FileInfo->Dloc->DataLoc.Length;

                // Set file attributes
                if (IsDirectory) {
                    NextFcb->FileAttributes = FILE_ATTRIBUTE_DIRECTORY;
                }

                if (!UDFIsAStreamDir(FileInfo)) {
                    PDIR_INDEX_HDR hDirIndex = UDFGetDirIndexByFileInfo(FileInfo);
                    DIR_INDEX_ITEM AttributesIndex;
                    PDIR_INDEX_ITEM DirNdx;
                    RtlZeroMemory(&AttributesIndex, sizeof(AttributesIndex));
                    DirNdx = hDirIndex ? UDFDirIndex(hDirIndex, FileInfo->Index) :
                                        &AttributesIndex;
                    NextFcb->FileAttributes = UDFAttributesToNT(
                        DirNdx, FileInfo->Dloc->FileEntry);
                    if (NextFcb->FileAttributes & FILE_ATTRIBUTE_READONLY) {
                        NextFcb->FcbState |= UDF_FCB_READ_ONLY;
                    }
                }

                /* The fresh open owns this FI reference until a CCB accepts
                 * it. A cached valid FCB is not an owner of a failed open. */
                OpenFileInfoReference = FileInfo;
                UnattachedFileInfo = NULL;

                // Table admission must succeed before publishing validity.
                Status = UDFInsertFcbIntoTable(IrpContext, NextFcb);
                if (Status != STATUS_SUCCESS) try_return(Status);
                NextFcb->NtReqFCBFlags |= UDF_NTREQ_FCB_VALID;
                NextFcb->FcbState |= UDF_FCB_VALID;

                UDFUnlockVcb(IrpContext, Vcb);
                NewFcbVcbLocked = FALSE;
            }
        }

        //
        // Determine TypeOfOpen from the actual FCB type.
        //

        if (NextFcb->NodeIdentifier.NodeTypeCode == UDF_NODE_TYPE_INDEX) {
            TypeOfOpen = UserDirectoryOpen;
        } else {
            TypeOfOpen = UserFileOpen;
        }

        //
        // Check that the type of the file is compatible with the
        // desired type of file to open.
        //

        if (FlagOn(NextFcb->FileAttributes, FILE_ATTRIBUTE_DIRECTORY)) {

            if (FlagOn(IrpSp->Parameters.Create.Options, FILE_NON_DIRECTORY_FILE)) {

                try_return(Status = STATUS_FILE_IS_A_DIRECTORY);
            }

        } else if (FlagOn(IrpSp->Parameters.Create.Options, FILE_DIRECTORY_FILE)) {

            try_return(Status = STATUS_NOT_A_DIRECTORY);
        }

        //
        // We now know the FCB and currently hold the FcbTable lock.
        // Try to acquire this FCB without waiting. Otherwise we
        // need to reference it, drop the lock, acquire the FCB,
        // relock and then dereference the FCB.
        //

        if (!UDFAcquireFcbExclusive(IrpContext, NextFcb, TRUE)) {
            /* No wait/retry token may outlive a consumed FI owner. */
            if (!BooleanFlagOn(IrpContext->Flags, IRP_CONTEXT_FLAG_WAIT))
                try_return(Status = STATUS_DEVICE_BUSY);
            UDFLockVcb(IrpContext, Vcb);
            if (NextFcb->FcbReference == MAXULONG || NextFcb->TeardownInProgress) {
                UDFUnlockVcb(IrpContext, Vcb);
                try_return(Status = STATUS_SHARING_VIOLATION);
            }
            NextFcb->FcbReference++;
            UDFUnlockVcb(IrpContext, Vcb);
            _SEH2_TRY {
                UnlockFcbTable = FALSE;
                UDFUnlockFcbTable(IrpContext, Vcb);
                UDFAcquireFcbExclusive(IrpContext, NextFcb, FALSE);
                *CurrentFcb = NextFcb; /* acquired, even if finalization raises */
            } _SEH2_FINALLY {
                BOOLEAN FailedAcquire = _SEH2_AbnormalTermination() || *CurrentFcb != NextFcb;
                UDFLockVcb(IrpContext, Vcb);
                _SEH2_TRY {
                    ASSERT(NextFcb->FcbReference);
                    NextFcb->FcbReference--;
                    if (FailedAcquire && !NextFcb->FcbReference && !NextFcb->TeardownRetryPending) {
                        NextFcb->TeardownRetryPending = TRUE;
                        InterlockedIncrement(&Vcb->ClosedFcbRetryCount);
                    }
                } _SEH2_FINALLY {
                    UDFUnlockVcb(IrpContext, Vcb);
                } _SEH2_END;
            } _SEH2_END;
        } else {
            *CurrentFcb = NextFcb; /* publish before dropping table ownership */
            UnlockFcbTable = FALSE;
            UDFUnlockFcbTable(IrpContext, Vcb);
        }

        //
        // Check the requested access on this FCB.
        //

        if (!UDFIllegalFcbAccess(Vcb,
                                 IrpSp->Parameters.Create.SecurityContext->DesiredAccess)) {

            //
            // UDFOpenRootFile__ acquired the reference for a newly built
            // FCB. An FCB found in the table still needs one FileInfo
            // reference for this handle: cleanup closes it once per open.
            //

            if (FcbExisted) {
                if (!NextFcb->FileInfo) {
                    try_return(Status = STATUS_FILE_CORRUPT_ERROR);
                }
                Status = UDFReferenceFileChecked(Vcb, NextFcb->FileInfo);
                if (!NT_SUCCESS(Status)) try_return(Status);
                OpenFileInfoReference = NextFcb->FileInfo;
            }

            Status = UDFCompleteFcbOpen(IrpContext,
                                         IrpSp,
                                         Vcb,
                                         CurrentFcb,
                                         OpenFileInfoReference,
                                         TypeOfOpen,
                                         CCB_FLAG_OPEN_BY_ID,
                                         FILE_OPEN,
                                         FALSE,        // VcbLocked
                                         FILE_OPENED);
            if (NT_SUCCESS(Status)) {
                // The successful CCB transfers this reference to cleanup.
                OpenFileInfoReference = NULL;
            }
        }

try_exit:   NOTHING;

    } _SEH2_FINALLY {
        if (NewFcbVcbLocked)
            UDFUnlockVcb(IrpContext, Vcb);
        BOOLEAN DeleteNewFcb = NextFcb && !FcbExisted &&
            !FlagOn(NextFcb->FcbState, UDF_FCB_VALID);
        if (DeleteNewFcb) {
            PUDF_FILE_INFO Partial = NextFcb->FileInfo;
            if (Partial) {
                Partial->Fcb = NULL;
                if (Partial->Dloc && Partial->Dloc->CommonFcb == NextFcb)
                    Partial->Dloc->CommonFcb = NULL;
                NextFcb->FileInfo = NULL;
            }
            if (FlagOn(NextFcb->FcbState, FCB_STATE_IN_FCB_TABLE)) {
                struct { FILE_ID FileId; PFCB Fcb; } Key;
                Key.FileId = NextFcb->FileId;
                UDFLockVcb(IrpContext, Vcb);
                RtlDeleteElementGenericTable(&Vcb->FcbTable, &Key);
                ClearFlag(NextFcb->FcbState, FCB_STATE_IN_FCB_TABLE);
                UDFUnlockVcb(IrpContext, Vcb);
            }
        }
        if (UnlockFcbTable) {
            UDFUnlockFcbTable(IrpContext, Vcb);
            UnlockFcbTable = FALSE;
        }

        if (_SEH2_AbnormalTermination() || !NT_SUCCESS(Status)) {
            UDFUnwindCompletedOpen(IrpContext, IrpSp->FileObject,
                                    (PUDF_FILE_INFO*)&OpenFileInfoReference);
        }
        if (OpenFileInfoReference) {
            UDFReleaseCreateReference(IrpContext, Vcb, (PUDF_FILE_INFO*)&OpenFileInfoReference, TRUE);
        }

        /* Successful anonymous aliases own one ordinary token; partial
         * constructors own their separate generation. Neither is a reason
         * to re-enter close-time I/O from a failed CREATE. */
        if (UnattachedFileInfo)
            UDFReleaseCreateReference(IrpContext, Vcb,
                (PUDF_FILE_INFO*)&UnattachedFileInfo, TRUE);

        //
        // Destroy the new FCB if it was not fully initialized.
        // Check UDF_FCB_VALID — if it's not set, the FCB was never inserted
        // into the table and never fully initialized.
        //

        if (DeleteNewFcb) {

            UDFDeleteFcb(IrpContext, NextFcb);
        }

    } _SEH2_END;

    return Status;
} // end UDFOpenObjectByFileId()


/*************************************************************************
*
* Function: UDFNormalizeStreamSuffix()
*
* Description:
*   Validate and strip the stream type suffix that UDFDissectName leaves in
*   RemainingName after extracting a stream component into StreamName.
*
*   On entry StreamName holds the stream name (e.g. "stream1", possibly empty
*   for the default data stream) and RemainingName holds the trailing suffix
*   (e.g. ":$DATA") or is empty when no suffix was present. UDF only supports
*   the default $DATA stream type, so the suffix — if any — must be exactly
*   ":$DATA"; on a match RemainingName is consumed (Length set to 0).
*
* Raises: STATUS_OBJECT_NAME_INVALID for an unknown suffix, or for an empty
*   stream name with no suffix (e.g. "file:").
*
*************************************************************************/
static
VOID
UDFNormalizeStreamSuffix(
    IN PIRP_CONTEXT IrpContext,
    IN PUNICODE_STRING StreamName,
    IN OUT PUNICODE_STRING RemainingName
    )
{
    static const UNICODE_STRING DataSuffix = RTL_CONSTANT_STRING(L":$DATA");

    if (RemainingName->Length == 0) {
        // No suffix follows the stream name. An empty stream name with no
        // suffix ("file:") is not a valid object.
        if (StreamName->Length == 0) {
            UDFRaiseStatus(IrpContext, STATUS_OBJECT_NAME_INVALID);
        }
        return;
    }

    // A suffix is present — it must be exactly ":$DATA" (case-insensitive).
    // Anything else is an unsupported stream type.
    if (RtlEqualUnicodeString(RemainingName, &DataSuffix, TRUE)) {
        RemainingName->Length = 0;
        return;
    }

    UDFRaiseStatus(IrpContext, STATUS_OBJECT_NAME_INVALID);
}

/*************************************************************************
*
* Function: UDFCommonCreate()
*
* Description:
*   The actual work is performed here. This routine may be invoked in one'
*   of the two possible contexts:
*   (a) in the context of a system worker thread
*   (b) in the context of the original caller
*
* Expected Interrupt Level (for execution) :
*
*  IRQL_PASSIVE_LEVEL
*
* Return Value: STATUS_SUCCESS/Error
*
*************************************************************************/
NTSTATUS
UDFCommonCreate(
    PIRP_CONTEXT IrpContext,
    PIRP Irp
    )
{
    _SEH2_VOLATILE NTSTATUS Status = STATUS_SUCCESS;
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    PFILE_OBJECT FileObject = NULL;
    PFILE_OBJECT RelatedFileObject = NULL;
    LONGLONG AllocationSize;     // if we create a new file
    ULONG Options;
    ULONG CreateDisposition;
    USHORT FileAttributes;
    USHORT ShareAccess;
    ACCESS_MASK DesiredAccess;
    PACCESS_STATE AccessState;

    PVCB Vcb = NULL;
    // Hold two locks during tree traversal (child + parent)
    // CurrentFcb = current node lock, PreviousFcb = parent node lock (released after operations)
    _SEH2_VOLATILE PFCB CurrentFcb = NULL;
    _SEH2_VOLATILE PFCB PreviousFcb = NULL;

    BOOLEAN DeleteOnClose;
    BOOLEAN OpenByFileId;
    BOOLEAN DirectoryFile;
    BOOLEAN NonDirectoryFile;

    // Is this open for a target directory (used in rename operations)?
    BOOLEAN OpenTargetDirectory;
    // Should we ignore case when attempting to locate the object?
    BOOLEAN IgnoreCase;

    PCCB RelatedCcb = NULL;
    PFCB NextFcb = NULL;
    _SEH2_VOLATILE PFCB PtrNewFcb = NULL;
    PLCB OpenLcb = NULL;            // LCB from prefix match (for direct open)

    PUNICODE_STRING FileName;
    PUNICODE_STRING RelatedFileName = NULL;

    //BOOLEAN VolumeOpen = FALSE;

    TYPE_OF_OPEN RelatedTypeOfOpen = UnopenedFileObject;

    UNICODE_STRING FinalName;           // 'cdf' - current path component
    UNICODE_STRING RemainingName;       // 'fff\rrrr.tre:s' - remaining path to parse
    UNICODE_STRING StreamName;          // bare stream name when creating a new stream

    PUDF_FILE_INFO RelatedFileInfo;
    PUDF_FILE_INFO OldRelatedFileInfo = NULL;
    _SEH2_VOLATILE PUDF_FILE_INFO NewFileInfo = NULL;
    _SEH2_VOLATILE PUDF_FILE_INFO LastGoodFileInfo = NULL;
    BOOLEAN VolumeOpen = FALSE;

    BOOLEAN StreamOpen = FALSE;
    ULONG SNameIndex = 0;

    BOOLEAN NewFileCreated = FALSE;
    _SEH2_VOLATILE BOOLEAN FreshFileCreated = FALSE;
    _SEH2_VOLATILE PUDF_FILE_INFO FreshFileInfo = NULL;
    DIR_ENUM_CONTEXT DirContext;

    PAGED_CODE();

    ASSERT(IrpContext);
    ASSERT(Irp);

    // If we were called with our file system device object instead of a
    // volume device object, just complete this request with STATUS_SUCCESS.

    if (IrpContext->Vcb == NULL) {

        UDFCompleteRequest(IrpContext, Irp, STATUS_SUCCESS);
        return STATUS_SUCCESS;
    }

    Options             = IrpSp->Parameters.Create.Options;
    OpenTargetDirectory = BooleanFlagOn(IrpSp->Flags, SL_OPEN_TARGET_DIRECTORY);
    DirectoryFile       = BooleanFlagOn(Options, FILE_DIRECTORY_FILE);
    NonDirectoryFile    = BooleanFlagOn(Options, FILE_NON_DIRECTORY_FILE);
    OpenByFileId = BooleanFlagOn(IrpSp->Parameters.Create.Options, FILE_OPEN_BY_FILE_ID);
    IgnoreCase = !BooleanFlagOn( IrpSp->Flags, SL_CASE_SENSITIVE );
    CreateDisposition = (IrpSp->Parameters.Create.Options >> 24) & 0x000000ff;

    DeleteOnClose = BooleanFlagOn(IrpSp->Parameters.Create.Options, FILE_DELETE_ON_CLOSE);
    FileAttributes = IrpSp->Parameters.Create.FileAttributes & ~FILE_ATTRIBUTE_NORMAL;
    AllocationSize = Irp->Overlay.AllocationSize.QuadPart;
    AccessState = IrpSp->Parameters.Create.SecurityContext->AccessState;
    DesiredAccess = IrpSp->Parameters.Create.SecurityContext->DesiredAccess;
    ShareAccess = IrpSp->Parameters.Create.ShareAccess;

    Vcb = IrpContext->Vcb;

    ASSERT_VCB(Vcb);

    Irp->IoStatus.Information = 0;

    // Check if the volume is read - only or write - protected and if the operation
    // requires write access

    if (FlagOn(Vcb->VcbState, VCB_STATE_MEDIA_WRITE_PROTECT | VCB_STATE_VOLUME_READ_ONLY)) {

        if (CreateDisposition == FILE_OVERWRITE_IF ||
            CreateDisposition == FILE_SUPERSEDE ||
            CreateDisposition == FILE_CREATE ||
            CreateDisposition == FILE_OVERWRITE ||
            OpenTargetDirectory ||
            FlagOn(Options, FILE_DELETE_ON_CLOSE)) {

            if (FlagOn(Vcb->VcbState, VCB_STATE_MEDIA_WRITE_PROTECT)) {

                IoSetHardErrorOrVerifyDevice(IrpContext->Irp, Vcb->Vpb->RealDevice);
                IrpContext->ExceptionStatus = STATUS_MEDIA_WRITE_PROTECTED;
                ExRaiseStatus(STATUS_MEDIA_WRITE_PROTECTED);
            }

            Status = STATUS_ACCESS_DENIED; 
            if (FlagOn(Vcb->VcbState, VCB_STATE_MOUNTED_DIRTY)) {

                Status = STATUS_VOLUME_DIRTY;
            }

            UDFCompleteRequest(IrpContext, Irp, Status);
            return Status;
        }
    }

    // Check for invalid combination of DELETE_ON_CLOSE with other flags

    if (FlagOn(Options, FILE_DELETE_ON_CLOSE) &&
        (OpenTargetDirectory || FlagOn(Options, FILE_OPEN_BY_FILE_ID))) {

        UDFCompleteRequest(IrpContext, Irp, STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }

    // UDFS does not support Extended Attributes

    if (IrpSp->Parameters.Create.EaLength || Irp->AssociatedIrp.SystemBuffer) {

        UDFCompleteRequest(IrpContext, Irp, STATUS_EAS_NOT_SUPPORTED);
        return STATUS_EAS_NOT_SUPPORTED;
    }

    //  UDFS does not support paging files

    if (FlagOn(IrpSp->Flags, SL_OPEN_PAGING_FILE)) {

        UDFCompleteRequest(IrpContext, Irp, STATUS_NOT_SUPPORTED);
        return STATUS_NOT_SUPPORTED;
    }

#if (NTDDI_VERSION >= NTDDI_WIN7)

    // UDFS does not support FILE_OPEN_REQUIRING_OPLOCK

    if (FlagOn(Options, FILE_OPEN_REQUIRING_OPLOCK)) {

        UDFCompleteRequest(IrpContext, Irp, STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }
#endif

    // Reference our input parameters to make things easier

    FileObject = IrpSp->FileObject;
    RelatedFileObject = NULL;

    FileName = &FileObject->FileName;

    // Set up the file object's Vpb pointer in case anything happens.
    // This will allow us to get a reasonable pop-up.

    if ((FileObject->RelatedFileObject != NULL) && !OpenByFileId) {

        RelatedFileObject = FileObject->RelatedFileObject;
        FileObject->Vpb = RelatedFileObject->Vpb;

        RelatedTypeOfOpen = UDFDecodeFileObject(RelatedFileObject, &NextFcb, &RelatedCcb);

        ASSERT_CCB(RelatedCcb);
        ASSERT_FCB(NextFcb);

        // Fail the request if this is not a user file object.

        if (RelatedTypeOfOpen < UserVolumeOpen) {
            UDFCompleteRequest( IrpContext, Irp, STATUS_INVALID_PARAMETER );
            return STATUS_INVALID_PARAMETER;
        }

        // Remember the name in the related file object.

        RelatedFileName = &RelatedFileObject->FileName;
    }

   // If we haven't initialized the names then make sure the strings are valid.
   // If this an OpenByFileId then verify the file id buffer.
   //
   // After this routine returns we know that the full name is in the
   // FileName buffer and the buffer will hold the upcased portion
   // of the name yet to parse immediately after the full name in the
   // buffer.  Any trailing backslash has been removed and the flag
   // in the IrpContext will indicate whether we removed the
   // backslash.

    Status = UDFNormalizeFileNames(IrpContext,
                               Vcb,
                               OpenByFileId,
                               RelatedTypeOfOpen,
                               RelatedCcb,
                               RelatedFileName,
                               FileName,
                               &RemainingName);

    // Return the error code if not successful.

    if (!NT_SUCCESS(Status)) {

        UDFCompleteRequest(IrpContext, Irp, Status);
        return Status;
    }

    // We want to acquire the Vcb.  Exclusively for a volume open, shared otherwise.
    // The file name is empty for a volume open.

    if ((FileName->Length == 0) &&
        (RelatedTypeOfOpen <= UserVolumeOpen) &&
        !OpenByFileId) {

        VolumeOpen = TRUE;
        UDFAcquireVcbExclusive(IrpContext, Vcb, FALSE);

    } else if (CreateDisposition == FILE_SUPERSEDE ||
               CreateDisposition == FILE_OVERWRITE ||
               CreateDisposition == FILE_OVERWRITE_IF) {

        // Overwriting/superseding an existing file may drop its stream directory,
        // which removes prefix LCBs and adjusts VCB-global reference counts that
        // are not individually locked — take the Vcb exclusively. These
        // dispositions are rare, so the reduced concurrency is acceptable.
        UDFAcquireVcbExclusive(IrpContext, Vcb, FALSE);

    } else {

        UDFAcquireVcbShared(IrpContext, Vcb, FALSE);
    }

    // Use a try-finally to facilitate cleanup.

    _SEH2_TRY {

        // A forced dismount can leave an old Vcb referenced by an in-flight
        // open.  Complete that open with the invalid-volume status while the
        // Vcb resource is held; raising here can escape the FSD dispatch on
        // Windows x64 and turn the stale open into a system bugcheck.
        if (Vcb->VcbCondition == VcbInvalid ||
            Vcb->VcbCondition == VcbDismountInProgress) {
            try_return(Status = FlagOn(Vcb->VcbState, VCB_STATE_DISMOUNT_IN_PROGRESS) ?
                       STATUS_VOLUME_DISMOUNTED : STATUS_FILE_INVALID);
        }

        // Verify that the Vcb is not in an unusable condition.  This routine
        // will raise if not usable.

        UDFVerifyVcb(IrpContext, Vcb);

        // Check if dismount is in progress
        if (FlagOn(Vcb->VcbState, VCB_STATE_DISMOUNT_IN_PROGRESS)) {
            try_return(Status = STATUS_ACCESS_DENIED);
        }

        // If the Vcb is locked then we cannot open another file

        if (FlagOn(Vcb->VcbState, VCB_STATE_LOCKED)) {

            try_return(Status = STATUS_ACCESS_DENIED);
        }

        ASSERT(Vcb->VcbCondition == VcbMounted);

        // If we are opening this volume Dasd then process this immediately
        // and exit.

        // ****************
        // If a Volume open is requested, satisfy it now
        // ****************
        if (VolumeOpen) {

            // The only create disposition we allow is OPEN.

            if ((CreateDisposition != FILE_OPEN) &&
                (CreateDisposition != FILE_OPEN_IF)) {

                try_return(Status = STATUS_ACCESS_DENIED);
            }

            // If a volume open is requested, perform checks to ensure that
            // invalid options have not also been specified ...

            if (OpenTargetDirectory) {

                try_return(Status = STATUS_NOT_A_DIRECTORY);
            }

            // If they wanted to open a directory, surprise.

            if (DirectoryFile) {

                try_return(Status = STATUS_NOT_A_DIRECTORY);
            }

            // Cannot delete a volume

            if (DeleteOnClose) {

                try_return(Status = STATUS_CANNOT_DELETE);
            }

            if (FlagOn(Vcb->VcbState, VCB_STATE_VOLUME_READ_ONLY)) {

                if (FlagOn(DesiredAccess, FILE_WRITE_DATA | FILE_APPEND_DATA | DELETE | WRITE_DAC)) {

                    try_return(Status = STATUS_ACCESS_DENIED);
                }
            }

            // Cannot supersede, overwrite, or create a volume.
            if (CreateDisposition == FILE_SUPERSEDE ||
                CreateDisposition == FILE_OVERWRITE ||
                CreateDisposition == FILE_OVERWRITE_IF ||
                CreateDisposition == FILE_CREATE) {

                try_return(Status = STATUS_ACCESS_DENIED);
            }

            // Publish only a resource acquired by this request. A no-wait
            // failure must not make finalization release another owner.
            UDFAcquireFcbExclusive(IrpContext, Vcb->VolumeDasdFcb, FALSE);
            CurrentFcb = Vcb->VolumeDasdFcb;

            Status = UDFCompleteFcbOpen(IrpContext,
                                        IrpSp,
                                        Vcb,
                                        (PFCB*)&CurrentFcb,
                                        NULL,
                                        UserVolumeOpen,
                                        0,
                                        CreateDisposition,
                                        FALSE,         // VcbLocked
                                        FILE_OPENED);  // DesiredInformation

            try_return(Status);
        }

        if (UDFIllegalFcbAccess(Vcb, DesiredAccess)) {
            AdPrint(("    Illegal share access\n"));
            try_return(Status = STATUS_ACCESS_DENIED);
        }

        ASSERT(Vcb->VcbCondition == VcbMounted);

        //
        // If we are opening this file by FileId then process this immediately
        // and exit.
        //

        if (OpenByFileId) {

            //
            // The only create disposition we allow is OPEN.
            //

            if ((CreateDisposition != FILE_OPEN) &&
                (CreateDisposition != FILE_OPEN_IF)) {

                try_return(Status = STATUS_ACCESS_DENIED);
            }

            try_return(Status = UDFOpenObjectByFileId(IrpContext,
                                                      IrpSp,
                                                      Vcb,
                                                      (PFCB*)&CurrentFcb));
        }

        //
        // If the remaining name is empty, we already have our target:
        //  - No RelatedFileObject: root directory open
        //  - RelatedFileObject + empty FileName: reopen the related file
        // Acquire the target FCB, reference its FileInfo, and go
        // directly to the common open path.
        //
        if (RemainingName.Length == 0) {

            if (OpenTargetDirectory) {
                // Reject OpenTargetDirectory on stream objects —
                // streams have no parent directory context.
                if (RelatedFileObject && NextFcb && NextFcb->FileInfo &&
                    (UDFIsAStream(NextFcb->FileInfo) || UDFIsAStreamDir(NextFcb->FileInfo))) {
                    try_return(Status = STATUS_INVALID_PARAMETER);
                }
                // For normal files/dirs: fall through to open the current directory
            }

            if (RelatedFileObject) {
                // Reopen: target is the FCB from RelatedFileObject
                PtrNewFcb = NextFcb;
            } else {
                // Root directory open
                PtrNewFcb = Vcb->RootIndexFcb;
                if (DeleteOnClose) {
                    try_return(Status = STATUS_CANNOT_DELETE);
                }
            }

            LastGoodFileInfo = PtrNewFcb->FileInfo;

            // Validate file/directory type against caller's request
            if (PtrNewFcb->FcbState & UDF_FCB_DIRECTORY) {
                if (NonDirectoryFile ||
                    (CreateDisposition == FILE_SUPERSEDE) ||
                    (CreateDisposition == FILE_OVERWRITE) ||
                    (CreateDisposition == FILE_OVERWRITE_IF)) {
                    try_return(Status = STATUS_FILE_IS_A_DIRECTORY);
                }
            } else {
                if (DirectoryFile) {
                    try_return(Status = STATUS_NOT_A_DIRECTORY);
                }
            }
            if (CreateDisposition == FILE_CREATE) {
                try_return(Status = STATUS_OBJECT_NAME_COLLISION);
            }

            // Acquire target FCB exclusively
            Status = UDFMoveCreateFcb(IrpContext, Vcb, PtrNewFcb, (PFCB*)&CurrentFcb, (PFCB*)&PreviousFcb);
            if (Status != STATUS_SUCCESS) try_return(Status);

            // Reference the FileInfo (balanced by UDFCloseFile__ on error
            // or by IRP_MJ_CLEANUP on success)
            NewFileInfo = (RelatedCcb && RelatedCcb->FileInfo) ?
                RelatedCcb->FileInfo : PtrNewFcb->FileInfo;
            Status = UDFReferenceFileChecked(Vcb, NewFileInfo);
            if (!NT_SUCCESS(Status)) {
                NewFileInfo = NULL;
                try_return(Status);
            }

            try_return(Status = UDFOpenExistingFcb(IrpContext, IrpSp, Vcb,
                                                    FileObject, NewFileInfo,
                                                    (PFCB*)&PtrNewFcb, IgnoreCase,
                                                    RelatedFileObject, NextFcb, CreateDisposition));
        }

        _SEH2_TRY {
            AdPrint(("    Opening file %wZ %8.8x\n",FileName, FileObject));
        } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
            AdPrint(("    Exception when printing FN\n"));
        } _SEH2_END;

        {

        //AdPrint(("    Opening file %ws %8.8x\n",FileName->Buffer, PtrNewFileObject));

        if (FileName->Length > UDF_X_PATH_LEN*sizeof(WCHAR)) {
            try_return(Status = STATUS_OBJECT_NAME_INVALID);
        }

        // validate path specified
        // (sometimes we can see here very strange characters ;)
        if (!UDFIsNameValid(FileName, &StreamOpen, &SNameIndex)) {
            AdPrint(("    Absolute path is not valid\n"));
            try_return(Status = STATUS_OBJECT_NAME_INVALID);
        }
        if (StreamOpen && !UDFIsStreamsSupported(Vcb)) {
            try_return(Status = STATUS_OBJECT_NAME_INVALID);
        }

        // RemainingName was set by UDFNormalizeFileNames to point to the relative path
        // after RelatedFileName (or the full path if no RelatedFileObject)

        if (RelatedFileObject) {
            // Our "start directory" is the one identified by the related file object
            RelatedFileInfo = (RelatedCcb && RelatedCcb->FileInfo) ?
                RelatedCcb->FileInfo : NextFcb->FileInfo;
        } else {
            // Start at the root of the file system
            RelatedFileInfo = Vcb->RootIndexFcb->FileInfo;
        }

        // RemainingName keeps the full path including any ":stream:$DATA"
        // suffix. The traversal loop parses the stream component in place:
        // UDFDissectName splits on ':' and the suffix is validated as each
        // stream component is reached.
        FinalName.MaximumLength = RemainingName.MaximumLength;

        Status = STATUS_SUCCESS;
        LastGoodFileInfo = RelatedFileInfo;
        // reference RelatedObject to prevent releasing parent structures
        // Acquire only CurrentFcb (= LastGoodFileInfo->Fcb)
        {
            PFCB Start = RelatedFileInfo->Fcb;
            UDF_CHECK_PAGING_IO_RESOURCE(Start);
            UDFAcquireFcbExclusive(IrpContext, Start, FALSE);
            CurrentFcb = Start; /* only after the resource was acquired */
        }

        // Parent references are now handled by LCB in UDFCompleteFcbOpen
        // when child files are opened through this directory.

        // ****************
        // Use UDFFindPathPrefix to find the deepest already-opened
        // directory in the path. This avoids redundant directory
        // traversal for already-cached paths.
        // ****************
        {
            BOOLEAN ShortNameMatch = FALSE;
            PFCB PrefixStart = CurrentFcb, PrefixFcb;
            UNICODE_STRING SearchPath = RemainingName;
            PLCB PrefixLcb;

            PrefixLcb = UDFFindPathPrefix(
                IrpContext,
                CurrentFcb,
                IgnoreCase,
                (PFCB*)&CurrentFcb,
                &SearchPath,
                &ShortNameMatch
            );

            /* The prefix walker updates our actual acquisition slot on
             * each transition, including before a later raised failure. */
            PrefixFcb = CurrentFcb;
            PreviousFcb = CurrentFcb; // only one resource survived the walk
            if (PrefixLcb && PrefixFcb && PrefixFcb != PrefixStart && PrefixLcb->LinkFileInfo) {

                // Check for delete-in-progress on matched FCB
                if (PrefixLcb->Flags & UDF_LCB_FLAG_LINK_DELETED) {
                    try_return(Status = STATUS_DELETE_PENDING);
                }

                // Check if entire path was matched
                if (SearchPath.Length == 0 ||
                    (SearchPath.Length == sizeof(WCHAR) && SearchPath.Buffer[0] == L'\\')) {

                    if (OpenTargetDirectory) {
                        // File found via prefix search, but caller wants the parent directory.
                        // Switch from file FCB to parent directory FCB and set up state
                        // so the loop runs one iteration — UDFFindDirEntry finds the file,
                        // then the early OpenTargetDirectory check opens the parent.
                        PFCB ParentFcb = PrefixLcb->ParentFcb;

                        if (!ParentFcb || !ParentFcb->FileInfo) {
                            try_return(Status = STATUS_INVALID_PARAMETER);
                        }

                        Status = UDFMoveCreateFcb(IrpContext, Vcb, ParentFcb,
                                    (PFCB*)&CurrentFcb, (PFCB*)&PreviousFcb);
                        if (Status != STATUS_SUCCESS) try_return(Status);
                        if (PreviousFcb && PreviousFcb != CurrentFcb) {
                            PFCB OldChild = PreviousFcb;
                            PreviousFcb = CurrentFcb;
                            UDFReleaseFcb(IrpContext, OldChild);
                        }

                        OldRelatedFileInfo = RelatedFileInfo;
                        RelatedFileInfo = PrefixLcb->ParentFileInfo;
                        LastGoodFileInfo = RelatedFileInfo;

                        // Set RemainingName to file name so UDFDissectName extracts it
                        // into FinalName, then the early OpenTargetDirectory check
                        // (before UDFOpenFileInfoFromDirContext) opens the parent.
                        RemainingName = PrefixLcb->FileName;

                    } else {

                    // Entire path found in prefix - skip loop, open directly
                    OpenLcb = PrefixLcb;
                    PtrNewFcb = PrefixFcb;
                    LastGoodFileInfo = PrefixLcb->LinkFileInfo;
                    OldRelatedFileInfo = RelatedFileInfo;

                    // Validate file/directory type against caller's request
                    if (PtrNewFcb->FcbState & UDF_FCB_DIRECTORY) {
                        if (NonDirectoryFile ||
                            (CreateDisposition == FILE_SUPERSEDE) ||
                            (CreateDisposition == FILE_OVERWRITE) ||
                            (CreateDisposition == FILE_OVERWRITE_IF)) {
                            try_return(Status = STATUS_FILE_IS_A_DIRECTORY);
                        }
                    } else {
                        if (DirectoryFile) {
                            try_return(Status = STATUS_NOT_A_DIRECTORY);
                        }
                    }
                    if (CreateDisposition == FILE_CREATE) {
                        try_return(Status = STATUS_OBJECT_NAME_COLLISION);
                    }

                    // Reference the FileInfo (will be balanced by cleanup)
                    NewFileInfo = PrefixLcb->LinkFileInfo;
                    Status = UDFReferenceFileChecked(Vcb, NewFileInfo);
                    if (!NT_SUCCESS(Status)) {
                        NewFileInfo = NULL;
                        try_return(Status);
                    }

                    // Skip the search loop - open directly
                    try_return(Status = UDFOpenExistingFcb(IrpContext, IrpSp, Vcb,
                                                            FileObject, NewFileInfo,
                                                            (PFCB*)&PtrNewFcb, IgnoreCase,
                                                            RelatedFileObject, NextFcb, CreateDisposition));

                    } // end if OpenTargetDirectory

                } else {
                    // Partial match - use as starting point for loop
                    OldRelatedFileInfo = RelatedFileInfo;
                    RelatedFileInfo = PrefixLcb->LinkFileInfo;
                    LastGoodFileInfo = RelatedFileInfo;
                    RemainingName = SearchPath;
                }
            }
        }

        // Check if the starting directory FCB is in a valid state.
        //   NEEDS_VERIFICATION: FID/ICB mismatch after verify → STATUS_REPARSE
        //   NOT_FOUND_ON_MEDIA: object gone from disk → STATUS_OBJECT_PATH_NOT_FOUND
        //   DELETED: FCB deleted → STATUS_OBJECT_PATH_NOT_FOUND
        if (CurrentFcb->FcbState & UDF_FCB_NEEDS_VERIFICATION) {
            try_return(Status = STATUS_REPARSE);
        }
        if (CurrentFcb->FcbState & (UDF_FCB_NOT_FOUND_ON_MEDIA | UDF_FCB_DELETED)) {
            try_return(Status = STATUS_OBJECT_PATH_NOT_FOUND);
        }

        // Path traversal start point must be a directory — unless the
        // remaining path is a stream suffix (":stream"), in which case the
        // start point is the base file that owns the stream (e.g. the prefix
        // search matched the base file and left ":stream" to process).
        if (!(CurrentFcb->FcbState & UDF_FCB_DIRECTORY) &&
            !(RemainingName.Length && RemainingName.Buffer[0] == L':')) {
            try_return(Status = STATUS_OBJECT_PATH_NOT_FOUND);
        }

        // go into a loop parsing the supplied name

        //  Note that we may have to "open" intermediate directory objects
        //  while traversing the path. We should __try to reuse existing code
        //  whenever possible therefore we should consider using a common
        //  open routine regardless of whether the open is on behalf of the
        //  caller or an intermediate (internal) open performed by the driver.

        // ****************
        // now we'll parse path to desired file
        // ****************

        // Path traversal requires blocking I/O — if caller cannot wait,
        // raise so the request gets requeued to a worker thread.
        if (!FlagOn(IrpContext->Flags, IRP_CONTEXT_FLAG_WAIT)) {
            UDFRaiseStatus(IrpContext, STATUS_CANT_WAIT);
        }

        BOOLEAN IsStreamComponent = FALSE;

        while (TRUE) {

            // Intermediate traversal references were released below. Do not
            // let a later name-validation failure close that borrowed FileInfo.
            NewFileInfo = NULL;
            PtrNewFcb = NULL;

            // Split off the next component from the name.

            UDFDissectName(IrpContext, &RemainingName, &FinalName, &IsStreamComponent);

            // Cannot open children within a named stream — streams are leaf objects.

            if (RelatedFileInfo && UDFIsAStream(RelatedFileInfo)) {

                try_return(Status = STATUS_INVALID_PARAMETER);
            }

            // For a stream component, the remaining tail is its type suffix.
            // Validate it (":$DATA" only) and consume it from RemainingName so
            // the stream name in FinalName is processed as the final component.

            if (IsStreamComponent) {

                UDFNormalizeStreamSuffix(IrpContext, &FinalName, &RemainingName);
            }

            if ( FinalName.Length &&
               (NT_SUCCESS(Status) || !StreamOpen)) {
                // ...wow! non-zero! try to open!
                if (!NT_SUCCESS(Status)) {
                    AdPrint(("    Error opening path component\n"));
                    if (Status == STATUS_OBJECT_NAME_NOT_FOUND)
                        Status = STATUS_OBJECT_PATH_NOT_FOUND;
                    try_return(Status);
                }

                // Note: FcbReference may be 0 for intermediate directories during path traversal;
                // it will be incremented when CCB is created in UDFCompleteFcbOpen

                // Mark intermediate directory FCB as valid (internal open)
                if (RelatedFileInfo && RelatedFileInfo->ParentFile) {
                    RelatedFileInfo->Fcb->NtReqFCBFlags |= UDF_NTREQ_FCB_VALID;
                    RelatedFileInfo->Fcb->FcbState |= UDF_FCB_VALID;
                }
                // Validate path component: length and characters.
                // Full path was validated by UDFIsNameValid, but stream names
                // injected via RemainingName need per-component checking.
                if (FinalName.Length > UDF_X_NAME_LEN * sizeof(WCHAR)) {
                    AdPrint(("    Path component is too long\n"));
                    try_return(Status = STATUS_OBJECT_NAME_INVALID);
                }
                // Note: per-component character validation is not needed here.
                // UDFIsNameValid already validated the full path before the loop.
                // UDFIsNameValid rejects trailing '.' which would break "." and ".." components.
                // Acquire FCB lock for tree traversal
                // Skip if same FCB to avoid recursive acquire leading to lock leak
                Status = UDFMoveCreateFcb(IrpContext, Vcb, RelatedFileInfo->Fcb,
                            (PFCB*)&CurrentFcb, (PFCB*)&PreviousFcb);
                if (Status != STATUS_SUCCESS) try_return(Status);

                // check traverse rights (uses RelatedFileInfo->Fcb which is now locked as PreviousFcb)
                Status = UDFCheckAccessRights(NULL, NULL, RelatedFileInfo->Fcb, RelatedCcb, FILE_TRAVERSE, 0);
                if (!NT_SUCCESS(Status)) {
                    AdPrint(("    Traverse check failed\n"));
                } else if (!IsStreamComponent) {
                    // Parent must be a directory for non-stream path traversal.
                    if (!(CurrentFcb->FcbState & UDF_FCB_DIRECTORY)) {
                        Status = STATUS_OBJECT_PATH_NOT_FOUND;
                    } else {
                    // standard open: first find, then open
                    Status = UDFFindDirEntry(Vcb, RelatedFileInfo, &FinalName, IgnoreCase, TRUE, &DirContext);
                    if (NT_SUCCESS(Status)) {
                        // Directories cannot exist inside a stream directory.
                        if (UDFIsAStreamDir(RelatedFileInfo) &&
                            (DirContext.DirNdx->FileCharacteristics & FILE_DIRECTORY)) {
                            Status = STATUS_OBJECT_PATH_NOT_FOUND;
                        } else
                        // Check if intermediate path component is a directory.
                        // A leading ':' in RemainingName is a stream suffix, not a
                        // subdirectory — a file with a stream is still valid here.
                        if (RemainingName.Length &&
                            RemainingName.Buffer[0] != L':' &&
                            !(DirContext.DirNdx->FileCharacteristics & FILE_DIRECTORY)) {
                            AdPrint(("    Not a directory\n"));
                            Status = STATUS_NOT_A_DIRECTORY;
                        } else {
                            // OpenTargetDirectory: file found via dir search,
                            // but caller wants the parent directory, not the file.
                            // Skip opening the file entirely — open parent with FILE_EXISTS.
                            if (OpenTargetDirectory && !RemainingName.Length) {
                                if (!UDFIsADirectory(LastGoodFileInfo)) {
                                    try_return(Status = STATUS_NOT_A_DIRECTORY);
                                }
                                NewFileInfo = LastGoodFileInfo;
                                PtrNewFcb = NewFileInfo->Fcb;
                                Status = UDFReferenceFileChecked(Vcb, NewFileInfo);
                                if (!NT_SUCCESS(Status)) {
                                    NewFileInfo = NULL;
                                    try_return(Status);
                                }
                                Status = UDFCheckAccessRights(FileObject, AccessState, PtrNewFcb, NULL, DesiredAccess, ShareAccess);
                                if (!NT_SUCCESS(Status)) {
                                    try_return(Status);
                                }
                                Status = UDFCompleteFcbOpen(IrpContext, IrpSp, Vcb, (PFCB*)&PtrNewFcb, NewFileInfo, UserDirectoryOpen,
                                                       (IgnoreCase ? CCB_FLAG_IGNORE_CASE : 0), CreateDisposition, FALSE,
                                                       FILE_EXISTS);
                                LastGoodFileInfo = NewFileInfo;
                                try_return(Status);
                            }
                            if (!RemainingName.Length) {
                                // Metadata files are internal volume structures and
                                // must not be opened by the caller.
                                if (DirContext.DirNdx->FileCharacteristics & FILE_METADATA) {
                                    try_return(Status = STATUS_ACCESS_DENIED);
                                }
                                // Final path component found — break out of loop.
                                // Will be opened post-loop with PerformUserOpen=TRUE.
                                break;
                            }
                            // Intermediate path component (or the base file of a
                            // stream path, where RemainingName still holds ":stream")
                            // — open now without user open. Non-zero RemainingNameLength
                            // tells UDFOpenObjectFromDirContext to create an LCB.
                            Status = UDFOpenObjectFromDirContext(
                                IrpContext, IrpSp, Vcb, &DirContext,
                                RelatedFileInfo,
                                (PFCB*)&CurrentFcb, (PFCB*)&PreviousFcb,
                                IgnoreCase, FALSE, CreateDisposition,
                                RemainingName.Length,
                                NULL, NULL,
                                NULL,  // ExistingFileInfo
                                (PUDF_FILE_INFO*)&NewFileInfo, (PFCB*)&PtrNewFcb);
                            if (NT_SUCCESS(Status)) {
                                LastGoodFileInfo = NewFileInfo;
                                if (FreshFileInfo == NewFileInfo) FreshFileInfo = NULL;
                                Status = UDFReleaseCreateReference(IrpContext, Vcb, (PUDF_FILE_INFO*)&NewFileInfo, FALSE);
                                if (Status != STATUS_SUCCESS) try_return(Status);
                            }
                        }
                    }
#ifdef UDF_DBG
                    else if (Status == STATUS_NOT_A_DIRECTORY) {
                        AdPrint(("    Not a directory\n"));
                    }
#endif // UDF_DBG
                    } // end directory check
                } else {
                    // Stream component: open or create the Stream Directory,
                    // then open it through the unified path (FCB + lock + LCB).
                    // DissectName already consumed the ':' prefix, so FinalName
                    // is the stream name (e.g., "stream1" not ":stream1").

                    // OpenTargetDirectory (rename/move target lookup) is not a
                    // meaningful operation on a named stream — reject it.

                    if (OpenTargetDirectory) {

                        try_return(Status = STATUS_INVALID_PARAMETER);
                    }

                    PUDF_FILE_INFO StreamDirInfo = NULL;
                    Status = UDFOpenStreamDir__(IrpContext, Vcb, RelatedFileInfo, &StreamDirInfo);
                    if (Status == STATUS_NOT_FOUND) {
                        // Stream Dir doesn't exist, but caller wants it to be
                        // created. Lets try to help him...
                        if ((CreateDisposition == FILE_CREATE) ||
                           (CreateDisposition == FILE_OPEN_IF) ||
                           (CreateDisposition == FILE_OVERWRITE_IF)) {
                            Status = UDFCreateStreamDir__(IrpContext, Vcb, RelatedFileInfo, &StreamDirInfo);
                        }
                        if (Status == STATUS_NOT_FOUND) {
                            // Preserve conversion/allocation/I/O errors from stream creation.
                            Status = STATUS_OBJECT_NAME_NOT_FOUND;
                        }
                    }
                    if (NT_SUCCESS(Status)) {
                        // Re-inject stream name: next iteration will search
                        // for FinalName within the stream directory.
                        RemainingName = FinalName;
                        // Open stream dir through unified path — handles
                        // FCB creation, lock management, and LCB creation.
                        Status = UDFOpenObjectFromDirContext(
                            IrpContext, IrpSp, Vcb,
                            NULL,           // no DirContext
                            RelatedFileInfo,
                            (PFCB*)&CurrentFcb, (PFCB*)&PreviousFcb,
                            IgnoreCase, FALSE, CreateDisposition,
                            RemainingName.Length,
                            NULL, NULL,
                            StreamDirInfo,  // ExistingFileInfo
                            (PUDF_FILE_INFO*)&NewFileInfo, (PFCB*)&PtrNewFcb);
                        if (NT_SUCCESS(Status)) {
                            LastGoodFileInfo = NewFileInfo;
                            if (FreshFileInfo == NewFileInfo) FreshFileInfo = NULL;
                            Status = UDFReleaseCreateReference(IrpContext, Vcb, (PUDF_FILE_INFO*)&NewFileInfo, FALSE);
                            if (Status != STATUS_SUCCESS) try_return(Status);
                        } else {
                            /* ExistingFileInfo's successful open is returned
                             * in NewFileInfo. Common failure unwind owns its
                             * one close; do not close StreamDirInfo as well. */
                            try_return(Status);
                        }
                    }
                }

                if (!NT_SUCCESS(Status)) {
                    AdPrint(("    Can't open file\n"));
                }

                // Delete-on-close flags on the parent directory should not block
                // creating new files inside it - only the specific deleted file
                // should return STATUS_DELETE_PENDING when opened.

                // OpenTargetDirectory + file not found: open parent directory.
                // LastGoodFileInfo still points to the parent (not updated on failure).
                if (OpenTargetDirectory && !RemainingName.Length &&
                    Status == STATUS_OBJECT_NAME_NOT_FOUND) {
                    if (!UDFIsADirectory(LastGoodFileInfo)) {
                        try_return(Status = STATUS_NOT_A_DIRECTORY);
                    }
                    NewFileInfo = LastGoodFileInfo;
                    PtrNewFcb = NewFileInfo->Fcb;
                    Status = UDFReferenceFileChecked(Vcb, NewFileInfo);
                    if (!NT_SUCCESS(Status)) {
                        NewFileInfo = NULL;
                        try_return(Status);
                    }
                    Status = UDFCheckAccessRights(FileObject, AccessState, PtrNewFcb, NULL, DesiredAccess, ShareAccess);
                    if (!NT_SUCCESS(Status)) {
                        try_return(Status);
                    }
                    Status = UDFCompleteFcbOpen(IrpContext, IrpSp, Vcb, (PFCB*)&PtrNewFcb, NewFileInfo, UserDirectoryOpen,
                                           (IgnoreCase ? CCB_FLAG_IGNORE_CASE : 0), CreateDisposition, FALSE,
                                           FILE_DOES_NOT_EXIST);
                    LastGoodFileInfo = NewFileInfo;
                    try_return(Status);
                }

                // update last good state information...
                OldRelatedFileInfo = RelatedFileInfo;
                RelatedFileInfo = NT_SUCCESS(Status) ? LastGoodFileInfo : NewFileInfo;

                // Base file of a stream path doesn't exist. Stop here: FinalName
                // holds the base name and RemainingName still holds the ":stream"
                // suffix. Post-loop creation builds the file, its stream directory,
                // and the stream from that suffix.
                if (Status == STATUS_OBJECT_NAME_NOT_FOUND && StreamOpen &&
                    RemainingName.Length && RemainingName.Buffer[0] == L':') {
                    break;
                }

                // If this was the final path component (RemainingName exhausted),
                // break out of loop for open/create finalization.
                // OpenTargetDirectory (file found) is handled earlier (before UDFOpenFileInfoFromDirContext).
                if (!RemainingName.Length) {
                    // File not found — create inline if disposition allows.
                    // Stream creation is handled separately post-loop.
                    if (Status == STATUS_OBJECT_NAME_NOT_FOUND && !StreamOpen) {
                        if ((CreateDisposition == FILE_OPEN) ||
                            (CreateDisposition == FILE_OVERWRITE)) {
                            AdPrint(("    File doesn't exist\n"));
                            try_return(Status);
                        }
                        if ((CreateDisposition != FILE_CREATE) && (CreateDisposition != FILE_OPEN_IF) &&
                            (CreateDisposition != FILE_OVERWRITE_IF) && (CreateDisposition != FILE_SUPERSEDE)) {
                            AdPrint(("    File doesn't exist (2)\n"));
                            try_return(Status);
                        }
                        if (Vcb->VcbState & VCB_STATE_MEDIA_WRITE_PROTECT) {
                            AdPrint(("    Media write protected\n"));
                            IoSetHardErrorOrVerifyDevice(Irp, Vcb->Vpb->RealDevice);
                            try_return(Status = STATUS_MEDIA_WRITE_PROTECTED);
                        }
                        if (Vcb->VcbState & VCB_STATE_VOLUME_READ_ONLY) {
                            AdPrint(("    Volume read only\n"));
                            try_return(Status = STATUS_ACCESS_DENIED);
                        }
                        if (DeleteOnClose &&
                           (FlagOn( IrpSp->Parameters.Create.FileAttributes, FILE_ATTRIBUTE_READONLY ))) {
                            AdPrint(("    Can't create r/o file marked for deletion\n"));
                            try_return(Status = FlagOn(Vcb->VcbState, VCB_STATE_MOUNTED_DIRTY)
                                                ? STATUS_VOLUME_DIRTY : STATUS_CANNOT_DELETE);
                        }
                        // Validate name: FinalName from UDFDissectName never contains backslashes
                        {
                            USHORT i;
                            USHORT CharCount = FinalName.Length / sizeof(WCHAR);
                            for(i = 0; i < CharCount; i++) {
                                if (FinalName.Buffer[i] == L'\\') {
                                    AdPrint(("    Target name should not contain (back)slashes\n"));
                                    try_return(Status = STATUS_OBJECT_NAME_INVALID);
                                }
                            }
                        }
                        if (DirectoryFile &&
                           ((IrpSp->Parameters.Create.FileAttributes & FILE_ATTRIBUTE_TEMPORARY) ||
                             FALSE)) {
                            AdPrint(("    Creation of _temporary_ directory not permited\n"));
                            try_return(Status = STATUS_INVALID_PARAMETER);
                        }
                        // Check access rights on parent directory
                        ASSERT(CurrentFcb);
                        Status = UDFCheckAccessRights(NULL, NULL, OldRelatedFileInfo->Fcb, RelatedCcb,
                                                      DirectoryFile ? FILE_ADD_SUBDIRECTORY : FILE_ADD_FILE, 0);
                        if (!NT_SUCCESS(Status)) {
                            AdPrint(("    Creation of File/Dir not permitted\n"));
                            try_return(Status);
                        }
                        // Create the file on disk
                        Status = UDFCreateFile__(IrpContext, Vcb, IgnoreCase, &FinalName, 0, 0,
                                             UdfIsExtendedFESupported(Vcb),
                                             (CreateDisposition == FILE_CREATE), OldRelatedFileInfo, (PUDF_FILE_INFO*)&NewFileInfo,
                                             (PBOOLEAN)&FreshFileCreated);
                        FreshFileInfo = NT_SUCCESS(Status) && FreshFileCreated ? NewFileInfo : NULL;
                        if (!NT_SUCCESS(Status)) {
                            UDFPrint(("UDF BM: UDFCreateFile__ FAILED Status=%x Dir=%d Name=%wZ\n",
                                Status, DirectoryFile, &FinalName));
                            try_return(Status);
                        }
                        // Update parent object
                        if ((Vcb->CompatFlags & UDF_VCB_IC_UPDATE_DIR_WRITE) &&
                           NextFcb &&
                           RelatedFileObject &&
                           (NextFcb->FileInfo == NewFileInfo->ParentFile)) {
                                RelatedFileObject->Flags |= (FO_FILE_MODIFIED | FO_FILE_SIZE_CHANGED);
                        }
                        if (DirectoryFile && FreshFileCreated) {
                            Status = UDFRecordDirectory__(IrpContext, Vcb, NewFileInfo);
                            if (!NT_SUCCESS(Status)) {
                                AdPrint(("    Can't transform to directory\n"));
                                /* Common unwind queues rollback only for a fresh FI. */
                                try_return(Status);
                            }
                        }
                        NewFileCreated = TRUE;
                        if (FreshFileCreated) CreateDisposition = FILE_CREATE;
                        Status = STATUS_SUCCESS;
                    }
                    break;
                }
                // ...and go to the next open cycle
            } else {
                // ************
                if (StreamOpen && (Status == STATUS_NOT_FOUND))
                    // handle SDir return code
                    Status = STATUS_OBJECT_NAME_NOT_FOUND;
                if (Status == STATUS_OBJECT_NAME_NOT_FOUND) {
                    // good path, but no such file
                    // break open loop and continue with Create
                    break;
                }
                if (!NT_SUCCESS(Status)) {
                    // Hard error or damaged data structures ...
#ifdef UDF_DBG
                    if ((Status != STATUS_OBJECT_PATH_NOT_FOUND) &&
                       (Status != STATUS_ACCESS_DENIED) &&
                       (Status != STATUS_NOT_A_DIRECTORY)) {
                        AdPrint(("    Hard error or damaged data structures\n"));
                    }
#endif // UDF_DBG
                    // ... and exit with error
                    try_return(Status);
                }
                // Note: With LCB model, FcbReference is not incremented during path traversal,
                // so no decrement is needed here (removed the old InterlockedDecrement).
                Status = STATUS_SUCCESS;
                ASSERT(!OpenTargetDirectory);
                // Restore PtrNewFcb/NewFileInfo from last good state.
                // The stream dir branch didn't open anything in this iteration,
                // but PtrNewFcb was reset to NULL at iteration start.
                // Restore from LastGoodFileInfo (the base file).
                PtrNewFcb = LastGoodFileInfo->Fcb;
                NewFileInfo = LastGoodFileInfo;
                Status = UDFReferenceFileChecked(Vcb, NewFileInfo);
                if (!NT_SUCCESS(Status)) {
                    NewFileInfo = NULL;
                    try_return(Status);
                }
                // break open loop and continue with Open
                // (Create will be skipped)
                break;
            }
        } // end of while(TRUE)

        // ****************
        // Newly created file — open via unified path
        // ****************
        if (NewFileCreated) {
            ASSERT(NT_SUCCESS(Status));
            ASSERT(NewFileInfo);
            ASSERT(!StreamOpen);

            // Open the newly created file with PerformUserOpen=TRUE.
            // Uses ExistingFileInfo to skip disk read (FileInfo already created by UDFCreateFile__).
            Status = UDFOpenObjectFromDirContext(
                IrpContext, IrpSp, Vcb, NULL,
                OldRelatedFileInfo,
                (PFCB*)&CurrentFcb, (PFCB*)&PreviousFcb,
                IgnoreCase, TRUE, CreateDisposition,
                0,  // RemainingNameLength = 0 (final component)
                RelatedFileObject, NextFcb,
                NewFileInfo,  // ExistingFileInfo — skip disk read
                (PUDF_FILE_INFO*)&NewFileInfo, (PFCB*)&PtrNewFcb);
            if (!NT_SUCCESS(Status)) {
                AdPrint(("    Can't open newly created file\n"));
                /* Common unwind separates the CCB hold from fresh-FI rollback. */
                try_return(Status);
            }

            // FILE_CREATED, allocation size, attributes, and notification are
            // all handled inside UDFOpenExistingFcb via CreateDisposition == FILE_CREATE.

            LastGoodFileInfo = NewFileInfo;
            try_return(Status);
        }

        // ****************
        // should we CREATE a new stream ?
        // (Non-stream creation is handled inline above)
        // ****************
        if (!NT_SUCCESS(Status)) {
            if (Status == STATUS_OBJECT_NAME_NOT_FOUND ||
                Status == STATUS_OBJECT_PATH_NOT_FOUND) {
                if ((CreateDisposition == FILE_OPEN) ||
                    (CreateDisposition == FILE_OVERWRITE)) {
                    AdPrint(("    File doesn't exist\n"));
                    try_return(Status);
                }
            } else {
                //  Any other operation return STATUS_ACCESS_DENIED.
                AdPrint(("    Can't create due to unexpected error\n"));
                try_return(Status);
            }
            // Object was not found, create if requested
            if ((CreateDisposition != FILE_CREATE) && (CreateDisposition != FILE_OPEN_IF) &&
                 (CreateDisposition != FILE_OVERWRITE_IF) && (CreateDisposition != FILE_SUPERSEDE)) {
                AdPrint(("    File doesn't exist (2)\n"));
                try_return(Status);
            }
            // Check write protection
            if (Vcb->VcbState & VCB_STATE_MEDIA_WRITE_PROTECT) {
                AdPrint(("    Media write protected\n"));
                IoSetHardErrorOrVerifyDevice(Irp, Vcb->Vpb->RealDevice);
                try_return(Status = STATUS_MEDIA_WRITE_PROTECTED);
            }
            if (Vcb->VcbState & VCB_STATE_VOLUME_READ_ONLY) {
                AdPrint(("    Volume read only\n"));
                try_return(Status = STATUS_ACCESS_DENIED);
            }
            // Check r/o + delete on close
            if (DeleteOnClose &&
               (FlagOn( IrpSp->Parameters.Create.FileAttributes, FILE_ATTRIBUTE_READONLY ))) {

                AdPrint(("    Can't create r/o file marked for deletion\n"));
                try_return(Status = FlagOn(Vcb->VcbState, VCB_STATE_MOUNTED_DIRTY)
                                    ? STATUS_VOLUME_DIRTY : STATUS_CANNOT_DELETE);
            }

            // Create a new file/directory here ...
            // FinalName from UDFDissectName never contains backslashes
            {
                USHORT i;
                USHORT CharCount = FinalName.Length / sizeof(WCHAR);
                for(i = 0; i < CharCount; i++) {
                    if (FinalName.Buffer[i] == L'\\') {
                        AdPrint(("    Target name should not contain (back)slashes\n"));
                        try_return(Status = STATUS_OBJECT_NAME_INVALID);
                    }
                }
            }
            if (DirectoryFile &&
               ((IrpSp->Parameters.Create.FileAttributes & FILE_ATTRIBUTE_TEMPORARY) ||
                 StreamOpen || FALSE)) {
                AdPrint(("    Creation of _temporary_ directory not permited\n"));
                try_return(Status = STATUS_INVALID_PARAMETER);
            }
            // check access rights
            ASSERT(CurrentFcb);
            Status = UDFCheckAccessRights(NULL, NULL, OldRelatedFileInfo->Fcb, RelatedCcb, DirectoryFile ? FILE_ADD_SUBDIRECTORY : FILE_ADD_FILE, 0);
            if (!NT_SUCCESS(Status)) {
                AdPrint(("    Creation of File/Dir not permitted\n"));
                try_return(Status);
            }
            // Note that a FCB structure will be allocated at this time
            // and so will a CCB structure.
            // Further, note that since the file is being created, no other
            // thread can have the file stream open at this time.
            RelatedFileInfo = OldRelatedFileInfo;

            Status = UDFCreateFile__(IrpContext, Vcb, IgnoreCase, &FinalName, 0, 0,
                                 UdfIsExtendedFESupported(Vcb),
                                 (CreateDisposition == FILE_CREATE), RelatedFileInfo, (PUDF_FILE_INFO*)&NewFileInfo,
                                 (PBOOLEAN)&FreshFileCreated);
            FreshFileInfo = NT_SUCCESS(Status) && FreshFileCreated ? NewFileInfo : NULL;
            if (!NT_SUCCESS(Status)) {
                AdPrint(("    Creation error\n"));
                try_return(Status);
            }
            // Update parent object
            if ((Vcb->CompatFlags & UDF_VCB_IC_UPDATE_DIR_WRITE) &&
               NextFcb &&
               RelatedFileObject &&
               (NextFcb->FileInfo == NewFileInfo->ParentFile)) {
                    RelatedFileObject->Flags |= (FO_FILE_MODIFIED | FO_FILE_SIZE_CHANGED);
            }

            if (DirectoryFile && FreshFileCreated) {
                // user wants the new directory to be created
                Status = UDFRecordDirectory__(IrpContext, Vcb, NewFileInfo);
                if (!NT_SUCCESS(Status)) {
                    AdPrint(("    Can't transform to directory\n"));
                    /* Common unwind queues rollback only for a fresh FI. */
                    try_return(Status);
                }
            }

            // Decide whether a separate stream object must be created.
            // When OldRelatedFileInfo is already a stream directory, the base file
            // and SDir existed and FinalName (the stream) was just created above.
            // Otherwise, for a stream path, the base file was created above and its
            // stream suffix still sits in RemainingName — extract the stream name
            // (validating/consuming ":$DATA"). An empty stream name ("file::$DATA")
            // is the default data stream, i.e. the base file itself — nothing more
            // to create.
            BOOLEAN CreateStreamObject = StreamOpen && !UDFIsAStreamDir(OldRelatedFileInfo) &&
                                         RemainingName.Length && RemainingName.Buffer[0] == L':';
            if (CreateStreamObject) {
                BOOLEAN IsStreamDummy;
                RtlZeroMemory(&StreamName, sizeof(StreamName));
                UDFDissectName(IrpContext, &RemainingName, &StreamName, &IsStreamDummy);
                UDFNormalizeStreamSuffix(IrpContext, &StreamName, &RemainingName);
                CreateStreamObject = (StreamName.Length != 0);
            }

            if (CreateStreamObject) {

                // PHASE 0: Open the base file through unified path (FCB + lock + LCB)
                PUDF_FILE_INFO BaseFileInfo = NewFileInfo;
                Status = UDFOpenObjectFromDirContext(
                    IrpContext, IrpSp, Vcb,
                    NULL, RelatedFileInfo,
                    (PFCB*)&CurrentFcb, (PFCB*)&PreviousFcb,
                    IgnoreCase, FALSE, CreateDisposition,
                    1,  // non-zero: intermediate — creates LCB
                    NULL, NULL,
                    BaseFileInfo,
                    (PUDF_FILE_INFO*)&NewFileInfo, (PFCB*)&PtrNewFcb);
                if (!NT_SUCCESS(Status)) {
                    AdPrint(("    Can't open file for stream creation\n"));
                    BrutePoint();
                    try_return(Status);
                }
                LastGoodFileInfo = NewFileInfo;

                UDFNotifyReportChange(IrpContext, Vcb, NewFileInfo->Fcb,
                    UDFIsADirectory(NewFileInfo) ? FILE_NOTIFY_CHANGE_DIR_NAME : FILE_NOTIFY_CHANGE_FILE_NAME,
                    FILE_ACTION_ADDED,
                    NULL, FileObject);

                RelatedFileInfo = NewFileInfo;
                if (FreshFileInfo == NewFileInfo) FreshFileInfo = NULL;
                Status = UDFReleaseCreateReference(IrpContext, Vcb, (PUDF_FILE_INFO*)&NewFileInfo, FALSE);
                if (Status != STATUS_SUCCESS) try_return(Status);

                // PHASE 1: Create stream directory + open through unified path
                PUDF_FILE_INFO StreamDirInfo = NULL;
                Status = UDFCreateStreamDir__(IrpContext, Vcb, RelatedFileInfo, &StreamDirInfo);
                if (!NT_SUCCESS(Status)) {
                    AdPrint(("    Can't create SDir\n"));
                    BrutePoint();
                    try_return(Status);
                }

                Status = UDFOpenObjectFromDirContext(
                    IrpContext, IrpSp, Vcb,
                    NULL, RelatedFileInfo,
                    (PFCB*)&CurrentFcb, (PFCB*)&PreviousFcb,
                    IgnoreCase, FALSE, CreateDisposition,
                    1,  // non-zero: intermediate — creates LCB
                    NULL, NULL,
                    StreamDirInfo,
                    (PUDF_FILE_INFO*)&NewFileInfo, (PFCB*)&PtrNewFcb);
                if (!NT_SUCCESS(Status)) {
                    AdPrint(("    Can't open SDir\n"));
                    /* The returned NewFileInfo owns this same temporary
                     * stream open for common failure unwind, once. */
                    try_return(Status);
                }
                LastGoodFileInfo = NewFileInfo;

                RelatedFileInfo = NewFileInfo;
                if (FreshFileInfo == NewFileInfo) FreshFileInfo = NULL;
                Status = UDFReleaseCreateReference(IrpContext, Vcb, (PUDF_FILE_INFO*)&NewFileInfo, FALSE);
                if (Status != STATUS_SUCCESS) try_return(Status);

                // PHASE 2: Create stream file in the stream directory.
                // StreamName already holds the bare stream name (no leading ':').
                Status = UDFCreateFile__(IrpContext, Vcb, IgnoreCase, &StreamName, 0, 0,
                         UdfIsExtendedFESupported(Vcb), (CreateDisposition == FILE_CREATE),
                         RelatedFileInfo, (PUDF_FILE_INFO*)&NewFileInfo, (PBOOLEAN)&FreshFileCreated);
                FreshFileInfo = NT_SUCCESS(Status) && FreshFileCreated ? NewFileInfo : NULL;
                if (!NT_SUCCESS(Status)) {
                    AdPrint(("    Can't create Stream\n"));
                    BrutePoint();
                    try_return(Status);
                }
            }

            if (FreshFileCreated) CreateDisposition = FILE_CREATE;
            // Open the created object through unified path (FCB + CCB + lock)
            {
                PUDF_FILE_INFO CreatedFileInfo = NewFileInfo;
                Status = UDFOpenObjectFromDirContext(
                    IrpContext, IrpSp, Vcb,
                    NULL, RelatedFileInfo,
                    (PFCB*)&CurrentFcb, (PFCB*)&PreviousFcb,
                    IgnoreCase, TRUE, CreateDisposition,
                    0,  // final component — no LCB
                    NULL, NULL,
                    CreatedFileInfo,
                    (PUDF_FILE_INFO*)&NewFileInfo, (PFCB*)&PtrNewFcb);
            }
            if (!NT_SUCCESS(Status)) {
                AdPrint(("    Can't open created file or stream\n"));
                BrutePoint();
                /* Common unwind separates the CCB hold from fresh-FI rollback. */
                try_return(Status);
            }

            LastGoodFileInfo = NewFileInfo;
            try_return(Status);
        }

        } // end of path traversal block

        // Path traversal found existing file - open it.
        // Two paths:
        // (a) File path: PtrNewFcb is NULL, DirContext has the found entry — open via UDFOpenObjectFromDirContext(TRUE)
        // (b) Stream path: PtrNewFcb is set from loop — open via UDFOpenExistingFcb
        if (PtrNewFcb) {
            // Stream path — FCB already created in loop
            // Validate file/directory type against caller's request
            if (PtrNewFcb->FcbState & UDF_FCB_DIRECTORY) {
                if (NonDirectoryFile ||
                    (CreateDisposition == FILE_SUPERSEDE) ||
                    (CreateDisposition == FILE_OVERWRITE) ||
                    (CreateDisposition == FILE_OVERWRITE_IF)) {
                    try_return(Status = STATUS_FILE_IS_A_DIRECTORY);
                }
            } else {
                if (FlagOn(IrpContext->Flags, IRP_CONTEXT_FLAG_TRAIL_BACKSLASH) || DirectoryFile) {
                    try_return(Status = STATUS_NOT_A_DIRECTORY);
                }
            }
            if (CreateDisposition == FILE_CREATE) {
                try_return(Status = STATUS_OBJECT_NAME_COLLISION);
            }

            try_return(Status = UDFOpenExistingFcb(IrpContext, IrpSp, Vcb,
                                                    FileObject, NewFileInfo,
                                                    (PFCB*)&PtrNewFcb, IgnoreCase,
                                                    RelatedFileObject, NextFcb, CreateDisposition));
        }

        // File path — final component not yet opened, DirContext has the entry
        if (!DirContext.DirNdx) {
            BrutePoint();
            try_return(Status = STATUS_OBJECT_NAME_NOT_FOUND);
        }

        {
            BOOLEAN IsDirectory = !!(DirContext.DirNdx->FileCharacteristics & FILE_DIRECTORY);
            // Trailing backslash or DirectoryFile: target must be a directory
            if ((FlagOn(IrpContext->Flags, IRP_CONTEXT_FLAG_TRAIL_BACKSLASH) || DirectoryFile) &&
                !IsDirectory) {
                try_return(Status = STATUS_NOT_A_DIRECTORY);
            }
            if (IsDirectory) {
                if (NonDirectoryFile ||
                    (CreateDisposition == FILE_SUPERSEDE) ||
                    (CreateDisposition == FILE_OVERWRITE) ||
                    (CreateDisposition == FILE_OVERWRITE_IF)) {
                    try_return(Status = STATUS_FILE_IS_A_DIRECTORY);
                }
            }
        }
        // Metadata stream files (UDF 2.0) cannot be opened by name
        if (DirContext.DirNdx->FileCharacteristics & FILE_METADATA) {
            try_return(Status = STATUS_ACCESS_DENIED);
        }
        if (CreateDisposition == FILE_CREATE) {
            try_return(Status = STATUS_OBJECT_NAME_COLLISION);
        }

        // Open the final component with PerformUserOpen=TRUE (creates FileInfo + FCB + CCB)
        Status = UDFOpenObjectFromDirContext(
            IrpContext, IrpSp, Vcb, &DirContext,
            RelatedFileInfo,
            (PFCB*)&CurrentFcb, (PFCB*)&PreviousFcb,
            IgnoreCase, TRUE, CreateDisposition,
            0,  // RemainingNameLength = 0 (final component)
            RelatedFileObject, NextFcb,
            NULL,  // ExistingFileInfo
            (PUDF_FILE_INFO*)&NewFileInfo, (PFCB*)&PtrNewFcb);
        try_return(Status);

try_exit:   NOTHING;

        //
        //  Set final file object flags on successful open.
        //

        if (NT_SUCCESS(Status) && PtrNewFcb) {

            if (DeleteOnClose) {
                ASSERT(!(PtrNewFcb->FcbState & UDF_FCB_ROOT_DIRECTORY));
            }

            if (StreamOpen) {
                FileObject->Flags |= FO_STREAM_FILE;
            }

            PtrNewFcb->NtReqFCBFlags |= UDF_NTREQ_FCB_VALID;
            PtrNewFcb->FcbState |= UDF_FCB_VALID;

            AdPrint(("    FCB %x, FO %x, Flags %x\n", PtrNewFcb, FileObject, PtrNewFcb->FcbState));
        }

    } _SEH2_FINALLY {
        BOOLEAN BodyAbnormal = _SEH2_AbnormalTermination();
        _SEH2_TRY {
            if ((BodyAbnormal || !NT_SUCCESS(Status)) && NewFileInfo &&
                NewFileInfo == FreshFileInfo) {
                _SEH2_VOLATILE NTSTATUS QueueStatus = STATUS_UNSUCCESSFUL;
                _SEH2_TRY {
                    QueueStatus = UDFQueueCreateRollback(IrpContext, FileObject,
                        (PUDF_FILE_INFO*)&NewFileInfo,
                        BodyAbnormal ? STATUS_UNSUCCESSFUL : Status);
                } _SEH2_EXCEPT(FsRtlIsNtstatusExpected(_SEH2_GetExceptionCode()) ?
                              EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
                    QueueStatus = _SEH2_GetExceptionCode();
                } _SEH2_END;
                if (QueueStatus != STATUS_SUCCESS) {
                    if (NT_SUCCESS(QueueStatus)) QueueStatus = STATUS_IO_DEVICE_ERROR;
                    Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
                    /* Keep the original CREATE error/exception; an ownership
                     * admission failure is separately latched and never
                     * followed by a speculative flush/unlink. */
                    InterlockedCompareExchange(&Vcb->MetadataWritebackError,
                                                QueueStatus, STATUS_SUCCESS);
                }
            }

            //
            //  The result of this open could be success, pending or some error
            //  condition.
            //

            if (BodyAbnormal) {
                UDFUnwindCompletedOpen(IrpContext, FileObject,
                                        (PUDF_FILE_INFO*)&NewFileInfo);
                if (NewFileInfo)
                    UDFReleaseCreateReference(IrpContext, Vcb, (PUDF_FILE_INFO*)&NewFileInfo, TRUE);
                UDFTeardownCreateFcb(IrpContext, Vcb, (PFCB*)&CurrentFcb, (PFCB*)&PreviousFcb);

                // No need to complete the request.

                IrpContext = NULL;
                Irp = NULL;

            // If we posted this request we need to show that there is no
            // reason to complete the request.

            } else if (Status == STATUS_PENDING) {

                IrpContext = NULL;
                Irp = NULL;

            } else if (!NT_SUCCESS(Status)) {

                UDFUnwindCompletedOpen(IrpContext, FileObject,
                                        (PUDF_FILE_INFO*)&NewFileInfo);

                // Failure path — clean up partial structures.

                AdPrint(("UDF: CREATE FAILED RC=%x Fcb=%p Name='%wZ' Disp=%x DesAccess=%x\n",
                         Status, PtrNewFcb,
                         &(IrpSp->FileObject->FileName),
                         (IrpSp->Parameters.Create.Options >> 24) & 0xFF,
                         IrpSp->Parameters.Create.SecurityContext ?
                             IrpSp->Parameters.Create.SecurityContext->DesiredAccess : 0));

                // Balance UDFReferenceFile__ from prefix match / path traversal.

                if (NewFileInfo) {
                    UDFReleaseCreateReference(IrpContext, Vcb, (PUDF_FILE_INFO*)&NewFileInfo, TRUE);
                }

                // Mark last successfully opened directory as valid.

                if (LastGoodFileInfo && LastGoodFileInfo->Fcb) {
                    LastGoodFileInfo->Fcb->FcbState |= UDF_FCB_VALID;
                    LastGoodFileInfo->Fcb->NtReqFCBFlags |= UDF_NTREQ_FCB_VALID;
                }


                UDFTeardownCreateFcb(IrpContext, Vcb, (PFCB*)&CurrentFcb, (PFCB*)&PreviousFcb);
            }

        } _SEH2_FINALLY {
            /* A raised finalizer belongs to the outer dispatcher. Release
             * our resources, but never complete/free that IRP context here. */
            if (_SEH2_AbnormalTermination()) { IrpContext = NULL; Irp = NULL; }
            // Release the Fcb locks.

            if (PreviousFcb && PreviousFcb != CurrentFcb) {

                UDFReleaseFcb(IrpContext, PreviousFcb);
            }

            if (CurrentFcb != NULL) {

                UDFReleaseFcb(IrpContext, CurrentFcb);
            }

            // Release the Vcb.

            UDFReleaseVcb(IrpContext, Vcb);

            // Call our completion routine.  It will handle the case where either
            // the Irp and/or IrpContext are gone.

            UDFCompleteRequest( IrpContext, Irp, Status );

        } _SEH2_END;
    } _SEH2_END;

    return(Status);
} // end UDFCommonCreate()

/*************************************************************************
*
* Function: UDFFirstOpenFile()
*
* Description:
*   Perform first Open/Create initialization.
*
* Expected Interrupt Level (for execution) :
*
*  IRQL_PASSIVE_LEVEL
*
* Return Value: STATUS_SUCCESS/Error
*
*************************************************************************/
NTSTATUS
UDFFirstOpenFile(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
   OUT PFCB* PtrNewFcb,
    IN PUDF_FILE_INFO RelatedFileInfo,
    IN PUDF_FILE_INFO NewFileInfo
    )
{
    _SEH2_VOLATILE NTSTATUS Status = STATUS_UNSUCCESSFUL;
    _SEH2_VOLATILE PFCB NewFcb = NULL;
    _SEH2_VOLATILE BOOLEAN FcbExisted = FALSE;
    _SEH2_VOLATILE BOOLEAN TableLocked = FALSE, VcbLocked = FALSE;
    _SEH2_VOLATILE BOOLEAN Published = FALSE;
    FILE_ID FileId;
    NODE_TYPE_CODE NodeTypeCode;
    BOOLEAN IsDirectory, Embedded;
    PUDF_FILE_INFO AttributesFile;
    PDIR_INDEX_HDR Index;
    PDIR_INDEX_ITEM Entry;
    DIR_INDEX_ITEM DefaultAttributes;

    if (!PtrNewFcb) return STATUS_INVALID_PARAMETER;
    *PtrNewFcb = NULL;
    if (!IrpContext || !Vcb || !RelatedFileInfo || !RelatedFileInfo->Dloc ||
        !RelatedFileInfo->Dloc->FileEntry || !NewFileInfo ||
        !NewFileInfo->Dloc || !NewFileInfo->Dloc->FileEntry || !NewFileInfo->RefCount)
        return STATUS_FILE_INVALID;
    if (UDFIsReservedMetadataFile(NewFileInfo)) return STATUS_ACCESS_DENIED;
    if (!Vcb->SectorSize || NewFileInfo->Dloc->DataLoc.Length < 0 ||
        NewFileInfo->Dloc->DataLoc.Length > MAXLONGLONG - (Vcb->SectorSize - 1))
        return STATUS_FILE_CORRUPT_ERROR;
    IsDirectory = UDFIsADirectory(NewFileInfo);
    Embedded = !IsDirectory &&
        ((((PFILE_ENTRY)NewFileInfo->Dloc->FileEntry)->icbTag.flags &
          ICB_FLAG_ALLOC_MASK) == ICB_FLAG_AD_IN_ICB);
    FileId = UDFGetNTFileId(Vcb, NewFileInfo);
    NodeTypeCode = IsDirectory ? UDF_NODE_TYPE_INDEX : UDF_NODE_TYPE_DATA;
    if (IsDirectory) SetFlag(FileId.HighPart, FID_DIR_MASK);

    _SEH2_TRY {
        UDFLockFcbTable(IrpContext, Vcb);
        TableLocked = TRUE;
        NewFcb = UDFCreateFcb(IrpContext, FileId, NodeTypeCode, (PBOOLEAN)&FcbExisted);
        if (!NewFcb) { Status = STATUS_INSUFFICIENT_RESOURCES; _SEH2_LEAVE; }
        if (FcbExisted) {
            PUDF_FILE_INFO Canonical = NewFcb->FileInfo;
            if (NewFcb->TeardownInProgress || !(NewFcb->FcbState & UDF_FCB_VALID) ||
                !Canonical || !Canonical->Dloc || Canonical->Dloc != NewFileInfo->Dloc ||
                Canonical->Dloc->CommonFcb != NewFcb ||
                UDFIsADirectory(Canonical) != IsDirectory ||
                (NewFcb->FcbState & (UDF_FCB_DELETED | UDF_FCB_NOT_FOUND_ON_MEDIA))) {
                Status = STATUS_SHARING_VIOLATION;
                _SEH2_LEAVE;
            }
            NewFileInfo->Fcb = NewFcb;
            *PtrNewFcb = NewFcb;
            Published = TRUE;
            Status = STATUS_SUCCESS;
            _SEH2_LEAVE;
        }
        /* A missing table key does not prove a non-NULL canonical pointer
         * stale. It belongs to an earlier owner/unfinished admission. */
        if (NewFileInfo->Dloc->CommonFcb) {
            Status = STATUS_SHARING_VIOLATION;
            _SEH2_LEAVE;
        }
        /* Everything below still belongs to this private FCB. No new FI
         * or Dloc backpointer is installed until table admission succeeds. */
        NewFcb->FileInfo = NewFileInfo;
        NewFcb->ParentFcb = RelatedFileInfo->Fcb;
        UDFGetFileXTime(NewFileInfo, &NewFcb->CreationTime.QuadPart,
            &NewFcb->LastAccessTime.QuadPart, &NewFcb->ChangeTime.QuadPart,
            &NewFcb->LastWriteTime.QuadPart);
        NewFcb->Header.AllocationSize.QuadPart =
            UDFSysGetAllocSize(Vcb, NewFileInfo->Dloc->DataLoc.Length);
        NewFcb->Header.FileSize.QuadPart =
        NewFcb->Header.ValidDataLength.QuadPart = NewFileInfo->Dloc->DataLoc.Length;
        AttributesFile = UDFIsAStreamDir(NewFileInfo) ? RelatedFileInfo : NewFileInfo;
        Index = UDFGetDirIndexByFileInfo(AttributesFile);
        Entry = Index ? UDFDirIndex(Index, AttributesFile->Index) : NULL;
        RtlZeroMemory(&DefaultAttributes, sizeof(DefaultAttributes));
        NewFcb->FileAttributes = UDFAttributesToNT(Entry ? Entry : &DefaultAttributes,
                                                 AttributesFile->Dloc->FileEntry);

        UDFLockVcb(IrpContext, Vcb);
        VcbLocked = TRUE;
        Status = UDFInitializeFCB(NewFcb, Vcb, IsDirectory ? UDF_FCB_DIRECTORY : 0);
        if (Status != STATUS_SUCCESS) _SEH2_LEAVE;
        if (Embedded) NewFcb->FcbState |= UDF_FCB_EMBEDDED_DATA;
        if (NewFcb->FileAttributes & FILE_ATTRIBUTE_READONLY)
            NewFcb->FcbState |= UDF_FCB_READ_ONLY;
        Status = UDFInsertFcbIntoTable(IrpContext, NewFcb);
        if (Status != STATUS_SUCCESS) _SEH2_LEAVE;
        /* No fallible work follows this receipt while the two identity
         * locks are held. Publish one stable shared-data representative. */
        NewFileInfo->Fcb = NewFcb;
        NewFileInfo->Dloc->CommonFcb = NewFcb;
        NewFcb->NtReqFCBFlags |= UDF_NTREQ_FCB_VALID;
        NewFcb->FcbState |= UDF_FCB_VALID;
        *PtrNewFcb = NewFcb;
        Published = TRUE;
    } _SEH2_FINALLY {
        _SEH2_TRY {
            if (!Published && NewFcb && !FcbExisted &&
                FlagOn(NewFcb->FcbState, FCB_STATE_IN_FCB_TABLE)) {
                /* No expected/fallible step follows insertion. If an
                 * unexpected exception still interrupts publication, the
                 * exact indexed FCB must reserve its FI before caller close.
                 * Keep a counted gate, never a dangling borrowed pointer. */
                if (!VcbLocked) {
                    UDFLockVcb(IrpContext, Vcb);
                    VcbLocked = TRUE;
                }
                NewFileInfo->Fcb = NewFcb;
                NewFileInfo->Dloc->CommonFcb = NewFcb;
                NewFcb->TeardownInProgress = TRUE;
                if (!NewFcb->TeardownRetryPending) {
                    NewFcb->TeardownRetryPending = TRUE;
                    InterlockedIncrement(&Vcb->ClosedFcbRetryCount);
                }
                Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
                InterlockedCompareExchange(&Vcb->MetadataWritebackError,
                                            STATUS_FILE_CORRUPT_ERROR, STATUS_SUCCESS);
            }
        } _SEH2_FINALLY {
            if (VcbLocked) UDFUnlockVcb(IrpContext, Vcb);
            if (TableLocked) UDFUnlockFcbTable(IrpContext, Vcb);
        } _SEH2_END;
        if (!Published && NewFcb && !FcbExisted &&
            !FlagOn(NewFcb->FcbState, FCB_STATE_IN_FCB_TABLE)) {
            NewFcb->FileInfo = NULL;
            NewFcb->ParentFcb = NULL;
            UDFDeleteFcb(IrpContext, NewFcb);
        }
    } _SEH2_END;
    return Status;

} // end UDFFirstOpenFile()

/*************************************************************************
*
* Function: UDFOpenFile()
*
* Description:
*   Open a file/dir for the caller.
*
* Expected Interrupt Level (for execution) :
*
*  IRQL_PASSIVE_LEVEL
*
* Return Value: STATUS_SUCCESS/Error
*
*************************************************************************/
_Requires_lock_held_(_Global_critical_region_)
NTSTATUS
UDFCompleteFcbOpen(
    _In_ PIRP_CONTEXT IrpContext,
    _In_ PIO_STACK_LOCATION IrpSp,
    _In_ PVCB Vcb,
    _Inout_ PFCB *CurrentFcb,
    _In_opt_ PUDF_FILE_INFO OpenFileInfo,
    _In_ TYPE_OF_OPEN TypeOfOpen,
    _In_ ULONG UserCcbFlags,
    _In_ ULONG CreateDisposition,
    _In_ BOOLEAN VcbLocked,
    _In_ ULONG DesiredInformation
    )
{
    _SEH2_VOLATILE NTSTATUS Status = STATUS_SUCCESS;
    BOOLEAN LockVolume = FALSE;
    _SEH2_VOLATILE BOOLEAN VcbAcquired = FALSE;
    _SEH2_VOLATILE BOOLEAN DeleteCcbOnUnwind = FALSE;
    _SEH2_VOLATILE BOOLEAN ShareAccessUpdated = FALSE;
    _SEH2_VOLATILE BOOLEAN CacheCountAdded = FALSE;
    _SEH2_VOLATILE UNICODE_STRING FileName = {0};
    _SEH2_VOLATILE UNICODE_STRING CaseFileName = {0};
    _SEH2_VOLATILE UNICODE_STRING ShortName = {0};
    SHARE_ACCESS ShareAccessBefore;
    PFCB Fcb = *CurrentFcb;
    _SEH2_VOLATILE PCCB Ccb = NULL;
    ULONG Information = DesiredInformation;
    USHORT FileAttributes = IrpSp->Parameters.Create.FileAttributes;
    PIO_SECURITY_CONTEXT SecurityContext = IrpSp->Parameters.Create.SecurityContext;
    ACCESS_MASK AddedAccess = 0;
    ACCESS_MASK DesiredAccess = SecurityContext->DesiredAccess;
    BOOLEAN DeleteOnClose = BooleanFlagOn(IrpSp->Parameters.Create.Options, FILE_DELETE_ON_CLOSE);

    ASSERT_FCB(Fcb);
    if (!IsListEmpty(&Fcb->AbortedOpenQueue)) return STATUS_DEVICE_BUSY;
    if (TypeOfOpen != UserVolumeOpen &&
        (!OpenFileInfo || !OpenFileInfo->Dloc || !Fcb->FileInfo ||
         OpenFileInfo->Dloc != Fcb->FileInfo->Dloc ||
         !OpenFileInfo->RefCount))
        return STATUS_FILE_INVALID;
    if (OpenFileInfo && OpenFileInfo->ParentFile && !OpenFileInfo->ParentFile->Fcb)
        return STATUS_FILE_INVALID;
    if ((OpenFileInfo && (OpenFileInfo->LinkDeleteCommitted ||
         (OpenFileInfo->Dloc && OpenFileInfo->Dloc->DeleteCommitted))) ||
        (Fcb->FcbState & (UDF_FCB_DELETED | UDF_FCB_NOT_FOUND_ON_MEDIA)))
        return STATUS_FILE_DELETED;
    if (TypeOfOpen != UserVolumeOpen && UDFIsLinkDeletePending(Fcb, OpenFileInfo))
        return STATUS_DELETE_PENDING;
    if (DeleteOnClose && TypeOfOpen != UserVolumeOpen &&
        (!OpenFileInfo->ParentFile ||
         FlagOn(UserCcbFlags, CCB_FLAG_OPEN_BY_ID | CCB_FLAG_OPEN_RELATIVE_BY_ID)))
        return STATUS_CANNOT_DELETE;

    // Expand maximum allowed to something sensible for share access checking

    if (DesiredAccess == MAXIMUM_ALLOWED) {

        if (FlagOn(Vcb->VcbState, VCB_STATE_VOLUME_READ_ONLY)) {

            DesiredAccess = FILE_ALL_ACCESS & ~((TypeOfOpen != UserVolumeOpen ?
                                                 (FILE_WRITE_ATTRIBUTES           |
                                                  FILE_WRITE_DATA                 |
                                                  FILE_WRITE_EA                   |
                                                  FILE_ADD_FILE                   |                     
                                                  FILE_ADD_SUBDIRECTORY           |
                                                  FILE_APPEND_DATA) : 0)          |
                                                FILE_DELETE_CHILD                 |
                                                DELETE                            |
                                                WRITE_DAC);
        } else {

            DesiredAccess = FILE_ALL_ACCESS & ~(WRITE_DAC | FILE_WRITE_EA);
        }

        SecurityContext->DesiredAccess = DesiredAccess;
    }

    if (CreateDisposition == FILE_SUPERSEDE) {

        SetFlag(AddedAccess, ~(DesiredAccess) & DELETE);
        SetFlag(DesiredAccess, DELETE);
        SecurityContext->DesiredAccess = DesiredAccess;

    } else if ((CreateDisposition == FILE_OVERWRITE) ||
               (CreateDisposition == FILE_OVERWRITE_IF)) {

        SetFlag(AddedAccess, ~DesiredAccess & (FILE_WRITE_DATA | FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES));
        SetFlag(DesiredAccess, FILE_WRITE_DATA | FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES);
        SecurityContext->DesiredAccess = DesiredAccess;
    }

    // If this a volume open and the user wants to lock the volume then
    // purge and lock the volume.

    if ((TypeOfOpen == UserVolumeOpen) &&
        !FlagOn( IrpSp->Parameters.Create.ShareAccess, FILE_SHARE_READ)) {

        // If there are open handles then fail this immediately.

        if (Vcb->VcbCleanup != 0) {

            return STATUS_SHARING_VIOLATION;
        }

        // If we can't wait then force this to be posted.

        if (!FlagOn(IrpContext->Flags, IRP_CONTEXT_FLAG_WAIT)) {

            UDFRaiseStatus(IrpContext, STATUS_CANT_WAIT);
        }

        LockVolume = TRUE;

        // Flush the volume and make sure all of the user references
        // are gone.

        Status = UDFFlushVolume(IrpContext, Vcb, 0);

        if (!NT_SUCCESS(Status)) {

            return Status;
        }

        // Now force all of the delayed close operations to go away.

        UDFFspClose(Vcb);

        if (Vcb->VcbUserReference > Vcb->VcbResidualUserReference) {

            return STATUS_SHARING_VIOLATION;
        }
    }

    if (CreateDisposition == FILE_CREATE) {

        if (FileAttributes & FILE_ATTRIBUTE_TEMPORARY) {

            Fcb->FcbState |= FCB_STATE_TEMPORARY;
            Fcb->FileAttributes |= FILE_ATTRIBUTE_TEMPORARY;
        }
    }
    else {

        if ((Fcb->FileAttributes & FILE_ATTRIBUTE_READONLY) &&
            (Fcb->Header.NodeTypeCode != UDF_NODE_TYPE_INDEX) &&
            (DesiredAccess & (FILE_WRITE_DATA | FILE_APPEND_DATA))) {

            return STATUS_ACCESS_DENIED;
        }

        if (DeleteOnClose && (Fcb->FileAttributes & FILE_ATTRIBUTE_READONLY)) {

            return STATUS_CANNOT_DELETE;
        }
    }

    if (!(Ccb = UDFCreateCcb())) {

        return STATUS_INSUFFICIENT_RESOURCES;
    }

    DeleteCcbOnUnwind = TRUE;

    _SEH2_TRY {

        if (AddedAccess) {

            ClearFlag(DesiredAccess, AddedAccess);
            SecurityContext->DesiredAccess = DesiredAccess;
        }

        // If write access or delete-on-close requested, verify that the file image
        // can be flushed. Pre-increment reference counts so the FCB/VCB stay alive
        // during the check; if the check fails, the finally block will undo them.

        if ((DesiredAccess & FILE_WRITE_DATA) || DeleteOnClose) {

            if (!VcbLocked) {
                UDFLockVcb(IrpContext, Fcb->Vcb);
            }

            if (Fcb->FcbReference == MAXULONG || Fcb->Vcb->VcbReference == MAXULONG) {
                Status = STATUS_INSUFFICIENT_RESOURCES;
            } else {
                Fcb->FcbReference++;
                Fcb->Vcb->VcbReference++;
                VcbAcquired = TRUE;
            }

            if (!VcbLocked) {
                UDFUnlockVcb(IrpContext, Fcb->Vcb);
            }

            if (Status != STATUS_SUCCESS) try_return(Status);

            if (!MmFlushImageSection(&Fcb->FcbNonpaged->SegmentObject, MmFlushForWrite)) {

                Status = DeleteOnClose ? STATUS_CANNOT_DELETE : STATUS_SHARING_VIOLATION;
                try_return(Status);
            }
        }

        // Check share access before updating

        if (Fcb->FcbCleanup != 0) {

            Status = IoCheckShareAccess(DesiredAccess,
                                        IrpSp->Parameters.Create.ShareAccess,
                                        IrpSp->FileObject,
                                        &Fcb->ShareAccess,
                                        FALSE);
            if (!NT_SUCCESS(Status)) {
                try_return(Status);
            }
        }

        // Update the share access and retain its exact accepted contribution.
        // Provider-specific ignored-share opens can contribute zero counters.
        ShareAccessBefore = Fcb->ShareAccess;
        if (!Fcb->FcbCleanup &&
            (ShareAccessBefore.OpenCount || ShareAccessBefore.Readers ||
             ShareAccessBefore.Writers || ShareAccessBefore.Deleters ||
             ShareAccessBefore.SharedRead || ShareAccessBefore.SharedWrite ||
             ShareAccessBefore.SharedDelete)) {
            try_return(Status = STATUS_FILE_CORRUPT_ERROR);
        }
        /* Main serializes this FCB's share state. IoUpdateShareAccess can
         * add one to each ULONG; reject before the provider mutates it. */
        if (ShareAccessBefore.OpenCount == MAXULONG ||
            ShareAccessBefore.Readers == MAXULONG || ShareAccessBefore.Writers == MAXULONG ||
            ShareAccessBefore.Deleters == MAXULONG || ShareAccessBefore.SharedRead == MAXULONG ||
            ShareAccessBefore.SharedWrite == MAXULONG || ShareAccessBefore.SharedDelete == MAXULONG)
            try_return(Status = STATUS_INSUFFICIENT_RESOURCES);
        if (Fcb->FcbCleanup == 0) {

            IoSetShareAccess(DesiredAccess,
                             IrpSp->Parameters.Create.ShareAccess,
                             IrpSp->FileObject,
                             &Fcb->ShareAccess);

        } else {

            IoUpdateShareAccess(IrpSp->FileObject, &Fcb->ShareAccess);
        }
        ShareAccessUpdated = TRUE;
        Ccb->OpenShareAccess.OpenCount = Fcb->ShareAccess.OpenCount - ShareAccessBefore.OpenCount;
        Ccb->OpenShareAccess.Readers = Fcb->ShareAccess.Readers - ShareAccessBefore.Readers;
        Ccb->OpenShareAccess.Writers = Fcb->ShareAccess.Writers - ShareAccessBefore.Writers;
        Ccb->OpenShareAccess.Deleters = Fcb->ShareAccess.Deleters - ShareAccessBefore.Deleters;
        Ccb->OpenShareAccess.SharedRead = Fcb->ShareAccess.SharedRead - ShareAccessBefore.SharedRead;
        Ccb->OpenShareAccess.SharedWrite = Fcb->ShareAccess.SharedWrite - ShareAccessBefore.SharedWrite;
        Ccb->OpenShareAccess.SharedDelete = Fcb->ShareAccess.SharedDelete - ShareAccessBefore.SharedDelete;

        // initialize the CCB
        Ccb->Fcb = Fcb;
        // initialize the CCB to point to the file object
        Ccb->FileObject = IrpSp->FileObject;

        // Set CCB flags from caller + DeleteOnClose from IrpSp
        Ccb->Flags = UserCcbFlags;
        if (DeleteOnClose) {
            Ccb->Flags |= UDF_CCB_DELETE_ON_CLOSE;
        }

        // A relative open (RelatedFileObject supplied) leaves FileObject->FileName
        // holding only the final path component. Record it so change notifications
        // build the absolute target name from the LCB chain instead of trusting
        // the relative FileName.

        if (IrpSp->FileObject->RelatedFileObject != NULL) {

            Ccb->Flags |= CCB_FLAG_HAS_RELATED_CCB;
        }

        // Acquire or create LCB to link parent directory FCB to this file FCB.
        // Skip for root directory (no parent).
        // LCB keeps parent FCB alive while children are open, preventing
        // premature teardown of in-memory directory index.
        // LCB is also inserted into splay trees for O(log n) name lookup.
        if (OpenFileInfo && OpenFileInfo->ParentFile &&
            OpenFileInfo->ParentFile->Fcb) {

            /* These scratch buffers remain owned by the outer finally,
             * including a raised pool allocation inside prefix creation.
             * UDFInsertPrefix copies them; an LCB never consumes them. */
            Status = UDFGetFileNameFromFileInfo(OpenFileInfo,
                                                (PUNICODE_STRING)&FileName);
            if (Status != STATUS_SUCCESS) try_return(Status);
            if (FileName.Buffer) {
                Status = UDFUpcaseString((PUNICODE_STRING)&CaseFileName,
                                         (PUNICODE_STRING)&FileName);
                if (Status != STATUS_SUCCESS) try_return(Status);
                if (!UDFCanNameBeA8dot3((PUNICODE_STRING)&FileName)) {
                    Status = UDFGenerateShortName(Vcb,
                        (PUNICODE_STRING)&FileName, (PUNICODE_STRING)&ShortName);
                    if (Status != STATUS_SUCCESS) try_return(Status);
                }
            }
            Status = UDFAcquirePrefix(IrpContext,
                OpenFileInfo->ParentFile->Fcb, Fcb, OpenFileInfo,
                FileName.Buffer ? (PUNICODE_STRING)&FileName : NULL,
                CaseFileName.Buffer ? (PUNICODE_STRING)&CaseFileName : NULL,
                ShortName.Buffer ? (PUNICODE_STRING)&ShortName : NULL,
                OpenFileInfo->Index, &Ccb->Lcb);
        }

        if (Status != STATUS_SUCCESS) try_return(Status);

        /* A named CCB needs the exact parent's LCB hold. Allocation failure
         * cannot be converted to an apparently successful unanchored open. */
        if (OpenFileInfo && OpenFileInfo->ParentFile && !Ccb->Lcb)
            try_return(Status = STATUS_INSUFFICIENT_RESOURCES);

        /* Reserve the exact FI/LCB request before reporting a successful
         * FILE_DELETE_ON_CLOSE. Cleanup must not need a new allocation or
         * borrow whichever FI happens to close last. */
        if (DeleteOnClose) {
            Status = UDFReserveDeleteOnClose(IrpContext, Ccb, OpenFileInfo, VcbLocked);
            if (!NT_SUCCESS(Status)) try_return(Status);
        }

        // Set the file object type.
        UDFSetFileObject(IrpSp->FileObject, TypeOfOpen, Fcb, Ccb);

        UDFLockFcb(IrpContext, Fcb);

        if (TypeOfOpen == UserFileOpen) {

            Fcb->Header.IsFastIoPossible = UDFIsFastIoPossible(Fcb);

            if (FlagOn(IrpSp->Parameters.Create.Options, FILE_SEQUENTIAL_ONLY) &&
               !FlagOn(Fcb->Vcb->CompatFlags, UDF_VCB_IC_IGNORE_SEQUENTIAL_IO)) {
                IrpSp->FileObject->Flags |= FO_SEQUENTIAL_ONLY;

                if (Fcb->Vcb->TargetDeviceObject->Characteristics & FILE_REMOVABLE_MEDIA) {
                    IrpSp->FileObject->Flags &= ~FO_WRITE_THROUGH;
                }

                if (Fcb->FileInfo) {
                    UDFSetFileAllocMode__(Fcb->FileInfo, EXTENT_FLAG_ALLOC_SEQUENTIAL);
                }
            }

            if (FlagOn(IrpSp->Parameters.Create.Options, FILE_NO_INTERMEDIATE_BUFFERING)) {
                IrpSp->FileObject->Flags |= FO_NO_INTERMEDIATE_BUFFERING;
            } else {
                if (Fcb->CachedOpenHandleCount == MAXULONG) {
                    Status = STATUS_INSUFFICIENT_RESOURCES;
                } else {
                    IrpSp->FileObject->Flags |= FO_CACHE_SUPPORTED;
                    InterlockedIncrement((PLONG)&Fcb->CachedOpenHandleCount);
                    CacheCountAdded = TRUE;
                }
            }

        } else if (TypeOfOpen == UserVolumeOpen) {

            Fcb->Header.IsFastIoPossible = FastIoIsNotPossible;
            IrpSp->FileObject->Flags |= FO_NO_INTERMEDIATE_BUFFERING;

        } else {

            Fcb->Header.IsFastIoPossible = FastIoIsNotPossible;
        }

        UDFUnlockFcb(IrpContext, Fcb);
        if (Status != STATUS_SUCCESS) try_return(Status);

        // Point to the section object pointer in the non-paged Fcb.
        IrpSp->FileObject->SectionObjectPointer = &Fcb->FcbNonpaged->SegmentObject;

        Fcb->FcbState &= ~UDF_FCB_DELAY_CLOSE;

        // Increment all reference counts here when CCB is successfully created.
        // If VcbLocked is TRUE, caller already holds VcbMutex - use simple ++.
        // If VcbLocked is FALSE, acquire VcbMutex for atomicity.
        //
        if (!VcbLocked) {
            UDFLockVcb(IrpContext, Fcb->Vcb);
        }

        /* Another FCB may have consumed the VCB budget since our early
         * pin. Admit the entire accepted-CCB group under the VCB mutex;
         * no callback/allocation separates these checks from publication. */
        if (Fcb->FcbCleanup == MAXULONG || Fcb->Vcb->VcbCleanup == MAXULONG ||
            Fcb->FcbUserReference == MAXULONG || Fcb->Vcb->VcbUserReference == MAXULONG ||
            (!VcbAcquired && (Fcb->FcbReference == MAXULONG ||
                             Fcb->Vcb->VcbReference == MAXULONG)) ||
            (Ccb->Lcb && Ccb->Lcb->Reference >= MAXLONG)) {
            Status = STATUS_INSUFFICIENT_RESOURCES;
        } else {
            Fcb->FcbCleanup++;
            Fcb->Vcb->VcbCleanup++;
            if (!VcbAcquired) {
                Fcb->FcbReference++;
                Fcb->Vcb->VcbReference++;
            }
            Fcb->FcbUserReference++;
            Fcb->Vcb->VcbUserReference++;
            if (Ccb->Lcb) InterlockedIncrement((PLONG)&Ccb->Lcb->Reference);
            Ccb->FileInfo = OpenFileInfo;

            /* Only this commit transfers the original FI and the exact
             * pin/share/cache receipts to the accepted CCB. */
            VcbAcquired = FALSE;
            DeleteCcbOnUnwind = FALSE;
            ShareAccessUpdated = FALSE;
            CacheCountAdded = FALSE;
        }

        if (!VcbLocked) {
            UDFUnlockVcb(IrpContext, Fcb->Vcb);
        }
        if (Status != STATUS_SUCCESS) try_return(Status);

        // For SL_OPEN_TARGET_DIRECTORY: save full path length in MaximumLength,
        // then trim Length to the parent directory path (up to last '\').
        // Rename/link handlers extract filename from the MaximumLength - Length gap.

        if (FlagOn(IrpSp->Flags, SL_OPEN_TARGET_DIRECTORY)) {

            PFILE_OBJECT FileObject = IrpSp->FileObject;

            FileObject->FileName.MaximumLength = FileObject->FileName.Length;

            // Root path "\" — nothing to trim
            if (FileObject->FileName.Length == sizeof(WCHAR) &&
                FileObject->FileName.Buffer[0] == L'\\') {
                // Length stays as root
            } else if (FileObject->FileName.Length >= sizeof(WCHAR)) {

                LONG i;
                USHORT CharCount = FileObject->FileName.Length / sizeof(WCHAR);

                // Scan backwards from second-to-last char to find last '\'
                for (i = (LONG)CharCount - 2; i >= 0; i--) {

                    if (FileObject->FileName.Buffer[i] == L'\\') {
                        break;
                    }
                }

                if (i < 0) {
                    // No backslash found
                    FileObject->FileName.Length = 0;
                } else if (i == 0) {
                    // Backslash at root position: keep "\"
                    FileObject->FileName.Length = sizeof(WCHAR);
                } else {
                    // Normal case: trim to position before backslash
                    FileObject->FileName.Length = (USHORT)(i * sizeof(WCHAR));
                }
            }
        }

        // Set the information field in the Irp to indicate the action taken.
        IrpContext->Irp->IoStatus.Information = Information;

try_exit:   NOTHING;
    } _SEH2_FINALLY {

        if (FileName.Buffer) MyFreePool__(FileName.Buffer);
        if (CaseFileName.Buffer) ExFreePool(CaseFileName.Buffer);
        if (ShortName.Buffer) ExFreePool(ShortName.Buffer);

        // Undo pre-incremented reference counts if we failed between
        // the early increment (MmFlushImageSection path) and the final
        // increment block where VcbAcquired is cleared.
        if (VcbAcquired) {

            if (!VcbLocked) {
                UDFLockVcb(IrpContext, Fcb->Vcb);
            }

            Fcb->FcbReference--;
            Fcb->Vcb->VcbReference--;

            if (!VcbLocked) {
                UDFUnlockVcb(IrpContext, Fcb->Vcb);
            }
        }

        if (DeleteCcbOnUnwind) {
            NTSTATUS RequestStatus = UDFDiscardDeleteOnClose(IrpContext, Ccb, VcbLocked);
            if (NT_SUCCESS(Status) && !NT_SUCCESS(RequestStatus)) Status = RequestStatus;
            if (ShareAccessUpdated)
                IoRemoveShareAccess(IrpSp->FileObject, &Fcb->ShareAccess);
            if (CacheCountAdded) {
                InterlockedDecrement((PLONG)&Fcb->CachedOpenHandleCount);
                ClearFlag(IrpSp->FileObject->Flags, FO_CACHE_SUPPORTED);
            }
            if (IrpSp->FileObject->FsContext == Fcb) {
                UDFSetFileObject(IrpSp->FileObject, UnopenedFileObject, NULL, NULL);
                IrpSp->FileObject->SectionObjectPointer = NULL;
            }
            Ccb->Lcb = NULL;
            UDFDeleteCcb(Ccb);
        }

    } _SEH2_END;

    return Status;
} // end UDFCompleteFcbOpen()

_When_(RelatedTypeOfOpen != UnopenedFileObject, _At_(RelatedCcb, _In_))
_When_(RelatedTypeOfOpen == UnopenedFileObject, _At_(RelatedCcb, _In_opt_))
_When_(RelatedTypeOfOpen != UnopenedFileObject, _At_(RelatedFileName, _In_))
_When_(RelatedTypeOfOpen == UnopenedFileObject, _At_(RelatedFileName, _In_opt_))
NTSTATUS
UDFNormalizeFileNames(
    _Inout_ PIRP_CONTEXT IrpContext,
    _In_ PVCB Vcb,
    _In_ BOOLEAN OpenByFileId,
    _In_ TYPE_OF_OPEN RelatedTypeOfOpen,
    PCCB RelatedCcb,
    PUNICODE_STRING RelatedFileName,
    _Inout_ PUNICODE_STRING FileName,
    _Inout_ PUNICODE_STRING RemainingName
    )

/*++

Routine Description:

    This routine is called to store the full name and upcased name into the
    filename buffer.  We only upcase the portion yet to parse.  We also
    check for a trailing backslash and lead-in double backslashes.  This
    routine also verifies the mode of the related open against the name
    currently in the filename.

Arguments:

    Vcb - Vcb for this volume.

    OpenByFileId - Indicates if the filename should be a 64 bit FileId.

    IgnoreCase - Indicates if this open is a case-insensitive operation.

    RelatedTypeOfOpen - Indicates the type of the related file object.

    RelatedCcb - Ccb for the related open.  Ignored if no relative open.

    RelatedFileName - FileName buffer for related open.  Ignored if no
        relative open.

    FileName - FileName to update in this routine.  The name should
        either be a 64-bit FileId or a Unicode string.

    RemainingName - Name with the remaining portion of the name.  This
        will begin after the related name and any separator.  For a
        non-relative open we also step over the initial separator.

Return Value:

    NTSTATUS - STATUS_SUCCESS if the names are OK, appropriate error code
        otherwise.

--*/

{
    ULONG RemainingNameLength = 0;
    ULONG RelatedNameLength = 0;
    ULONG SeparatorLength = 0;
    BOOLEAN HasColon = FALSE;

    ULONG BufferLength;

    UNICODE_STRING NewFileName;

    PAGED_CODE();

    // If this is the first pass then we need to build the full name and
    // check for name compatibility.

    if (!FlagOn(IrpContext->Flags, IRP_CONTEXT_FLAG_FULL_NAME)) {

        //  Deal with the regular file name case first.

        if (!OpenByFileId) {

            //  This is here because the Win32 layer can't avoid sending me double
            //  beginning backslashes.

            if ((FileName->Length > sizeof( WCHAR )) &&
                (FileName->Buffer[1] == L'\\') &&
                (FileName->Buffer[0] == L'\\')) {

                //
                //  If there are still two beginning backslashes, the name is bogus.
                //

                if ((FileName->Length > 2 * sizeof( WCHAR )) &&
                    (FileName->Buffer[2] == L'\\')) {

                    return STATUS_OBJECT_NAME_INVALID;
                }

                //
                //  Slide the name down in the buffer.
                //

                FileName->Length -= sizeof( WCHAR );

                RtlMoveMemory( FileName->Buffer,
                               FileName->Buffer + 1,
                               FileName->Length );
            }

            //
            //  Check for a trailing backslash.  Don't strip off if only character
            //  in the full name or for relative opens where this is illegal.
            //

            if (((FileName->Length > sizeof( WCHAR)) ||
                 ((FileName->Length == sizeof( WCHAR )) && (RelatedTypeOfOpen == UserDirectoryOpen))) &&
                (FileName->Buffer[ (FileName->Length/2) - 1 ] == L'\\')) {

                SetFlag(IrpContext->Flags, IRP_CONTEXT_FLAG_TRAIL_BACKSLASH);
                FileName->Length -= sizeof( WCHAR );
            }

            //
            //  Remember the length we need for this portion of the name.
            //

            RemainingNameLength = FileName->Length;

            //
            //  If this is a related file object then we verify the compatibility
            //  of the name in the file object with the relative file object.
            //

            if (RelatedTypeOfOpen != UnopenedFileObject) {

                //
                //  If the filename length was zero then it must be legal.
                //  If there are characters then check with the related
                //  type of open.
                //

                if (FileName->Length != 0) {

                    //
                    //  The name length must always be zero for a volume open.
                    //

                    if (RelatedTypeOfOpen <= UserVolumeOpen) {

                        return STATUS_INVALID_PARAMETER;

                    // The remaining name cannot begin with a backslash.

                    } else if (FileName->Buffer[0] == L'\\') {

                        return STATUS_INVALID_PARAMETER;

                        // If the related file is a user file then there
                        // is no file with this path.

                    } else if (RelatedTypeOfOpen == UserFileOpen &&
                               Vcb->UdfRevision < 0x200) {

                        return STATUS_OBJECT_PATH_NOT_FOUND;

                    } else if (FileName->Buffer[0] == L':') {

                        HasColon = TRUE;
                    }
                }

                // Remember the length of the related name when building
                // the full name.  We leave the RelatedNameLength and
                // SeparatorLength at zero if the relative file is opened
                // by Id.

                if (!FlagOn(RelatedCcb->Flags, CCB_FLAG_OPEN_BY_ID)) {

                    // Add a separator if the name length is non-zero
                    // unless the relative Fcb is at the root.

                    if ((FileName->Length != 0) &&
                        (RelatedCcb->Fcb != Vcb->RootIndexFcb)) {

                        if (!HasColon) {
                            SeparatorLength = sizeof(WCHAR);
                        }
                    }

                    RelatedNameLength = RelatedFileName->Length;
                }

            //  The full name is already in the filename.  It must either
            //  be length 0 or begin with a backslash.

            } else if (FileName->Length != 0) {

                if (FileName->Buffer[0] != L'\\') {

                    return STATUS_INVALID_PARAMETER;
                }

                //
                //  We will want to trim the leading backslash from the
                //  remaining name we return.
                //

                RemainingNameLength -= sizeof(WCHAR);
                SeparatorLength = sizeof(WCHAR);
            }

            //  Now see if the buffer is large enough to hold the full name.

            BufferLength = RelatedNameLength + SeparatorLength + RemainingNameLength;

            //  Check for an overflow of the maximum filename size.

            if (BufferLength > MAXUSHORT) {

                return STATUS_INVALID_PARAMETER;
            }

            //  Now see if we need to allocate a new buffer.

            if (FileName->MaximumLength < BufferLength) {

                NewFileName.Buffer = (PWCH)FsRtlAllocatePoolWithTag(PagedPool,
                                                                    BufferLength,
                                                                    TAG_FILE_NAME);

                NewFileName.MaximumLength = (USHORT) BufferLength;

            } else {

                NewFileName.Buffer = FileName->Buffer;
                NewFileName.MaximumLength = FileName->MaximumLength;
            }

            //  If there is a related name then we need to slide the remaining bytes up and
            //  insert the related name.  Otherwise the name is in the correct position
            //  already.

            if (RelatedNameLength != 0) {

                //
                //  Store the remaining name in its correct position.
                //

                if (RemainingNameLength != 0) {

                    RtlMoveMemory(Add2Ptr( NewFileName.Buffer, RelatedNameLength + SeparatorLength, PVOID),
                                  FileName->Buffer,
                                  RemainingNameLength);
                }

                RtlCopyMemory( NewFileName.Buffer,
                               RelatedFileName->Buffer,
                               RelatedNameLength );

                //
                //  Add the separator if needed.
                //

                if (SeparatorLength != 0) {

                    WCHAR separatorChar = (RelatedTypeOfOpen == UserDirectoryOpen) ? L'\\' : L':';
                    *(Add2Ptr(NewFileName.Buffer, RelatedNameLength, PWCHAR)) = separatorChar;
                }

                // Update the filename value we got from the user.

                if (NewFileName.Buffer != FileName->Buffer) {

                    if (FileName->Buffer != NULL) {

                        UDFFreePool((PVOID*)&FileName->Buffer);
                    }

                    FileName->Buffer = NewFileName.Buffer;
                    FileName->MaximumLength = NewFileName.MaximumLength;
                }

                //
                //  Copy the name length to the user's filename.
                //

                FileName->Length = (USHORT)(RelatedNameLength + SeparatorLength + RemainingNameLength);
            }

            // Now update the remaining name to parse.

            RemainingName->MaximumLength =
            RemainingName->Length = (USHORT)RemainingNameLength;

            RemainingName->Buffer = Add2Ptr(FileName->Buffer,
                                            RelatedNameLength + SeparatorLength,
                                            PWCHAR);

            //  For the open by file Id case we verify the name really contains
            //  a 64 bit value.

        } else {

            //
            //  Check for validity of the buffer.
            //

            if (FileName->Length != sizeof( FILE_ID )) {

                return STATUS_INVALID_PARAMETER;
            }
        }

        SetFlag(IrpContext->Flags, IRP_CONTEXT_FLAG_FULL_NAME);

        // If we are in the retry path then the full name is already in the
        // file object name.  If this is a case-sensitive operation then
        // we need to upcase the name from the end of any related file name already stored
        // there.

    } else {

        // Assume there is no relative name.

        *RemainingName = *FileName;

        // Nothing to do if the name length is zero.

        if (RemainingName->Length != 0) {

            //  If there is a relative name then we need to walk past it.

            if (RelatedTypeOfOpen != UnopenedFileObject) {

                // Nothing to walk past if the RelatedCcb is opened by FileId.

                if (!FlagOn( RelatedCcb->Flags, CCB_FLAG_OPEN_BY_ID )) {

                    //  Related file name is a proper prefix of the full name.
                    //  We step over the related name and if we are then
                    //  pointing at a separator character we step over that.

                    RemainingName->Buffer = Add2Ptr(RemainingName->Buffer,
                                                    RelatedFileName->Length,
                                                    PWCHAR);

                    RemainingName->Length -= RelatedFileName->Length;
                }
            }

            // If we are pointing at a separator character then step past that.

            if (RemainingName->Length != 0) {

                if (*(RemainingName->Buffer) == L'\\') {

                    RemainingName->Buffer = Add2Ptr(RemainingName->Buffer,
                                                    sizeof(WCHAR),
                                                    PWCHAR);

                    RemainingName->Length -= sizeof(WCHAR);
                }
            }
        }
    }

#pragma prefast(push)
#pragma prefast(suppress:26030, "RemainingName->FileName.Buffer = FileName.Buffer + (RelatedNameLength + SeparatorLength); FileName.MaximumLength < (RelatedNameLength + SeparatorLength + RemainingNameLength).")
    return STATUS_SUCCESS;
#pragma prefast(pop)
}
