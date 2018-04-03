/**
 * @file dir/dir.c
 *
 * Yori shell enumerate files in directories
 *
 * Copyright (c) 2017-2018 Malcolm J. Smith
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
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
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
        "Enumerate the contents of directories.\n"
        "\n"
        "DIR [-b] [-s] [<spec>...]\n"
        "\n"
        "   -b             Use basic search criteria for files only\n"
        "   -r             Display named stream\n"
        "   -s             Process files from all subdirectories\n"
        "   -x             Display short file names\n"
        "\n"
        "For a more powerful enumerator, consider using sdir instead.\n";

/**
 Display usage text to the user.
 */
BOOL
DirHelp()
{
    YORI_STRING License;

    YoriLibMitLicenseText(_T("2017-2018"), &License);

    YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("Dir %i.%i\n"), DIR_VER_MAJOR, DIR_VER_MINOR);
#if YORI_BUILD_ID
    YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("  Build %i\n"), YORI_BUILD_ID);
#endif
    YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("%hs\n"), strHelpText);
    YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("%y"), &License);
    YoriLibFreeStringContents(&License);
    return TRUE;
}

/**
 The maximum size of the stream name in WIN32_FIND_STREAM_DATA.
 */
#define DIR_MAX_STREAM_NAME          (MAX_PATH + 36)

/**
 A private definition of WIN32_FIND_STREAM_DATA in case the compilation
 environment doesn't provide it.
 */
typedef struct _DIR_WIN32_FIND_STREAM_DATA {

    /**
     The length of the stream, in bytes.
     */
    LARGE_INTEGER StreamSize;

    /**
     The name of the stream.
     */
    WCHAR cStreamName[DIR_MAX_STREAM_NAME];
} DIR_WIN32_FIND_STREAM_DATA, *PDIR_WIN32_FIND_STREAM_DATA;

/**
 Prototype for a pointer to a function to enumerate the first stream on a
 file.
 */
typedef HANDLE (WINAPI * FIND_FIRST_STREAM_FN)(LPCTSTR, DWORD, PDIR_WIN32_FIND_STREAM_DATA, DWORD);

/**
 Prototype for a pointer to a function to enumerate the next stream on a
 file.
 */
typedef BOOL (WINAPI * FIND_NEXT_STREAM_FN)(HANDLE, PDIR_WIN32_FIND_STREAM_DATA);

/**
 Context passed to the callback which is invoked for each file found.
 */
typedef struct _DIR_CONTEXT {

    /**
     TRUE if the directory enumeration should display short file names as well
     as long file names.  FALSE if only long names should be displayed.
     */
    BOOL DisplayShortNames;

    /**
     TRUE if the directory enumeration should display named streams on files.
     */
    BOOL DisplayStreams;

    /**
     Records the total number of files processed.
     */
    LONGLONG FilesFound;

    /**
     A string containing the directory currently being enumerated.
     */
    YORI_STRING CurrentDirectoryName;

    /**
     The total number of directory entries enumerated from this directory.
     */
    LONGLONG ObjectsFoundInThisDir;

    /**
     The total number of directory entries enumerated from this directory
     that were themselves directories.
     */
    LONGLONG DirsFoundInThisDir;

    /**
     The total number of directory entries enumerated from this directory
     that were themselves files.
     */
    LONGLONG FilesFoundInThisDir;

    /**
     The total amount of bytes consumed by files within this directory.
     */
    LONGLONG FileSizeInThisDir;

    /**
     Pointer to a function to find the first stream on a file.
     */
    FIND_FIRST_STREAM_FN FindFirstStreamW;

    /**
     Pointer to a function to find the next stream on a file.
     */
    FIND_NEXT_STREAM_FN FindNextStreamW;

} DIR_CONTEXT, *PDIR_CONTEXT;

/**
 This function right aligns a Yori string by moving characters in place
 to ensure the total length of the string equals the specified alignment.

 @param String Pointer to the string to align.

 @param Align The number of characters that the string should contain.  If
        it currently has less than this number, spaces are inserted at the
        beginning of the string.
 */
VOID
DirRightAlignString(
    __in PYORI_STRING String,
    __in DWORD Align
    )
{
    DWORD Index;
    DWORD Delta;
    if (String->LengthInChars >= Align) {
        return;
    }
    ASSERT(String->LengthAllocated >= Align);
    if (String->LengthAllocated < Align) {
        return;
    }
    Delta = Align - String->LengthInChars;
    for (Index = Align - 1; Index >= Delta; Index--) {
        String->StartOfString[Index] = String->StartOfString[Index - Delta];
    }
    for (Index = 0; Index < Delta; Index++) {
        String->StartOfString[Index] = ' ';
    }
    String->LengthInChars = Align;
}

/**
 The number of characters to use to display the size of objects in the
 directory.
 */
#define DIR_SIZE_FIELD_SIZE  18

/**
 The number of characters to use to display the count of objects in the
 directory.
 */
#define DIR_COUNT_FIELD_SIZE 12

/**
 Before displaying the contents of a directory, this function displays any
 directory level header information.

 @param DirContext Context describing the state of the enumeration.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOL
DirOutputBeginningOfDirectorySummary(
    __in PDIR_CONTEXT DirContext
    )
{
    YORI_STRING UnescapedPath;
    PYORI_STRING PathToDisplay;
    YoriLibInitEmptyString(&UnescapedPath);
    YoriLibUnescapePath(&DirContext->CurrentDirectoryName, &UnescapedPath);
    if (UnescapedPath.LengthInChars > 0) {
        PathToDisplay = &UnescapedPath;
    } else {
        PathToDisplay = &DirContext->CurrentDirectoryName;
    }
    YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("\n Directory of %y\n\n"), PathToDisplay);
    YoriLibFreeStringContents(&UnescapedPath);
    return TRUE;
}

/**
 After displaying the contents of a directory, this function displays any
 directory level footer information.

 @param DirContext Context describing the state of the enumeration.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOL
DirOutputEndOfDirectorySummary(
    __in PDIR_CONTEXT DirContext
    )
{
    YORI_STRING CountString;
    YORI_STRING SizeString;
    TCHAR CountStringBuffer[DIR_COUNT_FIELD_SIZE];
    TCHAR SizeStringBuffer[DIR_SIZE_FIELD_SIZE];

    YoriLibInitEmptyString(&SizeString);
    SizeString.StartOfString = SizeStringBuffer;
    SizeString.LengthAllocated = sizeof(SizeStringBuffer)/sizeof(SizeStringBuffer[0]);

    YoriLibInitEmptyString(&CountString);
    CountString.StartOfString = CountStringBuffer;
    CountString.LengthAllocated = sizeof(CountStringBuffer)/sizeof(CountStringBuffer[0]);

    YoriLibNumberToString(&CountString, DirContext->FilesFoundInThisDir, 10, 3, ',');
    YoriLibNumberToString(&SizeString, DirContext->FileSizeInThisDir, 10, 3, ',');

    DirRightAlignString(&CountString, DIR_COUNT_FIELD_SIZE);
    DirRightAlignString(&SizeString, DIR_SIZE_FIELD_SIZE);

    YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("%y File(s) %y bytes\n"), &CountString, &SizeString);

    YoriLibNumberToString(&CountString, DirContext->DirsFoundInThisDir, 10, 3, ',');

    DirRightAlignString(&CountString, DIR_COUNT_FIELD_SIZE);
    YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("%y Dir(s)\n"), &CountString);

    DirContext->ObjectsFoundInThisDir = 0;
    DirContext->FilesFoundInThisDir = 0;
    DirContext->FileSizeInThisDir = 0;
    DirContext->DirsFoundInThisDir = 0;

    YoriLibFreeStringContents(&CountString);
    YoriLibFreeStringContents(&SizeString);
    return TRUE;
}


/**
 A callback that is invoked when a file is found that matches a search criteria
 specified in the set of strings to enumerate.

 @param FilePath Pointer to the file path that was found.

 @param FileInfo Information about the file.

 @param Depth Recursion depth, ignored in this application.

 @param DirContext Pointer to the dir context structure indicating the
        action to perform and populated with the number of objects found.

 @return TRUE to continute enumerating, FALSE to abort.
 */
BOOL
DirFileFoundCallback(
    __in PYORI_STRING FilePath,
    __in PWIN32_FIND_DATA FileInfo,
    __in DWORD Depth,
    __in PDIR_CONTEXT DirContext
    )
{
    SYSTEMTIME FileWriteTime;
    FILETIME LocalFileTime;
    YORI_STRING SizeString;
    TCHAR SizeStringBuffer[DIR_SIZE_FIELD_SIZE];
    LARGE_INTEGER FileSize;
    LPTSTR FilePart;

    UNREFERENCED_PARAMETER(Depth);

    ASSERT(FilePath->StartOfString[FilePath->LengthInChars] == '\0');

    FilePart = YoriLibFindRightMostCharacter(FilePath, '\\');
    ASSERT(FilePart != NULL);
    if (FilePart != NULL) {
        YORI_STRING ThisDirName;
        YoriLibInitEmptyString(&ThisDirName);
        ThisDirName.StartOfString = FilePath->StartOfString;
        ThisDirName.LengthInChars = (DWORD)(FilePart - FilePath->StartOfString);
        if (YoriLibCompareString(&ThisDirName, &DirContext->CurrentDirectoryName) != 0) {
            if (DirContext->ObjectsFoundInThisDir != 0) {
                DirOutputEndOfDirectorySummary(DirContext);
            }
            if (DirContext->CurrentDirectoryName.LengthAllocated < ThisDirName.LengthInChars) {
                YoriLibFreeStringContents(&DirContext->CurrentDirectoryName);
                if (!YoriLibAllocateString(&DirContext->CurrentDirectoryName, ThisDirName.LengthInChars + 80)) {
                    return FALSE;
                }
            }
            CopyMemory(DirContext->CurrentDirectoryName.StartOfString, ThisDirName.StartOfString, ThisDirName.LengthInChars * sizeof(TCHAR));
            DirContext->CurrentDirectoryName.LengthInChars = ThisDirName.LengthInChars;
            DirOutputBeginningOfDirectorySummary(DirContext);
        }

    }

    DirContext->FilesFound++;
    DirContext->ObjectsFoundInThisDir++;

    FileTimeToLocalFileTime(&FileInfo->ftLastWriteTime, &LocalFileTime);
    FileTimeToSystemTime(&LocalFileTime, &FileWriteTime);
    YoriLibInitEmptyString(&SizeString);
    SizeString.StartOfString = SizeStringBuffer;
    SizeString.LengthAllocated = sizeof(SizeStringBuffer)/sizeof(SizeStringBuffer[0]);

    if ((FileInfo->dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        if (FileInfo->dwReserved0 == IO_REPARSE_TAG_MOUNT_POINT) {
            YoriLibYPrintf(&SizeString, _T(" <MNT>"));
        } else if (FileInfo->dwReserved0 == IO_REPARSE_TAG_SYMLINK) {
            YoriLibYPrintf(&SizeString, _T(" <LNK>"));
        }
    }

    if (SizeString.LengthInChars == 0) {
        if ((FileInfo->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            YoriLibYPrintf(&SizeString, _T(" <DIR>"));
            DirContext->DirsFoundInThisDir++;
        } else {
            FileSize.HighPart = FileInfo->nFileSizeHigh;
            FileSize.LowPart = FileInfo->nFileSizeLow;
            DirContext->FileSizeInThisDir += FileSize.QuadPart;
            YoriLibNumberToString(&SizeString, FileSize.QuadPart, 10, 3, ',');
            if (SizeString.LengthInChars < DIR_SIZE_FIELD_SIZE) {
                DirRightAlignString(&SizeString, DIR_SIZE_FIELD_SIZE);
            }
            DirContext->FilesFoundInThisDir++;
        }
    }

    if (SizeString.LengthInChars < DIR_SIZE_FIELD_SIZE) {
        DWORD Index;
        for (Index = SizeString.LengthInChars; Index < DIR_SIZE_FIELD_SIZE; Index++) {
            SizeString.StartOfString[Index] = ' ';
        }
        SizeString.LengthInChars = DIR_SIZE_FIELD_SIZE;
    }

    if (DirContext->DisplayShortNames) {
        YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("%04i/%02i/%02i  %02i:%02i %y %12s %s\n"), FileWriteTime.wYear, FileWriteTime.wMonth, FileWriteTime.wDay, FileWriteTime.wHour, FileWriteTime.wMinute, &SizeString, FileInfo->cAlternateFileName, FileInfo->cFileName);
    } else {
        YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("%04i/%02i/%02i  %02i:%02i %y %s\n"), FileWriteTime.wYear, FileWriteTime.wMonth, FileWriteTime.wDay, FileWriteTime.wHour, FileWriteTime.wMinute, &SizeString, FileInfo->cFileName);
    }

    if (DirContext->DisplayStreams) {
        HANDLE hFind;
        DIR_WIN32_FIND_STREAM_DATA FindStreamData;

        hFind = DirContext->FindFirstStreamW(FilePath->StartOfString, 0, &FindStreamData, 0);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (_tcscmp(FindStreamData.cStreamName, L"::$DATA") != 0) {
                    YoriLibNumberToString(&SizeString, FindStreamData.StreamSize.QuadPart, 10, 3, ',');
                    if (SizeString.LengthInChars < DIR_SIZE_FIELD_SIZE) {
                        DirRightAlignString(&SizeString, DIR_SIZE_FIELD_SIZE);
                    }
                    if (DirContext->DisplayShortNames) {
                        YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("%18s%y %13s%s%s\n"), _T(""), &SizeString, _T(""), FileInfo->cFileName, FindStreamData.cStreamName);
                    } else {
                        YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("%18s%y %s%s\n"), _T(""), &SizeString, FileInfo->cFileName, FindStreamData.cStreamName);
                    }
                }
            } while (DirContext->FindNextStreamW(hFind, &FindStreamData));
            FindClose(hFind);
        }
    }

    YoriLibFreeStringContents(&SizeString);

    return TRUE;
}

/**
 The main entrypoint for the dir cmdlet.

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
    DWORD i;
    DWORD StartArg = 0;
    DWORD MatchFlags;
    BOOL Recursive = FALSE;
    BOOL BasicEnumeration = FALSE;
    DIR_CONTEXT DirContext;
    YORI_STRING Arg;

    ZeroMemory(&DirContext, sizeof(DirContext));

    for (i = 1; i < ArgC; i++) {

        ArgumentUnderstood = FALSE;
        ASSERT(YoriLibIsStringNullTerminated(&ArgV[i]));

        if (YoriLibIsCommandLineOption(&ArgV[i], &Arg)) {

            if (YoriLibCompareStringWithLiteralInsensitive(&Arg, _T("?")) == 0) {
                DirHelp();
                return EXIT_SUCCESS;
            } else if (YoriLibCompareStringWithLiteralInsensitive(&Arg, _T("b")) == 0) {
                BasicEnumeration = TRUE;
                ArgumentUnderstood = TRUE;
            } else if (YoriLibCompareStringWithLiteralInsensitive(&Arg, _T("r")) == 0) {
                DirContext.DisplayStreams = TRUE;
                ArgumentUnderstood = TRUE;
            } else if (YoriLibCompareStringWithLiteralInsensitive(&Arg, _T("s")) == 0) {
                Recursive = TRUE;
                ArgumentUnderstood = TRUE;
            } else if (YoriLibCompareStringWithLiteralInsensitive(&Arg, _T("x")) == 0) {
                DirContext.DisplayShortNames = TRUE;
                ArgumentUnderstood = TRUE;
            }
        } else {
            ArgumentUnderstood = TRUE;
            StartArg = i;
            break;
        }

        if (!ArgumentUnderstood) {
            YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("Argument not understood, ignored: %y\n"), &ArgV[i]);
        }
    }

    if (DirContext.DisplayStreams) {
        HANDLE hKernel32;
        hKernel32 = GetModuleHandle(_T("KERNEL32"));
        DirContext.FindFirstStreamW = (FIND_FIRST_STREAM_FN)GetProcAddress(hKernel32, "FindFirstStreamW");
        DirContext.FindNextStreamW = (FIND_NEXT_STREAM_FN)GetProcAddress(hKernel32, "FindNextStreamW");
        if (DirContext.FindFirstStreamW == NULL || DirContext.FindNextStreamW == NULL) {
            DirContext.DisplayStreams = FALSE;
        }
    }

    //
    //  If no file name is specified, use *
    //

    MatchFlags = YORILIB_FILEENUM_RETURN_FILES |
                 YORILIB_FILEENUM_RETURN_DIRECTORIES |
                 YORILIB_FILEENUM_INCLUDE_DOTFILES |
                 YORILIB_FILEENUM_DIRECTORY_CONTENTS;
    if (Recursive) {
        MatchFlags |= YORILIB_FILEENUM_RECURSE_BEFORE_RETURN | YORILIB_FILEENUM_RECURSE_PRESERVE_WILD;
    }
    if (BasicEnumeration) {
        MatchFlags |= YORILIB_FILEENUM_BASIC_EXPANSION;
    }

    if (StartArg == 0) {
        YORI_STRING FilesInDirectorySpec;
        YoriLibConstantString(&FilesInDirectorySpec, _T("*"));
        YoriLibForEachFile(&FilesInDirectorySpec, MatchFlags, 0, DirFileFoundCallback, &DirContext);
    } else {
    
        for (i = StartArg; i < ArgC; i++) {
    
            YoriLibForEachFile(&ArgV[i], MatchFlags, 0, DirFileFoundCallback, &DirContext);
        }
    }


    if (DirContext.ObjectsFoundInThisDir > 0) {
        DirOutputEndOfDirectorySummary(&DirContext);
    } 

    YoriLibFreeStringContents(&DirContext.CurrentDirectoryName);

    if (DirContext.FilesFound == 0) {
        YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("dir: no matching files found\n"));
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

// vim:sw=4:ts=4:et: