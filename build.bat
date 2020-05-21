@echo off
rem
rem Copyright (c) 2011 The MyoMake Project <http://www.myomake.org>
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
if /i "%~1" == "/gui" (
    set "SUBSYTEM=WINDOWS"
    set "OUTFILE=cygwpexecw.exe"
    shift
) else (
    set "SUBSYTEM=CONSOLE"
    set "OUTFILE=cygwpexec.exe"
)
if /i "%~1" == "/x64" (
    set "MACHINE=X64"
    set "CFLAGS=/DWIN64 %CFLAGS%"
) else (
    set "MACHINE=X86"
)
cl /nologo /TC /O2 /Ob2 /Zi /MD /W3 /DWIN32 %CFLAGS% /DUNICODE /D_UNICODE /D%SUBSYTEM% /c cygwpexec.c /Fdcygwpexec
rc /l 0x409 /d "NDEBUG" %RCOPTS% cygwpexec.rc
link /NOLOGO /OPT:REF /INCREMENTAL:NO /SUBSYSTEM:%SUBSYTEM% /MACHINE:%MACHINE% %LDFLAGS% cygwpexec.obj cygwpexec.res kernel32.lib psapi.lib %EXTRA_LIBS% /pdb:cygwpexec.pdb /OUT:%OUTFILE%
@if exist %OUTFILE%.manifest mt -nologo -manifest %OUTFILE%.exe.manifest -outputresource:%OUTFILE%;1
