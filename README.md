# posix2wx

Run windows applications under Posix environment

## Overview

Cygwin and Msys2 use posix paths and environments which makes most of
the standard windows programs to fail because of path mismatch.
The traditional way of handling that is using Cygwin cygpath
utility which translates Cygwin (posix) paths to their windows
equivalents from shell.

For example a standard usage would be:
```
program.exe "--f1=`cygpath -w /tmp/f1`" "`cygpath -w /usr/f1`" ...
```
This can become very complex and it requires that the shell
script is aware it runs inside the Cygwin environment.

posix2wx utility does that automatically by replacing each posix
argument that contains path element with its windows equivalent.
It also replaces paths in the environment variable values making
sure the multiple path elements are correctly separated using
windows path separator `;`.

Using posix2wx the upper example would become:
```
posix2wx program.exe --f1=/tmp/f1 /usr/f1 ...
```
Before starting `program.exe` posix2wx converts all command line
and environment variables to windows format.

## Usage

Here is what the usage screen displays
```
Usage posix2wx [OPTIONS]... PROGRAM [ARGUMENTS]...
Execute PROGRAM [ARGUMENTS]...
Options are:

-d, -[-]debug       print replaced arguments and environment
                    instead executing PROGRAM
-v, -[-]version     print version information and exit.
-h, -[-]help        print this screen and exit.
-w, -[-]workdir DIR change working directory to DIR before calling PROGRAM
-r, -[-]root DIR    use DIR as posix root
```

Command options are case insensitive and can have only one dash
for short options. Long options can have one or two dashes.
This means that `-D -d -debug and --DeBug` are all valid options.

## Posix root

Posix root is used to replace posix parts with posix environment root
location inside Windows environment.

Use `--root <directory>` command line option to setup the install location
of the current posix subsystem.

You can use `POSIX_ROOT` or `CYGWIN_ROOT` environment variable instead.
If none are specified the `HOMEDIR` environment variable will be used.


If you installed your Cygwin inside `C:\cygwin64` then you will use
that for you root or `POSIX_ROOT`
```
    $ export POSIX_ROOT=C:/cygwin64
    ...
    $ posix2wx ... -f1=/usr/local
    or
    $ posix2wx -r C:/cygwin64 ... -f1=/usr/local

    --f1 parameter will evaluate to C:\cygwin64\usr\local
```

## License

The code in this repository is licensed under the [Apache-2.0 License](LICENSE.txt).
