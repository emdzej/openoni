/* The BSL script engine: see sl.h. */
#include "sl.h"

#include "crc32.h"
#include "vfs.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- tokens (SLiToken_Make 0x47dac0's types) ---- */

enum {
    T_ASSIGN = 0, T_PLUS = 1, T_MINUS = 2, T_AND = 3, T_OR = 4, T_NOT = 5, T_EQ = 6, T_NE = 7, T_LT = 8, T_GT = 9,
    T_LE = 10, T_GE = 11, T_LPAREN = 12, T_RPAREN = 13, T_LBRACE = 14, T_RBRACE = 15, T_LBRACKET = 16,
    T_RBRACKET = 17, T_SEMI = 18, T_COLON = 19, T_COMMA = 20, T_BAR = 21, T_IDENT = 22, T_INT = 23, T_BOOL = 24,
    T_FLOAT = 25, T_STRING = 26, T_SCHEDULE = 27, T_ITERATE = 28, T_REPEAT = 29, T_RETURN = 30, T_KSTRING = 31,
    T_EVERY = 32, T_USING = 35, T_SLEEP = 36, T_KBOOL = 37, T_ELSE = 38, T_FORK = 39, T_FUNC = 40, T_OVER = 41,
    T_VOID = 43, T_FOR = 44, T_KINT = 45, T_VAR = 46, T_AT = 47, T_IF = 48, T_EOF = 49,
    /* the port's: the operators the lexer has no single token for */
    T_STAR = 60, T_SLASH = 61, T_KFLOAT = 34,
};

typedef struct {
    uint8_t type, eol;      /* eol: a line end follows (the flag SLiToken_Make sets on the token before) */
    uint16_t line;
    const char *text;
} Token;

/* ---- interned strings ---- */

typedef struct Str { struct Str *next; char s[]; } Str;
static Str *strs;
const char *sl_intern(const char *s)
{
    for (Str *p = strs; p; p = p->next)
        if (!strcmp(p->s, s)) return p->s;
    size_t n = strlen(s);
    Str *p = malloc(sizeof *p + n + 1);
    if (!p) return "";
    memcpy(p->s, s, n + 1);
    p->next = strs, strs = p;
    return p->s;
}

static const struct { const char *w; uint8_t t; } KEYWORDS[] = {
    { "at", T_AT }, { "eq", T_EQ }, { "if", T_IF }, { "ne", T_NE }, { "or", T_OR }, { "and", T_AND },
    { "for", T_FOR }, { "int", T_KINT }, { "var", T_VAR }, { "bool", T_KBOOL }, { "else", T_ELSE },
    { "func", T_FUNC }, { "fork", T_FORK }, { "over", T_OVER }, { "void", T_VOID }, { "every", T_EVERY },
    { "float", T_KFLOAT }, { "sleep", T_SLEEP }, { "using", T_USING }, { "repeat", T_REPEAT },
    { "return", T_RETURN }, { "string", T_KSTRING }, { "iterate", T_ITERATE }, { "schedule", T_SCHEDULE },
};

static char errbuf[256];

static int lex(const char *src, const char *file, Token **out)
{
    int cap = 1024, n = 0;
    Token *tk = malloc(sizeof *tk * cap);
    uint16_t line = 1;
    const char *p = src;
    char buf[257];
    for (;;) {
        if (n + 1 >= cap) {
            Token *g = realloc(tk, sizeof *tk * (cap *= 2));
            if (!g) return free(tk), -1;
            tk = g;
        }
        char c = *p;
        if (!c) break;
        p++;
        if (c == '\n' || c == '\r') {
            if ((c == '\n' && *p == '\r') || (c == '\r' && *p == '\n')) p++;
            line++;
            if (n) tk[n - 1].eol = 1;
            continue;
        }
        if (isspace((unsigned char)c)) continue;
        if (c == '#') {     /* to the line's end */
            while (*p && *p != '\n' && *p != '\r') p++;
            continue;
        }
        Token *t = &tk[n];
        t->eol = 0;
        t->line = line;
        int k = 0;
        bool number = isdigit((unsigned char)c) || (c == '.' && isdigit((unsigned char)*p)) ||
                      (c == '-' && (isdigit((unsigned char)*p) || *p == '.'));
        if (c == 'f' && isdigit((unsigned char)*p)) {   /* "f5": a number, the f dropped */
            c = *p++;
            number = true;
        }
        if (number) {
            t->type = T_INT;
            buf[k++] = c;
            while ((isdigit((unsigned char)*p) || *p == '.') && k < 256) {
                if (*p == '.') t->type = T_FLOAT;
                buf[k++] = *p++;
            }
            if (c == '.') t->type = T_FLOAT;
            buf[k] = 0;
            t->text = sl_intern(buf);
            n++;
            continue;
        }
        if (isalpha((unsigned char)c) || c == '_') {
            buf[k++] = c;
            while ((isalnum((unsigned char)*p) || *p == '_') && k < 256) buf[k++] = *p++;
            buf[k] = 0;
            t->type = T_IDENT;
            for (size_t w = 0; w < sizeof KEYWORDS / sizeof *KEYWORDS; w++)
                if (!strcmp(buf, KEYWORDS[w].w)) t->type = KEYWORDS[w].t;
            if (!strcmp(buf, "true") || !strcmp(buf, "false")) {
                t->type = T_BOOL;
                strcpy(buf, buf[0] == 't' ? "1" : "0");
            }
            t->text = sl_intern(buf);
            n++;
            continue;
        }
        if (c == '"') {
            while (*p && *p != '"' && *p != '\n' && *p != '\r' && k < 256) buf[k++] = *p++;
            if (*p == '"') p++;
            buf[k] = 0;
            t->type = T_STRING;
            t->text = sl_intern(buf);
            n++;
            continue;
        }
        t->text = "";
        switch (c) {
        case '=': t->type = T_ASSIGN; if (*p == '=') p++, t->type = T_EQ; break;
        case '+': t->type = T_PLUS; break;
        case '-': t->type = T_MINUS; break;
        case '*': t->type = T_STAR; break;
        case '/': t->type = T_SLASH; break;
        case '!': t->type = T_NOT; if (*p == '=') p++, t->type = T_NE; break;
        case '(': t->type = T_LPAREN; break;
        case ')': t->type = T_RPAREN; break;
        case '{': t->type = T_LBRACE; break;
        case '}': t->type = T_RBRACE; break;
        case '[': t->type = T_LBRACKET; break;
        case ']': t->type = T_RBRACKET; break;
        case ';': t->type = T_SEMI; break;
        case ':': t->type = T_COLON; break;
        case ',': t->type = T_COMMA; break;
        case '|': t->type = T_BAR; if (*p == '|') p++, t->type = T_OR; break;
        case '&': if (*p == '&') { p++; t->type = T_AND; break; } /* fall through */
        case '<': t->type = T_LT; if (*p == '=') p++, t->type = T_LE; break;
        case '>': t->type = T_GT; if (*p == '=') p++, t->type = T_GE; break;
        default:
            snprintf(errbuf, sizeof errbuf, "%s:%u: illegal character '%c'", file, line, c);
            free(tk);
            return -1;
        }
        n++;
    }
    tk[n].type = T_EOF, tk[n].eol = 1, tk[n].line = line, tk[n].text = "";
    if (n) tk[n - 1].eol = 1;
    *out = tk;
    return n + 1;
}

/* ---- the database ---- */

enum {
    OP_PUSH, OP_LOAD_GLOBAL, OP_STORE_GLOBAL, OP_LOAD_LOCAL, OP_STORE_LOCAL, OP_CALL, OP_FORK, OP_RET, OP_JMP,
    OP_JZ, OP_SLEEP, OP_POP, OP_BIN, OP_NOT, OP_NEG, OP_LOAD_ENGINE, OP_STORE_ENGINE,
};
typedef struct { uint8_t op; uint8_t argc; uint16_t line; int32_t a; } Op;

typedef struct {
    const char *name;
    SlType ret;
    int nparams, nlocals;
    SlType ptype[8];
    uint32_t code;          /* first op */
    bool defined;
} Func;

typedef struct { const char *name; SlValue v; SlType type; } Global;   /* type: as declared (SL_VOID: any) */
typedef struct { const char *name; SlCommand fn; void *user; } Command;
typedef struct { const char *name; SlType type; void *p; } EngineVar;

static Op *code;
static uint32_t n_code, cap_code;
static SlValue *consts;
static uint32_t n_consts, cap_consts;
static Func *funcs;
static int n_funcs, cap_funcs;
static Global *globals;
static int n_globals, cap_globals;
static Command *cmds;
static int n_cmds, cap_cmds;
static EngineVar *evars;
static int n_evars, cap_evars;
static SlStats stats;
static SlLogFn logfn;

void sl_set_log(SlLogFn fn) { logfn = fn; }

/* A debug aid: every call of a script function, and of message and the dialog commands, logged. */
static bool tracing;
void sl_set_trace(bool on) { tracing = on; }
static void say(const char *m) { if (logfn) logfn(m); }

#define GROW(p, n, cap) ((n) < (cap) || ((p) = realloc((p), sizeof *(p) * ((cap) = (cap) ? (cap) * 2 : 64))) != NULL)

static int find_func(const char *name)
{
    for (int i = 0; i < n_funcs; i++) if (funcs[i].name == name) return i;
    return -1;
}
static int find_global(const char *name)
{
    for (int i = 0; i < n_globals; i++) if (globals[i].name == name) return i;
    return -1;
}
static int find_cmd(const char *name)
{
    for (int i = 0; i < n_cmds; i++) if (cmds[i].name == name) return i;
    return -1;
}
static int find_evar(const char *name)
{
    for (int i = 0; i < n_evars; i++) if (evars[i].name == name) return i;
    return -1;
}

void sl_register(const char *name, SlCommand fn, void *user)
{
    name = sl_intern(name);
    int i = find_cmd(name);
    if (i < 0) {
        if (!GROW(cmds, n_cmds, cap_cmds)) return;
        i = n_cmds++;
    }
    cmds[i] = (Command){ name, fn, user };
}

static void add_evar(const char *name, SlType t, void *p)
{
    name = sl_intern(name);
    int i = find_evar(name);
    if (i < 0) {
        if (!GROW(evars, n_evars, cap_evars)) return;
        i = n_evars++;
    }
    evars[i] = (EngineVar){ name, t, p };
}
void sl_register_int(const char *n, int32_t *v) { add_evar(n, SL_INT, v); }
void sl_register_float(const char *n, float *v) { add_evar(n, SL_FLOAT, v); }
void sl_register_bool(const char *n, bool *v) { add_evar(n, SL_BOOL, v); }

/* ---- values ---- */

static SlValue v_int(int32_t i) { return (SlValue){ SL_INT, i, (float)i, NULL }; }
static SlValue v_float(float f) { return (SlValue){ SL_FLOAT, (int32_t)f, f, NULL }; }
static SlValue v_str(const char *s) { return (SlValue){ SL_STRING, 0, 0, s }; }

static float as_float(SlValue v) { return v.type == SL_FLOAT ? v.f : v.type == SL_STRING ? (float)atof(v.s) : (float)v.i; }
static int32_t as_int(SlValue v) { return v.type == SL_FLOAT ? (int32_t)v.f : v.type == SL_STRING ? atoi(v.s) : v.i; }
static bool truthy(SlValue v) { return v.type == SL_STRING ? v.s && *v.s : v.type == SL_FLOAT ? v.f != 0 : v.i != 0; }

/* SLrExpr_ConvertValue 0x47b030: to the declared type */
static SlValue convert(SlValue v, SlType t)
{
    switch (t) {
    case SL_INT: return v_int(as_int(v));
    case SL_BOOL: { SlValue b = v_int(truthy(v)); b.type = SL_BOOL; return b; }
    case SL_FLOAT: return v_float(as_float(v));
    case SL_STRING:
        if (v.type == SL_STRING) return v;
        { char b[32]; if (v.type == SL_FLOAT) snprintf(b, sizeof b, "%g", v.f); else snprintf(b, sizeof b, "%d", v.i); return v_str(sl_intern(b)); }
    default: return v;
    }
}

static SlValue literal(const Token *t)
{
    switch (t->type) {
    case T_INT: return v_int(atoi(t->text));
    case T_FLOAT: return v_float((float)atof(t->text));
    case T_BOOL: { SlValue b = v_int(t->text[0] == '1'); b.type = SL_BOOL; return b; }
    default: return v_str(t->text);
    }
}

/* ---- the compiler ---- */

typedef struct {
    const Token *t;
    int pos, n;
    const char *file;
    Func *fn;
    const char *locals[64];
    SlType ltype[64];
    int nlocals, high;      /* high: the most locals live at once, the frame's size */
    bool failed;
} Parser;

static const Token *peek(Parser *P) { return &P->t[P->pos < P->n ? P->pos : P->n - 1]; }
static const Token *next(Parser *P) { const Token *t = peek(P); if (P->pos < P->n - 1) P->pos++; return t; }
static bool accept(Parser *P, int type) { if (peek(P)->type == type) { next(P); return true; } return false; }
static void fail(Parser *P, const char *what)
{
    if (!P->failed) snprintf(errbuf, sizeof errbuf, "%s:%u: %s (at '%s')", P->file, peek(P)->line, what, peek(P)->text);
    P->failed = true;
}
static void expect(Parser *P, int type, const char *what) { if (!accept(P, type)) fail(P, what); }

static uint32_t emit(Parser *P, uint8_t op, int32_t a, uint8_t argc)
{
    if (!GROW(code, n_code, cap_code)) { fail(P, "out of memory"); return 0; }
    code[n_code] = (Op){ op, argc, peek(P)->line, a };
    return n_code++;
}
static int32_t add_const(SlValue v)
{
    if (!GROW(consts, n_consts, cap_consts)) return 0;
    consts[n_consts] = v;
    return (int32_t)n_consts++;
}

static int local_slot(Parser *P, const char *name)
{
    for (int i = P->nlocals - 1; i >= 0; i--) if (P->locals[i] == name) return i;
    return -1;
}

static void expr(Parser *P);

static bool is_type_kw(int t) { return t == T_KINT || t == T_KFLOAT || t == T_KBOOL || t == T_KSTRING || t == T_VOID; }
static SlType type_of_kw(int t)
{
    return t == T_KINT ? SL_INT : t == T_KFLOAT ? SL_FLOAT : t == T_KBOOL ? SL_BOOL : t == T_KSTRING ? SL_STRING : SL_VOID;
}

/* A name read as a value: a local, a global, an engine variable, else the word itself (BSL takes bare
   words as strings: "particle lock1_locklight01 do stop"). */
static void name_value(Parser *P, const char *name)
{
    int k;
    if ((k = local_slot(P, name)) >= 0) emit(P, OP_LOAD_LOCAL, k, 0);
    else if ((k = find_global(name)) >= 0) emit(P, OP_LOAD_GLOBAL, k, 0);
    else if ((k = find_evar(name)) >= 0) emit(P, OP_LOAD_ENGINE, k, 0);
    else emit(P, OP_PUSH, add_const(v_str(name)), 0);
}

/* A call: name(args, ...) or, in a statement, name arg arg ... to the line's end. */
static void call(Parser *P, const char *name, bool statement, bool forked)
{
    int argc = 0;
    if (accept(P, T_LPAREN)) {
        if (!accept(P, T_RPAREN)) {
            do {
                expr(P);
                argc++;
            } while (accept(P, T_COMMA));
            expect(P, T_RPAREN, "expecting ')'");
        }
    } else if (statement && !P->t[P->pos - 1].eol) {
        for (;;) {
            int ty = peek(P)->type;
            if (ty == T_SEMI || ty == T_RBRACE || ty == T_EOF) break;
            bool last = peek(P)->eol;
            const Token *t = next(P);
            if (t->type == T_IDENT) name_value(P, t->text);
            else if (t->type == T_INT || t->type == T_FLOAT || t->type == T_BOOL || t->type == T_STRING)
                emit(P, OP_PUSH, add_const(literal(t)), 0);
            else if (t->type == T_MINUS && (peek(P)->type == T_INT || peek(P)->type == T_FLOAT)) {
                SlValue v = literal(next(P));
                v.i = -v.i, v.f = -v.f;
                emit(P, OP_PUSH, add_const(v), 0);
                last = P->t[P->pos - 1].eol;
            } else if (t->type == T_COMMA) {
                if (last) break;
                continue;
            } else emit(P, OP_PUSH, add_const(v_str(t->text)), 0);  /* keywords as words: "do", "at" */
            argc++;
            if (last) break;
        }
    }
    emit(P, forked ? OP_FORK : OP_CALL, add_const(v_str(name)), (uint8_t)argc);
}

static void primary(Parser *P)
{
    const Token *t = next(P);
    switch (t->type) {
    case T_INT: case T_FLOAT: case T_BOOL: case T_STRING:
        emit(P, OP_PUSH, add_const(literal(t)), 0);
        return;
    case T_LPAREN:
        expr(P);
        expect(P, T_RPAREN, "expecting ')'");
        return;
    case T_NOT: primary(P); emit(P, OP_NOT, 0, 0); return;
    case T_MINUS: primary(P); emit(P, OP_NEG, 0, 0); return;
    case T_IDENT:
        if (peek(P)->type == T_LPAREN || (find_func(t->text) >= 0 && local_slot(P, t->text) < 0)) call(P, t->text, false, false);
        else name_value(P, t->text);
        return;
    default:
        fail(P, "expecting a value");
    }
}

static int bin_prec(int t)
{
    switch (t) {
    case T_OR: return 1;
    case T_AND: return 2;
    case T_EQ: case T_NE: case T_LT: case T_GT: case T_LE: case T_GE: return 3;
    case T_PLUS: case T_MINUS: return 4;
    case T_STAR: case T_SLASH: return 5;
    default: return 0;
    }
}

static void expr_prec(Parser *P, int min)
{
    primary(P);
    for (;;) {
        int op = peek(P)->type, pr = bin_prec(op);
        if (!pr || pr < min) return;
        next(P);
        expr_prec(P, pr + 1);
        emit(P, OP_BIN, op, 0);
    }
}
static void expr(Parser *P) { expr_prec(P, 1); }

static void block(Parser *P);

static void statement(Parser *P)
{
    const Token *t = peek(P);
    switch (t->type) {
    case T_LBRACE: block(P); return;
    case T_SEMI: next(P); return;
    case T_IF: {
        next(P);
        expect(P, T_LPAREN, "expecting '(' after if");
        expr(P);
        expect(P, T_RPAREN, "expecting ')'");
        uint32_t jz = emit(P, OP_JZ, 0, 0);
        statement(P);
        if (accept(P, T_ELSE)) {
            uint32_t j = emit(P, OP_JMP, 0, 0);
            code[jz].a = (int32_t)n_code;
            statement(P);
            code[j].a = (int32_t)n_code;
        } else code[jz].a = (int32_t)n_code;
        return;
    }
    case T_SLEEP:
        next(P);
        expr(P);
        emit(P, OP_SLEEP, 0, 0);
        accept(P, T_SEMI);
        return;
    case T_FORK: {
        next(P);
        const Token *n = next(P);
        if (n->type != T_IDENT) { fail(P, "expecting a function to fork"); return; }
        call(P, n->text, true, true);
        accept(P, T_SEMI);
        return;
    }
    case T_RETURN:
        next(P);
        if (!t->eol && peek(P)->type != T_SEMI && peek(P)->type != T_RBRACE) expr(P);
        else emit(P, OP_PUSH, add_const((SlValue){ SL_VOID, 0, 0, NULL }), 0);
        emit(P, OP_RET, 0, 0);
        accept(P, T_SEMI);
        return;
    case T_VAR: {       /* a local */
        next(P);
        SlType ty = type_of_kw(next(P)->type);
        const Token *n = next(P);
        if (P->nlocals >= 64) { fail(P, "too many locals"); return; }
        int slot = P->nlocals++;
        if (P->nlocals > P->high) P->high = P->nlocals;   /* a local of an inner block counts too */
        P->locals[slot] = n->text, P->ltype[slot] = ty;
        if (accept(P, T_ASSIGN)) expr(P);
        else emit(P, OP_PUSH, add_const(convert(v_int(0), ty)), 0);
        emit(P, OP_STORE_LOCAL, slot, (uint8_t)ty);
        accept(P, T_SEMI);
        return;
    }
    case T_IDENT: {
        next(P);
        if (accept(P, T_ASSIGN)) {      /* assignment */
            expr(P);
            int k;
            if ((k = local_slot(P, t->text)) >= 0) emit(P, OP_STORE_LOCAL, k, (uint8_t)P->ltype[k]);
            else if ((k = find_global(t->text)) >= 0) emit(P, OP_STORE_GLOBAL, k, 0);
            else if ((k = find_evar(t->text)) >= 0) emit(P, OP_STORE_ENGINE, k, 0);
            else {      /* an engine variable not registered yet: kept as a global */
                if (!GROW(globals, n_globals, cap_globals)) { fail(P, "out of memory"); return; }
                globals[n_globals] = (Global){ t->text, v_int(0), SL_VOID };
                emit(P, OP_STORE_ENGINE, -1 - n_globals++, 0);
            }
            accept(P, T_SEMI);
            return;
        }
        call(P, t->text, true, false);
        emit(P, OP_POP, 0, 0);
        accept(P, T_SEMI);
        return;
    }
    default:
        fail(P, "expecting a statement");
        next(P);
    }
}

static void block(Parser *P)
{
    expect(P, T_LBRACE, "expecting '{'");
    int locals = P->nlocals;
    while (!P->failed && peek(P)->type != T_RBRACE && peek(P)->type != T_EOF) statement(P);
    expect(P, T_RBRACE, "expecting '}'");
    P->nlocals = locals > P->nlocals ? P->nlocals : locals;
}

/* The first pass over a file: its globals (var type name [= literal]) and function names. */
static void declare(Parser *P)
{
    while (!P->failed && peek(P)->type != T_EOF) {
        if (accept(P, T_VAR)) {
            SlType ty = type_of_kw(next(P)->type);
            const Token *n = next(P);
            SlValue v = convert(v_int(0), ty);
            if (accept(P, T_ASSIGN)) {
                bool neg = accept(P, T_MINUS);
                v = convert(literal(next(P)), ty);
                if (neg) v.i = -v.i, v.f = -v.f;
            }
            accept(P, T_SEMI);
            int k = find_global(n->text);
            if (k < 0) {
                if (!GROW(globals, n_globals, cap_globals)) { fail(P, "out of memory"); return; }
                k = n_globals++;
            }
            globals[k] = (Global){ n->text, v, ty };
        } else if (accept(P, T_FUNC)) {
            SlType ret = SL_VOID;
            if (is_type_kw(peek(P)->type)) ret = type_of_kw(next(P)->type);
            const Token *n = next(P);
            int k = find_func(n->text);
            if (k < 0) {
                if (!GROW(funcs, n_funcs, cap_funcs)) { fail(P, "out of memory"); return; }
                k = n_funcs++;
                memset(&funcs[k], 0, sizeof funcs[k]);
            }
            funcs[k].name = n->text, funcs[k].ret = ret;
            int depth = 0;      /* skip to the body's end */
            while (peek(P)->type != T_EOF) {
                int ty = next(P)->type;
                if (ty == T_LBRACE) depth++;
                else if (ty == T_RBRACE && --depth == 0) break;
            }
        } else
            next(P);
    }
}

static void define(Parser *P)
{
    while (!P->failed && peek(P)->type != T_EOF) {
        if (accept(P, T_VAR)) {     /* done by declare */
            while (!peek(P)->eol && peek(P)->type != T_SEMI && peek(P)->type != T_EOF) next(P);
            next(P);
            continue;
        }
        if (!accept(P, T_FUNC)) { fail(P, "expecting func or var"); return; }
        if (is_type_kw(peek(P)->type)) next(P);
        Func *f = &funcs[find_func(next(P)->text)];
        P->fn = f;
        P->nlocals = 0;
        f->nparams = 0;
        /* the parameter list may be left out: "func void sayonara_strikey {" (manplant) */
        if (accept(P, T_LPAREN) && !accept(P, T_RPAREN)) {
            do {
                int ty = next(P)->type;
                if (ty == T_VOID) break;
                const Token *n = next(P);
                if (f->nparams < 8) f->ptype[f->nparams] = type_of_kw(ty);
                f->nparams++;
                P->locals[P->nlocals] = n->text, P->ltype[P->nlocals++] = type_of_kw(ty);
            } while (accept(P, T_COMMA));
            expect(P, T_RPAREN, "expecting ')'");
        }
        f->code = n_code;
        f->defined = true;
        P->high = P->nlocals;
        expect(P, T_LBRACE, "expecting '{'");
        while (!P->failed && peek(P)->type != T_RBRACE && peek(P)->type != T_EOF) statement(P);
        expect(P, T_RBRACE, "expecting '}'");
        emit(P, OP_PUSH, add_const((SlValue){ SL_VOID, 0, 0, NULL }), 0);
        emit(P, OP_RET, 0, 0);
        f->nlocals = P->high > 64 ? 64 : P->high;
    }
}

typedef struct { char **names; int n, cap; } NameList;
static void collect(const char *name, bool is_dir, void *user)
{
    NameList *l = user;
    size_t n = strlen(name);
    if (is_dir || n < 4 || strcmp(name + n - 4, ".bsl")) return;
    if (!GROW(l->names, l->n, l->cap)) return;
    l->names[l->n++] = strdup(name);
}
static int by_name(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

bool sl_load_text(const char *name, const char *text, char *err, size_t errcap)
{
    Token *toks = NULL;
    int n = lex(text, name, &toks);
    bool ok = n >= 0;
    for (int pass = 1; pass < 3 && ok; pass++) {
        Parser P = { toks, 0, n, name, NULL, { 0 }, { 0 }, 0, false, false };
        if (pass == 1) declare(&P);
        else define(&P);
        if (P.failed) ok = false;
    }
    free(toks);
    if (!ok && err) snprintf(err, errcap, "%s", errbuf);
    stats.functions = (uint32_t)n_funcs, stats.globals = (uint32_t)n_globals, stats.code = n_code;
    return ok;
}

bool sl_load_folder(const char *dir, char *err, size_t errcap)
{
    NameList l = { 0 };
    vfs_list(dir, collect, &l);
    qsort(l.names, (size_t)l.n, sizeof *l.names, by_name);
    Token *toks[64] = { 0 };
    int ntok[64] = { 0 }, nf = l.n < 64 ? l.n : 64;
    bool ok = true;
    for (int pass = 0; pass < 3 && ok; pass++)
        for (int i = 0; i < nf && ok; i++) {
            char path[256];
            snprintf(path, sizeof path, "%s/%s", dir, l.names[i]);
            if (pass == 0) {
                size_t size;
                char *src = (char *)vfs_read_all(path, &size);
                if (!src) { snprintf(errbuf, sizeof errbuf, "%s: can't read", path); ok = false; break; }
                char *z = realloc(src, size + 1);
                if (!z) { free(src); ok = false; break; }
                z[size] = 0;
                ntok[i] = lex(z, l.names[i], &toks[i]);
                free(z);
                if (ntok[i] < 0) { ok = false; break; }
                stats.tokens += (uint32_t)ntok[i];
                for (int k = 0; k < ntok[i]; k++) {
                    uint8_t b[3] = { toks[i][k].type, toks[i][k].eol, 0 };
                    stats.hash = crc32_update(crc32_update(stats.hash, b, 2), toks[i][k].text, strlen(toks[i][k].text));
                }
                stats.files++;
                continue;
            }
            Parser P = { toks[i], 0, ntok[i], l.names[i], NULL, { 0 }, { 0 }, 0, false, false };
            if (pass == 1) declare(&P);
            else define(&P);
            if (P.failed) ok = false;
        }
    for (int i = 0; i < nf; i++) free(toks[i]);
    for (int i = 0; i < l.n; i++) free(l.names[i]);
    free(l.names);
    if (!ok && err) snprintf(err, errcap, "%s", errbuf);
    stats.functions = (uint32_t)n_funcs, stats.globals = (uint32_t)n_globals, stats.code = n_code;
    return ok;
}

SlStats sl_stats(void) { return stats; }
bool sl_function_exists(const char *name) { int k = find_func(sl_intern(name)); return k >= 0 && funcs[k].defined; }

/* ---- threads (SLrScript_Update 0x478070, SLrSchedule_*) ---- */

enum { STACK = 256, FRAMES = 32 };
typedef struct { uint32_t ret; int base; int func; } Frame;
struct SlThread {
    bool traced;        /* the blocking command at pc was logged already */
    uint32_t pc;
    int sp, fp;
    SlValue stack[STACK];
    Frame frames[FRAMES];
    uint32_t sleep;
    bool done;
    uint32_t id;        /* sl_start's handle */
};
static SlThread *threads[256];
static int n_threads;
static uint32_t next_id;

int sl_threads(void) { return n_threads; }
void sl_thread_sleep(SlThread *t, uint32_t ticks) { if (t) t->sleep = ticks; }

static bool enter(SlThread *t, int func, int argc)
{
    Func *f = &funcs[func];
    if (!f->defined || t->fp >= FRAMES || t->sp + f->nlocals + 8 > STACK) return false;
    int base = t->sp - argc;
    for (int i = 0; i < f->nlocals; i++) {
        if (i < argc) { if (i < 8) t->stack[base + i] = convert(t->stack[base + i], f->ptype[i]); }
        else t->stack[base + i] = v_int(0);
    }
    t->sp = base + f->nlocals;
    t->frames[t->fp++] = (Frame){ t->pc, base, func };
    t->pc = f->code;
    return true;
}

static SlThread *spawn(int func, int argc, const SlValue *argv)
{
    if (n_threads >= 256) return NULL;
    SlThread *t = calloc(1, sizeof *t);
    if (!t) return NULL;
    for (int i = 0; i < argc && i < 16; i++) t->stack[t->sp++] = argv[i];
    t->pc = UINT32_MAX;
    if (!enter(t, func, argc)) { free(t); return NULL; }
    threads[n_threads++] = t;
    if (!++next_id) next_id = 1;
    t->id = next_id;
    return t;
}

bool sl_call(const char *name, int argc, const char *const *args)
{
    return sl_start(name, argc, args) != 0;
}

uint32_t sl_start(const char *name, int argc, const char *const *args)
{
    int k = find_func(sl_intern(name));
    if (k < 0 || !funcs[k].defined) return 0;
    if (tracing && logfn) {
        char m[128];
        snprintf(m, sizeof m, "SL: start %s(%s)", name, argc >= 1 ? args[0] : "");
        logfn(m);
    }
    SlValue v[16];
    for (int i = 0; i < argc && i < 16; i++) v[i] = v_str(sl_intern(args[i]));
    SlThread *t = spawn(k, argc < 16 ? argc : 16, v);
    return t ? t->id : 0;
}

bool sl_running(uint32_t id)
{
    for (int i = 0; id && i < n_threads; i++)
        if (threads[i]->id == id && !threads[i]->done) return true;
    return false;
}

static SlValue binop(int op, SlValue a, SlValue b)
{
    bool fl = a.type == SL_FLOAT || b.type == SL_FLOAT;
    switch (op) {
    case T_AND: return v_int(truthy(a) && truthy(b));
    case T_OR: return v_int(truthy(a) || truthy(b));
    case T_EQ: case T_NE: {
        bool eq = a.type == SL_STRING || b.type == SL_STRING ? a.type == b.type && !strcmp(a.s, b.s)
                  : fl ? as_float(a) == as_float(b) : as_int(a) == as_int(b);
        return v_int(op == T_EQ ? eq : !eq);
    }
    case T_LT: return v_int(fl ? as_float(a) < as_float(b) : as_int(a) < as_int(b));
    case T_GT: return v_int(fl ? as_float(a) > as_float(b) : as_int(a) > as_int(b));
    case T_LE: return v_int(fl ? as_float(a) <= as_float(b) : as_int(a) <= as_int(b));
    case T_GE: return v_int(fl ? as_float(a) >= as_float(b) : as_int(a) >= as_int(b));
    case T_PLUS: return fl ? v_float(as_float(a) + as_float(b)) : v_int(as_int(a) + as_int(b));
    case T_MINUS: return fl ? v_float(as_float(a) - as_float(b)) : v_int(as_int(a) - as_int(b));
    case T_STAR: return fl ? v_float(as_float(a) * as_float(b)) : v_int(as_int(a) * as_int(b));
    case T_SLASH: {
        if (fl) return v_float(as_float(b) != 0 ? as_float(a) / as_float(b) : 0);
        int32_t d = as_int(b);
        return v_int(d ? as_int(a) / d : 0);
    }
    }
    return v_int(0);
}

static SlValue engine_get(int k)
{
    if (k < 0) return globals[-1 - k].v;
    EngineVar *e = &evars[k];
    switch (e->type) {
    case SL_INT: return v_int(*(int32_t *)e->p);
    case SL_FLOAT: return v_float(*(float *)e->p);
    default: { SlValue b = v_int(*(bool *)e->p); b.type = SL_BOOL; return b; }
    }
}
static void engine_set(int k, SlValue v)
{
    if (k < 0) { globals[-1 - k].v = v; return; }
    EngineVar *e = &evars[k];
    switch (e->type) {
    case SL_INT: *(int32_t *)e->p = as_int(v); break;
    case SL_FLOAT: *(float *)e->p = as_float(v); break;
    default: *(bool *)e->p = truthy(v);
    }
}

typedef struct Unknown { struct Unknown *next; const char *name; } Unknown;
static Unknown *unknown;

/* Runs a thread until it sleeps, blocks or ends; false when it has ended. */
static bool run(SlThread *t)
{
    for (int budget = 100000; budget > 0; budget--) {
        const Op *o = &code[t->pc++];
        switch (o->op) {
        case OP_PUSH: if (t->sp < STACK) t->stack[t->sp++] = consts[o->a]; break;
        case OP_LOAD_GLOBAL: if (t->sp < STACK) t->stack[t->sp++] = globals[o->a].v; break;
        case OP_STORE_GLOBAL: { Global *g = &globals[o->a]; g->v = g->type ? convert(t->stack[--t->sp], g->type) : t->stack[--t->sp]; break; }
        case OP_LOAD_LOCAL: if (t->sp < STACK) t->stack[t->sp++] = t->stack[t->frames[t->fp - 1].base + o->a]; break;
        case OP_STORE_LOCAL: t->stack[t->frames[t->fp - 1].base + o->a] = convert(t->stack[--t->sp], (SlType)o->argc); break;
        case OP_LOAD_ENGINE: if (t->sp < STACK) t->stack[t->sp++] = engine_get(o->a); break;
        case OP_STORE_ENGINE: engine_set(o->a, t->stack[--t->sp]); break;
        case OP_POP: t->sp--; break;
        case OP_NOT: t->stack[t->sp - 1] = v_int(!truthy(t->stack[t->sp - 1])); break;
        case OP_NEG: { SlValue *v = &t->stack[t->sp - 1]; v->i = -v->i, v->f = -v->f; break; }
        case OP_BIN: { SlValue b = t->stack[--t->sp]; t->stack[t->sp - 1] = binop(o->a, t->stack[t->sp - 1], b); break; }
        case OP_JMP: t->pc = (uint32_t)o->a; break;
        case OP_JZ: if (!truthy(t->stack[--t->sp])) t->pc = (uint32_t)o->a; break;
        case OP_SLEEP: {
            int32_t n = as_int(t->stack[--t->sp]);
            if (n > 0) { t->sleep = (uint32_t)n; return true; }
            break;
        }
        case OP_FORK: case OP_CALL: {
            const char *name = consts[o->a].s;
            int argc = o->argc, f = find_func(name);
            if (f >= 0 && funcs[f].defined) {
                if (o->op == OP_FORK) {     /* a new thread; nothing is left on this one's stack */
                    spawn(f, argc, &t->stack[t->sp - argc]);
                    t->sp -= argc;
                    break;
                }
                if (tracing && logfn) {
                    char m[96];
                    snprintf(m, sizeof m, "SL: call %s", name);
                    logfn(m);
                }
                if (!enter(t, f, argc)) return false;
                break;
            }
            int c = find_cmd(name);
            SlValue r = { SL_VOID, 0, 0, NULL };
            if (tracing && logfn && !t->traced && argc >= 1 && t->stack[t->sp - argc].type == SL_STRING &&
                (!strcmp(name, "message") || !strncmp(name, "sound_dialog_play", 17))) {
                char m[128];
                snprintf(m, sizeof m, "SL: %s %s", name, t->stack[t->sp - argc].s);
                logfn(m);
            }
            if (c >= 0) {
                if (!cmds[c].fn(t, argc, &t->stack[t->sp - argc], &r, cmds[c].user)) { t->pc--; t->sleep = 1; t->traced = true; return true; }
                t->traced = false;
            } else {
                Unknown *u = unknown;
                while (u && u->name != name) u = u->next;
                if (!u && (u = malloc(sizeof *u))) {
                    u->name = name, u->next = unknown, unknown = u;
                    char m[160];
                    snprintf(m, sizeof m, "SL: %s isn't ported yet", name);
                    say(m);
                }
            }
            t->sp -= argc;
            if (o->op == OP_CALL) t->stack[t->sp++] = r;
            if (t->sleep) return true;      /* the command put it to sleep (sl_thread_sleep) */
            break;
        }
        case OP_RET: {
            SlValue r = t->stack[--t->sp];
            Frame fr = t->frames[--t->fp];
            t->sp = fr.base;
            if (t->fp == 0) return false;
            t->stack[t->sp++] = r;
            t->pc = fr.ret;
            break;
        }
        }
    }
    return true;    /* a long loop: on next tick */
}

void sl_update(void)
{
    int n = n_threads;
    for (int i = 0; i < n; i++) {
        SlThread *t = threads[i];
        if (t->done) continue;
        if (t->sleep && --t->sleep) continue;
        if (!run(t)) t->done = true;
    }
    int k = 0;
    for (int i = 0; i < n_threads; i++) {
        if (threads[i]->done) free(threads[i]);
        else threads[k++] = threads[i];
    }
    n_threads = k;
}

void sl_reset(void)
{
    for (int i = 0; i < n_threads; i++) free(threads[i]);
    n_threads = 0;
    free(code), code = NULL, n_code = cap_code = 0;
    free(consts), consts = NULL, n_consts = cap_consts = 0;
    free(funcs), funcs = NULL, n_funcs = cap_funcs = 0;
    free(globals), globals = NULL, n_globals = cap_globals = 0;
    while (unknown) { Unknown *u = unknown->next; free(unknown); unknown = u; }
    memset(&stats, 0, sizeof stats);
}
