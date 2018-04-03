/**
 * @file builtins/if.c
 *
 * Yori shell invoke an expression and perform different actions based on
 * the result
 *
 * Copyright (c) 2018 Malcolm J. Smith
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
#include <yoricall.h>

/**
 Help text to display to the user.
 */
const
CHAR strIfHelpText[] =
        "\n"
        "Execute a command to evaluate a condition.\n"
        "\n"
        "IF <test cmd>; <true cmd>; <false cmd>\n";

/**
 Display usage text to the user.
 */
BOOL
IfHelp()
{
    YORI_STRING License;

    YoriLibMitLicenseText(_T("2018"), &License);

    YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("If %i.%i\n"), YORI_VER_MAJOR, YORI_VER_MINOR);
#if YORI_BUILD_ID
    YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("  Build %i\n"), YORI_BUILD_ID);
#endif
    YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("%hs\n"), strIfHelpText);
    YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("%y"), &License);
    YoriLibFreeStringContents(&License);
    return TRUE;
}

/**
 Escape any redirection operators.  This is used on the test condition which
 frequently contains characters like > or < but these are intended to describe
 evaluation conditions (and should be passed to the test application) not IO 
 redirection.

 @param String The string to expand redirection operators in.  This string may
        be reallocated to be large enough to contain the escaped string.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOL
IfExpandRedirectOperators(
    __in PYORI_STRING String
    )
{
    DWORD Index;
    DWORD DestIndex;
    DWORD MatchCount;
    LPTSTR NewAllocation;

    MatchCount = 0;
    for (Index = 0; Index < String->LengthInChars; Index++) {
        if (String->StartOfString[Index] == '>' ||
            String->StartOfString[Index] == '<') {

            MatchCount++;
        }
    }

    if (MatchCount == 0) {
        return TRUE;
    }

    NewAllocation = YoriLibReferencedMalloc((String->LengthInChars + MatchCount + 1) * sizeof(TCHAR));
    if (NewAllocation == NULL) {
        return FALSE;
    }

    DestIndex = 0;
    for (Index = 0; Index < String->LengthInChars; Index++) {
        if (String->StartOfString[Index] == '>' ||
            String->StartOfString[Index] == '<') {

            NewAllocation[DestIndex] = '^';
            DestIndex++;
        }
        NewAllocation[DestIndex] = String->StartOfString[Index];
        DestIndex++;
    }
    NewAllocation[DestIndex] = '\0';

    if (String->MemoryToFree != NULL) {
        YoriLibDereference(String->MemoryToFree);
    }

    String->MemoryToFree = NewAllocation;
    String->StartOfString = NewAllocation;
    String->LengthInChars = DestIndex;
    String->LengthAllocated = DestIndex + 1;

    return TRUE;
}

/**
 Yori shell test a condition and execute a command in response

 @param ArgC The number of arguments.

 @param ArgV The argument array.

 @return ExitCode, zero for success, nonzero for failure.
 */
DWORD
YORI_BUILTIN_FN
YoriCmd_IF(
    __in DWORD ArgC,
    __in YORI_STRING ArgV[]
    )
{
    BOOL ArgumentUnderstood;
    BOOL Result;
    YORI_STRING CmdLine;
    DWORD ErrorLevel;
    DWORD CharIndex;
    DWORD i;
    DWORD StartArg = 0;
    YORI_STRING TestCommand;
    YORI_STRING TrueCommand;
    YORI_STRING FalseCommand;
    YORI_STRING Arg;

    for (i = 1; i < ArgC; i++) {

        ArgumentUnderstood = FALSE;
        ASSERT(YoriLibIsStringNullTerminated(&ArgV[i]));

        if (YoriLibIsCommandLineOption(&ArgV[i], &Arg)) {
            if (YoriLibCompareStringWithLiteralInsensitive(&Arg, _T("?")) == 0) {
                IfHelp();
                return EXIT_SUCCESS;
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

    if (StartArg == 0) {
        YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("if: missing argument\n"));
        return EXIT_FAILURE;
    }

    if (!YoriLibBuildCmdlineFromArgcArgv(ArgC - StartArg, &ArgV[StartArg], FALSE, &CmdLine)) {
        return EXIT_FAILURE;
    }

    YoriLibInitEmptyString(&TestCommand);
    YoriLibInitEmptyString(&TrueCommand);
    YoriLibInitEmptyString(&FalseCommand);

    TestCommand.StartOfString = CmdLine.StartOfString;
    for (CharIndex = 0; CharIndex < CmdLine.LengthInChars; CharIndex++) {
        if (YoriLibIsEscapeChar(CmdLine.StartOfString[CharIndex])) {
            CharIndex++;
            continue;
        }
        if (CmdLine.StartOfString[CharIndex] == ';') {
            CmdLine.StartOfString[CharIndex] = '\0';
            break;
        }
    }

    TestCommand.LengthInChars = CharIndex;
    CharIndex++;
    TrueCommand.StartOfString = &CmdLine.StartOfString[CharIndex];

    for (; CharIndex < CmdLine.LengthInChars; CharIndex++) {
        if (YoriLibIsEscapeChar(CmdLine.StartOfString[CharIndex])) {
            CharIndex++;
            continue;
        }
        if (CmdLine.StartOfString[CharIndex] == ';') {
            CmdLine.StartOfString[CharIndex] = '\0';
            break;
        }
    }

    TrueCommand.LengthInChars = CharIndex - TestCommand.LengthInChars - 1;
    CharIndex++;
    FalseCommand.StartOfString = &CmdLine.StartOfString[CharIndex];

    for (; CharIndex < CmdLine.LengthInChars; CharIndex++) {
        if (YoriLibIsEscapeChar(CmdLine.StartOfString[CharIndex])) {
            CharIndex++;
            continue;
        }
        if (CmdLine.StartOfString[CharIndex] == ';') {
            CmdLine.StartOfString[CharIndex] = '\0';
            break;
        }
    }
    FalseCommand.LengthInChars = CharIndex - TestCommand.LengthInChars - 1 - TrueCommand.LengthInChars - 1;

    IfExpandRedirectOperators(&TestCommand);

    Result = YoriCallExecuteExpression(&TestCommand);
    if (!Result) {
        YoriLibFreeStringContents(&TestCommand);
        YoriLibFreeStringContents(&CmdLine);
        return EXIT_FAILURE;
    }

    ErrorLevel = YoriCallGetErrorLevel();
    if (ErrorLevel == 0) {
        if (TrueCommand.LengthInChars > 0) {
            Result = YoriCallExecuteExpression(&TrueCommand);
            if (!Result) {
                YoriLibFreeStringContents(&TestCommand);
                YoriLibFreeStringContents(&CmdLine);
                return EXIT_FAILURE;
            }
        }
    } else {
        if (FalseCommand.LengthInChars > 0) {
            Result = YoriCallExecuteExpression(&FalseCommand);
            if (!Result) {
                YoriLibFreeStringContents(&TestCommand);
                YoriLibFreeStringContents(&CmdLine);
                return EXIT_FAILURE;
            }
        }
    }

    YoriLibFreeStringContents(&TestCommand);

    YoriLibFreeStringContents(&CmdLine);
    return EXIT_SUCCESS;
}

// vim:sw=4:ts=4:et: