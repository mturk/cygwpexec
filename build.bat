@echo off
rem
rem Licensed under the Apache License, Version 2.0 (the "License");
rem you may not use this file except in compliance with the License.
rem You may obtain a copy of the License at
rem
rem     http://www.apache.org/licenses/LICENSE-2.0
rem
rem Unless required by applicable law or agreed to in writing, software
rem distributed under the License is distributed on an "AS IS" BASIS,
rem WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
rem See the License for the specific language governing permissions and
rem limitations under the License.
rem
rem Batch script for posix2winexec
rem
setlocal
set "SNAM=posix2winexec"
set "SVER=2.0.1"
set "CVER=2,0,1"
set "CFLAGS=/DSTR_VERSION=\"%SVER%\" /DSTR_INTNAME=\"%SNAM%\" %CFLAGS%"
rem
if /i "%~1" == "/x86" (
    set "MACHINE=X86"
) else (
    set "MACHINE=X64"
    set "CFLAGS=/DWIN64 %CFLAGS%"
)

rem
rem Clean previous build
for %%i in (exe obj res pdb) do del /F /Q  %SNAM%.%%i 2>NUL
if /i "%~1" == "/clean" exit /B 0
if /i "%~1" == "/test" (
    set "CFLAGS=/DDOTEST %CFLAGS%"
)
rem
cl /nologo /O2 /Ob2 /Zi /MD /W3 /DWIN32 %CFLAGS% /DUNICODE /D_UNICODE /DCONSOLE /c main.c /Fd%SNAM% /Fo%SNAM%.obj
rc /l 0x409 /n /d "NDEBUG" %RCOPTS% /d STR_VERSION=\"%SVER%\" /d STR_INTNAME=\"%SNAM%\" /d CSV_VERSION=%CVER% /fo %SNAM%.res util.rc
link /NOLOGO /OPT:REF /INCREMENTAL:NO /SUBSYSTEM:CONSOLE /MACHINE:%MACHINE% %LDFLAGS% %SNAM%.obj %SNAM%.res kernel32.lib %EXTRA_LIBS% /pdb:%SNAM%.pdb /OUT:%SNAM%.exe
if exist %SNAM%.exe.manifest mt -nologo -manifest %SNAM%.exe.manifest -outputresource:%SNAM%.exe;1
