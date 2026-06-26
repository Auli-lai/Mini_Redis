#include "epoll.h"
#include "resp_parser.h"
#include "timer.h"
#include "log.h"
#include "command.h"
#include "db.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>

extern Database g_db;
extern CommandTable g_cmd;

Epoll::Epoll() {
    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) Log::error("Failed to create epoll");
    events.resize(1024);
}

Epoll::~Epoll() {
    if (epoll_fd != -1) close(epoll_fd);
    for (auto& p : parsers) delete p.second;
}

int Epoll::add(int fd, uint32_t events) {
    struct epoll_event ev;
    ev.data.fd = fd;
    ev.events = events;
    return epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev);
}

int Epoll::mod(int fd, uint32_t events) {
    struct epoll_event ev;
    ev.data.fd = fd;
    ev.events = events;
    return epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev);
}

int Epoll::del(int fd) {
    return epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
}

int Epoll::wait(int timeout) {
    return epoll_wait(epoll_fd, events.data(), events.size(), timeout);
}

void Epoll::mark_timed_out(int fd) {
    std::lock_guard<std::mutex> lock(mtx);
    timed_out_fds.insert(fd);
}

bool Epoll::is_timed_out(int fd) {
    std::lock_guard<std::mutex> lock(mtx);
    return timed_out_fds.find(fd) != timed_out_fds.end();
}

void Epoll::remove_timed_out(int fd) {
    std::lock_guard<std::mutex> lock(mtx);
    timed_out_fds.erase(fd);
}

// ── 工具函数 ──
static void close_connection(int fd, Epoll* epoll) {
    epoll->del(fd);
    close(fd);
    Timer::remove(fd);
    epoll->remove_timed_out(fd);
    {
        std::lock_guard<std::mutex> lock(epoll->mtx);
        auto it = epoll->parsers.find(fd);
        if (it != epoll->parsers.end()) {
            delete it->second;
            epoll->parsers.erase(it);
        }
    }
}

// ── 处理单个客户端 fd 的可读事件 ──
static void process_client_read(int fd, Epoll* epoll, Database& db, CommandTable& cmd) {
    if (epoll->is_timed_out(fd)) {
        close_connection(fd, epoll);
        return;
    }

    RespParser* parser = nullptr;
    {
        std::lock_guard<std::mutex> lock(epoll->mtx);
        auto it = epoll->parsers.find(fd);
        if (it == epoll->parsers.end()) {
            parser = new RespParser(fd);
            epoll->parsers[fd] = parser;
        } else {
            parser = it->second;
        }
    }

    // 读数据
    char buf[4096];
    ssize_t n = read(fd, buf, sizeof(buf));
    if (n <= 0) {
        if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            epoll->mod(fd, EPOLLIN | EPOLLET | EPOLLONESHOT);
            return;
        }
        close_connection(fd, epoll);
        return;
    }

    Timer::refresh(fd, 60);

    // 喂给 RESP 解析器
    RespParser::ParseResult res = parser->feed(buf, n);
    if (res == RespParser::PARSE_OK) {
        // 执行命令
        std::string response = cmd.execute(parser->get_command());
        // 写响应: 直接写入（简单场景下同步写即可）
        parser->reset();

        // 把响应写到 fd
        size_t offset = 0;
        while (offset < response.size()) {
            ssize_t written = write(fd, response.c_str() + offset, response.size() - offset);
            if (written == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
                Log::error("Write failed (fd=%d)", fd);
                close_connection(fd, epoll);
                return;
            }
            offset += written;
        }
        // 响应写完，重新监听读
        epoll->mod(fd, EPOLLIN | EPOLLET | EPOLLONESHOT);
    } else if (res == RespParser::PARSE_AGAIN) {
        epoll->mod(fd, EPOLLIN | EPOLLET | EPOLLONESHOT);
    } else {
        Log::error("RESP parse error (fd=%d): %s", fd, parser->get_error().c_str());
        std::string err = "-ERR protocol error\r\n";
        write(fd, err.c_str(), err.size());
        close_connection(fd, epoll);
    }
}

void Epoll::handle_events(int listen_fd) {
    int nfds = wait(100); // 100ms 超时，让定期过期检查能及时运行
    if (nfds == -1) return;

    for (int i = 0; i < nfds; ++i) {
        int fd = events[i].data.fd;
        uint32_t ev = events[i].events;

        if (fd == listen_fd) {
            struct sockaddr_in client_addr;
            socklen_t len = sizeof(client_addr);
            int client_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &len);
            if (client_fd == -1) { Log::error("accept failed"); continue; }

            int flags = fcntl(client_fd, F_GETFL, 0);
            fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);

            add(client_fd, EPOLLIN | EPOLLET | EPOLLONESHOT);
            Timer::add(client_fd, 60);
            Log::info("New connection fd=%d", client_fd);
        } else if (ev & EPOLLIN) {
            process_client_read(fd, this, g_db, g_cmd);
        } else if (ev & (EPOLLERR | EPOLLHUP)) {
            close_connection(fd, this);
        }
    }
}
