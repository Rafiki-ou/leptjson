#ifndef LEPTJSON_H__
#define LEPTJSON_H__
// 利用宏加入include防范，避免重复声明  一般可以用 项目名_目录_文件名称_H__

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

// 声明json的数据结构  因为json是一个树形结构，所以结点使用lept_value表示
// 我们称其为一个json值
typedef struct 
{
    lept_type type;
} lept_value;


// 在解析过程解析器应该能判断一个输入是否是合法的json，如果不符合我们应该产生对应的错误码，方便使用者追查问题
enum {
    LEPT_PARSE_OK = 0,              // 无错误返回
    LEPT_PARSE_EXPECT_VALUE,        // 输入的json是空白的
    LEPT_PARSE_INVALID_VALUE,       // 不是一个合法的值
    LEPT_PARSE_ROOT_NOT_SINGULAR    // 一个值之后还有其它的字符
};

// 提供访问这个json结构的api接口

// 解析json 由于传入的json文本是一个c字符串，我们不希望改变它，所以使用const char*
// 传入的v一般是使用方负责分配的  返回值是错误类型
int lept_parse(lept_value* v, const char* json);

// 访问结果的函数，返回其类型
lept_type lept_get_type(const lept_value* v);
#endif