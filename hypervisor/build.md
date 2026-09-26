# HvMonitor - Build & Usage

## Prerequisites

- Windows 10/11 x64
- Visual Studio 2022 with WDK (Windows Driver Kit) installed
- Intel CPU with VT-x enabled in BIOS
- Test signing enabled: `bcdedit /set testsigning on`

## Build

### Option 1: Visual Studio

1. Open VS Developer Command Prompt (x64)
2. Navigate to `hypervisor/src/`
3. Use the provided sources/makefile or create a WDK project importing all .c/.h/.asm files

### Option 2: Command line with WDK

```cmd
cd hypervisor\src

cl /kernel /W4 /GS- /Gz /Oi /D_AMD64_ /DAMD64 ^
   driver.c vmx.c ept.c hooks.c log.c ^
   /Fe:hvmonitor.sys ^
   /link /DRIVER /SUBSYSTEM:NATIVE /ENTRY:DriverEntry ^
   ntoskrnl.lib hal.lib

ml64 /c /Cx asm.asm
link hvmonitor.obj asm.obj /DRIVER /SUBSYSTEM:NATIVE /ENTRY:DriverEntry ntoskrnl.lib hal.lib /OUT:hvmonitor.sys
```

### Option 3: Makefile (WDK build environment)

Create a `sources` file:
```
TARGETNAME=hvmonitor
TARGETTYPE=DRIVER
SOURCES=driver.c vmx.c ept.c hooks.c log.c
AMD64_SOURCES=asm.asm
```

Then run `build` from the WDK build environment.

## Install & Load

```cmd
:: Copy driver
copy hvmonitor.sys C:\Windows\System32\drivers\

:: Create service
sc create HvMonitor type= kernel binPath= C:\Windows\System32\drivers\hvmonitor.sys

:: Start (enables hypervisor + hooks)
sc start HvMonitor

:: View logs in real-time
dbgview.exe   (Sysinternals DebugView, enable kernel capture)

:: Stop (devirtualizes + removes hooks)
sc stop HvMonitor

:: Remove
sc delete HvMonitor
```

## What It Logs

Every call to these two functions is logged with:

### RtlPcToFileHeader
- PID/TID of the caller
- Return address (who called it)
- PC address being queried
- Resolved module base address

### MmGetSystemRoutineAddress
- PID/TID of the caller
- Return address (who called it)
- Function name being resolved (Unicode string)
- Resolved address (or NULL if not found)

## Output Format

Logs appear in DbgPrint output (DebugView / WinDbg):

```
[HvLog] RtlPcToFileHeader | PID=4 TID=120 RetAddr=0xFFFFF80012345678 QueryPC=0xFFFFF800AABBCCDD ModuleBase=0xFFFFF800AA000000
[HvLog] MmGetSystemRoutineAddress | PID=1234 TID=5678 RetAddr=0xFFFFF80011223344 Func="ZwQuerySystemInformation" Resolved=0xFFFFF80055667788
```

## Architecture

```
  Guest OS (Windows)
       |
       | calls RtlPcToFileHeader / MmGetSystemRoutineAddress
       v
  [EPT Shadow Page]  ──── execute-only, contains JMP to HkXxx handler
       |
       | JMP triggers our hook handler
       v
  [HkRtlPcToFileHeader / HkMmGetSystemRoutineAddress]
       |
       | 1. Log caller info (PID, TID, return addr, params)
       | 2. Call original function (EPT temporarily shows original page)
       | 3. Log result
       | 4. Return to caller
       v
  Guest OS continues normally
```

The EPT hook is invisible to the guest: memory reads see the original
unmodified code, only execution is redirected through the shadow page.
