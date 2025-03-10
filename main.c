#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// taken from https://github.com/rxi/lite/blob/master/src/main.c
#ifdef _WIN32
#include <windows.h>
#elif __linux__
#include <unistd.h>
#elif __APPLE__
#include <mach-o/dyld.h>
#endif

#if __STDC_VERSION__ >= 201112L
#include <stdnoreturn.h>
#define NORETURN noreturn
#elif defined(__GCC__) || defined(__clang__)
#define NORETURN __attribute__((noreturn))
#else
#define NORETURN
#endif

#include "fe/src/fe.h"

#define CTX_SIZE (1024 * 1024 * 1024) // 1MB
#define STR_SIZE 2048

#define DECLARE_APIFN(n) {"internal--" #n, apif_##n}
#define DEFINE_APIFN(n) fe_Object *apif_##n(fe_Context *ctx, fe_Object *args)

// taken from fe/src/fe.c itself
static const char *typenames[] = {"pair", "free",  "nil",  "number", "symbol", "string",
                                  "func", "macro", "prim", "cfunc",  "ptr"};

static inline NORETURN void throw_error(fe_Context *ctx, const char *fmt, ...)
{
    char error[256];
    va_list args;
    va_start(args, fmt);
    vsprintf(error, fmt, args);
    va_end(args);
    fe_error(ctx, error);
    abort(); // fe_error doesn't return so this just silences a compiler warning
}

#define NIL fe_bool(ctx, false)
#define IS_NIL(o) fe_isnil(ctx, o)

#define NEXT_ARG() fe_nextarg(ctx, &args)
#define MORE_ARGS !IS_NIL(args)

#define NEXT_AS_STRING(d, s) fe_tostring(ctx, NEXT_ARG(), d, s)
#define NEXT_AS_NUMBER() fe_tonumber(ctx, NEXT_ARG())

// TODO: I have no idea if this function works
static inline fe_Object *readfile(fe_Context *ctx, const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp)
        throw_error(ctx, "couldn't open %s: %s", path, strerror(errno));

    fe_Object *res = NIL;
    int gc = fe_savegc(ctx);
    for (;;)
    {
        fe_Object *obj;
        if (!(obj = fe_readfp(ctx, fp)))
            break;
        res = fe_eval(ctx, obj);
        fe_restoregc(ctx, gc);
    }

    fclose(fp);

    return res;
}

DEFINE_APIFN(readfile)
{
    char path[128];
    NEXT_AS_STRING(path, sizeof path);
    return readfile(ctx, path);
}

DEFINE_APIFN(write)
{
    fe_writefp(ctx, NEXT_ARG(), stdout);
    return NIL;
}

typedef struct
{
    char *s;
    int p;
} StrCursor;

#define newCursor(s) ((StrCursor){s, 0})
#define cursorWrite(c, ch) ((c)->s[(c)->p++] = (ch))
#define cursorRead(c) ((c)->s[(c)->p++])
#define cursorEnd(c) ((c)->s[(c)->p] = '\0', s)

static void cursorWriteFn(fe_Context *ctx, void *udata, char c)
{
    StrCursor *s = (StrCursor *)udata;
    cursorWrite(s, c);
}

static void cursorWriteObject(StrCursor *s, fe_Context *ctx, fe_Object *o, int qt)
{
    fe_write(ctx, o, cursorWriteFn, s, qt);
}

DEFINE_APIFN(error)
{
    char error[256];
    StrCursor s = newCursor(error);
    cursorWriteObject(&s, ctx, NEXT_ARG(), false);
    cursorEnd(&s);
    fe_error(ctx, error);
    return NIL;
}

DEFINE_APIFN(concat)
{
    fe_Object *a = NEXT_ARG(), *b = NEXT_ARG();
    bool qt = !IS_NIL(args) && !IS_NIL(NEXT_ARG());

    char str[STR_SIZE];
    StrCursor s = newCursor(str);
    cursorWriteObject(&s, ctx, a, qt);
    cursorWriteObject(&s, ctx, b, qt);
    cursorEnd(&s);
    return fe_string(ctx, str);
}

DEFINE_APIFN(slice)
{
    char str[STR_SIZE];
    NEXT_AS_STRING(str, sizeof str);
    int a = NEXT_AS_NUMBER();
    int b = strlen(str);
    if (!MORE_ARGS)
        b = NEXT_AS_NUMBER();

    str[b + 1] = '\0';
    return fe_string(ctx, str + a);
}

DEFINE_APIFN(numtoch)
{
    char ch = NEXT_AS_NUMBER();
    return fe_string(ctx, &ch);
}

DEFINE_APIFN(chtonum)
{
    char ch;
    NEXT_AS_STRING(&ch, 1);
    return fe_number(ctx, ch);
}

DEFINE_APIFN(readbyte)
{
    return fe_number(ctx, getchar());
}

typedef struct
{
    const char *name;
    fe_CFunc func;
} APIFunc;

APIFunc api[] = {
    DECLARE_APIFN(readfile), DECLARE_APIFN(error),

    DECLARE_APIFN(write),    DECLARE_APIFN(concat),  DECLARE_APIFN(slice),
    DECLARE_APIFN(chtonum),  DECLARE_APIFN(numtoch),

    DECLARE_APIFN(readbyte),
};

#define ARRLEN(l) (sizeof(l) / sizeof(l[0]))

// taken from https://github.com/rxi/lite/blob/master/src/main.c and modified
static void get_exe_filename(char *buf, int sz)
{
#if _WIN32
    int len = GetModuleFileName(NULL, buf, sz - 1);
    buf[len] = '\0';
#elif __linux__
    char path[512];
    sprintf(path, "/proc/%d/exe", getpid());
    int len = readlink(path, buf, sz - 1);
    buf[len] = '\0';
#elif __APPLE__
    unsigned int size = sz;
    _NSGetExecutablePath(buf, &size);
#else
    strcpy(buf, "./false");
#endif
}

static inline NORETURN void die(const char *fmt, ...)
{
    char error[256];
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    exit(EXIT_FAILURE);
}

int main(int argc, const char *argv[])
{
    void *data = malloc(CTX_SIZE);
    fe_Context *ctx = fe_open(data, CTX_SIZE);

    for (int i = 0; i < ARRLEN(api); i++)
    {
        APIFunc *f = api + i;
        fe_set(ctx, fe_symbol(ctx, f->name), fe_cfunc(ctx, f->func));
    }

    char exedir[2048];
    get_exe_filename(exedir, sizeof exedir);
    for (int i = strlen(exedir) - 1; i >= 0; i--)
        if (exedir[i] == '/' || exedir[i] == '\\')
        {
            if (i + 5 + 1 <= sizeof exedir)
            {
                exedir[i] = '\0';
                strcpy(&exedir[i], "/data");
                break;
            }
            else
                die("the path %s is located at is too long; can't access the server's code", argv[0]);
        }

    if (chdir(exedir))
        die("couldn't change process directory to %s: %s", exedir, strerror(errno));

    readfile(ctx, "init.fe");

    fe_close(ctx);
    free(data);

    return 0;
}
