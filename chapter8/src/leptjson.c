#ifdef _WINDOWS
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif  // windows下的内存泄露检查
#include "leptjson.h"
#include <stdlib.h>         // NULL, strtod(),malloc(),realloc() free()
#include <assert.h>         // asser()
#include <errno.h>          // errno, ERANGE
#include <math.h>           // HUGE_VAL
#include <string.h>         // memcpy()
#include <stdio.h>

#ifndef LEPT_PARSE_STACK_INIT_SIZE
#define LEPT_PARSE_STACK_INIT_SIZE 256
#endif

#ifndef LEPT_PARSE_STRINGIFY_INIT_SIZE
#define LEPT_PARSE_STRINGIFY_INIT_SIZE 256
#endif

// 这里使用do...while(0) 是一个编写宏的技巧
// 如果宏里面有多过一个语句，就使用do{}while(0)来包裹成单个语句
// 这个宏的作用是判断当前首字符是否是所期望的ch
#define EXPECT(c, ch)       do { assert(*c->json == (ch)); c->json++; } while(0)
#define ISDIGIT(ch)         ((ch) >= '0' && (ch) <= '9')
#define ISDIGIT1TO9(ch)     ((ch) >= '1' && (ch) <= '9')
#define PUTC(c, ch)         do { *(char*)lept_context_push(c, sizeof(char)) = (ch); } while(0)
// 输出字符串到自定义堆栈中
#define PUTS(c, s, len)     memcpy(lept_context_push(c, len), s, len)

// 首先为了减少解析函数之间传递多个参数，我们把这些数据都放进一个 `lept_context` 结构体
typedef struct 
{
    // 表示的是指针json指向的数值不能修改， 但是可以通过本身自己去修
    const char* json;
    // 解析string之后，需要把解析后的内容存储在临时缓冲区中，再用lept_set-string写入，这个buf中在完成解析前大小是不定的
    // 所以我们采用动态数组方式，空间不足时自动扩容
    char* stack;
    size_t size;    // 当前stack容量
    size_t top;     // 栈顶位置，由于自动扩容，所以不用指针
} lept_context;


// 压入任意大小数据，返回数据(压入的)起始的指针
static void* lept_context_push(lept_context* c, size_t size) {
    void* ret;
    assert(size > 0);
    // 考虑push之后内存会不够 扩容
    if(c->top + size >= c->size) {
        if (c->size == 0)
            c->size = LEPT_PARSE_STACK_INIT_SIZE;
        // 这里使用while 是因为可能需要push的字符串很长 可能不止需要扩一次 所以这里一次计算
        while (c->top + size >= c->size)
            c->size += c->size >> 1;    // 这里是1.5倍扩容
        // 这里如果是第一次分配 c->stack本身在init时是NULL 所以等价于malloc(size) 不需要为第一次作特别处理
        c->stack = (char*)realloc(c->stack, c->size);
    }
    // 这里记录的是开始插入的位置
    ret = c->stack + c->top;
    c->top += size;
    return ret;
}


// 弹出数据，返回需要返回的数据的起始位置
static void* lept_context_pop(lept_context* c, size_t size) {
    assert(c->top >= size);
    return c->stack + (c->top -= size);
}

// 解析ws
static void lept_parse_whitespace(lept_context* c) {
    const char* p = c->json;
    while(*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        ++p;
    // 最后得到的结果是去除了前面无效的空格的
    c->json = p;
}

#if 0
// 解析“true”  v是一个传出参数
static int lept_parse_true(lept_context* c, lept_value* v) {
    EXPECT(c,'t');
    if(c->json[0] != 'r' || c->json[1] != 'u' || c->json[2] != 'e')
        return LEPT_PARSE_INVALID_VALUE;
    // 解析成功，当前值是“true”
    c->json += 3;
    v->type = LEPT_TRUE;
    return LEPT_PARSE_OK;
}

// 解析“false”
static int lept_parse_false(lept_context* c, lept_value* v) {
    EXPECT(c, 'f');
    if(c->json[0] != 'a' || c->json[1] != 'l' || c->json[2] != 's' || c->json[3] != 'e')
        return LEPT_PARSE_INVALID_VALUE;
    c->json += 4;
    v->type = LEPT_FALSE;
    return LEPT_PARSE_OK;
}

// 解析“null"
static int lept_parse_null(lept_context* c, lept_value* v) {
    EXPECT(c, 'n');
    if(c->json[0] != 'u' || c->json[1] != 'l' || c->json[2] != 'l')
        return LEPT_PARSE_INVALID_VALUE;
    c->json += 3;
    v->type = LEPT_NULL;
    return LEPT_PARSE_OK;
}
#else

// 解析字面值 "null" "true" "false" 封装一个通用函数以代替上面三个函数
static int lept_parse_literal(lept_context* c, lept_value* v, const char* literal, lept_type type) {
    // 在c语言中，数组长度、索引值最好用size_t
    size_t i;
    // 判断第一个字符是否预期一样
    EXPECT(c, literal[0]);
    // 循环判断后面几个字符 如果有一位不匹配 则返回错误码 是无效值
    // 直到‘\0’结束
    for (i = 0; literal[i + 1]; i++)
        if (c->json[i] != literal[i + 1])
            return LEPT_PARSE_INVALID_VALUE;
    // 最后一个匹配字符，指针后移
    c->json += i;
    v->type = type;
    return LEPT_PARSE_OK;
}
#endif

/**
 * JSON数字语法
 * number = ["-"] int [frac] [exp]
 * int = "0" / digit1-9*digit           /表示或者  *表示至少0个
 * frac = "." 1*digit
 * exp = ("e" / "E") ["-" / "+"]1*digit        ()必选
*/
static int lept_parse_number(lept_context* c, lept_value* v) {
    const char* p = c->json;
    if (*p == '-') p++;      // 检验第一位负号，跳过
    if (*p == '0') p++;      // 检验开始的第一个数字是否为0，跳过，第一位数字是0，则后面直接遇到‘.’不再有其他数字
    else {
        // 开始符号位之后 不是0 也不是0-9的数字，则不是number
        if (!ISDIGIT1TO9(*p)) return LEPT_PARSE_INVALID_VALUE;
        // 连续跳过数字字符
        for (p++; ISDIGIT(*p); p++);
    }
    if(*p == '.') {
        p++;
        // 小数点之后不是数字，无效
        if (!ISDIGIT(*p)) return LEPT_PARSE_INVALID_VALUE;
        for (p++; ISDIGIT(*p); p++);
    }
    if (*p == 'e' || *p == 'E') {// 科学计数表示
        p++;
        if(*p == '+' || *p == '-') p++; // 指数符号
        // 含有数字以外字符 无效
        if (!ISDIGIT(*p)) return LEPT_PARSE_INVALID_VALUE;
        for (p++; ISDIGIT(*p); p++);
    }
    // errno是stdlib中的一个宏，保存程序运行中的错误码，初始为0，表示正常
    errno = 0;
    // strtod 把c->json 所指向的字符串转换为一个浮点数,第二个参数是一个char**不为NULL情况下 指向转换中最后一个字符后的字符的指针会存储在 endptr 引用的位置。
    v->u.n = strtod(c->json, NULL);
    // ERANGE 表示一个范围错误，它在输入参数超出数学函数定义的范围时发生，errno 被设置为 ERANGE。
    // HUGE_VAL 最大的双精度值 也就是inf-->无穷
    if (errno == ERANGE && (v->u.n == HUGE_VAL || v->u.n == -HUGE_VAL))
        return LEPT_PARSE_NUMBER_TOO_BIG;
    v->type = LEPT_NUMBER;
    c->json = p;
    return LEPT_PARSE_OK;
}

// 解析 4 位 16 进制数字 Unicode码点值
static const char* lept_parse_hex4(const char* p, unsigned* u) {
    int i;
    // 初始化为0 unsigned <=> unsigned int
    *u = 0;
    for (i = 0; i < 4; i++) {
        char ch = *p++;
        *u <<= 4;   // 每个字符转换成10进制后占4位 xxxx xxxx xxxx xxxx 4次循环后最后的16位有被改变
        if      (ch >= '0' && ch <= '9')    *u |= ch - '0';
        else if (ch >= 'A' && ch <= 'F')    *u |= ch - ('A' - 10);
        else if (ch >= 'a' && ch <= 'f')    *u |= ch - ('a' - 10);
        else return NULL;
    }
    return p;
}

// 将码点编码成utf8 按照码点范围可以拆分成1到至多4个字节
# if 0
码点范围        码点位数        字节1       字节2       字节3       字节4
U+0000~U+007F     7          0xxxxxxx
U+0080~U+07FF     11         110xxxxx     10xxxxxx
U+0800~U+FFFF     16         1110xxxx     10xxxxxx    10xxxxxx
U+10000~U+10FFFF  21         11110xxx     10xxxxxx    10xxxxxx    10xxxxxx
#endif
static void lept_encode_utf8(lept_context* c, unsigned u) {
    if (u <= 0x7F) 
        PUTC(c, u & 0xFF);
    else if (u <= 0x7FF) {
        PUTC(c, 0xC0 | ((u >> 6) & 0xFF)); /* 0xC0 = 110 00000 */
        PUTC(c, 0x80 | ( u       & 0x3F));
    }
    else if (u <= 0xFFFF) {
        PUTC(c, 0xE0 | ((u >> 12) & 0xFF)); /* 0xE0 = 1110 0000 */
        PUTC(c, 0x80 | ((u >> 6)  & 0x3F)); /* 0x80 = 10 000000 */
        PUTC(c, 0x80 | ( u        & 0x3F)); /* 0x3F = 00 111111 */
    }
    else if (u <= 0x10FFFF) {
        PUTC(c, 0xF0 | ((u >> 18) & 0xFF)); /* 0xF0 = 11110 000 */
        PUTC(c, 0x80 | ((u >> 12) & 0x3F));
        PUTC(c, 0x80 | ((u >> 6)  & 0x3F));
        PUTC(c, 0x80 | ( u        & 0x3F));
    }
}

#define STRING_ERROR(ret) do { c->top = head; return ret; } while(0)
// 将返回错误码抽取为宏

#if 0
JSON object语法
member = string ws %x3A ws value
object = %x7B ws [ member *( ws %x2C ws member ) ] ws %x7D
%x3A  ：    %x2C  ,
#endif
// 解析 JSON 字符串，把结果写入 str 和 len 把结果copy至lept_member的k和klen
// str 指向 c->stack 中的元素 
static int lept_parse_string_raw(lept_context* c, char** str, size_t* len) {
    size_t head = c->top;
    unsigned u, u2;
    const char* p;
    EXPECT(c, '\"');
    p = c->json;
    for (;;) {
        char ch = *p++;
        switch (ch) {
            case '\"':
                *len = c->top - head;
                *str = lept_context_pop(c, *len);
                c->json = p;
                return LEPT_PARSE_OK;
            case '\\':
                switch (*p++) {
                    case '\"': PUTC(c, '\"'); break;
                    case '\\': PUTC(c, '\\'); break;
                    case '/':  PUTC(c, '/' ); break;
                    case 'b':  PUTC(c, '\b'); break;
                    case 'f':  PUTC(c, '\f'); break;
                    case 'n':  PUTC(c, '\n'); break;
                    case 'r':  PUTC(c, '\r'); break;
                    case 't':  PUTC(c, '\t'); break;
                    case 'u':
                        if (!(p = lept_parse_hex4(p, &u)))
                            STRING_ERROR(LEPT_PARSE_INVALID_UNICODE_HEX);
                        if (u >= 0xD800 && u <= 0xDBFF) { /* surrogate pair */
                            if (*p++ != '\\')
                                STRING_ERROR(LEPT_PARSE_INVALID_UNICODE_SURROGATE);
                            if (*p++ != 'u')
                                STRING_ERROR(LEPT_PARSE_INVALID_UNICODE_SURROGATE);
                            if (!(p = lept_parse_hex4(p, &u2)))
                                STRING_ERROR(LEPT_PARSE_INVALID_UNICODE_HEX);
                            if (u2 < 0xDC00 || u2 > 0xDFFF)
                                STRING_ERROR(LEPT_PARSE_INVALID_UNICODE_SURROGATE);
                            u = (((u - 0xD800) << 10) | (u2 - 0xDC00)) + 0x10000;
                        }
                        lept_encode_utf8(c, u);
                        break;
                    default:
                        STRING_ERROR(LEPT_PARSE_INVALID_STRING_ESCAPE);
                }
                break;
            case '\0':
                STRING_ERROR(LEPT_PARSE_MISS_QUOTATION_MARK);
            default:
                if ((unsigned char)ch < 0x20)
                    STRING_ERROR(LEPT_PARSE_INVALID_STRING_CHAR);
                PUTC(c, ch);
        }
    }
}

// 把解析结果写入lept_value
static int lept_parse_string(lept_context* c, lept_value* v) {
    int ret;
    char* s;
    size_t len;
    if ((ret = lept_parse_string_raw(c, &s, &len)) == LEPT_PARSE_OK) 
        lept_set_string(v, s, len);
    return ret;
}

// forward declare 因为lept_parse_value 和lept_parse_array两个有互相调用
static int lept_parse_value(lept_context* c, lept_value* v);

#if 0
JSON array语法
array = %x5B ws [ value *( ws %x2C ws value ) ] ws %x5D
           [                    ,                      ] 
#endif
static int lept_parse_array(lept_context* c, lept_value* v) {
    size_t i, size = 0;
    int ret;
    EXPECT(c, '[');
    lept_parse_whitespace(c);
    if (*c->json == ']') {   // array empty
        c->json++;
        lept_set_array(v, 0);
        return LEPT_PARSE_OK;
    }

    // 在循环中建立一个临时值（`lept_value e`），然后调用 `lept_parse_value()` 去把元素解析至这个临时值，完成后把临时值压栈。
    for (;;) {
        // 生成一个临时的lept_value,用于存储之后解析的元素
        lept_value e;
        lept_init(&e);
        // 调用lept_parse_value()去解析临时的元素值
        if ((ret = lept_parse_value(c, &e)) != LEPT_PARSE_OK)
            break;
        // 一个value解析正确之后，我们把临时元素压栈
        memcpy(lept_context_push(c, sizeof(lept_value)), &e, sizeof(lept_value));
        size++;
        // 接着解析后面元素
        lept_parse_whitespace(c);
        if (*c->json == ',') {
            c->json++;
            lept_parse_whitespace(c);
        }
        else if (*c->json == ']') {
            c->json++;
            lept_set_array(v, size);
            memcpy(v->u.a.e, lept_context_pop(c, size * sizeof(lept_value)), size * sizeof(lept_value));
            v->u.a.size = size;
            return LEPT_PARSE_OK;
        }
        else {  // 一个值之后跟的不是`,`也不是`]`,就是非法
            ret = LEPT_PARSE_MISS_COMMA_OR_SQUARE_BRACKET;
            break;
        }
    }
    // 解析失败时 释放堆栈中的值，因为之前可以已经压入了一些值在自定义栈 避免内存泄露
    /* Pop and free values on the stack */
    for (i = 0; i < size; i++)
        lept_free((lept_value*)lept_context_pop(c, sizeof(lept_value)));
    return ret;
}


static int lept_parse_object(lept_context* c, lept_value* v) {
    size_t i, size;
    lept_member m;
    int ret;
    EXPECT(c, '{');
    lept_parse_whitespace(c);
    if (*c->json == '}') {
        c->json++;
        lept_set_object(v, 0);
        return LEPT_PARSE_OK;
    }
    // 1. 利用`lept_parse_string_raw()` 去解析键的字符串。字符串解析成功，它会把结果存储在我们的栈之中，需要把结果写入临时 `lept_member` 的 `k` 和 `klen` 字段中
    m.k = NULL;
    size = 0;
    for (;;) {
        char* str;
        lept_init(&m.v);
        // parse key
        if (*c->json != '"') {
            ret = LEPT_PARSE_MISS_KEY;
            break;
        }
        if ((ret = lept_parse_string_raw(c, &str, &m.klen)) != LEPT_PARSE_OK) 
            break;
        memcpy(m.k = (char*)malloc(m.klen + 1), str, m.klen);
        m.k[m.klen] = '\0';
        // 2. parse ws colon ws
        lept_parse_whitespace(c);
        if (*c->json != ':') {
            ret = LEPT_PARSE_MISS_COLON;
            break;
        }
        c->json++;
        lept_parse_whitespace(c);
        // 3. parse value
        if ((ret = lept_parse_value(c, &m.v)) != LEPT_PARSE_OK) 
            break;
        memcpy(lept_context_push(c, sizeof(lept_member)), &m, sizeof(lept_member));
        size++;
        // 其意义是说明该键的字符串的拥有权已转移至栈，之后如遇到错误，我们不会重覆释放栈里成员的键和这个临时成员的键。
        m.k = NULL; // ownership is transferred to member on stack
        // 4. parse ws [comma | right-curly-brace] ws
        lept_parse_whitespace(c);
        if (*c->json == ',') {
            c->json++;
            lept_parse_whitespace(c);
        }
        else if (*c->json == '}') {
            c->json++;
            lept_set_object(v, size);
            memcpy(v->u.o.m, lept_context_pop(c, sizeof(lept_member) * size), sizeof(lept_member) * size);
            v->u.o.size = size;
            return LEPT_PARSE_OK;
        }
        else {
            ret = LEPT_PARSE_MISS_COMMA_OR_CURLY_BRACKET;
            break;
        }
    }
    // pop and free members on the stack
    free(m.k);
    for (i = 0; i < size; i++) {
        lept_member* m = (lept_member*)lept_context_pop(c, sizeof(lept_member));
        free(m->k);
        lept_free(&m->v);
    }
    v->type = LEPT_NULL;
    return ret;
}

// 解析值
static int lept_parse_value(lept_context* c, lept_value* v) {
    switch (*c->json) {
        case 't':   return lept_parse_literal(c, v, "true", LEPT_TRUE);
        case 'n':   return lept_parse_literal(c, v, "null", LEPT_NULL);
        case 'f':   return lept_parse_literal(c, v, "false", LEPT_FALSE);
        default:    return lept_parse_number(c, v);
		case '"':   return lept_parse_string(c, v);
		case '[':   return lept_parse_array(c, v);
        case '{':  return lept_parse_object(c, v);
        case '\0':  return LEPT_PARSE_EXPECT_VALUE;
    }
}

int lept_parse(lept_value* v, const char* json) {
    lept_context c;
    int ret;
    assert(v != NULL);
    c.json = json;
    c.stack = NULL;
    c.size = c.top = 0;
    lept_init(v);
    // 此处先将v设置为LEPT_NULL  让lept_parse_value()写入解析出来的根值
    //v->type = LEPT_NULL; 使用了lept_init(v)
    // 去除ws
    lept_parse_whitespace(&c);
    if((ret = lept_parse_value(&c, v)) == LEPT_PARSE_OK) {
        // ws value ws 这个格式 前面ws value 已经解析完成，继续解析后面，看是否还有其他字符
        lept_parse_whitespace(&c);
        // 说明有其他字符-->不合法
		if (*c.json != '\0') {
			v->type = LEPT_NULL;
			ret = LEPT_PARSE_ROOT_NOT_SINGULAR;
		}          
    }
    assert(c.top == 0); // 加入断言确保所有数据都被弹出
    free(c.stack);      // 释放stack空间
    return ret;
}
#if 0
// Unoptimized
static void lept_stringify_string(lept_context* c, const char* s, size_t len) {
    size_t i;
    assert(s != NULL);
    PUTC(c, '"');
    for (i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)s[i];
        switch (ch) {
            case '\"': PUTS(c, "\\\"", 2); break;
            case '\\': PUTS(c, "\\\\", 2); break;
            case '\b': PUTS(c, "\\b",  2); break;
            case '\f': PUTS(c, "\\f",  2); break;
            case '\n': PUTS(c, "\\n",  2); break;
            case '\r': PUTS(c, "\\r",  2); break;
            case '\t': PUTS(c, "\\t",  2); break;
            default:
                if (ch < 0x20) {
                    char buffer[7];
                    sprintf(buffer, "\\u%04X", ch);
                    PUTS(c, buffer, 6);
                }
                else
                    PUTC(c, s[i]);
        }
    }
    PUTC(c, '"');
}
#else
static void lept_stringify_string(lept_context* c, const char* s, size_t len) {
    // 这个函数主要是用来字符化lept_member.k或者LEPT_STRING
    static const char hex_digits[] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };
    size_t i, size;
    char* head, *p;
    assert(s != NULL);
    p = head = lept_context_push(c, size = len * 6 + 2); /* "\u00xx..." */
    *p++ = '"';
    for (i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)s[i];
        // 特殊字符需要转义存储
        switch (ch) {
            case '\"': *p++ = '\\'; *p++ = '\"'; break;
            case '\\': *p++ = '\\'; *p++ = '\\'; break;
            case '\b': *p++ = '\\'; *p++ = 'b';  break;
            case '\f': *p++ = '\\'; *p++ = 'f';  break;
            case '\n': *p++ = '\\'; *p++ = 'n';  break;
            case '\r': *p++ = '\\'; *p++ = 'r';  break;
            case '\t': *p++ = '\\'; *p++ = 't';  break;
            default:
            // 少于0x20的字符需要转义为\u00xx
                if (ch < 0x20) {
                    *p++ = '\\'; *p++ = 'u'; *p++ = '0'; *p++ = '0';
                    *p++ = hex_digits[ch >> 4];
                    *p++ = hex_digits[ch & 15];
                }
                else
                    *p++ = s[i];
        }
    }
    *p++ = '"';
    c->top -= size - (p - head);
}
#endif

static void lept_stringify_value(lept_context* c, const lept_value* v) {
    size_t i;
    switch (v->type) {
        case LEPT_NULL:   PUTS(c, "null",  4); break;
        case LEPT_FALSE:  PUTS(c, "false", 5); break;
        case LEPT_TRUE:   PUTS(c, "true",  4); break;
        case LEPT_NUMBER: c->top -= 32 - sprintf(lept_context_push(c, 32), "%.17g", v->u.n); break;
        case LEPT_STRING: lept_stringify_string(c, v->u.s.s, v->u.s.len); break;
        case LEPT_ARRAY:
            PUTC(c, '[');
            for (i = 0; i < v->u.a.size; i++) {
                if (i > 0)
                    PUTC(c, ',');
                lept_stringify_value(c, &v->u.a.e[i]);
            }
            PUTC(c, ']');
            break;
        case LEPT_OBJECT:
            PUTC(c, '{');
            for (i = 0; i < v->u.o.size; i++) {
                if (i > 0)
                    PUTC(c, ',');
                lept_stringify_string(c, v->u.o.m[i].k, v->u.o.m[i].klen);
                PUTC(c, ':');
                lept_stringify_value(c, &v->u.o.m[i].v);
            }
            PUTC(c, '}');
            break;
        default: assert(0 && "invalid type");
    }
}


// 生成器
char* lept_stringify(const lept_value* v, size_t* length) {
    lept_context c;
    assert(v != NULL);
    c.stack = (char*)malloc(c.size = LEPT_PARSE_STRINGIFY_INIT_SIZE);
    c.top = 0;
    lept_stringify_value(&c, v);
    if (length)
        *length = c.top;
    PUTC(&c, '\0');
    return c.stack;
}


void lept_copy(lept_value* dst, const lept_value* src) {
    assert(src != NULL && dst != NULL && src != dst);
	size_t i;
    switch (src->type) {
        case LEPT_STRING:
            lept_set_string(dst, src->u.s.s, src->u.s.len);
            break;
        case LEPT_ARRAY:
            // 先设置大小
            lept_set_array(dst, src->u.a.size);
            // 逐个拷贝
            for (i = 0; i < src->u.a.size; i++)
                lept_copy(&dst->u.a.e[i], &src->u.a.e[i]);
            dst->u.a.size = src->u.a.size;
            break;
        case LEPT_OBJECT:
            // 先设置大小
            lept_set_object(dst, src->u.o.size);
            // 逐个拷贝
            for (i = 0; i < src->u.o.size; i++) {
                // k
                // 设置k字段为key的对象的值，如果在查找过程中找到了已经存在key，则返回；否则新申请一块空间并初始化，然后返回
                lept_value* val = lept_set_object_value(dst, src->u.o.m[i].k, src->u.o.m[i].klen);
                // v
                lept_copy(val, &src->u.o.m[i].v);
            } 
            dst->u.o.size = src->u.o.size;
            break;
        default:
            lept_free(dst);
            memcpy(dst, src, sizeof(lept_value));
            break;
    }
}

void lept_move(lept_value* dst, lept_value* src) {
    assert(dst != NULL && src != NULL && src != dst);
    lept_free(dst);
    memcpy(dst, src, sizeof(lept_value));
    // 其实也是把src指针置null
    lept_init(src);
}

void lept_swap(lept_value* lhs, lept_value* rhs) {
    assert(lhs != NULL && rhs != NULL);
    if (lhs != rhs) {
        lept_value temp;
        memcpy(&temp, lhs, sizeof(lept_value));
        memcpy(lhs,   rhs, sizeof(lept_value));
        memcpy(rhs, &temp, sizeof(lept_value));
    }
}

void lept_free(lept_value* v) {
    size_t i;
    assert(v != NULL);
    switch (v->type) {
        case LEPT_STRING:
            free(v->u.s.s);
            break;
        case LEPT_ARRAY:
            for (i = 0; i < v->u.a.size; i++)
                lept_free(&v->u.a.e[i]);
            free(v->u.a.e);
            break;
        case LEPT_OBJECT:
            for (i = 0; i < v->u.o.size; i++) {
                free(v->u.o.m[i].k);
                lept_free(&v->u.o.m[i].v);
            }
            free(v->u.o.m);
            break;
        default: break;
    }
    v->type = LEPT_NULL;
}

lept_type lept_get_type(const lept_value* v) {
    assert(v != NULL);
    return v->type;
}


int lept_is_equal(const lept_value* lhs, const lept_value* rhs) {
    size_t i;
    assert(lhs != NULL && rhs != NULL);
    if (lhs->type != rhs->type)
        return 0;
    switch (lhs->type) {
        case LEPT_STRING:
            return lhs->u.s.len == rhs->u.s.len && 
                memcmp(lhs->u.s.s, rhs->u.s.s, lhs->u.s.len) == 0;
        case LEPT_NUMBER:
            return lhs->u.n == rhs->u.n;
        case LEPT_ARRAY:
            if (lhs->u.a.size != rhs->u.a.size)
                return 0;
            for (i = 0; i < lhs->u.a.size; i++)
                if (!lept_is_equal(&lhs->u.a.e[i], &rhs->u.a.e[i]))
                    return 0;
            return 1;
        case LEPT_OBJECT:
            // 对于object 先比较键值个数是否一样
            // 一样的话，对左边的键值对，依次在右边进行查找
            if (lhs->u.o.size != rhs->u.o.size)
                return 0;
            // key-value comp
            for (i = 0; i < lhs->u.o.size; i++) {
                size_t index = lept_find_object_index(rhs, lhs->u.o.m[i].k, lhs->u.o.m[i].klen);
                if (index == LEPT_KEY_NOT_EXIST)
                    return 0;
                if (!lept_is_equal(&lhs->u.o.m[i].v, &rhs->u.o.m[index].v))
                    return 0;
            }
            return 1;
        default:
            return 1;
    }
}

int lept_get_boolean(const lept_value* v) {
    assert(v != NULL && (v->type == LEPT_TRUE || v->type == LEPT_FALSE));
    return v->type == LEPT_TRUE;
}

void lept_set_boolean(lept_value* v, int b) {
    lept_free(v);
    v->type = b ? LEPT_TRUE : LEPT_FALSE;
}

// 只有当type为LEPT_NUMBER时才可以取获得数值
double lept_get_number(const lept_value* v) {
    assert(v != NULL && v->type == LEPT_NUMBER);
    return v->u.n;
}

void lept_set_number(lept_value* v, double n) {
    lept_free(v);
    v->u.n = n;
    v->type = LEPT_NUMBER;
}

const char* lept_get_string(const lept_value* v) {
    assert(v != NULL && v->type == LEPT_STRING);
    return v->u.s.s;
}

size_t lept_get_string_length(const lept_value* v) {
    assert(v != NULL && v->type == LEPT_STRING);
    return v->u.s.len;
}

void lept_set_string(lept_value* v, const char* s, size_t len) {
    assert(v != NULL && (s != NULL || len == 0));
    // 先释放之前的v，避免以前的v没有释放造成影响
    lept_free(v);
    v->u.s.s = (char*)malloc(len + 1);
    memcpy(v->u.s.s, s, len);
    v->u.s.s[len] = '\0';
    v->u.s.len = len;
    v->type = LEPT_STRING;
}

void lept_set_array(lept_value* v, size_t capacity) {
    assert(v != NULL);
    lept_free(v);
    v->type = LEPT_ARRAY;
    v->u.a.size = 0;
    v->u.a.capacity = capacity;
    v->u.a.e = capacity > 0 ? (lept_value*)malloc(capacity * sizeof(lept_value)) : NULL;
}

size_t lept_get_array_size(const lept_value* v) {
    assert(v != NULL && v->type == LEPT_ARRAY);
    return v->u.a.size;
}

size_t lept_get_array_capacity(const lept_value* v) {
    assert(v != NULL && v->type == LEPT_ARRAY);
    return v->u.a.capacity;
}

void lept_reserve_array(lept_value* v, size_t capacity) {
    assert(v != NULL && v->type == LEPT_ARRAY);
    if (v->u.a.capacity < capacity) {
        v->u.a.capacity = capacity;
        v->u.a.e = (lept_value*)realloc(v->u.a.e, capacity * sizeof(lept_value));
    }
}

void lept_shrink_array(lept_value* v) {
	assert(v != NULL && v->type == LEPT_ARRAY);
	if (v->u.a.capacity > v->u.a.size) {
		v->u.a.capacity = v->u.a.size;
		v->u.a.e = (lept_value*)realloc(v->u.a.e, v->u.a.capacity * sizeof(lept_value));
	}
}
void lept_clear_array(lept_value* v) {
    assert(v != NULL && v->type == LEPT_ARRAY);
    lept_erase_array_element(v, 0, v->u.a.size);
}

lept_value* lept_get_array_element(lept_value* v, size_t index) {
    assert(v != NULL && v->type == LEPT_ARRAY);
    assert(index < v->u.a.size);
    return &v->u.a.e[index];
}

lept_value* lept_pushback_array_element(lept_value* v) {
    assert(v != NULL && v->type == LEPT_ARRAY);
    if (v->u.a.size == v->u.a.capacity)
        lept_reserve_array(v, v->u.a.capacity == 0 ? 1 : v->u.a.capacity * 2);
    lept_init(&v->u.a.e[v->u.a.size]);
    return &v->u.a.e[v->u.a.size++];
}

void lept_popback_array_element(lept_value* v) {
    assert(v != NULL && v->type == LEPT_ARRAY && v->u.a.size > 0);
    lept_free(&v->u.a.e[--v->u.a.size]);
}

// index不可以超过size，因为是插入，等于size的话就是相当于插在末尾
lept_value* lept_insert_array_element(lept_value* v, size_t index) {
    assert(v != NULL && v->type == LEPT_ARRAY && index <= v->u.a.size);
    if (v->u.a.size == v->u.a.capacity)
		lept_reserve_array(v, v->u.a.capacity == 0 ? 1 : (v->u.a.size << 1)); //扩容为原来一倍
	memcpy(&v->u.a.e[index + 1], &v->u.a.e[index], (v->u.a.size - index) * sizeof(lept_value));
	lept_init(&v->u.a.e[index]);
	v->u.a.size++;
	return &v->u.a.e[index];
}

void lept_erase_array_element(lept_value* v, size_t index, size_t count) {
    assert(v != NULL && v->type == LEPT_ARRAY && index + count <= v->u.a.size);
    /* \todo */
    size_t i;
	for (i = index; i < index + count; i++) {
		lept_free(&v->u.a.e[i]);
	}
	memcpy(v->u.a.e + index, v->u.a.e + index + count, (v->u.a.size - index - count) * sizeof(lept_value));
	for (i = v->u.a.size - count; i < v->u.a.size; i++)
		lept_init(&v->u.a.e[i]);
	v->u.a.size -= count;
}

void lept_set_object(lept_value* v, size_t capacity) {
    assert(v != NULL);
    lept_free(v);
    v->type = LEPT_OBJECT;
    v->u.o.size = 0;
    v->u.o.capacity = capacity;
    v->u.o.m = capacity > 0 ? (lept_member*)malloc(capacity * sizeof(lept_member)) : NULL;
}

size_t lept_get_object_size(const lept_value* v) {
    assert(v != NULL && v->type == LEPT_OBJECT);
    return v->u.o.size;
}

size_t lept_get_object_capacity(const lept_value* v) {
    assert(v != NULL && v->type == LEPT_OBJECT);
    // 返回对象容量大小
	return v->u.o.capacity;
}

void lept_reserve_object(lept_value* v, size_t capacity) {
    assert(v != NULL && v->type == LEPT_OBJECT);
    /* \todo */
    // 重置容量, 比原来大。
	if (v->u.o.capacity < capacity) {
		v->u.o.capacity = capacity;
		v->u.o.m = (lept_member*)realloc(v->u.o.m, capacity * sizeof(lept_member));
	}
}

void lept_shrink_object(lept_value* v) {
    assert(v != NULL && v->type == LEPT_OBJECT);
    /* \todo */
    // 收缩容量到刚好符合大小
	if (v->u.o.capacity > v->u.o.size) {
		v->u.o.capacity = v->u.o.size;
		v->u.o.m = (lept_member*)realloc(v->u.o.m, v->u.o.capacity * sizeof(lept_member));
	}
}

void lept_clear_object(lept_value* v) {
    assert(v != NULL && v->type == LEPT_OBJECT);
    /* \todo */
    // 清空对象
	size_t i;
	for (i = 0; i < v->u.o.size; i++) {
		//回收k和v空间
		free(v->u.o.m[i].k);
		v->u.o.m[i].k = NULL;
		v->u.o.m[i].klen = 0;
		lept_free(&v->u.o.m[i].v);
	}
	v->u.o.size = 0;
}

const char* lept_get_object_key(const lept_value* v, size_t index) {
    assert(v != NULL && v->type == LEPT_OBJECT);
    assert(index < v->u.o.size);
    return v->u.o.m[index].k;
}

size_t lept_get_object_key_length(const lept_value* v, size_t index) {
    assert(v != NULL && v->type == LEPT_OBJECT);
    assert(index < v->u.o.size);
    return v->u.o.m[index].klen;
}

lept_value* lept_get_object_value(lept_value* v, size_t index) {
    assert(v != NULL && v->type == LEPT_OBJECT);
    assert(index < v->u.o.size);
    return &v->u.o.m[index].v;
}

size_t lept_find_object_index(const lept_value* v, const char* key, size_t klen) {
    size_t i;
    assert(v != NULL && v->type == LEPT_OBJECT && key != NULL);
    for (i = 0; i < v->u.o.size; i++)
        if (v->u.o.m[i].klen == klen && memcmp(v->u.o.m[i].k, key, klen) == 0)
            return i;
    return LEPT_KEY_NOT_EXIST;
}

lept_value* lept_find_object_value(lept_value* v, const char* key, size_t klen) {
    size_t index = lept_find_object_index(v, key, klen);
    return index != LEPT_KEY_NOT_EXIST ? &v->u.o.m[index].v : NULL;
}

// 设置k字段为key的对象的值，如果在查找过程中找到了已经存在key，则返回；否则新申请一块空间并初始化，然后返回
lept_value* lept_set_object_value(lept_value* v, const char* key, size_t klen) {
    assert(v != NULL && v->type == LEPT_OBJECT && key != NULL);
    /* \todo */
    size_t i, index;
	index = lept_find_object_index(v, key, klen);
	if (index != LEPT_KEY_NOT_EXIST)
		return &v->u.o.m[index].v;
	//key not exist, then we make room and init
	if (v->u.o.size == v->u.o.capacity) {
		lept_reserve_object(v, v->u.o.capacity == 0 ? 1 : (v->u.o.capacity << 1));
	}
	i = v->u.o.size;
	v->u.o.m[i].k = (char*)malloc((klen + 1));
	memcpy(v->u.o.m[i].k, key, klen);
	v->u.o.m[i].k[klen] = '\0';
	v->u.o.m[i].klen = klen;
	lept_init(&v->u.o.m[i].v);
	v->u.o.size++;
	return &v->u.o.m[i].v;
}

void lept_remove_object_value(lept_value* v, size_t index) {
    assert(v != NULL && v->type == LEPT_OBJECT && index < v->u.o.size);
    /* \todo */
    free(v->u.o.m[index].k);
	lept_free(&v->u.o.m[index].v);
	//think like a list
	memcpy(v->u.o.m + index, v->u.o.m + index + 1, (v->u.o.size - index - 1) * sizeof(lept_member));   // 这里原来有错误
	// 原来的size比如是10，最多其实只能访问下标为9
	// 删除一个元素，再进行挪移，原来为9的地方要清空
	// 现在先将size--，则size就是9
	v->u.o.m[--v->u.o.size].k = NULL;  
	v->u.o.m[v->u.o.size].klen = 0;
	lept_init(&v->u.o.m[v->u.o.size].v);
}
