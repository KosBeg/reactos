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
   for Directory tree & related structures support

*/

#include "udf.h"

#ifdef UDF_CHECK_UTIL
  #include "..\namesup.h"
#else
  #ifdef UDF_BUG_CHECK_ID
    #undef UDF_BUG_CHECK_ID
  #endif //UDF_BUG_CHECK_ID
#endif //UDF_CHECK_UTIL

#define         UDF_BUG_CHECK_ID                UDF_FILE_UDF_INFO_DIR

#define         MEM_USDIRHASH_TAG               "USDirHash"

#define UDF_DUMP_DIRTREE
#ifdef UDF_DUMP_DIRTREE
#define DirPrint(x)  UDFPrint(x)
#else
#define DirPrint(x)  {;}
#endif

/*
    This routine initializes DirIndex array
 */
PDIR_INDEX_HDR
UDFDirIndexAlloc(
    IN uint_di i
    )
{
    uint_di j,k;
    PDIR_INDEX_HDR hDirNdx;
    PDIR_INDEX_ITEM* FrameList;

    if (!i)
        return NULL;

    j = i >> UDF_DIR_INDEX_FRAME_SH;
    i &= (UDF_DIR_INDEX_FRAME-1);

    hDirNdx = (PDIR_INDEX_HDR)MyAllocatePoolTag__(UDF_DIR_INDEX_MT, sizeof(DIR_INDEX_HDR)+(j+(i!=0))*sizeof(PDIR_INDEX_ITEM), MEM_DIR_HDR_TAG);
    if (!hDirNdx) return NULL;
    RtlZeroMemory(hDirNdx, sizeof(DIR_INDEX_HDR));

    FrameList = (PDIR_INDEX_ITEM*)(hDirNdx+1);
    for(k=0; k<j; k++, FrameList++) {
        (*FrameList) = (PDIR_INDEX_ITEM)MyAllocatePoolTag__(UDF_DIR_INDEX_MT, UDF_DIR_INDEX_FRAME*sizeof(DIR_INDEX_ITEM), MEM_DIR_NDX_TAG);
        if (!(*FrameList)) {
free_hdi:
            // item pointet by FrameList is NULL, it could not be allocated
            while(k) {
                k--;
                FrameList--;
                MyFreePool__(*FrameList);
            }
            MyFreePool__(hDirNdx);
            return NULL;
        }
        RtlZeroMemory((*FrameList), UDF_DIR_INDEX_FRAME*sizeof(DIR_INDEX_ITEM));
    }
    if (i) {
        (*FrameList) = (PDIR_INDEX_ITEM)MyAllocatePoolTag__(UDF_DIR_INDEX_MT, AlignDirIndex(i)*sizeof(DIR_INDEX_ITEM), MEM_DIR_NDX_TAG);
        if (!(*FrameList))
            goto free_hdi;
        RtlZeroMemory((*FrameList), i*sizeof(DIR_INDEX_ITEM));
    }

    hDirNdx->FrameCount = j+(i!=0);
    hDirNdx->LastFrameCount = i ? i : UDF_DIR_INDEX_FRAME;

    return hDirNdx;
} // UDFDirIndexAlloc()

/*
    This routine releases DirIndex array
 */
void
UDFDirIndexFree(
    PDIR_INDEX_HDR hDirNdx
    )
{
    uint32 k;
    PDIR_INDEX_ITEM* FrameList;

    if (!hDirNdx) return;
    FrameList = (PDIR_INDEX_ITEM*)(hDirNdx+1);
    for(k=0; k<hDirNdx->FrameCount; k++, FrameList++) {
        if (*FrameList) MyFreePool__(*FrameList);
    }
    MyFreePool__(hDirNdx);
} // UDFDirIndexFree();

/*
    This routine grows DirIndex array
 */
NTSTATUS
UDFDirIndexGrow(
    IN PDIR_INDEX_HDR* Index,
    IN uint_di Increment
    )
{
    PDIR_INDEX_HDR Old;
    _SEH2_VOLATILE PDIR_INDEX_HDR New = NULL;
    PDIR_INDEX_HDR RetiredHeader = NULL;
    PDIR_INDEX_ITEM *OldFrames, *Frames;
    PDIR_INDEX_ITEM OldLast, Last;
    _SEH2_VOLATILE PDIR_INDEX_ITEM Replacement = NULL, Extra = NULL;
    uint_di Count, LastCount, Needed, NewLast;
    uint64 Total;
    NTSTATUS Status = STATUS_INSUFFICIENT_RESOURCES;
    if (!Index || !(Old = *Index) || !Old->FrameCount ||
        !Old->LastFrameCount || Old->LastFrameCount > UDF_DIR_INDEX_FRAME ||
        Increment > UDF_DIR_INDEX_FRAME)
        return STATUS_INVALID_PARAMETER;
    if (!Increment)
        return STATUS_SUCCESS;
    Total = ((uint64)Old->FrameCount - 1) * UDF_DIR_INDEX_FRAME +
        Old->LastFrameCount + Increment;
    if (Total > MAXULONG)
        return STATUS_INVALID_PARAMETER;
    Count = Old->FrameCount;
    LastCount = Old->LastFrameCount;
    Needed = LastCount + Increment;
    NewLast = Needed;
    OldFrames = (PDIR_INDEX_ITEM*)(Old + 1);
    OldLast = OldFrames[Count - 1];
    if (!OldLast)
        return STATUS_FILE_CORRUPT_ERROR;
    /* Caller serializes the directory. Allocate every replacement first:
     * an OOM must leave the caller's header and all frame pointers valid. */
    _SEH2_TRY {
        if (Needed > UDF_DIR_INDEX_FRAME) {
            SIZE_T HeaderBytes;
            if ((SIZE_T)Count > (((SIZE_T)-1) - sizeof(*Old)) / sizeof(*Frames) - 1) {
                Status = STATUS_INVALID_PARAMETER;
                _SEH2_LEAVE;
            }
            HeaderBytes = sizeof(*Old) + ((SIZE_T)Count + 1) * sizeof(*Frames);
            New = MyAllocatePoolTag__(UDF_DIR_INDEX_MT, HeaderBytes, MEM_DIR_HDR_TAG);
            if (!New) _SEH2_LEAVE;
            RtlCopyMemory(New, Old, sizeof(*Old) + (SIZE_T)Count * sizeof(*Frames));
            Frames = (PDIR_INDEX_ITEM*)(New + 1);
            NewLast = Needed - UDF_DIR_INDEX_FRAME;
            Extra = MyAllocatePoolTag__(UDF_DIR_INDEX_MT,
                AlignDirIndex(NewLast) * sizeof(*Extra), MEM_DIR_NDX_TAG);
            if (!Extra) _SEH2_LEAVE;
            RtlZeroMemory(Extra, NewLast * sizeof(*Extra));
            Needed = UDF_DIR_INDEX_FRAME;
        } else {
            Frames = OldFrames;
        }
        if (AlignDirIndex(Needed) > AlignDirIndex(LastCount)) {
            Replacement = MyAllocatePoolTag__(UDF_DIR_INDEX_MT,
                AlignDirIndex(Needed) * sizeof(*Replacement), MEM_DIR_NDX_TAG);
            if (!Replacement) _SEH2_LEAVE;
            RtlCopyMemory(Replacement, OldLast, LastCount * sizeof(*Replacement));
        }
        Last = Replacement ? Replacement : OldLast;
        RtlZeroMemory(Last + LastCount, (Needed - LastCount) * sizeof(*Last));
        if (New) {
            Frames[Count - 1] = Last;
            Frames[Count] = Extra;
            New->FrameCount = Count + 1;
            New->LastFrameCount = NewLast;
            *Index = New;
            New = NULL;
            Extra = NULL;
            RetiredHeader = Old;
        } else {
            Frames[Count - 1] = Last;
            Old->LastFrameCount = Needed;
        }
        if (Replacement) {
            Replacement = NULL; /* frame transferred into the current header */
            MyFreePool__(OldLast);
        }
        if (RetiredHeader)
            MyFreePool__(RetiredHeader);
        Status = STATUS_SUCCESS;
    } _SEH2_FINALLY {
        if (Replacement) MyFreePool__(Replacement);
        if (Extra) MyFreePool__(Extra);
        if (New) MyFreePool__(New); /* its borrowed old frames are not ours */
    } _SEH2_END;
    return Status;
} // end UDFDirIndexGrow()

/*
    Thisd routine truncates DirIndex array
 */
NTSTATUS
UDFDirIndexTrunc(
    IN PDIR_INDEX_HDR* Index,
    IN uint_di Decrement
    )
{
    PDIR_INDEX_HDR Header;
    PDIR_INDEX_ITEM *Frames, Entry;
    uint64 Total, NewTotal;
    uint_di NewFrames, NewLast, Frame, Slot, Limit;
    if (!Index || !(Header = *Index) || !Header->FrameCount ||
        !Header->LastFrameCount || Header->LastFrameCount > UDF_DIR_INDEX_FRAME)
        return STATUS_INVALID_PARAMETER;
    Total = ((uint64)Header->FrameCount - 1) * UDF_DIR_INDEX_FRAME + Header->LastFrameCount;
    if (Total > MAXULONG || Total < 2 || Decrement > Total - 2)
        return STATUS_INVALID_PARAMETER;
    if (!Decrement)
        return STATUS_SUCCESS;
    NewTotal = Total - Decrement;
    NewFrames = (uint_di)((NewTotal + UDF_DIR_INDEX_FRAME - 1) >> UDF_DIR_INDEX_FRAME_SH);
    NewLast = (uint_di)((NewTotal - 1) & (UDF_DIR_INDEX_FRAME - 1)) + 1;
    Frames = (PDIR_INDEX_ITEM*)(Header + 1);
    /* The caller must first release or transfer the removed entry owners.
     * Retain capacity; shrinking never needs a fallible realloc after it
     * has already removed entries or freed another frame. */
    for (Frame = (uint_di)(NewTotal >> UDF_DIR_INDEX_FRAME_SH); Frame < Header->FrameCount; Frame++) {
        Slot = Frame == (uint_di)(NewTotal >> UDF_DIR_INDEX_FRAME_SH) ?
            (uint_di)(NewTotal & (UDF_DIR_INDEX_FRAME - 1)) : 0;
        Limit = Frame + 1 == Header->FrameCount ? Header->LastFrameCount : UDF_DIR_INDEX_FRAME;
        for (; Slot < Limit; Slot++) {
            Entry = &Frames[Frame][Slot];
            if (Entry->FileInfo || Entry->FName.Buffer)
                return STATUS_CANNOT_DELETE;
        }
    }
    for (Frame = NewFrames; Frame < Header->FrameCount; Frame++) {
        MyFreePool__(Frames[Frame]);
        Frames[Frame] = NULL;
    }
    Header->FrameCount = NewFrames;
    Header->LastFrameCount = NewLast;
    return STATUS_SUCCESS;
} // end UDFDirIndexTrunc()

/*
    This routine returns pointer to DirIndex item with index i.
 */
PDIR_INDEX_ITEM
UDFDirIndex(
    IN PDIR_INDEX_HDR hDirNdx,
    IN uint_di i
    )
{
    uint_di j, k;
    if ( hDirNdx &&
        ((j = (i >> UDF_DIR_INDEX_FRAME_SH)) < (k = hDirNdx->FrameCount) ) &&
        ((i = (i & (UDF_DIR_INDEX_FRAME-1))) < ((j < (k-1)) ? UDF_DIR_INDEX_FRAME : hDirNdx->LastFrameCount)) )
        return &( (((PDIR_INDEX_ITEM*)(hDirNdx+1))[j])[i] );
    return NULL;
}

/*
    This routine returns pointer to DirIndex'es frame & index inside it
    according to start Index parameter. It also initializes scan parameters
 */
PDIR_INDEX_ITEM
UDFDirIndexGetFrame(
    IN PDIR_INDEX_HDR hDirNdx,
    IN uint32 Frame,
   OUT uint32* FrameLen,
   OUT uint_di* Index,
    IN uint_di Rel
    )
{
    if (Frame >= hDirNdx->FrameCount)
        return NULL;
    if (Index) {
        (*Index) = Frame*UDF_DIR_INDEX_FRAME+Rel;
//    if (FrameLen)
        (*FrameLen) = (Frame < (hDirNdx->FrameCount-1)) ? UDF_DIR_INDEX_FRAME :
                                                          hDirNdx->LastFrameCount;
    }
    return ((PDIR_INDEX_ITEM*)(hDirNdx+1))[Frame]+Rel;
} // end UDFDirIndexGetFrame()

/*
    This routine initializes indexes for optimized DirIndex scan
    according to start Index parameter
 */

BOOLEAN
UDFDirIndexInitScan(
    IN PUDF_FILE_INFO DirInfo,   //
   OUT PUDF_DIR_SCAN_CONTEXT Context,
    IN uint_di Index
    )
{
    Context->DirInfo = DirInfo;
    Context->hDirNdx = DirInfo->Dloc->DirIndex;
    if ( (Context->frame = (Index >> UDF_DIR_INDEX_FRAME_SH)) >=
                                                 Context->hDirNdx->FrameCount) {
        return FALSE;
    }
    if ( (Context->j = Index & (UDF_DIR_INDEX_FRAME-1)) >=
               ((Context->frame < (Context->hDirNdx->FrameCount-1))
                                    ?
                                 UDF_DIR_INDEX_FRAME : Context->hDirNdx->LastFrameCount) ) {
        return FALSE;
    }
    Context->DirNdx = UDFDirIndexGetFrame(Context->hDirNdx,
                                          Context->frame,
                                          &(Context->d),
                                          &(Context->i),
                                          Context->j);
    Context->i--;
    Context->j--;
    Context->DirNdx--;

    return TRUE;
} // end UDFDirIndexInitScan()

PDIR_INDEX_ITEM
UDFDirIndexScan(
    PUDF_DIR_SCAN_CONTEXT Context,
    PUDF_FILE_INFO* _FileInfo
    )
{
    PUDF_FILE_INFO FileInfo;
    PUDF_FILE_INFO ParFileInfo;

    Context->i++;
    Context->j++;
    Context->DirNdx++;

    if (Context->j >= Context->d) {
        Context->j=0;
        Context->frame++;
        Context->DirNdx = UDFDirIndexGetFrame(Context->hDirNdx,
                                              Context->frame,
                                              &(Context->d),
                                              &(Context->i),
                                              Context->j);
    }
    if (!Context->DirNdx) {
        if (_FileInfo)
            (*_FileInfo) = NULL;
        return NULL;
    }

    if (_FileInfo) {
        if ((FileInfo = Context->DirNdx->FileInfo)) {
            if (FileInfo->ParentFile != Context->DirInfo) {
                ParFileInfo = UDFLocateParallelFI(Context->DirInfo,
                                                  Context->i,
                                                  FileInfo);
#ifdef UDF_DBG
                if (ParFileInfo->ParentFile != Context->DirInfo) {
                    BrutePoint();
                }
#endif // UDF_DBG
                FileInfo = ParFileInfo;
            }
        }
        (*_FileInfo) = FileInfo;
    }

    return (Context->DirNdx);
} // end UDFDirIndexScan()

/*
    This routine calculates hashes for directory search
 */
uint8
UDFBuildHashEntry(
    IN PVCB Vcb,
    IN PUNICODE_STRING Name,
   OUT PHASH_ENTRY hashes,
    IN uint8 Mask
    )
{
    UNICODE_STRING UName;
    WCHAR ShortNameBuffer[13];
    uint8 RetFlags = 0;

    // An empty name has no hashes.  In particular, do not pass zero bytes to
    // FsRtlAllocatePoolWithTag and then free its result below.
    if (!hashes)
        return 0;
    if (!Name || !Name->Buffer || !Name->Length) {
        RtlZeroMemory(hashes, sizeof(*hashes));
        return 0;
    }

    if (Mask & HASH_POSIX)
        hashes->hPosix = crc32((uint8*)(Name->Buffer), Name->Length);

    if (Mask & HASH_ULFN) {
        _SEH2_VOLATILE PWCHAR Buffer = NULL;
        NTSTATUS Status;
        _SEH2_TRY {
            UName.Length = 0;
            UName.MaximumLength = Name->Length;
            Buffer = (PWCHAR)FsRtlAllocatePoolWithTag(NonPagedPool,
                                                     UName.MaximumLength, TAG_FILE_NAME);
            if (!Buffer) ExRaiseStatus(STATUS_INSUFFICIENT_RESOURCES);
            UName.Buffer = Buffer;
            Status = RtlUpcaseUnicodeString(&UName, Name, FALSE);
            if (!NT_SUCCESS(Status)) ExRaiseStatus(Status);
            hashes->hLfn = crc32((uint8*)(UName.Buffer), UName.Length);
        } _SEH2_FINALLY {
            if (Buffer) ExFreePoolWithTag(Buffer, TAG_FILE_NAME);
        } _SEH2_END;
    }

    if (Mask & HASH_DOS) {
        UName.Buffer = (PWCHAR)(&ShortNameBuffer);
        UName.MaximumLength = 13*sizeof(WCHAR);
        UDFDOSName(Vcb, &UName, Name, (Mask & HASH_KEEP_NAME) ? TRUE : FALSE);
        if (!RtlCompareUnicodeString(Name, &UName, TRUE)) {
            RetFlags |= UDF_FI_FLAG_DOS;
        }
        hashes->hDos = crc32((uint8*)(UName.Buffer), UName.Length);
    }
    return RetFlags;
} // UDFBuildHashEntry()

#ifdef UDF_CHECK_UTIL
uint32
UDFFindNextFI(
    IN int8* buff,
    IN uint32 prevOffset,
    IN uint32 Length
    )
{
    PFILE_IDENT_DESC FileId;
    while(prevOffset+sizeof(FILE_IDENT_DESC) < Length) {
        prevOffset++;
        FileId = (PFILE_IDENT_DESC)(buff+prevOffset);
        if (FileId->descTag.tagIdent != TID_FILE_IDENT_DESC)
            continue;
        if (FileId->descTag.descVersion != 2 && FileId->descTag.descVersion != 3)
            continue;
        if (FileId->fileVersionNum != 1)
            continue;
        if (FileId->fileCharacteristics & (~0x1f))
            continue;
        if (prevOffset + ((FileId->lengthFileIdent + FileId->lengthOfImpUse + sizeof(FILE_IDENT_DESC) + 3) & (~((uint32)3))) <= Length) {
            UDFPrint(("UDFFindNextFI OK: %x\n", prevOffset));
            return prevOffset;
        }
    }
    return 0;
} // end UDFFindNextFI()
#else //UDF_CHECK_UTIL
#define UDFFindNextFI(a,b,c)  0
#endif //UDF_CHECK_UTIL

/* A directory index is also used while mounting an unclean volume.  Never
 * construct a partial namespace by skipping a malformed FID: recovery must
 * see the complete stream or reject it.  Check lengths before reading the
 * variable part so a corrupt record cannot move the scan beyond the extent. */
static NTSTATUS
UDFValidateDirectoryFid(
    IN PVCB Vcb,
    IN PEXTENT_INFO ExtInfo,
    IN const int8* Buffer,
    IN uint32 Offset,
    IN uint32 Length,
    OUT uint32* RecordLength
    )
{
    const FILE_IDENT_DESC* FileId;
    const uint8* Bytes;
    uint32 RawLength, AlignedLength, PhysicalLba, ExpectedLocation, i;
    uint32 RefPartNum;
    uint8 Checksum = 0;

    if (Offset > Length || Length - Offset < sizeof(FILE_IDENT_DESC))
        return STATUS_FILE_CORRUPT_ERROR;
    FileId = (const FILE_IDENT_DESC*)(Buffer + Offset);
    Bytes = (const uint8*)FileId;
    RawLength = sizeof(FILE_IDENT_DESC) + FileId->lengthOfImpUse +
                FileId->lengthFileIdent;
    AlignedLength = (RawLength + 3) & ~((uint32)3);
    if (AlignedLength > Length - Offset ||
        FileId->descTag.tagIdent != TID_FILE_IDENT_DESC ||
        (FileId->descTag.descVersion != 2 &&
         FileId->descTag.descVersion != 3) ||
        FileId->fileVersionNum != 1 ||
        (FileId->fileCharacteristics & ~((uint8)0x1f)) ||
        FileId->descTag.descCRCLength < RawLength - sizeof(tag) ||
        FileId->descTag.descCRCLength > AlignedLength - sizeof(tag))
        return STATUS_FILE_CORRUPT_ERROR;

    for (i = 0; i < sizeof(tag); i++) {
        if (i != 4)
            Checksum += Bytes[i];
    }
    if (Checksum != FileId->descTag.tagChecksum ||
        UDFCrc((uint8*)Bytes + sizeof(tag),
               FileId->descTag.descCRCLength, 0) != FileId->descTag.descCRC)
        return STATUS_CRC_ERROR;

    PhysicalLba = UDFExtentOffsetToLba(Vcb, ExtInfo->Mapping, Offset,
                                       NULL, NULL, NULL, NULL);
    if (PhysicalLba == LBA_OUT_OF_EXTENT)
        return STATUS_FILE_CORRUPT_ERROR;
    RefPartNum = UDFGetRefPartNumByPhysLba(Vcb, PhysicalLba);
    if (RefPartNum >= Vcb->PartitionMaps)
        return STATUS_FILE_CORRUPT_ERROR;
    ExpectedLocation = UDFPhysLbaToPart(Vcb, RefPartNum, PhysicalLba);
    if (ExpectedLocation == LBA_OUT_OF_EXTENT ||
        FileId->descTag.tagLocation != ExpectedLocation)
        return STATUS_FILE_CORRUPT_ERROR;

    /* Deleted names can use their erased-name representation. Every live
     * component must be a bounded CS0 string; a parent FID has no name. */
    if (!(FileId->fileCharacteristics & FILE_DELETED)) {
        const uint8* Name = Bytes + sizeof(*FileId) + FileId->lengthOfImpUse;
        if (FileId->fileCharacteristics & FILE_PARENT) {
            if (FileId->lengthFileIdent) return STATUS_FILE_CORRUPT_ERROR;
        } else if (FileId->lengthFileIdent < 2 ||
            (Name[0] != UDF_COMP_ID_8 && Name[0] != UDF_COMP_ID_16) ||
            (Name[0] == UDF_COMP_ID_16 && !(FileId->lengthFileIdent & 1))) {
            return STATUS_FILE_CORRUPT_ERROR;
        }
    }
    *RecordLength = AlignedLength;
    return STATUS_SUCCESS;
}

/* Only an unpublished index owns these names outright. The generic frame
 * destructor is also used after callers already retired their names, so do
 * not change its contract or free borrowed dot/parent names there. */
static VOID
UDFDiscardDirectoryIndex(PDIR_INDEX_HDR Index)
{
    uint_di i;
    PDIR_INDEX_ITEM Entry;
    if (!Index) return;
    for (i = 0; (Entry = UDFDirIndex(Index, i)); i++) {
        if (!(Entry->FI_Flags & UDF_FI_FLAG_KEEP_NAME) && Entry->FName.Buffer)
            MyFreePool__(Entry->FName.Buffer);
    }
    UDFDirIndexFree(Index);
}

typedef struct _UDF_DIRECTORY_INDEX_BUILD {
    int8* Buffer;
    PDIR_INDEX_HDR Index;
} UDF_DIRECTORY_INDEX_BUILD;

/* Both allocations are published to a volatile, private stack owner before
 * subsequent work can raise. No partially populated index escapes to Dloc. */
static NTSTATUS
UDFIndexDirectoryInternal(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN OUT PUDF_FILE_INFO FileInfo,
    IN OUT volatile UDF_DIRECTORY_INDEX_BUILD* Build
    )
{
    PDIR_INDEX_HDR hDirNdx;
    PDIR_INDEX_ITEM DirNdx;
    PFILE_IDENT_DESC FileId;
    uint32 Offset = 0;
    uint32 RecordLength;
//    uint32 prevOffset = 0;
    uint32 DelCount = 0;
    uint_di Count = 0;
    NTSTATUS status;
    int8* buff;
    PEXTENT_INFO ExtInfo;  // Extent array for directory
    uint16 PartNum;
    uint16 valueCRC;

    if (!FileInfo) return STATUS_INVALID_PARAMETER;
    ValidateFileInfo(FileInfo);

    ExtInfo = &(FileInfo->Dloc->DataLoc);
    UDFPrint(("UDF: scaning directory\n"));
    // allocate buffer for the whole directory
    if (ExtInfo->Length <= 0 || ExtInfo->Length > 0xffffffffu) {
        return STATUS_FILE_CORRUPT_ERROR;
    }
    ASSERT((uint32)(ExtInfo->Length));

    buff = (int8*)DbgAllocatePool(PagedPool, (uint32)(ExtInfo->Length));
    Build->Buffer = buff;
    if (!buff)
        return STATUS_INSUFFICIENT_RESOURCES;

    ExtInfo->Flags |= EXTENT_FLAG_ALLOC_SEQUENTIAL;

    // read FileIdents
    status = UDFReadFile__(IrpContext, Vcb, FileInfo, 0, (uint32)(ExtInfo->Length), FALSE, buff);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    // scan Dir to get entry counter
    FileId = (PFILE_IDENT_DESC)buff;
    DirPrint(("  ExtInfo->Length %x\n", ExtInfo->Length));
//    prevOffset = 0;
    while(Offset<ExtInfo->Length) {
        DirPrint(("  Offset %x\n", Offset));
        status = UDFValidateDirectoryFid(Vcb, ExtInfo, buff, Offset,
                                         (uint32)ExtInfo->Length,
                                         &RecordLength);
        if (!NT_SUCCESS(status)) {
            return status;
        }
        if (((ULONG)Offset & (Vcb->SectorSize-1)) > (Vcb->SectorSize-sizeof(FILE_IDENT_DESC))) {
            DirPrint(("  badly aligned\n", Offset));
        }
//        prevOffset = Offset;
        Offset += RecordLength;
        FileId = (PFILE_IDENT_DESC)((buff)+Offset);
        Count++;
        if (ExtInfo->Length - Offset < sizeof(FILE_IDENT_DESC)) {
            if (Offset != ExtInfo->Length) {
                return STATUS_FILE_CORRUPT_ERROR;
            }
            break;
        }
    }
    DirPrint(("  final Offset %x\n", Offset));
    if (Offset > ExtInfo->Length) {
        BrutePoint();
        UDFPrint(("  Unexpected end of Dir\n"));
        return STATUS_FILE_CORRUPT_ERROR;
    }
    // allocate buffer for directory index & zero it
    DirPrint(("  Count %x\n", Count));
    hDirNdx = UDFDirIndexAlloc(Count+1);
    Build->Index = hDirNdx;
    if (!hDirNdx) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    hDirNdx->DelCount = DelCount;

    Offset = Count = 0;
    hDirNdx->DIFlags |= (ExtInfo->Offset ? UDF_DI_FLAG_INIT_IN_ICB : 0);
    // add entry pointing to the directory itself
    DirNdx = UDFDirIndex(hDirNdx,0);
    ASSERT(FileInfo->Dloc->FELoc.Mapping[0].extLocation);
    DirNdx->FileEntryLoc.partitionReferenceNum = PartNum =
        (uint16)UDFGetRefPartNumByPhysLba(Vcb, FileInfo->Dloc->FELoc.Mapping[0].extLocation);
    ASSERT(PartNum != -1);
    DirNdx->FileEntryLoc.logicalBlockNum =
        UDFPhysLbaToPart(Vcb, PartNum, FileInfo->Dloc->FELoc.Mapping[0].extLocation);
    if (DirNdx->FileEntryLoc.logicalBlockNum == (ULONG)-1) {
        DirPrint(("  err: FileEntryLoc=-1\n"));
        return STATUS_FILE_CORRUPT_ERROR;
    }
    DirNdx->FileCharacteristics = (FileInfo->FileIdent) ?
                         FileInfo->FileIdent->fileCharacteristics :
                         FILE_DIRECTORY;
//    DirNdx->Offset = 0;
//    DirNdx->Length = 0;
    DirNdx->FI_Flags |= UDF_FI_FLAG_KEEP_NAME;
    RtlInitUnicodeString(&DirNdx->FName, L".");
    DirNdx->FileInfo = FileInfo;
    DirNdx->FI_Flags |= UDFBuildHashEntry(Vcb, &(DirNdx->FName), &(DirNdx->hashes),
        HASH_ALL | HASH_KEEP_NAME);
    Count++;
    FileId = (PFILE_IDENT_DESC)buff;
    status = STATUS_SUCCESS;
//    prevOffset = 0;
    while(Offset<ExtInfo->Length) {
        // add new entry to index list
        status = UDFValidateDirectoryFid(Vcb, ExtInfo, buff, Offset,
                                         (uint32)ExtInfo->Length,
                                         &RecordLength);
        if (!NT_SUCCESS(status)) {
            return status;
        }
        DirNdx = UDFDirIndex(hDirNdx,Count);
        // allocate buffer & fill it with decompressed unicode filename
        if (FileId->fileCharacteristics & FILE_DELETED) {
            DirPrint(("  FILE_DELETED\n"));
            hDirNdx->DelCount++;
        }
        DirPrint(("  FileId: offs %x, filen %x, iulen %x\n", Offset, FileId->lengthFileIdent, FileId->lengthOfImpUse));
        DirNdx->Length = RecordLength;
        DirPrint(("  DirNdx: Length %x, Charact %x\n", DirNdx->Length, FileId->fileCharacteristics));
        if (FileId->fileCharacteristics & FILE_PARENT) {
            DirPrint(("  parent\n"));
            uint32 ParentLba = UDFPartLbaToPhys(Vcb, &FileId->icb.extLocation);
            /* ParentFile owns this FI's namespace/open context. It is a
             * weak '..' cache only when it describes the actual FID target.
             * A parentless open-by-ID of a non-root is not its own parent. */
            DirNdx->FI_Flags |= UDF_FI_FLAG_KEEP_NAME;
            RtlInitUnicodeString(&DirNdx->FName, L"..");
            DirNdx->FileInfo = NULL;
            if (ParentLba != LBA_OUT_OF_EXTENT) {
                if (FileInfo->ParentFile && FileInfo->ParentFile->Dloc &&
                    FileInfo->ParentFile->Dloc->FELoc.Mapping &&
                    FileInfo->ParentFile->Dloc->FELoc.Mapping[0].extLocation == ParentLba)
                    DirNdx->FileInfo = FileInfo->ParentFile;
                else if (FileInfo->Dloc->FELoc.Mapping &&
                         FileInfo->Dloc->FELoc.Mapping[0].extLocation == ParentLba)
                    DirNdx->FileInfo = FileInfo; /* real root/self FID only */
            }
            DirNdx->FI_Flags |= UDFBuildHashEntry(Vcb, &(DirNdx->FName), &(DirNdx->hashes), HASH_ALL | HASH_KEEP_NAME);
        } else {

            // init plain file/dir entry
            ASSERT( (Offset+sizeof(FILE_IDENT_DESC)+FileId->lengthOfImpUse+FileId->lengthFileIdent) <=
                    ExtInfo->Length );

            UDFDecompressUnicode(&DirNdx->FName,
                             ((uint8*)(FileId+1)) + (FileId->lengthOfImpUse),
                             FileId->lengthFileIdent,
                             &valueCRC);

            if (!DirNdx->FName.Buffer &&
                !(FileId->fileCharacteristics & FILE_DELETED))
                return STATUS_INSUFFICIENT_RESOURCES;
            UDFNormalizeFileName(&(DirNdx->FName), valueCRC);

            DirNdx->FI_Flags |= UDFBuildHashEntry(Vcb, &(DirNdx->FName), &(DirNdx->hashes), HASH_ALL);
        }
        if ((FileId->fileCharacteristics & FILE_METADATA)
                       ||
              !DirNdx->FName.Buffer
                       ||
           ((DirNdx->FName.Length >= sizeof(UDF_RESERVED_NAME_HDR)-sizeof(WCHAR)) &&
            (RtlCompareMemory(DirNdx->FName.Buffer, UDF_RESERVED_NAME_HDR, sizeof(UDF_RESERVED_NAME_HDR)-sizeof(WCHAR)) == sizeof(UDF_RESERVED_NAME_HDR)-sizeof(WCHAR)) )) {
            DirPrint(("  metadata\n"));
            DirNdx->FI_Flags |= UDF_FI_FLAG_FI_INTERNAL;
        }
#if 0
        UDFPrint(("%ws\n", DirNdx->FName.Buffer));
#endif
        DirPrint(("%wZ\n", &DirNdx->FName));
        // remember FileEntry location...
        DirNdx->FileEntryLoc = FileId->icb.extLocation;
        // ... and some file characteristics
        DirNdx->FileCharacteristics = FileId->fileCharacteristics;
        DirNdx->Offset = Offset;
#ifdef UDF_CHECK_DISK_ALLOCATION
        if (!(FileId->fileCharacteristics & FILE_DELETED) &&
            (UDFPartLbaToPhys(Vcb, &(DirNdx->FileEntryLoc)) != LBA_OUT_OF_EXTENT) &&
             (Vcb->BitmapFcb ? UDFIsBitmapBitFree(Vcb, UDFPartLbaToPhys(Vcb, &(DirNdx->FileEntryLoc)) - Vcb->Partitions[0].PartitionRoot) : UDFGetFreeBit(((uint32*)(Vcb->FSBM_Bitmap)), UDFPartLbaToPhys(Vcb, &(DirNdx->FileEntryLoc)) - Vcb->Partitions[0].PartitionRoot))) {

            AdPrint(("Ref to Discarded block %x\n",UDFPartLbaToPhys(Vcb, &(DirNdx->FileEntryLoc)) ));
            BrutePoint();
            FileId->fileCharacteristics |= FILE_DELETED;
        } else
        if (UDFPartLbaToPhys(Vcb, &(DirNdx->FileEntryLoc)) == LBA_OUT_OF_EXTENT) {
            AdPrint(("Ref to Invalid block %x\n", UDFPartLbaToPhys(Vcb, &(DirNdx->FileEntryLoc)) ));
            BrutePoint();
            FileId->fileCharacteristics |= FILE_DELETED;
        }
#endif // UDF_CHECK_DISK_ALLOCATION
//        prevOffset = Offset;
        Offset += DirNdx->Length;
        FileId = (PFILE_IDENT_DESC)(((int8*)FileId)+DirNdx->Length);
        Count++;
        if (ExtInfo->Length - Offset < sizeof(FILE_IDENT_DESC)) {
            if (Offset != ExtInfo->Length) {
                return STATUS_FILE_CORRUPT_ERROR;
            }
            break;
        }
    } // while()
    // we needn't writing terminator 'cause the buffer is already zero-filled
    if (Count < 2) {
        UDFPrint(("  Directory too short\n"));
        return STATUS_FILE_CORRUPT_ERROR;
    }
    // store index
    FileInfo->Dloc->DirIndex = hDirNdx;
    Build->Index = NULL; /* committed to Dloc; scratch buffer stays private */
    return status;
}

NTSTATUS
UDFIndexDirectory(PIRP_CONTEXT IrpContext, PVCB Vcb, PUDF_FILE_INFO FileInfo)
{
    _SEH2_VOLATILE UDF_DIRECTORY_INDEX_BUILD Build = {0};
    _SEH2_VOLATILE NTSTATUS Status = STATUS_UNSUCCESSFUL;
    if (!Vcb || !FileInfo || !FileInfo->Dloc) return STATUS_INVALID_PARAMETER;
    if (FileInfo->Dloc->DirIndex) return STATUS_INVALID_DEVICE_STATE;
    _SEH2_TRY {
        Status = UDFIndexDirectoryInternal(IrpContext, Vcb, FileInfo, &Build);
    } _SEH2_FINALLY {
        if (Build.Buffer) DbgFreePool(Build.Buffer);
        if (Build.Index) UDFDiscardDirectoryIndex(Build.Index);
    } _SEH2_END;
    return Status;
} // end UDFIndexDirectory()


/* Retag owns the directory generation until the parent FE is serialized.
 * Combine only contiguous FID payloads inside this same call; never defer a
 * write beyond it or fill a gap from an unrelated/stale sector snapshot. */
#define UDF_RETAG_WINDOW_BYTES 16384
#define UDF_RETAG_WINDOW_FIDS  64

typedef struct _UDF_RETAG_WINDOW {
    PDIR_INDEX_ITEM Entry[UDF_RETAG_WINDOW_FIDS];
    int8 Bytes[UDF_RETAG_WINDOW_BYTES];
} UDF_RETAG_WINDOW;

#ifdef UDF_METADATA_BATCH_DIAGNOSTICS
/* Private diagnostic only: OR reason bits, last FID offset/length and low32
 * logical length. Query runs after this single-thread workload completes. */
static void
UDFReTagFailureReason(PVCB Vcb, uint32 Reason, uint32 Offset,
                     uint32 Length, int64 LogicalLength)
{
    LONG old, value;
    do {
        old = InterlockedCompareExchange(&Vcb->MetadataBatchDiag[26],0,0);
        value = (LONG)(((uint32)old & 0xffff) | Reason |
                      ((Length > 0xffff ? 0xffff : Length) << 16));
    } while (InterlockedCompareExchange(&Vcb->MetadataBatchDiag[26],value,old) != old);
    InterlockedExchange(&Vcb->MetadataBatchDiag[18],(LONG)Offset);
    InterlockedExchange(&Vcb->MetadataBatchDiag[27],(LONG)LogicalLength);
}
/* Bits6..15 count owned pending FIDs, saturating at1023. Preserve the
 * existing low6reason OR and high16last-failure-length diagnostic fields. */
static void
UDFReTagCountPending(PVCB Vcb)
{
    LONG old, value;
    do {
        old = InterlockedCompareExchange(&Vcb->MetadataBatchDiag[26],0,0);
        if (((uint32)old & 0xffc0) == 0xffc0)
            return;
        value = old + 0x40;
    } while (InterlockedCompareExchange(&Vcb->MetadataBatchDiag[26],value,old) != old);
}
#else
#define UDFReTagFailureReason(v,r,o,l,n) ((void)0)
#define UDFReTagCountPending(v) ((void)0)
#endif

/* Directory indexes can reserve a named FID before its complete payload
 * reaches DataLoc.Length. Its owning FI remains responsible for publishing
 * it through UDFFlushFI (including FE-before-FID), not through retag reads
 * past EOF. Never accept a partially present record or an unowned hole. */
static BOOLEAN
UDFReTagOwnsUnwrittenFid(PUDF_FILE_INFO Directory, PDIR_INDEX_ITEM Entry)
{
    PUDF_FILE_INFO owner;
    PDIR_INDEX_HDR index = Directory->Dloc->DirIndex;
    uint_di frame, rel;
    if (Directory->Dloc->DataLoc.Length < 0 ||
        (uint64)Entry->Offset < (uint64)Directory->Dloc->DataLoc.Length ||
        !(Entry->FI_Flags & UDF_FI_FLAG_FI_MODIFIED) ||
        !(owner = Entry->FileInfo) || !owner->Dloc ||
        !owner->ParentFile || owner->ParentFile->Dloc != Directory->Dloc ||
        !owner->FileIdent || owner->FileIdentLen != Entry->Length ||
        owner->FileIdentLen < sizeof(FILE_IDENT_DESC) ||
        owner->FileIdentLen > 0xffff || owner->Index < 2)
        return FALSE;
    frame = owner->Index >> UDF_DIR_INDEX_FRAME_SH;
    rel = owner->Index & (UDF_DIR_INDEX_FRAME-1);
    if (frame >= index->FrameCount ||
        (frame == index->FrameCount-1 && rel >= index->LastFrameCount) ||
        UDFDirIndex(index,owner->Index) != Entry ||
        owner->FileIdent->descTag.tagIdent != TID_FILE_IDENT_DESC ||
        owner->FileIdent->icb.extLocation.logicalBlockNum !=
            Entry->FileEntryLoc.logicalBlockNum ||
        owner->FileIdent->icb.extLocation.partitionReferenceNum !=
            Entry->FileEntryLoc.partitionReferenceNum ||
        (uint64)sizeof(FILE_IDENT_DESC) + owner->FileIdent->lengthOfImpUse +
            owner->FileIdent->lengthFileIdent > owner->FileIdentLen)
        return FALSE;
    return TRUE;
}

static NTSTATUS
UDFReTagDirectoryWindows(PIRP_CONTEXT IrpContext, PVCB Vcb,
    PUDF_FILE_INFO FileInfo, uint16 RefPartNum,
    PUDF_DIR_SCAN_CONTEXT ScanContext, UDF_RETAG_WINDOW* Window)
{
    PDIR_INDEX_ITEM entry, next;
    UDF_DIR_SCAN_CONTEXT lookahead;
    uint32 count, length, offset, i, lba;
    PUDF_FILE_INFO curFileInfo;
    SIZE_T written;
    NTSTATUS status;

    while ((entry = UDFDirIndexScan(ScanContext, NULL))) {
        offset = entry->Offset;
        length = entry->Length;
        /* Length0/Offset0 denotes an index-only node, not a disk FID.
         * Leave its owner/dirty state intact for later materialization. */
        if (!length && !offset)
            continue;
        if (UDFReTagOwnsUnwrittenFid(FileInfo,entry)) {
            UDFReTagCountPending(Vcb);
            continue; // Retain this FI's complete buffer and dirty flag.
        }
        if (length < sizeof(tag) || length > UDF_RETAG_WINDOW_BYTES) {
            UDFReTagFailureReason(Vcb,length ? 2 : 1,offset,length,
                                 FileInfo->Dloc->DataLoc.Length);
            return STATUS_FILE_CORRUPT_ERROR;
        }
        if (FileInfo->Dloc->DataLoc.Length < 0 ||
            (uint64)offset + length > (uint64)FileInfo->Dloc->DataLoc.Length) {
            UDFReTagFailureReason(Vcb,4,offset,length,FileInfo->Dloc->DataLoc.Length);
            return STATUS_FILE_CORRUPT_ERROR;
        }
        Window->Entry[0] = entry;
        count = 1;
        while (count < UDF_RETAG_WINDOW_FIDS) {
            /* Do not consume the first entry of the next window. The scan
             * and its index pointers remain under the caller's directory
             * ownership; no table cursor escapes across released locks. */
            lookahead = *ScanContext;
            next = UDFDirIndexScan(&lookahead, NULL);
            if (!next || (uint64)next->Offset != (uint64)offset + length ||
                next->Length < sizeof(tag) ||
                next->Length > UDF_RETAG_WINDOW_BYTES - length ||
                (uint64)next->Offset + next->Length >
                    (uint64)FileInfo->Dloc->DataLoc.Length)
                break;
            Window->Entry[count++] = next;
            length += next->Length;
            *ScanContext = lookahead;
        }
        UDF_METADATA_COUNT(Vcb,28);
        status = UDFReadFile__(IrpContext,Vcb,FileInfo,offset,length,FALSE,Window->Bytes);
        if (!NT_SUCCESS(status)) {
            UDFReTagFailureReason(Vcb,16,offset,length,FileInfo->Dloc->DataLoc.Length);
            return status;
        }
        for (i = 0; i < count; i++) {
            entry = Window->Entry[i];
            lba = UDFExtentOffsetToLba(Vcb,FileInfo->Dloc->DataLoc.Mapping,
                                      entry->Offset,NULL,NULL,NULL,NULL);
            if (lba == LBA_OUT_OF_EXTENT) {
                UDFReTagFailureReason(Vcb,8,entry->Offset,entry->Length,
                                     FileInfo->Dloc->DataLoc.Length);
                return STATUS_FILE_CORRUPT_ERROR;
            }
            UDFSetUpTag(Vcb,(tag*)(Window->Bytes + entry->Offset - offset),
                       (uint16)entry->Length,UDFPhysLbaToPart(Vcb,RefPartNum,lba),0);
            UDF_METADATA_COUNT(Vcb,23);
            curFileInfo = entry->FileInfo;
            if (curFileInfo && curFileInfo->FileIdent) {
                FileInfo->Dloc->FELoc.Modified = TRUE;
                FileInfo->Dloc->FE_Flags |= UDF_FE_FLAG_FE_MODIFIED;
            }
        }
        /* Same lower mapping/primary/mirror and short-I/O checks as the
         * previous per-FID path. No FID points to a not-yet-flushed new FE. */
        UDF_METADATA_COUNT(Vcb,24);
        status = UDFWriteFile__(IrpContext,Vcb,FileInfo,offset,length,FALSE,
                                Window->Bytes,&written);
        if (!NT_SUCCESS(status) || written != length) {
            UDFReTagFailureReason(Vcb,32,offset,length,FileInfo->Dloc->DataLoc.Length);
            return NT_SUCCESS(status) ? STATUS_IO_DEVICE_ERROR : status;
        }
        /* The retagged descriptors have reached the normal paired writer.
         * Keep genuine pending FID changes dirty, but do not mark a clean
         * FID dirty solely for its already-written tag. FlushFI and batched
         * FID publication rebuild that tag before any later content write;
         * neither depends on the cached FileIdent tag being current here.
         * Do not mutate child buffers under only the directory ownership. */
    }
    return STATUS_SUCCESS;
}

/*
    This routine rebuilds tags for all entries from Dir.
 */
NTSTATUS
UDFReTagDirectory(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN OUT PUDF_FILE_INFO FileInfo   // source (opened)
    )
{
    uint32 l;
    uint32 Offset;
    int8* Buf;
    NTSTATUS status = STATUS_SUCCESS;
    SIZE_T WrittenBytes;
    PUDF_FILE_INFO curFileInfo;
    PDIR_INDEX_ITEM DirNdx;
    UDF_DIR_SCAN_CONTEXT ScanContext;
    uint16 RefPartNum;

    ValidateFileInfo(FileInfo);
    PDIR_INDEX_HDR hDirNdx = FileInfo->Dloc->DirIndex;
    if (!hDirNdx) return STATUS_NOT_A_DIRECTORY;

    // do not pack dirs on unchanged disks
    if (!Vcb->Modified)
        return STATUS_SUCCESS;

    if ( ((hDirNdx->DIFlags & UDF_DI_FLAG_INIT_IN_ICB) ? TRUE : FALSE) ==
        ((FileInfo->Dloc->DataLoc.Offset) ? TRUE : FALSE) ) {
        return STATUS_SUCCESS;
    }

    /* External appends may already be owned while old in-ICB tags still
     * need relocation. Retag only materialized payload, never a RAM-only
     * generation; keep DIFlags unchanged on any failed or short drain. */
    status = UDFFlushDirectoryAppend(IrpContext,Vcb,FileInfo);
    if (!NT_SUCCESS(status))
        return status;

    // start packing
    Buf = (int8*)DbgAllocatePool(PagedPool, Vcb->SectorSize*2);
    if (!Buf) return STATUS_INSUFFICIENT_RESOURCES;

    Offset = UDFDirIndex(hDirNdx,1)->Offset;

    if (!UDFDirIndexInitScan(FileInfo, &ScanContext, 1)) {
        DbgFreePool(Buf);
        return STATUS_SUCCESS;
    }

    ASSERT(FileInfo->Dloc->FELoc.Mapping[0].extLocation);
    RefPartNum = (uint16)UDFGetRefPartNumByPhysLba(Vcb, FileInfo->Dloc->FELoc.Mapping[0].extLocation);
    ASSERT(RefPartNum != -1);

    /* The general optical/in-ICB path stays unchanged. Failure to obtain
     * the optional larger buffer uses the original per-FID implementation. */
    if (Vcb->VcbCondition == VcbMounted && !Vcb->CDR_Mode &&
        Vcb->SectorSize == 512 && !FileInfo->Dloc->DataLoc.Offset &&
        (Vcb->UdfRevision == 0x201 || Vcb->UdfRevision == 0x250) &&
        Vcb->TargetDeviceObject &&
        Vcb->TargetDeviceObject->DeviceType == FILE_DEVICE_DISK &&
        !FlagOn(Vcb->VcbState, VCB_STATE_VOLUME_READ_ONLY | UDF_VCB_FLAGS_DEAD)) {
        UDF_RETAG_WINDOW* window = DbgAllocatePool(PagedPool,sizeof(*window));
        if (window) {
            _SEH2_TRY {
                status = UDFReTagDirectoryWindows(IrpContext,Vcb,FileInfo,
                                                 RefPartNum,&ScanContext,window);
            } _SEH2_FINALLY {
                DbgFreePool(window);
                DbgFreePool(Buf);
            } _SEH2_END;
            if (!NT_SUCCESS(status)) {
                UDF_METADATA_COUNT(Vcb,29);
                return status;
            }
            goto retag_complete;
        }
    }

    while((DirNdx = UDFDirIndexScan(&ScanContext, NULL))) {
        if (!DirNdx->Length && !DirNdx->Offset)
            continue;
        /* The low-memory/general fallback must use the same ownership
         * rule; it must not fabricate a FID from a short-success EOF read. */
        if (UDFReTagOwnsUnwrittenFid(FileInfo,DirNdx)) {
            UDFReTagCountPending(Vcb);
            continue;
        }
        if (FileInfo->Dloc->DataLoc.Length < 0 ||
            (uint64)DirNdx->Offset + DirNdx->Length >
                (uint64)FileInfo->Dloc->DataLoc.Length) {
            UDFReTagFailureReason(Vcb,4,DirNdx->Offset,DirNdx->Length,
                                 FileInfo->Dloc->DataLoc.Length);
            UDF_METADATA_COUNT(Vcb,29);
            DbgFreePool(Buf);
            return STATUS_FILE_CORRUPT_ERROR;
        }
        UDF_METADATA_COUNT(Vcb,28);
        status = UDFReadFile__(IrpContext, Vcb, FileInfo, Offset = DirNdx->Offset,
                                                   l = DirNdx->Length, FALSE, Buf);
        if (!NT_SUCCESS(status)) {
            DbgFreePool(Buf);
            return status;
        }
        UDF_METADATA_COUNT(Vcb,23);
        curFileInfo = DirNdx->FileInfo;
        // write modified
        UDFSetUpTag(Vcb, (tag*)Buf, (uint16)l,
                  UDFPhysLbaToPart(Vcb, RefPartNum,
                             UDFExtentOffsetToLba(Vcb, FileInfo->Dloc->DataLoc.Mapping,
                                        Offset, NULL, NULL, NULL, NULL)), 0);

        if (curFileInfo && curFileInfo->FileIdent) {
            FileInfo->Dloc->FELoc.Modified = TRUE;
            FileInfo->Dloc->FE_Flags |= UDF_FE_FLAG_FE_MODIFIED;
        }

        UDF_METADATA_COUNT(Vcb,24);
        status = UDFWriteFile__(IrpContext, Vcb, FileInfo, Offset, l, FALSE, Buf, &WrittenBytes);
        if (!NT_SUCCESS(status) || WrittenBytes != l) {
            DbgFreePool(Buf);
            return NT_SUCCESS(status) ? STATUS_IO_DEVICE_ERROR : status;
        }
        if (curFileInfo) {
            DirNdx->FI_Flags |= UDF_FI_FLAG_FI_MODIFIED;
        }
    }
    // resize DirIndex
    DbgFreePool(Buf);

retag_complete:
    hDirNdx->DIFlags &= ~UDF_DI_FLAG_INIT_IN_ICB;
    hDirNdx->DIFlags |= (FileInfo->Dloc->DataLoc.Offset ? UDF_DI_FLAG_INIT_IN_ICB : 0);
    return status;

} // end UDFReTagDirectory()

/*
    This routine performs search for specified file in specified directory &
    returns corresponding offset in extent if found.
 */
NTSTATUS
UDFFindFile(PVCB Vcb, BOOLEAN IgnoreCase, BOOLEAN NotDeleted,
            PUNICODE_STRING Name, PUDF_FILE_INFO DirInfo, uint_di* Index)
{
    return UDFFindFileExceptIndex(Vcb, IgnoreCase, NotDeleted, Name,
                                  DirInfo, Index, (uint_di)-1);
}

NTSTATUS
UDFFindFileExceptIndex(
    IN PVCB Vcb,
    IN BOOLEAN IgnoreCase,
    IN BOOLEAN NotDeleted,
    IN PUNICODE_STRING Name,
    IN PUDF_FILE_INFO DirInfo,
 IN OUT uint_di* Index,     // IN:start index OUT:found file index
    IN uint_di ExcludedIndex
    )
{
//    PDIR_INDEX_HDR hDirIndex = DirInfo->Dloc->DirIndex;
    UNICODE_STRING ShortName;
    WCHAR ShortNameBuffer[13];
    PDIR_INDEX_ITEM DirNdx;
    UDF_DIR_SCAN_CONTEXT ScanContext;
    uint_di j=(-1), k=(-1);
    HASH_ENTRY hashes;
    BOOLEAN CanBe8d3;

    UDFBuildHashEntry(Vcb, Name, &hashes, HASH_POSIX | HASH_ULFN);

    if ((CanBe8d3 = UDFCanNameBeA8dot3(Name))) {
        ShortName.MaximumLength = 13 * sizeof(WCHAR);
        ShortName.Buffer = (PWCHAR)&ShortNameBuffer;
    }

    if (!UDFDirIndexInitScan(DirInfo, &ScanContext, (*Index)))
        return STATUS_OBJECT_NAME_NOT_FOUND;

    if (!IgnoreCase && !CanBe8d3) {
        // perform case sensetive sequential directory scan

        while((DirNdx = UDFDirIndexScan(&ScanContext, NULL))) {
            if (ScanContext.i == ExcludedIndex)
                continue;
            if ( (DirNdx->hashes.hPosix == hashes.hPosix) &&
                 DirNdx->FName.Buffer &&
                (!RtlCompareUnicodeString(&(DirNdx->FName), Name, FALSE)) &&
               ( (!UDFIsDeleted(DirNdx)) || (!NotDeleted) ) ) {
                (*Index) = ScanContext.i;
                return STATUS_SUCCESS;
            }
        }
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    if (hashes.hPosix == hashes.hLfn) {

        while((DirNdx = UDFDirIndexScan(&ScanContext, NULL))) {
            if (ScanContext.i == ExcludedIndex)
                continue;
            if (!DirNdx->FName.Buffer ||
               (NotDeleted && UDFIsDeleted(DirNdx)) )
                continue;
            if ( (DirNdx->hashes.hLfn == hashes.hLfn) &&
                (!RtlCompareUnicodeString(&(DirNdx->FName), Name, IgnoreCase)) ) {
                (*Index) = ScanContext.i;
                return STATUS_SUCCESS;
            } else
            if ( CanBe8d3 &&
                !(DirNdx->FI_Flags & UDF_FI_FLAG_DOS) &&
                (DirNdx->hashes.hDos == hashes.hLfn) &&
                (k == (uint_di)(-1))) {
                UDFDOSName(Vcb, &ShortName, &(DirNdx->FName), ScanContext.i < 2) ;
                if (!RtlCompareUnicodeString(&ShortName, Name, IgnoreCase))
                    k = ScanContext.i;
            }
        }

    } else {

        while((DirNdx = UDFDirIndexScan(&ScanContext, NULL))) {
            if (ScanContext.i == ExcludedIndex)
                continue;
            // perform sequential directory scan
            if (!DirNdx->FName.Buffer ||
               (NotDeleted && UDFIsDeleted(DirNdx)) )
                continue;
            if ( (DirNdx->hashes.hPosix == hashes.hPosix) &&
                (!RtlCompareUnicodeString(&(DirNdx->FName), Name, FALSE)) ) {
                (*Index) = ScanContext.i;
                return STATUS_SUCCESS;
            } else
            if ( (DirNdx->hashes.hLfn == hashes.hLfn) &&
                (j == (uint_di)(-1)) &&
                (!RtlCompareUnicodeString(&(DirNdx->FName), Name, IgnoreCase)) ) {
                j = ScanContext.i;
            } else
            if ( CanBe8d3 &&
                !(DirNdx->FI_Flags & UDF_FI_FLAG_DOS) &&
                (DirNdx->hashes.hDos == hashes.hLfn) &&
                (k == (uint_di)(-1))) {
                UDFDOSName(Vcb, &ShortName, &(DirNdx->FName), ScanContext.i < 2 );
                if (!RtlCompareUnicodeString(&ShortName, Name, IgnoreCase)) {
                    k = ScanContext.i;
                }
            }
        }
    }

    if (j != (uint_di)(-1)) {
        (*Index) = j;
        return STATUS_SUCCESS;
    } else
    if (k != (uint_di)(-1)) {
        (*Index) = k;
        return STATUS_SUCCESS;
    }

    return STATUS_OBJECT_NAME_NOT_FOUND;

} // end UDFFindFileExceptIndex()

/*
    Find file in directory and fill enumeration context.
    This separates search from open, allowing caller to inspect
    the found entry before opening.
*/
NTSTATUS
UDFFindDirEntry(
    IN PVCB Vcb,
    IN PUDF_FILE_INFO DirInfo,
    IN PUNICODE_STRING FileName,
    IN BOOLEAN IgnoreCase,
    IN BOOLEAN NotDeleted,
    OUT PDIR_ENUM_CONTEXT DirContext
    )
{
    NTSTATUS status;
    uint_di Index = 0;

    // Initialize context
    RtlZeroMemory(DirContext, sizeof(DIR_ENUM_CONTEXT));
    DirContext->ParentInfo = DirInfo;

    if (!DirInfo->Dloc->DirIndex) {
        return STATUS_NOT_A_DIRECTORY;
    }

    DirContext->DirIndex = DirInfo->Dloc->DirIndex;

    // Find the file
    status = UDFFindFile(Vcb, IgnoreCase, NotDeleted, FileName, DirInfo, &Index);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    // Get the directory entry
    DirContext->DirNdx = UDFDirIndex(DirContext->DirIndex, Index);
    if (!DirContext->DirNdx) {
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    DirContext->Index = Index;
    return STATUS_SUCCESS;

} // end UDFFindDirEntry()

/*
    This routine returns pointer to parent DirIndex
*/
PDIR_INDEX_HDR
UDFGetDirIndexByFileInfo(
    IN PUDF_FILE_INFO FileInfo
    )
{
    ValidateFileInfo(FileInfo);

    if (!FileInfo) {
        BrutePoint();
        return NULL;
    }
    if (FileInfo->ParentFile) {
        ValidateFileInfo(FileInfo->ParentFile);

        if (UDFIsAStreamDir(FileInfo))
            return NULL;
        if (FileInfo->ParentFile->Dloc)
            return FileInfo->ParentFile->Dloc->DirIndex;
        return NULL;
    }
    if (FileInfo->Dloc)
        return FileInfo->Dloc->DirIndex;
    return NULL;
}

/*
    File Data Location support routines (UDFXxxDloc)
    This group is responsible for caching FE locations
    If requested FE referenced by another FI the file is assumed to be linked
    All linked files reference to common Data Location (& attr) structure
 */

/*
    Check if given FE is already in use
 */
LONG
UDFFindDloc(
    IN PVCB Vcb,
    IN uint32 Lba
    )
{
    PUDF_DATALOC_INDEX DlocList;
    uint32 l;

    if (!(DlocList = Vcb->DlocList) || !Lba) return (-1);
    // scan FE location cache
    l = Vcb->DlocCount;
    for(uint32 i=0; i<l; i++, DlocList++) {
        if (DlocList->Lba == Lba)
            return i;
    }
    return (-1);
} // end UDFFindDloc()

/*
    Check if given FE is already stored in memory
 */
LONG
UDFFindDlocInMem(
    IN PVCB Vcb,
    IN PUDF_DATALOC_INFO Dloc
    )
{
    PUDF_DATALOC_INDEX DlocList;
    uint32 l;

    if (!(DlocList = Vcb->DlocList) || !Dloc) return (-1);
    // scan FE location cache
    l = Vcb->DlocCount;
    for(uint32 i=0; i<l; i++, DlocList++) {
        if (DlocList->Dloc == Dloc)
            return i;
    }
    return (-1);
} // end UDFFindDlocInMem()

/*
    Find free cache entry
 */
LONG
UDFFindFreeDloc(
    IN PVCB Vcb,
    IN uint32 Lba
    )
{
    PUDF_DATALOC_INDEX DlocList;
    uint32 l;

    if (!Vcb->DlocList) {
        // init FE location cache
        if (!(Vcb->DlocList = (PUDF_DATALOC_INDEX)MyAllocatePoolTag__(NonPagedPool, sizeof(UDF_DATALOC_INDEX)*DLOC_LIST_GRANULARITY, MEM_DLOC_NDX_TAG)))
            return (-1);
        RtlZeroMemory(Vcb->DlocList, DLOC_LIST_GRANULARITY*sizeof(UDF_DATALOC_INDEX));
        Vcb->DlocCount = DLOC_LIST_GRANULARITY;
        Vcb->DlocFreeHint = 0;
    }
    /* DlocResource protects this lower bound. Retain first-free selection,
     * but do not rescan the occupied prefix. Store advances the hint only
     * after allocating and publishing the Dloc; allocation failure must
     * leave this same free slot available for the next request. */
    l = Vcb->DlocCount;
    uint32 first = Vcb->DlocFreeHint <= l ? Vcb->DlocFreeHint : 0;
    DlocList = Vcb->DlocList + first;
    for(uint32 i=first; i<l; i++, DlocList++) {
        if (!DlocList->Dloc)
            return i;
    }
    // alloc some free entries
    if (!MyReallocPool__((int8*)(Vcb->DlocList), Vcb->DlocCount*sizeof(UDF_DATALOC_INDEX),
                     (int8**)&(Vcb->DlocList), (Vcb->DlocCount+DLOC_LIST_GRANULARITY)*sizeof(UDF_DATALOC_INDEX))) {
        return (-1);
    }
    RtlZeroMemory(&(Vcb->DlocList[Vcb->DlocCount]), DLOC_LIST_GRANULARITY*sizeof(UDF_DATALOC_INDEX));
    Vcb->DlocCount += DLOC_LIST_GRANULARITY;
    return (Vcb->DlocCount - DLOC_LIST_GRANULARITY);
} // end UDFFindFreeDloc()

/*
    Try to store FE location in cache
    If it is already in use, caller will be informed about it
 */
/* Existing-file constructors own one early FI reference. New private FEs
 * remain UNDER_INIT until publication. LinkRefCount is the sum of real FI
 * holds; each hold contributes to its exact parent's OpenCount. */
static BOOLEAN
UDFCanAddFileReference(PUDF_FILE_INFO FileInfo, PUDF_DATALOC_INFO Dloc)
{
    return FileInfo->RefCount < MAXLONG && Dloc->LinkRefCount < MAXLONG &&
        (!FileInfo->ParentFile || FileInfo->ParentFile->OpenCount < MAXLONG);
}

static NTSTATUS
UDFStoreDlocInternal(PVCB Vcb, PUDF_FILE_INFO FileInfo, uint32 Lba,
                     BOOLEAN RequireNew)
{
    LONG index;
    _SEH2_VOLATILE PUDF_DATALOC_INFO Dloc = NULL;
    _SEH2_VOLATILE BOOLEAN NewDloc = FALSE, Attached = FALSE;
    _SEH2_VOLATILE BOOLEAN IndexLocked = FALSE, ReferencesLocked = FALSE;
    NTSTATUS Status = STATUS_SUCCESS;

    if (!Vcb || !FileInfo || !Lba || Lba == MAXULONG || FileInfo->Dloc ||
        FileInfo->RefCount || FileInfo->Constructing)
        return STATUS_INVALID_PARAMETER;
    _SEH2_TRY {
    UDFAcquireResourceExclusive(&Vcb->DlocResource, TRUE);
    IndexLocked = TRUE;
    index = UDFFindDloc(Vcb, Lba);
    if (index >= 0) {
        Dloc = Vcb->DlocList[index].Dloc;
        if (RequireNew) {
            /* A newly allocated FE must not overwrite a cached generation
             * at the same LBA, even if the bitmap incorrectly offered it. */
            Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
            Status = STATUS_FILE_CORRUPT_ERROR;
            _SEH2_LEAVE;
        }
    } else {
        index = UDFFindFreeDloc(Vcb, Lba);
        if (index < 0) {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            _SEH2_LEAVE;
        }
        Dloc = (PUDF_DATALOC_INFO)MyAllocatePoolTag__(
            UDF_DATALOC_INFO_MT, sizeof(*Dloc), MEM_DLOC_INF_TAG);
        if (!Dloc) {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            _SEH2_LEAVE;
        }
        RtlZeroMemory(Dloc, sizeof(*Dloc));
        Dloc->LinkedFileInfo = FileInfo;
        NewDloc = TRUE;
    }

    UDFAcquireResourceExclusive(&Vcb->DlocResource2, TRUE);
    ReferencesLocked = TRUE;
    if (Vcb->RetainedOpenCount || Dloc->CleanupOwner || Dloc->AllocationReleaseActive ||
        !NT_SUCCESS(Dloc->AllocationReleaseError) || Dloc->NamespaceScratchOwner || Dloc->StreamDeleteContextOwner || Dloc->StreamCreate || Dloc->CreateRollbackOwner || Dloc->CreateConstructionOwner ||
        (Dloc->FE_Flags & UDF_FE_FLAG_UNDER_INIT)) {
        Status = Vcb->RetainedOpenCount ? STATUS_FILE_CORRUPT_ERROR :
            !NT_SUCCESS(Dloc->AllocationReleaseError) ? Dloc->AllocationReleaseError : STATUS_SHARING_PAUSED;
        _SEH2_LEAVE;
    }
    if (!RequireNew && !UDFCanAddFileReference(FileInfo, Dloc)) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        _SEH2_LEAVE;
    }
    /* All fallible allocation/resource admission is complete. No callback
     * or I/O occurs in this publication/reference group. */
    Attached = TRUE;
    FileInfo->Dloc = Dloc;
    FileInfo->Constructing = TRUE;
    FileInfo->DlocInitOwner = NewDloc;
    FileInfo->ConstructionFailed = FALSE;
    FileInfo->ConstructionReferenceOwned = !RequireNew;
    FileInfo->ConstructionParent = RequireNew ? NULL : FileInfo->ParentFile;
    if (NewDloc) {
        Dloc->FE_Flags |= UDF_FE_FLAG_UNDER_INIT;
        Vcb->DlocList[index].Lba = Lba;
        Vcb->DlocList[index].Dloc = Dloc;
        Vcb->DlocFreeHint = (ULONG)index + 1;
    } else {
        UDFInsertLinkedFile(FileInfo, Dloc->LinkedFileInfo);
    }
    /* Existing-file constructors can race the old canonical FCB. Pin
     * those immediately. A private, newly allocated FE is protected by
     * UNDER_INIT and the creator's parent resource until publication. */
    if (FileInfo->ConstructionReferenceOwned)
        UDFReferenceFile__(FileInfo);
    } _SEH2_FINALLY {
        if (ReferencesLocked) UDFReleaseResource(&Vcb->DlocResource2);
        if (IndexLocked) UDFReleaseResource(&Vcb->DlocResource);
        if (NewDloc && !Attached) MyFreePool__(Dloc); /* never published */
    } _SEH2_END;
    return Status;
}

NTSTATUS
UDFStoreDloc(PVCB Vcb, PUDF_FILE_INFO FileInfo, uint32 Lba)
{
    return UDFStoreDlocInternal(Vcb, FileInfo, Lba, FALSE);
}

NTSTATUS
UDFCreateDloc(PVCB Vcb, PUDF_FILE_INFO FileInfo, uint32 Lba)
{
    return UDFStoreDlocInternal(Vcb, FileInfo, Lba, TRUE);
}

NTSTATUS
UDFCompleteFileInfoConstruction(PVCB Vcb, PUDF_FILE_INFO FileInfo)
{
    NTSTATUS Status = STATUS_SUCCESS;
    PUDF_DATALOC_INFO Dloc = FileInfo->Dloc;
    if (!Dloc) return STATUS_FILE_INVALID;
    UDFAcquireResourceExclusive(&Vcb->DlocResource2, TRUE);
    if (FileInfo->ConstructionReferenceOwned &&
        (!FileInfo->RefCount || Dloc->LinkRefCount < FileInfo->RefCount ||
         FileInfo->ConstructionParent != FileInfo->ParentFile ||
         (FileInfo->ConstructionParent && (!FileInfo->ConstructionParent->Dloc ||
                                           !FileInfo->ConstructionParent->OpenCount)))) {
        Status = STATUS_FILE_CORRUPT_ERROR;
        Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
        InterlockedCompareExchange(&Vcb->MetadataWritebackError, Status, STATUS_SUCCESS);
    } else if (Vcb->RetainedOpenCount || !FileInfo->Constructing ||
        FileInfo->ConstructionFailed || Dloc->CleanupOwner || Dloc->CreateRollbackOwner ||
        (Dloc->CreateConstructionOwner && Dloc->CreateConstructionOwner != FileInfo)) {
        Status = Vcb->RetainedOpenCount ? STATUS_FILE_CORRUPT_ERROR : STATUS_SHARING_PAUSED;
    } else if (!FileInfo->ConstructionReferenceOwned && !UDFCanAddFileReference(FileInfo, Dloc)) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
    } else {
        /* Transfer an early reader pin, or publish the creator's first
         * open reference. Never acquire a second hold for a reader. */
        if (!FileInfo->ConstructionReferenceOwned)
            UDFReferenceFile__(FileInfo);
        FileInfo->ConstructionReferenceOwned = FALSE;
        FileInfo->Constructing = FALSE;
        FileInfo->ConstructionParent = NULL;
        if (FileInfo->DlocInitOwner)
            Dloc->FE_Flags &= ~UDF_FE_FLAG_UNDER_INIT;
        FileInfo->DlocInitOwner = FALSE;
    }
    UDFReleaseResource(&Vcb->DlocResource2);
    return Status;
}

VOID
UDFDropFileInfoConstruction(PVCB Vcb, PUDF_FILE_INFO FileInfo)
{
    UDFAcquireResourceExclusive(&Vcb->DlocResource2, TRUE);
    if (FileInfo->CreateConstructionParent ||
        (FileInfo->Dloc && FileInfo->Dloc->CreateConstructionOwner)) {
        /* A private publication owner must explicitly release its parent
         * or retain the failed generation. A generic failed-open cleanup
         * cannot erase UNDER_INIT or spend that operation's token. */
        FileInfo->ConstructionFailed = TRUE;
        FileInfo->CleanupPending = TRUE;
        Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
        InterlockedCompareExchange(&Vcb->MetadataWritebackError,
                                    STATUS_FILE_CORRUPT_ERROR, STATUS_SUCCESS);
        UDFReleaseResource(&Vcb->DlocResource2);
        return;
    }
    if (FileInfo->Constructing) {
        ASSERT(FileInfo->Dloc);
        if (FileInfo->ConstructionReferenceOwned) {
            ASSERT(FileInfo->RefCount);
            if (!FileInfo->Dloc || !FileInfo->RefCount ||
                !FileInfo->Dloc->LinkRefCount ||
                (FileInfo->ConstructionParent && !FileInfo->ConstructionParent->OpenCount)) {
                FileInfo->ConstructionFailed = TRUE;
                Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
                InterlockedCompareExchange(&Vcb->MetadataWritebackError,
                                            STATUS_FILE_CORRUPT_ERROR, STATUS_SUCCESS);
                UDFReleaseResource(&Vcb->DlocResource2);
                return; /* retain the unresolved hold; never underflow it */
            }
            InterlockedDecrement((PLONG)&FileInfo->RefCount);
            InterlockedDecrement((PLONG)&FileInfo->Dloc->LinkRefCount);
            if (FileInfo->ConstructionParent)
                InterlockedDecrement((PLONG)&FileInfo->ConstructionParent->OpenCount);
        }
        FileInfo->ConstructionReferenceOwned = FALSE;
        FileInfo->ConstructionParent = NULL;
        FileInfo->Constructing = FALSE;
        FileInfo->ConstructionFailed = TRUE;
        /* A failed first initializer remains UNDER_INIT until discarded.
         * Never expose its partial mapping as a successfully loaded cache. */
    }
    UDFReleaseResource(&Vcb->DlocResource2);
}

NTSTATUS
UDFSetFileInfoConstructionParent(PVCB Vcb, PUDF_FILE_INFO FileInfo,
                                 PUDF_FILE_INFO Parent)
{
    _SEH2_VOLATILE NTSTATUS Status = STATUS_SUCCESS;
    PUDF_FILE_INFO OldParent;
    if (!FileInfo || !FileInfo->Dloc || Parent == FileInfo || (Parent && !Parent->Dloc))
        return STATUS_FILE_INVALID;
    UDFAcquireResourceExclusive(&Vcb->DlocResource2, TRUE);
    _SEH2_TRY {
        if (!FileInfo->Constructing || FileInfo->ConstructionFailed) {
            Status = STATUS_FILE_INVALID;
            _SEH2_LEAVE;
        }
        if (FileInfo->CreateConstructionParent && FileInfo->CreateConstructionParent != Parent) {
            Status = STATUS_FILE_CORRUPT_ERROR;
            _SEH2_LEAVE; /* the private constructor owner pins this exact parent */
        }
        OldParent = FileInfo->ConstructionParent;
        if (FileInfo->ConstructionReferenceOwned) {
            if (!FileInfo->RefCount || FileInfo->Dloc->LinkRefCount < FileInfo->RefCount ||
                FileInfo->ParentFile != OldParent ||
                (OldParent && (!OldParent->Dloc || !OldParent->OpenCount))) {
                Status = STATUS_FILE_CORRUPT_ERROR;
                _SEH2_LEAVE;
            }
            if (OldParent != Parent) {
                if (Parent && Parent->OpenCount >= MAXLONG) {
                    Status = STATUS_INSUFFICIENT_RESOURCES;
                    _SEH2_LEAVE;
                }
                /* Both sides are admitted before changing either count.
                 * No callback separates the exact parent-token transfer. */
                if (Parent) InterlockedIncrement((PLONG)&Parent->OpenCount);
                if (OldParent) InterlockedDecrement((PLONG)&OldParent->OpenCount);
                FileInfo->ConstructionParent = Parent;
            }
        } else if (FileInfo->RefCount) {
            Status = STATUS_FILE_CORRUPT_ERROR;
            _SEH2_LEAVE;
        }
        FileInfo->ParentFile = Parent;
    } _SEH2_FINALLY {
        if (Status == STATUS_FILE_CORRUPT_ERROR) {
            Vcb->VcbState |= VCB_STATE_VOLUME_READ_ONLY;
            InterlockedCompareExchange(&Vcb->MetadataWritebackError, Status, STATUS_SUCCESS);
        }
        UDFReleaseResource(&Vcb->DlocResource2);
    } _SEH2_END;
    return Status;
}

NTSTATUS
UDFReferenceFileChecked(PVCB Vcb, PUDF_FILE_INFO FileInfo)
{
    NTSTATUS Status = STATUS_SUCCESS;
    PUDF_DATALOC_INFO Dloc = FileInfo ? FileInfo->Dloc : NULL;
    if (!Dloc) return STATUS_FILE_INVALID;
    UDFAcquireResourceExclusive(&Vcb->DlocResource2, TRUE);
    if (Vcb->RetainedOpenCount || Dloc->CleanupOwner || Dloc->AllocationReleaseActive ||
        !NT_SUCCESS(Dloc->AllocationReleaseError) || Dloc->NamespaceScratchOwner || Dloc->StreamDeleteContextOwner || Dloc->StreamCreate || Dloc->CreateRollbackOwner || Dloc->CreateConstructionOwner || FileInfo->Constructing ||
        FileInfo->ConstructionFailed || FileInfo->TemporaryRetired ||
        (Dloc->FE_Flags & UDF_FE_FLAG_UNDER_INIT))
        Status = Vcb->RetainedOpenCount ? STATUS_FILE_CORRUPT_ERROR :
            !NT_SUCCESS(Dloc->AllocationReleaseError) ? Dloc->AllocationReleaseError : STATUS_SHARING_PAUSED;
    else if (!UDFCanAddFileReference(FileInfo, Dloc))
        Status = STATUS_INSUFFICIENT_RESOURCES;
    else
        UDFReferenceFile__(FileInfo);
    UDFReleaseResource(&Vcb->DlocResource2);
    return Status;
}

NTSTATUS
UDFReferenceClonedFile(PVCB Vcb, PUDF_FILE_INFO FileInfo,
                       PUDF_FILE_INFO Source)
{
    NTSTATUS Status = STATUS_SUCCESS;
    PUDF_DATALOC_INFO Dloc = Source->Dloc;
    UDFAcquireResourceExclusive(&Vcb->DlocResource2, TRUE);
    if (!Dloc || Vcb->RetainedOpenCount || Dloc->CleanupOwner || Dloc->AllocationReleaseActive ||
        !NT_SUCCESS(Dloc->AllocationReleaseError) || Dloc->NamespaceScratchOwner || Dloc->StreamDeleteContextOwner || Dloc->StreamCreate || Dloc->CreateRollbackOwner || Dloc->CreateConstructionOwner || Source->Constructing ||
        Source->ConstructionFailed || Source->TemporaryRetired ||
        (Dloc->FE_Flags & UDF_FE_FLAG_UNDER_INIT)) {
        Status = !Dloc ? STATUS_FILE_INVALID : Vcb->RetainedOpenCount ? STATUS_FILE_CORRUPT_ERROR :
            !NT_SUCCESS(Dloc->AllocationReleaseError) ? Dloc->AllocationReleaseError : STATUS_SHARING_PAUSED;
    } else if (!UDFCanAddFileReference(FileInfo, Dloc)) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
    } else {
        InitializeListHead(&FileInfo->CreateRollbackLinks);
        FileInfo->CreateRollbackPhase = 0;
        FileInfo->CreateRollbackCounted = FileInfo->CreateRollbackTouched = FALSE;
        FileInfo->CreateFailureStatus = FileInfo->CreateRollbackFirstError =
            FileInfo->CreateRollbackLastError = STATUS_SUCCESS;
        FileInfo->CreateConstructionParent = NULL;
        FileInfo->CreateConstructionError = STATUS_SUCCESS;
        FileInfo->CreateConstructionCounted = FileInfo->CreateConstructionOpenOwned = FALSE;
        FileInfo->Constructing = FALSE;
        FileInfo->DlocInitOwner = FALSE;
        FileInfo->ConstructionFailed = FALSE;
        FileInfo->ConstructionReferenceOwned = FALSE;
        FileInfo->ConstructionParent = NULL;
        FileInfo->RetainedOpenReferences = 0;
        FileInfo->RetainedOpenError = STATUS_SUCCESS;
        FileInfo->CleanupPending = FALSE;
        FileInfo->InternalOpenReleased = FALSE;
        FileInfo->TemporaryRetired = FALSE;
        UDFInsertLinkedFile(FileInfo, Source);
        UDFReferenceFile__(FileInfo);
    }
    UDFReleaseResource(&Vcb->DlocResource2);
    return Status;
}

/* Check before any fallible allocation release, then detach immediately
 * before memory-only destruction. A failed decision leaves the indexed
 * generation, cleanup claim, buffers and prefree receipt owned in place. */
NTSTATUS
UDFCheckDlocCleanup(PVCB Vcb, PUDF_FILE_INFO FileInfo, BOOLEAN Detach)
{
    PUDF_DATALOC_INFO Dloc = FileInfo ? FileInfo->Dloc : NULL;
    _SEH2_VOLATILE BOOLEAN IndexLocked = FALSE, ReferencesLocked = FALSE;
    NTSTATUS Status = STATUS_SUCCESS;
    LONG Slot;
    if (!Dloc || !ExIsResourceAcquiredExclusiveLite(&Vcb->VcbResource) ||
        Dloc->CleanupOwner != PsGetCurrentThread())
        return STATUS_INVALID_DEVICE_STATE;
    if (Dloc->AllocationReleaseActive) return STATUS_DEVICE_BUSY;
    if (!NT_SUCCESS(Dloc->AllocationReleaseError)) return Dloc->AllocationReleaseError;
    if (!NT_SUCCESS(Dloc->ZeroDataError)) return Dloc->ZeroDataError;
    if (!NT_SUCCESS(Dloc->CacheSizeError)) return Dloc->CacheSizeError;
    if (!NT_SUCCESS(Dloc->NamespaceTargetError)) return Dloc->NamespaceTargetError;
    if (!UDFReleaseDirectoryAppend(Vcb, Dloc)) return STATUS_DEVICE_BUSY;
    _SEH2_TRY {
        UDFAcquireResourceExclusive(&Vcb->DlocResource, TRUE);
        IndexLocked = TRUE;
        UDFAcquireResourceExclusive(&Vcb->DlocResource2, TRUE);
        ReferencesLocked = TRUE;
        Slot = UDFFindDlocInMem(Vcb, Dloc);
        if (Slot < 0 || FileInfo->Dloc != Dloc || Dloc->LinkedFileInfo != FileInfo ||
            FileInfo->NextLinkedFile != FileInfo || FileInfo->PrevLinkedFile != FileInfo ||
            Dloc->CleanupOwner != PsGetCurrentThread()) {
            Status = STATUS_FILE_CORRUPT_ERROR;
            _SEH2_LEAVE;
        }
        if (FileInfo->RefCount || FileInfo->OpenCount || FileInfo->Fcb || FileInfo->Constructing ||
            FileInfo->RetainedOpenReferences || FileInfo->CreateConstructionParent ||
            Dloc->LinkRefCount || Dloc->CommonFcb || Dloc->CreateRollbackOwner ||
            Dloc->CreateConstructionOwner || Dloc->RetiredExtents || Dloc->NamespaceScratch ||
            Dloc->NamespaceTarget || Dloc->NamespaceFileInfo || Dloc->NamespaceScratchOwner ||
            Dloc->NamespacePhase != UdfNamespaceNone ||
            Dloc->NamespaceTargetState != UdfNamespaceTargetNone ||
            !NT_SUCCESS(Dloc->NamespaceTargetError) || Dloc->StreamDeleteOwner ||
            Dloc->StreamDeleteContextOwner || Dloc->StreamCreate ||
            !NT_SUCCESS(Dloc->ZeroDataError) || !NT_SUCCESS(Dloc->CacheSizeError)) {
            Status = STATUS_DEVICE_BUSY;
            _SEH2_LEAVE;
        }
        if (Dloc->DeletePrefree && (Detach || !Dloc->DeleteCommitted ||
            !(Dloc->FE_Flags & UDF_FE_FLAG_FREE_DEFERRED))) {
            Status = STATUS_FILE_CORRUPT_ERROR;
            _SEH2_LEAVE;
        }
        if (Detach) {
            /* Only an already-written optimization proof is retired here.
             * There is no allocation/cache/device I/O after index removal. */
            UDFReleaseDirectoryFECheckpoint(Vcb, Dloc);
            RtlZeroMemory(&Vcb->DlocList[Slot], sizeof(Vcb->DlocList[Slot]));
            Vcb->DlocFreeHint = min(Vcb->DlocFreeHint, (ULONG)Slot);
        }
    } _SEH2_FINALLY {
        if (ReferencesLocked) UDFReleaseResource(&Vcb->DlocResource2);
        if (IndexLocked) UDFReleaseResource(&Vcb->DlocResource);
    } _SEH2_END;
    return Status;
}

/*
    This routine updates Dloc LBA after relocation
 */
void
UDFRelocateDloc(
    IN PVCB Vcb,
    IN PUDF_DATALOC_INFO Dloc,
    IN uint32 NewLba
    )
{
    LONG i;

    UDFAcquireResourceExclusive(&(Vcb->DlocResource),TRUE);

    if ((i = UDFFindDlocInMem(Vcb, Dloc)) != (-1)) {
        ASSERT(Vcb->DlocList);
        Vcb->DlocList[i].Lba = NewLba;
    }
    UDFReleaseResource(&(Vcb->DlocResource));

} // end UDFRelocateDloc()

/* The caller owns the exclusive VCB resource. A retained FI/claim prevents
 * freeing its VCB resources even if VPB or user-handle counts reached zero. */
BOOLEAN
UDFHasRetainedDlocs(PVCB Vcb)
{
    uint32 i;
    BOOLEAN Retained = FALSE;
    ASSERT_EXCLUSIVE_VCB(Vcb);
    NT_ASSERT(Vcb->VcbLockThread != PsGetCurrentThread());
    /* Counts are independent inventory owners too. An empty FI pointer
     * must not erase an unresolved generation or a deferred final claim. */
    if (Vcb->FcbTeardownCount || Vcb->ClosedFcbRetryCount ||
        Vcb->FailedZeroDataCount || Vcb->RetainedOpenCount || Vcb->AbortedOpenCount || Vcb->PendingNamespaceCount ||
        Vcb->PendingDeleteCount || Vcb->RetainedDeleteCount ||
        Vcb->FailedAllocations || Vcb->FailedAllocationCount ||
        Vcb->ActiveAllocationLeases || Vcb->ActiveAllocationLeaseCount ||
        Vcb->PendingExtentRetirements ||
        !NT_SUCCESS(UDFAllocationFailureStatus(Vcb)))
        return TRUE;
    UDFAcquireResourceExclusive(&Vcb->DlocResource, TRUE);
    UDFAcquireResourceExclusive(&Vcb->DlocResource2, TRUE);
    for (i = 0; i < Vcb->DlocCount; i++) {
        PUDF_DATALOC_INFO Dloc = Vcb->DlocList[i].Dloc;
        if (Dloc && (Dloc->LinkedFileInfo || Dloc->CommonFcb ||
                     Dloc->LinkRefCount || Dloc->CleanupOwner || Dloc->AllocationReleaseActive ||
                     !NT_SUCCESS(Dloc->AllocationReleaseError) ||
                     Dloc->NamespaceScratch || Dloc->NamespaceTarget ||
                     Dloc->NamespaceFileInfo || Dloc->NamespacePhase != UdfNamespaceNone ||
                     Dloc->NamespaceScratchOwner || Dloc->StreamDeleteOwner ||
                     Dloc->StreamDeleteContextOwner || Dloc->StreamCreate || !NT_SUCCESS(Dloc->ZeroDataError) ||
                     Dloc->RetiredExtents ||
                     Dloc->DataLoc.Modified || Dloc->AllocLoc.Modified ||
                     Dloc->FELoc.Modified ||
                     (Dloc->FE_Flags & (UDF_FE_FLAG_UNDER_INIT |
                         UDF_FE_FLAG_FE_MODIFIED | UDF_FE_FLAG_DIR_MODIFIED |
                         UDF_FE_FLAG_CACHED_DATA_PENDING)))) {
            Retained = TRUE;
            break;
        }
    }
    UDFReleaseResource(&Vcb->DlocResource2);
    UDFReleaseResource(&Vcb->DlocResource);
    return Retained;
}

/*
    Release FE cache
 */
void
UDFReleaseDlocList(
    IN PVCB Vcb
    )
{
    NT_ASSERT(!Vcb->PendingExtentRetirements);
    if (!Vcb->DlocList) return;
    UDFAcquireResourceExclusive(&(Vcb->DlocResource),TRUE);
    for(uint32 i=0; i<Vcb->DlocCount; i++) {
        if (Vcb->DlocList[i].Dloc) {
            UDFReleaseDirectoryFECheckpoint(Vcb, Vcb->DlocList[i].Dloc);
            MyFreePool__(Vcb->DlocList[i].Dloc);
        }
    }
    MyFreePool__(Vcb->DlocList);
    Vcb->DlocList = NULL;
    Vcb->DlocCount = 0;
    Vcb->DlocFreeHint = 0;
    UDFReleaseResource(&(Vcb->DlocResource));
} // end UDFReleaseDlocList()

/*
    This routine walks through Linked/Parallel FI chain and looks for
    FE with same Index & Parent File
 */
PUDF_FILE_INFO
UDFLocateParallelFI(
    PUDF_FILE_INFO di,  // parent FileInfo
    uint_di i,            // Index
    PUDF_FILE_INFO fi   // FileInfo to start search from
    )
{
    PUDF_FILE_INFO    ParFileInfo = fi->NextLinkedFile;
//    PUDF_DATALOC_INFO Dloc = di->Dloc;
    while((ParFileInfo != fi) &&
          (ParFileInfo->Constructing || ParFileInfo->ConstructionFailed ||
           (ParFileInfo->ParentFile != di) ||
           (ParFileInfo->Index != i)) ) {
        ParFileInfo = ParFileInfo->NextLinkedFile;
    }
    return ParFileInfo;
//   BrutePoint();
} // end UDFLocateParallelFI()

/*
    This routine walks through Linked/Parallel FI chain and looks for
    FE with same Index & Parent Dloc
 */
PUDF_FILE_INFO
UDFLocateAnyParallelFI(
    PUDF_FILE_INFO fi   // FileInfo to start search from
    )
{
    PUDF_FILE_INFO    ParFileInfo = fi->NextLinkedFile;
    /* A parentless by-ID representation shares the Dloc, not a named
     * link's directory index. Only another parentless FI is its parallel.
     * Conversely, never dereference that FI as a named alias's parent. */
    while (ParFileInfo && ParFileInfo != fi) {
        if (ParFileInfo->Constructing || ParFileInfo->ConstructionFailed) {
            ParFileInfo = ParFileInfo->NextLinkedFile;
            continue;
        }
        if (!fi->ParentFile) {
            if (!ParFileInfo->ParentFile)
                return ParFileInfo;
        } else if (ParFileInfo->ParentFile &&
                   ParFileInfo->Index == fi->Index &&
                   ParFileInfo->ParentFile->Dloc == fi->ParentFile->Dloc) {
            return ParFileInfo;
        }
        ParFileInfo = ParFileInfo->NextLinkedFile;
    }
    return NULL;
//   BrutePoint();
} // end UDFLocateAnyParallelFI()

BOOLEAN
UDFHasLiveFileInfoPeer(PUDF_FILE_INFO FileInfo)
{
    PUDF_FILE_INFO Peer;
    for (Peer = FileInfo->NextLinkedFile; Peer && Peer != FileInfo;
         Peer = Peer->NextLinkedFile) {
        if (Peer->RefCount || Peer->OpenCount)
            return TRUE;
    }
    return Peer == NULL;
}

void
UDFInsertLinkedFile(
    PUDF_FILE_INFO fi,   // FileInfo to be added to chain
    PUDF_FILE_INFO fi2   // any FileInfo fro the chain
    )
{
    fi->NextLinkedFile = fi2->NextLinkedFile;
    fi->PrevLinkedFile = fi2;
    fi->NextLinkedFile->PrevLinkedFile =
    fi->PrevLinkedFile->NextLinkedFile = fi;
    return;
} // end UDFInsertLinkedFile()

