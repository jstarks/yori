/**
 * @file move/move.c
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
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, MOVEESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <yoripch.h>
#include <yorilib.h>

/**
 Help text to display to the user.
 */
const
CHAR strHelpText[] =
        "\n"
        "Moves or renames one or more files.\n"
        "\n"
        "MOVE [-b] <src>\n"
        "MOVE [-b] <src> [<src> ...] <dest>\n"
        "\n"
        "   -b             Use basic search criteria for files only\n";

/**
 Display usage text to the user.
 */
BOOL
MoveHelp()
{
    YORI_STRING License;

    YoriLibMitLicenseText(_T("2017"), &License);

    YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("Move %i.%i\n"), MOVE_VER_MAJOR, MOVE_VER_MINOR);
#if YORI_BUILD_ID
    YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("  Build %i\n"), YORI_BUILD_ID);
#endif
    YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("%hs\n"), strHelpText);
    YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("%y"), &License);
    YoriLibFreeStringContents(&License);
    return TRUE;
}

/**
 Prototype for the SetNamedSecurityInfoW API.
 */
typedef DWORD WINAPI SET_NAMED_SECURITY_INFOW(LPTSTR, DWORD, SECURITY_INFORMATION, PSID, PSID, PACL, PACL);

/**
 Prototype for a pointer to the SetNamedSecurityInfoW API.
 */
typedef SET_NAMED_SECURITY_INFOW *PSET_NAMED_SECURITY_INFOW;

/**
 Pointer to the SetNamedSecurityInfoW.
 */
PSET_NAMED_SECURITY_INFOW pSetNamedSecurityInfo;

#ifndef UNPROTECTED_DACL_SECURITY_INFORMATION
/**
 A private definition of this OS value in case the compilation environment
 doesn't define it.
 */
#define UNPROTECTED_DACL_SECURITY_INFORMATION (0x20000000)
#endif

#ifndef SE_FILE_OBJECT
/**
 A private definition of this OS value in case the compilation environment
 doesn't define it.
 */
#define SE_FILE_OBJECT 1
#endif

/**
 A context passed between each source file match when moving multiple
 files.
 */
typedef struct _MOVE_CONTEXT {

    /**
     Path to the destination for the move operation.
     */
    YORI_STRING Dest;

    /**
     The file system attributes of the destination.  Used to determine if
     the destination exists and is a directory.
     */
    DWORD DestAttributes;

    /**
     The number of files that have been previously moved.  This can be used
     to determine if we're about to move a second object over the top of
     an earlier moved file.
     */
    DWORD FilesMoved;
} MOVE_CONTEXT, *PMOVE_CONTEXT;

/**
 A callback that is invoked when a file is found that matches a search criteria
 specified in the set of strings to enumerate.

 @param FilePath Pointer to the file path that was found.

 @param FileInfo Information about the file.

 @param Depth Specifies the recursion depth.  Ignored in this application.

 @param Context Ignored.

 @return TRUE to continute enumerating, FALSE to abort.
 */
BOOL
MoveFileFoundCallback(
    __in PYORI_STRING FilePath,
    __in PWIN32_FIND_DATA FileInfo,
    __in DWORD Depth,
    __in PVOID Context
    )
{
    PMOVE_CONTEXT MoveContext = (PMOVE_CONTEXT)Context;
    YORI_STRING FullDest;
    DWORD OsMajor;
    DWORD OsMinor;
    DWORD OsBuild;

    UNREFERENCED_PARAMETER(Depth);

    ASSERT(FilePath->StartOfString[FilePath->LengthInChars] == '\0');

    YoriLibInitEmptyString(&FullDest);

    if (MoveContext->DestAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        YORI_STRING DestWithFile;
        if (!YoriLibAllocateString(&DestWithFile, MoveContext->Dest.LengthInChars + 1 + _tcslen(FileInfo->cFileName) + 1)) {
            return FALSE;
        }
        DestWithFile.LengthInChars = YoriLibSPrintf(DestWithFile.StartOfString, _T("%y\\%s"), &MoveContext->Dest, FileInfo->cFileName);
        if (!YoriLibGetFullPathNameReturnAllocation(&DestWithFile, TRUE, &FullDest, NULL)) {
            return FALSE;
        }
        YoriLibFreeStringContents(&DestWithFile);
    } else {
        if (!YoriLibGetFullPathNameReturnAllocation(&MoveContext->Dest, TRUE, &FullDest, NULL)) {
            return FALSE;
        }
        if (MoveContext->FilesMoved > 0) {
            YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("Attempting to move multiple files over a single file (%s)\n"), FullDest.StartOfString);
            YoriLibFreeStringContents(&FullDest);
            return FALSE;
        }
    }
    if (!MoveFileEx(FilePath->StartOfString, FullDest.StartOfString, MOVEFILE_COPY_ALLOWED|MOVEFILE_REPLACE_EXISTING)) {
        DWORD LastError = GetLastError();
        LPTSTR ErrText = YoriLibGetWinErrorText(LastError);
        YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("MoveFile failed: %s"), ErrText);
        YoriLibFreeWinErrorText(ErrText);
    }

    YoriLibGetOsVersion(&OsMajor, &OsMinor, &OsBuild);

    //
    //  Windows 2000 and above claim to support inherited ACLs, except they
    //  really depend on applications to perform the inheritance.  On these
    //  systems, attempt to reset the ACL on the target, which really resets
    //  security and picks up state from the parent.  Don't do this on older
    //  systems, because that will result in a file with no ACL.  Note this
    //  can fail due to not having access on the file, or a file system
    //  that doesn't support security, or because the file is no longer there,
    //  so errors here are just ignored.
    //

    if (OsMajor >= 5) {
        ACL EmptyAcl = {0};
        InitializeAcl(&EmptyAcl, sizeof(EmptyAcl), ACL_REVISION);

        //
        //  This function should exist on NT4+
        //

        ASSERT(pSetNamedSecurityInfo != NULL);

        if (pSetNamedSecurityInfo != NULL) {
            pSetNamedSecurityInfo(FullDest.StartOfString, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | UNPROTECTED_DACL_SECURITY_INFORMATION, NULL, NULL, &EmptyAcl, NULL);
        }
    }
    MoveContext->FilesMoved++;
    YoriLibDereference(FullDest.StartOfString);
    return TRUE;
}


/**
 The main entrypoint for the move cmdlet.

 @param ArgC The number of arguments.

 @param ArgV An array of arguments.

 @return Exit code of the process indicating success or failure.
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
    DWORD i;
    MOVE_CONTEXT MoveContext;
    HANDLE hAdvApi;
    BOOL AllocatedDest;
    BOOL BasicEnumeration;
    YORI_STRING Arg;

    FileCount = 0;
    AllocatedDest = FALSE;
    BasicEnumeration = FALSE;

    for (i = 1; i < ArgC; i++) {

        ArgumentUnderstood = FALSE;
        ASSERT(YoriLibIsStringNullTerminated(&ArgV[i]));

        if (YoriLibIsCommandLineOption(&ArgV[i], &Arg)) {

            if (YoriLibCompareStringWithLiteralInsensitive(&Arg, _T("?")) == 0) {
                MoveHelp();
                return EXIT_SUCCESS;
            } else if (YoriLibCompareStringWithLiteralInsensitive(&Arg, _T("b")) == 0) {
                BasicEnumeration = TRUE;
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
        YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("move: argument missing\n"));
        return EXIT_FAILURE;
    } else if (FileCount == 1) {
        YoriLibConstantString(&MoveContext.Dest, _T("."));
    } else {
        if (!YoriLibUserStringToSingleFilePath(&ArgV[LastFileArg], TRUE, &MoveContext.Dest)) {
            YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("copy: could not resolve %y\n"), &ArgV[LastFileArg]);
            return EXIT_FAILURE;
        }
        AllocatedDest = TRUE;
        FileCount--;
    }

    MoveContext.DestAttributes = GetFileAttributes(MoveContext.Dest.StartOfString);
    if (MoveContext.DestAttributes == 0xFFFFFFFF) {
        MoveContext.DestAttributes = 0;
    }
    MoveContext.FilesMoved = 0;
    FilesProcessed = 0;

    hAdvApi = LoadLibrary(_T("ADVAPI32.DLL"));
    if (hAdvApi != NULL) {
        pSetNamedSecurityInfo = (PSET_NAMED_SECURITY_INFOW)GetProcAddress(hAdvApi, "SetNamedSecurityInfoW");
        FreeLibrary(hAdvApi);
    }

    for (i = 1; i < ArgC; i++) {
        if (!YoriLibIsCommandLineOption(&ArgV[i], &Arg)) {
            MatchFlags = YORILIB_FILEENUM_RETURN_FILES | YORILIB_FILEENUM_RETURN_DIRECTORIES;
            if (BasicEnumeration) {
                MatchFlags |= YORILIB_FILEENUM_BASIC_EXPANSION;
            }
            YoriLibForEachFile(&ArgV[i], MatchFlags, 0, MoveFileFoundCallback, &MoveContext);
            FilesProcessed++;
            if (FilesProcessed == FileCount) {
                break;
            }
        }
    }

    if (AllocatedDest) {
        YoriLibFreeStringContents(&MoveContext.Dest);
    }

    return EXIT_SUCCESS;
}

// vim:sw=4:ts=4:et: