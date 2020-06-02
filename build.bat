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
rem Batch script for cygwpexec
rem
setlocal
set "SVER=2.0.1"
set "CVER=2,0,1"
set "CFLAGS=/DSTR_VERSION=\"%SVER%\" %CFLAGS%"
rem
if /i "%~1" == "/x86" (
    set "MACHINE=X86"
) else (
    set "MACHINE=X64"
    set "CFLAGS=/DWIN64 %CFLAGS%"
)
rem
rem Clean previous build
for %%i in (exe obj res pdb) do del /F /Q  cygwpexec.%%i.exe 2>NUL
rem
cl /nologo /O2 /Ob2 /Zi /MD /W3 /DWIN32 %CFLAGS% /DUNICODE /D_UNICODE /DCONSOLE /c cygwpexec.c /Fdcygwpexec
rc /l 0x409 /n /d "NDEBUG" %RCOPTS% /d STR_VERSION=\"%SVER%\" /d CSV_VERSION=%CVER% cygwpexec.rc
link /NOLOGO /OPT:REF /INCREMENTAL:NO /SUBSYSTEM:CONSOLE /MACHINE:%MACHINE% %LDFLAGS% cygwpexec.obj cygwpexec.res kernel32.lib %EXTRA_LIBS% /pdb:cygwpexec.pdb /OUT:cygwpexec.exe
if exist cygwpexec.exe.manifest mt -nologo -manifest cygwpexec.exe.exe.manifest -outputresource:cygwpexec.exe;1
