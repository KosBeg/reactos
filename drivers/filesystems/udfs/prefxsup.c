////////////////////////////////////////////////////////////////////
// Copyright (C) Alexander Telyatnikov, Ivan Keliukh, Yegor Anchishkin, SKIF Software, 1999-2013. Kiev, Ukraine
// All rights reserved
// This file was released under the GPLv2 on June 2015.
////////////////////////////////////////////////////////////////////
/*

 File: PrefxSup.cpp

 Module: UDF File System Driver (Kernel mode execution only)

 Description:
   This module implements the UDF Prefix support routines.

*/

#include "udffs.h"

// define the file specific bug-check id
#define UDF_BUG_CHECK_ID                UDF_FILE_MISC

/*
    Lexicographic comparison of two UNICODE_STRING names.
    Returns LessThan, EqualTo, or GreaterThan.
    Used for splay tree ordering.
*/
static
FSRTL_COMPARISON_RESULT
UdfCompareNames(
    IN PUNICODE_STRING NameA,
    IN PUNICODE_STRING NameB
    )
{
    ULONG MinLength = NameA->Length;
    FSRTL_COMPARISON_RESULT Result = LessThan;

    if (NameA->Length > NameB->Length) {
        MinLength = NameB->Length;
        Result = GreaterThan;
    } else if (NameA->Length == NameB->Length) {
        Result = EqualTo;
    }

    ULONG i = (ULONG)RtlCompareMemory(NameA->Buffer, NameB->Buffer, MinLength);

    if (i < MinLength) {
        return ((NameA->Buffer[i / 2] < NameB->Buffer[i / 2]) ?
                LessThan : GreaterThan);
    }

    return Result;
}


/*
    Search a splay tree for an LCB matching the given name.
    On match, rebalances the tree (splay) and returns the LCB.
    Returns NULL if no match.

    LinksOffset: byte offset of RTL_SPLAY_LINKS within LCB structure
    NameOffset:  byte offset of UNICODE_STRING within LCB structure
*/
static
PLCB
UdfFindNameLink(
    IN PRTL_SPLAY_LINKS *RootNode,
    IN PUNICODE_STRING Name,
    IN ULONG LinksOffset,
    IN ULONG NameOffset
    )
{
    PRTL_SPLAY_LINKS Links = *RootNode;

    while (Links != NULL) {

        PLCB Node = (PLCB)((PUCHAR)Links - LinksOffset);
        PUNICODE_STRING NodeName = (PUNICODE_STRING)((PUCHAR)Node + NameOffset);

        FSRTL_COMPARISON_RESULT Comparison = UdfCompareNames(NodeName, Name);

        if (Comparison == GreaterThan) {
            Links = RtlLeftChild(Links);
        } else if (Comparison == LessThan) {
            Links = RtlRightChild(Links);
        } else {
            *RootNode = RtlSplay(Links);
            return Node;
        }
    }

    return NULL;
}


/*
    Insert an LCB into a splay tree keyed by the given name.
    Returns TRUE if inserted, FALSE if duplicate exists.

    LinksOffset: byte offset of RTL_SPLAY_LINKS within LCB structure
    NameOffset:  byte offset of UNICODE_STRING within LCB structure
*/
static
BOOLEAN
UdfInsertNameLink(
    IN PRTL_SPLAY_LINKS *RootNode,
    IN PLCB NameLink,
    IN ULONG LinksOffset,
    IN ULONG NameOffset
    )
{
    PRTL_SPLAY_LINKS NewLinks = (PRTL_SPLAY_LINKS)((PUCHAR)NameLink + LinksOffset);
    PUNICODE_STRING NewName = (PUNICODE_STRING)((PUCHAR)NameLink + NameOffset);

    RtlInitializeSplayLinks(NewLinks);

    if (*RootNode == NULL) {
        *RootNode = NewLinks;
        return TRUE;
    }

    PLCB Node = (PLCB)((PUCHAR)*RootNode - LinksOffset);

    while (TRUE) {

        PRTL_SPLAY_LINKS NodeLinks = (PRTL_SPLAY_LINKS)((PUCHAR)Node + LinksOffset);
        PUNICODE_STRING NodeName = (PUNICODE_STRING)((PUCHAR)Node + NameOffset);

        FSRTL_COMPARISON_RESULT Comparison = UdfCompareNames(NodeName, NewName);

        if (Comparison == EqualTo) {
            // Duplicate name — not inserted. Caller will not set IN_TREE flag.
            return FALSE;
        }

        if (Comparison == GreaterThan) {
            if (RtlLeftChild(NodeLinks) == NULL) {
                RtlInsertAsLeftChild(NodeLinks, NewLinks);
                break;
            }
            Node = (PLCB)((PUCHAR)RtlLeftChild(NodeLinks) - LinksOffset);
        } else {
            if (RtlRightChild(NodeLinks) == NULL) {
                RtlInsertAsRightChild(NodeLinks, NewLinks);
                break;
            }
            Node = (PLCB)((PUCHAR)RtlRightChild(NodeLinks) - LinksOffset);
        }
    }

    return TRUE;
}


/*
    Insert LCB into all applicable splay trees of ParentFcb.
    Called after LCB names are initialized.
*/
VOID
UdfInsertNameLinks(
    IN PFCB ParentFcb,
    IN PLCB Lcb
    )
{
    // Exact case tree (keyed by FileName)
    if (Lcb->FileName.Buffer && Lcb->FileName.Length > 0) {
        if (UdfInsertNameLink(&ParentFcb->ExactCaseRoot, Lcb,
                              FIELD_OFFSET(struct LCB, ExactCaseLinks),
                              FIELD_OFFSET(struct LCB, FileName))) {
            SetFlag(Lcb->Flags, UDF_LCB_FLAG_EXACT_CASE_IN_TREE);
        }
    }

    // Ignore case tree (keyed by IgnoreCaseLinkName)
    if (Lcb->IgnoreCaseLinkName.Buffer && Lcb->IgnoreCaseLinkName.Length > 0) {
        if (UdfInsertNameLink(&ParentFcb->IgnoreCaseRoot, Lcb,
                              FIELD_OFFSET(struct LCB, IgnoreCaseLinks),
                              FIELD_OFFSET(struct LCB, IgnoreCaseLinkName))) {
            SetFlag(Lcb->Flags, UDF_LCB_FLAG_IGNORE_CASE_IN_TREE);
        }
    }

    // Short name tree (keyed by ShortName)
    if (Lcb->ShortName.Buffer && Lcb->ShortName.Length > 0) {
        if (UdfInsertNameLink(&ParentFcb->ShortNameRoot, Lcb,
                              FIELD_OFFSET(struct LCB, ShortNameLinks),
                              FIELD_OFFSET(struct LCB, ShortName))) {
            SetFlag(Lcb->Flags, UDF_LCB_FLAG_SHORT_NAME_IN_TREE);
        }
    }
}


/*
    Remove LCB from all splay trees of ParentFcb.
    Uses IN_TREE flags to track which trees the LCB was inserted into.
    After removal, clears all IN_TREE flags.
    Idempotent — safe to call multiple times.
*/
VOID
UdfRemoveNameLinks(
    IN PFCB ParentFcb,
    IN PLCB Lcb
    )
{
    if (FlagOn(Lcb->Flags, UDF_LCB_FLAG_EXACT_CASE_IN_TREE)) {
        ParentFcb->ExactCaseRoot = RtlDelete(&Lcb->ExactCaseLinks);
    }

    if (FlagOn(Lcb->Flags, UDF_LCB_FLAG_IGNORE_CASE_IN_TREE)) {
        ParentFcb->IgnoreCaseRoot = RtlDelete(&Lcb->IgnoreCaseLinks);
    }

    if (FlagOn(Lcb->Flags, UDF_LCB_FLAG_SHORT_NAME_IN_TREE)) {
        ParentFcb->ShortNameRoot = RtlDelete(&Lcb->ShortNameLinks);
    }

    ClearFlag(Lcb->Flags, UDF_LCB_FLAG_EXACT_CASE_IN_TREE |
                           UDF_LCB_FLAG_IGNORE_CASE_IN_TREE |
                           UDF_LCB_FLAG_SHORT_NAME_IN_TREE);
}


/*
    This routine creates and inserts an LCB linking the parent FCB to child FCB.

    Creates LCB with dynamic name storage.
    Names are stored in a buffer immediately after the LCB structure.

    The LCB is linked into:
    - ParentFcb->ChildLcbQueue (via Lcb->ParentFcbLinks)
    - ChildFcb->ParentLcbQueue (via Lcb->ChildFcbLinks)
    - ParentFcb splay trees (ExactCaseRoot, IgnoreCaseRoot, ShortNameRoot)

    Parameters:
      IrpContext - IRP context
      ParentFcb - Parent directory FCB
      ChildFcb - Child file FCB
      FileName - File name (component name, may be NULL)
      CaseFileName - Case-sensitive file name (may be NULL)
      ShortName - 8.3 short name (may be NULL)
      InitialOffset - Directory offset
*/
PLCB
UDFInsertPrefix(
    IN PIRP_CONTEXT IrpContext,
    IN PFCB ParentFcb,
    IN PFCB ChildFcb,
    IN PUDF_FILE_INFO LinkFileInfo, // exact parent holds already acquired
    IN PUNICODE_STRING FileName OPTIONAL,
    IN PUNICODE_STRING CaseFileName OPTIONAL,
    IN PUNICODE_STRING ShortName OPTIONAL,
    IN ULONGLONG InitialOffset
    )
{
    PLCB Lcb;
    ULONG Flags = 0;
    ULONG NameLength = 0;
    ULONG AllocationSize;
    PWCHAR NameBuffer;

    UNREFERENCED_PARAMETER(IrpContext);

    ASSERT(ParentFcb);
    ASSERT(ChildFcb);
    if (!ParentFcb || !ChildFcb || !LinkFileInfo ||
        !LinkFileInfo->ParentFile || LinkFileInfo->Fcb != ChildFcb ||
        LinkFileInfo->ParentFile->Fcb != ParentFcb)
        return NULL;

    /* NodeByteSize and UNICODE_STRING lengths are USHORT. Do not wrap
     * their combined backing allocation when multiple names are present. */
    if ((FileName && (FileName->Length % sizeof(WCHAR) ||
                      FileName->Length > FileName->MaximumLength ||
                      (FileName->Length && !FileName->Buffer))) ||
        (CaseFileName && (CaseFileName->Length % sizeof(WCHAR) ||
                          CaseFileName->Length > CaseFileName->MaximumLength ||
                          (CaseFileName->Length && !CaseFileName->Buffer))) ||
        (ShortName && (ShortName->Length % sizeof(WCHAR) ||
                      ShortName->Length > ShortName->MaximumLength ||
                      (ShortName->Length && !ShortName->Buffer))))
        return NULL;
    if (FileName && FileName->Length) {
        NameLength = FileName->Length + 0x18;
        if (CaseFileName) NameLength += CaseFileName->Length;
        if (ShortName) NameLength += ShortName->Length;
    }
    if (NameLength > MAXUSHORT - UDF_LCB_BASE_SIZE)
        return NULL;

    AllocationSize = UDF_LCB_BASE_SIZE + NameLength;

    //
    // Allocate LCB - use lookaside for small allocations
    //
    if (AllocationSize > UDF_LCB_LOOKASIDE_SIZE) {

        Lcb = (PLCB)FsRtlAllocatePoolWithTag(PagedPool,
                                              AllocationSize,
                                              TAG_LCB);
        if (!Lcb) {
            return NULL;
        }

        SetFlag(Flags, UDF_LCB_FLAG_POOL_ALLOCATED);

    } else {

        Lcb = (PLCB)ExAllocateFromPagedLookasideList(&UdfData.LcbLookasideList);
        if (!Lcb) {
            return NULL;
        }
    }

    //
    // Initialize LCB structure
    //
    UDFPrint(("UDFInsertPrefix: Lcb=%p, AllocationSize=0x%X, sizeof(LCB)=%u, NameLength=%u\n",
              Lcb, AllocationSize, (ULONG)sizeof(struct LCB), NameLength));

    ULONG BytesToZero = min(AllocationSize, UDF_LCB_BASE_SIZE);
    UDFPrint(("  BytesToZero=0x%X (min of AllocationSize and UDF_LCB_BASE_SIZE=0x%X)\n",
              BytesToZero, UDF_LCB_BASE_SIZE));

    RtlZeroMemory(Lcb, BytesToZero);

    Lcb->NodeIdentifier.NodeTypeCode = UDF_NODE_TYPE_LCB;
    Lcb->NodeIdentifier.NodeByteSize = (USHORT)AllocationSize;

    // Set up FCB pointers
    Lcb->ParentFcb = ParentFcb;
    Lcb->ChildFcb = ChildFcb;
    Lcb->LinkFileInfo = LinkFileInfo;
    Lcb->ParentFileInfo = LinkFileInfo->ParentFile;
    Lcb->InitialOffset = InitialOffset;

    // Set flags
    Lcb->Flags = Flags;

    // Initial reference count
    Lcb->Reference = 0;

    //
    // Initialize name buffer pointer
    // Buffer is located immediately after LCB structure
    //
    if (NameLength > 0) {
        Lcb->NameBuffer = (PVOID)((PUCHAR)Lcb + UDF_LCB_BASE_SIZE);
        Lcb->FileName.MaximumLength = NameLength;

        NameBuffer = (PWCHAR)Lcb->NameBuffer;

        //
        // Copy file names into buffer
        // Layout: [FileName][padding 0x18][CaseFileName][ShortName]
        //
        if (FileName != NULL && FileName->Length > 0) {
            // Copy FileName (exact case)
            Lcb->FileName.Buffer = NameBuffer;
            Lcb->FileName.Length = FileName->Length;
            RtlCopyMemory(NameBuffer, FileName->Buffer, FileName->Length);

            // Mirror the exact-case component name. It aliases the same
            // embedded buffer (never freed separately), consistent with
            // UDFApplyPrefixRename. Change-notify offset computation and
            // full-path building read ExactCaseLinkName; without this it
            // stayed zero-length on freshly created links, which forced a
            // TargetNameOffset of 0 in UDFNotifyReportChange — so FsRtl saw
            // an empty parent directory and no registered notify matched,
            // and Explorer would not refresh added/removed entries until F5.
            Lcb->ExactCaseLinkName = Lcb->FileName;

            // Copy CaseFileName (uppercase for ignore-case comparisons)
            if (CaseFileName != NULL && CaseFileName->Length > 0) {
                // Skip alignment padding (0x18 bytes) after FileName
                NameBuffer = (PWCHAR)((PUCHAR)Lcb->NameBuffer + FileName->Length + 0x18);

                Lcb->IgnoreCaseLinkName.Buffer = NameBuffer;
                Lcb->IgnoreCaseLinkName.Length = CaseFileName->Length;
                Lcb->IgnoreCaseLinkName.MaximumLength = CaseFileName->Length;
                RtlCopyMemory(NameBuffer, CaseFileName->Buffer, CaseFileName->Length);

                // Move pointer to next position for ShortName
                NameBuffer += CaseFileName->Length / sizeof(WCHAR);
            } else {
                // No CaseFileName, position after FileName + padding
                NameBuffer = (PWCHAR)((PUCHAR)Lcb->NameBuffer + FileName->Length + 0x18);
            }

            // Copy ShortName (8.3 DOS name)
            if (ShortName != NULL && ShortName->Length > 0) {
                Lcb->ShortName.Buffer = NameBuffer;
                Lcb->ShortName.Length = ShortName->Length;
                Lcb->ShortName.MaximumLength = ShortName->Length;
                RtlCopyMemory(NameBuffer, ShortName->Buffer, ShortName->Length);

                // Set flag indicating short name was created
                SetFlag(Lcb->Flags, UDF_LCB_FLAG_SHORT_NAME_CREATED);
            }
        }
    }

    //
    // Link LCB into the FCB queues
    //
    // Insert into parent's child queue
    InsertHeadList(&ParentFcb->ChildLcbQueue, &Lcb->ParentFcbLinks);
    // Insert into child's parent queue
    InsertHeadList(&ChildFcb->ParentLcbQueue, &Lcb->ChildFcbLinks);

    // Insert into splay trees for O(log n) lookup by name.
    // Caller must hold ParentFcb exclusive for splay tree protection.
    UdfInsertNameLinks(ParentFcb, Lcb);

    return Lcb;
} // end UDFInsertPrefix()


/*
    This routine removes an LCB from FCB queues and frees it.
    Frees name buffer if separately allocated, then frees LCB structure
    itself (from pool or lookaside list).
*/
VOID
UDFRemovePrefix(
    IN PIRP_CONTEXT IrpContext,
    IN PLCB Lcb
    )
{
    UNREFERENCED_PARAMETER(IrpContext);

    if (!Lcb) {
        return;
    }

    ASSERT(Lcb->NodeIdentifier.NodeTypeCode == UDF_NODE_TYPE_LCB);

    //
    // Remove from parent FCB's splay trees.
    // All callers hold ParentFcb exclusive or VcbResource exclusive.
    //
    if (Lcb->ParentFcb) {
        UdfRemoveNameLinks(Lcb->ParentFcb, Lcb);
    }

    //
    // Remove from parent FCB's ChildLcbQueue
    //
    RemoveEntryList(&Lcb->ParentFcbLinks);
    InitializeListHead(&Lcb->ParentFcbLinks);

    //
    // Remove from child FCB's ParentLcbQueue
    //
    RemoveEntryList(&Lcb->ChildFcbLinks);
    InitializeListHead(&Lcb->ChildFcbLinks);

    //
    // NameBuffer is embedded in the LCB allocated by UDFInsertPrefix.
    // Never pass this interior LCB pointer to a pool free routine.
    //
    Lcb->NameBuffer = NULL;

    /* Rename may allocate these two names outside the LCB. Their embedded
     * forms and ExactCaseLinkName (an alias of FileName) own no allocation. */
    if (Lcb->IgnoreCaseLinkName.Buffer &&
        Lcb->IgnoreCaseLinkName.Buffer != Lcb->FileName.Buffer &&
        ((PUCHAR)Lcb->IgnoreCaseLinkName.Buffer < (PUCHAR)Lcb ||
         (PUCHAR)Lcb->IgnoreCaseLinkName.Buffer >= (PUCHAR)Lcb + Lcb->NodeIdentifier.NodeByteSize))
        ExFreePool(Lcb->IgnoreCaseLinkName.Buffer);
    if (Lcb->ShortName.Buffer &&
        Lcb->ShortName.Buffer != Lcb->FileName.Buffer &&
        Lcb->ShortName.Buffer != Lcb->IgnoreCaseLinkName.Buffer &&
        ((PUCHAR)Lcb->ShortName.Buffer < (PUCHAR)Lcb ||
         (PUCHAR)Lcb->ShortName.Buffer >= (PUCHAR)Lcb + Lcb->NodeIdentifier.NodeByteSize))
        ExFreePool(Lcb->ShortName.Buffer);

    //
    // Free FileName.Buffer if it was separately allocated (e.g., after rename)
    // Check if it's not pointing to embedded buffer (within LCB structure)
    //
    if (Lcb->FileName.Buffer != NULL) {
        BOOLEAN IsEmbedded = ((PUCHAR)Lcb->FileName.Buffer >= (PUCHAR)Lcb &&
                              (PUCHAR)Lcb->FileName.Buffer < (PUCHAR)Lcb + Lcb->NodeIdentifier.NodeByteSize);
        if (!IsEmbedded) {
            MyFreePool__(Lcb->FileName.Buffer);
            Lcb->FileName.Buffer = NULL;
        }
    }

    //
    // Free Name.Buffer if present (ANSI name)
    //
    if (Lcb->Name.Buffer != NULL) {
        ExFreePool(Lcb->Name.Buffer);
        Lcb->Name.Buffer = NULL;
    }

    //
    // Free the LCB structure itself based on how it was allocated
    //
    if (FlagOn(Lcb->Flags, UDF_LCB_FLAG_POOL_ALLOCATED)) {

        ExFreePool(Lcb);

    } else {

        ExFreeToPagedLookasideList(&UdfData.LcbLookasideList, Lcb);
    }
} // end UDFRemovePrefix()


/*
    This routine finds an existing LCB linking the parent FCB to child FCB.
    Returns NULL if no such LCB exists.
*/
PLCB
UDFFindPrefix(
    IN PIRP_CONTEXT IrpContext,
    IN PFCB ParentFcb,
    IN PFCB ChildFcb
    )
{
    PLIST_ENTRY ListEntry;
    PLCB Lcb;

    UNREFERENCED_PARAMETER(IrpContext);

    ASSERT(ParentFcb);
    ASSERT(ChildFcb);

    //
    // Walk the child FCB's ParentLcbQueue to find LCB pointing to this parent
    //
    for (ListEntry = ChildFcb->ParentLcbQueue.Flink;
         ListEntry != &ChildFcb->ParentLcbQueue;
         ListEntry = ListEntry->Flink) {

        Lcb = CONTAINING_RECORD(ListEntry, struct LCB, ChildFcbLinks);

        ASSERT(Lcb->NodeIdentifier.NodeTypeCode == UDF_NODE_TYPE_LCB);

        if (Lcb->ParentFcb == ParentFcb) {
            return Lcb;
        }
    }

    return NULL;
} // end UDFFindPrefix()


/*
    This routine acquires or creates an LCB linking the parent FCB to child FCB.
    An existing LCB keeps its cache ownership; this call acquires no CCB pin.
    A new LCB consumes one checked exact-parent FI and FCB hold, acquired
    before publication. UDFCompleteFcbOpen adds the separate successful CCB pin.

    This is the primary function to call when establishing a parent-child
    relationship during file open.

    Returns the exact status and LCB output; a new LCB consumes pre-acquired
    exact-parent FI/FCB holds. Failure rolls private holds back without I/O.
*/
NTSTATUS
UDFAcquirePrefix(
    IN PIRP_CONTEXT IrpContext,
    IN PFCB ParentFcb,
    IN PFCB ChildFcb,
    IN PUDF_FILE_INFO LinkFileInfo,
    IN PUNICODE_STRING FileName OPTIONAL,
    IN PUNICODE_STRING CaseFileName OPTIONAL,
    IN PUNICODE_STRING ShortName OPTIONAL,
    IN ULONGLONG InitialOffset,
    OUT PLCB* Result
    )
{
    PLIST_ENTRY Entry;
    PVCB Vcb;
    _SEH2_VOLATILE PUDF_FILE_INFO ParentReference = NULL;
    _SEH2_VOLATILE BOOLEAN ParentAcquired = FALSE, ParentPinned = FALSE;
    _SEH2_VOLATILE BOOLEAN Transferred = FALSE;
    _SEH2_VOLATILE NTSTATUS Status = STATUS_SUCCESS;
    PLCB Lcb;
    if (!Result) return STATUS_INVALID_PARAMETER;
    *Result = NULL;
    if (!ParentFcb || !ChildFcb || !LinkFileInfo ||
        ParentFcb->Vcb != ChildFcb->Vcb || ParentFcb == ChildFcb ||
        !LinkFileInfo->Dloc || LinkFileInfo->Fcb != ChildFcb ||
        LinkFileInfo->Dloc->CommonFcb != ChildFcb ||
        !LinkFileInfo->ParentFile || !LinkFileInfo->ParentFile->Dloc ||
        LinkFileInfo->ParentFile->Fcb != ParentFcb)
        return STATUS_FILE_INVALID;
    Vcb = ChildFcb->Vcb;
    ASSERT_EXCLUSIVE_FCB(ChildFcb);
    if (ChildFcb->TeardownInProgress || ParentFcb->TeardownInProgress)
        return STATUS_FILE_CORRUPT_ERROR;
    for (Entry = ChildFcb->ParentLcbQueue.Flink;
         Entry != &ChildFcb->ParentLcbQueue; Entry = Entry->Flink) {
        PLCB Candidate = CONTAINING_RECORD(Entry, struct LCB, ChildFcbLinks);
        if (Candidate->ParentFcb == ParentFcb && Candidate->LinkFileInfo == LinkFileInfo &&
            !(Candidate->Flags & UDF_LCB_FLAG_LINK_DELETED)) {
            if (Candidate->ParentFileInfo != LinkFileInfo->ParentFile)
                return STATUS_FILE_CORRUPT_ERROR;
            *Result = Candidate;
            return STATUS_SUCCESS; /* existing cache owner, no new references */
        }
    }
    /* A new name mutates the parent's cache. Never wait/upgrade upwards
     * from child Main; existing LCB lookup above needs only child Main. */
    if (!ExIsResourceAcquiredExclusiveLite(&ParentFcb->FcbNonpaged->FcbResource)) {
        if (ExIsResourceAcquiredSharedLite(&ParentFcb->FcbNonpaged->FcbResource) ||
            !UDFAcquireFcbExclusive(IrpContext, ParentFcb, TRUE))
            return STATUS_DEVICE_BUSY;
        ParentAcquired = TRUE;
    }
    _SEH2_TRY {
        /* Acquire the exact FI first, with no fast mutex held. Reserve the
         * corresponding FCB hold before allocating/publishing the LCB. */
        Status = UDFReferenceFileChecked(Vcb, LinkFileInfo->ParentFile);
        if (Status != STATUS_SUCCESS) _SEH2_LEAVE;
        ParentReference = LinkFileInfo->ParentFile;
        UDFLockVcb(IrpContext, Vcb);
        _SEH2_TRY {
            if (ParentFcb->FcbReference == MAXULONG || ParentFcb->TeardownInProgress)
                Status = STATUS_FILE_CORRUPT_ERROR;
            else {
                ParentFcb->FcbReference++;
                ParentPinned = TRUE;
            }
        } _SEH2_FINALLY {
            UDFUnlockVcb(IrpContext, Vcb);
        } _SEH2_END;
        if (Status != STATUS_SUCCESS) _SEH2_LEAVE;
        Lcb = UDFInsertPrefix(IrpContext, ParentFcb, ChildFcb, LinkFileInfo,
                              FileName, CaseFileName, ShortName, InitialOffset);
        if (!Lcb) {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            _SEH2_LEAVE;
        }
        /* Exact FI/parent fields were initialized before queue/name-tree
         * publication. No fallible work remains before this transfer. */
        Transferred = TRUE;
        *Result = Lcb;
    } _SEH2_FINALLY {
        _SEH2_TRY {
            if (!Transferred && ParentReference) {
                NTSTATUS ReleaseStatus = UDFReleaseCreateReference(IrpContext, Vcb,
                    (PUDF_FILE_INFO*)&ParentReference, FALSE);
                if (Status == STATUS_SUCCESS) Status = ReleaseStatus;
            }
        } _SEH2_FINALLY {
            if (!Transferred && ParentPinned) {
                UDFLockVcb(IrpContext, Vcb);
                _SEH2_TRY {
                    ASSERT(ParentFcb->FcbReference);
                    ParentFcb->FcbReference--;
                    if (!ParentFcb->FcbReference &&
                        FlagOn(ParentFcb->FcbState, FCB_STATE_IN_FCB_TABLE) &&
                        !ParentFcb->TeardownRetryPending) {
                        ParentFcb->TeardownRetryPending = TRUE;
                        InterlockedIncrement(&Vcb->ClosedFcbRetryCount);
                    }
                } _SEH2_FINALLY {
                    UDFUnlockVcb(IrpContext, Vcb);
                } _SEH2_END;
            }
            if (ParentAcquired) UDFReleaseFcb(IrpContext, ParentFcb);
        } _SEH2_END;
    } _SEH2_END;
    return Status;
} // end UDFAcquirePrefix()


/*
    This routine releases a reference to an LCB.
    If Reference goes to zero, the LCB is eligible for removal during teardown.

    Note: This does NOT remove the LCB or decrement parent's FcbReference.
    That is done during teardown when walking the ParentLcbQueue.
*/
VOID
UDFReleasePrefix(
    IN PIRP_CONTEXT IrpContext,
    IN PLCB Lcb
    )
{
    UNREFERENCED_PARAMETER(IrpContext);

    if (!Lcb) {
        return;
    }

    ASSERT(Lcb->NodeIdentifier.NodeTypeCode == UDF_NODE_TYPE_LCB);
    ASSERT(Lcb->Reference > 0);

    InterlockedDecrement((PLONG)&Lcb->Reference);
} // end UDFReleasePrefix()


/* A full path is a caller-owned snapshot, never an LCB lookup key.
 * Acquire ancestors without waiting: callers may already hold a child FCB,
 * so blocking upwards would invert rename's parent-before-child order.
 * Keep every acquired FCB until both sizing and copying are complete. */
typedef struct _UDF_PATH_COMPONENT {
    struct _UDF_PATH_COMPONENT *Next;
    PFCB Fcb;
    PLCB Lcb;
    BOOLEAN Acquired;
} UDF_PATH_COMPONENT, *PUDF_PATH_COMPONENT;

NTSTATUS
UDFBuildFullPathFromLcb(
    IN PIRP_CONTEXT IrpContext,
    IN PLCB Lcb,
    OUT PUNICODE_STRING FullPath
    )
{
    _SEH2_VOLATILE PUDF_PATH_COMPONENT Components = NULL;
    PUDF_PATH_COMPONENT Component, Previous;
    PFCB CurrentFcb, ParentFcb;
    PLCB CurrentLcb;
    PLIST_ENTRY Link;
    ULONG TotalLength = 0;
    _SEH2_VOLATILE PWCHAR Buffer = NULL;
    PWCHAR Position;
    NTSTATUS Status = STATUS_SUCCESS;
    PVCB Vcb;
    UNREFERENCED_PARAMETER(IrpContext);

    RtlZeroMemory(FullPath, sizeof(*FullPath));
    if (!Lcb || !Lcb->ChildFcb)
        return STATUS_INVALID_PARAMETER;
    CurrentFcb = Lcb->ChildFcb;
    CurrentLcb = Lcb;
    Vcb = CurrentFcb->Vcb;
    if (!ExAcquireResourceSharedLite(&Vcb->VcbResource, FALSE))
        return STATUS_SHARING_VIOLATION;

    _SEH2_TRY {
        for (;;) {
            Component = ExAllocatePoolWithTag(PagedPool, sizeof(*Component), TAG_FILE_NAME);
            if (!Component) {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                _SEH2_LEAVE;
            }
            RtlZeroMemory(Component, sizeof(*Component));
            Component->Fcb = CurrentFcb;
            Component->Next = Components;
            Components = Component;
            if (!ExAcquireResourceSharedLite(&CurrentFcb->FcbNonpaged->FcbResource, FALSE)) {
                Status = STATUS_SHARING_VIOLATION;
                _SEH2_LEAVE;
            }
            Component->Acquired = TRUE;
            if (FlagOn(CurrentFcb->FcbState, UDF_FCB_ROOT_DIRECTORY))
                break;

            if (!CurrentLcb) {
                for (Link = CurrentFcb->ParentLcbQueue.Flink;
                     Link != &CurrentFcb->ParentLcbQueue; Link = Link->Flink) {
                    PLCB Candidate = CONTAINING_RECORD(Link, struct LCB, ChildFcbLinks);
                    if (!FlagOn(Candidate->Flags, UDF_LCB_FLAG_LINK_DELETED)) {
                        CurrentLcb = Candidate;
                        break;
                    }
                }
            }
            if (!CurrentLcb || CurrentLcb->ChildFcb != CurrentFcb ||
                FlagOn(CurrentLcb->Flags, UDF_LCB_FLAG_LINK_DELETED) ||
                !CurrentLcb->ExactCaseLinkName.Buffer || !CurrentLcb->ExactCaseLinkName.Length ||
                CurrentLcb->ExactCaseLinkName.Length % sizeof(WCHAR) ||
                CurrentLcb->ExactCaseLinkName.Length > CurrentLcb->ExactCaseLinkName.MaximumLength) {
                Status = STATUS_FILE_INVALID;
                _SEH2_LEAVE;
            }
            Component->Lcb = CurrentLcb;
            /* UNICODE_STRING's terminator must fit too. Use a wide sum,
             * then narrow only after the complete locked chain is known. */
            if (TotalLength + CurrentLcb->ExactCaseLinkName.Length + sizeof(WCHAR) >
                MAXUSHORT - sizeof(WCHAR)) {
                Status = STATUS_NAME_TOO_LONG;
                _SEH2_LEAVE;
            }
            TotalLength += CurrentLcb->ExactCaseLinkName.Length + sizeof(WCHAR);
            ParentFcb = CurrentLcb->ParentFcb;
            if (!ParentFcb || ParentFcb->Vcb != CurrentFcb->Vcb) {
                Status = STATUS_FILE_CORRUPT_ERROR;
                _SEH2_LEAVE;
            }
            /* A corrupt parent cycle must neither loop nor recursively
             * acquire the same resource an unbounded number of times. */
            for (Previous = Components; Previous; Previous = Previous->Next) {
                if (Previous->Fcb == ParentFcb) {
                    Status = STATUS_FILE_CORRUPT_ERROR;
                    _SEH2_LEAVE;
                }
            }
            CurrentFcb = ParentFcb;
            CurrentLcb = NULL;
        }
        if (!TotalLength)
            TotalLength = sizeof(WCHAR); /* root */
        Buffer = ExAllocatePoolWithTag(PagedPool, TotalLength + sizeof(WCHAR), TAG_FILE_NAME);
        if (!Buffer) {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            _SEH2_LEAVE;
        }
        Position = Buffer;
        for (Component = Components; Component; Component = Component->Next) {
            if (Component->Lcb) {
                PUNICODE_STRING Name = &Component->Lcb->ExactCaseLinkName;
                *Position++ = L'\\';
                RtlCopyMemory(Position, Name->Buffer, Name->Length);
                Position += Name->Length / sizeof(WCHAR);
            }
        }
        if (Position == Buffer)
            *Position++ = L'\\';
        *Position = 0;
        FullPath->Buffer = Buffer;
        FullPath->Length = (USHORT)TotalLength;
        FullPath->MaximumLength = (USHORT)(TotalLength + sizeof(WCHAR));
        Buffer = NULL;
    } _SEH2_FINALLY {
        if (Buffer)
            ExFreePool(Buffer);
        while (Components) {
            Component = Components;
            Components = Component->Next;
            if (Component->Acquired)
                ExReleaseResourceLite(&Component->Fcb->FcbNonpaged->FcbResource);
            ExFreePool(Component);
        }
        ExReleaseResourceLite(&Vcb->VcbResource);
    } _SEH2_END;
    return Status;
} // end UDFBuildFullPathFromLcb()


/*
    This routine releases a reference to an LCB and, if Reference becomes 0,
    immediately removes the LCB and decrements parent references.

    This is used during rename operations where we need immediate cleanup
    of the old parent link rather than waiting for teardown.

    Parameters:
        IrpContext - The IRP context
        Lcb - The LCB to release
        CloseParentFileInfo - If TRUE and LCB is removed, call UDFCloseFile__
                              on parent's FileInfo

    Returns:
        TRUE if the LCB was removed, FALSE if it still has references
*/
BOOLEAN
UDFReleasePrefixImmediate(
    IN PIRP_CONTEXT IrpContext,
    IN PLCB Lcb,
    IN BOOLEAN CloseParentFileInfo
    )
{
    PFCB ParentFcb;
    PUDF_FILE_INFO ParentFileInfo;
    PVCB Vcb;
    LONG NewRef;

    if (!Lcb) {
        return FALSE;
    }

    ASSERT(Lcb->NodeIdentifier.NodeTypeCode == UDF_NODE_TYPE_LCB);
    ASSERT(Lcb->Reference > 0);

    //
    // Decrement reference
    //
    NewRef = InterlockedDecrement((PLONG)&Lcb->Reference);

    if (NewRef > 0) {
        //
        // Still has references, leave it in place
        //
        return FALSE;
    }

    //
    // Reference is now 0 - remove LCB and decrement parent refs
    //
    ParentFcb = Lcb->ParentFcb;
    ParentFileInfo = Lcb->ParentFileInfo;
    Vcb = ParentFcb ? ParentFcb->Vcb : NULL;

    //
    // Remove LCB from queues and free it
    //
    UDFRemovePrefix(IrpContext, Lcb);

    //
    // Decrement parent's FcbReference
    //
    if (ParentFcb) {
        ASSERT(ParentFcb->FcbReference > 0);
        InterlockedDecrement((PLONG)&ParentFcb->FcbReference);

        //
        // Close parent's FileInfo if requested
        //
        if (CloseParentFileInfo && ParentFileInfo && Vcb) {
            NTSTATUS Status = UDFCloseFile__(IrpContext, Vcb, ParentFileInfo);
            UDFRememberMetadataWritebackError(IrpContext, Vcb, ParentFileInfo, Status);
        }
    }

    return TRUE;
} // end UDFReleasePrefixImmediate()


/*
    This routine searches for the maximum known prefix path in the LCB tree.

    Starting from StartFcb, walks through path components in RemainingName,
    looking for matching LCBs in the ChildLcbQueue. Returns the LCB of the
    deepest matched component and updates RemainingName to the unmatched portion.

    This optimization avoids redundant directory traversal - instead of always
    starting from root, we find the deepest already-opened directory in the
    path and start from there.

    Parameters:
        IrpContext - IRP context
        StartFcb - Starting FCB (usually RootIndexFcb or RelatedFcb)
        IgnoreCase - Whether to use case-insensitive comparison
        CurrentFcb - Output: FCB of the deepest matched component
        RemainingName - Input: full path to search; Output: unmatched portion
        ShortNameMatch - Output: TRUE if match was via short name

    Returns:
        PLCB - LCB of the deepest matched component, or NULL if no match
*/
/*
    Walk path components starting from StartFcb, looking for the longest
    match in the in-memory LCB tree.

    On entry:  *CurrentFcb == StartFcb, caller holds StartFcb FcbResource exclusive.
    On return: *CurrentFcb == deepest matched FCB (locked exclusive),
               all ancestor FCB locks released.
               RemainingName is advanced past matched components.
    Returns the LCB used to reach *CurrentFcb, or NULL if no prefix found.
 */
PLCB
UDFFindPathPrefix(
    IN PIRP_CONTEXT IrpContext,
    IN PFCB StartFcb,
    IN BOOLEAN IgnoreCase,
    IN OUT PFCB *CurrentFcb,
    IN OUT PUNICODE_STRING RemainingName,
    OUT PBOOLEAN ShortNameMatch
    )
{
    PLCB CurrentLcb = NULL;
    PLCB MatchLcb;
    BOOLEAN ComponentShortName;
    UNICODE_STRING LocalRemainingName;
    UNICODE_STRING ComponentName;
    WCHAR UpcaseBuffer[256];
    UNICODE_STRING UpcaseName;

    ASSERT(StartFcb);
    ASSERT(CurrentFcb);
    ASSERT(RemainingName);
    ASSERT(ShortNameMatch);

    *ShortNameMatch = FALSE;
    *CurrentFcb = StartFcb;
    LocalRemainingName = *RemainingName;

    while (TRUE) {

        // Stop if no more path components or current FCB is not a directory
        if (LocalRemainingName.Length == 0 ||
            !(*CurrentFcb) ||
            !((*CurrentFcb)->FcbState & UDF_FCB_DIRECTORY)) {
            return CurrentLcb;
        }

        // Skip leading backslashes
        while (LocalRemainingName.Length >= sizeof(WCHAR) &&
               LocalRemainingName.Buffer[0] == L'\\') {
            LocalRemainingName.Buffer++;
            LocalRemainingName.Length -= sizeof(WCHAR);
        }

        if (LocalRemainingName.Length == 0) {
            return CurrentLcb;
        }

        // Stop at stream boundary
        if (LocalRemainingName.Buffer[0] == L':') {
            return CurrentLcb;
        }

        // Extract next component (up to next '\' or ':' or end)
        ComponentName.Buffer = LocalRemainingName.Buffer;
        ComponentName.Length = 0;
        while (ComponentName.Length < LocalRemainingName.Length) {
            WCHAR ch = ComponentName.Buffer[ComponentName.Length / sizeof(WCHAR)];
            if (ch == L'\\' || ch == L':') {
                break;
            }
            ComponentName.Length += sizeof(WCHAR);
        }
        if (ComponentName.Length == 0) {
            return CurrentLcb;
        }
        ComponentName.MaximumLength = ComponentName.Length;

        // Advance local remaining name past this component
        LocalRemainingName.Buffer += ComponentName.Length / sizeof(WCHAR);
        LocalRemainingName.Length -= ComponentName.Length;

        //
        // Search for matching component. Try splay trees first (O(log n)),
        // then fall back to list iteration (O(n)) for LCBs without names,
        // duplicate-name LCBs, or names too long for upcase buffer.
        // *CurrentFcb is held exclusive, so trees and queue are safe.
        //
        MatchLcb = NULL;
        ComponentShortName = FALSE;

        // Splay tree search: try IgnoreCase or ExactCase tree
        if (IgnoreCase && (*CurrentFcb)->IgnoreCaseRoot != NULL) {
            // Upcase component for ignore-case tree lookup
            if (ComponentName.Length <= sizeof(UpcaseBuffer)) {
                UpcaseName.Buffer = UpcaseBuffer;
                UpcaseName.MaximumLength = sizeof(UpcaseBuffer);
                UpcaseName.Length = ComponentName.Length;
                RtlCopyMemory(UpcaseBuffer, ComponentName.Buffer, ComponentName.Length);
                RtlUpcaseUnicodeString(&UpcaseName, &UpcaseName, FALSE);

                MatchLcb = UdfFindNameLink(&(*CurrentFcb)->IgnoreCaseRoot,
                                            &UpcaseName,
                                            FIELD_OFFSET(struct LCB, IgnoreCaseLinks),
                                            FIELD_OFFSET(struct LCB, IgnoreCaseLinkName));
            }
        } else if (!IgnoreCase && (*CurrentFcb)->ExactCaseRoot != NULL) {
            MatchLcb = UdfFindNameLink(&(*CurrentFcb)->ExactCaseRoot,
                                        &ComponentName,
                                        FIELD_OFFSET(struct LCB, ExactCaseLinks),
                                        FIELD_OFFSET(struct LCB, FileName));
        }

        // Try short name splay tree if no match yet
        if (!MatchLcb && (*CurrentFcb)->ShortNameRoot != NULL) {
            if (ComponentName.Length <= sizeof(UpcaseBuffer)) {
                UpcaseName.Buffer = UpcaseBuffer;
                UpcaseName.MaximumLength = sizeof(UpcaseBuffer);
                UpcaseName.Length = ComponentName.Length;
                RtlCopyMemory(UpcaseBuffer, ComponentName.Buffer, ComponentName.Length);
                RtlUpcaseUnicodeString(&UpcaseName, &UpcaseName, FALSE);

                MatchLcb = UdfFindNameLink(&(*CurrentFcb)->ShortNameRoot,
                                            &UpcaseName,
                                            FIELD_OFFSET(struct LCB, ShortNameLinks),
                                            FIELD_OFFSET(struct LCB, ShortName));
                if (MatchLcb) {
                    ComponentShortName = TRUE;
                }
            }
        }

        // List fallback: catches LCBs without names in splay trees
        // (NULL names, duplicates, long names) and handles deleted-link filtering.
        if (!MatchLcb) {
            for (PLIST_ENTRY ListEntry = (*CurrentFcb)->ChildLcbQueue.Flink;
                 ListEntry != &(*CurrentFcb)->ChildLcbQueue;
                 ListEntry = ListEntry->Flink) {

                PLCB Lcb = CONTAINING_RECORD(ListEntry, struct LCB, ParentFcbLinks);

                ASSERT(Lcb->NodeIdentifier.NodeTypeCode == UDF_NODE_TYPE_LCB);
                ASSERT(Lcb->ParentFcb == *CurrentFcb);

                if (FlagOn(Lcb->Flags, UDF_LCB_FLAG_LINK_DELETED)) {
                    continue;
                }

                // Try FileName (exact or case-insensitive)
                if (Lcb->FileName.Buffer && Lcb->FileName.Length > 0 &&
                    RtlEqualUnicodeString(&ComponentName, &Lcb->FileName, IgnoreCase)) {
                    MatchLcb = Lcb;
                    break;
                }
                // Try IgnoreCaseLinkName
                if (IgnoreCase &&
                    Lcb->IgnoreCaseLinkName.Buffer && Lcb->IgnoreCaseLinkName.Length > 0 &&
                    RtlEqualUnicodeString(&ComponentName, &Lcb->IgnoreCaseLinkName, TRUE)) {
                    MatchLcb = Lcb;
                    break;
                }
                // Try ShortName
                if (Lcb->ShortName.Buffer && Lcb->ShortName.Length > 0 &&
                    RtlEqualUnicodeString(&ComponentName, &Lcb->ShortName, TRUE)) {
                    MatchLcb = Lcb;
                    ComponentShortName = TRUE;
                    break;
                }
            }
        }

        /* An uncertain final GC keeps its prefix structures as exact
         * owners, but their borrowed FI may already be partly retired.
         * Do not descend/reopen through a name-table bypass of the gated
         * FCB table entry, and do not reinterpret this as a missing name. */
        if (MatchLcb && MatchLcb->ChildFcb &&
            MatchLcb->ChildFcb->TeardownInProgress)
            UDFRaiseStatus(IrpContext, STATUS_FILE_CORRUPT_ERROR);

        //
        // Validate the match: skip deleted links and deleted FCBs.
        // A deleted FCB can still have LCBs in the splay tree/list if
        // teardown hasn't run yet (between cleanup and close).
        // Clean up stale splay entries when found.
        //
        if (MatchLcb && !FlagOn(MatchLcb->Flags, UDF_LCB_FLAG_LINK_DELETED) &&
            MatchLcb->ChildFcb &&
            (MatchLcb->ChildFcb->FcbState & UDF_FCB_DELETED)) {
            // Child FCB was deleted — remove LCB from splay trees
            // (we hold ParentFcb exclusive) and mark as deleted.
            UdfRemoveNameLinks(*CurrentFcb, MatchLcb);
            MatchLcb->Flags |= UDF_LCB_FLAG_LINK_DELETED;
            MatchLcb = NULL;
        }

        if (!MatchLcb || FlagOn(MatchLcb->Flags, UDF_LCB_FLAG_LINK_DELETED)) {
            // No match or deleted link — stop, *CurrentFcb stays locked
            return CurrentLcb;
        }

        PFCB ParentFcb = *CurrentFcb;
        PFCB ChildFcb = MatchLcb->ChildFcb;
        /* Every traversed LCB must describe the exact retained namespace
         * edge, not merely a matching string in a stale name tree. */
        if (!ChildFcb || !MatchLcb->LinkFileInfo ||
            !MatchLcb->LinkFileInfo->Dloc ||
            MatchLcb->LinkFileInfo->Fcb != ChildFcb ||
            MatchLcb->LinkFileInfo->Dloc->CommonFcb != ChildFcb ||
            MatchLcb->ParentFcb != ParentFcb || !MatchLcb->ParentFileInfo ||
            MatchLcb->LinkFileInfo->ParentFile != MatchLcb->ParentFileInfo ||
            MatchLcb->ParentFileInfo->Fcb != ParentFcb)
            UDFRaiseStatus(IrpContext, STATUS_FILE_CORRUPT_ERROR);
        if (ChildFcb == ParentFcb) return CurrentLcb;
        UDF_CHECK_PAGING_IO_RESOURCE(ChildFcb);
        /* Prefix lookup is an optimization. On contention keep the current
         * directory resource and unmatched name; ordinary traversal owns
         * the blocking FI/FCB handoff. No temporary cache pin or wait here. */
        if (ExIsResourceAcquiredSharedLite(&ChildFcb->FcbNonpaged->FcbResource) &&
            !ExIsResourceAcquiredExclusiveLite(&ChildFcb->FcbNonpaged->FcbResource))
            return CurrentLcb;
        if (!UDFAcquireFcbExclusive(IrpContext, ChildFcb, TRUE)) return CurrentLcb;
        *CurrentFcb = ChildFcb; /* authoritative caller slot, before next work */
        UDFReleaseFcb(IrpContext, ParentFcb);
        CurrentLcb = MatchLcb;
        *RemainingName = LocalRemainingName;
        *ShortNameMatch = *ShortNameMatch || ComponentShortName;
    }

    return CurrentLcb;
} // end UDFFindPathPrefix()


/*
    Atomically rename an LCB and optionally move it to a new parent directory.
    Updates the file name, moves between parent ChildLcbQueue lists,
    The prepare/apply/discard protocol reserves names and exact parent
    references before namespace mutation, then transfers or releases them.
*/
NTSTATUS
UDFPreparePrefixRename(
    IN PIRP_CONTEXT IrpContext,
    IN PLCB Lcb,
    IN PUNICODE_STRING NewName,
    IN PUDF_FILE_INFO NewParentFileInfo,
    OUT PUDF_PREFIX_RENAME_PLAN Plan
    )
{
    NTSTATUS Status;
    USHORT Bytes;
    RtlZeroMemory(Plan, sizeof(*Plan));
    if (!Lcb || !NewName || !NewName->Buffer || !NewName->Length ||
        NewName->Length % sizeof(WCHAR) ||
        NewName->Length > MAXUSHORT - sizeof(WCHAR) ||
        !NewParentFileInfo || !NewParentFileInfo->Fcb ||
        !Lcb->LinkFileInfo || !Lcb->ParentFcb || !Lcb->ParentFileInfo ||
        Lcb->LinkFileInfo->ParentFile != Lcb->ParentFileInfo ||
        NewParentFileInfo->Fcb->Vcb != Lcb->ParentFcb->Vcb)
        return STATUS_INVALID_PARAMETER;

    ASSERT_EXCLUSIVE_FCB(Lcb->ChildFcb);
    ASSERT_EXCLUSIVE_FCB(Lcb->ParentFcb);
    ASSERT_EXCLUSIVE_FCB(NewParentFileInfo->Fcb);
    Plan->Lcb = Lcb;
    Plan->LinkFileInfo = Lcb->LinkFileInfo;
    Plan->OldParentFcb = Lcb->ParentFcb;
    Plan->OldParentFileInfo = Lcb->ParentFileInfo;
    Plan->NewParentFcb = NewParentFileInfo->Fcb;
    Plan->NewParentFileInfo = NewParentFileInfo;
    Bytes = NewName->Length + sizeof(WCHAR);
    Plan->FileName.Buffer = MyAllocatePool__(NonPagedPool, Bytes);
    if (!Plan->FileName.Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;
    Plan->FileName.Length = NewName->Length;
    Plan->FileName.MaximumLength = Bytes;
    RtlCopyMemory(Plan->FileName.Buffer, NewName->Buffer, NewName->Length);
    Plan->FileName.Buffer[NewName->Length / sizeof(WCHAR)] = 0;
    Plan->IgnoreCaseName.Buffer = ExAllocatePoolWithTag(PagedPool, Bytes, TAG_FILE_NAME);
    if (!Plan->IgnoreCaseName.Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;
    Plan->IgnoreCaseName.Length = 0;
    Plan->IgnoreCaseName.MaximumLength = Bytes;
    Status = RtlUpcaseUnicodeString(&Plan->IgnoreCaseName, &Plan->FileName, FALSE);
    if (!NT_SUCCESS(Status))
        return Status;
    Plan->IgnoreCaseName.Buffer[NewName->Length / sizeof(WCHAR)] = 0;

    if (Plan->NewParentFileInfo != Plan->OldParentFileInfo ||
        Plan->NewParentFcb != Plan->OldParentFcb) {
        Status = UDFReferenceFileChecked(Plan->NewParentFcb->Vcb, NewParentFileInfo);
        if (!NT_SUCCESS(Status))
            return Status;
        /* No I/O or allocation while the scalar FCB count is protected. */
        UDFLockVcb(IrpContext, Plan->NewParentFcb->Vcb);
        InterlockedIncrement((PLONG)&Plan->NewParentFcb->FcbReference);
        UDFUnlockVcb(IrpContext, Plan->NewParentFcb->Vcb);
        Plan->ParentFileReferenceOwned = TRUE;
        Plan->ParentFcbReferenceOwned = TRUE;
    }
    Plan->Prepared = TRUE;
    return STATUS_SUCCESS;
}

NTSTATUS
UDFDiscardPrefixRename(
    IN PIRP_CONTEXT IrpContext,
    IN OUT PUDF_PREFIX_RENAME_PLAN Plan,
    IN BOOLEAN FlushParent
    )
{
    NTSTATUS Status = STATUS_SUCCESS;
    _SEH2_TRY {
        if (Plan->ParentFileReferenceOwned) {
            Status = UDFDereferenceFileChecked(Plan->NewParentFcb->Vcb,
                                                Plan->NewParentFileInfo);
            if (NT_SUCCESS(Status))
                Plan->ParentFileReferenceOwned = FALSE;
        }
        if (FlushParent && NT_SUCCESS(Status) && Plan->ParentFcbReferenceOwned)
            Status = UDFCompleteFileClose(IrpContext, Plan->NewParentFcb->Vcb,
                                           Plan->NewParentFileInfo);
    } _SEH2_FINALLY {
        if (Plan->ParentFcbReferenceOwned && !Plan->ParentFileReferenceOwned) {
            PVCB Vcb = Plan->NewParentFcb->Vcb;
            UDFLockVcb(IrpContext, Vcb);
            ASSERT(Plan->NewParentFcb->FcbReference);
            InterlockedDecrement((PLONG)&Plan->NewParentFcb->FcbReference);
            UDFUnlockVcb(IrpContext, Vcb);
            Plan->ParentFcbReferenceOwned = FALSE;
        }
        if (Plan->FileName.Buffer) {
            MyFreePool__(Plan->FileName.Buffer);
            RtlZeroMemory(&Plan->FileName, sizeof(Plan->FileName));
        }
        if (Plan->IgnoreCaseName.Buffer) {
            ExFreePool(Plan->IgnoreCaseName.Buffer);
            RtlZeroMemory(&Plan->IgnoreCaseName, sizeof(Plan->IgnoreCaseName));
        }
        Plan->Prepared = FALSE;
    } _SEH2_END;
    return Status;
}

NTSTATUS
UDFApplyPrefixRename(
    IN PIRP_CONTEXT IrpContext,
    IN OUT PUDF_PREFIX_RENAME_PLAN Plan,
    IN BOOLEAN FlushParent
    )
{
    PLCB Lcb = Plan->Lcb;
    NTSTATUS Status = STATUS_SUCCESS;
    BOOLEAN Embedded;
    if (!Plan->Prepared || Plan->Applied || !Lcb ||
        Lcb->ParentFcb != Plan->OldParentFcb ||
        Lcb->ParentFileInfo != Plan->OldParentFileInfo ||
        Lcb->LinkFileInfo != Plan->LinkFileInfo ||
        Lcb->LinkFileInfo->ParentFile != Plan->NewParentFileInfo)
        return STATUS_FILE_INVALID;

    /* Both parent resources and the source FCB remain acquired by the
     * caller. Names and new-parent holds were prepared before the move;
     * old-parent writeback follows the completed tree/queue update. */
    UdfRemoveNameLinks(Plan->OldParentFcb, Lcb);
    Embedded = Lcb->FileName.Buffer &&
        (PUCHAR)Lcb->FileName.Buffer >= (PUCHAR)Lcb &&
        (PUCHAR)Lcb->FileName.Buffer < (PUCHAR)Lcb + Lcb->NodeIdentifier.NodeByteSize;
    if (Lcb->FileName.Buffer && !Embedded) {
        MyFreePool__(Lcb->FileName.Buffer);
    }
    if (Lcb->IgnoreCaseLinkName.Buffer &&
        !((PUCHAR)Lcb->IgnoreCaseLinkName.Buffer >= (PUCHAR)Lcb &&
          (PUCHAR)Lcb->IgnoreCaseLinkName.Buffer < (PUCHAR)Lcb + Lcb->NodeIdentifier.NodeByteSize))
        ExFreePool(Lcb->IgnoreCaseLinkName.Buffer);
    if (Lcb->ShortName.Buffer &&
        !((PUCHAR)Lcb->ShortName.Buffer >= (PUCHAR)Lcb &&
          (PUCHAR)Lcb->ShortName.Buffer < (PUCHAR)Lcb + Lcb->NodeIdentifier.NodeByteSize))
        ExFreePool(Lcb->ShortName.Buffer);
    RtlZeroMemory(&Lcb->ShortName, sizeof(Lcb->ShortName));
    ClearFlag(Lcb->Flags, UDF_LCB_FLAG_SHORT_NAME_CREATED);
    Lcb->FileName = Plan->FileName;
    Lcb->ExactCaseLinkName = Lcb->FileName;
    Lcb->IgnoreCaseLinkName = Plan->IgnoreCaseName;
    RtlZeroMemory(&Plan->FileName, sizeof(Plan->FileName));
    RtlZeroMemory(&Plan->IgnoreCaseName, sizeof(Plan->IgnoreCaseName));

    if (Plan->ParentFileReferenceOwned && Plan->ParentFcbReferenceOwned) {
        if (Plan->NewParentFcb != Plan->OldParentFcb) {
            RemoveEntryList(&Lcb->ParentFcbLinks);
            InsertHeadList(&Plan->NewParentFcb->ChildLcbQueue, &Lcb->ParentFcbLinks);
        }
        Lcb->ParentFcb = Plan->NewParentFcb;
        Lcb->ParentFileInfo = Plan->NewParentFileInfo;
        Plan->ParentFileReferenceOwned = FALSE; /* transferred into the LCB */
        Plan->ParentFcbReferenceOwned = FALSE;
    }
    Lcb->InitialOffset = Lcb->LinkFileInfo->Index;
    UdfInsertNameLinks(Lcb->ParentFcb, Lcb);
    Plan->Applied = TRUE;

    if (Plan->NewParentFileInfo != Plan->OldParentFileInfo ||
        Plan->NewParentFcb != Plan->OldParentFcb) {
        /* Finish publishing the new trees/queues before the old parent's
         * last close can flush and fail. An error never leaves an old name
         * attached to an FI that already moved to the new parent. */
        Status = UDFDereferenceFileChecked(Plan->OldParentFcb->Vcb,
                                            Plan->OldParentFileInfo);
        if (NT_SUCCESS(Status)) {
            _SEH2_TRY {
                if (FlushParent)
                    Status = UDFCompleteFileClose(IrpContext, Plan->OldParentFcb->Vcb,
                                                   Plan->OldParentFileInfo);
            } _SEH2_FINALLY {
                UDFLockVcb(IrpContext, Plan->OldParentFcb->Vcb);
                ASSERT(Plan->OldParentFcb->FcbReference);
                InterlockedDecrement((PLONG)&Plan->OldParentFcb->FcbReference);
                UDFUnlockVcb(IrpContext, Plan->OldParentFcb->Vcb);
            } _SEH2_END;
        }
    }
    return Status;
}

