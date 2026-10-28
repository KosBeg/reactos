#include "udffs.h"

// The Bug check file id for this module

#define BugCheckFileId                   (UDFS_BUG_CHECK_STRUCSUP)

typedef struct _FCB_TABLE_ELEMENT {

    FILE_ID FileId;
    PFCB Fcb;

} FCB_TABLE_ELEMENT, *PFCB_TABLE_ELEMENT;

/* FcbTableMutex serializes identity admission; VcbMutex protects the
 * table's representation. Membership is a receipt from RtlInsert, never a
 * flag set on allocation refusal or an entry belonging to another FCB. */
NTSTATUS
UDFInsertFcbIntoTable(
    _In_ PIRP_CONTEXT IrpContext,
    _In_ PFCB Fcb
    )
{
    FCB_TABLE_ELEMENT Key;
    PFCB_TABLE_ELEMENT Entry = NULL;
    BOOLEAN NewElement;
    _SEH2_VOLATILE NTSTATUS Status = STATUS_SUCCESS;
    UNREFERENCED_PARAMETER(IrpContext);
    ASSERT_LOCKED_VCB(Fcb->Vcb);
    Key.FileId = Fcb->FileId;
    Key.Fcb = Fcb;
    _SEH2_TRY {
        Entry = RtlInsertElementGenericTable(&Fcb->Vcb->FcbTable,
                    &Key, sizeof(Key), &NewElement);
    } _SEH2_EXCEPT(FsRtlIsNtstatusExpected(_SEH2_GetExceptionCode()) ?
                  EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        Status = _SEH2_GetExceptionCode();
    } _SEH2_END;
    if (Status != STATUS_SUCCESS) return Status;
    if (!Entry) return STATUS_INSUFFICIENT_RESOURCES;
    if (Entry->FileId.QuadPart != Fcb->FileId.QuadPart || Entry->Fcb != Fcb)
        return STATUS_SHARING_VIOLATION;
    SetFlag(Fcb->FcbState, FCB_STATE_IN_FCB_TABLE);
    return STATUS_SUCCESS;
}

static
inline
PFCB_NONPAGED
UDFAllocateFcbNonpaged(
)
{
    return (PFCB_NONPAGED)ExAllocateFromNPagedLookasideList(&UdfData.UDFNonPagedFcbLookasideList);
}

static
inline
PFCB
UDFAllocateFcbIndex(
)
{
    return (PFCB)ExAllocateFromPagedLookasideList(&UdfData.UDFFcbIndexLookasideList);
}

static
inline
PFCB
UDFAllocateFcbData(
)
{
    return (PFCB)ExAllocateFromPagedLookasideList(&UdfData.UDFFcbDataLookasideList);
}

static
inline
PFCB
UDFAllocateFcb(
)
{
    return (PFCB)ExAllocatePoolWithTag(NonPagedPool, sizeof(struct FCB), TAG_FCB);
}

static
inline
VOID
UDFDeallocateFcbNonpaged(
    PFCB_NONPAGED FcbNonpaged
    )
{
    ExFreeToNPagedLookasideList(&UdfData.UDFNonPagedFcbLookasideList, FcbNonpaged);
}

static
inline
VOID
UDFDeallocateFcbIndex(
    PFCB Fcb
    )
{
    ExFreeToPagedLookasideList(&UdfData.UDFFcbIndexLookasideList, Fcb);
}

static
inline
VOID
UDFDeallocateFcbData(
    PFCB Fcb
    )
{
    ExFreeToPagedLookasideList(&UdfData.UDFFcbDataLookasideList, Fcb);
}

PFCB_NONPAGED
UDFCreateFcbNonpaged(
    _In_ PIRP_CONTEXT IrpContext
    )

/*++

Routine Description:

    This routine is called to create and initialize the non-paged portion
    of an Fcb.

Arguments:

Return Value:

    PFCB_NONPAGED - Pointer to the created nonpaged Fcb.  NULL if not created.

--*/

{
    PFCB_NONPAGED FcbNonpaged;
    NTSTATUS Status;

    PAGED_CODE();
    
    UNREFERENCED_PARAMETER(IrpContext);
    
    //  Allocate the non-paged pool and initialize the various
    //  synchronization objects.

    FcbNonpaged = UDFAllocateFcbNonpaged();
    if (!FcbNonpaged)
        return NULL;

    RtlZeroMemory(FcbNonpaged, sizeof(struct FCB_NONPAGED));

    FcbNonpaged->NodeTypeCode = UDF_NODE_TYPE_FCB_NONPAGED;
    FcbNonpaged->NodeByteSize = sizeof(struct FCB_NONPAGED);

    Status = ExInitializeResourceLite(&FcbNonpaged->FcbPagingIoResource);
    if (!NT_SUCCESS(Status)) {
        UDFDeallocateFcbNonpaged(FcbNonpaged);
        return NULL;
    }
    Status = ExInitializeResourceLite(&FcbNonpaged->FcbResource);
    if (!NT_SUCCESS(Status)) {
        ExDeleteResourceLite(&FcbNonpaged->FcbPagingIoResource);
        UDFDeallocateFcbNonpaged(FcbNonpaged);
        return NULL;
    }
    ExInitializeFastMutex(&FcbNonpaged->FcbMutex);
    ExInitializeFastMutex(&FcbNonpaged->AdvancedFcbHeaderMutex);
    ExInitializeFastMutex(&FcbNonpaged->FcbFastMutex);

    return FcbNonpaged;
}

VOID
UDFDeleteFcbNonpaged (
    _In_ PIRP_CONTEXT IrpContext,
    _In_ PFCB_NONPAGED FcbNonpaged
    )

/*++

Routine Description:

    This routine is called to cleanup the non-paged portion of an Fcb.

Arguments:

    FcbNonpaged - Structure to clean up.

Return Value:

    None

--*/

{
    PAGED_CODE();
    
    UNREFERENCED_PARAMETER(IrpContext);
    
    ExDeleteResourceLite(&FcbNonpaged->FcbResource);
    ExDeleteResourceLite(&FcbNonpaged->FcbPagingIoResource);

    UDFDeallocateFcbNonpaged(FcbNonpaged);

    return;
}

VOID
UDFDeleteFcb(
    _In_opt_ PIRP_CONTEXT IrpContext,
    _In_ PFCB Fcb
    )

/*++

Routine Description:

    This routine is called to cleanup and deallocate an Fcb.  We know there
    are no references remaining.  We cleanup any auxilary structures and
    deallocate this Fcb.

    NOTE: Caller should remove FCB from FcbTable before calling this routine
    (typically done by UDFTeardownStructures while holding VCB lock).
    FCB should NOT be in FcbTable when this is called - either it was removed
    by TeardownStructures, or it was never inserted (error path in create).

Arguments:

    IrpContext - Optional IrpContext (unused but kept for API consistency).

    Fcb - This is the Fcb to deallocate.

Return Value:

    None

--*/

{
    PVCB Vcb = NULL;
    BOOLEAN SystemReferenceHeld = Fcb->SystemReferenceHeld;
    BOOLEAN SystemUserReferenceHeld = Fcb->SystemUserReferenceHeld;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(IrpContext);

    UDFPrint(("UDFDeleteFcb: %x\n", Fcb));

    ASSERT_FCB(Fcb);

    // Sanity check the counts.

    NT_ASSERT( Fcb->FcbCleanup == 0 );
    NT_ASSERT( Fcb->FcbReference == 0 );
    NT_ASSERT(!Fcb->ZeroDataFileObject && !Fcb->ZeroDataRetryThread);
    if (Fcb->ZeroDataFileObject || Fcb->ZeroDataRetryThread) {
        Fcb->Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
        return;
    }

    // FCB must NOT be in FcbTable - either TeardownStructures removed it,
    // or it was never inserted (error path before UDFInsertFcbIntoTable).
    NT_ASSERT(!FlagOn(Fcb->FcbState, FCB_STATE_IN_FCB_TABLE));
    NT_ASSERT(!Fcb->MetadataCloseQueued);

    // LCB queues must be empty - all LCBs should be removed during teardown
    NT_ASSERT(IsListEmpty(&Fcb->ParentLcbQueue));
    NT_ASSERT(IsListEmpty(&Fcb->ChildLcbQueue));
    NT_ASSERT(IsListEmpty(&Fcb->PendingDeleteQueue));
    NT_ASSERT(IsListEmpty(&Fcb->AbortedOpenQueue));
    if (!IsListEmpty(&Fcb->PendingDeleteQueue) || !IsListEmpty(&Fcb->AbortedOpenQueue)) {
        /* Also preserve a failed constructor's quarantined request in free
         * builds. Its Dloc/VCB cannot be disposed as an empty failed open. */
        Fcb->Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
        return;
    }

    // NOTE: FCB no longer stores FCBName - all names are stored in LCB.
    // No FCBName cleanup needed.

    // Release any Filter Context structures associated with this FCB.
    // Only if FCB was fully initialized (Header.Resource is set by
    // UDFInitializeFCB).

    if (Fcb->Header.Resource) {
        FsRtlTeardownPerStreamContexts(&Fcb->Header);
    }

    // Delete non-paged portion (resources + dealloc).

    UDFDeleteFcbNonpaged(IrpContext, Fcb->FcbNonpaged);

    // Now do the type specific structures.

    switch (Fcb->Header.NodeTypeCode) {

    case UDF_NODE_TYPE_INDEX:

        if (Fcb == Fcb->Vcb->RootIndexFcb) {

            Vcb = Fcb->Vcb;
            Vcb->RootIndexFcb = NULL;
        }

        UDFDeallocateFcbIndex(Fcb);
        break;

    case UDF_NODE_TYPE_DATA:

        if (Fcb->FileLock != NULL) {

            FsRtlFreeFileLock( Fcb->FileLock );
        }

        if (Fcb == Fcb->Vcb->VolumeDasdFcb) {

            Vcb = Fcb->Vcb;
            Vcb->VolumeDasdFcb = NULL;
        }

        UDFDeallocateFcbData(Fcb);
        break;
    }

    // Decrement the Vcb reference count if this is a system
    // Fcb.

    if (Vcb != NULL) {
        if (SystemReferenceHeld)
            InterlockedDecrement((LONG*)&Vcb->VcbReference);
        if (SystemUserReferenceHeld)
            InterlockedDecrement((LONG*)&Vcb->VcbUserReference);
    }

    return;
}

/*
    This routine walks through the tree to RootDir & kills all unreferenced
    structures using LCB-based parent traversal.

    StartingFcb must be acquired exclusively by caller.
    This function will acquire locks for parent FCBs as needed.

    The algorithm:
    1. If FcbReference != 0, break (FCB still in use)
    2. Walk ParentLcbQueue to find LCBs with Reference == 0
    3. For each such LCB: remove it and decrement parent's FcbReference/FileInfo->RefCount
    4. If parent's FcbReference goes to 0, recursively tear down parent
    5. Delete the FCB when all LCBs are processed
 */
static NTSTATUS
UDFPrepareTeardownFile(PIRP_CONTEXT IrpContext, PVCB Vcb, PFCB Fcb)
{
    PUDF_FILE_INFO FileInfo = Fcb->FileInfo;
    NTSTATUS Status;

    if (!FileInfo) return STATUS_SUCCESS;
    if (FileInfo->Dloc && !NT_SUCCESS(FileInfo->Dloc->AllocationReleaseError))
        return FileInfo->Dloc->AllocationReleaseError;
    if (FileInfo->Dloc && FileInfo->Dloc->AllocationReleaseActive)
        return STATUS_DEVICE_BUSY;
    if (!IsListEmpty(&Fcb->PendingDeleteQueue)) {
        Status = UDFProcessPendingDeletes(IrpContext, Fcb, FALSE);
        if (!NT_SUCCESS(Status)) return Status;
    }
    if (Fcb->FcbState & UDF_FCB_DELETED)
        return STATUS_SUCCESS;
    return UDFFlushFile__(IrpContext, Vcb, FileInfo, 0);
}

/* Pins are independent of the original LCB holds. No parent/name owner is
 * consumed before private cleanup accepts retirement. On an uncertain GC,
 * the indexed gated FCB owns this plan until explicit repair/removal policy. */
typedef struct _UDF_TEARDOWN_PARENT {
    PFCB Fcb;
    ULONG LinkHolds;
    BOOLEAN PinOwned;
    BOOLEAN Acquired;
} UDF_TEARDOWN_PARENT, *PUDF_TEARDOWN_PARENT;

typedef struct _UDF_TEARDOWN_PLAN {
    ULONG Count;
    ULONG Capacity;
    ULONG CleanupResult;
    NTSTATUS CleanupStatus;
    PUDF_DATALOC_INFO CleanupDloc;
    BOOLEAN CleanupEntered;
    BOOLEAN CleanupAccepted;
    UDF_TEARDOWN_PARENT Parents[1];
} UDF_TEARDOWN_PLAN, *PUDF_TEARDOWN_PLAN;

/* VcbMutex held. A retry marker is an inventory entry, not a fabricated
 * handle/refcount and not evidence that an on-disk generation is dirty. */
static VOID
UDFMarkTeardownRetryLocked(PVCB Vcb, PFCB Fcb)
{
    if (FlagOn(Fcb->FcbState, FCB_STATE_IN_FCB_TABLE) &&
        !Fcb->TeardownRetryPending) {
        Fcb->TeardownRetryPending = TRUE;
        InterlockedIncrement(&Vcb->ClosedFcbRetryCount);
    }
}

static VOID
UDFMarkTeardownRetry(PIRP_CONTEXT IrpContext, PVCB Vcb, PFCB Fcb)
{
    UDFLockVcb(IrpContext, Vcb);
    UDFMarkTeardownRetryLocked(Vcb, Fcb);
    UDFUnlockVcb(IrpContext, Vcb);
}

VOID
UDFScheduleFcbTeardownRetry(PIRP_CONTEXT IrpContext, PFCB Fcb)
{
    UDFMarkTeardownRetry(IrpContext, Fcb->Vcb, Fcb);
}

static NTSTATUS
UDFPinTeardownParent(PIRP_CONTEXT IrpContext, PVCB Vcb, PFCB Child,
                     PUDF_TEARDOWN_PLAN Plan, PFCB Parent)
{
    ULONG Index;
    NTSTATUS Status = STATUS_SUCCESS;
    PUDF_TEARDOWN_PARENT Entry;
    if (!Parent)
        return STATUS_SUCCESS; /* weak FI can have no OS FCB */
    if (Parent == Child || Parent->Vcb != Vcb)
        return STATUS_FILE_CORRUPT_ERROR;
    for (Index = 0; Index < Plan->Count; Index++)
        if (Plan->Parents[Index].Fcb == Parent)
            return STATUS_SUCCESS;
    if (Plan->Count == Plan->Capacity)
        return STATUS_FILE_CORRUPT_ERROR;
    Entry = &Plan->Parents[Plan->Count++];
    Entry->Fcb = Parent;
    UDFLockFcbTable(IrpContext, Vcb);
    UDFLockVcb(IrpContext, Vcb);
    if (Parent->TeardownInProgress || !Parent->Header.Resource)
        Status = STATUS_DEVICE_BUSY;
    else if (Parent->FcbReference == MAXULONG || Vcb->VcbReference == MAXULONG)
        Status = STATUS_INSUFFICIENT_RESOURCES;
    else {
        Parent->FcbReference++;
        Vcb->VcbReference++;
        Entry->PinOwned = TRUE;
    }
    UDFUnlockVcb(IrpContext, Vcb);
    UDFUnlockFcbTable(IrpContext, Vcb);
    if (!NT_SUCCESS(Status))
        return Status;
    if (ExIsResourceAcquiredExclusiveLite(Parent->Header.Resource))
        return STATUS_SUCCESS; /* caller owns it; never transfer/dispose it */
    if (!UDFAcquireFcbExclusive(IrpContext, Parent, TRUE))
        return STATUS_DEVICE_BUSY;
    Entry->Acquired = TRUE;
    return STATUS_SUCCESS;
}

/* KeepPins leaves a concrete owner in Fcb->TeardownPlan after an uncertain
 * destructive phase. ERESOURCE ownership is always released on this thread.
 * A selected parent resource is transferred to the ancestor walk only after
 * dropping our last pin; every other newly closed parent stays inventoried. */
static VOID
UDFReleaseTeardownPlan(PIRP_CONTEXT IrpContext, PVCB Vcb,
                       PUDF_TEARDOWN_PLAN Plan, BOOLEAN KeepPins,
                       BOOLEAN WalkParents, PFCB* NextFcb)
{
    ULONG Index;
    if (!Plan)
        return;
    for (Index = 0; Index < Plan->Count; Index++) {
        PUDF_TEARDOWN_PARENT Entry = &Plan->Parents[Index];
        BOOLEAN Transfer = FALSE;
        if (Entry->PinOwned && !KeepPins) {
            UDFLockVcb(IrpContext, Vcb);
            ASSERT(Entry->Fcb->FcbReference && Vcb->VcbReference);
            Entry->Fcb->FcbReference--;
            Vcb->VcbReference--;
            Entry->PinOwned = FALSE;
            if (!Entry->Fcb->FcbReference) {
                UDFMarkTeardownRetryLocked(Vcb, Entry->Fcb);
                if (WalkParents && Entry->Acquired && !*NextFcb) {
                    *NextFcb = Entry->Fcb;
                    Transfer = TRUE;
                }
            }
            UDFUnlockVcb(IrpContext, Vcb);
        }
        if (Entry->Acquired) {
            if (!Transfer)
                UDFReleaseFcb(IrpContext, Entry->Fcb);
            Entry->Acquired = FALSE;
        }
    }
    if (!KeepPins)
        MyFreePool__(Plan);
}

static NTSTATUS
UDFPrepareTeardownPlan(PIRP_CONTEXT IrpContext, PVCB Vcb, PFCB Fcb,
                       PUDF_TEARDOWN_PLAN* PlanOut)
{
    PUDF_FILE_INFO Canonical = Fcb->FileInfo, Link;
    PLIST_ENTRY ListLink;
    ULONG Capacity = 0, Bytes;
    NTSTATUS Status;
    PUDF_TEARDOWN_PLAN Plan;
    *PlanOut = NULL;
    if (!Canonical || !Canonical->Dloc)
        return STATUS_FILE_CORRUPT_ERROR;
    if (Canonical->Dloc->CreateRollbackOwner || Canonical->Dloc->CreateConstructionOwner)
        return STATUS_DEVICE_BUSY;
    for (ListLink = Fcb->ParentLcbQueue.Flink;
         ListLink != &Fcb->ParentLcbQueue; ListLink = ListLink->Flink) {
        if (Capacity == MAXULONG)
            return STATUS_INSUFFICIENT_RESOURCES;
        Capacity++;
    }
    Link = Canonical;
    do {
        if (!Link || Link->Dloc != Canonical->Dloc)
            return STATUS_FILE_CORRUPT_ERROR;
        if (Capacity == MAXULONG)
            return STATUS_INSUFFICIENT_RESOURCES;
        Capacity++; /* upper bound; parent FCBs are deduplicated below */
        Link = Link->NextLinkedFile;
    } while (Link != Canonical);
    if (Capacity > (MAXULONG - FIELD_OFFSET(UDF_TEARDOWN_PLAN, Parents)) /
                    sizeof(UDF_TEARDOWN_PARENT))
        return STATUS_INSUFFICIENT_RESOURCES;
    Bytes = FIELD_OFFSET(UDF_TEARDOWN_PLAN, Parents) +
            Capacity * sizeof(UDF_TEARDOWN_PARENT);
    Plan = MyAllocatePool__(NonPagedPool, Bytes);
    if (!Plan)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Plan, Bytes);
    Plan->Capacity = Capacity;
    *PlanOut = Plan; /* every acquired pin/resource now has an unwind owner */
    for (ListLink = Fcb->ParentLcbQueue.Flink;
         ListLink != &Fcb->ParentLcbQueue; ListLink = ListLink->Flink) {
        PLCB Lcb = CONTAINING_RECORD(ListLink, struct LCB, ChildFcbLinks);
        if (Lcb->Reference)
            return STATUS_DEVICE_BUSY;
        if (Lcb->ChildFcb != Fcb || !Lcb->ParentFcb ||
            !Lcb->ParentFileInfo || !Lcb->ParentFileInfo->Dloc ||
            !Lcb->ParentFileInfo->RefCount ||
            !Lcb->ParentFileInfo->Dloc->LinkRefCount)
            return STATUS_FILE_CORRUPT_ERROR;
        Status = UDFPinTeardownParent(IrpContext, Vcb, Fcb, Plan, Lcb->ParentFcb);
        if (!NT_SUCCESS(Status))
            return Status;
        {
            ULONG ParentIndex;
            for (ParentIndex = 0; ParentIndex < Plan->Count; ParentIndex++) {
                PUDF_TEARDOWN_PARENT Parent = &Plan->Parents[ParentIndex];
                if (Parent->Fcb != Lcb->ParentFcb)
                    continue;
                Parent->LinkHolds++;
                /* Our temporary pin is not one of the original LCB holds.
                 * Reject corrupt aggregate counts before private cleanup. */
                UDFLockVcb(IrpContext, Vcb);
                if (Parent->Fcb->FcbReference <= Parent->LinkHolds)
                    Status = STATUS_FILE_CORRUPT_ERROR;
                UDFUnlockVcb(IrpContext, Vcb);
                break;
            }
            if (!NT_SUCCESS(Status))
                return Status;
        }
    }
    Link = Canonical;
    do {
        PUDF_FILE_INFO Parent = Link->ParentFile;
        if (Parent) {
            if (!Parent->Dloc)
                return STATUS_FILE_CORRUPT_ERROR;
            Status = UDFPinTeardownParent(IrpContext, Vcb, Fcb, Plan,
                                          Parent->Dloc->CommonFcb);
            if (!NT_SUCCESS(Status))
                return Status;
        }
        Link = Link->NextLinkedFile;
    } while (Link != Canonical);
    return STATUS_SUCCESS;
}

/* Called with table/VCB mutexes and the FCB resource held. The Dloc
 * claim closes the constructor/reference side of the same lifetime gate. */
static BOOLEAN
UDFClaimFcbTeardown(PVCB Vcb, PFCB Fcb)
{
    PUDF_FILE_INFO FileInfo = Fcb->FileInfo, Link;
    PUDF_DATALOC_INFO Dloc = FileInfo ? FileInfo->Dloc : NULL;
    BOOLEAN Available = TRUE;
    if (!Dloc || Fcb->TeardownInProgress || Fcb->ZeroDataFileObject ||
        Fcb->ZeroDataRetryThread || Fcb->FcbReference || Fcb->FcbCleanup ||
        Vcb->VcbReference == MAXULONG ||
        !IsListEmpty(&Fcb->ChildLcbQueue) ||
        (!IsListEmpty(&Fcb->PendingDeleteQueue) || !IsListEmpty(&Fcb->AbortedOpenQueue)))
        return FALSE;
    UDFAcquireResourceExclusive(&Vcb->DlocResource2, TRUE);
    if (Dloc->CreateRollbackOwner || Dloc->CreateConstructionOwner || Dloc->RetiredExtents || Dloc->CleanupOwner || Dloc->LinkRefCount || Dloc->StreamDeleteOwner ||
        Dloc->StreamDeleteContextOwner || Dloc->StreamCreate ||
        (Dloc->FE_Flags & UDF_FE_FLAG_UNDER_INIT) || Dloc->CommonFcb != Fcb)
        Available = FALSE;
    Link = FileInfo;
    if (Available) do {
        if (Link->RefCount || Link->OpenCount || Link->Constructing ||
            (Link->Fcb && Link->Fcb != Fcb)) {
            Available = FALSE;
            break;
        }
        Link = Link->NextLinkedFile;
    } while (Link && Link != FileInfo);
    if (!Link) Available = FALSE;
    if (Available) {
        Dloc->CleanupOwner = PsGetCurrentThread();
        Fcb->TeardownInProgress = TRUE;
        InterlockedIncrement(&Vcb->FcbTeardownCount);
        Vcb->VcbReference++;
    }
    UDFReleaseResource(&Vcb->DlocResource2);
    return Available;
}

/* The table entry is removed only after the cleanup decision. On refusal
 * the old canonical identity remains indexed and all its peer FIs are intact. */
static VOID
UDFFinishFcbTeardown(PIRP_CONTEXT IrpContext, PVCB Vcb, PFCB Fcb,
                     PUDF_DATALOC_INFO Dloc, ULONG CleanupResult)
{
    UDFLockFcbTable(IrpContext, Vcb);
    UDFLockVcb(IrpContext, Vcb);
    if (CleanupResult & UDF_FREE_FILEINFO) {
        if (Fcb->TeardownRetryPending) {
            Fcb->TeardownRetryPending = FALSE;
            ASSERT(Vcb->ClosedFcbRetryCount > 0);
            InterlockedDecrement(&Vcb->ClosedFcbRetryCount);
        }
        if (FlagOn(Fcb->FcbState, FCB_STATE_IN_FCB_TABLE)) {
            FCB_TABLE_ELEMENT Key;
            Key.FileId = Fcb->FileId;
            Key.Fcb = Fcb;
            RtlDeleteElementGenericTable(&Vcb->FcbTable, &Key);
            ClearFlag(Fcb->FcbState, FCB_STATE_IN_FCB_TABLE);
        }
    } else {
        Fcb->FileInfo->Fcb = Fcb;
        Fcb->FileInfo->Dloc->CommonFcb = Fcb;
    }
    if (!(CleanupResult & UDF_FREE_DLOC)) {
        UDFAcquireResourceExclusive(&Vcb->DlocResource2, TRUE);
        if (CleanupResult & UDF_FREE_FILEINFO) {
            PUDF_FILE_INFO Link = Dloc->LinkedFileInfo;
            if (Link) do {
                if (Link->Fcb == Fcb) Link->Fcb = NULL;
                /* Remaining aliases keep exact FID/dirty cache state. The
                 * explicit volume reaper must see them after canonical GC. */
                Link->CleanupPending = TRUE;
                Link = Link->NextLinkedFile;
            } while (Link && Link != Dloc->LinkedFileInfo);
        }
        Dloc->CleanupOwner = NULL;
        UDFReleaseResource(&Vcb->DlocResource2);
    }
    Fcb->TeardownInProgress = FALSE;
    ASSERT(Vcb->FcbTeardownCount > 0 && Vcb->VcbReference > 0);
    InterlockedDecrement(&Vcb->FcbTeardownCount);
    Vcb->VcbReference--;
    UDFUnlockVcb(IrpContext, Vcb);
    UDFUnlockFcbTable(IrpContext, Vcb);
}

_Requires_lock_held_(_Global_critical_region_)
VOID
UDFTeardownStructures(PIRP_CONTEXT IrpContext, PFCB StartingFcb,
                       BOOLEAN Recursive, PBOOLEAN RemovedStartingFcb)
{
    UDFTeardownStructuresEx(IrpContext, StartingFcb, Recursive, TRUE,
                             RemovedStartingFcb);
}

_Requires_lock_held_(_Global_critical_region_)
NTSTATUS
UDFTeardownStructuresEx(
    _In_ PIRP_CONTEXT IrpContext,
    _Inout_ PFCB StartingFcb,
    _In_ BOOLEAN Recursive,
    _In_ BOOLEAN WalkParents,
    _Out_ PBOOLEAN RemovedStartingFcb
    )
{
    PVCB Vcb = StartingFcb->Vcb;
    PFCB CurrentFcb = StartingFcb, NextFcb = NULL;
    PUDF_TEARDOWN_PLAN Plan = NULL;
    BOOLEAN CurrentAcquired = FALSE, VolumeAcquired = FALSE;
    BOOLEAN CleanupClaimed = FALSE, SetTeardownFlag = FALSE;
    NTSTATUS Status = STATUS_SUCCESS;

    ASSERT_EXCLUSIVE_FCB(StartingFcb);
    *RemovedStartingFcb = FALSE;
    if (InterlockedCompareExchange(&Vcb->RetainedOpenCount, 0, 0))
        return STATUS_FILE_CORRUPT_ERROR;
    if (!Recursive && WalkParents &&
        FlagOn(IrpContext->Flags, IRP_CONTEXT_FLAG_IN_TEARDOWN)) {
        UDFMarkTeardownRetry(IrpContext, Vcb, StartingFcb);
        return STATUS_DEVICE_BUSY;
    }

    /* Weak FI edges are cache pointers, not strong parent references. All
     * metadata cache collectors are serialized exclusively. From a held
     * child resource use try-only acquisition: never wait on a VCB upgrade.
     * A caller already owning the exclusive VCB lends it without recursion. */
    if (!ExIsResourceAcquiredExclusiveLite(&Vcb->VcbResource)) {
        if (!UDFAcquireVcbExclusive(IrpContext, Vcb, TRUE)) {
            UDFMarkTeardownRetry(IrpContext, Vcb, StartingFcb);
            return STATUS_DEVICE_BUSY;
        }
        VolumeAcquired = TRUE;
    }
    if (!Recursive && !FlagOn(IrpContext->Flags, IRP_CONTEXT_FLAG_IN_TEARDOWN)) {
        SetFlag(IrpContext->Flags, IRP_CONTEXT_FLAG_IN_TEARDOWN);
        SetTeardownFlag = TRUE;
    }
    _SEH2_TRY {
        while (CurrentFcb) {
            PUDF_FILE_INFO Canonical;
            PUDF_DATALOC_INFO Dloc;
            ULONG CleanupResult;
            NTSTATUS CleanupStatus;
            if (!IsListEmpty(&CurrentFcb->AbortedOpenQueue)) {
                Status = STATUS_DEVICE_BUSY;
                break; /* exact CCB/cache owner must finish before GC I/O */
            }
            if (CurrentFcb->FcbReference || CurrentFcb->FcbCleanup)
                break;
            if (CurrentFcb->TeardownInProgress || CurrentFcb->TeardownPlan) {
                PUDF_TEARDOWN_PLAN RetainedPlan = CurrentFcb->TeardownPlan;
                Status = RetainedPlan && !NT_SUCCESS(RetainedPlan->CleanupStatus) &&
                    RetainedPlan->CleanupStatus != STATUS_DEVICE_BUSY ?
                    RetainedPlan->CleanupStatus : STATUS_FILE_CORRUPT_ERROR;
                break; /* never reinterpret a partly retired FI */
            }
            if (!IsListEmpty(&CurrentFcb->PendingDeleteQueue)) {
                Status = UDFProcessPendingDeletes(IrpContext, CurrentFcb, FALSE);
                if (!NT_SUCCESS(Status))
                    break;
            }
            Status = UDFPrepareTeardownPlan(IrpContext, Vcb, CurrentFcb, &Plan);
            if (!NT_SUCCESS(Status))
                break;
            Status = UDFPrepareTeardownFile(IrpContext, Vcb, CurrentFcb);
            if (!NT_SUCCESS(Status)) {
                UDFRememberMetadataWritebackError(IrpContext, Vcb,
                                                  CurrentFcb->FileInfo, Status);
                break;
            }
            UDFLockFcbTable(IrpContext, Vcb);
            UDFLockVcb(IrpContext, Vcb);
            CleanupClaimed = UDFClaimFcbTeardown(Vcb, CurrentFcb);
            UDFUnlockVcb(IrpContext, Vcb);
            UDFUnlockFcbTable(IrpContext, Vcb);
            if (!CleanupClaimed) {
                Status = STATUS_DEVICE_BUSY;
                break;
            }
            Canonical = CurrentFcb->FileInfo;
            Dloc = Canonical->Dloc;
            CurrentFcb->TeardownPlan = Plan;
            Plan->CleanupDloc = Dloc;
            if (!NT_SUCCESS(Dloc->ZeroDataError) && Dloc->DeleteCommitted &&
                !Dloc->NamespaceScratch && !Dloc->NamespaceTarget &&
                !Dloc->NamespaceScratchOwner && !Dloc->StreamDeleteOwner &&
                !Dloc->StreamDeleteContextOwner && !Dloc->StreamCreate &&
                FlagOn(CurrentFcb->FcbState, UDF_FCB_DELETED) &&
                !CurrentFcb->ZeroDataFileObject &&
                !CurrentFcb->FcbNonpaged->SegmentObject.DataSectionObject &&
                !CurrentFcb->FcbNonpaged->SegmentObject.ImageSectionObject &&
                !CurrentFcb->FcbNonpaged->SegmentObject.SharedCacheMap &&
                !UDFGetFileNameLinkCount(Canonical)) {
                /* The claim already proved no FI/open/child owners. The
                 * namespace passed its deletion barrier and MM/cache is gone.
                 * Discarding unreachable failed data is not a successful flush
                 * or a repair of a live file. Keep unrelated readonly/error. */
                ASSERT(Vcb->FailedZeroDataCount > 0);
                InterlockedExchange(&Dloc->ZeroDataError, STATUS_SUCCESS);
                InterlockedDecrement(&Vcb->FailedZeroDataCount);
                Dloc->ZeroDataCanRetry = FALSE;
                Dloc->ZeroDataLastError = STATUS_SUCCESS;
                Dloc->ZeroDataStart = Dloc->ZeroDataEnd = 0;
                Dloc->FE_Flags &= ~UDF_FE_FLAG_CACHED_DATA_PENDING;
            }
            if (CurrentFcb->FcbState & UDF_FCB_DELETED) {
                if (!Dloc->DeleteCommitted) {
                    Status = STATUS_FILE_CORRUPT_ERROR;
                    break;
                }
                /* Confirmed unreachable FE; no writeback of a freed name. */
                Dloc->FE_Flags &= ~UDF_FE_FLAG_FE_MODIFIED;
                Dloc->DataLoc.Modified = FALSE;
                Dloc->DataLoc.Flags &= ~EXTENT_FLAG_PREALLOCATED;
                Dloc->AllocLoc.Modified = FALSE;
                Dloc->FELoc.Modified = FALSE;
                if (Canonical->ParentFile && Canonical->ParentFile->Dloc) {
                    PDIR_INDEX_ITEM Entry = UDFDirIndex(
                        Canonical->ParentFile->Dloc->DirIndex, Canonical->Index);
                    if (Entry) Entry->FI_Flags &= ~UDF_FI_FLAG_FI_MODIFIED;
                }
            }
            /* Keep ALL original LCBs and exact parent holds during cleanup.
             * A normal refusal restores the indexed canonical attachment. */
            Canonical->Fcb = NULL;
            Dloc->CommonFcb = NULL;
            Plan->CleanupEntered = TRUE;
            CleanupResult = UDFCleanUpFileForFcb(Vcb, Canonical, &CleanupStatus);
            Plan->CleanupResult = CleanupResult;
            Plan->CleanupStatus = CleanupStatus;
            Plan->CleanupAccepted = FlagOn(CleanupResult, UDF_FREE_FILEINFO);
            if (!Plan->CleanupAccepted) {
                if (CleanupStatus != STATUS_SUCCESS && CleanupStatus != STATUS_DEVICE_BUSY) {
                    /* A real/uncertain cleanup failure is not a restorable
                     * cache refusal. Keep the indexed FCB gate, plan and
                     * original parent pins; expose the actual first error. */
                    Status = CleanupStatus;
                    UDFRememberMetadataWritebackError(IrpContext, Vcb, Canonical, Status);
                    break;
                }
                UDFFinishFcbTeardown(IrpContext, Vcb, CurrentFcb, Dloc, CleanupResult);
                CleanupClaimed = FALSE;
                CurrentFcb->TeardownPlan = NULL;
                Status = STATUS_DEVICE_BUSY;
                break;
            }
            /* This phase is memory/owner retirement only. UDFCloseFile__
             * would add fallible parent metadata I/O after names were lost.
             * A checked invariant failure keeps the plan and remaining LCBs
             * behind the gate. Consumed holds are explicitly recorded. */
            while (!IsListEmpty(&CurrentFcb->ParentLcbQueue)) {
                PLCB Lcb = CONTAINING_RECORD(CurrentFcb->ParentLcbQueue.Flink,
                                             struct LCB, ChildFcbLinks);
                PFCB Parent = Lcb->ParentFcb;
                if (Lcb->ParentFileInfo) {
                    /* Replacing Close with non-I/O dereference must not
                     * lose an exact parent alias's later writeback owner. */
                    Lcb->ParentFileInfo->CleanupPending = TRUE;
                    Status = UDFDereferenceFileChecked(Vcb, Lcb->ParentFileInfo);
                    if (!NT_SUCCESS(Status))
                        break;
                    Lcb->ParentFileInfo = NULL;
                }
                UDFLockVcb(IrpContext, Vcb);
                _SEH2_TRY {
                    UDFRemovePrefix(IrpContext, Lcb);
                    ASSERT(Parent->FcbReference);
                    Parent->FcbReference--; /* exact original LCB hold */
                } _SEH2_FINALLY {
                    UDFUnlockVcb(IrpContext, Vcb);
                } _SEH2_END;
            }
            if (!NT_SUCCESS(Status))
                break;
            UDFFinishFcbTeardown(IrpContext, Vcb, CurrentFcb, Dloc, CleanupResult);
            CleanupClaimed = FALSE;
            CurrentFcb->TeardownPlan = NULL;
            if (CurrentFcb == StartingFcb)
                *RemovedStartingFcb = TRUE;
            UDFReleaseFcb(IrpContext, CurrentFcb);
            CurrentFcb->FileInfo = NULL;
            CurrentFcb->ParentFcb = NULL;
            UDFDeleteFcb(IrpContext, CurrentFcb);
            MyFreePool__(Canonical);
            CurrentFcb = NULL;
            CurrentAcquired = FALSE;
            NextFcb = NULL;
            UDFReleaseTeardownPlan(IrpContext, Vcb, Plan, FALSE,
                                   WalkParents && !Recursive, &NextFcb);
            Plan = NULL;
            CurrentFcb = NextFcb;
            CurrentAcquired = (CurrentFcb != NULL);
        }
    } _SEH2_FINALLY {
        if (CleanupClaimed && Plan && !Plan->CleanupEntered &&
            !_SEH2_AbnormalTermination()) {
            /* Rejected before destructive cleanup: attachments are intact. */
            UDFFinishFcbTeardown(IrpContext, Vcb, CurrentFcb,
                                 Plan->CleanupDloc, UDF_FREE_NOTHING);
            CurrentFcb->TeardownPlan = NULL;
            CleanupClaimed = FALSE;
        }
        if (CleanupClaimed) {
            /* Unknown/accepted partial retirement is not an ordinary retry.
             * Keep the indexed gate, VCB claim and exact parent pins. */
            if (Plan && NT_SUCCESS(Plan->CleanupStatus))
                Plan->CleanupStatus = NT_SUCCESS(Status) ? STATUS_UNSUCCESSFUL : Status;
            Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
            UDFRememberMetadataWritebackError(IrpContext, Vcb, NULL,
                NT_SUCCESS(Status) ? STATUS_UNSUCCESSFUL : Status);
        }
        if (CurrentFcb && !*RemovedStartingFcb)
            UDFMarkTeardownRetry(IrpContext, Vcb, CurrentFcb);
        else if (CurrentFcb && CurrentAcquired)
            UDFMarkTeardownRetry(IrpContext, Vcb, CurrentFcb);
        NextFcb = NULL;
        UDFReleaseTeardownPlan(IrpContext, Vcb, Plan, CleanupClaimed,
                               FALSE, &NextFcb);
        if (CurrentAcquired && CurrentFcb)
            UDFReleaseFcb(IrpContext, CurrentFcb);
        if (SetTeardownFlag)
            ClearFlag(IrpContext->Flags, IRP_CONTEXT_FLAG_IN_TEARDOWN);
        if (VolumeAcquired)
            UDFReleaseVcb(IrpContext, Vcb);
    } _SEH2_END;
    return Status;
}

PFCB
UDFLookupFcbTable (
    _In_ PIRP_CONTEXT IrpContext,
    _In_ PVCB Vcb,
    _In_ FILE_ID FileId
    )

/*++

Routine Description:

    This routine will look through the Fcb table looking for a matching
    entry.

Arguments:

    Vcb - Vcb for this volume.

    FileId - This is the key value to use for the search.

Return Value:

    PFCB - A pointer to the matching entry or NULL otherwise.

--*/

{
    FCB_TABLE_ELEMENT Key;
    PFCB_TABLE_ELEMENT Hit;
    PFCB ReturnFcb = NULL;

    PAGED_CODE();

    Key.FileId = FileId;

    Hit = (PFCB_TABLE_ELEMENT)RtlLookupElementGenericTable(&Vcb->FcbTable, &Key);

    if (Hit != NULL) {

        ReturnFcb = Hit->Fcb;
    }

    return ReturnFcb;

    UNREFERENCED_PARAMETER( IrpContext );
}

PFCB
UDFGetNextFcb  (
    _In_ PIRP_CONTEXT IrpContext,
    _In_ PVCB Vcb,
    _In_ PVOID *RestartKey
    )

/*++

Routine Description:

    This routine will enumerate through all of the Fcb's in the Fcb table.

Arguments:

    Vcb - Vcb for this volume.

    RestartKey - This value is used by the table package to maintain
        its position in the enumeration.  It is initialized to NULL
        for the first search.

Return Value:

    PFCB - A pointer to the next fcb or NULL if the enumeration is
        completed

--*/

{
    PFCB Fcb;

    PAGED_CODE();

    UNREFERENCED_PARAMETER( IrpContext );
    
    do {
        Fcb = (PFCB)RtlEnumerateGenericTableWithoutSplaying(&Vcb->FcbTable, RestartKey);
        if (!Fcb) break;
        Fcb = ((PFCB_TABLE_ELEMENT)Fcb)->Fcb;
    } while (Fcb->TeardownInProgress);

    return Fcb;
}

/*************************************************************************
*
* Function: UDFCreateFcb()
*
* Description:
*   Allocate a new FCB structure to represent an open on-disk object.
*   Also initialize the FCB structure to NULL.
*
* Expected Interrupt Level (for execution) :
*
*  IRQL_PASSIVE_LEVEL
*
* Return Value: A pointer to the FCB structure OR NULL.
*
*************************************************************************/

PFCB
UDFCreateFcb(
    _In_ PIRP_CONTEXT IrpContext,
    _In_ FILE_ID FileId,
    _In_ NODE_TYPE_CODE NodeTypeCode,
    _Out_opt_ PBOOLEAN FcbExisted
    )

/*++

Routine Description:

    This routine is called to find the Fcb for the given FileId.  We will
    look this up first in the Fcb table and if not found we will create
    an Fcb.  We don't initialize it or insert it into the FcbTable in this
    routine.

    This routine is called while the Vcb is locked.

Arguments:

    FileId - This is the Id for the target Fcb.

    NodeTypeCode - Node type for this Fcb if we need to create.

    FcbExisted - If specified, we store whether the Fcb existed.

Return Value:

    PFCB - The Fcb found in the table or created if needed.

--*/

{
    PFCB NewFcb = NULL;
    BOOLEAN LocalFcbExisted;
    BOOLEAN Allocated = FALSE;

    PAGED_CODE();

    _SEH2_TRY {

        // Use the local boolean if one was not passed in.

        if (!ARGUMENT_PRESENT(FcbExisted)) {

            FcbExisted = &LocalFcbExisted;
        }

        *FcbExisted = FALSE;

        // Maybe this is already in the table.

        NewFcb = UDFLookupFcbTable(IrpContext, IrpContext->Vcb, FileId);

        // If not then create the Fcb is requested by our caller.

        if (NewFcb == NULL) {

            // Allocate and initialize the structure depending on the
            // type code.

            switch (NodeTypeCode) {

            case UDF_NODE_TYPE_INDEX:

                NewFcb = UDFAllocateFcbIndex();
                if (!NewFcb) _SEH2_LEAVE;
                Allocated = TRUE;

                RtlZeroMemory(NewFcb, SIZEOF_FCB_INDEX);

                NewFcb->NodeIdentifier.NodeByteSize = SIZEOF_FCB_INDEX;

                break;

            case UDF_NODE_TYPE_DATA:

                NewFcb = UDFAllocateFcbData();
                if (!NewFcb) _SEH2_LEAVE;
                Allocated = TRUE;

                RtlZeroMemory(NewFcb, SIZEOF_FCB_DATA);

                NewFcb->NodeIdentifier.NodeByteSize = SIZEOF_FCB_DATA;

                break;

            default:

#pragma prefast( suppress: __WARNING_USE_OTHER_FUNCTION, "This is a bug." )   
                UDFBugCheck(0, 0, 0);
            }

            // Now do the common initialization.

            NewFcb->NodeIdentifier.NodeTypeCode = NodeTypeCode;

            NewFcb->Vcb = IrpContext->Vcb;
            NewFcb->FileId = FileId;

            // A failed root-directory open can delete this FCB before
            // UDFInitializeFCB.  Its teardown requires valid empty queues.
            InitializeListHead(&NewFcb->ParentLcbQueue);
            InitializeListHead(&NewFcb->ChildLcbQueue);
            InitializeListHead(&NewFcb->PendingDeleteQueue);
            InitializeListHead(&NewFcb->AbortedOpenQueue);

            //  Now create the non-paged section object.

            NewFcb->FcbNonpaged = UDFCreateFcbNonpaged(IrpContext);
            if (!NewFcb->FcbNonpaged) {
                if (NodeTypeCode == UDF_NODE_TYPE_INDEX)
                    UDFDeallocateFcbIndex(NewFcb);
                else
                    UDFDeallocateFcbData(NewFcb);
                NewFcb = NULL;
                _SEH2_LEAVE;
            }

            *FcbExisted = FALSE;

        } else {

            *FcbExisted = TRUE;
        }

    } _SEH2_FINALLY{

        if (_SEH2_AbnormalTermination() && Allocated) {

            if (NewFcb && NewFcb->FcbNonpaged) {

                UDFDeleteFcbNonpaged(IrpContext, NewFcb->FcbNonpaged);
            }

            if (NewFcb) {
                if (NodeTypeCode == UDF_NODE_TYPE_INDEX)
                    UDFDeallocateFcbIndex(NewFcb);
                else
                    UDFDeallocateFcbData(NewFcb);
                NewFcb = NULL;
            }
        }

    } _SEH2_END;

    return NewFcb;
}

/*************************************************************************
*
* Function: UDFInitializeFCB()
*
* Description:
*   Initialize a new FCB structure and also the sent-in file object
*   (if supplied)
*
* Expected Interrupt Level (for execution) :
*
*  IRQL_PASSIVE_LEVEL
*
* Return Value: None
*
*************************************************************************/
NTSTATUS
UDFInitializeFCB(
    IN PFCB             Fcb,            // FCB structure to be initialized
    IN PVCB             Vcb,            // logical volume (VCB) pointer
    IN ULONG            Flags)          // is this a file/directory, etc.
{
    ASSERT_LOCKED_VCB(Vcb);

    // Fill NT required Fcb part

    ASSERT(!Fcb->Header.Resource);
    Fcb->Header.Resource = &Fcb->FcbNonpaged->FcbResource;
    Fcb->Header.PagingIoResource = &Fcb->FcbNonpaged->FcbPagingIoResource;
    InitializeListHead(&Fcb->EofListHead);
    FsRtlSetupAdvancedHeader(&Fcb->Header, &Fcb->FcbNonpaged->AdvancedFcbHeaderMutex);
    Fcb->FileLock = NULL;

    Fcb->FcbState = Flags;

    // NOTE: FCB is NOT added to FcbTable here.
    // Caller is responsible for calling UDFInsertFcbIntoTable after
    // FCB is fully initialized.

    // initialize the various list heads
    InitializeListHead(&Fcb->ParentLcbQueue);
    InitializeListHead(&Fcb->ChildLcbQueue);
    InitializeListHead(&Fcb->PendingDeleteQueue);
    InitializeListHead(&Fcb->AbortedOpenQueue);

    // Splay tree roots for fast child lookup by name
    Fcb->ExactCaseRoot = NULL;
    Fcb->IgnoreCaseRoot = NULL;
    Fcb->ShortNameRoot = NULL;

    Fcb->FcbReference = 0;
    Fcb->FcbCleanup = 0;

    // Initialize file name cache synchronization
    Fcb->FcbLockThread = NULL;
    Fcb->FcbLockCount = 0;

    Fcb->Vcb = Vcb;

    return STATUS_SUCCESS;
} // end UDFInitializeFCB()

RTL_GENERIC_COMPARE_RESULTS
NTAPI /* ReactOS Change: GCC Does not support STDCALL by default */
UDFFcbTableCompare (
    _In_ PRTL_GENERIC_TABLE FcbTable,
    _In_ PVOID Fid1,
    _In_ PVOID Fid2
    )

/*++

Routine Description:

    This routine is the Cdfs compare routine called by the generic table package.
    If will compare the two File Id values and return a comparison result.

Arguments:

    FcbTable - This is the table being searched.

    Fid1 - First key value.

    Fid2 - Second key value.

Return Value:

    RTL_GENERIC_COMPARE_RESULTS - The results of comparing the two
        input structures

--*/

{
    FILE_ID Id1, Id2;
    PAGED_CODE();

    Id1 = *((FILE_ID UNALIGNED *) Fid1);
    Id2 = *((FILE_ID UNALIGNED *) Fid2);

    if (Id1.QuadPart < Id2.QuadPart) {

        return GenericLessThan;

    } else if (Id1.QuadPart > Id2.QuadPart) {

        return GenericGreaterThan;

    } else {

        return GenericEqual;
    }

    UNREFERENCED_PARAMETER( FcbTable );
}

PVOID
NTAPI /* ReactOS Change: GCC Does not support STDCALL by default */
UDFAllocateFcbTable (
    _In_ PRTL_GENERIC_TABLE FcbTable,
    _In_ CLONG ByteSize
    )

/*++

Routine Description:

    This is a generic table support routine to allocate memory

Arguments:

    FcbTable - Supplies the generic table being used

    ByteSize - Supplies the number of bytes to allocate

Return Value:

    PVOID - Returns a pointer to the allocated data

--*/

{
    PAGED_CODE();
    
    UNREFERENCED_PARAMETER(FcbTable);

    return FsRtlAllocatePoolWithTag(PagedPool, ByteSize, TAG_FCB_TABLE);
}

VOID
NTAPI /* ReactOS Change: GCC Does not support STDCALL by default */
UDFDeallocateFcbTable (
    _In_ PRTL_GENERIC_TABLE FcbTable,
    _In_ __drv_freesMem(Mem) _Post_invalid_ PVOID Buffer
    )
/*++

Routine Description:

    This is a generic table support routine that deallocates memory

Arguments:

    FcbTable - Supplies the generic table being used

    Buffer - Supplies the buffer being deallocated

Return Value:

    None.

--*/

{
    PAGED_CODE();

    UDFFreePool(&Buffer);

    UNREFERENCED_PARAMETER( FcbTable );
}

/*************************************************************************
*
* Function: UDFInitializeVCB()
*
* Description:
*   Perform the initialization for a VCB structure.
*
* Expected Interrupt Level (for execution) :
*
*   IRQL PASSIVE_LEVEL
*
* Return Value: status
*
*************************************************************************/
VOID
UDFInitializeVCB(
    _In_ PIRP_CONTEXT IrpContext,
    _Inout_ PVCB Vcb,
    _In_ PDEVICE_OBJECT TargetDeviceObject,
    _In_ PVPB Vpb,
    _In_ PDISK_GEOMETRY DiskGeometry,
    _In_ ULONG MediaChangeCount
    )
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(IrpContext);

    // We start by first zeroing out all of the VCB, this will guarantee
    // that any stale data is wiped clean.

    RtlZeroMemory(Vcb, sizeof(struct VCB));
    InitializeListHead(&Vcb->CreateRollbackQueue);
    KeInitializeSpinLock(&Vcb->MetadataViewLock);

    // Set the proper node type code and node byte size.

    Vcb->NodeIdentifier.NodeTypeCode = UDF_NODE_TYPE_VCB;
    Vcb->NodeIdentifier.NodeByteSize = sizeof(struct VCB);

    // Initialize the notify sync mutex. FsRtlNotifyInitializeSync can raise.

    FsRtlNotifyInitializeSync(&Vcb->NotifySync);

    _SEH2_TRY {

        ExInitializeResourceLite(&Vcb->VcbResource);
        ExInitializeResourceLite(&Vcb->BitMapResource1);

        ExInitializeResourceLite(&Vcb->DlocResource);
        ExInitializeResourceLite(&Vcb->DlocResource2);
        ExInitializeResourceLite(&Vcb->FlushResource);
        ExInitializeResourceLite(&Vcb->PreallocResource);
        ExInitializeFastMutex(&Vcb->VcbMutex);
        ExInitializeFastMutex(&Vcb->FcbTableMutex);

        // Initialize the generic Fcb Table.

        RtlInitializeGenericTable(&Vcb->FcbTable,
                                  (PRTL_GENERIC_COMPARE_ROUTINE)UDFFcbTableCompare,
                                  (PRTL_GENERIC_ALLOCATE_ROUTINE)UDFAllocateFcbTable,
                                  (PRTL_GENERIC_FREE_ROUTINE)UDFDeallocateFcbTable,
                                  NULL);

        // Pick up a VPB right now so we know we can pull this filesystem stack
        // off of the storage stack on demand.  This can raise - if it does,  
        // uninitialize the notify structures before returning.

        Vcb->SwapVpb = (PVPB)FsRtlAllocatePoolWithTag(NonPagedPoolNx, sizeof( VPB), TAG_VPB);

        RtlZeroMemory(Vcb->SwapVpb, sizeof(VPB));

        // We know the target device object.
        // Note that this is not neccessarily a pointer to the actual
        // physical/virtual device on which the logical volume should
        // be mounted. This is actually a pointer to either the actual
        // (real) device or to any device object that may have been
        // attached to it. Any IRPs that we send down should be sent to this
        // device object. However, the "real" physical/virtual device object
        // on which we perform our mount operation can be determined from the
        // RealDevice field in the VPB sent to us.
        Vcb->TargetDeviceObject = TargetDeviceObject;

        // We also have the VPB pointer. This was obtained from the
        // Parameters.MountVolume.Vpb field in the current I/O stack location
        // for the mount IRP.
        Vcb->Vpb = Vpb;

        //  Set the removable media flag based on the real device's
        //  characteristics
        if (Vpb->RealDevice->Characteristics & FILE_REMOVABLE_MEDIA) {

            Vcb->VcbState |= VCB_STATE_REMOVABLE_MEDIA;
        }

        // Initialize the list anchor (head) for some lists in this VCB.
        InitializeListHead(&Vcb->NextNotifyIRP);

        // Intilize FCB for this VCB

        // Refererence the Vcb for two reasons.  The first is a reference
        // that prevents the Vcb from going away on the last close unless
        // dismount has already occurred.  The second is to make sure
        // we don't go into the dismount path on any error during mount
        // until we get to the Mount cleanup.

        Vcb->VcbResidualReference = UDFS_BASE_RESIDUAL_REFERENCE;
        Vcb->VcbResidualUserReference = UDFS_BASE_RESIDUAL_USER_REFERENCE;

        Vcb->VcbReference = 1 + Vcb->VcbResidualReference;

        // Create a stream file object for this volume.
        //Vcb->PtrStreamFileObject = IoCreateStreamFileObject(NULL,
        //                                            Vcb->Vpb->RealDevice);
        //ASSERT(Vcb->PtrStreamFileObject);

        // Initialize some important fields in the newly created file object.
        //Vcb->PtrStreamFileObject->FsContext = (PVOID)Vcb;
        //Vcb->PtrStreamFileObject->FsContext2 = NULL;
        //Vcb->PtrStreamFileObject->SectionObjectPointer = &(Vcb->SectionObject);

        //Vcb->PtrStreamFileObject->Vpb = PtrVPB;

        // Insert this Vcb record on the CdData.VcbQueue.

        ASSERT_EXCLUSIVE_CDDATA;
        InsertTailList(&(UdfData.VcbQueue), &(Vcb->VcbLinks));

        // Initialize caching for the stream file object.
        //CcInitializeCacheMap(Vcb->PtrStreamFileObject, (PCC_FILE_SIZES)(&(Vcb->AllocationSize)),
        //                            TRUE,       // We will use pinned access.
        //                            &(UDFGlobalData.CacheMgrCallBacks), Vcb);

        Vcb->SectorSize = DiskGeometry->BytesPerSector;
        Vcb->SectorShift = UDFHighBit(DiskGeometry->BytesPerSector);
        Vcb->MediaChangeCount = MediaChangeCount;

    } _SEH2_FINALLY {

        if (_SEH2_AbnormalTermination()) {

            FsRtlNotifyUninitializeSync(&Vcb->NotifySync);
        }
    } _SEH2_END;
} // end UDFInitializeVCB()

NTSTATUS
UDFCompleteMount(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb
    )
{
    NTSTATUS Status;
    UNICODE_STRING LocalPath;
    ULONG LastSector = 0;
    BOOLEAN UnlockVcb = FALSE;
    FILE_ID FileId;
    RtlZeroMemory(&FileId, sizeof(FILE_ID));

    PAGED_CODE();

    UDFPrint(("UDFCompleteMount:\n"));

    // Use a try-finally to facilitate cleanup.

    _SEH2_TRY {

        Vcb->ZBuffer = (PCHAR)DbgAllocatePoolWithTag(NonPagedPool, max(Vcb->SectorSize, PAGE_SIZE), 'zNWD');

        if (!Vcb->ZBuffer) {

            try_return(Status = STATUS_INSUFFICIENT_RESOURCES);
        }

        RtlZeroMemory(Vcb->ZBuffer, Vcb->SectorSize);

        // Create the root index and reference it in the Vcb.

        Vcb->RootIndexFcb = UDFCreateFcb(IrpContext, FileId, UDF_NODE_TYPE_INDEX, NULL);

        if (!Vcb->RootIndexFcb) {

            try_return(Status = STATUS_INSUFFICIENT_RESOURCES);
        }

        //  Create the File id by hand for this Fcb.

        Vcb->RootIndexFcb->FileId = UdfGetFidFromLbAddr(Vcb->RootLbAddr);
        SetFlag(Vcb->RootIndexFcb->FileId.HighPart, FID_DIR_MASK);

        Vcb->RootIndexFcb->FileInfo = (PUDF_FILE_INFO)MyAllocatePool__(NonPagedPool,sizeof(UDF_FILE_INFO));

        if (!Vcb->RootIndexFcb->FileInfo) {

            UDFDeleteFcb(IrpContext, Vcb->RootIndexFcb);
            Vcb->RootIndexFcb = NULL;
            try_return(Status = STATUS_INSUFFICIENT_RESOURCES);
        }

        UDFPrint(("UDFCompleteMount: open Root Dir\n"));
        // Open Root Directory
        Status = UDFOpenRootFile__(IrpContext, Vcb, &Vcb->RootLbAddr, Vcb->RootIndexFcb->FileInfo);

        if (!NT_SUCCESS(Status)) {

            if (UDFCleanUpFile__(Vcb, Vcb->RootIndexFcb->FileInfo) & UDF_FREE_FILEINFO) {
                MyFreePool__(Vcb->RootIndexFcb->FileInfo);
                Vcb->RootIndexFcb->FileInfo = NULL;
                UDFDeleteFcb(IrpContext, Vcb->RootIndexFcb);
                Vcb->RootIndexFcb = NULL;
            }
            try_return(Status);
        }

        Vcb->RootIndexFcb->FileInfo->Fcb = Vcb->RootIndexFcb;

        if (!Vcb->RootIndexFcb->FileInfo->Dloc->CommonFcb) {
            Vcb->RootIndexFcb->FileInfo->Dloc->CommonFcb = Vcb->RootIndexFcb;
        }

        UDFLockVcb(IrpContext, Vcb);
        UnlockVcb = TRUE;

        Status = UDFInitializeFCB(Vcb->RootIndexFcb, Vcb, UDF_FCB_ROOT_DIRECTORY | UDF_FCB_DIRECTORY);
        if (Status == STATUS_SUCCESS)
            Status = UDFInsertFcbIntoTable(IrpContext, Vcb->RootIndexFcb);

        if (Status != STATUS_SUCCESS) {

            /* Resources may already be initialized, but neither an indexed
             * root nor its system reference counts were admitted. */
            Vcb->RootIndexFcb->FcbCleanup = 0;
            Vcb->RootIndexFcb->FcbReference = 0;

            /* No indexed FCB was published. Drop only the mount's open
             * hold; residual teardown retains an incomplete cleanup. */
            Vcb->RootIndexFcb->FileInfo->Fcb = NULL;
            if (Vcb->RootIndexFcb->FileInfo->Dloc &&
                Vcb->RootIndexFcb->FileInfo->Dloc->CommonFcb == Vcb->RootIndexFcb)
                Vcb->RootIndexFcb->FileInfo->Dloc->CommonFcb = NULL;
            UDFUnlockVcb(IrpContext, Vcb);
            UnlockVcb = FALSE;
            UDFReleaseInternalFile(IrpContext, Vcb, &Vcb->RootIndexFcb->FileInfo);
            if (!Vcb->RootIndexFcb->FileInfo) {
                UDFDeleteFcb(IrpContext, Vcb->RootIndexFcb);
                Vcb->RootIndexFcb = NULL;
            }
            try_return(Status);
        }

        // Initialization and exact FcbTable admission both succeeded.

        // this is a part of UDF_RESIDUAL_REFERENCE
        InterlockedIncrement((PLONG)&Vcb->VcbReference);
        Vcb->RootIndexFcb->FcbCleanup = 1;
        Vcb->RootIndexFcb->FcbReference = 1;
        Vcb->RootIndexFcb->SystemReferenceHeld = TRUE;

        UDFGetFileXTime(Vcb->RootIndexFcb->FileInfo,
                      &(Vcb->RootIndexFcb->CreationTime.QuadPart),
                      &(Vcb->RootIndexFcb->LastAccessTime.QuadPart),
                      &(Vcb->RootIndexFcb->ChangeTime.QuadPart),
                      &(Vcb->RootIndexFcb->LastWriteTime.QuadPart) );

        if (Vcb->SysStreamLbAddr.logicalBlockNum) {
            Vcb->SysSDirFileInfo = (PUDF_FILE_INFO)MyAllocatePool__(NonPagedPool,sizeof(UDF_FILE_INFO));
            if (!Vcb->SysSDirFileInfo) {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                goto unwind_1;
            }
            // Open System SDir Directory
            Status = UDFOpenRootFile__(IrpContext, Vcb, &Vcb->SysStreamLbAddr, Vcb->SysSDirFileInfo);
            if (!NT_SUCCESS(Status)) {
                if (UDFCleanUpFile__(Vcb, Vcb->SysSDirFileInfo) & UDF_FREE_FILEINFO) {
                    MyFreePool__(Vcb->SysSDirFileInfo);
                    Vcb->SysSDirFileInfo = NULL;
                }
                goto unwind_1;
            } else {
                Vcb->SysSDirFileInfo->Dloc->DataLoc.Flags |= EXTENT_FLAG_VERIFY;
            }
        }

        // Open Unallocatable space stream
        // Generally, it should be placed in SystemStreamDirectory, but some
        // stupid apps think that RootDirectory is much better place.... :((
        RtlInitUnicodeString(&LocalPath, UDF_FN_NON_ALLOCATABLE);
        Status = UDFOpenFile__(IrpContext, Vcb, FALSE, TRUE, &LocalPath, Vcb->RootIndexFcb->FileInfo, &Vcb->NonAllocFileInfo, NULL);

        if (!NT_SUCCESS(Status) && (Status != STATUS_OBJECT_NAME_NOT_FOUND)) {

    //unwind_2:
            if (UDFCleanUpFile__(Vcb, Vcb->NonAllocFileInfo) & UDF_FREE_FILEINFO) {
                MyFreePool__(Vcb->NonAllocFileInfo);
                Vcb->NonAllocFileInfo = NULL;
            }
            // this was a part of UDF_RESIDUAL_REFERENCE
            InterlockedDecrement((PLONG)&Vcb->VcbReference);
    unwind_1:

            // UDFCloseResidual() will clean up everything

            try_return(Status);
        }

        /* process Non-allocatable */
        if (NT_SUCCESS(Status)) {
            if (!Vcb->SparingTable && !Vcb->Vat && !Vcb->CDR_Mode)
                UDFMarkSpaceAsXXX(Vcb, Vcb->NonAllocFileInfo->Dloc, Vcb->NonAllocFileInfo->Dloc->DataLoc.Mapping, AS_USED); // used
            UDFDirIndex(UDFGetDirIndexByFileInfo(Vcb->NonAllocFileInfo), Vcb->NonAllocFileInfo->Index)->FI_Flags |= UDF_FI_FLAG_FI_INTERNAL;
        } else {
            /* try to read Non-allocatable from alternate locations */
            RtlInitUnicodeString(&LocalPath, UDF_FN_NON_ALLOCATABLE_2);
            Status = UDFOpenFile__(IrpContext, Vcb, FALSE, TRUE, &LocalPath, Vcb->RootIndexFcb->FileInfo, &(Vcb->NonAllocFileInfo), NULL);
            if (!NT_SUCCESS(Status) && (Status != STATUS_OBJECT_NAME_NOT_FOUND)) {
                goto unwind_1;
            }
            if (NT_SUCCESS(Status)) {
                if (!Vcb->SparingTable && !Vcb->Vat && !Vcb->CDR_Mode)
                    UDFMarkSpaceAsXXX(Vcb, Vcb->NonAllocFileInfo->Dloc, Vcb->NonAllocFileInfo->Dloc->DataLoc.Mapping, AS_USED); // used
                UDFDirIndex(UDFGetDirIndexByFileInfo(Vcb->NonAllocFileInfo), Vcb->NonAllocFileInfo->Index)->FI_Flags |= UDF_FI_FLAG_FI_INTERNAL;
            } else
            if (Vcb->SysSDirFileInfo) {
                RtlInitUnicodeString(&LocalPath, UDF_SN_NON_ALLOCATABLE);
                Status = UDFOpenFile__(IrpContext, Vcb, FALSE, TRUE, &LocalPath, Vcb->SysSDirFileInfo , &(Vcb->NonAllocFileInfo), NULL);
                if (!NT_SUCCESS(Status) && (Status != STATUS_OBJECT_NAME_NOT_FOUND)) {
                    goto unwind_1;
                }
                if (NT_SUCCESS(Status)) {
                    if (!Vcb->SparingTable && !Vcb->Vat && !Vcb->CDR_Mode)
                        UDFMarkSpaceAsXXX(Vcb, Vcb->NonAllocFileInfo->Dloc, Vcb->NonAllocFileInfo->Dloc->DataLoc.Mapping, AS_USED); // used
    //                    UDFDirIndex(UDFGetDirIndexByFileInfo(Vcb->NonAllocFileInfo), Vcb->NonAllocFileInfo->Index)->FI_Flags |= UDF_FI_FLAG_FI_INTERNAL;
                } else {
                    Status = STATUS_SUCCESS;
                }
            } else {
                Status = STATUS_SUCCESS;
            }
        }

        /* Read SN UID mapping */
        if (Vcb->SysSDirFileInfo) {

            RtlInitUnicodeString(&LocalPath, UDF_SN_UID_MAPPING);

            Status = UDFOpenFile__(IrpContext, Vcb, FALSE, TRUE, &LocalPath, Vcb->SysSDirFileInfo , &Vcb->UniqueIDMapFileInfo, NULL);

            if (NT_SUCCESS(Status)) {

            } else if  (Status == STATUS_OBJECT_NAME_NOT_FOUND) {

                Vcb->UniqueIDMapFileInfo = NULL;

            } else {

                goto unwind_1;
            }
            Status = STATUS_SUCCESS;
        }

        Status = STATUS_SUCCESS;

        // clear Modified flags. It was not real modify, just
        // bitmap construction
        Vcb->BitmapModified = FALSE;
        //Vcb->Modified = FALSE;
        UDFPreClrModified(Vcb);
        UDFClrModified(Vcb);
        // this is a part of UDF_RESIDUAL_REFERENCE
        InterlockedIncrement((PLONG)&Vcb->VcbReference);

        // Start initializing the fields contained in the Header.

        // DisAllow fast-IO for now.
    //    RootFcb->Header->IsFastIoPossible = FastIoIsNotPossible;
        Vcb->RootIndexFcb->Header.IsFastIoPossible = FastIoIsPossible;

        // Initialize the MainResource and PagingIoResource pointers in
        // the CommonFCBHeader structure to point to the ERESOURCE structures we
        // have allocated and already initialized above.
    //    RootFcb->Header.Resource = &RootFcb->MainResource;
    //    RootFcb->Header.PagingIoResource = &RootFcb->PagingIoResource;

        // Initialize the file size values here.
        Vcb->RootIndexFcb->Header.AllocationSize.QuadPart = 0;
        Vcb->RootIndexFcb->Header.FileSize.QuadPart = 0;

        // The following will disable ValidDataLength support.
    //    RootFcb->Header.ValidDataLength.QuadPart = 0x7FFFFFFFFFFFFFFFI64;
        Vcb->RootIndexFcb->Header.ValidDataLength.QuadPart = 0;

        if (!NT_SUCCESS(Status))
            try_return(Status);

        ASSERT(!Vcb->Modified);

        UDFUnlockVcb(IrpContext, Vcb);
        UnlockVcb = FALSE;

        //  Now do the volume dasd Fcb.  Create this and reference it in the Vcb.

        UDFLockVcb(IrpContext, Vcb);
        UnlockVcb = TRUE;

        Vcb->VolumeDasdFcb = UDFCreateFcb(IrpContext, FileId, UDF_NODE_TYPE_DATA, NULL);
        if (!Vcb->VolumeDasdFcb)
            try_return(Status = STATUS_INSUFFICIENT_RESOURCES);

        UDFIncrementReferenceCounts(IrpContext, Vcb->VolumeDasdFcb, 1, 1);
        Vcb->VolumeDasdFcb->SystemReferenceHeld = TRUE;
        Vcb->VolumeDasdFcb->SystemUserReferenceHeld = TRUE;
        UDFUnlockVcb(IrpContext, Vcb);
        UnlockVcb = FALSE;

        // Iterate through all partitions in the Pcb structure to find the highest sector number (LastSector)
        // occupied by any physical partition. If the end of the current partition exceeds the current LastSector,
        // update LastSector. This determines the upper boundary of space used on the device.

        for (USHORT RefPartNum = 0; RefPartNum < Vcb->PartitionMaps; RefPartNum++) {

            if ((Vcb->Partitions[RefPartNum].PartitionType == UDF_TYPE1_MAP15 ||
                Vcb->Partitions[RefPartNum].PartitionType == UDF_SPARABLE_MAP15) &&
                Vcb->Partitions[RefPartNum].PartitionRoot +
                Vcb->Partitions[RefPartNum].PartitionLen > LastSector) {

                LastSector = Vcb->Partitions[RefPartNum].PartitionRoot +
                             Vcb->Partitions[RefPartNum].PartitionLen;
            }
        }

        if ((Vcb->SessionEndLba + 1 > LastSector) 
            && !Vcb->CDR_Mode
        ) {
            LastSector = Vcb->SessionEndLba + 1;
        }

        Vcb->VolumeDasdFcb->Header.FileSize.QuadPart = LlBytesFromSectors(Vcb, LastSector);

        Vcb->VolumeDasdFcb->Header.AllocationSize.QuadPart =
        Vcb->VolumeDasdFcb->Header.ValidDataLength.QuadPart = Vcb->VolumeDasdFcb->Header.FileSize.QuadPart;

        // Point to the resource.

        Vcb->VolumeDasdFcb->Header.Resource = &Vcb->VolumeDasdFcb->FcbNonpaged->FcbResource;
        Vcb->VolumeDasdFcb->Header.PagingIoResource = &Vcb->VolumeDasdFcb->FcbNonpaged->FcbPagingIoResource;

        // TODO: use VolumeDasdFcb ?????

        FsRtlSetupAdvancedHeader(&Vcb->VolumeDasdFcb->Header, &Vcb->VolumeDasdFcb->FcbNonpaged->AdvancedFcbHeaderMutex);

        // Mark the Fcb as initialized.

        SetFlag(Vcb->VolumeDasdFcb->FcbState, FCB_STATE_INITIALIZED);

    try_exit:  NOTHING;
    } _SEH2_FINALLY {

        if (_SEH2_AbnormalTermination()) {

            UDFFreePool((PVOID*)&Vcb->ZBuffer);

            // Vcb->VolumeDasdFcb
        }

        if (UnlockVcb) {
            UDFUnlockVcb(IrpContext, Vcb);
        }
    } _SEH2_END;

    return Status;
} // end UDFCompleteMount()

/*************************************************************************
*
* Function: UDFDeallocateCcb()
*
* Description:
*   Deallocate a previously allocated structure.
*
* Expected Interrupt Level (for execution) :
*
*  IRQL_PASSIVE_LEVEL
*
* Return Value: None
*
*************************************************************************/
VOID
UDFDeallocateCcb(
    PCCB Ccb
    )
{
    ASSERT_CCB(Ccb);

    ExFreeToPagedLookasideList(&UdfData.CcbLookasideList, Ccb);

} // end UDFDeallocateCcb()

/*
  Function: UDFDeleteCcb()

  Description:
    Cleanup and deallocate a previously allocated structure.

  Expected Interrupt Level (for execution) :

   IRQL_PASSIVE_LEVEL

  Return Value: None

*/
VOID
UDFDeleteCcb(
    PCCB Ccb
)
{
    NT_ASSERT(Ccb->DeleteOnCloseRequest == NULL);
    NT_ASSERT(!(Ccb->AbortFlags & UDF_CCB_ABORT_LINKED) && !Ccb->AbortCacheEvent);
    if ((Ccb->AbortFlags & UDF_CCB_ABORT_LINKED) || Ccb->AbortCacheEvent) {
        Ccb->Fcb->Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
        return; /* pending cache callback still owns this CCB/event */
    }

    if (Ccb->SearchExpression.Buffer != NULL) {

        UDFFreePool((PVOID*)&Ccb->SearchExpression.Buffer);
    }

    UDFDeallocateCcb(Ccb);
} // end UDFDeleteCcb()

/*
  Function: UDFCreateBitmapStream()

  Description:
    Creates an internal stream FCB and FileObject for the free space bitmap.
    This allows bitmap data to be cached via CcPinRead/CcSetDirtyPinnedData.
    The MCB maps stream offsets to physical disk sectors where the bitmap resides.

  Arguments:
    Vcb         - Volume control block
    BitmapPsn   - Physical sector number where bitmap starts on disk
    BitmapLength - Total length of bitmap data on disk in bytes
                   (including SPACE_BITMAP_DESC header)

  Return Value:
    NTSTATUS
*/
NTSTATUS
UDFCreateBitmapStream(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN ULONG BitmapPsn,
    IN ULONG BitmapLength
    )
{
    NTSTATUS Status;
    PFCB Fcb;
    PFILE_OBJECT FileObject;
    PFCB_NONPAGED FcbNonpaged;
    ULONG BitmapSectors;
    LARGE_INTEGER FileSize;
    BOOLEAN McbInitialized = FALSE;

    PAGED_CODE();
    ASSERT_EXCLUSIVE_VCB(Vcb);
    if (Vcb->BitmapFcb || Vcb->BitmapStreamFileObject ||
        Vcb->BitmapCacheState != UdfBitmapCacheAbsent)
        return STATUS_DEVICE_BUSY;
    if (BitmapLength < sizeof(SPACE_BITMAP_DESC) || !Vcb->SectorSize || Vcb->SectorShift >= 32 ||
        Vcb->SectorSize != (1UL << Vcb->SectorShift))
        return STATUS_INVALID_PARAMETER;
    BitmapSectors = (ULONG)(((ULONGLONG)BitmapLength + Vcb->SectorSize - 1) >> Vcb->SectorShift);
    if (!BitmapSectors || BitmapPsn > Vcb->SessionEndLba ||
        BitmapSectors - 1 > Vcb->SessionEndLba - BitmapPsn)
        return STATUS_DISK_CORRUPT_ERROR;
    FileSize.QuadPart = (LONGLONG)BitmapSectors << Vcb->SectorShift;
    if (FileSize.QuadPart > MAXULONG)
        return STATUS_NOT_SUPPORTED; /* bitmap pin offsets are ULONG */

    UDFPrint(("UDFCreateBitmapStream: PSN=%x, Length=%x\n", BitmapPsn, BitmapLength));

    // Allocate the FCB

    Fcb = UDFAllocateFcb();
    if (!Fcb) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(Fcb, sizeof(struct FCB));

    // Initialize nonpaged data (inline in VCB to avoid extra allocation)

    FcbNonpaged = &Vcb->BitmapNonpaged;
    RtlZeroMemory(FcbNonpaged, sizeof(struct FCB_NONPAGED));

    FcbNonpaged->NodeTypeCode = UDF_NODE_TYPE_FCB_NONPAGED;
    FcbNonpaged->NodeByteSize = sizeof(struct FCB_NONPAGED);

    Status = ExInitializeResourceLite(&FcbNonpaged->FcbResource);
    if (!NT_SUCCESS(Status)) {
        ExFreePoolWithTag(Fcb, TAG_FCB);
        return Status;
    }
    Status = ExInitializeResourceLite(&FcbNonpaged->FcbPagingIoResource);
    if (!NT_SUCCESS(Status)) {
        ExDeleteResourceLite(&FcbNonpaged->FcbResource);
        ExFreePoolWithTag(Fcb, TAG_FCB);
        return Status;
    }
    ExInitializeFastMutex(&FcbNonpaged->FcbMutex);
    ExInitializeFastMutex(&FcbNonpaged->AdvancedFcbHeaderMutex);
    ExInitializeFastMutex(&FcbNonpaged->FcbFastMutex);

    // Initialize the FCB header

    Fcb->NodeIdentifier.NodeTypeCode = UDF_NODE_TYPE_DATA;
    Fcb->NodeIdentifier.NodeByteSize = sizeof(struct FCB);
    Fcb->FcbNonpaged = FcbNonpaged;
    Fcb->Vcb = Vcb;

    Fcb->Header.Resource = &FcbNonpaged->FcbResource;
    Fcb->Header.PagingIoResource = &FcbNonpaged->FcbPagingIoResource;
    Fcb->Header.IsFastIoPossible = FastIoIsNotPossible;

    InitializeListHead(&Fcb->EofListHead);
    FsRtlSetupAdvancedHeader(&Fcb->Header, &FcbNonpaged->AdvancedFcbHeaderMutex);

    InitializeListHead(&Fcb->ParentLcbQueue);
    InitializeListHead(&Fcb->ChildLcbQueue);
    InitializeListHead(&Fcb->PendingDeleteQueue);
    InitializeListHead(&Fcb->AbortedOpenQueue);

    // Set file sizes: bitmap data on disk (with header)

    FileSize.QuadPart = (LONGLONG)BitmapSectors << Vcb->SectorShift;

    Fcb->Header.AllocationSize.QuadPart = FileSize.QuadPart;
    /* This private cache owns whole physical bitmap sectors, including
     * their padding. It is not the descriptor's logical numOfBytes. Keeping
     * its EOF sector-aligned also gives the final RTL_BITMAP word a pinned
     * padding area without extending the on-disk descriptor's valid bits. */
    Fcb->Header.FileSize.QuadPart = FileSize.QuadPart;
    Fcb->Header.ValidDataLength.QuadPart = FileSize.QuadPart;

    // Initialize the MCB: maps VBN (stream sectors) -> PSN (disk sectors)

    _SEH2_TRY {
        FsRtlInitializeLargeMcb(&Vcb->BitmapMcb, PagedPool);
        McbInitialized = TRUE;
        Status = FsRtlAddLargeMcbEntry(&Vcb->BitmapMcb, 0,
                     (LONGLONG)BitmapPsn, (LONGLONG)BitmapSectors) ?
                     STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
    } _SEH2_EXCEPT(UDFExceptionFilter(IrpContext, _SEH2_GetExceptionInformation())) {
        Status = IrpContext->ExceptionStatus;
    } _SEH2_END;
    if (Status != STATUS_SUCCESS) {
        if (McbInitialized) FsRtlUninitializeLargeMcb(&Vcb->BitmapMcb);
        ExDeleteResourceLite(&FcbNonpaged->FcbResource);
        ExDeleteResourceLite(&FcbNonpaged->FcbPagingIoResource);
        ExFreePoolWithTag(Fcb, TAG_FCB);
        return Status;
    }

    // Create the internal stream FileObject

    FileObject = NULL;
    _SEH2_TRY {
        FileObject = IoCreateStreamFileObjectLite(NULL, Vcb->Vpb->RealDevice);
        Status = FileObject ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
    } _SEH2_EXCEPT(UDFExceptionFilter(IrpContext, _SEH2_GetExceptionInformation())) {
        Status = IrpContext->ExceptionStatus;
    } _SEH2_END;
    if (Status != STATUS_SUCCESS) {
        FsRtlUninitializeLargeMcb(&Vcb->BitmapMcb);
        ExDeleteResourceLite(&FcbNonpaged->FcbResource);
        ExDeleteResourceLite(&FcbNonpaged->FcbPagingIoResource);
        ExFreePoolWithTag(Fcb, TAG_FCB);
        return Status;
    }

    FileObject->ReadAccess = TRUE;
    FileObject->WriteAccess = FALSE;
    FileObject->DeleteAccess = FALSE;

    FileObject->FsContext = Fcb;
    FileObject->FsContext2 = (PVOID)(ULONG_PTR)StreamFileOpen;
    FileObject->SectionObjectPointer = &FcbNonpaged->SegmentObject;
    FileObject->Vpb = Vcb->Vpb;

    /* Publish the exact owner before Cache Manager can call back. A partial
     * InitializeCacheMap failure still belongs to this VCB and must be closed
     * before the mount can reuse the legacy bitmap path. */
    KeInitializeSpinLock(&Vcb->BitmapIoLock);
    KeInitializeEvent(&Vcb->BitmapIoDrained, NotificationEvent, TRUE);
    Vcb->BitmapUninitEvent.Next = NULL;
    KeInitializeEvent(&Vcb->BitmapUninitEvent.Event, NotificationEvent, FALSE);
    Vcb->BitmapIoCount = 0;
    Vcb->BitmapIoError = STATUS_SUCCESS;
    Vcb->BitmapIoStopped = FALSE;
    Vcb->BitmapCacheInitialized = FALSE;
    Vcb->BitmapHeaderAccepted = FALSE;
    Vcb->BitmapCloseError = STATUS_SUCCESS;
    Vcb->BitmapCloseThread = NULL;
    Vcb->BitmapFcb = Fcb;
    Vcb->BitmapStreamFileObject = FileObject;
    Vcb->BitmapBcb = NULL;
    Vcb->BitmapCacheState = UdfBitmapCacheActive;
    _SEH2_TRY {
        CcInitializeCacheMap(FileObject,
                             (PCC_FILE_SIZES)&Fcb->Header.AllocationSize,
                             TRUE, &UdfData.CacheMgrCallBacks, Fcb);
        Vcb->BitmapCacheInitialized = TRUE;
        /* Namespace and bitmap persistence are ordered by the explicit
         * checkpoint, not by the independent lazy bitmap writer. */
        CcSetAdditionalCacheAttributes(FileObject, FALSE, TRUE);
        Status = STATUS_SUCCESS;
    } _SEH2_EXCEPT(UDFExceptionFilter(IrpContext, _SEH2_GetExceptionInformation())) {
        Status = IrpContext->ExceptionStatus;
    } _SEH2_END;
    if (Status != STATUS_SUCCESS)
        return Status; /* VCB retains FileObject/MCB/FCB for checked unwind. */

    UDFPrint(("UDFCreateBitmapStream: OK, Sectors=%x, FileSize=%I64x\n",
        BitmapSectors, FileSize.QuadPart));

    return STATUS_SUCCESS;
} // end UDFCreateBitmapStream()

/* Finish Cache Manager work before releasing this stream's MCB/resources.
 * Caller owns exclusive VCB, but no bitmap Main/Paging/allocation resource.
 * Pending events and the exact owner remain in VCB across bounded refusal. */
NTSTATUS
UDFDeleteBitmapStream(PIRP_CONTEXT IrpContext, PVCB Vcb, BOOLEAN AbortUnpublished)
{
    NTSTATUS Status = STATUS_SUCCESS;
    IO_STATUS_BLOCK IoStatus;
    LARGE_INTEGER Timeout;
    KIRQL SavedIrql;
    PFCB Fcb;
    PFILE_OBJECT FileObject;
    BOOLEAN MainAcquired = FALSE, PagingAcquired = FALSE, BitmapAcquired = FALSE;

    PAGED_CODE();
    ASSERT_EXCLUSIVE_VCB(Vcb);
    if (!Vcb->BitmapFcb)
        return Vcb->BitmapStreamFileObject || Vcb->BitmapCacheState != UdfBitmapCacheAbsent ?
                   STATUS_FILE_CORRUPT_ERROR : STATUS_SUCCESS;
    if (Vcb->BitmapCloseThread)
        return STATUS_DEVICE_BUSY;
    if (Vcb->BitmapCacheState != UdfBitmapCacheActive &&
        Vcb->BitmapCacheState != UdfBitmapCacheUninitializing &&
        Vcb->BitmapCacheState != UdfBitmapCacheUninitialized)
        return STATUS_FILE_CORRUPT_ERROR;
    Fcb = Vcb->BitmapFcb;
    FileObject = Vcb->BitmapStreamFileObject;
    if (!FileObject || FileObject->FsContext != Fcb ||
        Fcb->FcbNonpaged != &Vcb->BitmapNonpaged)
        return STATUS_FILE_CORRUPT_ERROR;
    if (ExIsResourceAcquiredSharedLite(&Vcb->BitMapResource1) ||
        ExIsResourceAcquiredExclusiveLite(&Vcb->BitMapResource1) ||
        ExIsResourceAcquiredSharedLite(Fcb->Header.Resource) ||
        ExIsResourceAcquiredExclusiveLite(Fcb->Header.Resource) ||
        ExIsResourceAcquiredSharedLite(Fcb->Header.PagingIoResource) ||
        ExIsResourceAcquiredExclusiveLite(Fcb->Header.PagingIoResource))
        return STATUS_DEVICE_BUSY; /* never wait on our own cache callback locks */
    if (AbortUnpublished && (Vcb->BitmapHeaderAccepted || Vcb->BitmapModified))
        return STATUS_INVALID_DEVICE_STATE;
    /* A post-root-GC generation needs another checkpoint while metadata
     * mapping still exists. Uninitialization must not publish it out of order. */
    if (!AbortUnpublished && (Vcb->Modified || Vcb->BitmapModified ||
                             Vcb->MetadataBitmapModified))
        return STATUS_DEVICE_BUSY;
    Timeout.QuadPart = -30LL * 1000 * 10000;
    Vcb->BitmapCloseThread = PsGetCurrentThread();
    _SEH2_TRY {
        if (Vcb->BitmapCacheState == UdfBitmapCacheActive) {
            UDFAcquireResourceExclusive(&Vcb->BitMapResource1, TRUE);
            BitmapAcquired = TRUE;
            UDFUnpinBitmapPage(Vcb);
            Vcb->BitmapCacheState = UdfBitmapCacheQuiescing;
            UDFReleaseResource(&Vcb->BitMapResource1);
            BitmapAcquired = FALSE;
            if (Vcb->BitmapCacheInitialized && !AbortUnpublished) {
                Status = UDFBeginBitmapFlush(Vcb);
                if (Status != STATUS_SUCCESS) {
                    Vcb->BitmapCacheState = UdfBitmapCacheActive;
                    _SEH2_LEAVE;
                }
                IoStatus.Status = STATUS_SUCCESS;
                IoStatus.Information = 0;
                CcFlushCache(&Vcb->BitmapNonpaged.SegmentObject, NULL, 0, &IoStatus);
                Status = NT_SUCCESS(Vcb->BitmapIoError) ? IoStatus.Status : Vcb->BitmapIoError;
                if (Status != STATUS_SUCCESS) {
                    /* Actual cache writeback failure is different from a
                     * later event timeout or a temporarily held resource. */
                    InterlockedCompareExchange(&Vcb->MetadataWritebackError,
                        NT_SUCCESS(Status) ? STATUS_IO_DEVICE_ERROR : Status,
                        STATUS_SUCCESS);
                    Vcb->BitmapCacheState = UdfBitmapCacheActive;
                    _SEH2_LEAVE;
                }
            }
            if (!AbortUnpublished && (Vcb->Modified || Vcb->BitmapModified ||
                                     Vcb->MetadataBitmapModified)) {
                Vcb->BitmapCacheState = UdfBitmapCacheActive;
                Status = STATUS_DEVICE_BUSY;
                _SEH2_LEAVE; /* flush reopened integrity: checkpoint before uninit */
            }
            /* A private map might have been consumed even if this raises.
             * Never blindly replay the call. Only its persistent event and
             * detached map permit advancing this exact close generation. */
            Vcb->BitmapCacheState = UdfBitmapCacheUninitializing;
            CcUninitializeCacheMap(FileObject, NULL, &Vcb->BitmapUninitEvent);
        }
        if (Vcb->BitmapCacheState == UdfBitmapCacheUninitializing) {
            Status = KeWaitForSingleObject(&Vcb->BitmapUninitEvent.Event,
                         Executive, KernelMode, FALSE, &Timeout);
            if (Status != STATUS_SUCCESS) {
                if (Status == STATUS_TIMEOUT) Status = STATUS_IO_TIMEOUT;
                _SEH2_LEAVE;
            }
            if (!NT_SUCCESS(Vcb->BitmapIoError)) {
                Status = Vcb->BitmapIoError;
                _SEH2_LEAVE; /* detached cache does not cancel a failed write */
            }
            if (FileObject->PrivateCacheMap || Vcb->BitmapNonpaged.SegmentObject.SharedCacheMap) {
                Status = STATUS_DEVICE_BUSY;
                _SEH2_LEAVE;
            }
            if (!AbortUnpublished && (Vcb->Modified || Vcb->BitmapModified ||
                                     Vcb->MetadataBitmapModified)) {
                Vcb->BitmapCacheState = UdfBitmapCacheQuarantined;
                Status = STATUS_FILE_CORRUPT_ERROR;
                _SEH2_LEAVE; /* no new dirty generation after its cache was detached */
            }
            Vcb->BitmapCacheState = UdfBitmapCacheUninitialized;
        }
        /* Cache-map uninitialization does not retire MM's data section.
         * Its modified-page writer can still call us through this exact
         * stream FILE_OBJECT. Keep FsContext, resources and the I/O backend
         * alive until the entire clean section has actually been purged. */
        MainAcquired = UDFAcquireFcbExclusive(IrpContext, Fcb, TRUE);
        if (!MainAcquired) { Status = STATUS_DEVICE_BUSY; _SEH2_LEAVE; }
        if (Vcb->BitmapBcb || FileObject->PrivateCacheMap ||
            Vcb->BitmapNonpaged.SegmentObject.SharedCacheMap ||
            Vcb->BitmapNonpaged.SegmentObject.ImageSectionObject) {
            Status = STATUS_DEVICE_BUSY;
            _SEH2_LEAVE;
        }
        if (Vcb->BitmapNonpaged.SegmentObject.DataSectionObject &&
            (!CcPurgeCacheSection(&Vcb->BitmapNonpaged.SegmentObject,
                                  NULL, 0, FALSE) ||
             Vcb->BitmapNonpaged.SegmentObject.DataSectionObject)) {
            Status = STATUS_DEVICE_BUSY;
            _SEH2_LEAVE;
        }
        KeAcquireSpinLock(&Vcb->BitmapIoLock, &SavedIrql);
        Vcb->BitmapIoStopped = TRUE;
        KeReleaseSpinLock(&Vcb->BitmapIoLock, SavedIrql);
        Status = KeWaitForSingleObject(&Vcb->BitmapIoDrained,
                     Executive, KernelMode, FALSE, &Timeout);
        if (Status != STATUS_SUCCESS) {
            if (Status == STATUS_TIMEOUT) Status = STATUS_IO_TIMEOUT;
            _SEH2_LEAVE;
        }
        PagingAcquired = UDFAcquireResourceExclusive(Fcb->Header.PagingIoResource, FALSE);
        if (!PagingAcquired) { Status = STATUS_DEVICE_BUSY; _SEH2_LEAVE; }
        if (Vcb->BitmapIoCount || Vcb->BitmapBcb ||
            Vcb->BitmapNonpaged.SegmentObject.DataSectionObject ||
            Vcb->BitmapNonpaged.SegmentObject.ImageSectionObject ||
            Vcb->BitmapNonpaged.SegmentObject.SharedCacheMap || FileObject->PrivateCacheMap) {
            Status = STATUS_DEVICE_BUSY;
            _SEH2_LEAVE;
        }
        Vcb->BitmapCacheState = UdfBitmapCacheDisposing;
        FsRtlTeardownPerStreamContexts(&Fcb->Header);
        UDFReleaseResource(Fcb->Header.PagingIoResource);
        PagingAcquired = FALSE;
        UDFReleaseFcb(IrpContext, Fcb);
        MainAcquired = FALSE;
        /* Cache is detached and all old I/O is drained. A retained diagnostic
         * FileObject reference must not expose a freed private FCB pointer. */
        FileObject->FsContext = NULL;
        FileObject->FsContext2 = NULL;
        FileObject->SectionObjectPointer = NULL;
        ObDereferenceObject(FileObject);
        FsRtlUninitializeLargeMcb(&Vcb->BitmapMcb);
        ExDeleteResourceLite(&Vcb->BitmapNonpaged.FcbResource);
        ExDeleteResourceLite(&Vcb->BitmapNonpaged.FcbPagingIoResource);
        ExFreePoolWithTag(Fcb, TAG_FCB);
        Vcb->BitmapStreamFileObject = NULL;
        Vcb->BitmapFcb = NULL;
        Vcb->BitmapCacheInitialized = FALSE;
        Vcb->BitmapHeaderAccepted = FALSE;
        Vcb->BitmapPinnedData = NULL;
        Vcb->BitmapPinnedLength = Vcb->BitmapPinnedOffset = 0;
        Vcb->BitmapPageStartLbn = Vcb->BitmapPageBitCount = 0;
        RtlZeroMemory(&Vcb->BitmapRtl, sizeof(Vcb->BitmapRtl));
        Vcb->BitmapCacheState = UdfBitmapCacheAbsent;
    } _SEH2_EXCEPT(UDFExceptionFilter(IrpContext, _SEH2_GetExceptionInformation())) {
        Status = IrpContext->ExceptionStatus;
        if (Vcb->BitmapCacheState == UdfBitmapCacheDisposing)
            Vcb->BitmapCacheState = UdfBitmapCacheQuarantined;
        else if (Vcb->BitmapCacheState == UdfBitmapCacheQuiescing)
            Vcb->BitmapCacheState = UdfBitmapCacheActive; /* uninit not submitted */
    } _SEH2_END;
    Vcb->BitmapCloseThread = NULL;
    if (BitmapAcquired) UDFReleaseResource(&Vcb->BitMapResource1);
    if (PagingAcquired) UDFReleaseResource(Fcb->Header.PagingIoResource);
    if (MainAcquired) UDFReleaseFcb(IrpContext, Fcb);
    if (Status != STATUS_SUCCESS) {
        if (NT_SUCCESS(Status)) Status = STATUS_IO_DEVICE_ERROR;
        InterlockedCompareExchange(&Vcb->BitmapCloseError, Status, STATUS_SUCCESS);
        /* Keep the first attempted-close status for diagnostics. Lower
         * bitmap I/O/cache failures latch their own writeback error above
         * or in UDFBitmapStreamIo; a wait/claim refusal is not a new write
         * failure and must not poison the persistent event's resume path. */
    }
    return Status;
} // end UDFDeleteBitmapStream()
