/*
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations uSder the License.
 *
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <errno.h>
#include <process.h>
#include <fcntl.h>
#include <io.h>
#include <conio.h>
#include <direct.h>

#include "config.h"

#define IS_PSW(c) ((c) == L'/' || (c) == L'\\')

static int      debug      = 0;
static wchar_t *posixwroot = 0;

static const wchar_t *pathmatches[] = {
    L"/cygdrive/?/*",
    L"/?/*",
    L"/bin/*",
    L"/usr/*",
    L"/tmp/*",
    L"/home/*",
    L"/lib*/*",
    L"/sbin/*",
    L"/var/*",
    L"/run/*",
    L"/etc/*",
    L"/dev/*",
    L"/proc/*",
    L"/dir/*",
    L"/mingw*/*",
    L"/clang*/*",
    0
};

static const wchar_t *pathfixed[] = {
    L"/bin",
    L"/usr",
    L"/tmp",
    L"/home",
    L"/lib",
    L"/lib64",
    L"/sbin",
    L"/var",
    L"/run",
    L"/etc",
    L"/dir",
    0
};

static const wchar_t *posixpenv[] = {
    L"ORIGINAL_PATH=",
    L"ORIGINAL_TEMP=",
    L"ORIGINAL_TMP=",
    L"MINTTY_SHORTCUT=",
    L"EXECIGNORE=",
    L"SHELL=",
    L"TERM=",
    L"TERM_PROGRAM=",
    L"TERM_PROGRAM_VERSION=",
    L"PS1=",
    L"_=",
    L"!::=",
    L"!;=",
    L"POSIX_ROOT=",
    L"CYGWIN_ROOT=",
    L"PATH=",
    0
};

static const wchar_t *posixrenv[] = {
    L"POSIX_ROOT",
    L"CYGWIN_ROOT",
    L"HOMEDRIVE",
    0
};


static int usage(int rv)
{
    FILE *os = rv == 0 ? stdout : stderr;
    fprintf(os, "Usage %s [OPTIONS]... PROGRAM [ARGUMENTS]...\n", PROJECT_NAME);
    fprintf(os, "Execute PROGRAM [ARGUMENTS]...\n\nOptions are:\n");
    fprintf(os, " -d, -[-]debug       print replaced arguments and environment\n");
    fprintf(os, "                     instead executing PROGRAM.\n");
    fprintf(os, " -v, -[-]version     print version information and exit.\n");
    fprintf(os, " -h, -[-]help        print this screen and exit.\n");
    fprintf(os, " -w  -[-]workdir DIR change working directory to DIR before calling PROGRAM\n");
    fprintf(os, " -r  -[-]root DIR    use DIR as posix root\n\n");
    if (rv == 0)
        fputs(PROJECT_LICENSE, os);
    return rv;
}

static int version(int license)
{
    fprintf(stdout, "%s versiom %s compiled on %s\n",
            PROJECT_NAME, PROJECT_VERSION_STR,
            __DATE__ " " __TIME__);
    if (license)
        fputs(PROJECT_LICENSE, stdout);
    return 0;
}

/**
 * Malloc that causes process exit in case of ENOMEM
 */
static void *xmalloc(size_t size)
{
    void *p = calloc(size, 1);
    if (p == 0) {
        _wperror(L"xmalloc");
        _exit(1);
    }
    return p;
}

static wchar_t *xwalloc(size_t size)
{
    void *p = calloc(size, sizeof(wchar_t));
    if (p == 0) {
        _wperror(L"xwalloc");
        _exit(1);
    }
    return (wchar_t *)p;
}

static wchar_t **waalloc(size_t size)
{
    return (wchar_t **)xmalloc((size + 1) * sizeof(wchar_t *));
}

static void xfree(void *m)
{
    if (m != 0)
        free(m);
}

static void wafree(wchar_t **array)
{
    wchar_t **ptr = array;

    if (array == 0)
        return;
    while (*ptr != 0)
        xfree(*(ptr++));
    xfree(array);
}

static wchar_t *xwcsndup(const wchar_t *s, size_t size)
{
    wchar_t *p;
    size_t   n;

    if (s == 0)
        return 0;

    n = wcslen(s);
    if (n < size)
        size = n;
    p = xwalloc(size + 2);
    if (size > 0)
        wmemcpy(p, s, size);
    return p;
}

static wchar_t *xwcsdup(const wchar_t *s)
{
    return xwcsndup(s, SHRT_MAX - 2);
}

static wchar_t *xgetenv(const wchar_t *s)
{
    wchar_t *d;
    if (s == 0)
        return 0;
    if ((d = _wgetenv(s)) == 0)
        return 0;
    if (*d == L'\0')
        return 0;
    else
        return xwcsdup(d);
}

static size_t xwcslen(const wchar_t *s)
{
    if (s == 0)
        return 0;
    else
        return wcslen(s);
}

static wchar_t *xwcsconcat(const wchar_t *s1, const wchar_t *s2)
{
    wchar_t *cp, *res;
    size_t l1 = 0;
    size_t l2 = 0;

    l1 = xwcslen(s1);
    l2 = xwcslen(s2);
    /* Allocate the required string */
    res = xwalloc(l1 + l2 + 2);
    cp = res;

    if(l1 > 0)
        wmemcpy(cp, s1, l1);
    cp += l1;
    if(l2 > 0)
        wmemcpy(cp, s2, l2);
    return res;
}

/* Match = 0, NoMatch = 1, Abort = -1
 * Based loosely on sections of wildmat.c by Rich Salz
 */
static int wchrimatch(const wchar_t *wstr, const wchar_t *wexp)
{
    for ( ; *wexp != L'\0'; wstr++, wexp++) {
        if (*wstr == L'\0' && *wexp != L'*')
            return -1;
        switch (*wexp) {
            case L'*':
                wexp++;
                while (*wexp == L'*') {
                    /* Skip multiple stars */
                    wexp++;
                }
                if (*wexp == L'\0')
                    return 0;
                while (*wstr != L'\0') {
                    int rv;
                    if ((rv = wchrimatch(wstr++, wexp)) != 1)
                        return rv;
                }
                return -1;
            break;
            case L'?':
                if (*wstr > 127 || isalpha(*wstr) == 0)
                    return 1;
            break;
            default:
                if (*wstr != *wexp)
                    return 1;
            break;
        }
    }
    return (*wstr != L'\0');
}

static int strstartswith(const wchar_t *str, const wchar_t *src)
{
    while (*str != L'\0') {
        if (towlower(*str) != towlower(*src))
            break;
        str++;
        src++;
        if (*src == L'\0')
            return 1;
    }
    return 0;
}

static int iswinpath(const wchar_t *s)
{

    if (s[0] < 128 && isalpha(s[0]) && s[1] == L':') {
        if (IS_PSW(s[2]) || s[2] == L'\0')
            return 1;
    }
    return 0;
}

static int isrelpath(const wchar_t *s)
{
    int dots = 0;

    if (IS_PSW(s[0]))
        return 0;

    if (s[0] < 128 && isalpha(s[0]) && s[1] == L':')
        return 0;
    while (*(s++) == L'.') {
        if (dots++ > 2)
            return 0;
        if (IS_PSW(*s) || *s == L'\0')
            return 1;
    }
    return 0;
}

/* Is this a known posix path */
static int isposixpath(const wchar_t *str)
{
    int i = 0;
    const wchar_t **mp;
    const wchar_t  *ns;

    if (str[0] != L'/') {
        if (isrelpath(str))
            return 300;
        else
            return 0;
    }
    if (str[1] == L'\0') {
        /* Posix root */
        return 301;
    }
    if (wcscmp(str, L"/dev/null") == 0) {
        return 302;
    }
    ns = wcschr(str + 1, L'/');
    if (ns == 0) {
        /* No additional slashes */
        mp = pathfixed;
        while (mp[i] != 0) {
            if (wcscmp(str, mp[i]) == 0)
                return i + 200;
            i++;
        }
    }
    else {
        mp = pathmatches;
        while (mp[i] != 0) {
            if (wchrimatch(str, mp[i]) == 0)
                return i + 100;
            i++;
        }
    }
    return 0;
}

/**
 * Check if the argument is a cmdline option
 */
static wchar_t *cmdoptionval(wchar_t *str)
{
    wchar_t *s = str;

    /*
     * First check for [-/]I['"]/
     * or [-/]LIBPATH:
     */
    if (*s == L'-' || *s == L'/') {
        s++;
        if (towlower(*s) == L'i') {
            s++;
            if (isposixpath(s) || iswinpath(s))
                return s;
        }
    }
    if (isposixpath(str)) {
        /* The option starts with known path */
        return 0;
    }
    s = str + 1;
    while (*s != L'\0') {
        s++;
        if (*str == L'/') {
            if (*s == L'/')
                return 0;
            else if (*s == L':')
                return s + 1;

        }
        else if (*s == L'=')
            return s + 1;
    }
    return 0;
}

static int envsort(const void *arg1, const void *arg2)
{
    /* Compare all of both strings: */
    return _wcsicoll(*((wchar_t **)arg1), *((wchar_t **)arg2));
}

static void fs2bs(wchar_t *s)
{
    wchar_t *p = s;
    while (*p != 0) {
        if (*p == L'/')
            *p = L'\\';
        p++;
    }
}

static wchar_t **splitpath(const wchar_t *s, int *tokens)
{
    int c = 0;
    wchar_t **sa = 0;
    const wchar_t *b;
    const wchar_t *e;

    e = b = s;
    while (*e != L'\0') {
        if (*e++ == L':')
            c++;
    }
    sa = waalloc(c + 2);
    if (c > 0) {
        c  = 0;
        while ((e = wcschr(b, L':')) != 0) {
            int cn = 1;
            if (iswinpath(b)) {
                /*
                 * We have <ALPHA>:[/\]
                 */
                sa[c++] = xwcsdup(b);
                *tokens = c;
                return sa;
            }
            else {
                wchar_t *p;
                /* Is the previous token path or flag */
                p = xwcsndup(b, (size_t)(e - b));
                if (isposixpath(p)) {
                    while (*(e + cn) == L':') {
                        /* Drop multiple colons
                         */
                        cn++;
                    }
                }
                else {
                    /* Special case for /foo:next
                     * result is /foo:
                     * For /foo/bar:path
                     * result is /foo/bar
                     */
                    if (*p == L'/' && (wcschr(p + 1, L'/') == 0))
                        wcscat(p, L":");
                }
                sa[c++] = p;
                s = e + cn;
            }
            b = e + cn;
        }
    }
    if (*s != L'\0') {
        sa[c++] = xwcsdup(s);
    }
    *tokens = c;
    return sa;
}

static wchar_t *mergepath(wchar_t **paths)
{
    int  i, sc = 0;
    size_t len = 0;
    wchar_t  *rv;
    wchar_t **pp;

    pp = paths;
    while (*pp != 0) {
        len += wcslen(*pp) + 1;
        pp++;
    }
    rv = xwalloc(len + 1);
    pp = paths;
    for (i = 0; pp[i] != 0; i++) {
        len = wcslen(pp[i]);
        if (len > 0) {
            if (sc++ > 0) {
                wcscat(rv, L";");
            }
            if (pp[i][len - 1] == L':') {
                /* do not add semicolon before next path */
                sc = 0;
            }
            wcscat(rv, pp[i]);
        }
    }
    return rv;
}

static wchar_t *posix2win(wchar_t *pp)
{
    int m;
    wchar_t *rv;
    wchar_t  windrive[] = { 0, L':', L'\\', 0};

    if (wcschr(pp, L'/') == 0) {
        /* Nothing to do */
        return pp;
    }
    /*
     * Check for special paths
     */
    m = isposixpath(pp);
    if (m == 0) {
        /* Not a posix path */
        if (iswinpath(pp))
            fs2bs(pp);
        return pp;
    }
    else if (m == 100) {
        /* /cygdrive/x/... absolute path */
        windrive[0] = towupper(pp[10]);
        fs2bs(pp + 12);
        rv = xwcsconcat(windrive, pp + 12);
    }
    else if (m == 101) {
        /* /x/... msys absolute path */
        windrive[0] = towupper(pp[1]);
        fs2bs(pp + 3);
        rv = xwcsconcat(windrive, pp + 3);
    }
    else if (m == 300) {
        fs2bs(pp);
        return pp;
    }
    else if (m == 301) {
        rv = xwcsdup(posixwroot);
    }
    else if (m == 302) {
        /* replace /dev/null with NUL */
        rv = xwcsdup(L"NUL");
    }
    else {
        fs2bs(pp);
        rv = xwcsconcat(posixwroot, pp);
    }
    xfree(pp);
    return rv;
}

static wchar_t *convert2win(const wchar_t *str)
{
    wchar_t *rv;
    wchar_t **pa;
    int i, tokens;

    if (*str == L'\'' || wcschr(str, L'/') == 0) {
        /* Nothing to do */
        return 0;
    }
    pa = splitpath(str, &tokens);
    for (i = 0; i < tokens; i++) {
        wchar_t *pp = pa[i];
        pa[i] = posix2win(pp);
    }
    rv = mergepath(pa);
    wafree(pa);
    return rv;
}

/**
 * Remove trailing backslash and path separator(s)
 * so that we don't have problems with quoting
 * or appending
 */
static void rmtrailingsep(wchar_t *s)
{
    int i = (int)xwcslen(s);

    while (--i > 1) {
        if (IS_PSW(s[i]) || s[1] == L';')
            s[i] = L'\0';
        else
            break;
    }
}

static wchar_t *getposixwroot(wchar_t *argroot)
{
    wchar_t *r = argroot;

    if (r == 0) {
        const wchar_t **e = posixrenv;
        while (*e != 0) {
            if ((r = xgetenv(*e)) != 0) {
                /*
                 * Found root variable
                 */
                break;
            }
            e++;
        }
    }
    if (r != 0) {
        /*
         * Remove trailing slash (if present)
         */
        rmtrailingsep(r);
        fs2bs(r);
    }
    return r;
}

static int ppspawn(int argc, wchar_t **wargv, int envc, wchar_t **wenvp)
{
    int i, rc = 0;
    intptr_t rp;
    wchar_t *p;
    wchar_t *e;
    wchar_t *o;

    if (debug)
        wprintf(L"Arguments (%d):\n", argc);

    for (i = 0; i < argc; i++) {
        if (debug) {
            wprintf(L"[%2d] : %s\n", i, wargv[i]);
        }
        if (wcslen(wargv[i]) > 3) {
            o = wargv[i];
            if ((e = cmdoptionval(o)) == 0) {
                /*
                 * We dont have known option=....
                 */
                 e = o;
            }
            if ((p = convert2win(e)) != 0) {
                if (e != o) {
                    *e = L'\0';
                    if (debug)
                        wprintf(L"     + %s%s\n", o, p);
                    wargv[i] = xwcsconcat(o, p);
                    xfree(p);
                }
                else {
                    if (debug)
                        wprintf(L"     * %s\n", p);
                    wargv[i] = p;
                }
                xfree(o);
            }
        }
    }
    if (debug)
        wprintf(L"\nEnvironment variables (%d):\n", envc);
    for (i = 0; i < (envc - 2); i++) {

        if (debug)
            wprintf(L"[%2d] : %s\n", i, wenvp[i]);
        o = wenvp[i];
        if ((e = wcschr(o, L'=')) != 0) {
            ++e;
            if ((wcslen(e) > 3) && ((p = convert2win(e)) != 0)) {
                *e = L'\0';
                if (debug)
                    wprintf(L"     * %s%s\n", o, p);
                wenvp[i] = xwcsconcat(o, p);
                xfree(o);
                xfree(p);
            }
        }
    }
    if (debug) {
        wprintf(L"[%2d] : %s\n", i, wenvp[i]);
        i++;
        wprintf(L"[%2d] : %s\n", i, wenvp[i]);
    }        
    qsort((void *)wenvp, envc, sizeof(wchar_t *), envsort);
    if (debug) {
         _putws(L"");
        return 0;
    }
#ifdef DOTEST
    if (wcscmp(wargv[0], L"argv") == 0) {
        for (i = 1; i < argc; i++) {
            if (i > 1)
                _putws(L"");
             wprintf(L"%s", wargv[i]);
        }
        return 0;
    }
    if (wcscmp(wargv[0], L"envp") == 0) {
        for (i = 0; i < envc; i++) {
            if (i > 0)
                _putws(L"");
            wprintf(L"%s", wenvp[i]);
        }
        return 0;
    }
    fprintf(stderr, "unknown test %S .. use argv or envp\n", wargv[0]);
    return 1;
#endif
    _flushall();
    rp = _wspawnvpe(_P_WAIT, wargv[0], wargv, wenvp);
    if (rp == (intptr_t)-1) {
        rc = errno;
        _wperror(L"Fatal error _wspawnvpe()");
        fwprintf(stderr, L"Cannot execute: %s\n\n", wargv[0]);
        return usage(rc);
    }
    return (int)rp;
}

int wmain(int argc, const wchar_t **wargv, const wchar_t **wenv)
{
    int i, j = 0;
    wchar_t **dupwargv = 0;
    wchar_t **dupwenvp = 0;
    wchar_t *crp       = 0;
    wchar_t *cwd       = 0;
    wchar_t *opath;
    wchar_t  nnp[4] = { L'\0', L'\0', L'\0', L'\0' };

    int envc = 0;
    int narg = 0;
    int opts = 1;

    if (argc < 2)
        return usage(1);
    dupwargv = waalloc(argc);
    for (i = 1; i < argc; i++) {
        if (opts) {
            const wchar_t *p = wargv[i];
            /*
             * Simple argument parsing
             *
             */
            if (cwd == nnp) {
                cwd = xwcsdup(p);
                continue;
            }
            else if (crp == nnp) {
                crp = xwcsdup(p);
                continue;
            }

            if (*(p++) == L'-') {
                if (*p == L'\0')
                    return usage(1);
                if (*p == L'-') {
                    p++;
                    if (p[0] == L'\0' || p[1] == L'\0')
                        return usage(1);
                }
                if (_wcsicmp(p, L"v") == 0)
                    return version(0);
                else if (_wcsicmp(p, L"version") == 0)
                    return version(1);
                else if (_wcsicmp(p, L"d") == 0 || _wcsicmp(p, L"debug") == 0)
                    debug = 1;
                else if (_wcsicmp(p, L"h") == 0 || _wcsicmp(p, L"help") == 0)
                    return usage(0);
                else if (_wcsicmp(p, L"w") == 0 || _wcsicmp(p, L"workdir") == 0)
                    cwd = nnp;
                else if (_wcsicmp(p, L"r") == 0 || _wcsicmp(p, L"root") == 0)
                    crp = nnp;
                else
                    return usage(1);
                continue;
            }
            /* No more options */
            opts = 0;
        }
        dupwargv[narg++] = xwcsdup(wargv[i]);
    }
    if ((cwd == nnp) || (crp == nnp)) {
        fprintf(stderr, "Missing required parameter value\n\n");
        return usage(1);
    }
    opath = xgetenv(L"PATH");
    if (opath == 0) {
        fprintf(stderr, "Cannot determine initial PATH environment\n\n");
        return usage(1);
    }
    else {
        wchar_t *p = convert2win(opath);
        if (p != 0) {
            xfree(opath);
            opath = p;
        }
    }
    rmtrailingsep(opath);
    _wputenv_s(L"PATH", opath);

    posixwroot = getposixwroot(crp);
    if (posixwroot == 0) {
        /* Should not happen */
        fprintf(stderr, "Cannot determine POSIX_ROOT\n\n");
        return usage(1);
    }
#ifdef DOTEST
    debug = 0;
#endif
    if (debug) {
        printf("%s versiom %s (%s)\n",
                PROJECT_NAME, PROJECT_VERSION_STR,
                __DATE__ " " __TIME__);
        wprintf(L"POSIX_ROOT : %s\n\n", posixwroot);
    }
    if (cwd != 0) {
        /* Use the new cwd */
        rmtrailingsep(cwd);
        cwd = posix2win(cwd);
        if (_wchdir(cwd) != 0) {
            i = errno;
            _wperror(L"Fatal error _wchdir()");
            fwprintf(stderr, L"Invalid dir: %s\n\n", cwd);
            return usage(i);
        }
    }
    if (wenv != 0) {
        while (wenv[j] != 0)
            ++j;
    }

    dupwenvp = waalloc(j + 3);
    for (i = 0; i < j; i++) {
        const wchar_t **e = posixpenv;
        const wchar_t *p  = wenv[i];

        while (*e != 0) {
            if (strstartswith(p, *e)) {
                /*
                 * Skip private environment variable
                 */
                p = 0;
                break;
            }
            e++;
        }
        if (p != 0)
            dupwenvp[envc++] = xwcsdup(p);
    }

    /*
     * Add aditional environment variables
     */
    dupwenvp[envc++] = xwcsconcat(L"PATH=", opath);
    dupwenvp[envc++] = xwcsconcat(L"POSIX_ROOT=", posixwroot);

    xfree(opath);
    /*
     * Call main worker function
     */
    return ppspawn(narg, dupwargv, envc, dupwenvp);
}
