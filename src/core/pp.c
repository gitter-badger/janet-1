/*
* Copyright (c) 2019 Calvin Rose
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to
* deal in the Software without restriction, including without limitation the
* rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
* sell copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
* IN THE SOFTWARE.
*/

#ifndef JANET_AMALG
#include <janet/janet.h>
#include "util.h"
#include "state.h"
#endif

/* Implements a pretty printer for Janet. The pretty printer
 * is farily simple and not that flexible, but fast. */

/* Temporary buffer size */
#define BUFSIZE 64

static void number_to_string_b(JanetBuffer *buffer, double x) {
    janet_buffer_ensure(buffer, buffer->count + BUFSIZE, 2);
    int count = snprintf((char *) buffer->data + buffer->count, BUFSIZE, "%g", x);
    buffer->count += count;
}

/* expects non positive x */
static int count_dig10(int32_t x) {
    int result = 1;
    for (;;) {
        if (x > -10) return result;
        if (x > -100) return result + 1;
        if (x > -1000) return result + 2;
        if (x > -10000) return result + 3;
        x /= 10000;
        result += 4;
    }
}

static void integer_to_string_b(JanetBuffer *buffer, int32_t x) {
    janet_buffer_extra(buffer, BUFSIZE);
    uint8_t *buf = buffer->data + buffer->count;
    int32_t neg = 0;
    int32_t len = 0;
    if (x == 0) {
        buf[0] = '0';
        buffer->count++;
        return;
    }
    if (x > 0) {
        x = -x;
    } else {
        neg = 1;
        *buf++ = '-';
    }
    len = count_dig10(x);
    buf += len;
    while (x) {
        uint8_t digit = (uint8_t) -(x % 10);
        *(--buf) = '0' + digit;
        x /= 10;
    }
    buffer->count += len + neg;
}

#define HEX(i) (((uint8_t *) janet_base64)[(i)])

/* Returns a string description for a pointer. Truncates
 * title to 32 characters */
static void string_description_b(JanetBuffer *buffer, const char *title, void *pointer) {
    janet_buffer_ensure(buffer, buffer->count + BUFSIZE, 2);
    uint8_t *c = buffer->data + buffer->count;
    int32_t i;
    union {
        uint8_t bytes[sizeof(void *)];
        void *p;
    } pbuf;

    pbuf.p = pointer;
    *c++ = '<';
    /* Maximum of 32 bytes for abstract type name */
    for (i = 0; title[i] && i < 32; ++i)
        *c++ = ((uint8_t *)title) [i];
    *c++ = ' ';
    *c++ = '0';
    *c++ = 'x';
#if defined(JANET_64)
#define POINTSIZE 6
#else
#define POINTSIZE (sizeof(void *))
#endif
    for (i = POINTSIZE; i > 0; --i) {
        uint8_t byte = pbuf.bytes[i - 1];
        *c++ = HEX(byte >> 4);
        *c++ = HEX(byte & 0xF);
    }
    *c++ = '>';
    buffer->count = (int32_t)(c - buffer->data);
#undef POINTSIZE
}

#undef HEX
#undef BUFSIZE

static void janet_escape_string_impl(JanetBuffer *buffer, const uint8_t *str, int32_t len) {
    janet_buffer_push_u8(buffer, '"');
    for (int32_t i = 0; i < len; ++i) {
        uint8_t c = str[i];
        switch (c) {
            case '"':
                janet_buffer_push_bytes(buffer, (const uint8_t *)"\\\"", 2);
                break;
            case '\n':
                janet_buffer_push_bytes(buffer, (const uint8_t *)"\\n", 2);
                break;
            case '\r':
                janet_buffer_push_bytes(buffer, (const uint8_t *)"\\r", 2);
                break;
            case '\0':
                janet_buffer_push_bytes(buffer, (const uint8_t *)"\\0", 2);
                break;
            case '\\':
                janet_buffer_push_bytes(buffer, (const uint8_t *)"\\\\", 2);
                break;
            default:
                if (c < 32 || c > 127) {
                    uint8_t buf[4];
                    buf[0] = '\\';
                    buf[1] = 'x';
                    buf[2] = janet_base64[(c >> 4) & 0xF];
                    buf[3] = janet_base64[c & 0xF];
                    janet_buffer_push_bytes(buffer, buf, 4);
                } else {
                    janet_buffer_push_u8(buffer, c);
                }
                break;
        }
    }
    janet_buffer_push_u8(buffer, '"');
}

static void janet_escape_string_b(JanetBuffer *buffer, const uint8_t *str) {
    janet_escape_string_impl(buffer, str, janet_string_length(str));
}

static void janet_escape_buffer_b(JanetBuffer *buffer, JanetBuffer *bx) {
    janet_buffer_push_u8(buffer, '@');
    janet_escape_string_impl(buffer, bx->data, bx->count);
}

void janet_description_b(JanetBuffer *buffer, Janet x) {
    switch (janet_type(x)) {
    case JANET_NIL:
        janet_buffer_push_cstring(buffer, "nil");
        return;
    case JANET_TRUE:
        janet_buffer_push_cstring(buffer, "true");
        return;
    case JANET_FALSE:
        janet_buffer_push_cstring(buffer, "false");
        return;
    case JANET_NUMBER:
        number_to_string_b(buffer, janet_unwrap_number(x));
        return;
    case JANET_KEYWORD:
        janet_buffer_push_u8(buffer, ':');
        /* fallthrough */
    case JANET_SYMBOL:
        janet_buffer_push_bytes(buffer,
                janet_unwrap_string(x),
                janet_string_length(janet_unwrap_string(x)));
        return;
    case JANET_STRING:
        janet_escape_string_b(buffer, janet_unwrap_string(x));
        return;
    case JANET_BUFFER:
        janet_escape_buffer_b(buffer, janet_unwrap_buffer(x));
        return;
    case JANET_ABSTRACT:
        {
            const char *n = janet_abstract_type(janet_unwrap_abstract(x))->name;
            string_description_b(buffer, n, janet_unwrap_abstract(x));
			return;
        }
    case JANET_CFUNCTION:
        {
            Janet check = janet_table_get(janet_vm_registry, x);
            if (janet_checktype(check, JANET_SYMBOL)) {
                janet_buffer_push_cstring(buffer, "<cfunction ");
                janet_buffer_push_bytes(buffer,
                        janet_unwrap_symbol(check),
                        janet_string_length(janet_unwrap_symbol(check)));
                janet_buffer_push_u8(buffer, '>');
                break;
            }
            goto fallthrough;
        }
    case JANET_FUNCTION:
        {
            JanetFunction *fun = janet_unwrap_function(x);
            JanetFuncDef *def = fun->def;
            if (def->name) {
                const uint8_t *n = def->name;
                janet_buffer_push_cstring(buffer, "<function ");
                janet_buffer_push_bytes(buffer, n, janet_string_length(n));
                janet_buffer_push_u8(buffer, '>');
                break;
            }
            goto fallthrough;
        }
    fallthrough:
    default:
        string_description_b(buffer, janet_type_names[janet_type(x)], janet_unwrap_pointer(x));
        break;
    }
}

void janet_to_string_b(JanetBuffer *buffer, Janet x) {
    switch (janet_type(x)) {
        default:
            janet_description_b(buffer, x);
            break;
        case JANET_BUFFER:
            janet_buffer_push_bytes(buffer,
                    janet_unwrap_buffer(x)->data,
                    janet_unwrap_buffer(x)->count);
            break;
        case JANET_STRING:
        case JANET_SYMBOL:
        case JANET_KEYWORD:
            janet_buffer_push_bytes(buffer,
                    janet_unwrap_string(x),
                    janet_string_length(janet_unwrap_string(x)));
            break;
    }
}

const uint8_t *janet_description(Janet x) {
    JanetBuffer b;
    janet_buffer_init(&b, 10);
    janet_description_b(&b, x);
    const uint8_t *ret = janet_string(b.data, b.count);
    janet_buffer_deinit(&b);
    return ret;
}

/* Convert any value to a janet string. Similar to description, but
 * strings, symbols, and buffers will return their content. */
const uint8_t *janet_to_string(Janet x) {
    switch (janet_type(x)) {
        default:
            {
                JanetBuffer b;
                janet_buffer_init(&b, 10);
                janet_to_string_b(&b, x);
                const uint8_t *ret = janet_string(b.data, b.count);
                janet_buffer_deinit(&b);
                return ret;
            }
        case JANET_BUFFER:
            return janet_string(janet_unwrap_buffer(x)->data, janet_unwrap_buffer(x)->count);
        case JANET_STRING:
        case JANET_SYMBOL:
        case JANET_KEYWORD:
            return janet_unwrap_string(x);
    }
}

/* Hold state for pretty printer. */
struct pretty {
    JanetBuffer *buffer;
    int depth;
    int indent;
    JanetTable seen;
};

static void print_newline(struct pretty *S, int just_a_space) {
    int i;
    if (just_a_space) {
        janet_buffer_push_u8(S->buffer, ' ');
        return;
    }
    janet_buffer_push_u8(S->buffer, '\n');
    for (i = 0; i < S->indent; i++) {
        janet_buffer_push_u8(S->buffer, ' ');
    }
}

/* Helper for pretty printing */
static void janet_pretty_one(struct pretty *S, Janet x, int is_dict_value) {
    /* Add to seen */
    switch (janet_type(x)) {
        case JANET_NIL:
        case JANET_NUMBER:
        case JANET_SYMBOL:
        case JANET_TRUE:
        case JANET_FALSE:
            break;
        default:
            {
                Janet seenid = janet_table_get(&S->seen, x);
                if (janet_checktype(seenid, JANET_NUMBER)) {
                    janet_buffer_push_cstring(S->buffer, "<cycle ");
                    integer_to_string_b(S->buffer, janet_unwrap_integer(seenid));
                    janet_buffer_push_u8(S->buffer, '>');
                    return;
                } else {
                    janet_table_put(&S->seen, x, janet_wrap_integer(S->seen.count));
                    break;
                }
            }
    }

    switch (janet_type(x)) {
        default:
            janet_description_b(S->buffer, x);
            break;
        case JANET_ARRAY:
        case JANET_TUPLE:
            {
                int isarray = janet_checktype(x, JANET_ARRAY);
                janet_buffer_push_cstring(S->buffer, isarray ? "@[" : "(");
                S->depth--;
                S->indent += 2;
                if (S->depth == 0) {
                    janet_buffer_push_cstring(S->buffer, "...");
                } else {
                    int32_t i, len;
                    const Janet *arr;
                    janet_indexed_view(x, &arr, &len);
                    if (!isarray && len >= 5)
                        janet_buffer_push_u8(S->buffer, ' ');
                    if (is_dict_value && len >= 5) print_newline(S, 0);
                    for (i = 0; i < len; i++) {
                        if (i) print_newline(S, len < 5);
                        janet_pretty_one(S, arr[i], 0);
                    }
                }
                S->indent -= 2;
                S->depth++;
                janet_buffer_push_u8(S->buffer, isarray ? ']' : ')');
                break;
            }
        case JANET_STRUCT:
        case JANET_TABLE:
            {
                int istable = janet_checktype(x, JANET_TABLE);
                janet_buffer_push_cstring(S->buffer, istable ? "@" : "{");

                /* For object-like tables, print class name */
                if (istable) {
                    JanetTable *t = janet_unwrap_table(x);
                    JanetTable *proto = t->proto;
                    if (NULL != proto) {
                        Janet name = janet_table_get(proto, janet_csymbolv(":name"));
                        if (janet_checktype(name, JANET_SYMBOL)) {
                            const uint8_t *sym = janet_unwrap_symbol(name);
                            janet_buffer_push_bytes(S->buffer, sym, janet_string_length(sym));
                        }
                    }
                    janet_buffer_push_cstring(S->buffer, "{");
                }

                S->depth--;
                S->indent += 2;
                if (S->depth == 0) {
                    janet_buffer_push_cstring(S->buffer, "...");
                } else {
                    int32_t i, len, cap;
                    int first_kv_pair = 1;
                    const JanetKV *kvs;
                    janet_dictionary_view(x, &kvs, &len, &cap);
                    if (!istable && len >= 4)
                        janet_buffer_push_u8(S->buffer, ' ');
                    if (is_dict_value && len >= 5) print_newline(S, 0);
                    for (i = 0; i < cap; i++) {
                        if (!janet_checktype(kvs[i].key, JANET_NIL)) {
                            if (first_kv_pair) {
                                first_kv_pair = 0;
                            } else {
                                print_newline(S, len < 4);
                            }
                            janet_pretty_one(S, kvs[i].key, 0);
                            janet_buffer_push_u8(S->buffer, ' ');
                            janet_pretty_one(S, kvs[i].value, 1);
                        }
                    }
                }
                S->indent -= 2;
                S->depth++;
                janet_buffer_push_u8(S->buffer, '}');
                break;
            }
    }
    /* Remove from seen */
    janet_table_remove(&S->seen, x);
    return;
}

/* Helper for printing a janet value in a pretty form. Not meant to be used
 * for serialization or anything like that. */
JanetBuffer *janet_pretty(JanetBuffer *buffer, int depth, Janet x) {
    struct pretty S;
    if (NULL == buffer) {
        buffer = janet_buffer(0);
    }
    S.buffer = buffer;
    S.depth = depth;
    S.indent = 0;
    janet_table_init(&S.seen, 10);
    janet_pretty_one(&S, x, 0);
    janet_table_deinit(&S.seen);
    return S.buffer;
}

static const char *typestr(Janet x) {
    JanetType t = janet_type(x);
    return (t == JANET_ABSTRACT)
        ? janet_abstract_type(janet_unwrap_abstract(x))->name
        : janet_type_names[t];
}

static void pushtypes(JanetBuffer *buffer, int types) {
    int first = 1;
    int i = 0;
    while (types) {
        if (1 & types) {
            if (first) {
                first = 0;
            } else {
                janet_buffer_push_u8(buffer, '|');
            }
            janet_buffer_push_cstring(buffer, janet_type_names[i]);
        }
        i++;
        types >>= 1;
    }
}

/* Helper function for formatting strings. Useful for generating error messages and the like.
 * Similar to printf, but specialized for operating with janet. */
const uint8_t *janet_formatc(const char *format, ...) {
    va_list args;
    int32_t len = 0;
    int32_t i;
    const uint8_t *ret;
    JanetBuffer buffer;
    JanetBuffer *bufp = &buffer;

    /* Calculate length */
    while (format[len]) len++;

    /* Initialize buffer */
    janet_buffer_init(bufp, len);

    /* Start args */
    va_start(args, format);

    /* Iterate length */
    for (i = 0; i < len; i++) {
        uint8_t c = format[i];
        switch (c) {
            default:
                janet_buffer_push_u8(bufp, c);
                break;
            case '%':
            {
                if (i + 1 >= len)
                    break;
                switch (format[++i]) {
                    default:
                        janet_buffer_push_u8(bufp, format[i]);
                        break;
                    case 'f':
                        number_to_string_b(bufp, va_arg(args, double));
                        break;
                    case 'd':
                        integer_to_string_b(bufp, va_arg(args, long));
                        break;
                    case 'S':
                    {
                        const uint8_t *str = va_arg(args, const uint8_t *);
                        janet_buffer_push_bytes(bufp, str, janet_string_length(str));
                        break;
                    }
                    case 's':
                        janet_buffer_push_cstring(bufp, va_arg(args, const char *));
                        break;
                    case 'c':
                        janet_buffer_push_u8(bufp, (uint8_t) va_arg(args, long));
                        break;
                    case 'q':
                    {
                        const uint8_t *str = va_arg(args, const uint8_t *);
                        janet_escape_string_b(bufp, str);
                        break;
                    }
                    case 't':
                    {
                        janet_buffer_push_cstring(bufp, typestr(va_arg(args, Janet)));
                        break;
                    }
                    case 'T':
                    {
                        int types = va_arg(args, long);
                        pushtypes(bufp, types);
                        break;
                    }
                    case 'V':
                    {
                        janet_to_string_b(bufp, va_arg(args, Janet));
                        break;
                    }
                    case 'v':
                    {
                        janet_description_b(bufp, va_arg(args, Janet));
                        break;
                    }
                    case 'p':
                    {
                        janet_pretty(bufp, 4, va_arg(args, Janet));
                    }
                }
            }
        }
    }

    va_end(args);

    ret = janet_string(buffer.data, buffer.count);
    janet_buffer_deinit(&buffer);
    return ret;
}

