/**
 * @file type/type.c
 *
 * Yori shell display file contents
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
        "Output the contents of one or more files.\n"
        "\n"
        "TYPE [-b] [-s] [-h <num>] [<file>...]\n"
        "\n"
        "   -b             Use basic search criteria for files only\n"
        "   -h <num>       Display <num> lines from the beginning of each file\n"
        "   -s             Process files from all subdirectories\n";

/**
 Display usage text to the user.
 */
BOOL
TypeHelp()
{
    YORI_STRING License;

    YoriLibMitLicenseText(_T("2017-2018"), &License);

    YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("Type %i.%i\n"), TYPE_VER_MAJOR, TYPE_VER_MINOR);
#if YORI_BUILD_ID
    YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("  Build %i\n"), YORI_BUILD_ID);
#endif
    YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("%hs\n"), strHelpText);
    YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("%y"), &License);
    YoriLibFreeStringContents(&License);
    return TRUE;
}

/**
 Context passed to the callback which is invoked for each file found.
 */
typedef struct _TYPE_CONTEXT {

    /**
     Specifies the number of lines from the top of each file to display.
     */
    ULONG HeadLines;

    /**
     Records the total number of files processed.
     */
    LONGLONG FilesFound;

    /**
     Records the number of lines found from a specific file.
     */
    LONGLONG FileLinesFound;

} TYPE_CONTEXT, *PTYPE_CONTEXT;

/**
 Process a single opened stream, enumerating through all lines and displaying
 the set requested by the user.

 @param hSource The opened source stream.

 @param TypeContext Pointer to context information specifying which lines to
        display.
 
 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOL
TypeProcessStream(
    __in HANDLE hSource,
    __in PTYPE_CONTEXT TypeContext
    )
{
    PVOID LineContext = NULL;
    CONSOLE_SCREEN_BUFFER_INFO ScreenInfo;
    YORI_STRING LineString;

    YoriLibInitEmptyString(&LineString);

    TypeContext->FilesFound++;
    TypeContext->FileLinesFound = 0;

    while (TRUE) {

        if (!YoriLibReadLineToString(&LineString, &LineContext, hSource)) {
            break;
        }

        TypeContext->FileLinesFound++;

        if ((TypeContext->HeadLines == 0 || TypeContext->FileLinesFound <= TypeContext->HeadLines)) {
            YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("%y"), &LineString);
            if (LineString.LengthInChars == 0 || !GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &ScreenInfo) || ScreenInfo.dwCursorPosition.X != 0) {
                YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("\n"));
            }
        }
    }

    YoriLibLineReadClose(LineContext);
    YoriLibFreeStringContents(&LineString);

    return TRUE;
}

/**
 A callback that is invoked when a file is found that matches a search criteria
 specified in the set of strings to enumerate.

 @param FilePath Pointer to the file path that was found.

 @param FileInfo Information about the file.

 @param Depth Specifies recursion depth.  Ignored in this application.

 @param TypeContext Pointer to the type context structure indicating the
        action to perform and populated with the file and line count found.

 @return TRUE to continute enumerating, FALSE to abort.
 */
BOOL
TypeFileFoundCallback(
    __in PYORI_STRING FilePath,
    __in PWIN32_FIND_DATA FileInfo,
    __in DWORD Depth,
    __in PTYPE_CONTEXT TypeContext
    )
{
    HANDLE FileHandle;

    UNREFERENCED_PARAMETER(Depth);

    ASSERT(FilePath->StartOfString[FilePath->LengthInChars] == '\0');

    if ((FileInfo->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        FileHandle = CreateFile(FilePath->StartOfString,
                                GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                NULL,
                                OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL,
                                NULL);

        if (FileHandle == NULL || FileHandle == INVALID_HANDLE_VALUE) {
            DWORD LastError = GetLastError();
            LPTSTR ErrText = YoriLibGetWinErrorText(LastError);
            YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("type: open of %y failed: %s"), FilePath, ErrText);
            YoriLibFreeWinErrorText(ErrText);
            return TRUE;
        }

        TypeProcessStream(FileHandle, TypeContext);

        CloseHandle(FileHandle);
    }

    return TRUE;
}


/**
 The main entrypoint for the type cmdlet.

 @param ArgC The number of arguments.

 @param ArgV An array of arguments.

 @return Exit code of the process, zero on success, nonzero on failure.
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
    TYPE_CONTEXT TypeContext;
    YORI_STRING Arg;

    ZeroMemory(&TypeContext, sizeof(TypeContext));

    for (i = 1; i < ArgC; i++) {

        ArgumentUnderstood = FALSE;

        if (YoriLibIsCommandLineOption(&ArgV[i], &Arg)) {

            if (YoriLibCompareStringWithLiteralInsensitive(&Arg, _T("?")) == 0) {
                TypeHelp();
                return EXIT_SUCCESS;
            } else if (YoriLibCompareStringWithLiteralInsensitive(&Arg, _T("b")) == 0) {
                BasicEnumeration = TRUE;
                ArgumentUnderstood = TRUE;
            } else if (YoriLibCompareStringWithLiteralInsensitive(&Arg, _T("h")) == 0) {
                if (ArgC > i + 1) {
                    DWORD CharsConsumed;
                    LONGLONG HeadLines = 0;
                    YoriLibStringToNumber(&ArgV[i + 1], TRUE, &HeadLines, &CharsConsumed);

                    //
                    //  Hack to allow "head -50" to mean "positive 50"
                    //

                    if (HeadLines < 0) {
                        HeadLines = HeadLines * -1;
                    }
                    TypeContext.HeadLines = (ULONG)HeadLines;
                    ArgumentUnderstood = TRUE;
                    i++;
                }
            } else if (YoriLibCompareStringWithLiteralInsensitive(&Arg, _T("s")) == 0) {
                Recursive = TRUE;
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

    //
    //  If no file name is specified, use stdin; otherwise open
    //  the file and use that
    //

    if (StartArg == 0) {
        DWORD FileType = GetFileType(GetStdHandle(STD_INPUT_HANDLE));
        FileType = FileType & ~(FILE_TYPE_REMOTE);
        if (FileType == FILE_TYPE_CHAR) {
            YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("No file or pipe for input\n"));
            return EXIT_FAILURE;
        }

        TypeProcessStream(GetStdHandle(STD_INPUT_HANDLE), &TypeContext);
    } else {
        MatchFlags = YORILIB_FILEENUM_RETURN_FILES | YORILIB_FILEENUM_DIRECTORY_CONTENTS;
        if (Recursive) {
            MatchFlags |= YORILIB_FILEENUM_RECURSE_BEFORE_RETURN | YORILIB_FILEENUM_RECURSE_PRESERVE_WILD;
        }
        if (BasicEnumeration) {
            MatchFlags |= YORILIB_FILEENUM_BASIC_EXPANSION;
        }
    
        for (i = StartArg; i < ArgC; i++) {
    
            YoriLibForEachFile(&ArgV[i], MatchFlags, 0, TypeFileFoundCallback, &TypeContext);
        }
    }

    if (TypeContext.FilesFound == 0) {
        YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("type: no matching files found\n"));
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

// vim:sw=4:ts=4:et: