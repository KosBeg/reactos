////////////////////////////////////////////////////////////////////
// Copyright (C) Alexander Telyatnikov, Ivan Keliukh, Yegor Anchishkin, SKIF Software, 1999-2013. Kiev, Ukraine
// All rights reserved
// This file was released under the GPLv2 on June 2015.
////////////////////////////////////////////////////////////////////
/*

 Module name: Cleanup.cpp

 Abstract:

    Contains code to handle the "Cleanup" dispatch entry point.

 Environment:

    Kernel mode only
*/

#include            "udffs.h"

// define the file specific bug-check id
#define         UDF_BUG_CHECK_ID                UDF_FILE_CLEANUP

VOID
UDFAutoUnlock (
    IN PVCB Vcb
    );

/* The FCB resource serializes these records. VcbMutex is used only for
 * the extra LCB pin; no pool allocation, FI acquisition or I/O under it.
 * A request's FI reference is distinct from every successful CCB hold. */
static BOOLEAN
UDFDeleteRequestMatches(PUDF_DELETE_REQUEST Request, PUDF_FILE_INFO FileInfo)
{
    return FileInfo && FileInfo->Dloc && FileInfo->ParentFile &&
           Request->FileInfo && Request->FileInfo->Dloc == FileInfo->Dloc &&
           Request->ParentDloc == FileInfo->ParentFile->Dloc &&
           Request->Index == FileInfo->Index;
}

static VOID
UDFRefreshDeletePending(PFCB Fcb)
{
    PLIST_ENTRY Link;
    Fcb->FcbState &= ~UDF_FCB_DELETE_ON_CLOSE;
    for (Link = Fcb->PendingDeleteQueue.Flink;
         Link != &Fcb->PendingDeleteQueue; Link = Link->Flink) {
        PUDF_DELETE_REQUEST Request = CONTAINING_RECORD(Link, UDF_DELETE_REQUEST, FcbLinks);
        if (!Request->CancelOnly) {
            Fcb->FcbState |= UDF_FCB_DELETE_ON_CLOSE;
            break;
        }
    }
}

BOOLEAN
UDFIsLinkDeletePending(PFCB Fcb, PUDF_FILE_INFO FileInfo)
{
    PLIST_ENTRY Link;
    for (Link = Fcb->PendingDeleteQueue.Flink;
         Link != &Fcb->PendingDeleteQueue; Link = Link->Flink) {
        PUDF_DELETE_REQUEST Request = CONTAINING_RECORD(Link,
            UDF_DELETE_REQUEST, FcbLinks);
        if (!Request->CancelOnly && UDFDeleteRequestMatches(Request, FileInfo))
            return TRUE;
    }
    return FALSE;
}

static VOID
UDFRetainDeleteRequest(PUDF_DELETE_REQUEST Request, NTSTATUS Status)
{
    if (!Request->Active) {
        InsertTailList(&Request->Fcb->PendingDeleteQueue, &Request->FcbLinks);
        Request->Active = TRUE;
        InterlockedIncrement(&Request->Fcb->Vcb->PendingDeleteCount);
    }
    if (!Request->Retained) {
        Request->Retained = TRUE;
        InterlockedIncrement(&Request->Fcb->Vcb->RetainedDeleteCount);
    }
    if (NT_SUCCESS(Request->FirstError))
        Request->FirstError = Status;
    Request->LastStatus = Status;
    UDFRefreshDeletePending(Request->Fcb);
}

/* No writeback here: exact owner consumption must not re-enter metadata
 * I/O after an earlier failure or repeat a handle reference decrement. */
static NTSTATUS
UDFReleaseDeleteRequest(PIRP_CONTEXT IrpContext, PUDF_DELETE_REQUEST Request,
                        BOOLEAN VcbLocked)
{
    PVCB Vcb = Request->Fcb->Vcb;
    NTSTATUS Status;
    /* Private cancelled reservations need a durable owner before a raised
     * release can bypass their caller's pointer handoff. Queue membership
     * never promotes this record to a namespace deletion intention. */
    if (Request->CancelOnly && !Request->Active) {
        InsertTailList(&Request->Fcb->PendingDeleteQueue, &Request->FcbLinks);
        Request->Active = TRUE;
        InterlockedIncrement(&Vcb->PendingDeleteCount);
        UDFRefreshDeletePending(Request->Fcb);
    }
    if (Request->FileReferenceOwned) {
        if (Request->FileReleaseStarted) {
            UDFRetainDeleteRequest(Request, STATUS_FILE_CORRUPT_ERROR);
            Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
            return STATUS_FILE_CORRUPT_ERROR; /* uncertain decrement, never replay */
        }
        Request->FileReleaseStarted = TRUE;
        _SEH2_TRY {
            Status = UDFDereferenceFileChecked(Vcb, Request->FileInfo);
            /* A returned refusal did not consume the checked reference. */
            if (NT_SUCCESS(Status)) Request->FileReferenceOwned = FALSE;
            Request->FileReleaseStarted = FALSE;
        } _SEH2_FINALLY {
            if (_SEH2_AbnormalTermination()) {
                UDFRetainDeleteRequest(Request, STATUS_FILE_CORRUPT_ERROR);
                Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
            }
        } _SEH2_END;
        if (!NT_SUCCESS(Status)) {
            UDFRetainDeleteRequest(Request, Status);
            Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
            return Status;
        }
    }
    if (Request->LcbReferenceOwned) {
        if (!VcbLocked) { UDFLockVcb(IrpContext, Vcb); }
        if (!Request->Lcb->Reference) {
            if (!VcbLocked) { UDFUnlockVcb(IrpContext, Vcb); }
            UDFRetainDeleteRequest(Request, STATUS_FILE_CORRUPT_ERROR);
            Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
            return STATUS_FILE_CORRUPT_ERROR;
        }
        InterlockedDecrement((PLONG)&Request->Lcb->Reference);
        Request->LcbReferenceOwned = FALSE;
        if (!VcbLocked) { UDFUnlockVcb(IrpContext, Vcb); }
    }
    if (Request->Active) {
        RemoveEntryList(&Request->FcbLinks);
        NT_ASSERT(Vcb->PendingDeleteCount > 0);
        InterlockedDecrement(&Vcb->PendingDeleteCount);
    }
    if (Request->Retained) {
        NT_ASSERT(Vcb->RetainedDeleteCount > 0);
        InterlockedDecrement(&Vcb->RetainedDeleteCount);
    }
    UDFRefreshDeletePending(Request->Fcb);
    MyFreePool__(Request);
    return STATUS_SUCCESS;
}

static NTSTATUS
UDFAllocateDeleteRequest(PIRP_CONTEXT IrpContext, PFCB Fcb,
    PUDF_FILE_INFO FileInfo, PLCB Lcb, BOOLEAN VcbLocked,
    PUDF_DELETE_REQUEST* Result)
{
    PUDF_DELETE_REQUEST Request;
    NTSTATUS Status;
    *Result = NULL;
    if (!FileInfo || !FileInfo->Dloc || !FileInfo->ParentFile ||
        !FileInfo->ParentFile->Dloc || !FileInfo->ParentFile->Fcb ||
        !Fcb->FileInfo || Fcb->FileInfo->Dloc != FileInfo->Dloc ||
        FileInfo->Dloc->DeleteCommitted ||
        ((FileInfo->Index < 2) && !UDFIsAStreamDir(FileInfo)))
        return STATUS_CANNOT_DELETE;
    if (Lcb && (Lcb->ChildFcb != Fcb || !Lcb->LinkFileInfo ||
        Lcb->LinkFileInfo != FileInfo ||
        Lcb->ParentFcb != FileInfo->ParentFile->Fcb ||
        FlagOn(Lcb->Flags, UDF_LCB_FLAG_LINK_DELETED)))
        return STATUS_FILE_INVALID;
    Request = MyAllocatePool__(NonPagedPool, sizeof(*Request));
    if (!Request)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Request, sizeof(*Request));
    Request->Fcb = Fcb;
    Request->FileInfo = FileInfo;
    Request->ParentDloc = FileInfo->ParentFile->Dloc;
    Request->Index = FileInfo->Index;
    Request->Lcb = Lcb;
    Request->LastStatus = STATUS_SUCCESS;
    Request->CancelOnly = TRUE; /* private reservation until activation commits */
    Status = UDFReferenceFileChecked(Fcb->Vcb, FileInfo);
    if (!NT_SUCCESS(Status)) {
        MyFreePool__(Request);
        return Status;
    }
    Request->FileReferenceOwned = TRUE;
    if (Lcb) {
        if (!VcbLocked) { UDFLockVcb(IrpContext, Fcb->Vcb); }
        if (Lcb->Reference >= MAXLONG) {
            if (!VcbLocked) { UDFUnlockVcb(IrpContext, Fcb->Vcb); }
            Status = UDFReleaseDeleteRequest(IrpContext, Request, VcbLocked);
            return NT_SUCCESS(Status) ? STATUS_INSUFFICIENT_RESOURCES : Status;
        }
        InterlockedIncrement((PLONG)&Lcb->Reference);
        Request->LcbReferenceOwned = TRUE;
        if (!VcbLocked) { UDFUnlockVcb(IrpContext, Fcb->Vcb); }
    }
    *Result = Request;
    return STATUS_SUCCESS;
}

NTSTATUS
UDFReserveDeleteOnClose(PIRP_CONTEXT IrpContext, PCCB Ccb,
                        PUDF_FILE_INFO FileInfo, BOOLEAN VcbLocked)
{
    if (!Ccb || Ccb->DeleteOnCloseRequest || !Ccb->Lcb)
        return STATUS_FILE_INVALID;
    /* FI acquisition takes DlocResource2; callers must not hold a fast
     * mutex across it. Current create callers all pass VcbLocked=FALSE. */
    if (VcbLocked)
        return STATUS_DEVICE_BUSY;
    return UDFAllocateDeleteRequest(IrpContext, Ccb->Fcb, FileInfo,
        Ccb->Lcb, VcbLocked, &Ccb->DeleteOnCloseRequest);
}

NTSTATUS
UDFDiscardDeleteOnClose(PIRP_CONTEXT IrpContext, PCCB Ccb,
                        BOOLEAN VcbLocked)
{
    PUDF_DELETE_REQUEST Request = Ccb->DeleteOnCloseRequest;
    if (Request) Request->CancelOnly = TRUE; /* before any failed reference release */
    Ccb->DeleteOnCloseRequest = NULL;
    return Request ? UDFReleaseDeleteRequest(IrpContext, Request, VcbLocked) :
                     STATUS_SUCCESS;
}

NTSTATUS
UDFActivateDeleteOnClose(PIRP_CONTEXT IrpContext, PCCB Ccb)
{
    PLIST_ENTRY Link;
    PUDF_DELETE_REQUEST Request = Ccb->DeleteOnCloseRequest;
    Ccb->DeleteOnCloseRequest = NULL;
    if (!Request)
        return STATUS_FILE_INVALID;
    for (Link = Ccb->Fcb->PendingDeleteQueue.Flink;
         Link != &Ccb->Fcb->PendingDeleteQueue; Link = Link->Flink) {
        PUDF_DELETE_REQUEST Existing = CONTAINING_RECORD(Link,
            UDF_DELETE_REQUEST, FcbLinks);
        if (!Existing->CancelOnly && UDFDeleteRequestMatches(Existing, Request->FileInfo)) {
            Request->CancelOnly = TRUE;
            return UDFReleaseDeleteRequest(IrpContext, Request, FALSE);
        }
    }
    Request->CancelOnly = FALSE; /* namespace intent starts only at activation */
    InsertTailList(&Ccb->Fcb->PendingDeleteQueue, &Request->FcbLinks);
    Request->Active = TRUE;
    InterlockedIncrement(&Ccb->Fcb->Vcb->PendingDeleteCount);
    UDFRefreshDeletePending(Ccb->Fcb);
    return STATUS_SUCCESS;
}

NTSTATUS
UDFQueueLinkDelete(PIRP_CONTEXT IrpContext, PFCB Fcb,
                   PUDF_FILE_INFO FileInfo, PLCB Lcb)
{
    PLIST_ENTRY Link;
    PUDF_DELETE_REQUEST Request;
    NTSTATUS Status;
    for (Link = Fcb->PendingDeleteQueue.Flink;
         Link != &Fcb->PendingDeleteQueue; Link = Link->Flink) {
        Request = CONTAINING_RECORD(Link, UDF_DELETE_REQUEST, FcbLinks);
        if (!Request->CancelOnly && UDFDeleteRequestMatches(Request, FileInfo))
            return Request->Retained ? Request->LastStatus : STATUS_SUCCESS;
    }
    if (FileInfo && FileInfo->LinkDeleteCommitted)
        return STATUS_SUCCESS; /* this exact namespace was already confirmed absent */
    Status = UDFAllocateDeleteRequest(IrpContext, Fcb, FileInfo, Lcb,
                                     FALSE, &Request);
    if (!NT_SUCCESS(Status))
        return Status;
    Request->CancelOnly = FALSE;
    InsertTailList(&Fcb->PendingDeleteQueue, &Request->FcbLinks);
    Request->Active = TRUE;
    InterlockedIncrement(&Fcb->Vcb->PendingDeleteCount);
    UDFRefreshDeletePending(Fcb);
    return STATUS_SUCCESS;
}

NTSTATUS
UDFCancelLinkDelete(PIRP_CONTEXT IrpContext, PFCB Fcb,
                    PUDF_FILE_INFO FileInfo)
{
    PLIST_ENTRY Link;
    for (Link = Fcb->PendingDeleteQueue.Flink;
         Link != &Fcb->PendingDeleteQueue; Link = Link->Flink) {
        PUDF_DELETE_REQUEST Request = CONTAINING_RECORD(Link,
            UDF_DELETE_REQUEST, FcbLinks);
        if (!Request->CancelOnly && UDFDeleteRequestMatches(Request, FileInfo)) {
            /* Once a write was submitted, cancellation cannot imply that
             * the old name is still on disk or silently discard its owner. */
            if (Request->NamespaceTouched || Request->NamespaceCommitted)
                return STATUS_CANNOT_DELETE;
            Request->CancelOnly = TRUE;
            return UDFReleaseDeleteRequest(IrpContext, Request, FALSE);
        }
    }
    return STATUS_SUCCESS;
}

/* Exact requested FI and all same-link LCB aliases, never a different
 * hard link's name. Parent resource is held before altering its trees. */
static VOID
UDFMarkDeletedLinkAliases(PUDF_DELETE_REQUEST Request)
{
    PLIST_ENTRY Link;
    for (Link = Request->Fcb->ParentLcbQueue.Flink;
         Link != &Request->Fcb->ParentLcbQueue; Link = Link->Flink) {
        PLCB Lcb = CONTAINING_RECORD(Link, struct LCB, ChildFcbLinks);
        if (UDFDeleteRequestMatches(Request, Lcb->LinkFileInfo)) {
            Lcb->Flags |= UDF_LCB_FLAG_LINK_DELETED;
            UdfRemoveNameLinks(Lcb->ParentFcb, Lcb);
        }
    }
}

/* Called only after the core unlink's namespace barrier, with this FCB
 * exclusive. Direct stream-tree deletion and an owned cleanup may converge
 * on the same FI generation; remember the already-confirmed commit rather
 * than issuing another unlink on the explicit retry path. */
VOID
UDFNoteCommittedUnlink(PVCB Vcb, PFCB Fcb, PUDF_FILE_INFO FileInfo)
{
    PLIST_ENTRY Link;
    PUDF_FILE_INFO Peer;
    if (Fcb) { ASSERT_EXCLUSIVE_FCB(Fcb); }
    /* Keep a commit fact for a preallocated but not-yet-active option owner
     * too. A replacement may remove its old link while another hard link
     * survives, so Dloc.DeleteCommitted alone is insufficient evidence. */
    UDFAcquireResourceExclusive(&Vcb->DlocResource2, TRUE);
    Peer = FileInfo;
    do {
        if (Peer->ParentFile && FileInfo->ParentFile &&
            Peer->ParentFile->Dloc == FileInfo->ParentFile->Dloc &&
            Peer->Index == FileInfo->Index)
            Peer->LinkDeleteCommitted = TRUE;
        Peer = Peer->NextLinkedFile;
    } while (Peer && Peer != FileInfo);
    UDFReleaseResource(&Vcb->DlocResource2);
    if (!Fcb)
        return;
    for (Link = Fcb->PendingDeleteQueue.Flink;
         Link != &Fcb->PendingDeleteQueue; Link = Link->Flink) {
        PUDF_DELETE_REQUEST Request = CONTAINING_RECORD(Link,
            UDF_DELETE_REQUEST, FcbLinks);
        if (!Request->CancelOnly && UDFDeleteRequestMatches(Request, FileInfo)) {
            Request->NamespaceCommitted = TRUE;
            Request->NamespaceTouched = TRUE;
        }
    }
    /* The confirming caller holds this name's parent FCB exclusive (or
     * the whole VCB exclusive). Retire every parallel LCB for this link,
     * retaining the structures themselves while CCB/request pins exist. */
    for (Link = Fcb->ParentLcbQueue.Flink;
         Link != &Fcb->ParentLcbQueue; Link = Link->Flink) {
        PLCB Lcb = CONTAINING_RECORD(Link, struct LCB, ChildFcbLinks);
        Peer = Lcb->LinkFileInfo;
        if (Peer && Peer->Dloc == FileInfo->Dloc && Peer->ParentFile &&
            FileInfo->ParentFile && Peer->ParentFile->Dloc == FileInfo->ParentFile->Dloc &&
            Peer->Index == FileInfo->Index) {
            Lcb->Flags |= UDF_LCB_FLAG_LINK_DELETED;
            UdfRemoveNameLinks(Lcb->ParentFcb, Lcb);
        }
    }
    if (!UDFGetFileNameLinkCount(FileInfo)) {
        Fcb->FcbState |= UDF_FCB_DELETED;
        Fcb->NtReqFCBFlags |= UDF_NTREQ_FCB_DELETED;
    }

}

/* Caller holds VCB (at least shared) and the target FCB exclusive. Preserve
 * the existing last-FCB-cleanup scheduling, but process every requested
 * link, with its own hold, rather than whichever FI the last CCB used.
 * One attempt per record per call; uncertain generations are never retried
 * as fresh unlink operations. A later explicit volume flush can retry only
 * a failure that occurred before namespace mutation. */
NTSTATUS
UDFProcessPendingDeletes(PIRP_CONTEXT IrpContext, PFCB Fcb, BOOLEAN ExplicitRetry)
{
    PVCB Vcb = Fcb->Vcb;
    PLIST_ENTRY Link;
    NTSTATUS FirstStatus = STATUS_SUCCESS, Status;
    ASSERT_EXCLUSIVE_FCB(Fcb);
    if (Fcb->ProcessingDeletes)
        return STATUS_SUCCESS;
    if (!Fcb->FileInfo || !Fcb->FileInfo->Dloc) {
        Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
        return STATUS_FILE_CORRUPT_ERROR;
    }
    Fcb->ProcessingDeletes = TRUE;
    _SEH2_TRY {
        Link = Fcb->PendingDeleteQueue.Flink;
        while (Link != &Fcb->PendingDeleteQueue) {
            PUDF_DELETE_REQUEST Request = CONTAINING_RECORD(Link,
                UDF_DELETE_REQUEST, FcbLinks);
            PUDF_FILE_INFO FileInfo = Request->FileInfo;
            PFCB ParentFcb = NULL;
            BOOLEAN ParentAcquired = FALSE;
            Link = Link->Flink;
            if (!Request->CancelOnly && Fcb->FcbCleanup)
                continue; /* only actual unlink waits for last cleanup */
            Status = STATUS_SUCCESS;
            _SEH2_TRY {
                if (Request->CancelOnly) {
                    /* A failed cancellation is reference inventory, not
                     * permission for an explicit retry to remove the name.
                     * FileReferenceOwned may already be false while an
                     * exact LCB hold still needs to be consumed. */
                    if (Request->Retained && !ExplicitRetry)
                        Status = NT_SUCCESS(Request->LastStatus) ?
                            STATUS_FILE_CORRUPT_ERROR : Request->LastStatus;
                    _SEH2_LEAVE;
                }
                if (!Request->FileReferenceOwned ||
                    !UDFDeleteRequestMatches(Request, FileInfo) ||
                    !FileInfo->ParentFile->Fcb ||
                    FileInfo->Dloc != Fcb->FileInfo->Dloc) {
                    Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
                    Status = STATUS_FILE_CORRUPT_ERROR;
                    _SEH2_LEAVE;
                }
                ParentFcb = FileInfo->ParentFile->Fcb;
                if (ParentFcb == Fcb) {
                    Status = STATUS_FILE_CORRUPT_ERROR;
                    _SEH2_LEAVE;
                }
                if (!UDFAcquireFcbExclusive(IrpContext, ParentFcb, TRUE)) {
                    Status = STATUS_DEVICE_BUSY;
                    _SEH2_LEAVE;
                }
                ParentAcquired = TRUE;
                if (FileInfo->LinkDeleteCommitted || FileInfo->Dloc->DeleteCommitted) {
                    Request->NamespaceCommitted = TRUE;
                    Request->NamespaceTouched = TRUE;
                }
                if (Request->Retained && !ExplicitRetry && !Request->NamespaceCommitted) {
                    Status = Request->LastStatus;
                    _SEH2_LEAVE; /* a CLOSE does not silently retry failed metadata I/O */
                }
                if (Request->NamespaceTouched && !Request->NamespaceCommitted) {
                    Status = NT_SUCCESS(Request->LastStatus) ?
                        STATUS_FILE_CORRUPT_ERROR : Request->LastStatus;
                    _SEH2_LEAVE;
                }
                if (!Request->NamespaceCommitted) {
                    if (Vcb->VcbState & VCB_STATE_VOLUME_READ_ONLY) {
                        Status = STATUS_MEDIA_WRITE_PROTECTED;
                        _SEH2_LEAVE;
                    }
                    if (UDFIsADirectory(FileInfo) && !UDFIsDirEmpty__(FileInfo)) {
                        Status = STATUS_DIRECTORY_NOT_EMPTY;
                        _SEH2_LEAVE;
                    }
                    Status = UDFFlushFile__(IrpContext, Vcb, FileInfo, 0);
                    if (!NT_SUCCESS(Status)) _SEH2_LEAVE;
                    Status = UDFUnlinkFileEx__(IrpContext, Vcb, FileInfo, TRUE,
                                               &Request->NamespaceTouched);
                    if (!NT_SUCCESS(Status)) _SEH2_LEAVE;
                    Request->NamespaceCommitted = TRUE;
                }
                UDFMarkDeletedLinkAliases(Request);
                if (!Request->RemovalReported && Request->Lcb) {
                    Request->RemovalReported = TRUE;
                    UDFNotifyReportChange(IrpContext, Vcb, Fcb,
                        UDFIsAStream(FileInfo) ? FILE_NOTIFY_CHANGE_STREAM_NAME :
                        (UDFIsADirectory(FileInfo) ? FILE_NOTIFY_CHANGE_DIR_NAME :
                                                    FILE_NOTIFY_CHANGE_FILE_NAME),
                        UDFIsAStream(FileInfo) ? FILE_ACTION_REMOVED_STREAM :
                                                FILE_ACTION_REMOVED,
                        Request->Lcb, NULL);
                }
            } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
                Status = _SEH2_GetExceptionCode();
                if (Request->NamespaceTouched)
                    Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
            } _SEH2_END;
            if (ParentAcquired)
                UDFReleaseFcb(IrpContext, ParentFcb);
            if (NT_SUCCESS(Status))
                Status = UDFReleaseDeleteRequest(IrpContext, Request, FALSE);
            else {
                UDFRetainDeleteRequest(Request, Status);
                if (Request->NamespaceTouched)
                    Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
            }
            if (NT_SUCCESS(FirstStatus) && !NT_SUCCESS(Status))
                FirstStatus = Status;
            if (!NT_SUCCESS(Status))
                break; /* no further metadata I/O after the first failure */
        }
    } _SEH2_FINALLY {
        Fcb->ProcessingDeletes = FALSE;
        UDFRefreshDeletePending(Fcb);
    } _SEH2_END;
    return FirstStatus;
}

/* Cleanup cannot truncate a still-mapped deleted stream. Once all handles
 * are cleaned, an explicit volume flush may purge its cache under Main and
 * a snapshot reference. MM's final FILE_OBJECT close can then retire the
 * committed tombstone and release its allocations. Never discard a live or
 * incompletely unlinked generation merely to make free space increase. */
static NTSTATUS
UDFPurgeDeletedFileCache(PIRP_CONTEXT IrpContext, PFCB Fcb)
{
    PUDF_FILE_INFO FileInfo = Fcb->FileInfo;
    PSECTION_OBJECT_POINTERS Sections = &Fcb->FcbNonpaged->SegmentObject;
    ASSERT_EXCLUSIVE_FCB(Fcb);
    if (!(Fcb->FcbState & UDF_FCB_DELETED) || Fcb->FcbCleanup)
        return STATUS_SUCCESS;
    if (!FileInfo || !FileInfo->Dloc || !FileInfo->Dloc->DeleteCommitted ||
        FileInfo->Dloc->CommonFcb != Fcb || UDFGetFileNameLinkCount(FileInfo))
        return STATUS_FILE_CORRUPT_ERROR;
    if (!MmCanFileBeTruncated(Sections, &UdfData.UDFLargeZero))
        return STATUS_DEVICE_BUSY;
    if (Sections->ImageSectionObject &&
        !MmFlushImageSection(Sections, MmFlushForDelete))
        return STATUS_DEVICE_BUSY;
    if ((Sections->DataSectionObject || Sections->SharedCacheMap) &&
        !CcPurgeCacheSection(Sections, NULL, 0, FALSE))
        return STATUS_DEVICE_BUSY;
    if (Sections->DataSectionObject || Sections->SharedCacheMap ||
        Sections->ImageSectionObject) {
        /* Purging pages does not complete an asynchronous cache-map
         * uninitialization. Only this proved deleted, unmapped generation
         * admits a lazy-close drain after releasing every FCB/VCB lock.
         * A purge refusal above or an actual write error gives no receipt. */
        if (Sections->SharedCacheMap && !Sections->ImageSectionObject)
            IrpContext->DeletedCacheClosePending = TRUE;
        return STATUS_DEVICE_BUSY;
    }
    return STATUS_SUCCESS;
}

/* One explicit volume-flush retry pass over an exact pinned snapshot.
 * Do not retain a generic-table restart pointer across I/O/removal. Closed
 * records are not necessarily reachable through the live directory tree. */
NTSTATUS
UDFRetryVolumeDeletes(PIRP_CONTEXT IrpContext, PVCB Vcb)
{
    PFCB* Files;
    PFCB Fcb;
    PVOID Restart = NULL;
    ULONG Capacity, Count = 0, Index;
    BOOLEAN TableLocked = FALSE, VcbLocked = FALSE;
    BOOLEAN DeletedClosePending = FALSE;
    NTSTATUS Status = STATUS_SUCCESS;
    ASSERT_EXCLUSIVE_VCB(Vcb);
    /* A successfully unlinked stream may still have MM's last reference,
     * with neither a pending delete nor a zero-reference teardown retry. */
    UDFLockFcbTable(IrpContext, Vcb);
    Capacity = RtlNumberGenericTableElements(&Vcb->FcbTable);
    UDFUnlockFcbTable(IrpContext, Vcb);
    if (!Capacity)
        return (Vcb->PendingDeleteCount || Vcb->ClosedFcbRetryCount) ?
               STATUS_FILE_CORRUPT_ERROR : STATUS_SUCCESS;
    if (Capacity > MAXULONG / sizeof(PFCB))
        return STATUS_FILE_CORRUPT_ERROR;
    Files = MyAllocatePool__(NonPagedPool, Capacity * sizeof(PFCB));
    if (!Files)
        return STATUS_INSUFFICIENT_RESOURCES;
    _SEH2_TRY {
        UDFLockFcbTable(IrpContext, Vcb);
        TableLocked = TRUE;
        UDFLockVcb(IrpContext, Vcb);
        VcbLocked = TRUE;
        while (Count < Capacity && (Fcb = UDFGetNextFcb(IrpContext, Vcb, &Restart))) {
            if (Fcb->TeardownInProgress || !Fcb->Header.Resource ||
                (IsListEmpty(&Fcb->PendingDeleteQueue) &&
                 !Fcb->TeardownRetryPending &&
                 !(Fcb->FcbState & UDF_FCB_DELETED)))
                continue;
            if (Fcb->FcbReference == MAXULONG || Vcb->VcbReference == MAXULONG) {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }
            Fcb->FcbReference++;
            Vcb->VcbReference++;
            Files[Count++] = Fcb;
        }
        UDFUnlockVcb(IrpContext, Vcb);
        VcbLocked = FALSE;
        UDFUnlockFcbTable(IrpContext, Vcb);
        TableLocked = FALSE;
        for (Index = 0; NT_SUCCESS(Status) && Index < Count; Index++) {
            Fcb = Files[Index];
            if (!UDFAcquireFcbExclusive(IrpContext, Fcb, TRUE)) {
                Status = STATUS_DEVICE_BUSY;
                break;
            }
            _SEH2_TRY {
                Status = UDFProcessPendingDeletes(IrpContext, Fcb, TRUE);
                if (NT_SUCCESS(Status))
                    Status = UDFPurgeDeletedFileCache(IrpContext, Fcb);
            } _SEH2_FINALLY {
                UDFReleaseFcb(IrpContext, Fcb);
            } _SEH2_END;
            if (!NT_SUCCESS(Status))
                break;
        }
        /* Purge may deliver MM's last close through the FSP queue. Our
         * snapshot pins keep every candidate alive while those original
         * close references are consumed exactly once. No FCB is held here. */
        if (NT_SUCCESS(Status))
            UDFFspClose(Vcb);
    } _SEH2_FINALLY {
        if (VcbLocked) { UDFUnlockVcb(IrpContext, Vcb); }
        if (TableLocked) { UDFUnlockFcbTable(IrpContext, Vcb); }
        /* Release only our snapshot pins. On a successful explicit retry,
         * retire now-closed FCBs while their resources can be acquired. A
         * failed operation does no additional metadata I/O in this unwind. */
        for (Index = 0; Index < Count; Index++) {
            BOOLEAN Acquired = FALSE, Removed = FALSE;
            Fcb = Files[Index];
            if (!_SEH2_AbnormalTermination() && NT_SUCCESS(Status))
                Acquired = UDFAcquireFcbExclusive(IrpContext, Fcb, TRUE);
            UDFLockVcb(IrpContext, Vcb);
            NT_ASSERT(Fcb->FcbReference > 0 && Vcb->VcbReference > 0);
            Fcb->FcbReference--;
            Vcb->VcbReference--;
            UDFUnlockVcb(IrpContext, Vcb);
            if (Acquired) {
                BOOLEAN WasWriter = BooleanFlagOn(IrpContext->Flags,
                                           IRP_CONTEXT_FLAG_METADATA_WRITER);
                NTSTATUS TeardownStatus = STATUS_SUCCESS;
                SetFlag(IrpContext->Flags, IRP_CONTEXT_FLAG_METADATA_WRITER);
                _SEH2_TRY {
                    _SEH2_TRY {
                        /* Releasing this child's LCB can close a parent
                         * whose snapshot slot was already passed. Use the
                         * existing checked parent plan to retire it too. */
                        if (!Fcb->FcbReference &&
                            IsListEmpty(&Fcb->PendingDeleteQueue))
                            TeardownStatus = UDFTeardownStructuresEx(
                                IrpContext, Fcb, FALSE, TRUE, &Removed);
                    } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
                        TeardownStatus = _SEH2_GetExceptionCode();
                    } _SEH2_END;
                } _SEH2_FINALLY {
                    if (!WasWriter)
                        ClearFlag(IrpContext->Flags, IRP_CONTEXT_FLAG_METADATA_WRITER);
                    if (!Removed) UDFReleaseFcb(IrpContext, Fcb);
                } _SEH2_END;
                if (NT_SUCCESS(TeardownStatus))
                    TeardownStatus = UDFTakeMetadataWritebackError(Vcb);
                if (NT_SUCCESS(Status) && !NT_SUCCESS(TeardownStatus))
                    Status = TeardownStatus;
            }
        }
        MyFreePool__(Files);
    } _SEH2_END;
    if (NT_SUCCESS(Status)) {
        /* Parent/close retirement later in the pass can consume an owner
         * observed before its snapshot pin was dropped. Never dereference
         * that old snapshot now: accepted teardown may have freed it. Check
         * the current indexed owners under the same lifetime domain. */
        DeletedClosePending = FALSE;
        Restart = NULL;
        _SEH2_TRY {
            UDFLockFcbTable(IrpContext, Vcb);
            TableLocked = TRUE;
            UDFLockVcb(IrpContext, Vcb);
            VcbLocked = TRUE;
            while ((Fcb = UDFGetNextFcb(IrpContext, Vcb, &Restart))) {
                if ((Fcb->FcbState & UDF_FCB_DELETED) && !Fcb->FcbCleanup) {
                    DeletedClosePending = TRUE;
                    break;
                }
            }
        } _SEH2_FINALLY {
            if (VcbLocked) UDFUnlockVcb(IrpContext, Vcb);
            if (TableLocked) UDFUnlockFcbTable(IrpContext, Vcb);
        } _SEH2_END;
        if (DeletedClosePending ||
            InterlockedCompareExchange(&Vcb->PendingDeleteCount, 0, 0))
            return STATUS_DEVICE_BUSY;
    }
    return Status;
}

/* Called under this directory's FCB resource. An indexed, referenced FCB
 * and its Dloc retain the unpublished append generation. Final teardown
 * drains it before removing the FCB/LCB; an explicit flush drains before FE.
 * Ordinary file cleanup and all error/write-through boundaries stay eager.
 * Directory FileObjects do not carry FO_CACHE_SUPPORTED (create.c); the
 * pending generation was already admitted by a cached child cleanup. */
static BOOLEAN
UDFCanDeferDirectoryAppendCleanup(PVCB Vcb, PUDF_FILE_INFO FileInfo,
                                  PFILE_OBJECT FileObject)
{
    PFCB fcb;
    if (!FileInfo || !FileInfo->Dloc || !(fcb = FileInfo->Fcb) ||
        FileInfo->Dloc->CommonFcb != fcb ||
        !(fcb->FcbState & UDF_FCB_DIRECTORY) ||
        (fcb->FcbState & (UDF_FCB_DELETE_ON_CLOSE | UDF_FCB_DELETED)) ||
        !(fcb->FcbState & FCB_STATE_IN_FCB_TABLE) || fcb->FcbReference <= 0 ||
        !FileInfo->Dloc->DirIndex || UDFIsAStream(FileInfo) || UDFIsAStreamDir(FileInfo) ||
        Vcb->VcbCondition != VcbMounted ||
        FlagOn(Vcb->VcbState, VCB_STATE_VOLUME_READ_ONLY | UDF_VCB_FLAGS_DEAD) ||
        !FileObject ||
        FlagOn(FileObject->Flags, FO_WRITE_THROUGH | FO_NO_INTERMEDIATE_BUFFERING))
        return FALSE;
    return UDFDirectoryAppendPending(FileInfo);
}

/* Limit asynchronous cached-data scheduling to the same canonical,
 * single-link unpublished owner as metadata batching. Embedded payload,
 * mixed noncached opens, zero tails, streams and WT retain eager cleanup.
 * CcUninitializeCacheMap is still called at the original cleanup boundary. */
static BOOLEAN
UDFCanDeferNewFileDataCleanup(PIRP_CONTEXT IrpContext, PVCB Vcb,
                              PUDF_FILE_INFO FileInfo)
{
    PFCB fcb;
    if (!UDFDeferNewFileMetadata(IrpContext, Vcb, FileInfo) ||
        !(fcb = FileInfo->Fcb) || fcb->FcbCleanup ||
        fcb->CachedOpenHandleCount ||
        FlagOn(fcb->FcbState, UDF_FCB_DIRECTORY | UDF_FCB_EMBEDDED_DATA) ||
        fcb->Header.FileSize.QuadPart <= 0 ||
        fcb->Header.ValidDataLength.QuadPart < fcb->Header.FileSize.QuadPart ||
        FileInfo->Dloc->DataLoc.Offset ||
        ((((PFILE_ENTRY)FileInfo->Dloc->FileEntry)->icbTag.flags &
          ICB_FLAG_ALLOC_MASK) == ICB_FLAG_AD_IN_ICB))
        return FALSE;
    FileInfo->Dloc->FE_Flags |= UDF_FE_FLAG_CACHED_DATA_PENDING;
    UDF_METADATA_COUNT(Vcb,51);
    return TRUE;
}

/*************************************************************************
*
* Function: UDFCommonCleanup()
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
* Return Value: Does not matter!
*
*************************************************************************/
NTSTATUS
UDFCommonCleanup(
    PIRP_CONTEXT IrpContext,
    PIRP Irp)
{
    IO_STATUS_BLOCK         IoStatus;
    NTSTATUS                RC = STATUS_SUCCESS;
    NTSTATUS                UnlinkError = STATUS_SUCCESS;
    PFILE_OBJECT            FileObject = NULL;
    PFCB                    Fcb = NULL;
    PCCB                    Ccb = NULL;
    PVCB                    Vcb = NULL;
    ULONG                   lc = 0;
    BOOLEAN                 AcquiredVcb = FALSE;
    BOOLEAN                 AcquiredFCB = FALSE;
    BOOLEAN SendUnlockNotification = FALSE;
    BOOLEAN                 ChangeTime = FALSE;
    BOOLEAN                 ForcedCleanUp = FALSE;

    PUDF_FILE_INFO          NextFileInfo = NULL;
    TYPE_OF_OPEN            TypeOfOpen;

    ASSERT_IRP_CONTEXT(IrpContext);
    ASSERT_IRP(Irp);

    // If we were called with our file system device object instead of a
    // volume device object, just complete this request with STATUS_SUCCESS.

    if (IrpContext->Vcb == NULL) {

        UDFCompleteRequest(IrpContext, Irp, STATUS_SUCCESS);
        return STATUS_SUCCESS;
    }

    // Get the file object out of the Irp and decode the type of open.

    FileObject = IoGetCurrentIrpStackLocation(Irp)->FileObject;

    TypeOfOpen = UDFDecodeFileObject(FileObject, &Fcb, &Ccb);

    // No work here for either an UnopenedFile object or a StreamFileObject.

    if (TypeOfOpen <= StreamFileOpen) {

        UDFCompleteRequest(IrpContext, Irp, STATUS_SUCCESS);

        return STATUS_SUCCESS;
    }

    //  Keep a local pointer to the Vcb.
    Vcb = Fcb->Vcb;

    ASSERT_CCB(Ccb);
    ASSERT_FCB(Fcb);
    ASSERT_VCB(Vcb);

    if (FlagOn(Ccb->Flags, UDF_CCB_CLEANED)) {
        UDFCompleteRequest(IrpContext, Irp, STATUS_SUCCESS);
        return STATUS_SUCCESS;
    }

    _SEH2_TRY {

        // Steps we shall take at this point are:
        // (a) Acquire the file (FCB) exclusively
        // (b) Flush file data to disk
        // (c) Talk to the FSRTL package (if we use it) about pending oplocks.
        // (d) Notify the FSRTL package for use with pending notification IRPs
        // (e) Unlock byte-range locks (if any were acquired by process)
        // (f) Update time stamp values (e.g. fast-IO had been performed)
        // (g) Inform the Cache Manager to uninitialize Cache Maps ...
        // and other similar stuff.
        //  BrutePoint();

        if (TypeOfOpen == UserVolumeOpen) {

            // For a force dismount, physically disconnect this Vcb from the device so 
            // a new mount can occur.  Vcb deletion cannot happen at this time since 
            // there is a reference on it associated with this very request,  but we'll 
            // call check for dismount again later after we process this close.

            if (FlagOn(Ccb->Flags, CCB_FLAG_DISMOUNT_ON_CLOSE)) {
        
                UDFAcquireUdfData(IrpContext);
                UDFCheckForDismount(IrpContext, Vcb, TRUE);
                UDFReleaseUdfData(IrpContext);

            // If this handle actually wrote something, flush the device buffers,
            // and then set the verify bit now just to be safe (in case there is no
            // dismount).
        
            } else if (FlagOn(FileObject->Flags, FO_FILE_MODIFIED)) {
        
                UDFHijackIrpAndFlushDevice(IrpContext, Irp, Vcb->TargetDeviceObject);
                UDFUpdateMediaChangeCount(Vcb, 0);
                UDFMarkDevForVerifyIfVcbMounted(Vcb);
            }

            //  If the volume is locked by this file object then release
            //  the volume and send notification.

            if (FlagOn(Vcb->VcbState, VCB_STATE_LOCKED) &&
                FileObject == Vcb->VolumeLockFileObject) {

                UDFAutoUnlock(Vcb);
                SendUnlockNotification = TRUE;
            }

            UDFLockVcb(IrpContext, Vcb);
            UDFDecrementCleanupCounts(IrpContext, Fcb);
            UDFUnlockVcb(IrpContext, Vcb);

            if (FileObject->Flags & FO_CACHE_SUPPORTED) {
                // we've cached close
                InterlockedDecrement((PLONG)&Fcb->CachedOpenHandleCount);
            }

            MmPrint(("    CcUninitializeCacheMap()\n"));
            CcUninitializeCacheMap(FileObject, NULL, NULL);

            //  We must clean up the share access at this time, since we may not
            //  get a Close call for awhile if the file was mapped through this
            //  File Object.
            IoRemoveShareAccess( FileObject, &Fcb->ShareAccess);

            Ccb->Flags |= UDF_CCB_CLEANED;
            FileObject->Flags |= FO_CLEANUP_COMPLETE;

            try_return(RC = STATUS_SUCCESS);
        }
        else if ((TypeOfOpen == UserFileOpen) || (TypeOfOpen == UserDirectoryOpen)) {

            UDFAcquireVcbExclusive(IrpContext, Vcb, FALSE);
            AcquiredVcb = TRUE;
        }

        // Acquire current object only
        // Parent is acquired later only for delete operations (Child → Parent order)
        UDF_CHECK_PAGING_IO_RESOURCE(Fcb);
        UDFAcquireFcbExclusive(IrpContext, Fcb, FALSE);
        AcquiredFCB = TRUE;

        // Decrement the cleanup counts in the Vcb and Fcb.
        // Also decrement LCB reference count.

        UDFLockVcb(IrpContext, Vcb);
        UDFDecrementCleanupCounts(IrpContext, Fcb);
        if (Ccb->Lcb) {
            ASSERT(Ccb->Lcb->Reference > 0);
            InterlockedDecrement((PLONG)&Ccb->Lcb->Reference);
        }
        UDFUnlockVcb(IrpContext, Vcb);

        if (FileObject->Flags & FO_CACHE_SUPPORTED) {
            // we've cached close
            InterlockedDecrement((PLONG)&Fcb->CachedOpenHandleCount);
        }
        // No ASSERT on FcbCleanup vs FcbReference here - FcbCleanup
        // can be temporarily bumped by try-lock reordering in create.cpp

        // Transfer the preallocated option owner exactly once, independently
        // of the CCB hold consumed at the end of this cleanup.
        if (Ccb->Flags & UDF_CCB_DELETE_ON_CLOSE) {
            NTSTATUS RequestStatus = UDFActivateDeleteOnClose(IrpContext, Ccb);
            Ccb->Flags &= ~UDF_CCB_DELETE_ON_CLOSE;
            if (!NT_SUCCESS(RequestStatus))
                UnlinkError = RequestStatus;
            else if (Fcb->FcbState & UDF_FCB_DIRECTORY)
                FsRtlNotifyFullChangeDirectory(Vcb->NotifySync, &(Vcb->NextNotifyIRP),
                    (PVOID)Ccb, NULL, FALSE, FALSE, 0, NULL, NULL, NULL);
        }

        if (!(Fcb->FcbState & UDF_FCB_DIRECTORY)) {

            //  Unlock all outstanding file locks.
            if (Fcb->FileLock != NULL) {

                FsRtlFastUnlockAll(Fcb->FileLock,
                                   FileObject,
                                   IoGetRequestorProcess(Irp),
                                   NULL);
            }
        }
        // get Link count
        NextFileInfo = Ccb->FileInfo;
        if (!NextFileInfo || !NextFileInfo->Dloc) {
            UnlinkError = STATUS_FILE_INVALID;
            Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
            IoRemoveShareAccess(FileObject, &Fcb->ShareAccess);
            Ccb->Flags |= UDF_CCB_CLEANED;
            FileObject->Flags |= FO_CLEANUP_COMPLETE;
            try_return(RC = UnlinkError);
        }
        lc = UDFGetFileNameLinkCount(NextFileInfo);

        if (!Fcb->FcbCleanup &&
            (!IsListEmpty(&Fcb->PendingDeleteQueue))) {
            NTSTATUS DeleteStatus = UDFProcessPendingDeletes(IrpContext, Fcb, FALSE);
            if (NT_SUCCESS(UnlinkError) && !NT_SUCCESS(DeleteStatus))
                UnlinkError = DeleteStatus;
        }
        /* NextFileInfo remains this CCB's exact hold. The request processor
         * used its own FI and cannot retarget the close/cache/time path. */
        lc = UDFGetFileNameLinkCount(NextFileInfo);
        ForcedCleanUp = BooleanFlagOn(Fcb->FcbState, UDF_FCB_DELETED);
        FileObject->DeletePending = FlagOn(Fcb->FcbState, UDF_FCB_DELETED) ||
            UDFIsLinkDeletePending(Fcb, NextFileInfo) ||
            (Ccb->Lcb && FlagOn(Ccb->Lcb->Flags, UDF_LCB_FLAG_LINK_DELETED));

        if (Fcb->FcbState & UDF_FCB_DIRECTORY) {
            //  Report to the dir notify package for a directory.
            FsRtlNotifyCleanup( Vcb->NotifySync, &(Vcb->NextNotifyIRP), (PVOID)Ccb );
        }

        // we can't purge Cache when more than one link exists
        if (lc > 1) {
            ForcedCleanUp = FALSE;
        }

        if (NT_SUCCESS(UnlinkError) &&
            !(Vcb->VcbState & VCB_STATE_VOLUME_READ_ONLY) &&
            (FileObject->Flags & FO_CACHE_SUPPORTED) &&
             Fcb->FcbNonpaged->SegmentObject.DataSectionObject) {
            BOOLEAN LastNonCached = (!Fcb->CachedOpenHandleCount &&
                                      Fcb->FcbCleanup);
            // If this was the last cached open, and there are open
            // non-cached handles, attempt a flush and purge operation
            // to avoid cache coherency overhead from these non-cached
            // handles later. Preserve the first initialization/flush error.
            // We shall not flush deleted files
            RC = STATUS_SUCCESS;
            if (  LastNonCached
                      ||
                (!Fcb->FcbCleanup &&
                 !ForcedCleanUp &&
                 !UDFCanDeferNewFileDataCleanup(IrpContext, Vcb, NextFileInfo)) ) {

                LONGLONG OldFileSize, NewFileSize;

                if ((OldFileSize = Fcb->Header.ValidDataLength.QuadPart) <
                    (NewFileSize = Fcb->Header.FileSize.QuadPart)) {
                    RC = UDFZeroData(IrpContext, Fcb, FileObject,
                        OldFileSize, NewFileSize - OldFileSize, TRUE, FALSE);
                    if (RC == STATUS_SUCCESS) {
                        ExAcquireFastMutex(Fcb->Header.FastMutex);
                        Fcb->Header.ValidDataLength.QuadPart = NewFileSize;
                        ExReleaseFastMutex(Fcb->Header.FastMutex);
                    } else if (NT_SUCCESS(UnlinkError))
                        UnlinkError = RC;
                }

                /* A cache flush cannot stand in for a failed zero request.
                 * Preserve its first status and skip later metadata/purge I/O. */
                if (NT_SUCCESS(UnlinkError)) {
                    MmPrint(("    CcFlushCache()\n"));
                    CcFlushCache(&Fcb->FcbNonpaged->SegmentObject, NULL, 0, &IoStatus);
                    if (!NT_SUCCESS(IoStatus.Status)) {
                        MmPrint(("    CcFlushCache() error: %x\n", IoStatus.Status));
                        RC = IoStatus.Status;
                        UnlinkError = IoStatus.Status;
                    } else if (NextFileInfo && NextFileInfo->Dloc &&
                               NT_SUCCESS(NextFileInfo->Dloc->ZeroDataError)) {
                        NextFileInfo->Dloc->FE_Flags &= ~UDF_FE_FLAG_CACHED_DATA_PENDING;
                    }
                }
            }
            // If file is deleted or it is last cached open, but there are
            // some non-cached handles we should purge cache section
            if (NT_SUCCESS(UnlinkError) && (ForcedCleanUp || LastNonCached)) {
                if (Fcb->FcbNonpaged->SegmentObject.DataSectionObject) {
                    MmPrint(("    CcPurgeCacheSection()\n"));
                    CcPurgeCacheSection(&Fcb->FcbNonpaged->SegmentObject, NULL, 0, FALSE);
                }
/*                MmPrint(("    CcPurgeCacheSection()\n"));
                CcPurgeCacheSection(&Fcb->SectionObject, NULL, 0, FALSE);*/
            }
            // we needn't Flush here. It will be done in UDFCloseFile__
        }

        // Update FileTimes & Attrs
        if (NT_SUCCESS(UnlinkError) &&
           !(Vcb->VcbState & VCB_STATE_VOLUME_READ_ONLY) &&
           !(Fcb->FcbState & (UDF_FCB_DELETE_ON_CLOSE |
                              UDF_FCB_DELETED /*|
                              UDF_FCB_DIRECTORY |
                              UDF_FCB_READ_ONLY*/)) &&
           !UDFIsAStreamDir(NextFileInfo)) {
            LONGLONG NtTime;
            LONGLONG ASize;
            KeQuerySystemTime((PLARGE_INTEGER)&NtTime);
            // Check if we should set ARCHIVE bit & LastWriteTime
            if (FileObject->Flags & FO_FILE_MODIFIED) {
                ULONG Attr;
                PDIR_INDEX_ITEM DirNdx;
                DIR_INDEX_ITEM AttributesIndex;
                DirNdx = UDFDirIndex(UDFGetDirIndexByFileInfo(NextFileInfo), NextFileInfo->Index);
                if (!DirNdx) {
                    /* A cold open-by-ID owns its FI but has no FID index.
                     * Archive is stored in the common FE, so update that
                     * field without inventing a namespace entry. */
                    RtlZeroMemory(&AttributesIndex, sizeof(AttributesIndex));
                    AttributesIndex.FileInfo = NextFileInfo;
                    AttributesIndex.SysAttr = Fcb->FileAttributes;
                    DirNdx = &AttributesIndex;
                }
                // Archive bit
                if (!(Ccb->Flags & UDF_CCB_ATTRIBUTES_SET) &&
                    (Vcb->CompatFlags & UDF_VCB_IC_UPDATE_ARCH_BIT)) {
                    Attr = UDFAttributesToNT(DirNdx, NextFileInfo->Dloc->FileEntry);
                    if (!(Attr & FILE_ATTRIBUTE_ARCHIVE))
                        UDFAttributesToUDF(DirNdx, NextFileInfo->Dloc->FileEntry, Attr | FILE_ATTRIBUTE_ARCHIVE);
                    Fcb->FileAttributes = DirNdx->SysAttr;
                }
                // WriteTime
                if (!(Ccb->Flags & UDF_CCB_WRITE_TIME_SET) &&
                    (Vcb->CompatFlags & UDF_VCB_IC_UPDATE_MODIFY_TIME)) {
                    UDFSetFileXTime(NextFileInfo, NULL, &NtTime, NULL, &NtTime);
                    Fcb->LastWriteTime.QuadPart =
                    Fcb->LastAccessTime.QuadPart = NtTime;
                    ChangeTime = TRUE;
                }
            }
            if (!(Fcb->FcbState & UDF_FCB_DIRECTORY)) {
                // Update sizes in DirIndex
                if (!Fcb->FcbCleanup) {
                    ASize = UDFGetFileAllocationSize(Vcb, NextFileInfo);
//                        Fcb->CommonFCBHeader.AllocationSize.QuadPart;
                    UDFSetFileSizeInDirNdx(Vcb, NextFileInfo, &ASize);

                } else if (FileObject->Flags & FO_FILE_SIZE_CHANGED) {

                    ASize = //UDFGetFileAllocationSize(Vcb, NextFileInfo);
                    Fcb->Header.AllocationSize.QuadPart;
                    UDFSetFileSizeInDirNdx(Vcb, NextFileInfo, &ASize);

                    if (UDFIsAStream(NextFileInfo)) {

                        UDFNotifyReportChange(IrpContext, Vcb,
                            Fcb,
                            FILE_NOTIFY_CHANGE_STREAM_SIZE,
                            FILE_ACTION_MODIFIED_STREAM,
                            Ccb->Lcb, FileObject);
                    }
                    else {

                        UDFNotifyReportChange(IrpContext, Vcb,
                            Fcb,
                            FILE_NOTIFY_CHANGE_SIZE,
                            FILE_ACTION_MODIFIED,
                            Ccb->Lcb, FileObject);
                    }

                }
            }
            // AccessTime
            if ((FileObject->Flags & FO_FILE_FAST_IO_READ) &&
               !(Ccb->Flags & UDF_CCB_ACCESS_TIME_SET) &&
                (Vcb->CompatFlags & UDF_VCB_IC_UPDATE_ACCESS_TIME)) {
                UDFSetFileXTime(NextFileInfo, NULL, &NtTime, NULL, NULL);
                Fcb->LastAccessTime.QuadPart = NtTime;
//                ChangeTime = TRUE;
            }
            // ChangeTime (AttrTime)
            if (!(Ccb->Flags & UDF_CCB_MODIFY_TIME_SET) &&
                (Vcb->CompatFlags & UDF_VCB_IC_UPDATE_ATTR_TIME) &&
                (ChangeTime || (Ccb->Flags & (UDF_CCB_ATTRIBUTES_SET |
                                                 UDF_CCB_CREATE_TIME_SET |
                                                 UDF_CCB_ACCESS_TIME_SET |
                                                 UDF_CCB_WRITE_TIME_SET))) ) {
                UDFSetFileXTime(NextFileInfo, NULL, NULL, &NtTime, NULL);
                Fcb->ChangeTime.QuadPart = NtTime;
            }
        }

        // Keep structural updates eager. Only an owned unpublished append or
        // a proved attribute-only directory generation may remain resident.
        // Final teardown flushes before removing the canonical FCB/LCB;
        // explicit flush never takes either cleanup-only exception.
        if (NT_SUCCESS(UnlinkError) && !Fcb->FcbCleanup &&
            !ForcedCleanUp &&
            !(Fcb->FcbState & UDF_FCB_DELETED) &&
            !(Vcb->VcbState & VCB_STATE_VOLUME_READ_ONLY) &&
            NextFileInfo &&
            !UDFCanDeferDirectoryAppendCleanup(Vcb, NextFileInfo, FileObject) &&
            !UDFCanDeferDirectoryAttributesCleanup(IrpContext, Vcb,
                                                  NextFileInfo, FileObject) &&
            !UDFDeferNewFileMetadata(IrpContext, Vcb, NextFileInfo)) {
            NTSTATUS FlushStatus = UDFFlushFile__(IrpContext, Vcb,
                                                  NextFileInfo, 0);
            if (NT_SUCCESS(UnlinkError) && !NT_SUCCESS(FlushStatus))
                UnlinkError = FlushStatus;
        }

        /* Namespace removal does not prove the shared cache/section has
         * no remaining owner. Do not truncate it to zero on this cleanup;
         * allocation stays reserved until final FI/Dloc/FCB retirement. */
        MmPrint(("    CcUninitializeCacheMap()\n"));
        CcUninitializeCacheMap(FileObject, NULL, NULL);

        if (
            !Fcb->FcbCleanup &&
            !(Fcb->FcbState & (UDF_FCB_DELETE_ON_CLOSE | UDF_FCB_DELETED)) &&
            !UDFIsAStream(NextFileInfo) &&
            !UDFIsAStreamDir(NextFileInfo)
        )
            Fcb->FcbState |= UDF_FCB_DELAY_CLOSE;

        // release resources now.
        UDFReleaseFcb(IrpContext, Fcb);
        AcquiredFCB = FALSE;

        // Close the target file's FileInfo - this decrements FileInfo->RefCount
        // Parent FileInfo references are now handled by LCB mechanism in UDFTeardownStructures
        ASSERT(AcquiredVcb);
        if (NextFileInfo) {
            PUDF_FILE_INFO DirInfo = NextFileInfo->ParentFile;
            /* UDFCloseFile__ consumes its reference even when a later
             * write fails. Never let a retry/close consume it twice. */
            Ccb->FileInfo = NULL;
            NTSTATUS CloseStatus = NT_SUCCESS(UnlinkError) ?
                UDFCloseFile__(IrpContext, Vcb, NextFileInfo) :
                UDFDereferenceFileChecked(Vcb, NextFileInfo);
            /* On an earlier failure consume this CCB's hold without
             * silently repeating writeback through a different FI alias. */
            if (NT_SUCCESS(UnlinkError) && !NT_SUCCESS(CloseStatus))
                UnlinkError = CloseStatus;
            if (NT_SUCCESS(UnlinkError) && DirInfo &&
                DirInfo->Dloc && 
                DirInfo->Fcb &&
                !(Vcb->VcbState & VCB_STATE_VOLUME_READ_ONLY) &&
                (
                    (DirInfo->Dloc->FE_Flags & UDF_FE_FLAG_FE_MODIFIED) ||
                    DirInfo->Dloc->AllocLoc.Modified ||
                    DirInfo->Dloc->DataLoc.Modified ||
                    DirInfo->Dloc->FELoc.Modified
                )
                ) {
                UDF_CHECK_PAGING_IO_RESOURCE(DirInfo->Fcb);
                UDFAcquireFcbExclusive(IrpContext, DirInfo->Fcb, FALSE);
                /* An append buffer owns the unpublished directory bytes and
                 * keeps its FE dirty. The indexed, referenced parent remains
                 * authoritative; teardown/explicit flush drains before FE
                 * publication. Ordinary child FE cleanup above is unchanged. */
                BOOLEAN DeferAppend =
                    FlagOn(FileObject->Flags, FO_CACHE_SUPPORTED) &&
                    (UDFCanDeferDirectoryAppendCleanup(Vcb, DirInfo, FileObject) ||
                     (UDFDeferNewFileMetadata(IrpContext, Vcb, NextFileInfo) &&
                      DirInfo == NextFileInfo->ParentFile &&
                      DirInfo->Dloc->CommonFcb == DirInfo->Fcb &&
                      DirInfo->Fcb->FcbReference > 0 &&
                      FlagOn(DirInfo->Fcb->FcbState, FCB_STATE_IN_FCB_TABLE)));
                if (!DeferAppend) {
                    NTSTATUS FlushStatus = UDFFlushFile__(IrpContext, Vcb,
                                                           DirInfo,
                                                           UDF_FLUSH_FLAGS_LITE);
                    if (NT_SUCCESS(UnlinkError) && !NT_SUCCESS(FlushStatus))
                        UnlinkError = FlushStatus;
                }
                UDFReleaseFcb(IrpContext, DirInfo->Fcb);
            }
        }

        if (!NT_SUCCESS(UnlinkError))
            Fcb->FcbState &= ~UDF_FCB_DELAY_CLOSE;

        Ccb->Flags |= UDF_CCB_CLEANED;

        //  We must clean up the share access at this time, since we may not
        //  get a Close call for awhile if the file was mapped through this
        //  File Object.
        IoRemoveShareAccess(FileObject, &Fcb->ShareAccess);

        Fcb->Header.IsFastIoPossible = UDFIsFastIoPossible(Fcb);

        FileObject->Flags |= FO_CLEANUP_COMPLETE;

try_exit: NOTHING;

    } _SEH2_FINALLY {

        if (AcquiredFCB) {
            UDFReleaseFcb(IrpContext, Fcb);
        }

        if (AcquiredVcb) {
            UDFReleaseVcb(IrpContext, Vcb);
            AcquiredVcb = FALSE;
        }

        if (SendUnlockNotification) {

            FsRtlNotifyVolumeEvent(FileObject, FSRTL_VOLUME_UNLOCK);
        }

        if (!_SEH2_AbnormalTermination()) {
                if (!NT_SUCCESS(UnlinkError))
                    RC = UnlinkError;
                UDFCompleteRequest(IrpContext, Irp, RC);
        }

    } _SEH2_END; // end of "__finally" processing
    return(RC);
} // end UDFCommonCleanup()

VOID
UDFAutoUnlock (
    IN PVCB Vcb
    )
{
    KIRQL SavedIrql;

    //  Unlock the volume.
 
    IoAcquireVpbSpinLock( &SavedIrql );

    ClearFlag(Vcb->Vpb->Flags, VPB_LOCKED | VPB_DIRECT_WRITES_ALLOWED);
    ClearFlag(Vcb->VcbState, VCB_STATE_LOCKED);
    Vcb->VolumeLockFileObject = NULL;

    IoReleaseVpbSpinLock( SavedIrql );
}
