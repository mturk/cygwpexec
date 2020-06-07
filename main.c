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
 * limitations under the License.
 *
 */

#define _CRT_SECURE_NO_DEPRECATE
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0601
#define _WIN32_WINNT WINVER

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <process.h>
#include <fcntl.h>
#include <io.h>
#include <conio.h>
#include <direct.h>

static int debug = 0;

static const char aslicense[] = "\n"                                             \
    "Licensed under the Apache License, Version 2.0 (the ""License"");\n"        \
    "you may not use this file except in compliance with the License.\n"         \
    "You may obtain a copy of the License at\n\n"                                \
    "http://www.apache.org/licenses/LICENSE-2.0\n\n"                             \
    "Unless required by applicable law or agreed to in writing, software\n"      \
    "distributed under the License is distributed on an ""AS IS"" BASIS,\n"      \
    "WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.\n" \
    "See the License for the specific language governing permissions and\n"      \
    "limitations under the License.\n";

static const wchar_t *stdwinpaths = L";"    \
    L"%SystemRoot%\\System32;"              \
    L"%SystemRoot%;"                        \
    L"%SystemRoot%\\System32\\Wbem;"        \
    L"%SystemRoot%\\System32\\WindowsPowerShell\\v1.0";

static wchar_t *posixwroot = 0;
static wchar_t *realpwpath = 0;

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
    0
};

static const wchar_t *posixpenv[] = {
    L"ORIGINAL_PATH=",
    L"ORIGINAL_TEMP=",
    L"ORIGINAL_TMP=",
    L"MINTTY_SHORTCUT=",
    L"EXECIGNORE=",
    L"PS1=",
    L"_=",
    L"!::=",
    L"POSIX_ROOT=",
    L"SAFE_ENVVARS=",
    L"PATH=",
    L"CLEAN_PATH=",
    0
};

#define SAFE_WINENVC 15
static const wchar_t *safewinenv[SAFE_WINENVC] = {
    L"COMSPEC=",
    L"HOMEDRIVE=",
    L"NUMBER_OF_PROCESSORS=",
    L"OS=",
    L"PATHEXT=",
    L"PROCESSOR_ARCHITECTURE=",
    L"PROCESSOR_IDENTIFIER=",
    L"PROCESSOR_LEVEL=",
    L"SYSTEMDRIVE=",
    L"SYSTEMROOT=",
    L"PROCESSOR_REVISION=",
    L"TEMP=",
    L"TMP=",
    L"USERNAME=",
    L"WINDIR="
};

static int usage(int rv)
{
    FILE *os = rv == 0 ? stdout : stderr;
    fprintf(os, "Usage %s [OPTIONS]... PROGRAM [ARGUMENTS]...\n", STR_INTNAME);
    fprintf(os, "Execute PROGRAM [ARGUMENTS]...\n\nOptions are:\n");
    fprintf(os, " -D, -[-]debug      print replaced arguments and environment\n");
    fprintf(os, "                    instead executing PROGRAM.\n");
    fprintf(os, " -V, -[-]version    print version information and exit.\n");
    fprintf(os, " -?, -[-]help       print this screen and exit.\n");
    fprintf(os, " -C, -[-]clean      use CLEAN_PATH environment variable instead PATH\n");
    fprintf(os, "     -[-]env=LIST   pass only environment variables listed inside LIST\n");
    fprintf(os, "                    variables must be comma separated.\n");
    fprintf(os, "     -[-]cwd=DIR    change working directory to DIR before calling PROGRAM\n");
    fprintf(os, "     -[-]root=DIR   use DIR as posix root\n\n");
    if (rv == 0)
        fputs(aslicense, os);
    return rv;
}

static int version(int license)
{
    fprintf(stdout, "%s versiom %s compiled on %s\n", STR_INTNAME, STR_VERSION, __DATE__);
    if (license)
        fputs(aslicense, stdout);
    return 0;
}

/**
 * Maloc that causes process exit in case of ENOMEM
 */
static void *xmalloc(size_t size)
{
    void *p = calloc(size, 1);
    if (p == 0) {
        _wperror(L"malloc");
        _exit(1);
    }
    return p;
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

    if (s == 0)
        return 0;
    if (wcslen(s) < size)
        size = wcslen(s);
    p = (wchar_t *)xmalloc((size + 2) * sizeof(wchar_t));
    if (size > 0)
        memcpy(p, s, size * sizeof(wchar_t));
    return p;
}

static wchar_t *xwcsdup(const wchar_t *s)
{
    return xwcsndup(s, SHRT_MAX);
}

static wchar_t *xgetenv(const wchar_t *s)
{
    wchar_t *d;
    if (s == 0)
        return 0;
    if ((d = _wgetenv(s)) == 0)
        return 0;
    return xwcsdup(d);
}

static wchar_t *xwcsvcat(const wchar_t *str, ...)
{
    wchar_t *cp, *argp, *res;
    size_t  saved_lengths[32];
    int     nargs = 0;
    size_t  len;
    va_list adummy;

    /* Pass one --- find length of required string */
    if (str == 0)
        return 0;

    len = wcslen(str);
    va_start(adummy, str);
    saved_lengths[nargs++] = len;
    while ((cp = va_arg(adummy, wchar_t *)) != NULL) {
        size_t cplen = wcslen(cp);
        if (nargs < 32)
            saved_lengths[nargs++] = cplen;
        len += cplen;
    }
    va_end(adummy);

    /* Allocate the required string */
    res = (wchar_t *)xmalloc((len + 4) * sizeof(wchar_t));
    cp = res;

    /* Pass two --- copy the argument strings into the result space */
    va_start(adummy, str);

    nargs = 0;
    len = saved_lengths[nargs++];
    memcpy(cp, str, len * sizeof(wchar_t));
    cp += len;

    while ((argp = va_arg(adummy, wchar_t *)) != NULL) {
        if (nargs < 32)
            len = saved_lengths[nargs++];
        else
            len = wcslen(argp);
        if (len > 0) {
            memcpy(cp, argp, len * sizeof(wchar_t));
            cp += len;
        }
    }

    va_end(adummy);
    return res;
}

static size_t endswithchrs(const wchar_t *str, const wchar_t *ch)
{
    const wchar_t *s;
    size_t n = wcslen(str);

    s = str + n;
    if (s > str) {
        --s;
        if (wcschr(ch, *s) != 0) {
            return (size_t)(s - str);
        }
    }
    return n;
}

#if 0
# define TLWR(x)    towlower(x)
#else
# define TLWR(x)    x
#endif
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
            break;
            default:
                if (TLWR(*wstr) != TLWR(*wexp))
                    return 1;
            break;
        }
    }
    return (*wstr != L'\0');
}

static int strstartswith(const wchar_t *str, const wchar_t *src, int icase)
{
    while (*str != L'\0') {
        wchar_t wch = icase ? toupper(*str) : *str;
        if (wch != *src)
            return 0;
        str++;
        src++;
        if (*src == L'\0')
            return 1;
    }
    return 0;
}

/* Is this a known posix path */
static int isposixpath(const wchar_t *str)
{
    int i = 0;
    const wchar_t **mp;

    if (*str != '/')
        return 0;
    if (wcscmp(str, L"/dev/null") == 0) {
        return 200;
    }
    if (wcschr(str + 1, L'/') == 0) {
        /* No additional slashes */
        mp = pathfixed;
        while (mp[i] != 0) {
            size_t n = endswithchrs(str, L"'\"");
            if (wcsncmp(str, mp[i], n) == 0)
                return i + 201;
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
            if (*s == L'\'' || *s == L'"')
                s++;
            if (*s == L'/')
                return s;
        }
        else if (strstartswith(s, L"LIBPATH:", 1)) {
            return s + 8;
        }
    }
    if (isposixpath(str)) {
        /* The option starts with known path */
        return 0;
    }
    s = str;
    while (*s != L'\0') {
        s++;
        if (*s == L'=') {
            /* Return poonter after '=' */
            return s + 1;
        }
    }
    return 0;
}

static int envsort(const void *arg1, const void *arg2)
{
    /* Compare all of both strings: */
    return _wcsicmp(*(wchar_t **)arg1, *(wchar_t **)arg2);
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

static wchar_t **splitsev(const wchar_t *str)
{
    int i, c = 0;
    wchar_t **sa = 0;
    const wchar_t *b;

    if (*str == L'\0')
        return 0;
    b = str;
    while (*b != L'\0') {
        if (*b++ == L',')
            c++;
    }
    sa = waalloc(c + SAFE_WINENVC + 2);
    if (c > 0 ) {
        const wchar_t *e;
        c  = 0;
        b = str;
        while ((e = wcschr(b, L','))) {
            int cn = 1;
            size_t nn = (size_t)(e - b);
            while (*(e + cn) == L',') {
                /* Drop multiple colons
                 */
                cn++;
            }
            if (nn > 0) {
                sa[c] = xwcsndup(b, nn);
                wcscat(sa[c++], L"=");
                str = e + cn;
            }
            b = e + cn;
        }
    }
    if (*str != L'\0') {
        sa[c] = xwcsdup(str);
        wcscat(sa[c++], L"=");
    }
    for (i = 0; i < SAFE_WINENVC; i++) {
        sa[c++] = xwcsdup(safewinenv[i]);
    }
    return sa;
}

static wchar_t **splitpath(const wchar_t *str, int *tokens)
{
    int c = 0;
    wchar_t **sa = 0;
    const wchar_t *b;
    const wchar_t *e;
    const wchar_t *s;

    b = s = str;
    while (*b != L'\0') {
        if (*b++ == L':')
            c++;
    }
    sa = waalloc(c + 2);
    if (c > 0 ) {
        c  = 0;
        b = str;
        while ((e = wcschr(b, L':'))) {
            int cn = 1;
            int ch = *(e + 1);
            if (((b + 1) == e) && isalpha(*b & 0xFF) && (ch == L'/' || ch == L'\\' || ch == L'\0')) {
                /*
                 * We have <ALPHA>:[/\]
                 * Find next colon
                 */
                if ((ch != L'\0') && ((e = wcschr(b + 2, L':')) != 0)) {
                    sa[c] = xwcsndup(b, e - b);
                    fs2bs(sa[c++]);
                    s = e + cn;
                }
                else {
                    /* No more paths */
                    sa[c] = xwcsdup(s);
                    fs2bs(sa[c++]);
                    s = 0;
                    break;
                }
            }
            else if (ch == L'/' || ch == L'.' || ch == L':' || ch == L'\0') {
                size_t nn = (size_t)(e - b);
                /* Is the previous token path or flag */
                if (isposixpath(b)) {
                    while (*(e + cn) == L':') {
                        /* Drop multiple colons
                         */
                        cn++;
                    }
                }
                else {
                    /* Copy ':' as well */
                    nn++;
                }
                sa[c++] = xwcsndup(b, nn);
                s = e + cn;
            }
            b = e + cn;
        }
    }
    if (s && *s != L'\0') {
        sa[c++] = xwcsdup(s);
    }
    *tokens = c;
    return sa;
}

static wchar_t *mergepath(wchar_t * const *paths)
{
    int  i, sc = 2;
    size_t len = 0;
    wchar_t *rv;
    wchar_t *const *pp;

    pp = paths;
    while (*pp != 0) {
        len += wcslen(*pp) + 1;
        pp++;
    }
    rv = xmalloc((len + 1) * sizeof(wchar_t));
    pp = paths;
    for (i = 0; pp[i] != 0; i++) {
        len = wcslen(pp[i]);
        if ((len == 0) || (pp[i][len - 1] == L':')) {
            /* do not add semicolon before next path */
            sc = 0;
        }
        if (i > 0 && sc != 1) {
            wcscat(rv, L";");
        }
        wcscat(rv, pp[i]);
        sc++;
    }
    return rv;
}

static wchar_t *posix2winpath(wchar_t *pp)
{
    int m;
    wchar_t *rv;

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
        return pp;
    }
    else if (m == 100) {
        /* /cygdrive/x/... absolute path */
        wchar_t windrive[] = { 0, L':', L'\\', 0};
        windrive[0] = towupper(pp[10]);
        fs2bs(pp + 12);
        rv = xwcsvcat(windrive, pp + 12, 0);
    }
    else if (m == 101) {
        /* /x/... msys absolute path */
        wchar_t windrive[] = { 0, L':', L'\\', 0};
        windrive[0] = towupper(pp[1]);
        fs2bs(pp + 3);
        rv = xwcsvcat(windrive, pp + 3, 0);
    }
    else if (m == 200) {
        /* replace /dev/null with NUL */
        rv = xwcsdup(L"NUL");
    }
    else {
        fs2bs(pp);
        rv = xwcsvcat(posixwroot, pp, 0);
    }
    xfree(pp);
    return rv;
}

static wchar_t *posix2win(const wchar_t *str)
{
    wchar_t *rv;
    wchar_t **pa;
    int i, tokens;

    if (wcschr(str, L'/') == 0) {
        /* Nothing to do */
        return 0;
    }
    pa = splitpath(str, &tokens);
    for (i = 0; i < tokens; i++) {
        wchar_t *pp = pa[i];
        pa[i] = posix2winpath(pp);
    }
    rv = mergepath(pa);
    wafree(pa);
    return rv;
}

/*
 * Remove trailing char if it's one of chars
 */
static void rmtrailingchrs(wchar_t *str, const wchar_t *ch)
{
    size_t n = endswithchrs(str, ch);

    if (n > 0) {
        *(str + n) = L'\0';
    }
}

static wchar_t *getposixwroot(wchar_t *argroot)
{
    wchar_t *r = argroot;

    if (r == 0) {
        /*
         * No --root-<PATH> was provided
         * Try POSIX_ROOT environment var
         */
        r = xgetenv(L"POSIX_ROOT");
    }
    if (r != 0) {
        wchar_t *s;
        /*
         * Remove trailing slash (if present)
         */
        rmtrailingchrs(r, L"/\\");
        /*
         * Verify if the provided path
         * contains bash.exe
         */
        fs2bs(r);
        s = xwcsvcat(r, L"\\bin\\bash.exe", 0);
        if (_waccess(s, 0) != 0) {
            fprintf(stderr, "Cannot determine valid POSIX_ROOT\n\n");
            usage(1);
            _exit(1);
        }
        xfree(s);
    }
    return r;
}

static void __cdecl stdinrw(void *p)
{
    unsigned char buf[512];
    int nr;
    while ((nr = _read(_fileno(stdin), buf, 512)) > 0) {
        _write(*((int *)p), buf, nr);
    }
}

static int ppspawn(int argc, wchar_t **wargv, int envc, wchar_t **wenvp)
{
    int i, rc = 0;
    intptr_t rp;
    wchar_t *p;
    wchar_t *e;
    wchar_t *o;
    int org_stdin;
    int stdinpipe[2];

    if (debug) {
        wprintf(L"Arguments (%d):\n", argc);
    }
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
            if (*e == L'\'' || *e == L'"')
                e++;
            if ((p = posix2win(e)) != 0) {
                if (e != o) {
                    *e = L'\0';
                    if (debug) {
                        wprintf(L"     + %s%s\n", o, p);
                    }
                    wargv[i] = xwcsvcat(o, p, 0);
                    xfree(p);
                }
                else {
                    if (debug) {
                        wprintf(L"     * %s\n", p);
                    }
                    wargv[i] = p;
                }
                xfree(o);
            }
        }
    }
    if (debug)
        wprintf(L"\nEnvironment variables (%d):\n", envc);
    for (i = 0; i < envc; i++) {

        if (debug) {
            wprintf(L"[%2d] : %s\n", i, wenvp[i]);
        }
        o = wenvp[i];
        if ((e = wcschr(o, L'=')) != 0) {
            e = e + 1;
            if (*e == L'\'' || *e == '"')
                e++;
            if ((wcslen(e) > 3) && ((p = posix2win(e)) != 0)) {
                *e = L'\0';
                if (debug) {
                    wprintf(L"     * %s%s\n", o, p);
                }
                wenvp[i] = xwcsvcat(o, p, 0);
                xfree(o);
                xfree(p);
            }
        }
        else if (debug) {
            wprintf(L"<%2d> : %s\n", i, wenvp[i]);
        }
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
    if(_pipe(stdinpipe, 512, O_NOINHERIT) == -1) {
        rc = errno;
        _wperror(L"Fatal error _pipe()");
        return rc;
    }
    org_stdin = _dup(_fileno(stdin));
    if(_dup2(stdinpipe[0], _fileno(stdin)) != 0) {
        rc = errno;
        _wperror(L"Fatal error _dup()");
        return rc;
    }
    _close(stdinpipe[0]);
    _flushall();
    _wputenv(realpwpath);
    rp = _wspawnvpe(_P_NOWAIT, wargv[0], wargv, wenvp);
    if (rp == (intptr_t)-1) {
        rc = errno;
        _wperror(L"Fatal error _wspawnvpe()");
        fwprintf(stderr, L"Cannot execute: %s\n\n", wargv[0]);
        return usage(rc);
    }
    else {
        int *ppipeid = &stdinpipe[1];
        /*
         * Restore original stdin handle
         */
        if(_dup2(org_stdin, _fileno(stdin)) != 0) {
            rc = errno;
            _wperror(L"Fatal error _dup2()");
            return rc;
        }
        _close(org_stdin);
        /* Create stdin thread */
        _beginthread(stdinrw, 0, (void *)ppipeid);
        if (_cwait(&rc, rp, _WAIT_CHILD) == (intptr_t)-1) {
            rc = errno;
            _wperror(wargv[0]);
        }
    }
    return rc;
}

int wmain(int argc, const wchar_t **wargv, const wchar_t **wenv)
{
    int i, j = 0;
    wchar_t **dupwargv = 0;
    wchar_t **dupwenvp = 0;
    wchar_t **safeenvp = 0;
    wchar_t *crp = 0;
    wchar_t *sev = 0;
    wchar_t *cwd = 0;
    wchar_t *opath = 0;
    wchar_t *cpath = 0;

    int cleanpath = 0;
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
            if (*(p++) == L'-') {
                if (*p == L'-') {
                    if (*(p + 1) == L'\0') {
                        /* We have --
                         * Stop processing our options
                         */
                        opts = 0;
                        continue;
                    }
                    if (wcslen(p + 1) > 2)
                        p++;
                    else
                        return usage(1);
                }
                if (*p == L'\0')
                    return usage(1);
                else if (wcscmp(p, L"V") == 0)
                    return version(0);
                else if (_wcsicmp(p, L"version") == 0)
                    return version(1);
                else if (wcscmp(p, L"D") == 0 || _wcsicmp(p, L"debug") == 0)
                    debug = 1;
                else if (wcscmp(p, L"C") == 0 || _wcsicmp(p, L"clean") == 0)
                    cleanpath = 1;
                else if (wcscmp(p, L"?") == 0 || _wcsicmp(p, L"help") == 0)
                    return usage(0);
                else if (_wcsnicmp(p, L"env=", 4) == 0)
                    sev = xwcsdup(p + 4);
                else if (_wcsnicmp(p, L"cwd=", 4) == 0)
                    cwd = xwcsdup(p + 4);
                else if (_wcsnicmp(p, L"root=", 5) == 0)
                    crp = xwcsdup(p + 5);
                else
                    return usage(1);
                continue;
            }
            /* No more options */
            opts = 0;
        }
        dupwargv[narg++] = xwcsdup(wargv[i]);
    }
    opath = xgetenv(L"PATH");
    if (cleanpath)
        cpath = xgetenv(L"CLEAN_PATH");
    posixwroot = getposixwroot(crp);
    if (posixwroot == 0) {
#if 0
        fprintf(stderr, "Cannot determine POSIX_ROOT\n\n");
        return usage(1);
#else
        if (cwd != 0)
            posixwroot = xwcsdup(cwd);
        else
            posixwroot = _wgetcwd(0, 0);
#endif
    }
    if (debug) {
        wprintf(L"POSIX_ROOT : %s\n", posixwroot);
    }
    if (sev == 0) {
        sev = xgetenv(L"SAFE_ENVVARS");
    }
    if (sev != 0) {
        /*
         * We have array of comma separated
         * environment variables that are allowed to be passed to the child
         */
        safeenvp = splitsev(sev);
        if (safeenvp == 0) {
            fprintf(stderr, "SAFE_ENVVARS cannot be empty list\n\n");
            return usage(1);
        }
    }
    if (cwd != 0) {
        /* Use the new cwd */
        cwd = posix2winpath(cwd);
        if (_wchdir(cwd) != 0) {
            i = errno;
            _wperror(L"Fatal error _wchdir()");
            fwprintf(stderr, L"Invalid dir: %s\n\n", cwd);
            return usage(i);
        }
    }
    while (wenv[j] != 0) {

        ++j;
    }

    dupwenvp = waalloc(j + 3);
    for (i = 0; i < j; i++) {
        const wchar_t **e = posixpenv;
        const wchar_t *p  = wenv[i];

        while (*e != 0) {
            if (strstartswith(p, *e, 0)) {
                /*
                 * Skip private environment variable
                 */
                p = 0;
                break;
            }
            e++;
        }
        if (p == 0)
            continue;
        if (safeenvp) {
            e = safeenvp;
            p = 0;
            while (*e != 0) {
                if (strstartswith(wenv[i], *e, 1)) {
                    /*
                     * We have safe environment variable
                     */
                    p = wenv[i];
                    break;
                }
                e++;
            }
        }
        if (p != 0) {
            dupwenvp[envc++] = xwcsdup(p);
        }
    }
    /*
     * Replace PATH with CLEAN_PATH if present
     */
    if (cpath != 0) {
        wchar_t *sebuf;
        wchar_t *inbuf;

        sebuf = posix2win(cpath);
        inbuf = xwcsvcat(sebuf ? sebuf : cpath, stdwinpaths, 0);
        xfree(sebuf);
        sebuf = (wchar_t *)xmalloc((8192) * sizeof(wchar_t));
        /* Add standard set of Windows paths */
        i = ExpandEnvironmentStringsW(inbuf, sebuf, 8190);
        if ((i == 0) || (i > 8190)) {
            fprintf(stderr, "Failed to expand standard Environment variables. (rv = %d)\n", i);
            fprintf(stderr, "for \'%S\'\n\n", inbuf);
            return usage(1);
        }
        if ((wcschr(sebuf, L'%') != 0) && (wchrimatch(sebuf, L"*%*%*") == 0)) {
            fprintf(stderr, "Failed to resolve Environment variables for CLEAN_PATH\n");
            fprintf(stderr, "for \'%S\'\n\n", sebuf);
            return usage(1);
        }
        realpwpath = xwcsvcat(L"PATH=", sebuf, 0);
        xfree(inbuf);
        xfree(sebuf);
        xfree(cpath);
    }
    else if (opath != 0) {
        wchar_t *pxbuf;

        pxbuf = posix2win(opath);
        realpwpath = xwcsvcat(L"PATH=", pxbuf ? pxbuf : opath, 0);
        xfree(pxbuf);
    }
    xfree(opath);
    if (realpwpath == 0) {
        fprintf(stderr, "Cannot determine PATH environment\n\n");
        return usage(1);
    }
    /*
     * Add aditional environment variables
     */
    dupwenvp[envc++] = realpwpath;
    dupwenvp[envc++] = xwcsvcat(L"POSIX_ROOT=", posixwroot, 0);
    /*
     * Call main worker function
     */
    return ppspawn(narg, dupwargv, envc, dupwenvp);
}
