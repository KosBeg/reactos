////////////////////////////////////////////////////////////////////
// Copyright (C) Alexander Telyatnikov, Ivan Keliukh, Yegor Anchishkin, SKIF Software, 1999-2013. Kiev, Ukraine
// All rights reserved
// This file was released under the GPLv2 on June 2015.
////////////////////////////////////////////////////////////////////
/*************************************************************************
*
* File: Fileinfo.cpp
*
* Module: UDF File System Driver (Kernel mode execution only)
*
* Description:
*   Contains code to handle the "set/query file information" dispatch
*   entry points.
*
*************************************************************************/

#include            "udffs.h"

// define the file specific bug-check id
#define         UDF_BUG_CHECK_ID                UDF_FILE_INFORMATION

#define         MEM_USREN_TAG                   "US_Ren"
#define         MEM_USREN2_TAG                  "US_Ren2"
#define         MEM_USFIDC_TAG                  "US_FIDC"
#define         MEM_USHL_TAG                    "US_HL"

/*************************************************************************
*
* Function: UDFCommonQueryInfo()
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
UDFCommonQueryInfo(
    PIRP_CONTEXT IrpContext,
    PIRP             Irp
    )
{
    NTSTATUS Status = STATUS_SUCCESS;
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);

    LONG Length;
    FILE_INFORMATION_CLASS FileInformationClass;
    PVOID Buffer;
    TYPE_OF_OPEN TypeOfOpen;
    PFCB Fcb;
    PCCB Ccb;
    BOOLEAN ReleaseFcb = FALSE;
    BOOLEAN ReleaseVolume = FALSE;

    PAGED_CODE();

    // Reference our input parameters to make things easier

    Length = IrpSp->Parameters.QueryFile.Length;
    FileInformationClass = IrpSp->Parameters.QueryFile.FileInformationClass;
    Buffer = Irp->AssociatedIrp.SystemBuffer;

    // Decode the file object

    TypeOfOpen = UDFDecodeFileObject(IrpSp->FileObject, &Fcb, &Ccb);

    // An unopened or stream file object has no user CCB.  Reject it before
    // the debug assertions dereference the decoded pointers.
    if ((TypeOfOpen != UserDirectoryOpen && TypeOfOpen != UserFileOpen) ||
        Fcb == NULL || Ccb == NULL) {
        UDFCompleteRequest(IrpContext, Irp, STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }

    ASSERT_CCB(Ccb);
    ASSERT_FCB(Fcb);

    _SEH2_TRY {
        /* Stream and name queries can follow weak FI cache edges. Join the
         * metadata lifetime domain before taking the data FCB resource. */
        UDFAcquireVcbShared(IrpContext, Fcb->Vcb, FALSE);
        ReleaseVolume = TRUE;

        // We only support query on file and directory handles.

        switch (TypeOfOpen) {

        case UserDirectoryOpen:
        case UserFileOpen:

            if (FileInformationClass == FileStreamInformation) {
                UDFAcquireFcbExclusive(IrpContext, Fcb, FALSE);
            } else {
                UDFAcquireFcbShared(IrpContext, Fcb, FALSE);
            }
            ReleaseFcb = TRUE;

            // Make sure the Fcb is in a usable condition.  This will raise
            // an error condition if the volume is unusable

            UDFVerifyFcbOperation(IrpContext, Fcb, Ccb);

            // Do whatever the caller asked us to do
            switch (FileInformationClass) {
            case FileBasicInformation:
                Status = UDFGetBasicInformation(IrpSp->FileObject, Fcb, (PFILE_BASIC_INFORMATION)Buffer, &Length);
                break;
            case FileStandardInformation:
                Status = UDFGetStandardInformation(Fcb, Ccb, (PFILE_STANDARD_INFORMATION)Buffer, &Length);
                break;
            case FileNetworkOpenInformation:
                Status = UDFGetNetworkInformation(Fcb, (PFILE_NETWORK_OPEN_INFORMATION)Buffer, &Length);
                break;
            case FileInternalInformation:
                Status = UDFGetInternalInformation(IrpContext, Fcb, (PFILE_INTERNAL_INFORMATION)Buffer, &Length);
                break;
            case FileEaInformation:
                Status = UDFGetEaInformation(IrpContext, Fcb, (PFILE_EA_INFORMATION)Buffer, &Length);
                break;
            case FileNameInformation:

                // We don't allow this operation on a file opened by file Id.

                if (FlagOn(Ccb->Flags, CCB_FLAG_OPEN_BY_ID)) {

                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }

                Status = UDFGetFullNameInformation(IrpSp->FileObject, (PFILE_NAME_INFORMATION)Buffer, &Length);
                break;
            case FileAlternateNameInformation:
                Status = UDFGetAltNameInformation(Fcb, Ccb, (PFILE_NAME_INFORMATION)Buffer, &Length);
                break;
            //TODO: impl
    //            case FileCompressionInformation:
    //                // Status = UDFGetCompressionInformation(...);
    //                break;
            case FilePositionInformation:
                Status = UDFGetPositionInformation(IrpSp->FileObject, (PFILE_POSITION_INFORMATION)Buffer, &Length);
                break;
            case FileStreamInformation:
                Status = UDFGetFileStreamInformation(IrpContext, Fcb, (PFILE_STREAM_INFORMATION)Buffer, (PULONG)&Length);
                break;
            case FileAllInformation:
                {
                    // We don't allow this operation on a file opened by file Id.

                    if (FlagOn(Ccb->Flags, CCB_FLAG_OPEN_BY_ID)) {

                        Status = STATUS_INVALID_PARAMETER;
                        break;
                    }

                    PFILE_ALL_INFORMATION PtrAllInfo = (PFILE_ALL_INFORMATION)Buffer;

                    Length -= (sizeof(FILE_MODE_INFORMATION) +
                               sizeof(FILE_ACCESS_INFORMATION) +
                               sizeof(FILE_ALIGNMENT_INFORMATION));

                    // Get the remaining stuff.
                    if (!NT_SUCCESS(Status = UDFGetBasicInformation(IrpSp->FileObject, Fcb, &(PtrAllInfo->BasicInformation), &Length)) ||
                        !NT_SUCCESS(Status = UDFGetStandardInformation(Fcb, Ccb, &(PtrAllInfo->StandardInformation), &Length)) ||
                        !NT_SUCCESS(Status = UDFGetInternalInformation(IrpContext, Fcb, &(PtrAllInfo->InternalInformation), &Length)) ||
                        !NT_SUCCESS(Status = UDFGetEaInformation(IrpContext, Fcb, &(PtrAllInfo->EaInformation), &Length)) ||
                        !NT_SUCCESS(Status = UDFGetPositionInformation(IrpSp->FileObject, &(PtrAllInfo->PositionInformation), &Length)) ||
                        !NT_SUCCESS(Status = UDFGetFullNameInformation(IrpSp->FileObject, &(PtrAllInfo->NameInformation), &Length))
                        )
                        break;
                }

                break;

            default:

                Status = STATUS_INVALID_PARAMETER;
                try_return(Status);
            }

            break;

        default:

            Status = STATUS_INVALID_PARAMETER;
        }

        // Set the information field to the number of bytes actually filled in
        // and then complete the request

        Irp->IoStatus.Information = IrpSp->Parameters.QueryFile.Length - Length;

try_exit:   NOTHING;

    } _SEH2_FINALLY {

        if (ReleaseFcb) {
            UDFReleaseFcb(IrpContext, Fcb);
        }
        if (ReleaseVolume)
            UDFReleaseVcb(IrpContext, Fcb->Vcb);

    } _SEH2_END;// end of "__finally" processing

    // Complete the request if we didn't raise.

    UDFCompleteRequest(IrpContext, Irp, Status);

    return Status;
} // end UDFCommonQueryInfo()

/*************************************************************************
*
* Function: UDFCommonSetInfo()
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
UDFCommonSetInfo(
    PIRP_CONTEXT IrpContext,
    PIRP             Irp
    )
{
    NTSTATUS                Status = STATUS_SUCCESS;
    PIO_STACK_LOCATION      IrpSp = NULL;
    PFILE_OBJECT            FileObject = NULL;
    TYPE_OF_OPEN TypeOfOpen;
    PFCB                    Fcb = NULL;
    PCCB                    Ccb = NULL;
    PVCB                    Vcb = NULL;
    _SEH2_VOLATILE BOOLEAN   MainResourceAcquired = FALSE;
    PVOID                   Buffer = NULL;
    FILE_INFORMATION_CLASS  FunctionalityRequested;
    BOOLEAN                 CanWait = FALSE;
    BOOLEAN                 PostRequest = FALSE;
    _SEH2_VOLATILE BOOLEAN   VcbAcquired = FALSE;

    TmPrint(("UDFCommonSetInfo: irp %x\n", Irp));

    IrpSp = IoGetCurrentIrpStackLocation(Irp);

    FileObject = IrpSp->FileObject;

    // Decode the file object

    TypeOfOpen = UDFDecodeFileObject(FileObject, &Fcb, &Ccb);

    if ((TypeOfOpen != UserFileOpen && TypeOfOpen != UserDirectoryOpen) ||
        Fcb == NULL || Ccb == NULL) {
        UDFCompleteRequest(IrpContext, Irp, STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }

    Vcb = Fcb->Vcb;

    ASSERT_CCB(Ccb);
    ASSERT_FCB(Fcb);
    ASSERT_VCB(Vcb);

    _SEH2_TRY {

        // Case on the type of open we're dealing with

        switch (TypeOfOpen) {

        case UserVolumeOpen:

            // We cannot query the user volume open.

            try_return(Status = STATUS_INVALID_PARAMETER);
            break;
        case UserFileOpen:

            break;

        case UserDirectoryOpen:

            break;

        default:

            try_return(Status = STATUS_INVALID_PARAMETER);
        }


        CanWait = (IrpContext->Flags & IRP_CONTEXT_FLAG_WAIT) ? TRUE : FALSE;

        // The NT I/O Manager always allocates and supplies a system
        // buffer for query and set file information calls.
        // Copying information to/from the user buffer and the system
        // buffer is performed by the I/O Manager and the FSD need not worry about it.
        Buffer = Irp->AssociatedIrp.SystemBuffer;

        // Now, obtain some parameters.
        FunctionalityRequested = IrpSp->Parameters.SetFile.FileInformationClass;
        if (FunctionalityRequested == FileAllocationInformation ||
            FunctionalityRequested == FileEndOfFileInformation) {
            ULONG MinimumLength = FunctionalityRequested == FileAllocationInformation ?
                sizeof(FILE_ALLOCATION_INFORMATION) : sizeof(FILE_END_OF_FILE_INFORMATION);
            if (IrpSp->Parameters.SetFile.Length < MinimumLength)
                try_return(Status = STATUS_INFO_LENGTH_MISMATCH);
            if (!Buffer) try_return(Status = STATUS_INVALID_PARAMETER);
        }
        if ((Vcb->VcbState & VCB_STATE_VOLUME_READ_ONLY) &&
            (FunctionalityRequested != FilePositionInformation)) {
            try_return(Status = STATUS_ACCESS_DENIED);
        }

        if (FunctionalityRequested == FileRenameInformation ||
            FunctionalityRequested == FileLinkInformation ||
            FunctionalityRequested == FileDispositionInformation) {

            /* Delete-owner publication/cancel shares the volume-close
             * serialization, before taking the target FCB resource. */
            UDFAcquireVcbExclusive(IrpContext, Vcb, FALSE);

            VcbAcquired = TRUE;
        } else if (FunctionalityRequested == FileAllocationInformation ||
                   FunctionalityRequested == FileEndOfFileInformation) {
            BOOLEAN UnderFileResource =
                ExIsResourceAcquiredSharedLite(&Fcb->FcbNonpaged->FcbResource) != 0;
            /* There is no safe shared-Main upgrade or Paging->Main wait.
             * Do not release a resource borrowed from a callback's caller. */
            if ((UnderFileResource &&
                 !ExIsResourceAcquiredExclusiveLite(&Fcb->FcbNonpaged->FcbResource)) ||
                ExIsResourceAcquiredSharedLite(&Fcb->FcbNonpaged->FcbPagingIoResource))
                try_return(Status = STATUS_DEVICE_BUSY);
            /* A CC/MM callback may already own Main. As with writable
             * section admission, never wait for VCB in that reverse order. */
            if (!UDFAcquireVcbShared(IrpContext, Vcb, UnderFileResource))
                try_return(Status = STATUS_DEVICE_BUSY);
            VcbAcquired = TRUE;
        }

        //
        // Acquire FcbResource exclusive for all operations except
        // rename/link (those acquire resources in their own functions).
        //
        if ((FunctionalityRequested != FileRenameInformation) &&
            (FunctionalityRequested != FileLinkInformation)) {

            UDFAcquireFcbExclusive(IrpContext, Fcb, FALSE);
            MainResourceAcquired = TRUE;
        }

        if (FunctionalityRequested != FilePositionInformation &&
            Fcb->FileInfo && Fcb->FileInfo->Dloc &&
            !NT_SUCCESS(Fcb->FileInfo->Dloc->ZeroDataError) &&
            !(FunctionalityRequested == FileEndOfFileInformation &&
              IrpSp->Parameters.SetFile.AdvanceOnly &&
              UDFIsZeroDataRetry(Fcb->FileInfo->Dloc)))
            try_return(Status = Fcb->FileInfo->Dloc->ZeroDataError);

        /* Drain an owned cached generation before namespace or allocation
         * changes. Rename/link have not acquired their ordered parent/child
         * locks yet; EOF/allocation have not acquired PagingIoResource yet.
         * Basic timestamps/attributes retain ordinary cached semantics. */
        if (FunctionalityRequested == FileRenameInformation ||
            FunctionalityRequested == FileLinkInformation ||
            FunctionalityRequested == FileAllocationInformation ||
            (FunctionalityRequested == FileEndOfFileInformation &&
             !IrpSp->Parameters.SetFile.AdvanceOnly)) {
            Status = UDFDrainNewFileData(IrpContext, Vcb, Fcb->FileInfo);
            if (!NT_SUCCESS(Status))
                try_return(Status);
        }

        // Do whatever the caller asked us to do
        switch (FunctionalityRequested) {
        case FileBasicInformation:
            Status = UDFSetBasicInformation(IrpContext, Fcb, Ccb, FileObject, (PFILE_BASIC_INFORMATION)Buffer);
            break;
        case FilePositionInformation: {
            // Check if no intermediate buffering has been specified.
            // If it was specified, do not allow non-aligned set file
            // position requests to succeed.
            PFILE_POSITION_INFORMATION       PtrFileInfoBuffer;

            PtrFileInfoBuffer = (PFILE_POSITION_INFORMATION)Buffer;

            if (FileObject->Flags & FO_NO_INTERMEDIATE_BUFFERING) {
                if (PtrFileInfoBuffer->CurrentByteOffset.LowPart & IrpSp->DeviceObject->AlignmentRequirement) {
                    // Invalid alignment.
                    try_return(Status = STATUS_INVALID_PARAMETER);
                }
            }

            FileObject->CurrentByteOffset = PtrFileInfoBuffer->CurrentByteOffset;
            break;
        }
        case FileDispositionInformation:
            Status = UDFSetDispositionInfo(IrpContext, FileObject, Fcb, Ccb, (PFILE_DISPOSITION_INFORMATION)Buffer);
            break;
        case FileRenameInformation:
            if (!CanWait) {
                PostRequest = TRUE;
                try_return(Status = STATUS_PENDING);
            }
            Status = UDFSetRenameInfo(IrpContext, Fcb, Ccb, FileObject, (PFILE_RENAME_INFORMATION)Buffer);
            if (Status == STATUS_PENDING) {
                PostRequest = TRUE;
                try_return(Status);
            }
            break;
#ifdef UDF_ALLOW_HARD_LINKS
        case FileLinkInformation:
            if (!CanWait) {
                PostRequest = TRUE;
                try_return(Status = STATUS_PENDING);
            }
            Status = UDFHardLink(IrpContext, IrpSp, Fcb, Ccb, FileObject, (PFILE_LINK_INFORMATION)Buffer);
            break;
#endif //UDF_ALLOW_HARD_LINKS
        case FileAllocationInformation:
            Status = UDFSetAllocationInfo(Fcb, Ccb, Vcb, FileObject, IrpContext, Irp, (PFILE_ALLOCATION_INFORMATION)Buffer);
            break;
        case FileEndOfFileInformation:
            Status = UDFSetEndOfFileInfo(IrpContext, IrpSp, Fcb, Ccb, Vcb, FileObject, Irp, (PFILE_END_OF_FILE_INFORMATION)Buffer);
            break;
        default:
            Status = STATUS_INVALID_PARAMETER;
            try_return(Status);
        }

try_exit:   NOTHING;

    } _SEH2_FINALLY {

        if (MainResourceAcquired) {
            UDFReleaseFcb(IrpContext, Fcb);
            MainResourceAcquired = FALSE;
        }

        if (VcbAcquired) {

            UDFReleaseVcb(IrpContext, Vcb);
            VcbAcquired = FALSE;
        }

        // Post IRP if required
        if (PostRequest) {

            // Since, the I/O Manager gave us a system buffer, we do not
            // need to "lock" anything.

            // Perform the post operation which will mark the IRP pending
            // and will return STATUS_PENDING back to us
            Status = UDFFsdPostRequest(IrpContext, Irp);

        } else {

            if (!_SEH2_AbnormalTermination()) {

                if (NT_SUCCESS(Status)) {

                    if (FunctionalityRequested == FileDispositionInformation) {
                        UDFFspClose(Vcb);
                    }
                }

                UDFCompleteRequest(IrpContext, Irp, Status);
            }
        }
    } _SEH2_END;// end of "__finally" processing

    return Status;
} // end UDFCommonSetInfo()

/*
    Return some time-stamps and file attributes to the caller.
 */
NTSTATUS
UDFGetBasicInformation(
    IN PFILE_OBJECT                FileObject,
    IN PFCB                        Fcb,
    IN PFILE_BASIC_INFORMATION     PtrBuffer,
 IN OUT LONG*                      PtrReturnedLength
    )
{
    NTSTATUS            RC = STATUS_SUCCESS;
    PUDF_FILE_INFO      FileInfo;
    PDIR_INDEX_ITEM     DirNdx;
    DIR_INDEX_ITEM AttributesIndex;
    PFCB OpenFcb;
    PCCB OpenCcb;

    AdPrint(("UDFGetBasicInformation: \n"));

    _SEH2_TRY {

        if (*PtrReturnedLength < (LONG)sizeof(FILE_BASIC_INFORMATION)) {
            try_return(RC = STATUS_BUFFER_OVERFLOW);
        }

        // Zero out the supplied buffer.
        RtlZeroMemory(PtrBuffer, sizeof(FILE_BASIC_INFORMATION));

        // Get information from the FCB and update TimesCache in DirIndex
        UDFDecodeFileObject(FileObject, &OpenFcb, &OpenCcb);
        FileInfo = OpenCcb && OpenCcb->FileInfo ? OpenCcb->FileInfo : Fcb->FileInfo;

        if (!FileInfo) {
            AdPrint(("!!!!!!!! Bu-u-u-u-u-g !!!!!!!!!!!\n"));
            AdPrint(("!!!! GetBasicInfo to unopened file !!!!\n"));
            try_return(RC = STATUS_INVALID_PARAMETER);
        }

        DirNdx = UDFDirIndex(UDFGetDirIndexByFileInfo(FileInfo), FileInfo->Index);
        if (!DirNdx) {
            RtlZeroMemory(&AttributesIndex, sizeof(AttributesIndex));
            AttributesIndex.FileInfo = FileInfo;
            DirNdx = &AttributesIndex;
        }

        PtrBuffer->CreationTime = Fcb->CreationTime;
        DirNdx->CreationTime = PtrBuffer->CreationTime.QuadPart;

        PtrBuffer->LastAccessTime = Fcb->LastAccessTime;
        DirNdx->LastAccessTime = PtrBuffer->LastAccessTime.QuadPart;

        PtrBuffer->LastWriteTime = Fcb->LastWriteTime;
        DirNdx->LastWriteTime = PtrBuffer->LastWriteTime.QuadPart;

        PtrBuffer->ChangeTime = Fcb->ChangeTime;
        DirNdx->ChangeTime = PtrBuffer->ChangeTime.QuadPart;

        // Now fill in the attributes.
        if (Fcb->FcbState & UDF_FCB_DIRECTORY) {
            PtrBuffer->FileAttributes = FILE_ATTRIBUTE_DIRECTORY;
#ifdef UDF_DBG
            if (!FileInfo->Dloc->DirIndex) AdPrint(("*****!!!!! Directory has no DirIndex !!!!!*****\n"));
#endif
        }
        // Similarly, fill in attributes indicating a hidden file, system
        // file, compressed file, temporary file, etc. if the FSD supports
        // such file attribute values.
        PtrBuffer->FileAttributes |= UDFAttributesToNT(DirNdx,NULL);
        if (FileObject->Flags & FO_TEMPORARY_FILE) {
            PtrBuffer->FileAttributes |= FILE_ATTRIBUTE_TEMPORARY;
        } else {
            PtrBuffer->FileAttributes &= ~FILE_ATTRIBUTE_TEMPORARY;
        }
        if (!PtrBuffer->FileAttributes) {
            PtrBuffer->FileAttributes = FILE_ATTRIBUTE_NORMAL;
        }

try_exit: NOTHING;

    } _SEH2_FINALLY {

        if (NT_SUCCESS(RC)) {
            // Return the amount of information filled in.
            (*PtrReturnedLength) -= sizeof(FILE_BASIC_INFORMATION);
        }
    } _SEH2_END;
    return(RC);
} // end UDFGetBasicInformation()


/*
    Return file sizes to the caller.
 */
NTSTATUS
UDFGetStandardInformation(
    IN PFCB                        Fcb,
    IN PCCB                        Ccb,
    IN PFILE_STANDARD_INFORMATION  PtrBuffer,
 IN OUT LONG*                      PtrReturnedLength
    )
{
    NTSTATUS            RC = STATUS_SUCCESS;
    PUDF_FILE_INFO      FileInfo;
//    PVCB Vcb;

    AdPrint(("UDFGetStandardInformation: \n"));

    _SEH2_TRY {

        if (*PtrReturnedLength < (LONG)sizeof(FILE_STANDARD_INFORMATION)) {
            try_return(RC = STATUS_BUFFER_OVERFLOW);
        }

        // Zero out the supplied buffer.
        RtlZeroMemory(PtrBuffer, sizeof(FILE_STANDARD_INFORMATION));

        FileInfo = Fcb->FileInfo;

        if (!FileInfo) {
            AdPrint(("!!!!!!!! Bu-u-u-u-u-g !!!!!!!!!!!\n"));
            AdPrint(("!!!! GetStandardInfo to unopened file !!!!\n"));
            try_return(RC = STATUS_INVALID_PARAMETER);
        }
//        Vcb = Fcb->Vcb;
        PtrBuffer->NumberOfLinks = UDFGetFileNameLinkCount(FileInfo);
        PtrBuffer->DeletePending = FlagOn(Fcb->FcbState, UDF_FCB_DELETED) ||
            UDFIsLinkDeletePending(Fcb,
            Ccb && Ccb->FileInfo ? Ccb->FileInfo : FileInfo) ||
            (Ccb && Ccb->Lcb && FlagOn(Ccb->Lcb->Flags, UDF_LCB_FLAG_LINK_DELETED));

        //  Case on whether this is a file or a directory, and extract
        //  the information and fill in the fcb/dcb specific parts
        //  of the output buffer
        if (UDFIsADirectory(Fcb->FileInfo)) {
            PtrBuffer->Directory = TRUE;
        } else {
            if (Fcb->Header.AllocationSize.LowPart == 0xffffffff) {
                Fcb->Header.AllocationSize.QuadPart =
                    UDFSysGetAllocSize(Fcb->Vcb, UDFGetFileSize(FileInfo));
            }
            PtrBuffer->AllocationSize = Fcb->Header.AllocationSize;
            PtrBuffer->EndOfFile = Fcb->Header.FileSize;

            PtrBuffer->Directory = FALSE;
        }

        try_exit: NOTHING;
    } _SEH2_FINALLY {
        if (NT_SUCCESS(RC)) {
            // Return the amount of information filled in.
            *PtrReturnedLength -= sizeof(FILE_STANDARD_INFORMATION);
        }
    } _SEH2_END;
    return(RC);
} // end UDFGetStandardInformation()

/*
    Return some time-stamps and file attributes to the caller.
 */
NTSTATUS
UDFGetNetworkInformation(
    IN PFCB                           Fcb,
    IN PFILE_NETWORK_OPEN_INFORMATION PtrBuffer,
 IN OUT PLONG                         PtrReturnedLength
    )
{
    NTSTATUS            RC = STATUS_SUCCESS;
    PUDF_FILE_INFO      FileInfo;

    AdPrint(("UDFGetNetworkInformation: \n"));

    _SEH2_TRY {

        if (*PtrReturnedLength < (LONG)sizeof(FILE_NETWORK_OPEN_INFORMATION)) {
            try_return(RC = STATUS_BUFFER_OVERFLOW);
        }

        // Zero out the supplied buffer.
        RtlZeroMemory(PtrBuffer, sizeof(FILE_NETWORK_OPEN_INFORMATION));

        // Get information from the FCB.
        PtrBuffer->CreationTime = Fcb->CreationTime;
        PtrBuffer->LastAccessTime = Fcb->LastAccessTime;
        PtrBuffer->LastWriteTime = Fcb->LastWriteTime;
        PtrBuffer->ChangeTime = Fcb->ChangeTime;

        FileInfo = Fcb->FileInfo;

        if (!FileInfo) {
            AdPrint(("!!!!!!!! Bu-u-u-u-u-g !!!!!!!!!!!\n"));
            AdPrint(("!!!! UDFGetNetworkInformation to unopened file !!!!\n"));
            try_return(RC = STATUS_INVALID_PARAMETER);
        }
        // Now fill in the attributes.
        if (Fcb->FcbState & UDF_FCB_DIRECTORY) {
            PtrBuffer->FileAttributes = FILE_ATTRIBUTE_DIRECTORY;
#ifdef UDF_DBG
            if (!FileInfo->Dloc->DirIndex) AdPrint(("*****!!!!! Directory has no DirIndex !!!!!*****\n"));
#endif
        } else {
            if (Fcb->Header.AllocationSize.LowPart == 0xffffffff) {
                Fcb->Header.AllocationSize.QuadPart =
                    UDFSysGetAllocSize(Fcb->Vcb, UDFGetFileSize(FileInfo));
            }
            PtrBuffer->AllocationSize = Fcb->Header.AllocationSize;
            PtrBuffer->EndOfFile = Fcb->Header.FileSize;
        }
        // Similarly, fill in attributes indicating a hidden file, system
        // file, compressed file, temporary file, etc. if the FSD supports
        // such file attribute values.
        PtrBuffer->FileAttributes |= Fcb->FileAttributes;
        if (!PtrBuffer->FileAttributes) {
            PtrBuffer->FileAttributes = FILE_ATTRIBUTE_NORMAL;
        }

try_exit: NOTHING;

    } _SEH2_FINALLY {
        if (NT_SUCCESS(RC)) {
            // Return the amount of information filled in.
            (*PtrReturnedLength) -= sizeof(FILE_NETWORK_OPEN_INFORMATION);
        }
    } _SEH2_END;
    return(RC);
} // end UDFGetNetworkInformation()


/*
    Return some time-stamps and file attributes to the caller.
 */
NTSTATUS
UDFGetInternalInformation(
    _In_ PIRP_CONTEXT IrpContext,
    _In_ PFCB Fcb,
    _Out_ PFILE_INTERNAL_INFORMATION Buffer,
    _Inout_ PLONG Length
    )
{
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(IrpContext);

    AdPrint(("UDFGetInternalInformation\n"));

    if (*Length < (LONG)sizeof(FILE_INTERNAL_INFORMATION)) {

        return STATUS_BUFFER_OVERFLOW;
    }

    // Index number is the file Id number in the Fcb.

    Buffer->IndexNumber = Fcb->FileId;

    *Length -= sizeof(FILE_INTERNAL_INFORMATION);

    return Status;
} // end UDFGetInternalInformation()

/*
    Return zero-filled EAs to the caller.
 */
NTSTATUS
UDFGetEaInformation(
    PIRP_CONTEXT IrpContext,
    IN PFCB                 Fcb,
    IN PFILE_EA_INFORMATION PtrBuffer,
 IN OUT PLONG               PtrReturnedLength
    )
{
    NTSTATUS            RC = STATUS_SUCCESS;

    AdPrint(("UDFGetEaInformation\n"));

    _SEH2_TRY {

        if (*PtrReturnedLength < (LONG)sizeof(FILE_EA_INFORMATION)) {
            try_return(RC = STATUS_BUFFER_OVERFLOW);
        }

        // Zero out the supplied buffer.
        PtrBuffer->EaSize = 0;

try_exit: NOTHING;

    } _SEH2_FINALLY {
        if (NT_SUCCESS(RC)) {
            // Return the amount of information filled in.
            *PtrReturnedLength -= sizeof(FILE_EA_INFORMATION);
        }
    } _SEH2_END;
    return(RC);
} // end UDFGetEaInformation()

/*
    Return file's long name to the caller.
 */
NTSTATUS
UDFGetFullNameInformation(
    IN PFILE_OBJECT                FileObject,
    IN PFILE_NAME_INFORMATION      PtrBuffer,
 IN OUT PLONG                      PtrReturnedLength
    )
{
    ULONG BytesToCopy;

    AdPrint(("UDFGetFullNameInformation\n"));

    /* If buffer can't hold at least the file name length, bail out */
    if (*PtrReturnedLength < (LONG)FIELD_OFFSET(FILE_NAME_INFORMATION, FileName[0]))
        return STATUS_BUFFER_OVERFLOW;

    /* Save file name length, and as much file len, as buffer length allows */
    PtrBuffer->FileNameLength = FileObject->FileName.Length;

    /* Calculate amount of bytes to copy not to overflow the buffer */
    BytesToCopy = min(FileObject->FileName.Length,
                      *PtrReturnedLength - FIELD_OFFSET(FILE_NAME_INFORMATION, FileName[0]));

    /* Fill in the bytes */
    RtlCopyMemory(PtrBuffer->FileName, FileObject->FileName.Buffer, BytesToCopy);

    /* Check if we could write more but are not able to */
    if (*PtrReturnedLength < (LONG)FileObject->FileName.Length + (LONG)FIELD_OFFSET(FILE_NAME_INFORMATION, FileName[0]))
    {
        /* Return number of bytes written */
        *PtrReturnedLength -= FIELD_OFFSET(FILE_NAME_INFORMATION, FileName[0]) + BytesToCopy;
        return STATUS_BUFFER_OVERFLOW;
    }

    /* We filled up as many bytes, as needed */
    *PtrReturnedLength -= (FIELD_OFFSET(FILE_NAME_INFORMATION, FileName[0]) + FileObject->FileName.Length);

    return STATUS_SUCCESS;
} // end UDFGetFullNameInformation()

/*
    Return file short(8.3) name to the caller.
 */
NTSTATUS
UDFGetAltNameInformation(
    IN PFCB                        Fcb,
    IN PCCB                        Ccb,
    IN PFILE_NAME_INFORMATION      PtrBuffer,
    IN OUT PLONG                   PtrReturnedLength
    )
{
    PDIR_INDEX_ITEM DirNdx;
    ULONG BytesToCopy;
    UNICODE_STRING ShortName;
    WCHAR ShortNameBuffer[13];
    PUDF_FILE_INFO FileInfo = Ccb && Ccb->FileInfo ? Ccb->FileInfo : Fcb->FileInfo;

    AdPrint(("UDFGetAltNameInformation: \n"));

    *PtrReturnedLength -= FIELD_OFFSET(FILE_NAME_INFORMATION, FileName[0]);
    if (!FileInfo)
        return STATUS_FILE_INVALID;
    DirNdx = UDFDirIndex(UDFGetDirIndexByFileInfo(FileInfo), FileInfo->Index);
    if (!DirNdx)
        return STATUS_OBJECT_NAME_NOT_FOUND;

    ShortName.MaximumLength = 13 * sizeof(WCHAR);
    ShortName.Buffer = (PWCHAR)&ShortNameBuffer;

    UDFDOSName__(Fcb->Vcb, &ShortName, &(DirNdx->FName), FileInfo);

    if (*PtrReturnedLength < ShortName.Length) {
        return(STATUS_BUFFER_OVERFLOW);
    } else {
        BytesToCopy = ShortName.Length;
        *PtrReturnedLength -= ShortName.Length;
    }

    RtlCopyMemory( &(PtrBuffer->FileName),
                   ShortName.Buffer,
                   BytesToCopy );

    PtrBuffer->FileNameLength = ShortName.Length;

    return(STATUS_SUCCESS);
} // end UDFGetAltNameInformation()

/*
    Get file position information
 */
NTSTATUS
UDFGetPositionInformation(
    IN PFILE_OBJECT               FileObject,
    IN PFILE_POSITION_INFORMATION PtrBuffer,
 IN OUT PLONG                     PtrReturnedLength
    )
{
    if (*PtrReturnedLength < (LONG)sizeof(FILE_POSITION_INFORMATION)) {
        return(STATUS_BUFFER_OVERFLOW);
    }
    PtrBuffer->CurrentByteOffset = FileObject->CurrentByteOffset;
    // Modify the local variable for BufferLength appropriately.
    *PtrReturnedLength -= sizeof(FILE_POSITION_INFORMATION);

    return(STATUS_SUCCESS);
} // end UDFGetAltNameInformation()

/*
    Get file file stream(s) information
 */
NTSTATUS
UDFGetFileStreamInformation(
    IN PIRP_CONTEXT IrpContext,
    IN PFCB Fcb,
    IN PFILE_STREAM_INFORMATION PtrBuffer,
    IN OUT PULONG PtrReturnedLength
    )
{
    NTSTATUS        RC = STATUS_SUCCESS;
    PUDF_FILE_INFO  FileInfo;
    PUDF_FILE_INFO  SDirInfo = NULL;
    PVCB            Vcb = Fcb->Vcb;

    uint_di         i;
    ULONG CurrentSize;
    ULONG AlignedSize;
    ULONG BufferLength;
    ULONG TotalBytesWritten = 0;
    ULONG PreviousOffset = 0;   // padding of the previous entry (aligned - real)
    PDIR_INDEX_HDR  hSDirIndex;
    PDIR_INDEX_ITEM SDirIndex;
    PDIR_INDEX_ITEM DirNdx;
    PFILE_BOTH_DIR_INFORMATION NTFileInfo = NULL;

    PFILE_STREAM_INFORMATION CurrentInfo = PtrBuffer;
    PFILE_STREAM_INFORMATION Previous = NULL;

    AdPrint(("UDFGetFileStreamInformation\n"));

    DECLARE_CONST_UNICODE_STRING(StreamPrefix, L":");
    DECLARE_CONST_UNICODE_STRING(StreamSuffix, L":$DATA");

    _SEH2_TRY {

        FileInfo = Fcb->FileInfo;
        if (!FileInfo) {
            AdPrint(("!!!!!!!! Bu-u-u-u-u-g !!!!!!!!!!!\n"));
            AdPrint(("!!!! UDFGetFileStreamInformation to unopened file !!!!\n"));
            try_return(RC = STATUS_INVALID_PARAMETER);
        }
        Vcb = Fcb->Vcb;

        DirNdx = UDFDirIndex(UDFGetDirIndexByFileInfo(FileInfo), FileInfo->Index);
        ASSERT(DirNdx);

        NTFileInfo = (PFILE_BOTH_DIR_INFORMATION)MyAllocatePool__(NonPagedPool, sizeof(FILE_BOTH_DIR_INFORMATION)+UDF_NAME_LEN*sizeof(WCHAR));
        if (!NTFileInfo) try_return(RC = STATUS_INSUFFICIENT_RESOURCES);

        RC = UDFFileDirInfoToNT(IrpContext, Vcb, DirNdx, NTFileInfo);

        if (!NT_SUCCESS(RC)) {
            try_return(RC);
        }

        // Clear output buffer so alignment padding between entries and the final
        // NextEntryOffset are guaranteed zero.
        BufferLength = *PtrReturnedLength;
        RtlZeroMemory(PtrBuffer, BufferLength);

        CurrentSize = FIELD_OFFSET(FILE_STREAM_INFORMATION, StreamName) + StreamPrefix.Length + StreamSuffix.Length;

        if (CurrentSize > BufferLength) {
            try_return(RC = STATUS_BUFFER_OVERFLOW);
        }

        CurrentInfo->StreamNameLength = StreamPrefix.Length + StreamSuffix.Length;
        CurrentInfo->StreamSize = NTFileInfo->EndOfFile;
        CurrentInfo->StreamAllocationSize = NTFileInfo->AllocationSize;

        RtlCopyMemory(&CurrentInfo->StreamName[0], StreamPrefix.Buffer, StreamPrefix.Length);
        RtlCopyMemory(&CurrentInfo->StreamName[1], StreamSuffix.Buffer, StreamSuffix.Length);

        // Each entry must start on an 8-byte boundary: advance by the aligned
        // size, but count only the real size as written.
        TotalBytesWritten = CurrentSize;
        AlignedSize = UDFQuadAlign(CurrentSize);
        PreviousOffset = AlignedSize - CurrentSize;

        Previous = CurrentInfo;
        CurrentInfo = (PFILE_STREAM_INFORMATION)((ULONG_PTR)CurrentInfo + AlignedSize);

        if (
            UDFIsAStreamDir(FileInfo) ||
            UDFIsAStream(FileInfo) || 
            !UDFHasAStreamDir(FileInfo) ||
            !NT_SUCCESS(UDFOpenStreamDir__(IrpContext, Vcb, FileInfo, &SDirInfo))
        ) {
            if (SDirInfo) {
                if (UDFCleanUpFile__(Vcb, SDirInfo) & UDF_FREE_FILEINFO)
                    MyFreePool__(SDirInfo);
                /* Failure leaves the exact FI in the Dloc retry ring. */
                SDirInfo = NULL;
            }

            *PtrReturnedLength = BufferLength - TotalBytesWritten;
            
            try_return(RC = STATUS_SUCCESS);
        }

        hSDirIndex = SDirInfo->Dloc->DirIndex;

        for(i=2; (SDirIndex = UDFDirIndex(hSDirIndex,i)); i++) {
            if ((SDirIndex->FI_Flags & UDF_FI_FLAG_FI_INTERNAL) ||
                UDFIsDeleted(SDirIndex) ||
                !SDirIndex->FName.Buffer )
                continue;

            CurrentSize = FIELD_OFFSET(FILE_STREAM_INFORMATION, StreamName) +
                            StreamPrefix.Length + SDirIndex->FName.Length + StreamSuffix.Length;

            // Need room for the previous entry's alignment padding plus this entry.
            if (PreviousOffset + CurrentSize > BufferLength - TotalBytesWritten) {
                RC = STATUS_BUFFER_OVERFLOW;
                break;
            }

            RC = UDFFileDirInfoToNT(IrpContext, Vcb, SDirIndex, NTFileInfo);

            if (!NT_SUCCESS(RC)) {
                try_return(RC);
            }

            CurrentInfo->StreamNameLength = StreamPrefix.Length + SDirIndex->FName.Length + StreamSuffix.Length;
            CurrentInfo->StreamSize = NTFileInfo->EndOfFile;
            CurrentInfo->StreamAllocationSize = NTFileInfo->AllocationSize;

            RtlCopyMemory(&CurrentInfo->StreamName[0], StreamPrefix.Buffer, StreamPrefix.Length);
            RtlCopyMemory(&CurrentInfo->StreamName[1], SDirIndex->FName.Buffer, SDirIndex->FName.Length);
            RtlCopyMemory(&CurrentInfo->StreamName[1 + SDirIndex->FName.Length / sizeof(WCHAR)], StreamSuffix.Buffer, StreamSuffix.Length);

            // Link the previous entry to this one using the aligned distance.
            Previous->NextEntryOffset = (ULONG)((ULONG_PTR)CurrentInfo - (ULONG_PTR)Previous);

            TotalBytesWritten = (ULONG)((ULONG_PTR)CurrentInfo - (ULONG_PTR)PtrBuffer) + CurrentSize;
            AlignedSize = UDFQuadAlign(CurrentSize);
            PreviousOffset = AlignedSize - CurrentSize;

            Previous = CurrentInfo;
            CurrentInfo = (PFILE_STREAM_INFORMATION)((ULONG_PTR)CurrentInfo + AlignedSize);
        }

        // Returned length is the real bytes used (no trailing padding).
        *PtrReturnedLength = BufferLength - TotalBytesWritten;

try_exit: NOTHING;

    } _SEH2_FINALLY {
        if (SDirInfo) {
            UDFCloseFile__(IrpContext, Vcb, SDirInfo);
            if (UDFCleanUpFile__(Vcb, SDirInfo) & UDF_FREE_FILEINFO)
                MyFreePool__(SDirInfo);
        }
        if (NTFileInfo)
           MyFreePool__(NTFileInfo);
    } _SEH2_END;
    return(RC);
} // end UDFGetFileStreamInformation()

//*******************************************************************
/*
    Set some time-stamps and file attributes supplied by the caller.
 */
NTSTATUS
UDFSetBasicInformation(
    IN PIRP_CONTEXT                IrpContext,
    IN PFCB                        Fcb,
    IN PCCB                        Ccb,
    IN PFILE_OBJECT                FileObject,
    IN PFILE_BASIC_INFORMATION PtrBuffer)
{
    PUDF_FILE_INFO FileInfo = Ccb && Ccb->FileInfo ? Ccb->FileInfo : Fcb->FileInfo;
    NTSTATUS        RC = STATUS_SUCCESS;
    ULONG           NotifyFilter = 0;
    PDIR_INDEX_ITEM DirNdx;

    if (!Ccb || !FileInfo || !FileInfo->Dloc)
        return STATUS_FILE_INVALID;
    /* Attributes which live in a FID require a resolved namespace link.
     * A cold by-ID FI can have no parent/index; do not report such a FID
     * update as persisted when only the common FE is available. */
    DirNdx = UDFDirIndex(UDFGetDirIndexByFileInfo(FileInfo), FileInfo->Index);
    if (PtrBuffer->FileAttributes &&
        (!DirNdx || (!FileInfo->ParentFile && Fcb != Fcb->Vcb->RootIndexFcb)))
        return STATUS_NOT_SUPPORTED;

    AdPrint(("UDFSetBasicInformation\n"));

    _SEH2_TRY {

        // If the user is specifying -1 for a field, that means
        // we should leave that field unchanged, even if we might
        // have otherwise set it ourselves.  We'll set the Ccb flag
        // saying that the user set the field so that we
        // don't do our default updating.

        // We set the field to 0 then so we know not to actually
        // set the field to the user-specified (and in this case,
        // illegal) value.

        if (PtrBuffer->LastWriteTime.QuadPart == -1) {

            SetFlag(Ccb->Flags, UDF_CCB_WRITE_TIME_SET);
            PtrBuffer->LastWriteTime.QuadPart = 0;
        }

        if (PtrBuffer->LastAccessTime.QuadPart == -1) {

            SetFlag(Ccb->Flags, UDF_CCB_ACCESS_TIME_SET);
            PtrBuffer->LastAccessTime.QuadPart = 0;
        }

        if (PtrBuffer->CreationTime.QuadPart == -1) {

            SetFlag(Ccb->Flags, UDF_CCB_CREATE_TIME_SET);
            PtrBuffer->CreationTime.QuadPart = 0;
        }

        // Obtain a pointer to the directory entry associated with
        // the FCB being modifed. The directory entry is obviously
        // part of the data associated with the parent directory that
        // contains this particular file stream.
        if (PtrBuffer->FileAttributes) {
            UDFUpdateAttrTime(Fcb->Vcb, FileInfo);
        } else
        if ( UDFIsADirectory(FileInfo) &&
            !(Fcb->Vcb->CompatFlags & UDF_VCB_IC_UPDATE_UCHG_DIR_ACCESS_TIME) &&
              ((FileInfo->Dloc->DataLoc.Modified ||
                FileInfo->Dloc->AllocLoc.Modified ||
                (FileInfo->Dloc->FE_Flags & UDF_FE_FLAG_FE_MODIFIED) ||
                FileInfo->Dloc->FELoc.Modified))
             ) {
            // ignore Access Time Modification for unchanged Dir
            if (!PtrBuffer->CreationTime.QuadPart &&
               PtrBuffer->LastAccessTime.QuadPart &&
               !PtrBuffer->ChangeTime.QuadPart &&
               !PtrBuffer->LastWriteTime.QuadPart)
                try_return(RC);
        }

        UDFSetFileXTime(FileInfo,
            &(PtrBuffer->CreationTime.QuadPart),
            &(PtrBuffer->LastAccessTime.QuadPart),
            &(PtrBuffer->ChangeTime.QuadPart),
            &(PtrBuffer->LastWriteTime.QuadPart) );

        if (PtrBuffer->CreationTime.QuadPart) {
            // The interesting thing here is that the user has set certain time
            // fields. However, before doing this, the user may have performed
            // I/O which in turn would have caused FSD to mark the fact that
            // write/access time should be modifed at cleanup.
            // We'll mark the fact that such updates are no longer
            // required since the user has explicitly specified the values he
            // wishes to see associated with the file stream.
            Fcb->CreationTime = PtrBuffer->CreationTime;
            Ccb->Flags |= UDF_CCB_CREATE_TIME_SET;
            NotifyFilter |= FILE_NOTIFY_CHANGE_CREATION;
        }
        if (PtrBuffer->LastAccessTime.QuadPart) {
            Fcb->LastAccessTime = PtrBuffer->LastAccessTime;
            Ccb->Flags |= UDF_CCB_ACCESS_TIME_SET;
            NotifyFilter |= FILE_NOTIFY_CHANGE_LAST_ACCESS;
        }
        if (PtrBuffer->ChangeTime.QuadPart) {
            Fcb->ChangeTime = PtrBuffer->ChangeTime;
            Ccb->Flags |= UDF_CCB_MODIFY_TIME_SET;
        }
        if (PtrBuffer->LastWriteTime.QuadPart) {
            Fcb->LastWriteTime = PtrBuffer->LastWriteTime;
            Ccb->Flags |= UDF_CCB_WRITE_TIME_SET;
            NotifyFilter |= FILE_NOTIFY_CHANGE_LAST_WRITE;
        }

        // Now come the attributes.
        if (PtrBuffer->FileAttributes) {
            // We have a non-zero attribute value.
            // The presence of a particular attribute indicates that the
            // user wishes to set the attribute value. The absence indicates
            // the user wishes to clear the particular attribute.

            // Our routine ignores unsupported flags
            PtrBuffer->FileAttributes &= ~(FILE_ATTRIBUTE_NORMAL);

            // Similarly, we should pick out other invalid flag values.
            if ( (PtrBuffer->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
               !(Fcb->FcbState & UDF_FCB_DIRECTORY))
                try_return(RC = STATUS_INVALID_PARAMETER);

            if (PtrBuffer->FileAttributes & FILE_ATTRIBUTE_TEMPORARY) {
                if (Fcb->FcbState & UDF_FCB_DIRECTORY)
                    try_return(RC = STATUS_INVALID_PARAMETER);
                FileObject->Flags |= FO_TEMPORARY_FILE;
            } else {
                FileObject->Flags &= ~FO_TEMPORARY_FILE;
            }

            if (PtrBuffer->FileAttributes & FILE_ATTRIBUTE_READONLY) {
                Fcb->FcbState |= UDF_FCB_READ_ONLY;
            } else {
                Fcb->FcbState &= ~UDF_FCB_READ_ONLY;
            }

            DirNdx = UDFDirIndex(UDFGetDirIndexByFileInfo(FileInfo),
                                 FileInfo->Index);
            UDFAttributesToUDF(DirNdx, NULL, PtrBuffer->FileAttributes);
            /* A later open checks this cached value for READONLY.  Keep it
             * in step with the FE and directory index updated above. */
            Fcb->FileAttributes = DirNdx->SysAttr;
            DirNdx->FI_Flags |= UDF_FI_FLAG_SYS_ATTR;
            // If the FSD supports file compression, we may wish to
            // note the user's preferences for compressing/not compressing
            // the file at this time.
            Ccb->Flags |= UDF_CCB_ATTRIBUTES_SET;
            NotifyFilter |= FILE_NOTIFY_CHANGE_ATTRIBUTES;
        }

        if (NotifyFilter) {

            UDFNotifyReportChange(IrpContext, Fcb->Vcb,
                                      Fcb,
                                      NotifyFilter,
                                      FILE_ACTION_MODIFIED,
                                      Ccb->Lcb, FileObject);

            UDFSetFileSizeInDirNdx(Fcb->Vcb, FileInfo, NULL);
            FileInfo->Dloc->FE_Flags |= UDF_FE_FLAG_FE_MODIFIED;
        }

try_exit: NOTHING;
    } _SEH2_FINALLY {
        ;
    } _SEH2_END;
    return(RC);
} // end UDFSetBasicInformation()

/* Synchronous subtree deletion owns the actual operation. Preflight does
 * not install separate delayed parent/canonical-FI deletion intents. */
NTSTATUS
UDFPreflightStreamDirectoryDelete(PIRP_CONTEXT IrpContext, PVCB Vcb,
                                  PUDF_FILE_INFO SDirInfo)
{
    PUDF_DATALOC_INFO Owner;
    PUDF_FILE_INFO FileInfo = NULL;
    PFCB SDirFcb, ChildFcb = NULL;
    BOOLEAN SDirAcquired = FALSE, ChildAcquired = FALSE;
    BOOLEAN Opened = FALSE, SlotOwned = FALSE;
    NTSTATUS Status = STATUS_SUCCESS, ReleaseStatus;
    uint_di Index, Limit;
    if (!SDirInfo || !(Owner = SDirInfo->Dloc) || !Owner->DirIndex ||
        !SDirInfo->RefCount || !UDFIsAStreamDir(SDirInfo))
        return STATUS_FILE_INVALID;
    if (Owner->StreamDeleteOwner)
        return STATUS_DEVICE_BUSY;
    SDirFcb = Owner->CommonFcb;
    if (SDirFcb && !SDirFcb->Header.Resource)
        return STATUS_FILE_INVALID;
    if (SDirFcb && !ExIsResourceAcquiredExclusiveLite(SDirFcb->Header.Resource)) {
        if (!UDFAcquireFcbExclusive(IrpContext, SDirFcb, TRUE))
            return STATUS_DEVICE_BUSY;
        SDirAcquired = TRUE;
    }
    _SEH2_TRY {
        Limit = UDFDirIndexGetLastIndex(Owner->DirIndex);
        for (Index = 2; Index < Limit; Index++) {
            FileInfo = NULL;
            Opened = SlotOwned = ChildAcquired = FALSE;
            ChildFcb = NULL;
            Status = UDFOpenFile__(IrpContext, Vcb, FALSE, TRUE, NULL,
                                    SDirInfo, &FileInfo, &Index);
            if (Status == STATUS_FILE_DELETED) {
                ReleaseStatus = UDFRetainFailedStreamOpen(Vcb, &FileInfo);
                if (!NT_SUCCESS(ReleaseStatus)) try_return(Status = ReleaseStatus);
                Status = STATUS_SUCCESS;
                continue;
            }
            if (!NT_SUCCESS(Status)) try_return(Status);
            Opened = TRUE;
            Status = UDFOwnStreamDeleteFile(Vcb, Owner, FileInfo);
            if (!NT_SUCCESS(Status)) try_return(Status);
            SlotOwned = TRUE;
            ChildFcb = FileInfo->Dloc->CommonFcb;
            if (ChildFcb) {
                if (!ChildFcb->Header.Resource)
                    try_return(Status = STATUS_FILE_INVALID);
                if (!UDFAcquireFcbExclusive(IrpContext, ChildFcb, TRUE))
                    try_return(Status = STATUS_DEVICE_BUSY);
                ChildAcquired = TRUE;
                if (ChildFcb->FcbCleanup)
                    try_return(Status = STATUS_CANNOT_DELETE);
            }
#ifndef UDF_ALLOW_LINKS_TO_STREAMS
            if (UDFGetFileLinkCount(FileInfo) >= 2)
                try_return(Status = STATUS_CANNOT_DELETE);
#endif
            if (ChildFcb && !MmFlushImageSection(
                &ChildFcb->FcbNonpaged->SegmentObject, MmFlushForDelete))
                try_return(Status = STATUS_CANNOT_DELETE);
            /* Consume only this successful temporary open, without a
             * second flush through cleanup/close on a preflight failure. */
            ReleaseStatus = UDFReleaseStreamDeleteFile(Vcb, Owner, &FileInfo, FALSE);
            Opened = SlotOwned = FALSE;
            if (!NT_SUCCESS(ReleaseStatus)) try_return(Status = ReleaseStatus);
            if (ChildAcquired) {
                UDFReleaseFcb(IrpContext, ChildFcb);
                ChildAcquired = FALSE;
            }
            FileInfo = NULL;
        }
try_exit: NOTHING;
    } _SEH2_FINALLY {
        if (Opened) {
            ReleaseStatus = SlotOwned ?
                UDFReleaseStreamDeleteFile(Vcb, Owner, &FileInfo, FALSE) :
                UDFAbortStreamDeleteFile(Vcb, Owner, &FileInfo);
            if (!_SEH2_AbnormalTermination() && NT_SUCCESS(Status))
                Status = ReleaseStatus;
        }
        ReleaseStatus = UDFRetainFailedStreamOpen(Vcb, &FileInfo);
        if (!_SEH2_AbnormalTermination() && NT_SUCCESS(Status))
            Status = ReleaseStatus;
        if (ChildAcquired)
            UDFReleaseFcb(IrpContext, ChildFcb);
        if (SDirAcquired)
            UDFReleaseFcb(IrpContext, SDirFcb);
    } _SEH2_END;
    return Status;
} // UDFPreflightStreamDirectoryDelete


/*
    (Un)Mark file for deletion.
 */
NTSTATUS
UDFSetDispositionInfo(
    IN PIRP_CONTEXT IrpContext,
    IN PFILE_OBJECT FileObject,
    IN PFCB Fcb,
    IN PCCB Ccb,
    IN PFILE_DISPOSITION_INFORMATION Buffer
    )
{
    PUDF_FILE_INFO FileInfo = Ccb && Ccb->FileInfo ? Ccb->FileInfo : Fcb->FileInfo;
    NTSTATUS RC = STATUS_SUCCESS;
    PVCB Vcb = IrpContext->Vcb;
//    PUDF_FILE_INFO  SDirInfo = NULL;
//    PUDF_FILE_INFO  FileInfo = NULL;
    ULONG lc;

    if (Fcb == Vcb->VolumeDasdFcb)
        return STATUS_ACCESS_DENIED;

    if (!FileInfo || !FileInfo->Dloc)
        return STATUS_FILE_INVALID;
    /* Deleting by ID without a resolved link must not unlink whichever
     * namespace happens to be the FCB's canonical representative. */
    if (Buffer->DeleteFile &&
        (!FileInfo->ParentFile ||
         (Ccb && FlagOn(Ccb->Flags, CCB_FLAG_OPEN_BY_ID | CCB_FLAG_OPEN_RELATIVE_BY_ID))))
        return STATUS_CANNOT_DELETE;
    if (!Buffer->DeleteFile) {
        if (FileInfo->LinkDeleteCommitted || FileInfo->Dloc->DeleteCommitted ||
            (Ccb && Ccb->Lcb && FlagOn(Ccb->Lcb->Flags, UDF_LCB_FLAG_LINK_DELETED)))
            return STATUS_CANNOT_DELETE;
        /* Cancellation is scoped to this namespace, not every link in the
         * shared FCB. A touched/committed generation keeps its retry owner. */
        RC = UDFCancelLinkDelete(IrpContext, Fcb, FileInfo);
        if (NT_SUCCESS(RC) && FileObject)
            FileObject->DeletePending = UDFIsLinkDeletePending(Fcb, FileInfo);
        return RC;
    }

    _SEH2_TRY {

        AdPrint(("    SET DeleteOnClose\n"));

        // The easy part is over. Now, we know that the user wishes to
        // delete the corresponding directory entry (of course, if this
        // is the only link to the file stream, any on-disk storage space
        // associated with the file stream will also be released when the
        // (only) link is deleted!)

        // Do some checking to see if the file can even be deleted.
        if (UDFIsLinkDeletePending(Fcb, FileInfo))
            try_return(RC = UDFQueueLinkDelete(IrpContext, Fcb, FileInfo,
                                               Ccb ? Ccb->Lcb : NULL));

        if (Vcb->VcbState & VCB_STATE_VOLUME_READ_ONLY) {
            try_return(RC = STATUS_CANNOT_DELETE);
        }

        if (Fcb->FcbState & UDF_FCB_READ_ONLY) {
            RC = UDFCheckAccessRights(NULL, NULL, FileInfo->ParentFile ? FileInfo->ParentFile->Fcb : NULL, NULL, FILE_DELETE_CHILD, 0);
            if (!NT_SUCCESS(RC)) {
                try_return (RC = STATUS_CANNOT_DELETE);
            }
        }

        // It would not be prudent to allow deletion of either a root
        // directory or a directory that is not empty.
        if (Fcb->FcbState & UDF_FCB_ROOT_DIRECTORY)
            try_return(RC = STATUS_CANNOT_DELETE);

        lc = UDFGetFileNameLinkCount(FileInfo);

        if (Fcb->FcbState & UDF_FCB_DIRECTORY) {
            // Perform check to determine whether the directory
            // is empty or not.
            if (!UDFIsDirEmpty__(FileInfo)) {
                 try_return(RC = STATUS_DIRECTORY_NOT_EMPTY);
            }

        } else {
            // An important step is to check if the file stream has been
            // mapped by any process. The delete cannot be allowed to proceed
            // in this case.
            MmPrint(("    MmFlushImageSection()\n"));

            if (!MmFlushImageSection(&Fcb->FcbNonpaged->SegmentObject,
                    (lc > 1) ? MmFlushForWrite : MmFlushForDelete)) {

                try_return(RC = STATUS_CANNOT_DELETE);
            }
        }
        /* Validate live-stream refusal without installing stream-wide
         * delete flags before this link has an owned request. Actual stream
         * preparation is repeated under the last-cleanup commit locks. */
        if (lc <= 1 && UDFHasAStreamDir(FileInfo) &&
            UDFStreamDirectoryHasLiveHandles(FileInfo->Dloc->SDirInfo))
            try_return(RC = STATUS_CANNOT_DELETE);
        RC = UDFQueueLinkDelete(IrpContext, Fcb, FileInfo, Ccb ? Ccb->Lcb : NULL);
        if (!NT_SUCCESS(RC)) try_return(RC);

        if (FileObject)
            FileObject->DeletePending = TRUE;

        if ((Fcb->FcbState & UDF_FCB_DIRECTORY) && Ccb) {
            FsRtlNotifyFullChangeDirectory( Vcb->NotifySync, &(Vcb->NextNotifyIRP),
                                            (PVOID)Ccb, NULL, FALSE, FALSE,
                                            0, NULL, NULL, NULL );
        }

try_exit: NOTHING;

    } _SEH2_FINALLY {
        ;
    } _SEH2_END;
    return(RC);
} // end UDFSetDispositionInformation()


/* A size change can arrive after handle cleanup. Keep its exact FI token
 * and FCB/VCB pins until all size/cache callbacks have left. Main and VCB
 * are held by the caller; publication/retirement below performs no I/O. */
typedef struct _UDF_SET_SIZE_REFERENCE {
    PFCB Fcb;
    PUDF_FILE_INFO FileInfo;
    BOOLEAN PinsOwned;
} UDF_SET_SIZE_REFERENCE, *PUDF_SET_SIZE_REFERENCE;

static NTSTATUS
UDFAcquireSetSizeReference(PIRP_CONTEXT IrpContext, PFCB Fcb,
    PUDF_FILE_INFO FileInfo, PUDF_SET_SIZE_REFERENCE Owner)
{
    NTSTATUS Status = STATUS_SUCCESS;
    PVCB Vcb = Fcb->Vcb;
    ASSERT_EXCLUSIVE_FCB(Fcb);
    Owner->Fcb = Fcb;
    UDFLockVcb(IrpContext, Vcb);
    if (Fcb->FcbReference == MAXULONG || Vcb->VcbReference == MAXULONG) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
    } else {
        Fcb->FcbReference++;
        Vcb->VcbReference++;
        Owner->PinsOwned = TRUE;
    }
    UDFUnlockVcb(IrpContext, Vcb);
    if (Status != STATUS_SUCCESS) return Status;
    Status = UDFReferenceFileChecked(Vcb, FileInfo);
    if (Status == STATUS_SUCCESS) Owner->FileInfo = FileInfo;
    return Status; /* caller's finally owns any pins already acquired */
}

static NTSTATUS
UDFReleaseSetSizePins(PIRP_CONTEXT IrpContext, PUDF_SET_SIZE_REFERENCE Owner)
{
    NTSTATUS Status = STATUS_SUCCESS;
    PFCB Fcb = Owner->Fcb;
    PVCB Vcb;
    if (!Owner->PinsOwned) return Status;
    Vcb = Fcb->Vcb;
    UDFLockVcb(IrpContext, Vcb);
    ASSERT(Fcb->FcbReference && Vcb->VcbReference);
    if (!Fcb->FcbReference || !Vcb->VcbReference) {
        Status = STATUS_FILE_CORRUPT_ERROR;
        Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
        InterlockedCompareExchange(&Vcb->MetadataWritebackError, Status, STATUS_SUCCESS);
    } else {
        Fcb->FcbReference--;
        Vcb->VcbReference--;
        Owner->PinsOwned = FALSE;
    }
    UDFUnlockVcb(IrpContext, Vcb);
    if (!Owner->PinsOwned && !Fcb->FcbReference)
        UDFScheduleFcbTeardownRetry(IrpContext, Fcb);
    return Status;
}

static NTSTATUS
UDFCompleteSetSizeMetadata(PIRP_CONTEXT IrpContext, PFCB Fcb,
                           PUDF_FILE_INFO FileInfo)
{
    PFCB Parent;
    BOOLEAN ParentAcquired = FALSE;
    NTSTATUS Status;
    if (FileInfo->RefCount || FileInfo->OpenCount) return STATUS_SUCCESS;
    Parent = FileInfo->ParentFile ? FileInfo->ParentFile->Fcb : NULL;
    if (FileInfo->ParentFile && !Parent) return STATUS_FILE_INVALID;
    if (Parent == Fcb) return STATUS_FILE_CORRUPT_ERROR;
    if (Parent && Parent != Fcb) {
        if (!Parent->Header.Resource || Parent->TeardownInProgress)
            return STATUS_FILE_INVALID;
        if (!ExIsResourceAcquiredExclusiveLite(Parent->Header.Resource)) {
            if (ExIsResourceAcquiredSharedLite(Parent->Header.Resource) ||
                !UDFAcquireFcbExclusive(IrpContext, Parent, TRUE))
                return STATUS_DEVICE_BUSY;
            ParentAcquired = TRUE;
        }
    }
    _SEH2_TRY {
        Status = UDFCompleteFileClose(IrpContext, Fcb->Vcb, FileInfo);
    } _SEH2_FINALLY {
        if (ParentAcquired) UDFReleaseFcb(IrpContext, Parent);
    } _SEH2_END;
    return Status;
}

/* PagingIoResource has been released, while Main and our pins still hold
 * the FI/cache generation. Consume the ordinary reference once; metadata
 * completion is separate and cannot replay an already spent close. */
static NTSTATUS
UDFFinishSetSizeReference(PIRP_CONTEXT IrpContext, PFCB Fcb,
    PUDF_FILE_INFO FileInfo, PFILE_OBJECT FileObject,
    PUDF_SET_SIZE_REFERENCE Owner, BOOLEAN CompleteMetadata,
    BOOLEAN UninitializeCache, NTSTATUS OriginalStatus)
{
    _SEH2_VOLATILE NTSTATUS Status = OriginalStatus;
    NTSTATUS Current;
    _SEH2_TRY {
        Current = UDFReleaseCreateReference(IrpContext, Fcb->Vcb, &Owner->FileInfo, TRUE);
        if (NT_SUCCESS(Status) && Current != STATUS_SUCCESS) Status = Current;
        if (CompleteMetadata && NT_SUCCESS(Status)) {
            _SEH2_TRY {
                Current = UDFCompleteSetSizeMetadata(IrpContext, Fcb, FileInfo);
            } _SEH2_EXCEPT(FsRtlIsNtstatusExpected(_SEH2_GetExceptionCode()) ?
                          EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
                Current = _SEH2_GetExceptionCode();
                if (Current == STATUS_CANT_WAIT) Current = STATUS_DEVICE_BUSY;
            } _SEH2_END;
            if (Current != STATUS_SUCCESS) {
                if (NT_SUCCESS(Current)) Current = STATUS_IO_DEVICE_ERROR;
                Status = Current;
                InterlockedCompareExchange(&Fcb->Vcb->MetadataWritebackError,
                                            Current, STATUS_SUCCESS);
            }
        }
    } _SEH2_FINALLY {
        if (UninitializeCache) {
            _SEH2_TRY {
                /* FALSE may represent unfinished cache lifetime. The real,
                 * accepted FILE_OBJECT retains its normal cleanup/close path;
                 * this size setter does not claim a durable flush from it. */
                CcUninitializeCacheMap(FileObject, NULL, NULL);
            } _SEH2_EXCEPT(FsRtlIsNtstatusExpected(_SEH2_GetExceptionCode()) ?
                          EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
                Current = _SEH2_GetExceptionCode();
                if (Current == STATUS_CANT_WAIT) Current = STATUS_DEVICE_BUSY;
                InterlockedCompareExchange(&Fcb->Vcb->MetadataWritebackError,
                                            Current, STATUS_SUCCESS);
                if (NT_SUCCESS(Status)) Status = Current;
            } _SEH2_END;
        }
    } _SEH2_END;
    return Status;
}

static NTSTATUS
UDFPublishSetSizeCache(PIRP_CONTEXT IrpContext, PFCB Fcb, PFILE_OBJECT FileObject)
{
    _SEH2_VOLATILE NTSTATUS Status = STATUS_SUCCESS;
    _SEH2_TRY {
        _SEH2_TRY {
            CcSetFileSizes(FileObject, (PCC_FILE_SIZES)&Fcb->Header.AllocationSize);
        } _SEH2_EXCEPT(FsRtlIsNtstatusExpected(_SEH2_GetExceptionCode()) ?
                      EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
            Status = _SEH2_GetExceptionCode();
            /* This operation may already own the new size. Do not return
             * the dispatcher's internal repost token after accepting it. */
            if (Status == STATUS_CANT_WAIT) Status = STATUS_DEVICE_BUSY;
        } _SEH2_END;
    } _SEH2_FINALLY {
        if (_SEH2_AbnormalTermination()) Status = STATUS_UNSUCCESSFUL;
        if (Status != STATUS_SUCCESS)
            UDFRetainCacheSizeError(IrpContext, Fcb, FileObject, Status);
    } _SEH2_END;
    return Status;
}

/*
      Change file allocation length.
 */
NTSTATUS
UDFSetAllocationInfo(
    IN PFCB                            Fcb,
    IN PCCB                            Ccb,
    IN PVCB                            Vcb,
    IN PFILE_OBJECT                    FileObject,
    IN PIRP_CONTEXT IrpContext,
    IN PIRP                            Irp,
    IN PFILE_ALLOCATION_INFORMATION    Buffer
    )
{
    PUDF_FILE_INFO FileInfo = Ccb && Ccb->FileInfo ? Ccb->FileInfo : Fcb->FileInfo;
    _SEH2_VOLATILE NTSTATUS RC = STATUS_SUCCESS;
    BOOLEAN         TruncatedFile = FALSE;
    BOOLEAN         ModifiedAllocSize = FALSE;
    _SEH2_VOLATILE BOOLEAN CacheMapInitialized = FALSE;
    _SEH2_VOLATILE BOOLEAN PagingAcquired = FALSE;
    _SEH2_VOLATILE UDF_SET_SIZE_REFERENCE Owner = {0};

    if (!FileInfo || !FileInfo->Dloc)
        return STATUS_FILE_INVALID;
    if (!Buffer || Buffer->AllocationSize.QuadPart < 0) return STATUS_INVALID_PARAMETER;

    // Allocation is only allowed on a file and not a directory

    if (NodeType(Fcb) != UDF_NODE_TYPE_DATA) {

        return STATUS_INVALID_PARAMETER;
    }

    _SEH2_TRY {
        // Increasing the allocation size associated with a file stream
        // is relatively easy. All we have to do is execute some FSD
        // specific code to check whether we have enough space available
        // (and if the FSD supports user/volume quotas, whether the user
        // is not exceeding quota), and then increase the file size in the
        // corresponding on-disk and in-memory structures.
        // Then, all we should do is inform the Cache Manager about the
        // increased allocation size.

        Fcb->Header.IsFastIoPossible = UDFIsFastIoPossible(Fcb);

        if ((FileObject->SectionObjectPointer->DataSectionObject != NULL) &&
            (FileObject->SectionObjectPointer->SharedCacheMap == NULL) &&
            !FlagOn(Irp->Flags, IRP_PAGING_IO)) {
            ASSERT( !FlagOn( FileObject->Flags, FO_CLEANUP_COMPLETE ) );
            //  Now initialize the cache map.
            MmPrint(("    CcInitializeCacheMap()\n"));
            CcInitializeCacheMap(FileObject,
                                 (PCC_FILE_SIZES)&Fcb->Header.AllocationSize,
                                 FALSE,
                                 &(UdfData.CacheMgrCallBacks),
                                 Fcb);

            CacheMapInitialized = TRUE;
        }
        UDFAcquirePagingIoExclusive(IrpContext, Fcb);
        PagingAcquired = TRUE;

        // Are we increasing the allocation size?
        if (Fcb->Header.AllocationSize.QuadPart <
            Buffer->AllocationSize.QuadPart) {

            // Yes. Do the FSD specific stuff i.e. increase reserved
            // space on disk.
            if (
                ( (Vcb->BitmapFcb ? Vcb->FreeAllocUnits : (LONGLONG)UDFGetFreeSpace(Vcb))
                    << Vcb->SectorShift
                ) 
                    < Buffer->AllocationSize.QuadPart
            ) {
                try_return(RC = STATUS_DISK_FULL);
            }
//          RC = STATUS_SUCCESS;
            ModifiedAllocSize = TRUE;

        } else if (Fcb->Header.AllocationSize.QuadPart > Buffer->AllocationSize.QuadPart) {
            // This is the painful part. See if the VMM will allow us to proceed.
            // The VMM will deny the request if:
            // (a) any image section exists OR
            // (b) a data section exists and the size of the user mapped view
            //       is greater than the new size
            // Otherwise, the VMM should allow the request to proceed.
            MmPrint(("    MmCanFileBeTruncated()\n"));
            if (!MmCanFileBeTruncated(&Fcb->FcbNonpaged->SegmentObject, &Buffer->AllocationSize)) {
                // VMM said no way!
                try_return(RC = STATUS_USER_MAPPED_FILE);
            }

            // Perform our directory entry modifications. Release any on-disk
            // space we may need to in the process.
            ModifiedAllocSize = TRUE;
            TruncatedFile = TRUE;
        }

        ASSERT(NT_SUCCESS(RC));
        // This is a good place to check if we have performed a truncate
        // operation. If we have perform a truncate (whether we extended
        // or reduced file size or even leave it intact), we should update
        // file time stamps.
        FileObject->Flags |= FO_FILE_MODIFIED;

        // Last, but not the lease, we must inform the Cache Manager of file size changes.
        if (ModifiedAllocSize) {

            // If we decreased the allocation size to less than the
            // current file size, modify the file size value.
            // Similarly, if we decreased the value to less than the
            // current valid data length, modify that value as well.

            // Update the FCB Header with the new allocation size.
            if (TruncatedFile) {
                if (Fcb->Header.FileSize.QuadPart > Buffer->AllocationSize.QuadPart) {
                    /* A shrink can be rejected before touching the on-disk
                     * file, including by the ordered-truncate layout guard.
                     * Keep the live FCB and Cache Manager sizes unchanged
                     * until the file-system resize has actually succeeded. */
                    RC = UDFAcquireSetSizeReference(IrpContext, Fcb, FileInfo,
                                                   (PUDF_SET_SIZE_REFERENCE)&Owner);
                    if (RC != STATUS_SUCCESS) try_return(RC);
                    RC = UDFResizeFile__(IrpContext, Vcb, FileInfo, Buffer->AllocationSize.QuadPart);
                    if (!NT_SUCCESS(RC))
                        try_return(RC);
                    Fcb->Header.FileSize = Buffer->AllocationSize;
//                    UDFSetFileSizeInDirNdx(Vcb, FileInfo, NULL);
                }
                if (Fcb->Header.ValidDataLength.QuadPart > Buffer->AllocationSize.QuadPart) {
                    // Decrease the valid data length value.
                    Fcb->Header.ValidDataLength = Buffer->AllocationSize;
                }
            } else {
                Fcb->Header.AllocationSize = Buffer->AllocationSize;
//                UDFSetFileSizeInDirNdx(Vcb, FileInfo,
//                                       &(PtrBuffer->AllocationSize.QuadPart));
            }

            // If the FCB has not had caching initiated, it is still valid
            // for us to invoke the NT Cache Manager. It is possible in such
            // situations for the call to be no'oped (unless some user has
            // mapped in the file)

            // NOTE: The invocation to CcSetFileSizes() will quite possibly
            //  result in a recursive call back into the file system.
            //  This is because the NT Cache Manager will typically
            //  perform a flush before telling the VMM to purge pages
            //  especially when caching has not been initiated on the
            //  file stream, but the user has mapped the file into
            //  the process' virtual address space.
            MmPrint(("    CcSetFileSizes()\n"));

            Fcb->NtReqFCBFlags |= UDF_NTREQ_FCB_MODIFIED;
            RC = UDFPublishSetSizeCache(IrpContext, Fcb, FileObject);
            if (RC != STATUS_SUCCESS) try_return(RC);

            // Inform any pending IRPs (notify change directory).
            if (UDFIsAStream(FileInfo)) {
                UDFNotifyReportChange(IrpContext, Vcb, Fcb,
                                          FILE_NOTIFY_CHANGE_STREAM_SIZE,
                                          FILE_ACTION_MODIFIED_STREAM,
                                          Ccb->Lcb, FileObject);
            } else {
                UDFNotifyReportChange(IrpContext, Vcb, Fcb,
                                          FILE_NOTIFY_CHANGE_SIZE,
                                          FILE_ACTION_MODIFIED,
                                          Ccb->Lcb, FileObject);
            }
        }

try_exit: NOTHING;

    } _SEH2_FINALLY {
        BOOLEAN Abnormal = _SEH2_AbnormalTermination();
        NTSTATUS FinishStatus, PinStatus;
        _SEH2_TRY {
            if (PagingAcquired) {
                PagingAcquired = FALSE;
                UDFReleasePagingIo(IrpContext, Fcb);
            }
            FinishStatus = UDFFinishSetSizeReference(IrpContext, Fcb, FileInfo, FileObject,
                (PUDF_SET_SIZE_REFERENCE)&Owner,
                !Abnormal && NT_SUCCESS(RC) && Owner.PinsOwned,
                CacheMapInitialized, RC);
            if (!Abnormal) RC = FinishStatus;
        } _SEH2_FINALLY {
            PinStatus = UDFReleaseSetSizePins(IrpContext, (PUDF_SET_SIZE_REFERENCE)&Owner);
            if (!Abnormal && NT_SUCCESS(RC) && PinStatus != STATUS_SUCCESS) RC = PinStatus;
        } _SEH2_END;
    } _SEH2_END;
    return(RC);
} // end UDFSetAllocationInformation()

/*
    Set end of file (resize).
 */
NTSTATUS
UDFSetEndOfFileInfo(
    IN PIRP_CONTEXT IrpContext,
    IN PIO_STACK_LOCATION              IrpSp,
    IN PFCB                            Fcb,
    IN PCCB                            Ccb,
    IN PVCB                            Vcb,
    IN PFILE_OBJECT                    FileObject,
    IN PIRP                            Irp,
    IN PFILE_END_OF_FILE_INFORMATION   PtrBuffer
    )
{
    PUDF_FILE_INFO FileInfo = Ccb && Ccb->FileInfo ? Ccb->FileInfo : Fcb->FileInfo;
    _SEH2_VOLATILE NTSTATUS RC = STATUS_SUCCESS;
    BOOLEAN         TruncatedFile = FALSE;
    BOOLEAN         ModifiedAllocSize = FALSE;
    ULONG           Attr;
    PDIR_INDEX_ITEM DirNdx;
    LONGLONG        OldFileSize;
//    BOOLEAN         ZeroBlock;
    _SEH2_VOLATILE BOOLEAN CacheMapInitialized = FALSE;
    _SEH2_VOLATILE BOOLEAN PagingAcquired = FALSE;
    _SEH2_VOLATILE UDF_SET_SIZE_REFERENCE Owner = {0};

    if (!FileInfo || !FileInfo->Dloc)
        return STATUS_FILE_INVALID;
    if (!PtrBuffer || PtrBuffer->EndOfFile.QuadPart < 0) return STATUS_INVALID_PARAMETER;

    // Allocation is only allowed on a file and not a directory

    if (NodeType(Fcb) != UDF_NODE_TYPE_DATA) {

        return STATUS_INVALID_PARAMETER;
    }

    _SEH2_TRY {
        // Increasing the allocation size associated with a file stream
        // is relatively easy. All we have to do is execute some FSD
        // specific code to check whether we have enough space available
        // (and if the FSD supports user/volume quotas, whether the user
        // is not exceeding quota), and then increase the file size in the
        // corresponding on-disk and in-memory structures.
        // Then, all we should do is inform the Cache Manager about the
        // increased allocation size.

        if ((Fcb->FcbState & UDF_FCB_DELETED) ||
           (Fcb->NtReqFCBFlags & UDF_NTREQ_FCB_DELETED)) {
#ifdef UDF_DBG
            if (UDFGetFileLinkCount(FileInfo) < 1) {
                BrutePoint();
                try_return(RC = STATUS_SUCCESS);
            } else
#endif // UDF_DBG
                try_return(RC = STATUS_SUCCESS);
        }

        Fcb->Header.IsFastIoPossible = UDFIsFastIoPossible(Fcb);

        if ((FileObject->SectionObjectPointer->DataSectionObject != NULL) &&
            (FileObject->SectionObjectPointer->SharedCacheMap == NULL) &&
            !(Irp->Flags & IRP_PAGING_IO)) {

            ASSERT( !FlagOn( FileObject->Flags, FO_CLEANUP_COMPLETE ) );
            //  Now initialize the cache map.
            MmPrint(("    CcInitializeCacheMap()\n"));
            CcInitializeCacheMap(FileObject,
                                 (PCC_FILE_SIZES)&Fcb->Header.AllocationSize,
                                 FALSE,
                                 &UdfData.CacheMgrCallBacks,
                                 Fcb);

            CacheMapInitialized = TRUE;
        }
        UDFAcquirePagingIoExclusive(IrpContext, Fcb);
        PagingAcquired = TRUE;

        //  Do a special case here for the lazy write of file sizes.
        if (IrpSp->Parameters.SetFile.AdvanceOnly) {
            //  Never have the dirent filesize larger than the fcb filesize
            PtrBuffer->EndOfFile.QuadPart =
                min(PtrBuffer->EndOfFile.QuadPart, Fcb->Header.FileSize.QuadPart);
            //  Only advance the file size, never reduce it with this call
            RC = STATUS_SUCCESS;
            if (UDFGetFileSizeFromDirNdx(Vcb, FileInfo) >=
               PtrBuffer->EndOfFile.QuadPart)
                try_return(RC);

            UDFSetFileSizeInDirNdx(Vcb, FileInfo, &(PtrBuffer->EndOfFile.QuadPart));
            goto notify_size_changes;
        }

        //             !!! IMPORTANT !!!

        // We can get here after all Handles to the file are closed
        // To prevent allocation size incoherency we should
        // Own the exact FileInfo before resize. The outer finally consumes
        // that reference once, separately from metadata close completion.

        // Are we increasing the allocation size?
        OldFileSize = Fcb->Header.FileSize.QuadPart;
        if (OldFileSize < PtrBuffer->EndOfFile.QuadPart) {

            // Yes. Do the FSD specific stuff i.e. increase reserved
            // space on disk.
/*
            if (FileObject->PrivateCacheMap)
                ZeroBlock = TRUE;
*/

            RC = UDFAcquireSetSizeReference(IrpContext, Fcb, FileInfo,
                                           (PUDF_SET_SIZE_REFERENCE)&Owner);
            if (RC != STATUS_SUCCESS) try_return(RC);
            // perform resize operation
            RC = UDFResizeFile__(IrpContext, Vcb, FileInfo, PtrBuffer->EndOfFile.QuadPart);
            if (!NT_SUCCESS(RC)) {
                try_return(RC);
            }
            // update values in NtReqFcb
            Fcb->Header.FileSize.QuadPart =
//            NtReqFcb->CommonFCBHeader.ValidDataLength.QuadPart =
                PtrBuffer->EndOfFile.QuadPart;
            ModifiedAllocSize = TRUE;

        } else if (Fcb->Header.FileSize.QuadPart > PtrBuffer->EndOfFile.QuadPart) {

            // This is the painful part. See if the VMM will allow us to proceed.
            // The VMM will deny the request if:
            // (a) any image section exists OR
            // (b) a data section exists and the size of the user mapped view
            //       is greater than the new size
            // Otherwise, the VMM should allow the request to proceed.

            MmPrint(("    MmCanFileBeTruncated()\n"));
            if (!MmCanFileBeTruncated(&Fcb->FcbNonpaged->SegmentObject, &PtrBuffer->EndOfFile)) {
                // VMM said no way!
                try_return(RC = STATUS_USER_MAPPED_FILE);
            }

            // Perform directory entry modifications. Release any on-disk
            // space we may need to in the process.
            RC = UDFAcquireSetSizeReference(IrpContext, Fcb, FileInfo,
                                           (PUDF_SET_SIZE_REFERENCE)&Owner);
            if (RC != STATUS_SUCCESS) try_return(RC);
            // perform resize operation
            RC = UDFResizeFile__(IrpContext, Vcb, FileInfo, PtrBuffer->EndOfFile.QuadPart);

            ModifiedAllocSize = TRUE;
            TruncatedFile = TRUE;
        }

        // This is a good place to check if we have performed a truncate
        // operation. If we have perform a truncate (whether we extended
        // or reduced file size), we should update file time stamps.

        // Last, but not the least, we must inform the Cache Manager of file size changes.
        if (ModifiedAllocSize && NT_SUCCESS(RC)) {
            // If we decreased the allocation size to less than the
            // current file size, modify the file size value.
            // Similarly, if we decreased the value to less than the
            // current valid data length, modify that value as well.
            if (TruncatedFile) {
                if (Fcb->Header.ValidDataLength.QuadPart > PtrBuffer->EndOfFile.QuadPart) {
                    // Decrease the valid data length value.
                    Fcb->Header.ValidDataLength = PtrBuffer->EndOfFile;
                }
                if (Fcb->Header.FileSize.QuadPart > PtrBuffer->EndOfFile.QuadPart) {
                    // Decrease the file size value.
                    Fcb->Header.FileSize = PtrBuffer->EndOfFile;
                }
                UDFSetFileSizeInDirNdx(Vcb, FileInfo, NULL);
            } else {
                // Update the FCB Header with the new allocation size.
                // NT expects AllocationSize to be decreased on Close only
                Fcb->Header.AllocationSize.QuadPart =
                    PtrBuffer->EndOfFile.QuadPart;

                UDFSetFileSizeInDirNdx(Vcb, FileInfo, &(PtrBuffer->EndOfFile.QuadPart));
            }

            FileObject->Flags |= FO_FILE_MODIFIED;
//                UDFGetFileAllocationSize(Vcb, FileInfo);

            // If the FCB has not had caching initiated, it is still valid
            // for us to invoke the NT Cache Manager. It is possible in such
            // situations for the call to be no'oped (unless some user has
            // mapped in the file)

            // Archive bit
            if (Vcb->CompatFlags & UDF_VCB_IC_UPDATE_ARCH_BIT) {
                DirNdx = UDFDirIndex(UDFGetDirIndexByFileInfo(FileInfo), FileInfo->Index);
                if (Ccb)
                    Ccb->Flags &= ~UDF_CCB_ATTRIBUTES_SET;
                if (DirNdx) {
                    Attr = UDFAttributesToNT(DirNdx, FileInfo->Dloc->FileEntry);
                    if (!(Attr & FILE_ATTRIBUTE_ARCHIVE))
                        UDFAttributesToUDF(DirNdx, FileInfo->Dloc->FileEntry, Attr | FILE_ATTRIBUTE_ARCHIVE);
                } else {
                    /* The archive bit is an FE field, not a FID field. */
                    DIR_INDEX_ITEM AttributesIndex;
                    RtlZeroMemory(&AttributesIndex, sizeof(AttributesIndex));
                    AttributesIndex.FileInfo = FileInfo;
                    Attr = Fcb->FileAttributes | FILE_ATTRIBUTE_ARCHIVE;
                    UDFAttributesToUDF(&AttributesIndex, FileInfo->Dloc->FileEntry, Attr);
                    Fcb->FileAttributes = AttributesIndex.SysAttr;
                }
            }

            // NOTE: The invocation to CcSetFileSizes() will quite possibly
            //  result in a recursive call back into the file system.
            //  This is because the NT Cache Manager will typically
            //  perform a flush before telling the VMM to purge pages
            //  especially when caching has not been initiated on the
            //  file stream, but the user has mapped the file into
            //  the process' virtual address space.
            MmPrint(("    CcSetFileSizes(), thrd:%8.8x\n",PsGetCurrentThread()));

            Fcb->NtReqFCBFlags |= UDF_NTREQ_FCB_MODIFIED;
            RC = UDFPublishSetSizeCache(IrpContext, Fcb, FileObject);
            if (RC != STATUS_SUCCESS) try_return(RC);

/*            if (ZeroBlock) {
                UDFZeroDataEx(NtReqFcb,
                              OldFileSize,
                              PtrBuffer->EndOfFile.QuadPart - OldFileSize,
                              TRUE // CanWait, Vcb, FileObject);
            }*/
notify_size_changes:

            // Inform any pending IRPs (notify change directory).
            if (UDFIsAStream(FileInfo)) {
                UDFNotifyReportChange( IrpContext, Vcb, Fcb,
                                           FILE_NOTIFY_CHANGE_STREAM_SIZE,
                                           FILE_ACTION_MODIFIED_STREAM,
                                           Ccb ? Ccb->Lcb : NULL, FileObject);
            } else {
                UDFNotifyReportChange( IrpContext, Vcb, Fcb,
                                           FILE_NOTIFY_CHANGE_SIZE,
                                           FILE_ACTION_MODIFIED,
                                           Ccb ? Ccb->Lcb : NULL, FileObject);
            }
        }

try_exit: NOTHING;

    } _SEH2_FINALLY {

        BOOLEAN Abnormal = _SEH2_AbnormalTermination();
        NTSTATUS FinishStatus, PinStatus;
        _SEH2_TRY {
            if (PagingAcquired) {
                PagingAcquired = FALSE;
                UDFReleasePagingIo(IrpContext, Fcb);
            }
            FinishStatus = UDFFinishSetSizeReference(IrpContext, Fcb, FileInfo, FileObject,
                (PUDF_SET_SIZE_REFERENCE)&Owner,
                !Abnormal && NT_SUCCESS(RC) && Owner.PinsOwned,
                CacheMapInitialized, RC);
            if (!Abnormal) RC = FinishStatus;
        } _SEH2_FINALLY {
            PinStatus = UDFReleaseSetSizePins(IrpContext, (PUDF_SET_SIZE_REFERENCE)&Owner);
            if (!Abnormal && NT_SUCCESS(RC) && PinStatus != STATUS_SUCCESS) RC = PinStatus;
        } _SEH2_END;
    } _SEH2_END;
    return(RC);
} // end UDFSetEndOfFileInfo()



/*
    Check if a directory subtree has any open handles (FcbCleanup != 0).
    Iterative depth-first walk via ChildLcbQueue — no recursion, kernel-safe.
    Returns TRUE if subtree is clean (no open handles), FALSE otherwise.
*/
BOOLEAN
UDFCheckDirOpenHandles(
    IN PFCB DirectoryFcb
    )
{
    PFCB CheckFcb = DirectoryFcb;
    PLIST_ENTRY CheckLink = CheckFcb->ChildLcbQueue.Flink;

    while (CheckFcb != NULL) {

        // Process all children at current level
        while (CheckLink != &CheckFcb->ChildLcbQueue) {
            PLCB ChildLcb = CONTAINING_RECORD(CheckLink, struct LCB, ParentFcbLinks);
            PFCB ChildFcb = ChildLcb->ChildFcb;
            CheckLink = CheckLink->Flink;

            if (!ChildFcb) continue;

            if (ChildFcb->FcbCleanup != 0) {
                return FALSE;
            }

            // If child is a directory with children, descend into it
            if ((ChildFcb->FcbState & UDF_FCB_DIRECTORY) &&
                !IsListEmpty(&ChildFcb->ChildLcbQueue)) {
                CheckFcb = ChildFcb;
                CheckLink = CheckFcb->ChildLcbQueue.Flink;
            }
        }

        // Back to root — done
        if (CheckFcb == DirectoryFcb) break;

        // Ascend: find our LCB in parent, advance to next sibling
        PLIST_ENTRY ParentLink;
        for (ParentLink = CheckFcb->ParentLcbQueue.Flink;
             ParentLink != &CheckFcb->ParentLcbQueue;
             ParentLink = ParentLink->Flink) {
            PLCB ParentLcb = CONTAINING_RECORD(ParentLink, struct LCB, ChildFcbLinks);
            if (ParentLcb->ParentFcb) {
                CheckFcb = ParentLcb->ParentFcb;
                CheckLink = ParentLcb->ParentFcbLinks.Flink;
                break;
            }
        }
    }

    return TRUE;
}

/* The source FCB and VCB resources are held exclusive by the caller.
 * IoCheckShareAccess does not count attribute-only opens. Permit those peers
 * only when the renaming FILE_OBJECT is the sole data/delete participant and
 * every outstanding cleanup belongs to this same live name link. This keeps
 * other data handles and unresolved/by-ID ownership on the conservative path.
 */
static BOOLEAN
UDFRenameHasOnlyMetadataPeers(
    IN PFCB Fcb,
    IN PCCB Ccb,
    IN PFILE_OBJECT FileObject
    )
{
    return FileObject->DeleteAccess &&
           (UDFIsADirectory(Fcb->FileInfo) ||
            UDFGetFileNameLinkCount(Fcb->FileInfo) == 1) &&
           Fcb->ShareAccess.OpenCount == 1 &&
           Ccb->Lcb &&
           Ccb->Lcb->ChildFcb == Fcb &&
           Ccb->FileInfo && Ccb->FileInfo->ParentFile &&
           Ccb->Lcb->LinkFileInfo == Ccb->FileInfo &&
           Ccb->Lcb->ParentFcb == Ccb->FileInfo->ParentFile->Fcb &&
           !FlagOn(Ccb->Lcb->Flags, UDF_LCB_FLAG_LINK_DELETED) &&
           Ccb->Lcb->Reference == Fcb->FcbCleanup;
}

/*
    Rename or move file
 */
NTSTATUS
UDFSetRenameInfo(
    IN PIRP_CONTEXT IrpContext,
    IN PFCB Fcb,
    IN PCCB Ccb,
    IN PFILE_OBJECT FileObject,   // Source File
    IN PFILE_RENAME_INFORMATION PtrBuffer
    )
{
    PIRP Irp = IrpContext->Irp;
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    PFILE_OBJECT TargetFileObject = IrpSp->Parameters.SetFile.FileObject;
    BOOLEAN ReplaceIfExists = IrpSp->Parameters.SetFile.ReplaceIfExists;

    PFCB ParentFcb = Ccb->FileInfo && Ccb->FileInfo->ParentFile ?
        Ccb->FileInfo->ParentFile->Fcb : NULL;

    NTSTATUS RC = STATUS_SUCCESS;
    NTSTATUS RenameStatus = STATUS_SUCCESS;
    BOOLEAN NamespaceChanged = FALSE, TargetRemoved = FALSE;
    UDF_PREFIX_RENAME_PLAN PrefixPlan;
    PVCB Vcb = Fcb->Vcb;
    PFCB TargetFcb;
    BOOLEAN IgnoreCase;
    BOOLEAN SourceFcbAcquired = FALSE;
    BOOLEAN ParentFcbAcquired = FALSE;
    BOOLEAN TargetParentFcbAcquired = FALSE;
    BOOLEAN StaleFcbAcquired = FALSE;
    PFCB StaleFcb = NULL;
    PLCB StaleLcb = NULL;
    BOOLEAN NeedRemovePrefix = FALSE;
    BOOLEAN SingleDir = TRUE;

    PUDF_FILE_INFO FileInfo;
    PUDF_FILE_INFO DirInfo;
    PUDF_FILE_INFO TargetDirInfo;

    UNICODE_STRING NewName;
    UNICODE_STRING LocalPath;

    LocalPath.Buffer = NULL;
    RtlZeroMemory(&PrefixPlan, sizeof(PrefixPlan));

    // Make sure we can wait for this request.

    if (!FlagOn(IrpContext->Flags, IRP_CONTEXT_FLAG_WAIT)) {

        UDFRaiseStatus(IrpContext, STATUS_CANT_WAIT);
    }

    _SEH2_TRY {

        // do we try to rename Volume ?
        if (ParentFcb == NULL) {

             try_return (RC = STATUS_INVALID_PARAMETER);
        }

        // do we try to rename RootDir ?
        if (Fcb == Vcb->RootIndexFcb) {

            try_return (RC = STATUS_INVALID_PARAMETER);
        }

        /* Serialize the open/share counters and keep the source FI/LCB stable.
         * Use the same child-before-parent order as cleanup. */
        UDF_CHECK_PAGING_IO_RESOURCE(Fcb);
        UDFAcquireFcbExclusive(IrpContext, Fcb, FALSE);
        SourceFcbAcquired = TRUE;

        FileInfo = Ccb->FileInfo;
        if (!FileInfo || !FileInfo->ParentFile)
            try_return(RC = STATUS_FILE_INVALID);
        if (FileInfo->LinkDeleteCommitted ||
            (Ccb->Lcb && FlagOn(Ccb->Lcb->Flags, UDF_LCB_FLAG_LINK_DELETED)))
            try_return(RC = STATUS_FILE_DELETED);
        if (UDFIsLinkDeletePending(Fcb, FileInfo) || Ccb->DeleteOnCloseRequest)
            try_return(RC = STATUS_DELETE_PENDING);
        DirInfo = FileInfo->ParentFile;
        ASSERT(DirInfo);

        // We don't allow this operation on a file opened by file Id.

        if (FlagOn(Ccb->Flags, CCB_FLAG_OPEN_BY_ID | CCB_FLAG_OPEN_RELATIVE_BY_ID)) {

            try_return (RC = STATUS_INVALID_PARAMETER);
        }

        // do we try to rename to RootDir or Volume ?
        if (!TargetFileObject) {

            TargetDirInfo = FileInfo->ParentFile;
            // For streams or same-dir rename, target FCB is the parent
            TargetFcb = TargetDirInfo->Fcb;

        } else {

            PCCB TargetCcb;
            UDFDecodeFileObject(TargetFileObject, &TargetFcb, &TargetCcb);

            ASSERT_FCB(TargetFcb);

            if (!TargetFcb) {
                try_return (RC = STATUS_INVALID_PARAMETER);
            }

            // Check if TargetFcb is a directory or a file
            // TargetFileObject should be a directory.
            // But if it points to a file (e.g., existing file to be replaced),
            // we need to use its parent directory for correct SingleDir calculation

            PUDF_FILE_INFO TargetFileInfo = TargetCcb ? TargetCcb->FileInfo : NULL;
            if (!TargetFileInfo)
                try_return(RC = STATUS_FILE_INVALID);
            if (UDFIsADirectory(TargetFileInfo)) {

                TargetDirInfo = TargetFileInfo;

            } else {

                // TargetFileObject points to a file, use its parent directory
                TargetDirInfo = TargetFileInfo->ParentFile;

                if (!TargetDirInfo) {
                    try_return (RC = STATUS_INVALID_PARAMETER);
                }
            }
        }

        // invalid destination ?
        if (!TargetDirInfo) try_return (RC = STATUS_ACCESS_DENIED);

        // Stream can't be a Dir or have StreamDir
        if (UDFIsAStreamDir(TargetDirInfo)) {
            if (UDFIsADirectory(FileInfo) ||
               UDFHasAStreamDir(FileInfo)) {
                try_return (RC = STATUS_ACCESS_DENIED);
            }
        }

        // Parse NewName before acquiring locks to enable early-out optimizations
        if (!TargetFileObject) {
            //  Make sure the name is of legal length.
            if (PtrBuffer->FileNameLength > UDF_NAME_LEN*sizeof(WCHAR)) {
                try_return(RC = STATUS_OBJECT_NAME_INVALID);
            }
            NewName.Length = NewName.MaximumLength = (USHORT)(PtrBuffer->FileNameLength);
            NewName.Buffer = (PWCHAR)&(PtrBuffer->FileName);
        } else {
            // TargetFileObject->FileName is set up by UDFCommonCreate for
            // SL_OPEN_TARGET_DIRECTORY:
            //   Length      = parent directory path (trimmed to last '\')
            //   MaximumLength = full original path (parent + '\' + filename)
            // Extract the filename component starting after Length.
            USHORT FileNameStart = TargetFileObject->FileName.Length;

            if (FileNameStart < TargetFileObject->FileName.MaximumLength &&
                TargetFileObject->FileName.Buffer[FileNameStart / sizeof(WCHAR)] == L'\\') {
                FileNameStart += sizeof(WCHAR);
            }

            NewName.Length = TargetFileObject->FileName.MaximumLength - FileNameStart;
            NewName.MaximumLength = NewName.Length;
            NewName.Buffer = (PWCHAR)((PCHAR)TargetFileObject->FileName.Buffer + FileNameStart);
        }

        IgnoreCase = FlagOn(Ccb->Flags, CCB_FLAG_IGNORE_CASE);

        SingleDir = (TargetDirInfo->Fcb == ParentFcb);

        // Self-rename check: if same directory and same name → no-op
        if (SingleDir && !ReplaceIfExists && Ccb->Lcb) {
            if (FsRtlAreNamesEqual(&NewName, &Ccb->Lcb->ExactCaseLinkName, FALSE, NULL)) {
                try_return(RC = STATUS_SUCCESS);
            }
        }

        //
        // Always acquire source parent directory — needed for splay tree
        // modifications in UDFRenameMovePrefix and UDFRemovePrefix.
        //
        if (SingleDir) {

            UDFAcquireFcbExclusive(IrpContext, ParentFcb, FALSE);
            ParentFcbAcquired = TRUE;

        } else {

            // Cross-directory rename: acquire both parents.
            // Use address ordering to prevent ABBA deadlock.
            if ((ULONG_PTR)ParentFcb < (ULONG_PTR)TargetDirInfo->Fcb) {
                UDFAcquireFcbExclusive(IrpContext, ParentFcb, FALSE);
                ParentFcbAcquired = TRUE;
                UDFAcquireFcbExclusive(IrpContext, TargetDirInfo->Fcb, FALSE);
                TargetParentFcbAcquired = TRUE;
            } else if (ParentFcb != TargetDirInfo->Fcb) {
                UDFAcquireFcbExclusive(IrpContext, TargetDirInfo->Fcb, FALSE);
                TargetParentFcbAcquired = TRUE;
                UDFAcquireFcbExclusive(IrpContext, ParentFcb, FALSE);
                ParentFcbAcquired = TRUE;
            } else {
                // Same directory (shouldn't happen for !SingleDir, but handle gracefully)
                UDFAcquireFcbExclusive(IrpContext, ParentFcb, FALSE);
                ParentFcbAcquired = TRUE;
            }
        }

        // Extra metadata-only opens do not make the source data busy.
        if (Fcb->FcbCleanup > 1 &&
            !UDFRenameHasOnlyMetadataPeers(Fcb, Ccb, FileObject)) {

            try_return (RC = STATUS_ACCESS_DENIED);
        }

        ASSERT(Fcb->FcbCleanup);
        ASSERT(!Fcb->IrpContextLite);

        if (Fcb->IrpContextLite) {
            try_return (RC = STATUS_ACCESS_DENIED);
        }

        // For directories: check that no descendant has open handles.
        if ((Fcb->FcbState & UDF_FCB_DIRECTORY) &&
            !UDFCheckDirOpenHandles(Fcb)) {
            try_return (RC = STATUS_ACCESS_DENIED);
        }

        // Check if renaming to stream name - only allowed for streams
        if (!TargetFileObject && NewName.Length >= sizeof(WCHAR) && NewName.Buffer[0] == L':') {

            if (!UDFIsAStream(FileInfo)) {

                try_return(RC = STATUS_OBJECT_NAME_INVALID);
            }
        }

        // UDFDoesOSAllowFileToBeMoved__ (= UDFDoesOSAllowFileToBeUnlinked__)
        // checks DirNdx->FileInfo != NULL for each child — blocks if any
        // child has a cached FileInfo structure, even with FcbCleanup=0
        // (no user handles). This is too strict for rename/move where file
        // data stays intact. The subtree is already validated above via
        // UDFCheckDirOpenHandles which checks FcbCleanup (user handles).
        {

            // Note: With LCB model, FcbReference may not always be >= RefCount
            // for directories opened internally during path traversal

            // Pre-validate target for ReplaceIfExists: look up the target name
            // in the directory index, then search for its LCB by FCB pointer
            // (not by name). This correctly handles case-insensitive matches,
            // short name aliases, and avoids string comparison overhead.
            if (ReplaceIfExists) {

                PFCB TargetParentFcb = TargetDirInfo->Fcb;

                if (TargetParentFcb) {

                    // Look up target name in directory index
                    DIR_ENUM_CONTEXT TargetDirContext;
                    NTSTATUS FindStatus = UDFFindDirEntry(Vcb, TargetDirInfo,
                                                         &NewName, IgnoreCase,
                                                         TRUE, &TargetDirContext);

                    if (NT_SUCCESS(FindStatus) && TargetDirContext.DirNdx) {

                        PDIR_INDEX_ITEM FoundDirNdx = TargetDirContext.DirNdx;

                        // Cannot replace a directory
                        if (FoundDirNdx->FileCharacteristics & FILE_DIRECTORY) {

                            try_return(RC = STATUS_OBJECT_NAME_COLLISION);
                        }

                        // Skip stale LCB logic for self-rename (e.g. case change)
                        BOOLEAN IsSelfRename = (SingleDir &&
                                                FoundDirNdx->FileInfo == FileInfo);

                        if (!IsSelfRename) {

                            // If target was previously opened, find its LCB
                            // by matching ChildFcb pointer
                            if (FoundDirNdx->FileInfo &&
                                FoundDirNdx->FileInfo->Fcb) {

                                PFCB TargetFileFcb = FoundDirNdx->FileInfo->Fcb;

                                PLIST_ENTRY Link;
                                for (Link = TargetParentFcb->ChildLcbQueue.Flink;
                                     Link != &TargetParentFcb->ChildLcbQueue;
                                     Link = Link->Flink) {
                                    PLCB TestLcb = CONTAINING_RECORD(Link, struct LCB, ParentFcbLinks);
                                    if (TestLcb != Ccb->Lcb &&
                                        !(TestLcb->Flags & UDF_LCB_FLAG_LINK_DELETED) &&
                                        TestLcb->ChildFcb == TargetFileFcb &&
                                        TestLcb->LinkFileInfo &&
                                        TestLcb->LinkFileInfo->Index == TargetDirContext.Index &&
                                        TestLcb->ParentFileInfo &&
                                        TestLcb->ParentFileInfo->Dloc == TargetDirInfo->Dloc) {
                                        StaleLcb = TestLcb;
                                        break;
                                    }
                                }
                            }
                            // If FileInfo is NULL (never opened), no FCB/LCB exists.
                            // UDFRenameMoveFile__ will handle the on-disk deletion.

                            if (StaleLcb) {

                                StaleFcb = StaleLcb->ChildFcb;

                                // Acquire target FCB to serialize with cleanup
                                if (StaleFcb) {
                                    UDF_CHECK_PAGING_IO_RESOURCE(StaleFcb);
                                    if (!UDFAcquireFcbExclusive(IrpContext, StaleFcb, TRUE)) {
                                        try_return(RC = STATUS_ACCESS_DENIED);
                                    }
                                    StaleFcbAcquired = TRUE;
                                }

                                // Cannot remove LCB that still has open references.
                                if (StaleLcb->Reference != 0) {

                                    try_return(RC = STATUS_ACCESS_DENIED);
                                }
                            }
                        }
                    }
                    // If target not found in directory, proceed normally.
                    // UDFRenameMoveFile__ will create the new entry.
                }
            }

            if (!Ccb->Lcb)
                try_return(RC = STATUS_FILE_INVALID);
            RC = UDFPreparePrefixRename(IrpContext, Ccb->Lcb, &NewName,
                                         TargetDirInfo, &PrefixPlan);
            if (!NT_SUCCESS(RC))
                try_return(RC);

            //
            // Pre-rename: mark stale LCB as deleted so concurrent
            // create lookups won't find it during disk operations.
            // Purge cache before modifying directory.
            //
            if (StaleLcb) {
                StaleLcb->Flags |= UDF_LCB_FLAG_LINK_DELETED;

                // Remove from splay trees immediately so the renamed LCB
                // can be inserted with the same name without duplicate conflict.
                if (StaleLcb->ParentFcb) {
                    UdfRemoveNameLinks(StaleLcb->ParentFcb, StaleLcb);
                }

                // Purge cache for target before disk operations
                if (StaleFcb && StaleFcb->FcbNonpaged) {
                    if (StaleFcb->FcbNonpaged->SegmentObject.DataSectionObject ||
                        StaleFcb->FcbNonpaged->SegmentObject.ImageSectionObject) {
                        MmPrint(("    CcPurgeCacheSection() stale target\n"));
                        CcPurgeCacheSection(&StaleFcb->FcbNonpaged->SegmentObject, NULL, 0, FALSE);
                    }
                }
            }

            RenameStatus = UDFRenameMoveFile__(IrpContext, Vcb, IgnoreCase,
                &ReplaceIfExists, &NewName, DirInfo, TargetDirInfo, FileInfo,
                &NamespaceChanged, &TargetRemoved);
            RC = RenameStatus;
        }
        NeedRemovePrefix = StaleLcb && TargetRemoved && !StaleLcb->Reference;
        if (!NT_SUCCESS(RC) && !NamespaceChanged) {
            // Rename failed — restore original state if target was not
            // actually deleted on disk.
            if (StaleLcb && !TargetRemoved) {
                ClearFlag(StaleLcb->Flags, UDF_LCB_FLAG_LINK_DELETED);
                // Re-insert into splay trees (was removed before rename attempt)
                if (StaleLcb->ParentFcb) {
                    UdfInsertNameLinks(StaleLcb->ParentFcb, StaleLcb);
                }
            }
            try_return (RC);
        }

        //
        // Rename succeeded — set NeedRemovePrefix so the finally
        // handler removes the stale LCB from queues.
        //
        /* TargetRemoved describes the exact replaced link. Its shared
         * FCB may still own other hard links and must remain live. */

        ASSERT(UDFDirIndex(FileInfo->ParentFile->Dloc->DirIndex, FileInfo->Index)->FileInfo == FileInfo);

        {
            ULONG Filter = UDFIsADirectory(FileInfo)
                         ? FILE_NOTIFY_CHANGE_DIR_NAME
                         : FILE_NOTIFY_CHANGE_FILE_NAME;

            // Step 1: Notify OLD location (LCB still points to old parent)
            // Use FileObject->FileName — stable path independent of LCB chain
            if (SingleDir && !ReplaceIfExists) {
                UDFNotifyReportChange(IrpContext, Vcb, Fcb, Filter, FILE_ACTION_RENAMED_OLD_NAME,
                                      Ccb->Lcb, FileObject);
            } else {
                UDFNotifyReportChange(IrpContext, Vcb, Fcb, Filter, FILE_ACTION_REMOVED,
                                      Ccb->Lcb, FileObject);
            }

            // Step 2: Move LCB to new parent and update name
            if (Ccb->Lcb) {
                NTSTATUS PrefixStatus = UDFApplyPrefixRename(IrpContext, &PrefixPlan, NT_SUCCESS(RenameStatus));
                if (NT_SUCCESS(RenameStatus)) RenameStatus = PrefixStatus;
                if (!PrefixPlan.Applied) {
                    Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
                    try_return(RC = RenameStatus);
                }
            }

            // Step 3: Update FCB parent pointer for cross-directory rename
            if (!SingleDir) {
                if (Fcb->FileInfo == FileInfo)
                    Fcb->ParentFcb = TargetDirInfo->Fcb;
            }

            // Step 4: Notify NEW location (LCB now points to new parent)
            // The prepared LCB now has its new component name.
            if (SingleDir && !ReplaceIfExists) {
                UDFNotifyReportChange(IrpContext, Vcb, Fcb, Filter, FILE_ACTION_RENAMED_NEW_NAME,
                                      Ccb->Lcb, NULL);
            } else if (ReplaceIfExists) {
                UDFNotifyReportChange(IrpContext, Vcb, Fcb,
                    FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_SIZE |
                    FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_LAST_ACCESS |
                    FILE_NOTIFY_CHANGE_CREATION | FILE_NOTIFY_CHANGE_EA,
                    FILE_ACTION_MODIFIED, Ccb->Lcb, NULL);
            } else {
                UDFNotifyReportChange(IrpContext, Vcb, Fcb, Filter, FILE_ACTION_ADDED,
                                      Ccb->Lcb, NULL);
            }
        }

        ASSERT(TargetDirInfo->RefCount || SingleDir);

        RC = RenameStatus;

try_exit:    NOTHING;

    } _SEH2_FINALLY {
        BOOLEAN Abnormal = _SEH2_AbnormalTermination();
        BOOLEAN StaleRemoved = FALSE;
        NTSTATUS DiscardStatus;
        NeedRemovePrefix = StaleLcb && TargetRemoved && !StaleLcb->Reference;
        if (Abnormal && (NamespaceChanged || TargetRemoved ||
                         Vcb->PendingNamespaceCount)) {
            Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
            InterlockedCompareExchange(&Vcb->MetadataWritebackError,
                                        STATUS_UNSUCCESSFUL, STATUS_SUCCESS);
        }
        /* Keep prefix identity consistent with an FI that already moved,
         * including on expected I/O exceptions. No new I/O while unwinding. */
        if (NamespaceChanged && PrefixPlan.Prepared && !PrefixPlan.Applied)
            UDFApplyPrefixRename(IrpContext, &PrefixPlan, FALSE);
        _SEH2_TRY {
            DiscardStatus = UDFDiscardPrefixRename(IrpContext, &PrefixPlan, !Abnormal && NT_SUCCESS(RC));
            if (!Abnormal && NT_SUCCESS(RC))
                RC = DiscardStatus;
        } _SEH2_FINALLY {
            BOOLEAN CleanupAbnormal = Abnormal || _SEH2_AbnormalTermination();
            _SEH2_TRY {
                /* Only a committed target unlink retires its exact LCB.
                 * Other links of the shared FCB remain live. */
                if (NeedRemovePrefix && !StaleLcb->Reference &&
                    StaleLcb->ParentFcbLinks.Flink != &StaleLcb->ParentFcbLinks) {
                    PFCB StaleParent = StaleLcb->ParentFcb;
                    PUDF_FILE_INFO ParentInfo = StaleLcb->ParentFileInfo;
                    NTSTATUS CloseStatus = STATUS_SUCCESS;
                    BOOLEAN Consumed = ParentInfo == NULL;
                    UDFRemovePrefix(IrpContext, StaleLcb);
                    if (StaleParent) {
                        if (ParentInfo) {
                            CloseStatus = UDFDereferenceFileChecked(Vcb, ParentInfo);
                            Consumed = NT_SUCCESS(CloseStatus);
                        }
                        _SEH2_TRY {
                            if (Consumed && ParentInfo && !CleanupAbnormal && NT_SUCCESS(RC))
                                CloseStatus = UDFCompleteFileClose(IrpContext, Vcb, ParentInfo);
                        } _SEH2_FINALLY {
                            if (Consumed) {
                                ASSERT(StaleParent->FcbReference);
                                InterlockedDecrement((PLONG)&StaleParent->FcbReference);
                            }
                        } _SEH2_END;
                        UDFRememberMetadataWritebackError(IrpContext, Vcb, ParentInfo, CloseStatus);
                        if (!Abnormal && NT_SUCCESS(RC))
                            RC = CloseStatus;
                    }
                }
                if (StaleFcbAcquired && NeedRemovePrefix && !CleanupAbnormal && NT_SUCCESS(RC))
                    UDFTeardownStructures(IrpContext, StaleFcb, FALSE, &StaleRemoved);
            } _SEH2_FINALLY {
                if (StaleFcbAcquired && !StaleRemoved)
                    UDFReleaseFcb(IrpContext, StaleFcb);
                if (TargetParentFcbAcquired)
                    UDFReleaseFcb(IrpContext, TargetDirInfo->Fcb);
                if (ParentFcbAcquired)
                    UDFReleaseFcb(IrpContext, DirInfo->Fcb);
                if (SourceFcbAcquired)
                    UDFReleaseFcb(IrpContext, Fcb);
                if (LocalPath.Buffer)
                    MyFreePool__(LocalPath.Buffer);
            } _SEH2_END;
        } _SEH2_END;
    } _SEH2_END;

    return RC;
} // end UDFRename()

LONG
UDFFindFileId(
    IN PVCB Vcb,
    IN FILE_ID Id
    )
{
    if (!Vcb->FileIdCache) return (-1);
    for(ULONG i=0; i<Vcb->FileIdCount; i++) {
        if (Vcb->FileIdCache[i].Id.QuadPart == Id.QuadPart) return i;
    }
    return (-1);
} // end UDFFindFileId()

LONG
UDFFindFreeFileId(
    IN PVCB Vcb,
    IN FILE_ID FileId
    )
{
    if (!Vcb->FileIdCache) {
        if (!(Vcb->FileIdCache = (PUDFFileIDCacheItem)MyAllocatePool__(NonPagedPool, sizeof(UDFFileIDCacheItem)*FILE_ID_CACHE_GRANULARITY)))
            return (-1);
        RtlZeroMemory(Vcb->FileIdCache, FILE_ID_CACHE_GRANULARITY*sizeof(UDFFileIDCacheItem));
        Vcb->FileIdCount = FILE_ID_CACHE_GRANULARITY;
    }
    for(ULONG i=0; i<Vcb->FileIdCount; i++) {
        if (!Vcb->FileIdCache[i].FullName.Buffer) return i;
    }
    if (!MyReallocPool__((PCHAR)(Vcb->FileIdCache), Vcb->FileIdCount*sizeof(UDFFileIDCacheItem),
                     (PCHAR*)&(Vcb->FileIdCache), (Vcb->FileIdCount+FILE_ID_CACHE_GRANULARITY)*sizeof(UDFFileIDCacheItem))) {
        return (-1);
    }
    RtlZeroMemory(&(Vcb->FileIdCache[Vcb->FileIdCount]), FILE_ID_CACHE_GRANULARITY*sizeof(UDFFileIDCacheItem));
    Vcb->FileIdCount += FILE_ID_CACHE_GRANULARITY;
    return (Vcb->FileIdCount - FILE_ID_CACHE_GRANULARITY);
} // end UDFFindFreeFileId()

NTSTATUS
UDFStoreFileId(
    IN PVCB Vcb,
    IN PCCB Ccb,
    IN PUDF_FILE_INFO fi,
    IN FILE_ID FileId
    )
{
    LONG i;
    NTSTATUS RC = STATUS_SUCCESS;

    // NOTE: This function appears to be unused legacy code (no callers found).
    // TODO: If ever used, need to add IrpContext parameter and use UDFBuildFullPathFromLcb
    // to build the path from LCB instead of FCBName (which no longer exists).

    if ((i = UDFFindFileId(Vcb, FileId)) == (-1)) {
        if ((i = UDFFindFreeFileId(Vcb, FileId)) == (-1)) return STATUS_INSUFFICIENT_RESOURCES;
    } else {
        return STATUS_SUCCESS;
    }
    Vcb->FileIdCache[i].Id = FileId;
    Vcb->FileIdCache[i].IgnoreCase = BooleanFlagOn(Ccb->Flags, CCB_FLAG_IGNORE_CASE);

    // TODO: Build path from LCB using UDFBuildFullPathFromLcb (requires IrpContext)
    // For now, return error if this function is ever called
    RC = STATUS_NOT_IMPLEMENTED;

    return RC;
} // end UDFStoreFileId()

NTSTATUS
UDFRemoveFileId(
    IN PVCB Vcb,
    IN FILE_ID FileId
    )
{
    LONG i;

    if ((i = UDFFindFileId(Vcb, FileId)) == (-1)) return STATUS_INVALID_PARAMETER;
    MyFreePool__(Vcb->FileIdCache[i].FullName.Buffer);
    RtlZeroMemory(&(Vcb->FileIdCache[i]), sizeof(UDFFileIDCacheItem));
    return STATUS_SUCCESS;
} // end UDFRemoveFileId()

VOID
UDFReleaseFileIdCache(
    IN PVCB Vcb
    )
{
    if (!Vcb->FileIdCache) return;
    for(ULONG i=0; i<Vcb->FileIdCount; i++) {
        if (Vcb->FileIdCache[i].FullName.Buffer) {
            MyFreePool__(Vcb->FileIdCache[i].FullName.Buffer);
        }
    }
    MyFreePool__(Vcb->FileIdCache);
    Vcb->FileIdCache = NULL;
    Vcb->FileIdCount = 0;
} // end UDFReleaseFileIdCache()

NTSTATUS
UDFGetOpenParamsByFileId(
    IN PVCB Vcb,
    IN FILE_ID FileId,
    OUT PUNICODE_STRING* FName,
    OUT BOOLEAN* IgnoreCase
    )
{
    LONG i;

    if ((i = UDFFindFileId(Vcb, FileId)) == (-1)) return STATUS_NOT_FOUND;
    (*FName) = &(Vcb->FileIdCache[i].FullName);
    (*IgnoreCase) = (Vcb->FileIdCache[i].IgnoreCase);
    return STATUS_SUCCESS;
} // end UDFGetOpenParamsByFileId()

#ifdef UDF_ALLOW_HARD_LINKS
/*
    create hard link for the file
 */
NTSTATUS
UDFHardLink(
    IN PIRP_CONTEXT IrpContext,
    IN PIO_STACK_LOCATION IrpSp,
    IN PFCB Fcb1,
    IN PCCB Ccb1,
    IN PFILE_OBJECT FileObject1,
    IN PFILE_LINK_INFORMATION PtrBuffer
    )
{
    PFILE_OBJECT DirObject2 = IrpSp->Parameters.SetFile.FileObject;
    BOOLEAN Replace = IrpSp->Parameters.SetFile.ReplaceIfExists && PtrBuffer->ReplaceIfExists;
    PVCB Vcb = Fcb1->Vcb;
    PFCB TargetFcb = NULL;
    PUDF_FILE_INFO File1 = Ccb1->FileInfo, Dir1 = NULL, Dir2 = NULL;
    BOOLEAN AcquiredDir1 = FALSE, AcquiredDir2 = FALSE, AcquiredFcb1 = FALSE;
    BOOLEAN AcquiredTarget = FALSE;
    _SEH2_VOLATILE BOOLEAN TargetRemoved = FALSE, LinkCreated = FALSE;
    PLCB StaleLcb = NULL, ParentLcb = NULL;
    PLIST_ENTRY Entry;
    UNICODE_STRING NewName, ParentPath, TargetPath;
    ULONG PrefixBytes, TotalBytes;
    USHORT TargetNameOffset = 0;
    _SEH2_VOLATILE NTSTATUS RC = STATUS_SUCCESS;
    BOOLEAN IgnoreCase = FlagOn(Ccb1->Flags, CCB_FLAG_IGNORE_CASE);

    RtlZeroMemory(&ParentPath, sizeof(ParentPath));
    RtlZeroMemory(&TargetPath, sizeof(TargetPath));
    _SEH2_TRY {
        ASSERT_EXCLUSIVE_VCB(Vcb);
        if (!File1 || File1->Fcb != Fcb1 || !(Dir1 = File1->ParentFile) ||
            !Dir1->Fcb || UDFIsADirectory(File1) || UDFIsAStream(File1) || UDFIsAStreamDir(File1))
            try_return(RC = STATUS_INVALID_PARAMETER);
        if (!DirObject2) {
            Dir2 = Dir1;
            if (PtrBuffer->FileNameLength > UDF_NAME_LEN * sizeof(WCHAR))
                try_return(RC = STATUS_OBJECT_NAME_INVALID);
            NewName.Length = (USHORT)PtrBuffer->FileNameLength;
            NewName.MaximumLength = NewName.Length;
            NewName.Buffer = PtrBuffer->FileName;
        } else {
            PFCB ParentFcb;
            PCCB ParentCcb;
            USHORT Start, ParentLength = DirObject2->FileName.Length;
            USHORT FullLength = DirObject2->FileName.MaximumLength;
            if (UDFDecodeFileObject(DirObject2, &ParentFcb, &ParentCcb) != UserDirectoryOpen ||
                !ParentCcb || !(Dir2 = ParentCcb->FileInfo) || !Dir2->Fcb ||
                !DirObject2->FileName.Buffer || ParentLength > FullLength ||
                ParentLength % sizeof(WCHAR) || FullLength % sizeof(WCHAR))
                try_return(RC = STATUS_INVALID_PARAMETER);
            Start = ParentLength;
            if (Start < FullLength && DirObject2->FileName.Buffer[Start / sizeof(WCHAR)] == L'\\')
                Start += sizeof(WCHAR);
            NewName.Buffer = (PWCHAR)((PUCHAR)DirObject2->FileName.Buffer + Start);
            NewName.Length = NewName.MaximumLength = FullLength - Start;
        }
        if (!Dir2 || !Dir2->Fcb || Dir2->Fcb->Vcb != Vcb || UDFIsAStreamDir(Dir2) ||
            !NewName.Buffer || !NewName.Length || NewName.Length % sizeof(WCHAR) ||
            NewName.Length > UDF_NAME_LEN * sizeof(WCHAR))
            try_return(RC = STATUS_OBJECT_NAME_INVALID);

        /* No successful-looking IgnoreWait acquisition: each flag records
         * a resource actually acquired, including cross-directory links. */
        if (!UDFAcquireFcbExclusive(IrpContext, Dir1->Fcb, TRUE))
            try_return(RC = STATUS_SHARING_VIOLATION);
        AcquiredDir1 = TRUE;
        if (Dir2->Fcb != Dir1->Fcb) {
            if (!UDFAcquireFcbExclusive(IrpContext, Dir2->Fcb, TRUE))
                try_return(RC = STATUS_SHARING_VIOLATION);
            AcquiredDir2 = TRUE;
        }
        if (!UDFAcquireFcbExclusive(IrpContext, Fcb1, TRUE))
            try_return(RC = STATUS_SHARING_VIOLATION);
        AcquiredFcb1 = TRUE;

        /* Reserve notification storage before a successful namespace change.
         * The path builder returns ExAllocatePool storage, never MyPool. */
        if (!FlagOn(Dir2->Fcb->FcbState, UDF_FCB_ROOT_DIRECTORY)) {
            for (Entry = Dir2->Fcb->ParentLcbQueue.Flink;
                 Entry != &Dir2->Fcb->ParentLcbQueue; Entry = Entry->Flink) {
                PLCB Candidate = CONTAINING_RECORD(Entry, struct LCB, ChildFcbLinks);
                if (!FlagOn(Candidate->Flags, UDF_LCB_FLAG_LINK_DELETED)) {
                    ParentLcb = Candidate;
                    break;
                }
            }
            if (!ParentLcb)
                try_return(RC = STATUS_FILE_INVALID);
            RC = UDFBuildFullPathFromLcb(IrpContext, ParentLcb, &ParentPath);
            if (!NT_SUCCESS(RC)) try_return(RC);
        }
        PrefixBytes = ParentPath.Length + sizeof(WCHAR);
        TotalBytes = PrefixBytes + NewName.Length;
        if (TotalBytes > MAXUSHORT - sizeof(WCHAR))
            try_return(RC = STATUS_NAME_TOO_LONG);
        TargetPath.Buffer = ExAllocatePoolWithTag(PagedPool, TotalBytes + sizeof(WCHAR), TAG_FILE_NAME);
        if (!TargetPath.Buffer)
            try_return(RC = STATUS_INSUFFICIENT_RESOURCES);
        if (ParentPath.Length)
            RtlCopyMemory(TargetPath.Buffer, ParentPath.Buffer, ParentPath.Length);
        TargetPath.Buffer[ParentPath.Length / sizeof(WCHAR)] = L'\\';
        RtlCopyMemory((PUCHAR)TargetPath.Buffer + PrefixBytes, NewName.Buffer, NewName.Length);
        TargetPath.Buffer[TotalBytes / sizeof(WCHAR)] = 0;
        TargetPath.Length = (USHORT)TotalBytes;
        TargetPath.MaximumLength = (USHORT)(TotalBytes + sizeof(WCHAR));
        TargetNameOffset = (USHORT)PrefixBytes;

        if (Replace) {
            DIR_ENUM_CONTEXT Context;
            NTSTATUS FindStatus = UDFFindDirEntry(Vcb, Dir2, &NewName, IgnoreCase, TRUE, &Context);
            if (NT_SUCCESS(FindStatus) && Context.DirNdx && Context.DirNdx->FileInfo &&
                Context.DirNdx->FileInfo->Dloc != File1->Dloc) {
                TargetFcb = Context.DirNdx->FileInfo->Fcb;
                if (TargetFcb && TargetFcb != Fcb1 && TargetFcb != Dir1->Fcb && TargetFcb != Dir2->Fcb) {
                    if (!UDFAcquireFcbExclusive(IrpContext, TargetFcb, TRUE))
                        try_return(RC = STATUS_SHARING_VIOLATION);
                    AcquiredTarget = TRUE;
                }
                for (Entry = Dir2->Fcb->ChildLcbQueue.Flink;
                     Entry != &Dir2->Fcb->ChildLcbQueue; Entry = Entry->Flink) {
                    PLCB Candidate = CONTAINING_RECORD(Entry, struct LCB, ParentFcbLinks);
                    if (Candidate->ChildFcb == TargetFcb && Candidate->LinkFileInfo &&
                        Candidate->LinkFileInfo->Index == Context.Index &&
                        Candidate->ParentFileInfo && Candidate->ParentFileInfo->Dloc == Dir2->Dloc &&
                        !FlagOn(Candidate->Flags, UDF_LCB_FLAG_LINK_DELETED)) {
                        if (Candidate->Reference)
                            try_return(RC = STATUS_SHARING_VIOLATION);
                        StaleLcb = Candidate;
                        break;
                    }
                }
            }
        }
        RC = UDFHardLinkFile__(IrpContext, Vcb, IgnoreCase, &Replace, &NewName,
                                Dir1, Dir2, File1, &TargetRemoved, &LinkCreated);
        if (!NT_SUCCESS(RC) || !LinkCreated) try_return(RC);
        if (DirObject2 && FlagOn(Vcb->CompatFlags, UDF_VCB_IC_UPDATE_DIR_WRITE)) {
            DirObject2->Flags |= FO_FILE_MODIFIED;
            if (!Replace) DirObject2->Flags |= FO_FILE_SIZE_CHANGED;
        }
        UDFNotifyReportChange(IrpContext, Vcb, Fcb1,
            FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_LAST_ACCESS,
            FILE_ACTION_MODIFIED, Ccb1->Lcb, FileObject1);
        FsRtlNotifyFullReportChange(Vcb->NotifySync, &Vcb->NextNotifyIRP,
            (PSTRING)&TargetPath, TargetNameOffset, NULL, NULL,
            Replace ? (FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_SIZE |
                FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_LAST_ACCESS |
                FILE_NOTIFY_CHANGE_CREATION | FILE_NOTIFY_CHANGE_EA) : FILE_NOTIFY_CHANGE_FILE_NAME,
            Replace ? FILE_ACTION_MODIFIED : FILE_ACTION_ADDED, NULL);
try_exit: NOTHING;
    } _SEH2_FINALLY {
        BOOLEAN Abnormal = _SEH2_AbnormalTermination();
        _SEH2_VOLATILE BOOLEAN TargetFcbRemoved = FALSE;
        if (Abnormal && (TargetRemoved || LinkCreated || Vcb->PendingNamespaceCount)) {
            Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
            InterlockedCompareExchange(&Vcb->MetadataWritebackError, STATUS_UNSUCCESSFUL, STATUS_SUCCESS);
        }
        _SEH2_TRY {
            if (TargetRemoved && StaleLcb) {
                PFCB Parent = StaleLcb->ParentFcb;
                PUDF_FILE_INFO ParentInfo = StaleLcb->ParentFileInfo;
                NTSTATUS CloseStatus = STATUS_SUCCESS;
                BOOLEAN Consumed = !ParentInfo;
                UDFRemovePrefix(IrpContext, StaleLcb);
                StaleLcb = NULL;
                if (ParentInfo) {
                    CloseStatus = UDFDereferenceFileChecked(Vcb, ParentInfo);
                    Consumed = NT_SUCCESS(CloseStatus);
                }
                _SEH2_TRY {
                    if (Consumed && ParentInfo && !Abnormal && NT_SUCCESS(RC))
                        CloseStatus = UDFCompleteFileClose(IrpContext, Vcb, ParentInfo);
                } _SEH2_FINALLY {
                    if (Consumed && Parent) {
                        ASSERT(Parent->FcbReference);
                        InterlockedDecrement((PLONG)&Parent->FcbReference);
                    }
                } _SEH2_END;
                UDFRememberMetadataWritebackError(IrpContext, Vcb, ParentInfo, CloseStatus);
                if (!Abnormal && NT_SUCCESS(RC)) RC = CloseStatus;
            }
            if (TargetRemoved && AcquiredTarget && !Abnormal && NT_SUCCESS(RC))
                UDFTeardownStructures(IrpContext, TargetFcb, FALSE, &TargetFcbRemoved);
        } _SEH2_FINALLY {
            if (AcquiredTarget && !TargetFcbRemoved) UDFReleaseFcb(IrpContext, TargetFcb);
            if (AcquiredFcb1) UDFReleaseFcb(IrpContext, Fcb1);
            if (AcquiredDir2) UDFReleaseFcb(IrpContext, Dir2->Fcb);
            if (AcquiredDir1) UDFReleaseFcb(IrpContext, Dir1->Fcb);
            if (ParentPath.Buffer) ExFreePool(ParentPath.Buffer);
            if (TargetPath.Buffer) ExFreePool(TargetPath.Buffer);
        } _SEH2_END;
    } _SEH2_END;
    return RC;
} // end UDFHardLink()
#endif //UDF_ALLOW_HARD_LINKS
