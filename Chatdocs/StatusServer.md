# StatusServer 模块文档

------

## 一、模块职责

- 提供聊天服务器（ChatServer）的负载均衡服务，根据当前各聊天服务器连接数分配用户访问地址
- 管理用户登录 Token，保证 Token 有效性，支持用户身份验证
- 对接 Redis 作为缓存存储，存储 Token 和聊天服务器连接数等信息
- 为外部提供 gRPC 接口供 GateServer 调用

------

## 二、核心类说明

### 1. `StatusServiceImpl`

- **功能：** 实现 gRPC 定义的服务接口
- **主要接口：**
  - `GetChatServer`: 根据负载情况返回负载最小的 ChatServer 信息和对应 Token
  - `Login`: 验证客户端提交的 Token 是否有效
- **辅助方法：**
  - `insertToken(int uid, std::string token)`: 将用户 Token 写入 Redis
  - `getChatServer()`: 从内存中维护的服务器列表结合 Redis 统计信息选出连接数最少的服务器

------

### 2. `ChatGrpcClient`

- **功能：** 作为 gRPC 客户端调用其他 ChatServer 的服务（如好友通知等）
- **主要职责：** 管理多个 ChatServer 连接池，分发请求
- **代码特点：**
  - 通过配置文件读取多台 ChatServer 信息，初始化对应连接池
  - 实现调用接口（目前示例为 `NotifyAddFriend`）

------

## 三、详细流程说明

### 1. 负载均衡流程（GetChatServer）

- 客户端（GateServer）调用 `GetChatServer` 接口请求聊天服务器信息
- `getChatServer()` 方法：
  - 从配置文件加载的服务器列表 `_servers` 里，结合 Redis 中的登录连接数（`LOGIN_COUNT`）统计每台服务器当前连接数
  - 选出连接数最少的服务器返回
- 生成随机唯一 Token 并保存到 Redis
- 返回服务器地址及 Token

------

### 2. 登录校验流程（Login）

- 客户端发起登录请求，携带 `uid` 和 `token`
- 从 Redis 中根据 `uid` 读取存储的 Token
- 校验请求 Token 与 Redis 中 Token 是否一致
- 校验成功返回成功状态，否则返回对应错误码

------

## 四、设计亮点与待完善

### 亮点

- 利用 Redis 实时统计服务器连接数，实现负载均衡调度
- 使用 Token 机制保证安全性，防止伪造登录
- 结合配置文件实现动态服务器管理，方便扩展

### 待完善

- 当前连接数依赖 Redis 数据的实时更新，需保证 Redis 数据一致性
- Token 生成采用 UUID，安全性尚可，但缺少有效期机制（建议添加Token过期时间管理）
- 缺少对 Redis 访问失败的容错机制，需要增加异常处理和重试
- `ChatGrpcClient` 功能待完善，目前只有简单的好友通知示例
- 增加日志记录和性能监控

------

## 五、代码示例解读（节选）

```c++
// 负载均衡选择连接数最少的聊天服务器
ChatServer StatusServiceImpl::getChatServer() {
    std::lock_guard<std::mutex> guard(_server_mtx);
    auto minServer = _servers.begin()->second;
    auto count_str = RedisMgr::GetInstance()->HGet(LOGIN_COUNT,minServer.name);
    minServer.con_count = count_str.empty() ? INT_MAX : std::stoi(count_str);

    for (auto& server : _servers) {
        if (server.second.name == minServer.name) continue;
        count_str = RedisMgr::GetInstance()->HGet(LOGIN_COUNT, server.second.name);
        server.second.con_count = count_str.empty() ? INT_MAX : std::stoi(count_str);
        if (server.second.con_count < minServer.con_count) {
            minServer = server.second;
        }
    }
    return minServer;
}
```

- 利用互斥锁保证线程安全
- 从 Redis 读取连接数，空时默认极大数，保证被跳过
- 选出连接数最少的服务器