////////////////////////////////////////////////////////////////////
// Copyright (C) Alexander Telyatnikov, Ivan Keliukh, Yegor Anchishkin, SKIF Software, 1999-2013. Kiev, Ukraine
// All rights reserved
// This file was released under the GPLv2 on June 2015.
////////////////////////////////////////////////////////////////////

#ifndef __MY_MEM_H__
#define __MY_MEM_H__

#define MyFreeMemoryAndPointer(ptr) \
    if (ptr) {                   \
        MyFreePool__(ptr);      \
        ptr = NULL;             \
    }

#define MY_HEAP_ALIGN             63

/* The debug allocator also accepts only ULONG bytes. Check the original
 * wide size before aligning or narrowing it; a wrapped request must not
 * become a small successful pool allocation. */
static __inline ULONG MyCheckedAlignSize__(ULONGLONG Size)
{
    if (!Size || Size > MAXULONG - MY_HEAP_ALIGN)
        return 0;
    return (ULONG)((Size + MY_HEAP_ALIGN) & ~((ULONGLONG)MY_HEAP_ALIGN));
}
#define MyAlignSize__(size) MyCheckedAlignSize__((ULONGLONG)(size))

#define PAGE_SIZE_ALIGN             (PAGE_SIZE - 1)
#define AlignToPageSize(size) (((size)+PAGE_SIZE_ALIGN)&(~PAGE_SIZE_ALIGN))

static inline BOOLEAN MyAllocInit(VOID) {return TRUE;}
#define MyAllocRelease()

static __inline PVOID MyAllocateSizedPool__(ULONGLONG Size, ULONG Tag
#ifdef TRACK_SYS_ALLOC_CALLERS
    , ULONG Source, ULONG Line
#endif
    )
{
    ULONG Aligned = MyCheckedAlignSize__(Size);
    if (!Aligned)
        return NULL;
#ifdef TRACK_SYS_ALLOC_CALLERS
    (void)Tag;
    return DebugAllocatePool(NonPagedPool, Aligned, Source, Line);
#else
    return DbgAllocatePoolWithTag(NonPagedPool, Aligned, Tag);
#endif
}

#ifdef TRACK_SYS_ALLOC_CALLERS
  #define MyAllocatePool__(type,size) MyAllocateSizedPool__((ULONGLONG)(size), 'fNWD', UDF_BUG_CHECK_ID, __LINE__)
  #define MyAllocatePoolTag__(type,size,tag) MyAllocateSizedPool__((ULONGLONG)(size), tag, UDF_BUG_CHECK_ID, __LINE__)
#else //TRACK_SYS_ALLOC_CALLERS
  #define MyAllocatePool__(type,size) MyAllocateSizedPool__((ULONGLONG)(size), 'fNWD')
  #define MyAllocatePoolTag__(type,size,tag) MyAllocateSizedPool__((ULONGLONG)(size), tag)
#endif //TRACK_SYS_ALLOC_CALLERS
#define MyFreePool__(addr) DbgFreePool((PCHAR)(addr))

/* This function just scares the hell out of GCC */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-overflow"
#endif

/* Preserve the old owner and the output pointer on every failure. Zero
 * growth in the same alignment bucket too, not only after moving it. */
static __inline ULONG MyReallocPool__(PCHAR addr, ULONGLONG len,
                                      PCHAR *pnewaddr, ULONGLONG newlen)
{
    ULONG OldAligned, NewAligned;
    PCHAR NewAddress;

    if (!pnewaddr)
        return 0;
    *pnewaddr = addr;
    OldAligned = MyCheckedAlignSize__(len);
    NewAligned = MyCheckedAlignSize__(newlen);
    if (!addr || !OldAligned || !NewAligned)
        return 0;
    if (OldAligned != NewAligned) {
#ifdef TRACK_SYS_ALLOC_CALLERS
        NewAddress = (PCHAR)MyAllocateSizedPool__(newlen, 'fNWD', 0x202, __LINE__);
#else
        NewAddress = (PCHAR)MyAllocateSizedPool__(newlen, 'fNWD');
#endif
        if (!NewAddress)
            return 0;
        RtlCopyMemory(NewAddress, addr, (SIZE_T)min(len, newlen));
        if (newlen > len)
            RtlZeroMemory(NewAddress + (SIZE_T)len, NewAligned - (SIZE_T)len);
        *pnewaddr = NewAddress;
        MyFreePool__(addr);
    } else if (newlen > len) {
        RtlZeroMemory(addr + (SIZE_T)len, (SIZE_T)(newlen - len));
    }
    return (ULONG)newlen;
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

#define MyCheckArray(base, index)

#endif // __MY_MEM_H__
