#include <stdlib.h>         // NULL, strtod()
#include <assert.h>         // asser()
#include "leptjson.h"
#include <errno.h>          // errno, ERANGE
#include <math.h>           // HUGE_VAL

// 这里使用do...while(0) 是一个编写宏的技巧
// 如果宏里面有多过一个语句，就使用do{}while(0)来包裹成单个语句
// 这个宏的作用是判断当前首字符是否是所期望的ch
#define EXPECT(c, ch)       do { assert(*c->json == (ch)); c->json++; } while(0)
#define ISDIGIT(ch)         ((ch) >= '0' && (ch) <= '9')
#define ISDIGIT1TO9(ch)     ((ch) >= '1' && (ch) <= '9')

// 首先为了减少解析函数之间传递多个参数，我们把这些数据都放进一个 `lept_context` 结构体
typedef struct 
{
    // 表示的是指针json指向的数值不能修改， 但是可以通过本身自己去修
    const char* json;
} lept_context;

// 解析ws
static void lept_parse_whitespace(lept_context* c) {
    const char* p = c->json;
    while(*p == ' ' || *p == '\t' || *p == '\r')
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
#endif

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

// 解析number
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
    v->n = strtod(c->json, NULL);
    // ERANGE 表示一个范围错误，它在输入参数超出数学函数定义的范围时发生，errno 被设置为 ERANGE。
    // HUGE_VAL 最大的双精度值 也就是inf-->无穷
    if (errno == ERANGE && (v->n == HUGE_VAL || v->n == -HUGE_VAL))
        return LEPT_PARSE_NUMBER_TOO_BIG;
    v->type = LEPT_NUMBER;
    c->json = p;
    return LEPT_PARSE_OK;
}

// 解析值
static int lept_parse_value(lept_context* c, lept_value* v) {
    switch (*c->json) {
        case 't':   return lept_parse_literal(c, v, "true", LEPT_TRUE);
        case 'n':   return lept_parse_literal(c, v, "null", LEPT_NULL);
        case 'f':   return lept_parse_literal(c, v, "false", LEPT_FALSE);
        default:    return lept_parse_number(c, v);
        case '\0':  return LEPT_PARSE_EXPECT_VALUE;
    }
}

int lept_parse(lept_value* v, const char* json) {
    lept_context c;
    int ret;
    assert(v != NULL);
    c.json = json;
    // 此处先将v设置为LEPT_NULL  让lept_parse_value()写入解析出来的根值
    v->type = LEPT_NULL;
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
    return ret;
}

lept_type lept_get_type(const lept_value* v) {
    assert(v != NULL);
    return v->type;
}

// 只有当type为LEPT_NUMBER时才可以取获得数值
double lept_get_number(const lept_value* v) {
    assert(v != NULL && v->type == LEPT_NUMBER);
    return v->n;
}