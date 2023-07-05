#include <stdlib.h>
#include <assert.h>
#include "leptjson.h"

// 这里使用do...while(0) 是一个编写宏的技巧
// 如果宏里面有多过一个语句，就使用do{}while(0)来包裹成单个语句
// 这个宏的作用是判断当前首字符是否是所期望的ch
#define EXPECT(c, ch) do{ assert(*c->json == (ch)); c->json++;} while(0)

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

// 解析值
static int lept_parse_value(lept_context* c, lept_value* v) {
    switch (*c->json) {
        case 't':   return lept_parse_true(c,v);
        case 'n':   return lept_parse_null(c,v);
        case 'f':   return lept_parse_false(c,v);
        case '\0':  return LEPT_PARSE_EXPECT_VALUE;
        default:    return LEPT_PARSE_INVALID_VALUE;
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
        if(*c.json != '\0')
            ret = LEPT_PARSE_ROOT_NOT_SINGULAR;
    }
    return ret;
}

lept_type lept_get_type(const lept_value* v) {
    assert(v != NULL);
    return v->type;
}