Run windows applications under Cygwin (cygwpexec)
=================================================

Cygwin uses posix paths and environments which makes most of
the standard windows programs to fail because of path mismatch.
The traditional way of handling that is using Cygwin cygpath
utility which translates Cygwin (posix) paths to their windows
equivalents from shell.

For example a standard usage would be:

    program.exe "--f1=`cygpath -w /tmp/f1`" "`cygpath -w /usr/f1`" ...

This can become very complex and it requires that the shell
script is aware it runs inside the Cygwin environment.

cygwpexec utility does that automatically by replacing each posix
argument that contains path element with its windows equivalent.
It also replaces paths in the environment variable values making
sure the multiple path elements are correctly separated using
windows path separator `;`.

Using cygwpexec the upper example would become:

    cygwpexec program.exe --foo/tmp/f1 /usr/f1 ...

Before starting `program.exe` cygwpexec converts all command line
and environment variables to windows format.
