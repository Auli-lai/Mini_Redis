# mini-redis — 兼容 Redis 协议的内存数据库

基于 C++11 从零构建的 Redis 兼容服务器。使用 **跳表** 实现 Sorted Set，**epoll** 单线程事件循环，支持 **5 种核心数据结构**、**键过期** 和 **AOF 持久化**。

---

## 特性

- **RESP 协议兼容** — 可用 `redis-cli` 直接连接操作
- **5 种数据结构** — String、List、Hash、Set、Sorted Set（跳表实现）
- **键过期** — 惰性删除 + 定期删除，支持 EXPIRE/TTL 命令
- **AOF 持久化** — 写命令日志，重启恢复数据
- **单线程 epoll** — 同 Redis 设计，避免锁竞争和上下文切换
- **连接超时** — 最小堆定时器，60 秒自动关闭空闲连接
- **优雅关闭** — SIGINT/SIGTERM 安全退出

---

## 快速开始

```bash
# 编译
cd mini_redis
mkdir build && cd build
cmake .. && make -j$(nproc)

# 启动
./mini_redis           # 默认 6379 端口
./mini_redis 6380      # 自定义端口

# 连接
redis-cli -p 6379
```

---

## 支持的命令

### String
| 命令 | 说明 |
|------|------|
| SET key value | 设置键值 |
| GET key | 获取值 |
| DEL key [key ...] | 删除键 |
| EXISTS key | 检查键是否存在 |
| KEYS pattern | 查找匹配的键 |

### List
| 命令 | 说明 |
|------|------|
| LPUSH key value [value ...] | 左侧插入 |
| RPUSH key value [value ...] | 右侧插入 |
| LPOP key | 左侧弹出 |
| RPOP key | 右侧弹出 |
| LRANGE key start stop | 范围查询 |
| LLEN key | 列表长度 |

### Hash
| 命令 | 说明 |
|------|------|
| HSET key field value | 设置字段 |
| HGET key field | 获取字段 |
| HDEL key field | 删除字段 |
| HGETALL key | 获取全部字段 |
| HEXISTS key field | 字段是否存在 |

### Set
| 命令 | 说明 |
|------|------|
| SADD key member [member ...] | 添加成员 |
| SREM key member | 删除成员 |
| SMEMBERS key | 获取全部成员 |
| SISMEMBER key member | 是否成员 |
| SCARD key | 集合大小 |

### Sorted Set
| 命令 | 说明 |
|------|------|
| ZADD key score member | 添加成员 |
| ZREM key member | 删除成员 |
| ZSCORE key member | 获取分数 |
| ZRANGE key start stop [WITHSCORES] | 范围查询 |
| ZRANK key member | 获取排名 |
| ZCARD key | 有序集合大小 |

### 其他
| 命令 | 说明 |
|------|------|
| EXPIRE key seconds | 设置过期时间 |
| TTL key | 查看剩余时间 |
| PING | 心跳检测 |
| DBSIZE | 键总数 |
| FLUSHDB | 清空数据库 |
| INFO | 服务器信息 |

---

## 技术亮点

| 层面 | 实现 |
|------|------|
| 网络模型 | 单线程 epoll ET + EPOLLONESHOT |
| 协议解析 | RESP 协议状态机 |
| Sorted Set | 跳表（随机层高，O(log n) 增删查） |
| 键过期 | 惰性删除 + 定期扫描 |
| 持久化 | AOF 追加日志 |
| 定时器 | 最小堆，O(log n) 管理连接超时 |

---

## 项目结构

```
mini_redis/
├── CMakeLists.txt
├── README.md
├── include/
│   ├── server.h          # TCP 服务器
│   ├── epoll.h           # epoll 事件循环
│   ├── resp_parser.h     # RESP 协议解析器
│   ├── db.h              # 数据库引擎 + 跳表
│   ├── command.h         # 命令路由表
│   ├── timer.h           # 最小堆定时器
│   ├── log.h             # 日志系统
│   └── queue.h           # 线程安全队列
└── src/
    ├── main.cpp
    ├── server.cpp
    ├── epoll.cpp
    ├── resp_parser.cpp
    ├── db.cpp
    ├── command.cpp
    ├── timer.cpp
    └── log.cpp
```

## 编译要求

- **系统:** Linux (epoll)
- **编译器:** g++ 4.8+ / clang 3.3+ (C++11)
- **工具:** CMake 3.10+, make

## 许可证

MIT License
