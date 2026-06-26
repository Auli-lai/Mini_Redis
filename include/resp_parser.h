#ifndef RESP_PARSER_H
#define RESP_PARSER_H

#include <string>
#include <vector>
#include <cstddef>

class RespParser {
public:
    enum ParseResult {
        PARSE_OK,       // 一条完整命令解析完成
        PARSE_AGAIN,    // 数据不完整，需要继续读
        PARSE_ERROR     // 协议错误
    };

    RespParser(int fd);
    void reset();

    // 喂数据，返回解析结果。解析成功后可通过 get_command() 获取结果。
    ParseResult feed(const char* data, size_t len);

    // 获取解析后的命令: 第一个元素是命令名(大写)，后续是参数
    const std::vector<std::string>& get_command() const { return command; }
    const std::string& get_error() const { return error_msg; }

private:
    int fd;

    // 输入缓冲区 (处理半包)
    std::string buffer;

    // 解析出来的命令
    std::vector<std::string> command;

    // 错误信息
    std::string error_msg;

    // 内部解析状态
    enum State {
        S_WAIT_TYPE,     // 等待 * $ : + -
        S_ARRAY_COUNT,   // 读取数组元素个数
        S_ARRAY_ITEM,    // 读取数组中的下一个元素
        S_BULK_LEN,      // 读取批量字符串长度
        S_BULK_DATA,     // 读取批量字符串数据
        S_SIMPLE,        // 读取简单字符串 +
        S_ERROR,         // 读取错误 -
        S_INTEGER,       // 读取整数 :
    };

    State state;
    int array_count;     // 数组剩余元素数
    int bulk_len;        // 当前批量字符串预期长度
    size_t parse_pos;    // 当前解析位置

    bool try_parse();
    bool parse_char(char c);
};

#endif // RESP_PARSER_H
