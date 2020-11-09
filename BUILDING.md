# Building posix2wx

This project contains the source code for posix2wx, a program
that helps to run windows applications under Posix environment.

## Prerequisites

To compile posix2wx from source code you will need
Microsoft C/C++ Compiler from Microsoft Visual Studio 2010
or some later version.

The official distribution release is build by using
Custom Microsoft Compiler Toolkit Compilation from
<https://github.com/mturk/cmsc> which is based on compiler
that comes from Windows Driver Kit version 7.1.0

posix2wx uses Microsoft compiler, and command line tools to
produce the binary for Windows 64-bit version. The reason is
to simplify the source code and ensure portability across future
Visual Studio versions.

## Build

posix2wx release comes with posix2wx.exe binary. However in
case you need to create your own build simply
download posix2wx source release or clone
this repository and follow this simply rules.

### Build using CMSC

Presuming that you have downloaded and unzip CMSC release
to the root of C drive.

Open command prompt in the directory where you have
downloaded or cloned posix2wx and do the following

```no-highlight
> C:\cmsc-15.0_32\setenv.bat
Using default architecture: x64
Setting build environment for win-x64/0x0601

> nmake

Microsoft (R) Program Maintenance Utility Version 9.00.30729.207
...
```
In case there are no compile errors the posix2wx.exe is located
inside **x64_RELEASE** subdirectory.

### Build using other MSC versions

To build the posix2wx using Visual Studio you already
have on your box you will need to open the Visual Studio
native x64 command line tool. The rest is almost the same

Here is the example for Visual Studio 2012 (others are similar)

Inside Start menu select Microsoft Visual Studio 2012 then
click on Visual Studio Tools and click on
Open VC2012 x64 Native Tools Command Prompt, and then

```no-highlight
> cd C:\Some\Location\posix2wx
> nmake

Microsoft (R) Program Maintenance Utility Version 11.00.50727.1
...
```

### Makefile targets

Makefile has two additional targets which can be useful
for posix2wx development and maintenance

```no-highlight
> nmake clean
```

This will remove all produced binaries and object files
by simply deleting **x64_RELEASE** subdirectory.

```no-highlight
> nmake install PREFIX=C:\some\directory
```

Standard makefile install target that will
copy the executable to the PREFIX location.

This can be useful if you are building posix2wx with
some Continuous build application that need produced
binaries at a specific location for later use.

### Debug compile option

Posix2wx can be compiled to have additional --debug
command line option which when specified displays various
internal options at runtime.

When specified this option prints replaced arguments
and environment instead executing PROGRAM.

To compile posix2wix with  this option enabled
use the following:

```no-highlight
> nmake install EXTRA_CFLAGS=-D_HAVE_DEBUG_MODE
```

### Test compile option

For test suite purposed use the following flags:

```no-highlight
> nmake install EXTRA_CFLAGS=-D_TEST_MODE
```

This compiles posix2wx in such a way that instead
PROGRAM you can specify either `args` or `envp`.

On execution posix2wx will display either arguments
or environment variables and exit.

For example open cygwin shell and type the following:

```no-highlight
$ ./posix2wx.exe -r C:/posixroot argv /bin/:/foo:/:/tmp /I/usr/include
C:\posixroot\bin\;/foo:C:\posixroot;C:\posixroot\tmp
/IC:\posixroot\usr\include

```

This option is used for test purposes to verify the
produced path translation.

