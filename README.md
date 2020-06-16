Run windows applications under Posix envinonment
================================================

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

Here is what the usage screan displays
```
    Usage posix2wx [OPTIONS]... PROGRAM [ARGUMENTS]...
    Execute PROGRAM [ARGUMENTS]...
    Options are:

    -D, -[-]debug      print replaced arguments and environment
                       instead executing PROGRAM
    -V, -[-]version    print version information and exit.
    -?, -[-]help       print this screen and exit.
    -C, -[-]clean      use CLEAN_PATH environment variable instead PATH
        -[-]env=LIST   pass only environment variables listed inside LIST
                       variables must be separated space character.
    -[-]cwd=DIR        change working directory to DIR before calling PROGRAM
    -[-]root=DIR       use DIR as posix root
```
Note that long command options are case insensitive and have one or two dashes
which means that
```
-debug
-Debug
--DEBUG
```
are all valid oprions which cause processing and displaying processed
command line and arguments, without executing the `PROGRAM` itself

## Clean PATH

When you pass `-clean` option to posix2wx the program will replace `PATH`
environment variable with `CLEAN_PATH` variable and evalue standard windows
minimum path requirements. The final PATH environment variable will be
evaluated as
```
    PATH=%CLEAN_PATH%;%SystemRoot%\\System32;%SystemRoot%;
         %SystemRoot%\\System32\\Wbem;
         %SystemRoot%\\System32\\WindowsPowerShell\\v1.0
```
Note that the new `PATH` will be evauated using `ExpandEnvironmentStrings` function and the lenght of expanded
variables must not exceed 8190 characters.
For example
```
$ export CLEAN_PATH=/cygdrive/c/perl/bin:%ProgramFiles%\\SomeProgram"
$ posix2wx -clean cmd.exe /c set

will replace PATH with processed CLEAN_PATH
```

Note that `-clean and CLEAN_PATH` will be applied to `posix2wx` before calling spawn function meaning
that the program to be run and all of its dependencies must be in that new PATH.

## Safe environment

The safe environment allows that you pass to child program only the subset of environment variables listed
as part of `--env` option. Beside those variables the "Standard" Windows system variables are
passed as well like `COMPUTERNAME TEMP TMP ... etc`.
For example
```
$ posix2wx -env="TERM SHELL CMAKE_HOME" cmd.exe /c set
```
Will allow to pass to chill program only the standard plus `TERM SHELL and CMAKE_HOME` environment
variables.
Instead using command line `-env` you can use `SAFE_ENVVARS` environment variable that has the
same effect as listing them as options when calling posix2wx
```
$ export SAFE_ENVVARS="TERM SHELL CMAKE_HOME"
$ posix2wx cmd.exe /c set

has the same effect as calling

$ posix2wx -env="TERM SHELL CMAKE_HOME" cmd.exe /c set
```

Note that both `CLEAN_PATH` and `SAFE_ENVVARS` are never passed to the child process.


