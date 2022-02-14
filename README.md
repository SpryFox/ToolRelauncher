# ToolRelauncher
A simple command line tool that can be used as a wrapper for your other tools to
launch the most recent built configuration of your tool. (How many times do you
need to say the word "tool" before it loses all meaning?)

E.g. you might have a config validator that you launch from VSCode and from your
server, and you want most people to run the optimized build configuration called
"Development", but still allow programmers to easily run the "Debug" build when
they want to. Instead of teaching both VSCode and your server how to pick the
correct build configuration (e.g. via a config or looking which was most
recently built), you can just build Relauncher and name it `ConfigValidator.exe`,
and have both VSCode and the server run `ConfigValidator.exe`. Relauncher will then
look for `ConfigValidatorDebug.exe`, `ConfigValidatorDevelopment.exe`, and
`ConfigValidatorShipping.exe`, and pick the most recently modified version of these
options -- forwarding all the arguments.

The goal of ToolRelauncher is to be simple and small, so as to add as little overhead
as possible (both in build times and launch times).

## Setup

1. Build `RelauncherMain.cpp`
2. Name it after the tool you want it to relaunch (either by having the compiler
   output that name, or by copying `Relauncher.exe` to the appropriate name)
3. Point your scripts & tools at the base tool name without a build configuration

### Windows

Here's a stripped down command line to build ToolRelauncher on Windows with a
static CRT:

```bat
"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang++.exe" RelauncherMain.cpp -g -Wall -Wextra -Werror -O2 -DNDEBUG -DUNICODE -D_UNICODE -o Relauncher.exe
```

You can add `-D_MT -D_DLL "-Wl,/nodefaultlib:libcmt" -lmsvcrt -lvcruntime -lucrt`
to the command line to link against a dynamic CRT.

It should also build with `cl.exe` without any specific options needed.

### Linux

Here's a stripped down command line to build ToolRelauncher on Linux:

```sh
clang++-12 RelauncherMain.cpp -g -Wall -Wextra -Werror -o Relauncher
```

## Assumptions

Relauncher assumes your tools are named after the build configuration, rather than
put in a different folder based on it. It also assumes Unreal-esque build
configurations of `Debug`, `Development`, and `Shipping` (because that's what we use).

Pull requests that extend ToolRelauncher to support other setups are welcome.

## Compatibility

ToolRelauncher has only been tested with clang 12 on Windows (from Visual Studio's package)
and Linux, and with both unicode and non-unicode Windows build options.