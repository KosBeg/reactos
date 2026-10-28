/*
 * PROJECT: FreeLoader
 * LICENSE: GPL-2.0-or-later
 * PURPOSE: Read-only UDF 2.50 metadata-partition filesystem access
 *
 * This reader handles the recorded short/long allocation descriptors used
 * by the Windows UDF 2.50 hard-disk formatter and by ReactOS UDFS. It is
 * intentionally read-only and rejects unsupported partition maps and sparse
 * extents rather than guessing their location.
 */

#include <freeldr.h>
#include <udf_descriptor.h>

#include <debug.h>
DBG_DEFAULT_CHANNEL(FILESYSTEM);

#define UDF_BLOCK_SIZE 512
#define UDF_MAX_EXTENTS 256
#define UDF_MAX_DIRECTORY (1024 * 1024)
#define TAG_UDF_VOLUME 'VfdU'
#define TAG_UDF_FILE 'FfdU'
#define TAG_UDF_DIRECTORY 'DfdU'

typedef struct _UDF_EXTENT
{
    ULONG Length;
    ULONG Block;
    USHORT PartitionRef;
} UDF_EXTENT;

typedef struct _UDF_VOLUME
{
    ULONG DeviceId;
    ULONG SectorCount;
    ULONG PartitionStart;
    ULONG PartitionBlocks;
    ULONG MetadataBlocks;
    ULONG MetadataCount;
    UDF_EXTENT Metadata[UDF_MAX_EXTENTS];
    ULONG MirrorCount;
    UDF_EXTENT Mirror[UDF_MAX_EXTENTS];
    UDF_EXTENT RootIcb;
} UDF_VOLUME;

typedef struct _UDF_FILE
{
    ULONGLONG Size;
    ULONGLONG Position;
    ULONG Attributes;
    ULONG FileType;
    ULONG ExtentCount;
    UDF_EXTENT Extents[UDF_MAX_EXTENTS];
    ULONG InlineLength;
    ULONG IcbBlock;
    BOOLEAN MetadataMirror;
    UCHAR Inline[UDF_BLOCK_SIZE];
    CHAR Name[256];
} UDF_FILE;

static UDF_VOLUME* UdfVolumes[MAX_FDS];

static USHORT
Udf16(const UCHAR* Buffer)
{
    return (USHORT)(Buffer[0] | ((USHORT)Buffer[1] << 8));
}

static ULONG
Udf32(const UCHAR* Buffer)
{
    return (ULONG)Udf16(Buffer) | ((ULONG)Udf16(Buffer + 2) << 16);
}

static ULONGLONG
Udf64(const UCHAR* Buffer)
{
    return (ULONGLONG)Udf32(Buffer) | ((ULONGLONG)Udf32(Buffer + 4) << 32);
}

static BOOLEAN
UdfReadPhysicalBlock(UDF_VOLUME* Volume, ULONG Block, UCHAR* Buffer)
{
    LARGE_INTEGER Position;
    ULONG Count;

    if (Block >= Volume->SectorCount)
        return FALSE;
    Position.QuadPart = (ULONGLONG)Block * UDF_BLOCK_SIZE;
    if (ArcSeek(Volume->DeviceId, &Position, SeekAbsolute) != ESUCCESS)
        return FALSE;
    return (ArcRead(Volume->DeviceId, Buffer, UDF_BLOCK_SIZE, &Count) == ESUCCESS &&
            Count == UDF_BLOCK_SIZE);
}

static BOOLEAN
UdfMapBlockCopy(UDF_VOLUME* Volume, USHORT PartitionRef, ULONG Block,
                BOOLEAN Mirror, ULONG* Physical)
{
    ULONG Index;
    ULONGLONG Offset = Block;

    if (PartitionRef == 0)
    {
        if (Block >= Volume->PartitionBlocks)
            return FALSE;
        if (Volume->PartitionStart >= Volume->SectorCount ||
            Block >= Volume->SectorCount - Volume->PartitionStart)
            return FALSE;
        *Physical = Volume->PartitionStart + Block;
        return TRUE;
    }
    if (PartitionRef != 1)
        return FALSE;

    if (Block >= Volume->MetadataBlocks)
        return FALSE;
    for (Index = 0; Index < (Mirror ? Volume->MirrorCount : Volume->MetadataCount); ++Index)
    {
        UDF_EXTENT* Extent = Mirror ? &Volume->Mirror[Index] : &Volume->Metadata[Index];
        ULONG Blocks = Extent->Length / UDF_BLOCK_SIZE;
        if (Offset < Blocks)
        {
            if (Extent->PartitionRef != 0 || Extent->Block >= Volume->PartitionBlocks ||
                Offset >= Volume->PartitionBlocks - Extent->Block)
                return FALSE;
            *Physical = Volume->PartitionStart + Extent->Block + (ULONG)Offset;
            return TRUE;
        }
        Offset -= Blocks;
    }
    return FALSE;
}

static BOOLEAN
UdfReadLogicalBlockCopy(UDF_VOLUME* Volume, USHORT PartitionRef, ULONG Block,
                        UCHAR* Buffer, BOOLEAN Mirror)
{
    ULONG Physical;
    return (UdfMapBlockCopy(Volume, PartitionRef, Block, Mirror, &Physical) &&
            UdfReadPhysicalBlock(Volume, Physical, Buffer));
}

static BOOLEAN
UdfReadTaggedBlock(UDF_VOLUME* Volume, USHORT PartitionRef, ULONG Block,
                   ULONG Kind, UCHAR* Buffer, PBOOLEAN MirrorUsed)
{
    if (MirrorUsed) *MirrorUsed = FALSE;
    if (UdfReadLogicalBlockCopy(Volume, PartitionRef, Block, Buffer, FALSE) &&
        UdfValidateDescriptor(Buffer, UDF_BLOCK_SIZE, Kind, Block))
        return TRUE;
    if (PartitionRef != 1 || !Volume->MirrorCount ||
        !UdfReadLogicalBlockCopy(Volume, PartitionRef, Block, Buffer, TRUE) ||
        !UdfValidateDescriptor(Buffer, UDF_BLOCK_SIZE, Kind, Block))
        return FALSE;
    if (MirrorUsed) *MirrorUsed = TRUE;
    return TRUE;
}

static BOOLEAN
UdfExtentFits(UDF_VOLUME* Volume, const UDF_EXTENT* Extent)
{
    ULONGLONG Blocks = ((ULONGLONG)Extent->Length + UDF_BLOCK_SIZE - 1) / UDF_BLOCK_SIZE;
    ULONG Limit;
    if (Extent->PartitionRef == 0) Limit = Volume->PartitionBlocks;
    else if (Extent->PartitionRef == 1) Limit = Volume->MetadataBlocks;
    else return FALSE;
    return Extent->Block < Limit && Blocks <= Limit - Extent->Block;
}

static BOOLEAN
UdfGetLongAd(const UCHAR* Buffer, UDF_EXTENT* Extent)
{
    ULONG Length = Udf32(Buffer);
    if (Length >> 30)
        return FALSE;
    Extent->Length = Length;
    Extent->Block = Udf32(Buffer + 4);
    Extent->PartitionRef = Udf16(Buffer + 8);
    return TRUE;
}

static BOOLEAN
UdfParseAds(UDF_VOLUME* Volume, UDF_FILE* File, const UCHAR* Buffer,
            ULONG Start, ULONG Length, ULONG Mode, USHORT ParentRef, ULONG Depth)
{
    ULONG Offset, Step = (Mode == 0) ? 8 : 16;

    if (Depth >= 32 || (Length % Step) != 0 ||
        Start > UDF_BLOCK_SIZE || Length > UDF_BLOCK_SIZE - Start)
        return FALSE;

    for (Offset = Start; Offset < Start + Length; Offset += Step)
    {
        UDF_EXTENT Extent;
        ULONG Value = Udf32(Buffer + Offset);
        ULONG Kind = Value >> 30;

        Extent.Length = Value & 0x3fffffff;
        Extent.Block = Udf32(Buffer + Offset + 4);
        Extent.PartitionRef = (Mode == 0) ? ParentRef : Udf16(Buffer + Offset + 8);
        if (Extent.Length == 0)
            continue;

        if (Kind == 0)
        {
            if (!UdfExtentFits(Volume, &Extent) ||
                File->ExtentCount >= UDF_MAX_EXTENTS)
                return FALSE;
            File->Extents[File->ExtentCount++] = Extent;
        }
        else if (Kind == 3)
        {
            UCHAR Continuation[UDF_BLOCK_SIZE];
            ULONG NextLength;
            if (Extent.Length > UDF_BLOCK_SIZE || Extent.Length < 24 ||
                !UdfReadTaggedBlock(Volume, Extent.PartitionRef, Extent.Block,
                                    258, Continuation, NULL))
                return FALSE;
            NextLength = Udf32(Continuation + 20);
            if (NextLength > Extent.Length - 24)
                return FALSE;
            if (!UdfParseAds(Volume, File, Continuation, 24, NextLength,
                             Mode, ParentRef, Depth + 1))
                return FALSE;
        }
        else
        {
            /* Unrecorded and sparse extents are outside the boot profile. */
            return FALSE;
        }
    }
    return TRUE;
}

static BOOLEAN
UdfLoadFile(UDF_VOLUME* Volume, const UDF_EXTENT* Icb, UDF_FILE* File)
{
    UCHAR Entry[UDF_BLOCK_SIZE];
    ULONG Tag, AttrOffset, AllocOffset, DataOffset, Start, Length, Mode;
    ULONG Index;
    ULONGLONG Allocated = 0;
    BOOLEAN Mirror;

    if (Icb->Length < UDF_BLOCK_SIZE ||
        !UdfReadTaggedBlock(Volume, Icb->PartitionRef, Icb->Block,
                            UDF_DESCRIPTOR_ANY_ID, Entry, &Mirror))
        return FALSE;
    Tag = Udf16(Entry);
    if (Tag == 266)
    {
        AttrOffset = 208;
        AllocOffset = 212;
        DataOffset = 216;
    }
    else if (Tag == 261)
    {
        AttrOffset = 168;
        AllocOffset = 172;
        DataOffset = 176;
    }
    else
        return FALSE;

    RtlZeroMemory(File, sizeof(*File));
    File->IcbBlock = Icb->Block;
    File->MetadataMirror = Mirror;
    File->Size = Udf64(Entry + 56);
    File->Attributes = ReadOnlyFile;
    File->FileType = Entry[27];
    if (Entry[27] == 4)
        File->Attributes |= DirectoryFile;
    else if (Entry[27] != 5 && Entry[27] != 250 && Entry[27] != 251)
        return FALSE;

    Mode = Udf16(Entry + 34) & 7;
    Length = Udf32(Entry + AllocOffset);
    Start = Udf32(Entry + AttrOffset);
    if (Start > UDF_BLOCK_SIZE - DataOffset)
        return FALSE;
    Start += DataOffset;
    if (Length > UDF_BLOCK_SIZE - Start)
        return FALSE;

    if (Mode == 3)
    {
        File->InlineLength = Length;
        RtlCopyMemory(File->Inline, Entry + Start, Length);
        return (File->Size <= Length);
    }
    if (Mode != 0 && Mode != 1)
        return FALSE;
    if (!UdfParseAds(Volume, File, Entry, Start, Length, Mode, Icb->PartitionRef, 0))
        return FALSE;
    for (Index = 0; Index < File->ExtentCount; ++Index)
    {
        if (Index + 1 < File->ExtentCount &&
            (File->Extents[Index].Length % UDF_BLOCK_SIZE))
            return FALSE;
        Allocated += File->Extents[Index].Length;
    }
    return File->Size <= Allocated;
}

static BOOLEAN
UdfReadFileRangeCopy(UDF_VOLUME* Volume, UDF_FILE* File,
                     ULONGLONG Offset, ULONG Length, UCHAR* Output, BOOLEAN Mirror)
{
    ULONG Index;
    UCHAR Sector[UDF_BLOCK_SIZE];

    if (Offset > File->Size || Length > File->Size - Offset)
        return FALSE;
    if (File->InlineLength)
    {
        if (Offset + Length > File->InlineLength)
            return FALSE;
        RtlCopyMemory(Output, File->Inline + (ULONG)Offset, Length);
        return TRUE;
    }

    for (Index = 0; Index < File->ExtentCount && Length; ++Index)
    {
        UDF_EXTENT* Extent = &File->Extents[Index];
        if (Offset >= Extent->Length)
        {
            Offset -= Extent->Length;
            continue;
        }
        while (Length && Offset < Extent->Length)
        {
            ULONG InBlock = (ULONG)(Offset % UDF_BLOCK_SIZE);
            ULONG Transfer = min(Length, UDF_BLOCK_SIZE - InBlock);
            Transfer = min(Transfer, Extent->Length - (ULONG)Offset);
            if (!UdfReadLogicalBlockCopy(Volume, Extent->PartitionRef,
                                         Extent->Block + (ULONG)(Offset / UDF_BLOCK_SIZE),
                                         Sector, Mirror))
                return FALSE;
            RtlCopyMemory(Output, Sector + InBlock, Transfer);
            Output += Transfer;
            Length -= Transfer;
            Offset += Transfer;
        }
        Offset = 0;
    }
    return Length == 0;
}

static BOOLEAN
UdfReadFileRange(UDF_VOLUME* Volume, UDF_FILE* File,
                 ULONGLONG Offset, ULONG Length, UCHAR* Output)
{
    return UdfReadFileRangeCopy(Volume, File, Offset, Length, Output,
                                File->MetadataMirror);
}

static BOOLEAN
UdfFileTagLocation(UDF_FILE* File, ULONG Offset, ULONG* Location)
{
    ULONG Index;
    if (File->InlineLength)
    {
        *Location = File->IcbBlock;
        return TRUE;
    }
    for (Index = 0; Index < File->ExtentCount; ++Index)
    {
        const UDF_EXTENT* Extent = &File->Extents[Index];
        if (Offset < Extent->Length)
        {
            *Location = Extent->Block + Offset / UDF_BLOCK_SIZE;
            return TRUE;
        }
        Offset -= Extent->Length;
    }
    return FALSE;
}

static BOOLEAN
UdfDecodeName(const UCHAR* Source, ULONG Length, CHAR* Target, ULONG Capacity)
{
    ULONG Index, Count;
    if (!Length || !Capacity)
        return FALSE;
    if (Source[0] == 8)
    {
        Count = Length - 1;
        if (Count >= Capacity)
            return FALSE;
        for (Index = 0; Index < Count; ++Index)
        {
            if (Source[Index + 1] == 0 || Source[Index + 1] >= 0x80 ||
                Source[Index + 1] == '/' || Source[Index + 1] == '\\')
                return FALSE;
            Target[Index] = (CHAR)Source[Index + 1];
        }
    }
    else if (Source[0] == 16)
    {
        if ((Length - 1) % 2)
            return FALSE;
        Count = (Length - 1) / 2;
        if (Count >= Capacity)
            return FALSE;
        for (Index = 0; Index < Count; ++Index)
        {
            if (Source[1 + Index * 2] != 0 ||
                Source[2 + Index * 2] == 0 || Source[2 + Index * 2] >= 0x80 ||
                Source[2 + Index * 2] == '/' || Source[2 + Index * 2] == '\\')
                return FALSE;
            Target[Index] = (CHAR)Source[2 + Index * 2];
        }
    }
    else
        return FALSE;
    Target[Count] = ANSI_NULL;
    return TRUE;
}

static BOOLEAN
UdfFindChild(UDF_VOLUME* Volume, const UDF_EXTENT* DirectoryIcb,
             PCSTR Name, UDF_EXTENT* Found)
{
    UDF_FILE* Directory;
    UCHAR* Data;
    ULONG Size, Offset;
    BOOLEAN Result = FALSE;
    BOOLEAN RetriedMirror = FALSE;

    Directory = FrLdrTempAlloc(sizeof(*Directory), TAG_UDF_FILE);
    if (!Directory)
        return FALSE;
    if (!UdfLoadFile(Volume, DirectoryIcb, Directory) ||
        !(Directory->Attributes & DirectoryFile) ||
        Directory->Size > UDF_MAX_DIRECTORY)
        goto FinishFile;

    Size = (ULONG)Directory->Size;
    Data = FrLdrTempAlloc(max(Size, 1), TAG_UDF_DIRECTORY);
    if (!Data)
        goto FinishFile;
ReadDirectory:
    if (!UdfReadFileRange(Volume, Directory, 0, Size, Data))
        goto RetryMirror;

    for (Offset = 0; Offset < Size; )
    {
        UCHAR* Fid = Data + Offset;
        ULONG ImpLength, NameLength, RecordLength, Location;
        CHAR Decoded[256];

        if (Size - Offset < 38 || Udf16(Fid) != 257)
            goto RetryMirror;
        ImpLength = Udf16(Fid + 36);
        NameLength = Fid[19];
        RecordLength = (38 + ImpLength + NameLength + 3) & ~3;
        if (!RecordLength || RecordLength > Size - Offset ||
            !UdfFileTagLocation(Directory, Offset, &Location) ||
            !UdfValidateDescriptor(Fid, RecordLength, 257, Location))
            goto RetryMirror;
        if (!(Fid[18] & (4 | 8)) &&
            UdfDecodeName(Fid + 38 + ImpLength, NameLength,
                          Decoded, sizeof(Decoded)) &&
            !_stricmp(Decoded, Name))
        {
            Result = UdfGetLongAd(Fid + 20, Found);
            break;
        }
        Offset += RecordLength;
    }
    goto FinishData;

RetryMirror:
    /* Retry a damaged metadata directory through the independent copy.
     * Never resurrect deleted FIDs or skip an invalid record. */
    if (!RetriedMirror && !Directory->MetadataMirror && Volume->MirrorCount &&
        !Directory->InlineLength)
    {
        Directory->MetadataMirror = TRUE;
        RetriedMirror = TRUE;
        goto ReadDirectory;
    }

FinishData:
    FrLdrTempFree(Data, TAG_UDF_DIRECTORY);
FinishFile:
    FrLdrTempFree(Directory, TAG_UDF_FILE);
    return Result;
}

static BOOLEAN
UdfLookup(UDF_VOLUME* Volume, PCSTR Path, UDF_EXTENT* Found, CHAR* FinalName)
{
    UDF_EXTENT Current = Volume->RootIcb;
    CHAR Component[256];
    ULONG Length;

    while (*Path == '\\' || *Path == '/')
        ++Path;
    if (!*Path)
        return FALSE;

    while (*Path)
    {
        Length = 0;
        while (Path[Length] && Path[Length] != '\\' && Path[Length] != '/')
        {
            if (Length + 1 >= sizeof(Component))
                return FALSE;
            Component[Length] = Path[Length];
            ++Length;
        }
        Component[Length] = ANSI_NULL;
        if (!UdfFindChild(Volume, &Current, Component, &Current))
            return FALSE;
        RtlCopyMemory(FinalName, Component, Length + 1);
        Path += Length;
        while (*Path == '\\' || *Path == '/')
            ++Path;
    }
    *Found = Current;
    return TRUE;
}

static ARC_STATUS
UdfClose(ULONG FileId)
{
    FrLdrTempFree(FsGetDeviceSpecific(FileId), TAG_UDF_FILE);
    return ESUCCESS;
}

static ARC_STATUS
UdfGetFileInformation(ULONG FileId, FILEINFORMATION* Information)
{
    UDF_FILE* File = FsGetDeviceSpecific(FileId);
    RtlZeroMemory(Information, sizeof(*Information));
    Information->EndingAddress.QuadPart = File->Size;
    Information->CurrentAddress.QuadPart = File->Position;
    Information->Attributes = File->Attributes;
    Information->FileNameLength = min((ULONG)strlen(File->Name),
                                      sizeof(Information->FileName) - 1);
    RtlCopyMemory(Information->FileName, File->Name, Information->FileNameLength + 1);
    return ESUCCESS;
}

static ARC_STATUS
UdfOpen(CHAR* Path, OPENMODE OpenMode, ULONG* FileId)
{
    UDF_VOLUME* Volume = UdfVolumes[FsGetDeviceId(*FileId)];
    UDF_EXTENT Icb;
    UDF_FILE* File;
    CHAR Name[256];

    if (OpenMode != OpenReadOnly)
        return EACCES;
    if (!UdfLookup(Volume, Path, &Icb, Name))
        return ENOENT;
    File = FrLdrTempAlloc(sizeof(*File), TAG_UDF_FILE);
    if (!File)
        return ENOMEM;
    if (!UdfLoadFile(Volume, &Icb, File))
    {
        FrLdrTempFree(File, TAG_UDF_FILE);
        return EIO;
    }
    RtlCopyMemory(File->Name, Name, strlen(Name) + 1);
    FsSetDeviceSpecific(*FileId, File);
    return ESUCCESS;
}

static ARC_STATUS
UdfRead(ULONG FileId, VOID* Buffer, ULONG N, ULONG* Count)
{
    UDF_FILE* File = FsGetDeviceSpecific(FileId);
    UDF_VOLUME* Volume = UdfVolumes[FsGetDeviceId(FileId)];

    *Count = 0;
    if (File->Position >= File->Size)
        return ESUCCESS;
    N = (ULONG)min((ULONGLONG)N, File->Size - File->Position);
    if (!UdfReadFileRange(Volume, File, File->Position, N, Buffer))
        return EIO;
    File->Position += N;
    *Count = N;
    return ESUCCESS;
}

static ARC_STATUS
UdfSeek(ULONG FileId, LARGE_INTEGER* Position, SEEKMODE Mode)
{
    UDF_FILE* File = FsGetDeviceSpecific(FileId);
    LONGLONG NewPosition = Position->QuadPart;

    if (Mode == SeekRelative)
        NewPosition += File->Position;
    else if (Mode != SeekAbsolute)
        return EINVAL;
    if (NewPosition < 0 || (ULONGLONG)NewPosition > File->Size)
        return EINVAL;
    File->Position = (ULONGLONG)NewPosition;
    return ESUCCESS;
}

ULONGLONG
UdfGetVolumeSize(ULONG DeviceId)
{
    UDF_VOLUME* Volume = UdfVolumes[DeviceId];
    ASSERT(Volume);
    return (ULONGLONG)Volume->PartitionBlocks * UDF_BLOCK_SIZE;
}

static const DEVVTBL UdfFuncTable =
{
    UdfClose,
    UdfGetFileInformation,
    UdfOpen,
    UdfRead,
    UdfSeek,
    L"udfs",
};

/* 1 = complete supported sequence; 0 = damaged/unreadable; -1 = a valid
 * descriptor requiring a different topology. Only damage permits reserve
 * VDS fallback, matching the pre-mount recovery selector. */
static int
UdfReadVds(UDF_VOLUME* Volume, ULONG Start, ULONG Bytes, UCHAR* Lvd)
{
    UCHAR Block[UDF_BLOCK_SIZE], Partition[UDF_BLOCK_SIZE];
    ULONG Index, Count, PvdCount = 0, PdCount = 0, LvdCount = 0;
    ULONG Kind, PartitionStart, PartitionBlocks;

    if (!Bytes || (Bytes % UDF_BLOCK_SIZE)) return 0;
    if (Bytes > 256 * UDF_BLOCK_SIZE) return -1;
    Count = Bytes / UDF_BLOCK_SIZE;
    if (Start >= Volume->SectorCount || Count > Volume->SectorCount - Start)
        return 0;
    for (Index = 0; Index < Count; ++Index)
    {
        if (!UdfReadPhysicalBlock(Volume, Start + Index, Block)) return 0;
        Kind = Udf16(Block);
        if (!UdfValidateDescriptor(Block, sizeof(Block), Kind, Start + Index)) return 0;
        switch (Kind)
        {
        case 1:
            if (++PvdCount != 1) return -1;
            break;
        case 4:
        case 7:
            break;
        case 5:
            if (++PdCount != 1) return -1;
            RtlCopyMemory(Partition, Block, sizeof(Partition));
            break;
        case 6:
            if (++LvdCount != 1) return -1;
            RtlCopyMemory(Lvd, Block, UDF_BLOCK_SIZE);
            break;
        case 8:
            if (!PvdCount || !PdCount || !LvdCount) return 0;
            PartitionStart = Udf32(Partition + 188);
            PartitionBlocks = Udf32(Partition + 192);
            if (!PartitionBlocks || PartitionStart >= Volume->SectorCount ||
                PartitionBlocks > Volume->SectorCount - PartitionStart)
                return 0;
            if (Udf32(Lvd + 212) != UDF_BLOCK_SIZE ||
                !RtlEqualMemory(Lvd + 217, "*OSTA UDF Compliant", 19) ||
                Udf16(Lvd + 240) != 0x0250 ||
                Udf32(Lvd + 268) != 2 || Udf32(Lvd + 264) != 70 ||
                Lvd[440] != 1 || Lvd[441] != 6 ||
                Udf16(Lvd + 442) != 1 ||
                Udf16(Lvd + 444) != Udf16(Partition + 22) ||
                Lvd[446] != 2 || Lvd[447] != 64 ||
                Udf16(Lvd + 482) != 1 ||
                Udf16(Lvd + 484) != Udf16(Partition + 22) ||
                !RtlEqualMemory(Lvd + 451, "*UDF Metadata Partition", 23) ||
                !Udf32(Lvd + 498) || (Udf32(Lvd + 498) % 32) ||
                !Udf16(Lvd + 502))
                return -1;
            Volume->PartitionStart = PartitionStart;
            Volume->PartitionBlocks = PartitionBlocks;
            return 1;
        default:
            /* VDS continuation and multiple partitions are outside this
             * boot profile; do not choose an older alternate view. */
            return -1;
        }
    }
    return 0;
}

static BOOLEAN
UdfSelectVds(UDF_VOLUME* Volume, UCHAR* Lvd)
{
    UCHAR Anchor[UDF_BLOCK_SIZE];
    ULONG Last = Volume->SectorCount - 1;
    ULONG Anchors[8] = {256, 512, Last - 256, Last - 255,
                        Last - 258, Last - 2, Last, Last - 512};
    ULONG Index, MainStart, MainBytes, ReserveStart, ReserveBytes;
    int Status;

    for (Index = 0; Index < 8; ++Index)
    {
        if (!Anchors[Index] || Anchors[Index] >= Volume->SectorCount ||
            !UdfReadPhysicalBlock(Volume, Anchors[Index], Anchor) ||
            !UdfValidateDescriptor(Anchor, sizeof(Anchor), 2, Anchors[Index]))
            continue;
        MainBytes = Udf32(Anchor + 16);
        MainStart = Udf32(Anchor + 20);
        ReserveBytes = Udf32(Anchor + 24);
        ReserveStart = Udf32(Anchor + 28);
        Status = UdfReadVds(Volume, MainStart, MainBytes, Lvd);
        if (Status == 1) return TRUE;
        if (Status == -1) return FALSE;
        /* Once an anchor validates, never search another anchor merely
         * because its VDS or filesystem contents do not validate. */
        return UdfReadVds(Volume, ReserveStart, ReserveBytes, Lvd) == 1;
    }
    return FALSE;
}

static BOOLEAN
UdfMetadataExtentsValid(UDF_FILE* Metadata, ULONG Unit, ULONG Alignment)
{
    ULONG Index, Other;
    ULONGLONG Bytes = 0;
    if (!Metadata->ExtentCount || (Metadata->Size % UDF_BLOCK_SIZE)) return FALSE;
    for (Index = 0; Index < Metadata->ExtentCount; ++Index)
    {
        if (Metadata->Extents[Index].PartitionRef != 0 ||
            (Metadata->Extents[Index].Length % UDF_BLOCK_SIZE) ||
            (Metadata->Extents[Index].Block % Alignment) ||
            (Metadata->Extents[Index].Length / UDF_BLOCK_SIZE) % Unit)
            return FALSE;
        for (Other = 0; Other < Index; ++Other)
        {
            const UDF_EXTENT* A = &Metadata->Extents[Index];
            const UDF_EXTENT* B = &Metadata->Extents[Other];
            if ((ULONGLONG)A->Block < (ULONGLONG)B->Block + B->Length / UDF_BLOCK_SIZE &&
                (ULONGLONG)B->Block < (ULONGLONG)A->Block + A->Length / UDF_BLOCK_SIZE)
                return FALSE;
        }
        Bytes += Metadata->Extents[Index].Length;
    }
    return Bytes == Metadata->Size && Bytes / UDF_BLOCK_SIZE <= MAXULONG;
}

const DEVVTBL*
UdfMount(ULONG DeviceId)
{
    UDF_VOLUME* Volume;
    UDF_FILE* Metadata;
    UCHAR Lvd[UDF_BLOCK_SIZE], Fileset[UDF_BLOCK_SIZE];
    ULONG Index, Other;
    FILEINFORMATION DeviceInformation;
    ULONGLONG SectorCount;
    BOOLEAN HaveMain = FALSE, HaveMirror = FALSE;
    UDF_EXTENT FilesetIcb, MetadataIcb;

    if (DeviceId >= MAX_FDS)
        return NULL;
    if (ArcGetFileInformation(DeviceId, &DeviceInformation) != ESUCCESS ||
        DeviceInformation.EndingAddress.QuadPart <= DeviceInformation.StartingAddress.QuadPart)
        return NULL;
    SectorCount = DeviceInformation.EndingAddress.QuadPart -
                  DeviceInformation.StartingAddress.QuadPart;
    if ((SectorCount % UDF_BLOCK_SIZE) || SectorCount / UDF_BLOCK_SIZE > MAXULONG ||
        SectorCount / UDF_BLOCK_SIZE <= 256)
        return NULL;
    SectorCount /= UDF_BLOCK_SIZE;
    Volume = FrLdrTempAlloc(sizeof(*Volume), TAG_UDF_VOLUME);
    if (!Volume)
        return NULL;
    RtlZeroMemory(Volume, sizeof(*Volume));
    Volume->DeviceId = DeviceId;
    Volume->SectorCount = (ULONG)SectorCount;

    if (!UdfSelectVds(Volume, Lvd))
        goto Failure;

    Metadata = FrLdrTempAlloc(sizeof(*Metadata), TAG_UDF_FILE);
    if (!Metadata)
        goto Failure;
    MetadataIcb.Length = UDF_BLOCK_SIZE;
    MetadataIcb.Block = Udf32(Lvd + 486);
    MetadataIcb.PartitionRef = 0;
    if (UdfLoadFile(Volume, &MetadataIcb, Metadata) &&
        Metadata->FileType == 250 &&
        UdfMetadataExtentsValid(Metadata, Udf32(Lvd + 498), Udf16(Lvd + 502)))
    {
        HaveMain = TRUE;
        Volume->MetadataBlocks = (ULONG)(Metadata->Size / UDF_BLOCK_SIZE);
        Volume->MetadataCount = Metadata->ExtentCount;
        RtlCopyMemory(Volume->Metadata, Metadata->Extents,
                       Metadata->ExtentCount * sizeof(UDF_EXTENT));
    }
    MetadataIcb.Block = Udf32(Lvd + 490);
    if (UdfLoadFile(Volume, &MetadataIcb, Metadata) &&
        Metadata->FileType == 251 &&
        UdfMetadataExtentsValid(Metadata, Udf32(Lvd + 498), Udf16(Lvd + 502)))
    {
        if (HaveMain && Metadata->Size / UDF_BLOCK_SIZE != Volume->MetadataBlocks)
            goto FailureMetadata;
        HaveMirror = TRUE;
        if (!HaveMain)
        {
            Volume->MetadataBlocks = (ULONG)(Metadata->Size / UDF_BLOCK_SIZE);
            Volume->MetadataCount = Metadata->ExtentCount;
            RtlCopyMemory(Volume->Metadata, Metadata->Extents,
                           Metadata->ExtentCount * sizeof(UDF_EXTENT));
        }
        else if (Lvd[504] & 1)
        {
            Volume->MirrorCount = Metadata->ExtentCount;
            RtlCopyMemory(Volume->Mirror, Metadata->Extents,
                           Metadata->ExtentCount * sizeof(UDF_EXTENT));
            for (Index = 0; Index < Volume->MetadataCount; ++Index)
                for (Other = 0; Other < Volume->MirrorCount; ++Other)
                {
                    const UDF_EXTENT* A = &Volume->Metadata[Index];
                    const UDF_EXTENT* B = &Volume->Mirror[Other];
                    if ((ULONGLONG)A->Block < (ULONGLONG)B->Block + B->Length / UDF_BLOCK_SIZE &&
                        (ULONGLONG)B->Block < (ULONGLONG)A->Block + A->Length / UDF_BLOCK_SIZE)
                        goto FailureMetadata;
                }
        }
        else
        {
            /* Without DUP, these are two descriptors of the same payload.
             * Distinct mappings do not define a safe fallback generation. */
            if (Metadata->ExtentCount != Volume->MetadataCount)
                goto FailureMetadata;
            for (Index = 0; Index < Volume->MetadataCount; ++Index)
                if (Metadata->Extents[Index].Block != Volume->Metadata[Index].Block ||
                    Metadata->Extents[Index].Length != Volume->Metadata[Index].Length ||
                    Metadata->Extents[Index].PartitionRef != Volume->Metadata[Index].PartitionRef)
                    goto FailureMetadata;
        }
    }
    if (!HaveMain && !HaveMirror) goto FailureMetadata;
    FrLdrTempFree(Metadata, TAG_UDF_FILE);

    if (!UdfGetLongAd(Lvd + 248, &FilesetIcb) ||
        FilesetIcb.Length < UDF_BLOCK_SIZE ||
        !UdfReadTaggedBlock(Volume, FilesetIcb.PartitionRef,
                            FilesetIcb.Block, 256, Fileset, NULL) ||
        !UdfGetLongAd(Fileset + 400, &Volume->RootIcb))
        goto Failure;

    UdfVolumes[DeviceId] = Volume;
    TRACE("Mounted UDF 2.50 metadata partition on device %lu\n", DeviceId);
    return &UdfFuncTable;

FailureMetadata:
    FrLdrTempFree(Metadata, TAG_UDF_FILE);
Failure:
    FrLdrTempFree(Volume, TAG_UDF_VOLUME);
    return NULL;
}
