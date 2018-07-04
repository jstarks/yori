/**
 * @file copy/copy.c
 *
 * Yori shell perform simple math operations
 *
 * Copyright (c) 2017 Malcolm J. Smith
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, COPYESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <yoripch.h>
#include <yorilib.h>

#ifndef SE_CREATE_SYMBOLIC_LINK_NAME
/**
 If the compilation environment hasn't defined it, define the privilege
 for creating symbolic links.
 */
#define SE_CREATE_SYMBOLIC_LINK_NAME _T("SeCreateSymbolicLinkPrivilege")
#endif

#ifndef FILE_FLAG_OPEN_NO_RECALL
/**
 If the compilation environment hasn't defined it, define the flag for not
 recalling objects from slow storage.
 */
#define FILE_FLAG_OPEN_NO_RECALL 0x100000
#endif

/**
 Help text to display to the user.
 */
const
CHAR strHelpText[] =
        "\n"
        "Copies one or more files.\n"
        "\n"
        "COPY [-b] [-l] [-s] [-v] <src>\n"
        "COPY [-b] [-l] [-s] [-v] <src> [<src> ...] <dest>\n"
        "\n"
        "   -b             Use basic search criteria for files only\n"
        "   -l             Copy links as links rather than contents\n"
        "   -s             Copy subdirectories as well as files\n"
        "   -v             Verbose output\n";

/**
 Display usage text to the user.
 */
BOOL
CopyHelp()
{
    YORI_STRING License;

    YoriLibMitLicenseText(_T("2017-2018"), &License);

    YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("Copy %i.%i\n"), COPY_VER_MAJOR, COPY_VER_MINOR);
#if YORI_BUILD_ID
    YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("  Build %i\n"), YORI_BUILD_ID);
#endif
    YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("%hs\n"), strHelpText);
    YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("%y"), &License);
    YoriLibFreeStringContents(&License);
    return TRUE;
}

/**
 A context passed between each source file match when copying multiple
 files.
 */
typedef struct _COPY_CONTEXT {
    /**
     Path to the destination for the copy operation.
     */
    YORI_STRING Dest;

    /**
     The file system attributes of the destination.  Used to determine if
     the destination exists and is a directory.
     */
    DWORD DestAttributes;

    /**
     The number of files that have been previously copied to this
     destination.  This can be used to determine if we're about to copy
     a second object over the top of an earlier copied file.
     */
    DWORD FilesCopied;

    /**
     If TRUE, links are copied as links rather than having their contents
     copied.
     */
    BOOL CopyAsLinks;

    /**
     If TRUE, output is generated for each object copied.
     */
    BOOL Verbose;
} COPY_CONTEXT, *PCOPY_CONTEXT;


/**
 Copy a single file from the source to the target by preserving its link
 contents.

 @param SourceFileName A NULL terminated string specifying the source file.

 @param DestFileName A NULL terminated string specifying the destination file.

 @param IsDirectory TRUE if the object being copied is a directory, FALSE if
        it is not.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOL
CopyAsLink(
    __in LPTSTR SourceFileName,
    __in LPTSTR DestFileName,
    __in BOOL IsDirectory
    )
{
    PVOID ReparseData;
    HANDLE SourceFileHandle;
    HANDLE DestFileHandle;
    DWORD LastError;
    DWORD BytesReturned;
    LPTSTR ErrText;

    SourceFileHandle = CreateFile(SourceFileName,
                                  GENERIC_READ,
                                  FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                                  NULL,
                                  OPEN_EXISTING,
                                  FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_OPEN_NO_RECALL|FILE_FLAG_BACKUP_SEMANTICS,
                                  NULL);

    if (SourceFileHandle == INVALID_HANDLE_VALUE) {
        LastError = GetLastError();
        ErrText = YoriLibGetWinErrorText(LastError);
        YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("Open of source failed: %s: %s"), SourceFileName, ErrText);
        YoriLibFreeWinErrorText(ErrText);
        return FALSE;
    }

    if (IsDirectory) {
        if (!CreateDirectory(DestFileName, NULL)) {
            LastError = GetLastError();
            if (LastError != ERROR_ALREADY_EXISTS) {
                ErrText = YoriLibGetWinErrorText(LastError);
                YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("Create of destination failed: %s: %s"), DestFileName, ErrText);
                YoriLibFreeWinErrorText(ErrText);
                CloseHandle(SourceFileHandle);
                return FALSE;
            }
        }

        DestFileHandle = CreateFile(DestFileName,
                                    GENERIC_WRITE,
                                    FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                                    NULL,
                                    OPEN_EXISTING,
                                    FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_OPEN_NO_RECALL|FILE_FLAG_BACKUP_SEMANTICS,
                                    NULL);
    
        if (DestFileHandle == INVALID_HANDLE_VALUE) {
            LastError = GetLastError();
            ErrText = YoriLibGetWinErrorText(LastError);
            YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("Open of destination failed: %s: %s"), DestFileName, ErrText);
            YoriLibFreeWinErrorText(ErrText);
            CloseHandle(SourceFileHandle);
            RemoveDirectory(DestFileName);
            return FALSE;
        }
    } else {
        DestFileHandle = CreateFile(DestFileName,
                                    GENERIC_WRITE,
                                    FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                                    NULL,
                                    CREATE_ALWAYS,
                                    FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_OPEN_NO_RECALL|FILE_FLAG_BACKUP_SEMANTICS,
                                    NULL);
    
        if (DestFileHandle == INVALID_HANDLE_VALUE) {
            LastError = GetLastError();
            ErrText = YoriLibGetWinErrorText(LastError);
            YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("Open of destination failed: %s: %s"), DestFileName, ErrText);
            YoriLibFreeWinErrorText(ErrText);
            CloseHandle(SourceFileHandle);
            return FALSE;
        }
    }

    ReparseData = YoriLibMalloc(64 * 1024);
    if (ReparseData == NULL) {
        CloseHandle(DestFileHandle);
        CloseHandle(SourceFileHandle);
        if (IsDirectory) {
            RemoveDirectory(DestFileName);
        } else {
            DeleteFile(DestFileName);
        }
        return FALSE;
    }

    if (!DeviceIoControl(SourceFileHandle, FSCTL_GET_REPARSE_POINT, NULL, 0, ReparseData, 64 * 1024, &BytesReturned, NULL)) {
        LastError = GetLastError();
        ErrText = YoriLibGetWinErrorText(LastError);
        YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("Querying reparse data of source failed: %s: %s"), SourceFileName, ErrText);
        YoriLibFreeWinErrorText(ErrText);
        CloseHandle(SourceFileHandle);
        CloseHandle(DestFileHandle);
        YoriLibFree(ReparseData);
        if (IsDirectory) {
            RemoveDirectory(DestFileName);
        } else {
            DeleteFile(DestFileName);
        }
        return FALSE;
    }

    if (!DeviceIoControl(DestFileHandle, FSCTL_SET_REPARSE_POINT, ReparseData, BytesReturned, NULL, 0, &BytesReturned, NULL)) {
        LastError = GetLastError();
        ErrText = YoriLibGetWinErrorText(LastError);
        YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("Setting reparse data on dest failed: %s: %s"), DestFileName, ErrText);
        YoriLibFreeWinErrorText(ErrText);
        CloseHandle(SourceFileHandle);
        CloseHandle(DestFileHandle);
        YoriLibFree(ReparseData);
        if (IsDirectory) {
            RemoveDirectory(DestFileName);
        } else {
            DeleteFile(DestFileName);
        }
        return FALSE;
    }

    CloseHandle(SourceFileHandle);
    CloseHandle(DestFileHandle);
    YoriLibFree(ReparseData);
    return TRUE;
}

/**
 A callback that is invoked when a file is found that matches a search criteria
 specified in the set of strings to enumerate.

 @param FilePath Pointer to the file path that was found.

 @param FileInfo Information about the file.

 @param Depth Indicates the recursion depth.  Used by copy to check if it
        needs to create new directories in the destination path.

 @param Context Pointer to a context block specifying the destination of the
        copy, indicating parameters to the copy operation, and tracking how
        many objects have been copied.

 @return TRUE to continute enumerating, FALSE to abort.
 */
BOOL
CopyFileFoundCallback(
    __in PYORI_STRING FilePath,
    __in PWIN32_FIND_DATA FileInfo,
    __in DWORD Depth,
    __in PVOID Context
    )
{
    PCOPY_CONTEXT CopyContext = (PCOPY_CONTEXT)Context;
    YORI_STRING FullDest;

    ASSERT(FilePath->StartOfString[FilePath->LengthInChars] == '\0');

    YoriLibInitEmptyString(&FullDest);

    if (CopyContext->DestAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        YORI_STRING DestWithFile;
        YORI_STRING TrailingPath;
        DWORD SlashesFound;
        DWORD Index;

        SlashesFound = 0;
        for (Index = FilePath->LengthInChars; Index > 0; Index--) {
            if (FilePath->StartOfString[Index - 1] == '\\') {
                SlashesFound++;
                if (SlashesFound == Depth + 1) {
                    break;
                }
            }
        }

        ASSERT(Index > 0);
        ASSERT(SlashesFound == Depth + 1);

        YoriLibInitEmptyString(&TrailingPath);
        TrailingPath.StartOfString = &FilePath->StartOfString[Index];
        TrailingPath.LengthInChars = FilePath->LengthInChars - Index;

        if (!YoriLibAllocateString(&DestWithFile, CopyContext->Dest.LengthInChars + 1 + TrailingPath.LengthInChars + 1)) {
            return FALSE;
        }
        DestWithFile.LengthInChars = YoriLibSPrintf(DestWithFile.StartOfString, _T("%y\\%y"), &CopyContext->Dest, &TrailingPath);
        if (!YoriLibGetFullPathNameReturnAllocation(&DestWithFile, TRUE, &FullDest, NULL)) {
            return FALSE;
        }
        YoriLibFreeStringContents(&DestWithFile);
    } else {
        if (!YoriLibGetFullPathNameReturnAllocation(&CopyContext->Dest, TRUE, &FullDest, NULL)) {
            return FALSE;
        }
        if (CopyContext->FilesCopied > 0) {
            YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("Attempting to copy multiple files over a single file (%s)\n"), FullDest.StartOfString);
            YoriLibFreeStringContents(&FullDest);
            return FALSE;
        }
    }

    if (CopyContext->Verbose) {
        YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("Copying %y to %s\n"), FilePath, FullDest.StartOfString);
    }

    if (FileInfo->dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT &&
        CopyContext->CopyAsLinks &&
        (FileInfo->dwReserved0 == IO_REPARSE_TAG_MOUNT_POINT || FileInfo->dwReserved0 == IO_REPARSE_TAG_SYMLINK)) {

        CopyAsLink(FilePath->StartOfString, FullDest.StartOfString, ((FileInfo->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0));

    } else if (FileInfo->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        if (!CreateDirectory(FullDest.StartOfString, NULL)) {
            DWORD LastError = GetLastError();
            if (LastError != ERROR_ALREADY_EXISTS) {
                LPTSTR ErrText = YoriLibGetWinErrorText(LastError);
                YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("CreateDirectory failed: %s: %s"), FullDest.StartOfString, ErrText);
                YoriLibFreeWinErrorText(ErrText);
            }
        }
    } else {
        if (!CopyFile(FilePath->StartOfString, FullDest.StartOfString, FALSE)) {
            DWORD LastError = GetLastError();
            LPTSTR ErrText = YoriLibGetWinErrorText(LastError);
            YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("CopyFile failed: %y: %s"), FilePath, ErrText);
            YoriLibFreeWinErrorText(ErrText);
        }
    }

    CopyContext->FilesCopied++;
    YoriLibFreeStringContents(&FullDest);
    return TRUE;
}

/**
 If the privilege for creating symbolic links is available to the process,
 enable it.

 @return TRUE to indicate the privilege was enabled, FALSE if it was not.
 */
BOOL
CopyEnableSymlinkPrivilege()
{
    TOKEN_PRIVILEGES TokenPrivileges;
    LUID SymlinkLuid;
    HANDLE TokenHandle;

    YoriLibLoadAdvApi32Functions();
    if (DllAdvApi32.pLookupPrivilegeValueW == NULL ||
        DllAdvApi32.pOpenProcessToken == NULL ||
        DllAdvApi32.pAdjustTokenPrivileges == NULL) {

        return FALSE;
    }

    if (!DllAdvApi32.pLookupPrivilegeValueW(NULL, SE_CREATE_SYMBOLIC_LINK_NAME, &SymlinkLuid)) {
        return FALSE;
    }

    if (!DllAdvApi32.pOpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &TokenHandle)) {
        return FALSE;
    }

    TokenPrivileges.PrivilegeCount = 1;
    TokenPrivileges.Privileges[0].Luid = SymlinkLuid;
    TokenPrivileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    if (!DllAdvApi32.pAdjustTokenPrivileges(TokenHandle, FALSE, &TokenPrivileges, 0, NULL, 0)) {
        CloseHandle(TokenHandle);
        return FALSE;
    }

    return TRUE;
}

/**
 The main entrypoint for the copy cmdlet.

 @param ArgC The number of arguments.

 @param ArgV An array of arguments.

 @return Exit code of the child process on success, or failure if the child
         could not be launched.
 */
DWORD
ymain(
    __in DWORD ArgC,
    __in YORI_STRING ArgV[]
    )
{
    BOOL ArgumentUnderstood;
    DWORD FilesProcessed;
    DWORD FileCount;
    DWORD LastFileArg = 0;
    DWORD MatchFlags;
    BOOL AllocatedDest;
    BOOL BasicEnumeration;
    BOOL Recursive;
    DWORD i;
    DWORD Result;
    COPY_CONTEXT CopyContext;
    YORI_STRING Arg;

    FileCount = 0;
    Recursive = FALSE;
    AllocatedDest = FALSE;
    BasicEnumeration = FALSE;
    ZeroMemory(&CopyContext, sizeof(CopyContext));

    for (i = 1; i < ArgC; i++) {

        ASSERT(YoriLibIsStringNullTerminated(&ArgV[i]));
        ArgumentUnderstood = FALSE;

        if (YoriLibIsCommandLineOption(&ArgV[i], &Arg)) {

            if (YoriLibCompareStringWithLiteralInsensitive(&Arg, _T("?")) == 0) {
                CopyHelp();
                return EXIT_SUCCESS;
            } else if (YoriLibCompareStringWithLiteralInsensitive(&Arg, _T("b")) == 0) {
                BasicEnumeration = TRUE;
                ArgumentUnderstood = TRUE;
            } else if (YoriLibCompareStringWithLiteralInsensitive(&Arg, _T("l")) == 0) {
                CopyContext.CopyAsLinks = TRUE;
                ArgumentUnderstood = TRUE;
            } else if (YoriLibCompareStringWithLiteralInsensitive(&Arg, _T("s")) == 0) {
                Recursive = TRUE;
                ArgumentUnderstood = TRUE;
            } else if (YoriLibCompareStringWithLiteralInsensitive(&Arg, _T("v")) == 0) {
                CopyContext.Verbose = TRUE;
                ArgumentUnderstood = TRUE;
            }
        } else {
            ArgumentUnderstood = TRUE;
            FileCount++;
            LastFileArg = i;
        }

        if (!ArgumentUnderstood) {
            YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("Argument not understood, ignored: %y\n"), &ArgV[i]);
        }
    }

    if (FileCount == 0) {
        YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("copy: argument missing\n"));
        return EXIT_FAILURE;
    } else if (FileCount == 1) {
        YoriLibConstantString(&CopyContext.Dest, _T("."));
    } else {
        if (!YoriLibUserStringToSingleFilePath(&ArgV[LastFileArg], TRUE, &CopyContext.Dest)) {
            YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("copy: could not resolve %y\n"), &ArgV[LastFileArg]);
            return EXIT_FAILURE;
        }
        AllocatedDest = TRUE;
        FileCount--;
    }

    ASSERT(YoriLibIsStringNullTerminated(&CopyContext.Dest));

    if (CopyContext.CopyAsLinks) {
        if (!CopyEnableSymlinkPrivilege()) {
            YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("copy: warning: could not enable symlink privilege\n"));
        }
    }

    CopyContext.DestAttributes = GetFileAttributes(CopyContext.Dest.StartOfString);
    if (CopyContext.DestAttributes == 0xFFFFFFFF) {
        if (Recursive) {
            if (!CreateDirectory(CopyContext.Dest.StartOfString, NULL)) {
                DWORD LastError = GetLastError();
                LPTSTR ErrText = YoriLibGetWinErrorText(LastError);
                YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("CreateDirectory failed: %y: %s"), &CopyContext.Dest, ErrText);
                YoriLibFreeWinErrorText(ErrText);
                return EXIT_FAILURE;
            }
            CopyContext.DestAttributes = GetFileAttributes(CopyContext.Dest.StartOfString);
        }

        if (CopyContext.DestAttributes == 0xFFFFFFFF) {
            CopyContext.DestAttributes = 0;
        }
    }
    CopyContext.FilesCopied = 0;

    FilesProcessed = 0;

    for (i = 1; i < ArgC; i++) {
        if (!YoriLibIsCommandLineOption(&ArgV[i], &Arg)) {
            MatchFlags = YORILIB_FILEENUM_RETURN_FILES | YORILIB_FILEENUM_DIRECTORY_CONTENTS;
            if (BasicEnumeration) {
                MatchFlags |= YORILIB_FILEENUM_BASIC_EXPANSION;
            }
            if (Recursive) {
                MatchFlags |= YORILIB_FILEENUM_RECURSE_AFTER_RETURN | YORILIB_FILEENUM_RETURN_DIRECTORIES;
                if (CopyContext.CopyAsLinks) {
                    MatchFlags |= YORILIB_FILEENUM_NO_LINK_TRAVERSE;
                }
            }

            YoriLibForEachFile(&ArgV[i], MatchFlags, 0, CopyFileFoundCallback, &CopyContext);
            FilesProcessed++;
            if (FilesProcessed == FileCount) {
                break;
            }
        }
    }

    Result = EXIT_SUCCESS;

    if (CopyContext.FilesCopied == 0) {
        YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("copy: no matching files found\n"));
        Result = EXIT_FAILURE;
    }

    if (AllocatedDest) {
        YoriLibFreeStringContents(&CopyContext.Dest);
    }

    return Result;
}

// vim:sw=4:ts=4:et:
