#ifndef LEPTJSON_H__
#define LEPTJSON_H__
// 利用宏加入include防范，避免重复声明  一般可以用 项目名_目录_文件名称_H__

#include <stddef.h> // size_t

// 使用枚举定义json的6种数据类型（true和false看作两种的话就是7种）
// 由于c没有c++的namespace，所以一般使用项目简写作为标识符的前缀
typedef enum {
    LEPT_NULL,
    LEPT_FALSE,
    LEPT_TRUE,
    LEPT_NUMBER,
    LEPT_STRING,
    LEPT_ARRAY,
    LEPT_OBJECT
}lept_type;

// 因为在struct lept_value中我们还需要用到自身的类型，所以需要前置声明 forward declare
typedef struct lept_value lept_value;
typedef struct lept_member lept_member;

// 了解需求后我们会发现，lept_value事实上是一个变体类型，我们通过type来决定它现时是哪种类型，也决定哪些成员是有效的
// 声明json的数据结构  因为json是一个树形结构，所以结点使用lept_value表示
// 我们称其为一个json值
struct lept_value{
    union { // 因为一个值不可能同时为数字和字符串，所以为了节省内存我们使用union
        struct {
            lept_member* m;     // members
            size_t size;        // member的个数
        } o;
        struct {    // 储存数组元素
            lept_value* e;  // 元素 可变长度分配
            size_t size;    // 元素个数
        } a;
        struct {    // 字符串的长度变长分配
            char* s;
            size_t len;
        } s;
        double n;       //对于数字我们考虑以double来存储解析后的结果，仅当type=LEPT_NUMBER时，n才表示json数字的数值
    } u;
    
    lept_type type; //当值是bool和NULL时仅需要这个成员变量就可以了
};

struct lept_member {
    char* k;        // key 必须是一个JSON string
    size_t klen;    // length of key
    lept_value v;   // value 
};

// 在解析过程解析器应该能判断一个输入是否是合法的json，如果不符合我们应该产生对应的错误码，方便使用者追查问题
enum {
    LEPT_PARSE_OK = 0,                      // 无错误返回
    LEPT_PARSE_EXPECT_VALUE,                // 输入的json是空白的
    LEPT_PARSE_INVALID_VALUE,               // 不是一个合法的值
    LEPT_PARSE_ROOT_NOT_SINGULAR,           // 一个值之后还有其它的字符
    LEPT_PARSE_NUMBER_TOO_BIG,              // 数值过大，超过表示范围
    LEPT_PARSE_MISS_QUOTATION_MARK,         // 错误的引号
    LEPT_PARSE_INVALID_STRING_ESCAPE,       // 无效转义字符
    LEPT_PARSE_INVALID_STRING_CHAR,         // 无效字符
    LEPT_PARSE_INVALID_UNICODE_HEX,         // 无效的4位16进制xxxx
    LEPT_PARSE_INVALID_UNICODE_SURROGATE,   // 无效的代理对
    LEPT_PARSE_MISS_COMMA_OR_SQUARE_BRACKET,// 错误的逗号或中括号
    LEPT_PARSE_MISS_KEY,                    // 错误key
    LEPT_PARSE_MISS_COLON,                  // 冒号错误
    LEPT_PARSE_MISS_COMMA_OR_CURLY_BRACKET  // 逗号或{}错误
};

// 为了把表达式转为语句，模仿无返回值的函数
#define lept_init(v) do { (v)->type = LEPT_NULL; } while(0)

// 提供访问这个json结构的api接口

// 解析json 由于传入的json文本是一个c字符串，我们不希望改变它，所以使用‘const char*’
// 传入的v一般是使用方负责分配的  返回值是错误类型
int lept_parse(lept_value* v, const char* json);
// 生成器 字符化 length是一个可选参数 
char* lept_stringify(const lept_value* v, size_t* length);

// 现时仅当值是string array object时需要释放内存，释放后将type类型置为null避免重复释放
void lept_free(lept_value* v);

// 访问结果的函数，返回其类型
lept_type lept_get_type(const lept_value* v);

#define lept_set_null(v) lept_free(v)

int lept_get_boolean(const lept_value* v);
void lept_set_boolean(lept_value* v, int b);

void lept_set_number(lept_value* v, double n);
double lept_get_number(const lept_value* v);

const char* lept_get_string(const lept_value* v);
size_t lept_get_string_length(const lept_value* v);
void lept_set_string(lept_value* v, const char* s, size_t len);

size_t lept_get_array_size(const lept_value* v);
lept_value* lept_get_array_element(const lept_value* v, size_t index);

size_t lept_get_object_size(const lept_value* v);
const char* lept_get_object_key(const lept_value* v, size_t index);
size_t lept_get_object_key_length(const lept_value* v, size_t index);
lept_value* lept_get_object_value(const lept_value* v, size_t index);
#endif /* LEPTJSON_H__ */