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

#pragma warning(push, 3)

/*
 * Ignore Microsoft's interpretation of secure development
 * and the POSIX string handling API
 */
#define _CRT_SECURE_NO_DEPRECATE

/*
 * Ensure to use run-time dynamic linking, load Psapi.dll.
 */
#define PSAPI_VERSION 1
#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <process.h>
#include <io.h>

#define XPATH_MAX 16384

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

static wchar_t *cygroot = 0;

static const wchar_t *pathmatches[] = {
    L"/cygdrive/?/*",
    L"/usr/*",
    L"/tmp/*",
    L"/bin/*",
    L"/dev/*",
    L"/etc/*",
    L"/home/*",
    L"/lib/*",
    L"/proc/*",
    L"/sbin/*",
    L"/var/*",
    0
};

static const wchar_t *cygpenv[] = {
    L"ORIGINAL_PATH=",
    L"MINTTY_SHORTCUT=",
    L"EXECIGNORE=",
    L"PS1=",
    L"_=",
    L"CYGWIN_ROOT=",
    0
};


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

static wchar_t *xwcsdup(const wchar_t *s)
{
    wchar_t *d;
    if (s == 0)
        return 0;
    d = wcsdup(s);
    if (d == 0) {
        _wperror(L"wcsdup");
        _exit(1);
    }
    return d;
}

static wchar_t *xwcsndup(const wchar_t *s, size_t size)
{
    wchar_t *p;

    if (s == 0)
        return 0;
    if (wcslen(s) < size)
        size = wcslen(s);
    p = (wchar_t *)xmalloc((size + 2) * sizeof(wchar_t));
    memcpy(p, s, size * sizeof(wchar_t));
    return p;
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
    res = (wchar_t *)xmalloc((len + 2) * sizeof(wchar_t));
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
        memcpy(cp, argp, len * sizeof(wchar_t));
        cp += len;
    }

    va_end(adummy);
    return res;
}

/* Match = 0, NoMatch = 1, Abort = -1
 * Based loosely on sections of wildmat.c by Rich Salz
 */
static int wchrimatch(const wchar_t *str, const wchar_t *exp, int *match)
{
    int x, y, d;

    if (match == 0)
        match = &d;
    for (x = 0, y = 0; exp[y]; ++y, ++x) {
        if (!str[x] && exp[y] != L'*')
            return -1;
        if (exp[y] == L'*') {
            while (exp[++y] == L'*');
            if (!exp[y])
                return 0;
            while (str[x]) {
                int ret;
                *match = x;
                if ((ret = wchrimatch(&str[x++], &exp[y], match)) != 1)
                    return ret;
            }
            *match = 0;
            return -1;
        }
        else if (exp[y] != L'?') {
            if (towlower(str[x]) != towlower(exp[y]))
                return 1;
        }
    }
    return (str[x] != L'\0');
}

static int strstartswith(const wchar_t *str, const wchar_t *src)
{
    while (*str != 0) {
        if (towupper(*str) != *src)
            return 0;
        str++;
        src++;
        if (*src == L'\0')
            return 1;
    }

    return 0;
}

/**
 * Check if the argument is a cmdline option starting with
 * --option= and return the pointer to '='
 */
static wchar_t *cmdoptionval(wchar_t *str)
{

    if (str[0] == L'-' && str[1] == L'-') {
        /* Check for --foo=/...
         */
        wchar_t *p = str + 2;
        while (*p != L'\0')  {
            /*
             * We have a ':' before '='
             */
            if (*p == L':')
                return 0;
            if (*p == L'=')
                return p + 1;
            p++;
        }
    }
    return 0;
}

static int iscygwinpath(const wchar_t *str)
{
    const wchar_t **mp = pathmatches;

    while (*mp != 0) {
        if (wchrimatch(str, *mp, 0) == 0)
            return 1;
        mp++;
    }
    return 0;
}

static int envsort(const void *arg1, const void *arg2)
{
    /* Compare all of both strings: */
    return _wcsicmp( *(wchar_t **)arg1, *(wchar_t **)arg2);
}

static wchar_t **splitpath(const wchar_t *str, int *tokens)
{
    int c = 0;
    wchar_t **sa = 0;
    const wchar_t *b;
    const wchar_t *e;
    const wchar_t *s;
    const wchar_t *p;

    b = s = str;
    while (*b != L'\0') {
        if (*b++ == L':')
            c++;
    }
    sa = waalloc(c + 1);
    if (c > 0 ) {
        c  = 0;
        p = b = str;
        while ((e = wcschr(b, L':'))) {
            int cn = 1;
            int ch = *(e + 1);
            if (ch == L'/' || ch == L'.' || ch == L':' || ch == L'\0') {
                /* Is the previous token path or flag */
                if (iscygwinpath(p)) {
                    while ((ch = *(e + cn)) == L':') {
                        /* Drop multiple colons
                         * sa[c++] = xwcsdup(L"");
                        */
                        cn++;
                    }
                    sa[c++] = xwcsndup(b, e - b);
                    s = e + cn;
                }
            }
            p = b = e + cn;
        }
    }
    if (*s != L'\0')
        sa[c++] = xwcsdup(s);
    if (tokens != 0)
        *tokens = c;
    return sa;
}

static wchar_t *mergepath(wchar_t * const *paths)
{
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
    while (*pp != 0) {
        if (pp != paths)
            wcscat(rv, L";");
        wcscat(rv, *pp);
        pp++;
    }
    return rv;
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

static wchar_t *posix2win(const wchar_t *str)
{
    wchar_t *rv;
    wchar_t **pa;
    int i, tokens;

    if ((wcschr(str, L'/') == 0) || (*str == L'.')) {
        /* Nothing to do */
        return 0;
    }
    pa = splitpath(str, &tokens);
    for (i = 0; i < tokens; i++) {
        const wchar_t **mp = pathmatches;
        wchar_t *pp = pa[i];

        while (*mp != 0) {
            if (wchrimatch(pp, *mp, 0) == 0) {
                wchar_t windrive[] = { 0, L':', L'\\', 0};
                wchar_t *lp = pp;
                const wchar_t *wp;
                if (mp == pathmatches) {
                    /* /cygdrive/x/... absolute path */
                    windrive[0] = towupper(pp[10]);
                    wp  = windrive;
                    lp += 12;
                }
                else {
                    /* Posix internal path */
                    wp  = cygroot;
                }
                fs2bs(lp);
                pa[i] = xwcsvcat(wp, lp, 0);
                xfree(pp);
                break;
            }
            mp++;
        }
    }
    rv = mergepath(pa);
    wafree(pa);
    return rv;
}


static BOOL WINAPI console_handler(DWORD ctrl)
{
    switch (ctrl) {
        case CTRL_BREAK_EVENT:
            return FALSE;
        case CTRL_C_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_SHUTDOWN_EVENT:
        case CTRL_LOGOFF_EVENT:
            return TRUE;
        break;
    }
    return FALSE;
}

wchar_t *getpexe(DWORD pid)
{
    wchar_t buf[XPATH_MAX];
    wchar_t *pp = 0;
    DWORD  ppid = 0;
    HANDLE h;
    PROCESSENTRY32W e;

    h = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (h == INVALID_HANDLE_VALUE)
        return 0;

    e.dwSize = (DWORD)sizeof(PROCESSENTRY32W);
    if (Process32FirstW(h, &e)) {
        do {
            if (e.th32ProcessID == pid) {
                /* We found ourself :)
                 */
                ppid = e.th32ParentProcessID;
                break;
            }

        } while (Process32NextW(h, &e));
    }
    CloseHandle(h);
    if (ppid == 0)
        return 0;
    h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, ppid);
    if (h == 0)
        return 0;
    if (GetModuleFileNameExW(h, 0, buf, XPATH_MAX) != 0)
        pp = xwcsdup(buf);
    CloseHandle(h);
    return pp;
}

static wchar_t *getcygroot(wchar_t *argroot)
{
    wchar_t *r = argroot;

    if (r == 0) {
        /*
         * No --root-<PATH> was provided
         * Try CYGWIN_ROOT environment var
         */
        r = xgetenv(L"CYGWIN_ROOT");
    }
    if (r == 0) {
        /*
         * Find parent process and check
         * if it's path is cygwin
         */
        r = getpexe(GetCurrentProcessId());
        if (r != 0) {
            int x = 0;
            if (wchrimatch(r, L"*\\cygwin\\*", &x) == 0) {
                r[x + 7] = L'\0';
            }
            else if (wchrimatch(r, L"*\\cygwin64\\*", &x) == 0) {
                r[x + 9] = L'\0';
            }
            else if (wchrimatch(r, L"*\\cygwin32\\*", &x) == 0) {
                r[x + 9] = L'\0';
            }
            else {
                xfree(r);
                r = 0;
            }
        }
    }
    if (r != 0) {
        /*
         * Remove trailing slash (if present)
         */
        wchar_t *s = r;
        while (*s != L'\0') {
            if (*s == L'/' || *s == L'\\') {
                if(*(s + 1) == L'\0')
                    *s = L'\0';
                else
                    *s = L'\\';
            }
            s++;
        }
        s = xwcsvcat(r, L"\\bin\\bash.exe", 0);
        if (_waccess(s, 0) != 0) {
            xfree(r);
            r = 0;
        }
        xfree(s);
    }
    return r;
}

static int cygwpexec(int argc, wchar_t **wargv, int envc, wchar_t **wenvp)
{
    int i;
    intptr_t rp;
    wchar_t *p;
    wchar_t *e;
    wchar_t *o;

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
                 * We dont have --option=....
                 * Variable e points to value
                 */
                 e = o;
            }
            if ((p = posix2win(e)) != 0) {
                if (e != o) {
                    *e = L'\0';
                    if (debug) {
                        wprintf(L"     * %s%s\n", o, p);
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
        wprintf(L"\nEnvironment i (%d):\n", envc);
    for (i = 0; i < envc; i++) {

        if (debug) {
            wprintf(L"[%2d] : %s\n", i, wenvp[i]);
        }
        o = wenvp[i];
        if ((e = wcschr(o, L'=')) != 0) {
            e = e + 1;
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
    }
    qsort((void *)wenvp, envc, sizeof(wchar_t *), envsort);
    if (debug) {
        wprintf(L"\nEnvironment o (%d):\n", i);
        return 0;
    }
    _flushall();
    /* We have a valid environment. Install the console handler
     * XXX: Check if its needed ?
     */
    SetConsoleCtrlHandler((PHANDLER_ROUTINE)console_handler, TRUE);
    rp = _wspawnvpe(_P_WAIT, wargv[0], wargv, wenvp);
    if (rp == (intptr_t)-1) {
        rp = errno;
        _wperror(wargv[0]);
    }
    return (int)rp;
}

static int usage(int rv)
{
    FILE *os = rv == 0 ? stdout : stderr;
    fprintf(os, "Usage cygwpexec [OPTIONS]... PROGRAM [ARGUMENTS]...\n");
    fprintf(os, "Execute PROGRAM.\n\nOptions are:\n");
    fprintf(os, " -D, --debug      print replaced arguments and environment\n");
    fprintf(os, "                  instead executing PROGRAM.\n");
    fprintf(os, " -V, --version    print version information and exit.\n");
    fprintf(os, "     --help       print this screen and exit.\n");
    fprintf(os, "     --root=PATH  use PATH as cygwin root\n\n");
    if (rv == 0)
        fputs(aslicense, os);
    return rv;
}


static int version(int license)
{
    fprintf(stdout, "cygwpexec versiom %s compiled on %s\n", STR_VERSION, __DATE__);
    if (license)
        fputs(aslicense, stdout);
    return 0;
}

int wmain(int argc, const wchar_t **wargv, const wchar_t **wenv)
{
    int i, j = 0;
    wchar_t **dupwargv = 0;
    wchar_t **dupwenvp = 0;
    wchar_t *crp = 0;
    wchar_t strb[512];

    const wchar_t *opath = 0;
    const wchar_t *cpath = 0;

    int envc = 0;
    int narg = 0;
    int opts = 1;

    if (argc < 2)
        return usage(1);
    dupwargv = waalloc(argc);
    for (i = 1; i < argc; i++) {
        const wchar_t *p = wargv[i];
        if (opts) {
            if (p[0] == L'-' && p[1] != L'\0') {
                if (wcscmp(p, L"-V") == 0 || wcscmp(p, L"--version") == 0)
                    return version(1);
                else if (wcscmp(p, L"-D") == 0 || wcscmp(p, L"--debug") == 0)
                    debug = 1;
                else if (wcsncmp(p, L"--root=", 7) == 0)
                    crp = xwcsdup(wargv[i] + 7);
                else if (wcscmp(p, L"--help") == 0)
                    return usage(0);
                else
                    return usage(1);
                continue;
            }
            opts = 0;
        }
        dupwargv[narg++] = xwcsdup(wargv[i]);
    }

    cygroot = getcygroot(crp);
    if (!cygroot) {
        fprintf(stderr, "Cannot determine CYGWIN_ROOT\n\n");
        return usage(1);
    }
    else if (debug) {
        wprintf(L"cygwin root : %s\n", cygroot);
    }
    while (wenv[j] != 0) {

        ++j;
    }
    dupwenvp = waalloc(j + 3);
    for (i = 0; i < j; i++) {
        const wchar_t **e = cygpenv;
        const wchar_t *p  = wenv[i];

        while (*e != 0) {
            if (strstartswith(p, *e)) {
                /*
                 * Skip cygwin's private environment variable
                 */
                p = 0;
                break;
            }
            e++;
        }
        if (p != 0) {
            if ((opath == 0) && strstartswith(p, L"PATH=")) {
                opath = p;
            }
            else if ((cpath == 0) && strstartswith(p, L"CLEAN_PATH=")) {
                cpath = p + 6;
            }
            else {
                dupwenvp[envc++] = xwcsdup(p);
            }
        }
    }
    /*
     * Replace PATH with CLEAN_PATH if present
     */
    if (cpath != 0)
        dupwenvp[envc++] = xwcsdup(cpath);
    else if (opath != 0)
        dupwenvp[envc++] = xwcsdup(opath);
    /*
     * Add aditional environment variables
     */
    swprintf(strb, L"CYGWPEXEC_VER=%S", STR_VERSION);
    dupwenvp[envc++] = xwcsdup(strb);
    swprintf(strb, L"CYGWPEXEC_PID=%d", GetCurrentProcessId());
    dupwenvp[envc++] = xwcsdup(strb);
    dupwenvp[envc++] = xwcsvcat(L"CYGWIN_ROOT=", cygroot, 0);
    /*
     * Call main worken function
     */
    return cygwpexec(narg, dupwargv, envc, dupwenvp);
}
