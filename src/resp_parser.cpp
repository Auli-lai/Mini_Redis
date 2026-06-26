#include "resp_parser.h"
#include "log.h"
#include <cstdlib>
#include <cstring>

RespParser::RespParser(int fd)
    : fd(fd), state(S_WAIT_TYPE), array_count(0), bulk_len(0), parse_pos(0) {}

void RespParser::reset() {
    buffer.clear();
    command.clear();
    error_msg.clear();
    state = S_WAIT_TYPE;
    array_count = 0;
    bulk_len = 0;
    parse_pos = 0;
}

RespParser::ParseResult RespParser::feed(const char* data, size_t len) {
    buffer.append(data, len);

    // 至少需要一个字节来确定类型
    if (buffer.empty()) return PARSE_AGAIN;

    // 确定顶层类型
    if (state == S_WAIT_TYPE) {
        if (buffer[0] == '*') {
            state = S_ARRAY_COUNT;
            parse_pos = 1;
        } else {
            // 内联命令（非标准 RESP，但 redis-cli 也会发）
            // 格式: SET key value\r\n
            state = S_SIMPLE;
            parse_pos = 0;
        }
    }

    // 逐状态驱动解析
    while (parse_pos < buffer.size()) {
        size_t crlf = buffer.find("\r\n", parse_pos);
        if (crlf == std::string::npos) {
            // 对于 bulk data，不需要等待 \r\n
            if (state == S_BULK_DATA) {
                // bulk data 以 \r\n 结尾，我们已经知道长度
                if (buffer.size() - parse_pos < (size_t)bulk_len + 2) {
                    return PARSE_AGAIN;
                }
                command.push_back(buffer.substr(parse_pos, bulk_len));
                parse_pos += bulk_len + 2; // 跳过 data + \r\n
                if (--array_count > 0) {
                    state = S_ARRAY_ITEM;
                } else {
                    state = S_WAIT_TYPE;
                    return PARSE_OK;
                }
                continue;
            }
            return PARSE_AGAIN;
        }

        std::string line = buffer.substr(parse_pos, crlf - parse_pos);
        parse_pos = crlf + 2;

        switch (state) {
        case S_ARRAY_COUNT: {
            if (line.empty() || line[0] != '*') {
                // 内联命令回退
                state = S_SIMPLE;
                parse_pos = 0;
                continue;
            }
            array_count = std::stoi(line.substr(1));
            if (array_count <= 0) {
                command.clear();
                state = S_WAIT_TYPE;
                return PARSE_OK;
            }
            state = S_ARRAY_ITEM;
            break;
        }
        case S_ARRAY_ITEM: {
            if (line.empty() || line[0] != '$') {
                error_msg = "Expected bulk string in array";
                return PARSE_ERROR;
            }
            bulk_len = std::stoi(line.substr(1));
            if (bulk_len == -1) {
                command.push_back(""); // NULL
                if (--array_count > 0) {
                    state = S_ARRAY_ITEM;
                } else {
                    state = S_WAIT_TYPE;
                    return PARSE_OK;
                }
                break;
            }
            state = S_BULK_DATA;
            break;
        }
        case S_BULK_DATA: {
            size_t need = (size_t)bulk_len + 2; // data + \r\n
            // 回退 parse_pos，因为数据不是以 \r\n 开始的 line
            parse_pos = crlf + 2; // 重置到 crlf 之后
            if (buffer.size() - parse_pos < need) {
                // 回退到 crlf 之前
                parse_pos = crlf;
                return PARSE_AGAIN;
            }
            command.push_back(buffer.substr(parse_pos, bulk_len));
            parse_pos += need;
            if (--array_count > 0) {
                state = S_ARRAY_ITEM;
            } else {
                state = S_WAIT_TYPE;
                return PARSE_OK;
            }
            break;
        }
        case S_SIMPLE: {
            // 内联命令: 以空格分割
            std::string cmd_line = line;
            // 去掉末尾 \r (如果有)
            if (!cmd_line.empty() && cmd_line.back() == '\r')
                cmd_line.pop_back();

            // 按空格分割
            size_t pos = 0;
            while (pos < cmd_line.size()) {
                // 跳过空格
                while (pos < cmd_line.size() && cmd_line[pos] == ' ') pos++;
                if (pos >= cmd_line.size()) break;
                size_t end = cmd_line.find(' ', pos);
                if (end == std::string::npos) end = cmd_line.size();
                command.push_back(cmd_line.substr(pos, end - pos));
                pos = end;
            }
            state = S_WAIT_TYPE;
            return PARSE_OK;
        }
        default:
            error_msg = "Unknown state";
            return PARSE_ERROR;
        }
    }

    return PARSE_AGAIN;
}
