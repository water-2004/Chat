# README

## 1. 模块概览

本即时通讯系统主要由四个核心模块组成：

| 模块名           | 主要职责                                            |
| ---------------- | --------------------------------------------------- |
| **ChatServer**   | 提供聊天服务、好友申请/认证服务、在线消息推送       |
| **GateServer**   | 提供客户端接入、身份验证、负载均衡和消息转发        |
| **StatusServer** | 负责服务器状态监控，返回负载最低的 ChatServer       |
| **VerifyServer** | 提供用户邮箱验证码生成与发送服务，用于注册/身份验证 |



------

## 2. 模块职责与架构

### 2.1 ChatServer

- **模块职责**：
  - 处理客户端登录请求，验证 Token，并返回用户基础信息、好友列表、申请列表等。
  - 管理用户在线状态、会话绑定及跨服通信。
  - 提供好友申请、好友认证、文本消息推送等服务。
- **系统架构与依赖**：
  - 使用 **Boost.Asio + Boost.Beast** 实现 HTTP 服务监听与异步处理。
  - **RedisMgr**：管理 Redis 连接池，用于缓存用户基础信息、Token、会话映射等。
  - **MysqlMgr**：封装数据库连接池，操作用户数据及好友关系。
  - **ChatGrpcClient**：跨服务器 RPC 客户端，用于跨服好友申请、认证、文本消息推送。
  - **UserMgr**：管理在线用户会话，支持在线检测和条件推送。
  - **Defer + RAII**：统一资源管理，保证响应发送和连接归还。
- **设计亮点**：
  - **缓存优先策略**：优先 Redis，未命中再查 MySQL。
  - **跨服通信统一接口**：所有跨服操作通过 ChatGrpcClient 实现。
  - **高并发支持**：线程池 + 异步非阻塞处理，提高吞吐量。

------

### 2.2 GateServer

- **模块职责**：
  - 处理客户端接入请求，包括登录、消息转发、心跳检测。
  - 校验客户端身份信息（Token/UID），并维护会话信息。
  - 根据负载信息选择最合适的 ChatServer 转发客户端请求。
- **系统架构与依赖**：
  - **Boost.Asio** 异步网络 IO。
  - **RedisMgr**：缓存用户会话映射、Token 等。
  - **StatusServer**：通过 gRPC 获取负载最低的 ChatServer。
  - **ChatServer**：通过 gRPC 或 HTTP 转发消息。
- **设计亮点**：
  - **高可用接入**：支持多 GateServer 部署，负载均衡。
  - **异步消息转发**：减少阻塞，提高并发性能。

------

### 2.3 StatusServer

- **模块职责**：
  - 监控 ChatServer 状态，包括在线人数、CPU/内存使用率等。
  - 提供接口供 GateServer 查询负载最低的 ChatServer。
- **系统架构与依赖**：
  - 使用 **gRPC** 提供状态查询接口。
  - 与各 ChatServer 定期通信或接收心跳，维护服务器状态。
- **设计亮点**：
  - **负载均衡优化**：动态返回最佳 ChatServer，提高系统吞吐。
  - **模块解耦**：ChatServer 与 GateServer 无需直接感知其他 ChatServer 状态。

------

### 2.4 VerifyServer

- **模块职责**：
  - 提供邮箱验证码生成与发送服务，用于用户注册或身份验证。
  - 管理验证码缓存（Redis）及过期策略。
- **系统架构与依赖**：
  - **gRPC** 提供 `VarifyService` 接口。
  - **Redis (ioredis)**：缓存验证码，设置过期时间。
  - **nodemailer**：SMTP 邮件发送。
  - **uuid**：生成唯一验证码。
  - **config.js/config.json**：管理 Redis、邮箱配置。
- **核心接口**：
  - `GetVarifyCode(email)`：生成验证码、缓存至 Redis 并发送邮件。
- **设计亮点**：
  - **缓存优先**：Redis 查询优先，减少重复生成。
  - **安全性**：验证码随机生成，设置 10 分钟有效期。
  - **模块解耦**：Redis、邮件、配置独立封装。
  - **异步处理**：保证高并发下的稳定性。

------

### 3. 四模块交互关系

```
[客户端] 
   │
   ▼
[GateServer] ── gRPC ──> [StatusServer] (获取负载最低 ChatServer)
   │
   ▼
[ChatServer] ── gRPC ──> [VerifyServer] (请求邮箱验证码)
   │
   ▼
[Redis / MySQL] (用户信息、会话、好友关系、验证码缓存)
```

- 客户端通过 GateServer 接入系统，GateServer 根据状态选择 ChatServer。
- ChatServer 与 VerifyServer、Redis、MySQL 交互，完成登录、好友管理、消息推送等。
- StatusServer 提供 ChatServer 状态查询，实现负载均衡。
