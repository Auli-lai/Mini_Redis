#ifndef EPOLL_H
#define EPOLL_H

#include <vector>
#include <sys/epoll.h>
#include <unordered_map>
#include <set>
#include <mutex>

class RespParser;

class Epoll {
public:
    Epoll();
    ~Epoll();
    int add(int fd, uint32_t events);
    int mod(int fd, uint32_t events);
    int del(int fd);
    int wait(int timeout = -1);
    void handle_events(int listen_fd);

    // 连接级别的 parser 管理
    std::unordered_map<int, RespParser*> parsers;
    std::mutex mtx;  // 保护 parsers 和 timed_out_fds
    void mark_timed_out(int fd);
    bool is_timed_out(int fd);
    void remove_timed_out(int fd);

private:
    int epoll_fd;
    std::vector<struct epoll_event> events;
    std::set<int> timed_out_fds;
};

#endif // EPOLL_H
