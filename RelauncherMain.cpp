// Copyright (c) Spry Fox LLC. All rights reserved.
//
// Relauncher is a small tool that you can rename to whatever you want, and when it's run it'll look for
// its own filename with a Debug, Development, Test, or Shipping suffix, and launch the most recently built
// configuration.

#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#include <string>

#if defined(_WIN32) // Building for Windows
#	if !defined(WIN32_LEAN_AND_MEAN)
#		define WIN32_LEAN_AND_MEAN
#	endif
#	include <Windows.h>
#	include <malloc.h>
#	include <process.h>
#	include <processenv.h>
#	include <tchar.h>

#	ifdef UNICODE
#		define stat _wstat
#		define spawnv _wspawnv

#		define main wmain

#		define LOG(Format, ...) fwprintf_s(stderr, Format L"\n", __VA_ARGS__)

typedef std::wstring str;
typedef wchar_t char_t;
#	else // 
#		define stat _stat
#		define spawnv _spawnv
#		define strdup _strdup

#		define LOG(Format, ...) fprintf_s(stderr, Format "\n", __VA_ARGS__)

typedef std::string str;
typedef char char_t;
#	endif

typedef struct _stat StatBuf;

// Seek past any number of Character in Source
const char_t* SeekPast(const char_t* Source, char_t Character)
{
	for (; *Source == Character; ++Source)
	{
	}

	return Source;
}

// Seek past this Identifier in the Source, returns nullptr if it wasn't matched
// Also seeks past quotes and any leading or trailing whitespace.
const char_t* SeekPast(const char_t* Source, const char_t* Identifier)
{
	// Seek past any spaces at the start
	Source = SeekPast(Source, TEXT(' '));

	const size_t IdentifierLength = _tcslen(Identifier);
	const char_t* IdentifierEnd = Identifier + _tcslen(Identifier);
	const bool bIdentifierIsQuoted = *Identifier == TEXT('"') && *(IdentifierEnd - 1) == TEXT('"') && IdentifierLength > 1;
	if (bIdentifierIsQuoted)
	{
		++Identifier;
		--IdentifierEnd;
	}

	// Seek past an opening quote
	const bool bIsQuoted = *Source == TEXT('"');
	if (bIsQuoted)
	{
		++Source;
	}

	// Seek past the entire identifier
	for (; *Source && Identifier != IdentifierEnd; ++Source, ++Identifier)
	{
		// If they don't match, fail
		if (*Source != *Identifier)
		{
			return nullptr;
		}
	}

	// Source did not contain all of Identifier
	if (Identifier != IdentifierEnd)
	{
		return nullptr;
	}

	if (bIsQuoted)
	{
		if (*Source != TEXT('"'))
		{
			return nullptr;
		}
		++Source;
	}

	// Seek past any trailing spaces
	return SeekPast(Source, TEXT(' '));
}

#elif defined(__linux__) // Building for Linux
#	include <unistd.h>
#	define TEXT(x) (x)

#	define LOG(Format, ...) fprintf(stderr, Format "\n", __VA_ARGS__)

typedef std::string str;
typedef char char_t;
typedef struct stat StatBuf;
#else // Unknown build platform
#	error Unable to detect the platform
#endif

#define DEBUG(Format, ...)                      \
	do                                          \
	{                                           \
		if (bDebug)                             \
		{                                       \
			LOG("DEBUG: " Format, __VA_ARGS__); \
		}                                       \
	} while (0)

// These are the executable suffixes that we evaluate to find the most recently built exe
static const str PotentialSuffixes[] = {TEXT("Debug"), TEXT("Development"), TEXT("Test"), TEXT("Shipping")};

// Combines a path like 'C:\foo\Bar.exe' and a suffix from PotentialSuffixes like 'Debug' into 'C:\foo\BarDebug.exe'
str CombineExeNameAndSuffix(str ExeName, const bool bHasExeSuffix, int SuffixIndex)
{
	if (bHasExeSuffix)
	{
		return ExeName.substr(0, ExeName.length() - 4) + PotentialSuffixes[SuffixIndex] + TEXT(".exe");
	}
	else
	{
		return ExeName + PotentialSuffixes[SuffixIndex];
	}
}

int main(int argc, char_t** argv)
{
	static const char_t* DebugArgument = TEXT("--debug-relauncher");
	const auto ExeName = str(argv[0]);
	const bool bDebug = argc > 1 ? str(argv[1]) == DebugArgument : false;
	const bool bHasExeSuffix = ExeName.length() >= 4 && ExeName.substr(ExeName.length() - 4) == TEXT(".exe");

	// Find which of the suffixes exist and pick the one with the most recent mtime
	time_t MostRecentSuffixMtime = -1;
	int MostRecentSuffixIndex = -1;
	for (size_t SuffixIndex = 0; SuffixIndex < sizeof(PotentialSuffixes) / sizeof(PotentialSuffixes[0]); ++SuffixIndex)
	{
		const str SuffixedExeName = CombineExeNameAndSuffix(ExeName, bHasExeSuffix, SuffixIndex);
		StatBuf Stat;
		if (stat(SuffixedExeName.c_str(), &Stat) == 0)
		{
			if (Stat.st_mtime > MostRecentSuffixMtime)
			{
				MostRecentSuffixMtime = Stat.st_mtime;
				MostRecentSuffixIndex = SuffixIndex;
				DEBUG("'%s': File more recent than previous best, choose as current best candidate", SuffixedExeName.c_str());
			}
			else
			{
				DEBUG("'%s': File older than previous best, ignoring", SuffixedExeName.c_str());
			}
		}
		else
		{
			if (errno == ENOENT)
			{
				DEBUG("'%s': File does not exist, ignoring", SuffixedExeName.c_str());
			}
			else
			{
				LOG("ERROR: '%s': Could not stat file, errno %d", SuffixedExeName.c_str(), errno);
			}
		}
	}

	if (MostRecentSuffixIndex >= 0)
	{
		LOG("==> Relauncher starting %s exe", PotentialSuffixes[MostRecentSuffixIndex].c_str());
		const str NewExeName = CombineExeNameAndSuffix(ExeName, bHasExeSuffix, MostRecentSuffixIndex);

#if defined(_WIN32)
		// Because Windows is the worst, there's no "replace this process with a different process" call.
		// They implement `_execve`, but under the hood it just spawns a new process and then the parent
		// process immediately exits. This just spawns & waits for a process, and returns the exit code
		// unless there's an issue
		// In addition, `_spawnv` does not correctly quote arguments before forwarding them, so if we're
		// called with quoted arguments, the ucrt will split them into `argv`, but `_spawnv` will not
		// recreate a quoted string and just forward them unquoted.

		// Take the verbatim command line, and seek past the old exe name (e.g. Tool.exe)
		const char_t* Arguments = SeekPast(GetCommandLine(), argv[0]);
		if (Arguments == nullptr)
		{
			LOG("ERROR: Could not seek past '%s' in command line '%s'", argv[0], GetCommandLine());
			return 1;
		}

		// Also seek past the --debug-relauncher argument if we saw it, so we don't forward that
		if (bDebug)
		{
			Arguments = SeekPast(Arguments, DebugArgument);
			if (Arguments == nullptr)
			{
				LOG("ERROR: Could not seek past '%s' in command line '%s'", DebugArgument, GetCommandLine());
				return 1;
			}
		}

		// Reconstruct a new command line which should look like: `"ToolDebug.exe" "Old Arguments" However they were passed "Here"`
		const char_t *ExePrefix = TEXT("\""),
					 *ExeSuffix = TEXT("\" ");
		const size_t NewCommandlineLength = _tcslen(Arguments) + NewExeName.length() + _tcslen(ExePrefix) + _tcslen(ExeSuffix) + 1;
		char_t* NewCommandline = reinterpret_cast<char_t*>(_malloca(NewCommandlineLength * sizeof(wchar_t)));
		if (NewCommandline == nullptr)
		{
			LOG("ERROR: Failed to allocate new command line buffer (%zu bytes)", NewCommandlineLength * sizeof(wchar_t));
			return 1;
		}

		_tcsset_s(NewCommandline, NewCommandlineLength, TEXT('\0'));
		_tcscat_s(NewCommandline, NewCommandlineLength, ExePrefix);
		_tcscat_s(NewCommandline, NewCommandlineLength, NewExeName.c_str());
		_tcscat_s(NewCommandline, NewCommandlineLength, ExeSuffix);
		_tcscat_s(NewCommandline, NewCommandlineLength, Arguments);

		// Spawn a new process that inherits our handles and otherwise is pretty bog standard.
		constexpr bool bInheritHandles = true;
		constexpr DWORD dwFlags = 0;

		STARTUPINFO StartupInfo;
		memset(&StartupInfo, 0, sizeof(StartupInfo));
		StartupInfo.cb              = sizeof(STARTUPINFO);
		StartupInfo.lpReserved      = nullptr;
		StartupInfo.lpDesktop       = nullptr;
		StartupInfo.lpTitle         = nullptr;
		StartupInfo.dwX             = (DWORD) CW_USEDEFAULT;
		StartupInfo.dwY             = (DWORD) CW_USEDEFAULT;
		StartupInfo.dwXSize         = (DWORD) CW_USEDEFAULT;
		StartupInfo.dwYSize         = (DWORD) CW_USEDEFAULT;
		StartupInfo.dwXCountChars   = (DWORD) 0;
		StartupInfo.dwYCountChars   = (DWORD) 0;
		StartupInfo.dwFillAttribute = (DWORD) 0;
		StartupInfo.dwFlags         = (DWORD) 0;
		StartupInfo.wShowWindow     = (DWORD) 0;
		StartupInfo.cbReserved2     = (DWORD) 0;
		StartupInfo.lpReserved2     = nullptr;
		StartupInfo.hStdInput       = nullptr;
		StartupInfo.hStdOutput      = nullptr;
		StartupInfo.hStdError       = nullptr;

		constexpr size_t ERROR_BUF_SIZE = 512;
		char_t Buffer[ERROR_BUF_SIZE] = {0};

		DEBUG("$ %s", NewCommandline);

		PROCESS_INFORMATION ProcInfo = {};
		if (CreateProcess(NewExeName.c_str(), NewCommandline, nullptr, nullptr, bInheritHandles, dwFlags, nullptr, nullptr,
						  &StartupInfo, &ProcInfo))
		{
			// Wait for it to terminate
			WaitForSingleObject(ProcInfo.hProcess, INFINITE);

			// Retrieve the exit code so we can return it to the caller
			DWORD ExitCode = 0;
			if (GetExitCodeProcess(ProcInfo.hProcess, &ExitCode) != TRUE)
			{
				FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
							  Buffer, ERROR_BUF_SIZE, nullptr);

				DEBUG("ERROR: '%s' failed to retrieve exit code: %s", NewExeName.c_str(), Buffer);
				ExitCode = 1;
			}

			CloseHandle(ProcInfo.hProcess);
			CloseHandle(ProcInfo.hThread);
			return ExitCode;
		}
		else
		{
			FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), Buffer,
						  ERROR_BUF_SIZE, nullptr);

			DEBUG("ERROR: '%s' failed to spawn: %s", NewExeName.c_str(), Buffer);
			return 1;
		}
#elif defined(__linux__)
		// We just re-use `argv`, and either:
		//  - if argv[1] is our "--debug-relauncher" argument, swallow it by inserting our new EXE name
		//    in its place and passing argv[1...] as the new arguments, or
		//  - if we did not get our secret argument, insert our new EXE name
		//    in place of the old EXE name in argv[0] and pass argv[0...] as the new arguments.
		const int FirstRealArgument = bDebug ? 1 : 0;
		argv[FirstRealArgument] = strdup(NewExeName.c_str());

		DEBUG("$ '%s' [.. args ..]", NewExeName.c_str());
		// `execv` never returns unless it failed to launch the new process
		execv(argv[FirstRealArgument], &argv[FirstRealArgument]);
		LOG("ERROR: Could not execute %s, errno %d", NewExeName.c_str(), errno);
		return 1;
#else // Unknown build platform
#	error Unable to detect the platform
#endif
	}
	else
	{
		LOG("ERROR: Could not find any candidates for '%s' (try launching with --debug-relauncher to see information)",
			ExeName.c_str());
		return 1;
	}
}