/**
 * @file nice/nice.c
 *
 * Yori shell child process priority tool
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
        "Runs a child program at low priority.\n"
        "\n"
        "NICE <command>\n";

/**
 Display usage text to the user.
 */
BOOL
NiceHelp()
{
    YORI_STRING License;

    YoriLibMitLicenseText(_T("2017"), &License);

    YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("Nice %i.%i\n"), NICE_VER_MAJOR, NICE_VER_MINOR);
#if YORI_BUILD_ID
    YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("  Build %i\n"), YORI_BUILD_ID);
#endif
    YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("%hs\n"), strHelpText);
    YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("%y"), &License);
    YoriLibFreeStringContents(&License);
    return TRUE;
}

/**
 The main entrypoint for the nice cmdlet.

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
    YORI_STRING CmdLine;
    YORI_STRING Executable;
    PYORI_STRING ChildArgs;
    PROCESS_INFORMATION ProcessInfo;
    STARTUPINFO StartupInfo;
    DWORD ExitCode;
    BOOL ArgumentUnderstood;
    HANDLE hJob;
    DWORD StartArg = 1;
    DWORD i;
    YORI_STRING Arg;

    for (i = 1; i < ArgC; i++) {

        ArgumentUnderstood = FALSE;
        ASSERT(YoriLibIsStringNullTerminated(&ArgV[i]));

        if (YoriLibIsCommandLineOption(&ArgV[i], &Arg)) {

            if (YoriLibCompareStringWithLiteralInsensitive(&Arg, _T("?")) == 0) {
                NiceHelp();
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

    if (ArgC < 2) {
        YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("nice: missing argument\n"));
        return EXIT_FAILURE;
    }

    YoriLibInitEmptyString(&Executable);
    if (!YoriLibLocateExecutableInPath(&ArgV[StartArg], NULL, NULL, &Executable) ||
        Executable.LengthInChars == 0) {

        YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("nice: unable to find executable\n"));
        return EXIT_FAILURE;
    }

    ChildArgs = YoriLibMalloc((ArgC - StartArg) * sizeof(YORI_STRING));
    if (ChildArgs == NULL) {
        return EXIT_FAILURE;
    }

    memcpy(&ChildArgs[0], &Executable, sizeof(YORI_STRING));
    if (StartArg + 1 < ArgC) {
        memcpy(&ChildArgs[1], &ArgV[StartArg + 1], (ArgC - StartArg - 1) * sizeof(YORI_STRING));
    }

    if (!YoriLibBuildCmdlineFromArgcArgv(ArgC - StartArg, ChildArgs, TRUE, &CmdLine)) {
        return EXIT_FAILURE;
    }

    ASSERT(YoriLibIsStringNullTerminated(&CmdLine));

    hJob = YoriLibCreateJobObject();

    memset(&StartupInfo, 0, sizeof(StartupInfo));
    StartupInfo.cb = sizeof(StartupInfo);

    if (!CreateProcess(NULL, CmdLine.StartOfString, NULL, NULL, TRUE, IDLE_PRIORITY_CLASS | CREATE_SUSPENDED, NULL, NULL, &StartupInfo, &ProcessInfo)) {
        DWORD LastError = GetLastError();
        LPTSTR ErrText = YoriLibGetWinErrorText(LastError);
        YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("nice: execution failed: %s"), ErrText);
        YoriLibFreeWinErrorText(ErrText);
        return EXIT_FAILURE;
    }

    if (hJob != NULL) {
        YoriLibAssignProcessToJobObject(hJob, ProcessInfo.hProcess);
        YoriLibLimitJobObjectPriority(hJob, IDLE_PRIORITY_CLASS);
    }

    ResumeThread(ProcessInfo.hThread);
    WaitForSingleObject(ProcessInfo.hProcess, INFINITE);
    GetExitCodeProcess(ProcessInfo.hProcess, &ExitCode);
    CloseHandle(ProcessInfo.hProcess);
    CloseHandle(ProcessInfo.hThread);
    if (hJob != NULL) {
        CloseHandle(hJob);
    }
    YoriLibFreeStringContents(&Executable);
    YoriLibFreeStringContents(&CmdLine);
    YoriLibFree(ChildArgs);

    return ExitCode;
}

// vim:sw=4:ts=4:et: