# GateServer 模块技术文档

------

## 1. 模块概述

### 1.1 模块职责

- 提供用户注册功能，校验邮箱验证码，验证唯一性，并写入数据库
- 提供用户登录功能，验证邮箱与密码，并从 `StatusServer` 获取可用的 `ChatServer` 信息
- 提供密码重置功能，验证验证码及用户信息，更新数据库中的密码
- 提供邮箱验证码请求功能，通过 `VarifyServer` 获取验证码并写入 `Redis`

### 1.2 系统架构与依赖关系

- 使用 `Boost.Asio` + `Boost.Beast` 实现 HTTP 服务监听与异步处理
- 使用 `Redis` 作为缓存存储用户信息和验证码
- 依赖 `VerifyServer` 进行验证码相关校验，通过 gRPC 通信
- 依赖 `StatusServer` 获取负载最低的 `ChatServer`，通过 gRPC 通信
- 使用线程池管理 IO，上层封装了 `AsioIOServicePool`
- 使用数据库连接池 `MySqlPool`，封装为 `MysqlMgr` 管理层，具体数据库操作由 `MysqlDao` 实现层完成
- 使用 `RedisMgr` 管理 Redis 连接池

### 1.3资源管理与池化设计

- **AsioIOServicePool**：管理和维护 `io_context` 池，实现异步 IO 的多线程处理，提升并发性能与响应速度。
- **MySqlPool**：数据库连接池，管理 MySQL 连接资源，避免频繁建立连接造成性能损耗，支持高并发数据库访问。
- **MysqlMgr**：数据库管理层，封装业务逻辑与持久层接口，协调数据库操作，保证事务一致性。
- **MysqlDao**：数据库访问实现层，直接执行具体 SQL 语句，封装细节，提升代码复用性和维护性。
- **RedisMgr**：Redis 缓存池管理，统一管理 Redis 连接，优化缓存访问，降低数据库压力。

## 2. 启动流程

1. 加载配置项，读取端口号
2. 创建 `io_context`，用于异步事件处理（通过 `AsioIOServicePool` 获取）
3. 注册系统信号（`SIGINT`、`SIGTERM`）处理，支持安全退出
4. 初始化服务器对象 `CServer`，调用其 `Start()` 方法监听客户端连接
5. 运行事件循环，处理所有异步事件

示例代码：

```c++
int main() {
    auto &gCfgMgr = ConfigMgr::Inst();
    std::string gate_port_str = gCfgMgr["GateServer"]["Port"];
    unsigned short gate_port = atoi(gate_port_str.c_str());

    try {
        net::io_context ioc{ 1 };
        boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);
        signals.async_wait([&ioc](const boost::system::error_code& error, int signal_number) {
            if (error) {
                return;
            }
            ioc.stop();
        });

        std::make_shared<CServer>(ioc, gate_port)->Start();
        std::cout << "Gate Server listen on port:" << gate_port << std::endl;

        ioc.run();
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
}
```

------

## 3. 核心类设计

### 3.1 CServer

- **职责**：封装服务器监听器逻辑，负责接收客户端 TCP 连接
- **实现**：
  - 监听绑定端口
  - 异步接受连接，创建 `HttpConnection` 对象管理连接
  - 采用递归调用保证持续监听
- **示例代码（启动监听）**：

```c++
void CServer::Start() {
    auto self = shared_from_this();
    auto& io_context = AsioIOServicePool::GetInstance()->GetIOService();
    std::shared_ptr<HttpConnection> new_con = std::make_shared<HttpConnection>(io_context);

    _acceptor.async_accept(new_con->GetSocket(), [self, new_con](beast::error_code ec) {
        try {
            if (ec) {
                std::cout << "Accept failed: " << ec.message() << std::endl;
                self->Start();
                return;
            }
            std::cout << "Accept success, new connection established" << std::endl;
            new_con->Start();
            self->Start();
        }
        catch (std::exception& exp) {
            std::cout << "exception is " << exp.what() << std::endl;
            self->Start();
        }
    });
}
```

- **设计要点**：
  - 使用 `shared_from_this()` 确保对象生命周期
  - IO 上下文使用 `AsioIOServicePool`，注意线程一致性
- **存在风险**：
  - 未限制连接数量，可能导致资源耗尽
  - 未实现连接超时和恶意连接限制策略

------

### 3.2 HttpConnection

- **职责**：管理每个 HTTP 请求连接，异步读写请求和响应
- **关键方法**：
  - `Start()`：异步读取请求数据
  - `HandleReq()`：路由分发请求处理
- **异步读取示例**：

```c++
auto self = shared_from_this();
http::async_read(_socket, _buffer, _request, [self](beast::error_code ec, std::size_t bytes_transferred) {
    try {
        if (ec) {
            std::cout << "http read err is " << ec.what() << std::endl;
            return;
        }

        boost::ignore_unused(bytes_transferred);
        self->HandleReq();
        self->CheckDeadline();
    }
    catch (std::exception& exp) {
        std::cout << "exception is" << exp.what() << std::endl;
    }
});
```

- **请求处理示例**：

```c++
_response.version(_request.version());
_request.keep_alive(false);

if (_request.method() == http::verb::get) {
    PreParseGetParam();
    bool success = LogicSystem::GetInstance()->HandleGet(_get_url, shared_from_this());
    if (!success) {
        _response.result(http::status::not_found);
        _response.set(http::field::content_type, "text/plain");
        beast::ostream(_response.body()) << "url not found\r\n";
        WriteResponse();
        return;
    }
    _response.result(http::status::ok);
    _response.set(http::field::server, "GateServer");
    WriteResponse();
    return;
}

if (_request.method() == http::verb::post) {
    bool success = LogicSystem::GetInstance()->HandlePost(_request.target(), shared_from_this());
    if (!success) {
        _response.result(http::status::not_found);
        _response.set(http::field::content_type, "text/plain");
        beast::ostream(_response.body()) << "url not found\r\n";
        WriteResponse();
        return;
    }
    _response.result(http::status::ok);
    _response.set(http::field::server, "GateServer");
    WriteResponse();
    return;
}
```

------

### 3.3 LogicSystem

- **职责**：逻辑调度中心，管理接口路由与业务处理
- **核心数据**：

```c++
std::map<std::string, HttpHandler> _post_handlers;
std::map<std::string, HttpHandler> _get_handlers;
```

- **主要接口**：

| 函数                                                         | 功能                     |
| ------------------------------------------------------------ | ------------------------ |
| `void RegGet(const std::string&, HttpHandler)`               | 注册 GET 路由及处理函数  |
| `void RegPost(const std::string&, HttpHandler)`              | 注册 POST 路由及处理函数 |
| `bool HandleGet(const std::string&, std::shared_ptr<HttpConnection>)` | 查找并执行 GET 处理函数  |
| `bool HandlePost(const std::string&, std::shared_ptr<HttpConnection>)` | 查找并执行 POST 处理函数 |



- **启动注册示例**：

```c++
logicSystem.RegPost("/user_register", HandleUserRegister);
logicSystem.RegPost("/user_login", HandleUserLogin);
logicSystem.RegPost("/get_varifycode", HandleGetVarifyCode);
logicSystem.RegPost("/reset_pwd", HandleResetPwd);
```

------

## 4. 错误码定义

| 错误码                | 含义          |
| --------------------- | ------------- |
| `Success`=0           | 成功          |
| `Error_Json`=1001     | JSON 解析失败 |
| `RPCFailed`=1002      | RPC 请求错误  |
| `VarifyExpired`=1003  | 验证码过期    |
| `VarifyCodeErr`=1004  | 验证码错误    |
| `UserExist`=1005      | 用户名已存在  |
| `PasswdErr`=1006      | 密码错误      |
| `EmailNotMatch`=1007  | 邮箱不匹配    |
| `PasswdUpFailed`=1008 | 更新密码失败  |
| `PasswdInvalid`=1009  | 密码更新失败  |



------

## 5. 主要接口说明

### 5.1 `/user_register` — 用户注册

- **请求方式**：POST
- **请求参数**：

| 参数名       | 类型   | 描述       |
| ------------ | ------ | ---------- |
| `email`      | string | 用户邮箱   |
| `user`       | string | 用户名     |
| `passwd`     | string | 密码       |
| `confirm`    | string | 确认密码   |
| `varifycode` | string | 邮箱验证码 |



- **响应字段**：

| 字段名  | 类型   | 描述                  |
| ------- | ------ | --------------------- |
| `error` | int    | 错误码（0为成功）     |
| `uid`   | int    | 用户 ID（成功时返回） |
| `email` | string | 用户邮箱              |
| `user`  | string | 用户名                |



- **流程描述**：
  1. 解析 JSON 请求体
  2. 验证密码和确认密码是否一致
  3. 查询 Redis 校验验证码是否正确且未过期
  4. 查询 Redis 判断用户名是否已存在
  5. 写入 MySQL 用户信息
  6. 返回注册结果

------

### 5.2 `/user_login` — 用户登录

- **请求方式**：POST
- **请求参数**：

| 参数名   | 类型   | 描述     |
| -------- | ------ | -------- |
| `email`  | string | 用户邮箱 |
| `passwd` | string | 密码     |



- **响应字段**：

| 字段名  | 类型   | 描述                  |
| ------- | ------ | --------------------- |
| `error` | int    | 错误码（0为成功）     |
| `uid`   | int    | 用户 ID（成功时返回） |
| `email` | string | 用户邮箱              |
| `token` | string | 登录令牌              |



- **流程描述**：
  1. 解析 JSON 请求体
  2. 查询数据库校验邮箱与密码匹配
  3. 通过 `StatusServer` 查询可用的 `ChatServer`
  4. 返回登录结果

------

### 5.3 `/get_varifycode` — 请求邮箱验证码

- **请求方式**：POST
- **请求参数**：

| 参数名  | 类型   | 描述     |
| ------- | ------ | -------- |
| `email` | string | 用户邮箱 |



- **响应字段**：

| 字段名  | 类型   | 描述              |
| ------- | ------ | ----------------- |
| `error` | int    | 错误码（0为成功） |
| `email` | string | 用户邮箱          |



- **流程描述**：
  1. 解析 JSON 请求体
  2. 调用 `VarifyGrpcClient` 发送验证码
  3. 返回请求结果

------

### 5.4 `/reset_pwd` — 重置密码

- **请求方式**：POST
- **请求参数**：

| 参数名       | 类型   | 描述     |
| ------------ | ------ | -------- |
| `email`      | string | 用户邮箱 |
| `user`       | string | 用户名   |
| `passwd`     | string | 新密码   |
| `varifycode` | string | 验证码   |



- **响应字段**：

| 字段名  | 类型   | 描述              |
| ------- | ------ | ----------------- |
| `error` | int    | 错误码（0为成功） |
| `email` | string | 用户邮箱          |
| `user`  | string | 用户名            |



- **流程描述**：
  1. 解析 JSON 请求体
  2. 查询 Redis 验证验证码有效性
  3. 校验用户名与邮箱是否匹配
  4. 更新数据库密码
  5. 返回结果

------

## 6. 与其他服务的通信

| 服务         | 协议 | 功能描述                       | 主要调用类         |
| ------------ | ---- | ------------------------------ | ------------------ |
| VerifyServer | gRPC | 发送并验证邮箱验证码           | `VarifyGrpcClient` |
| StatusServer | gRPC | 获取负载最低的 ChatServer 地址 | `StatusGrpcClient` |



------

## 7.性能与并发设计

- **多线程 IO 处理**：使用 `AsioIOServicePool` 将异步事件分配到线程池，充分利用多核资源，提升请求吞吐。
- **连接池优化**：数据库和缓存使用连接池，减少连接建立开销，提高并发性能和系统稳定性。
- **异步非阻塞调用**：所有网络通信均使用异步方式，避免同步阻塞导致线程饥饿。
- **资源限制与超时机制（待优化）**：目前缺少连接数限制、请求超时和恶意连接拦截策略，需后续完善防护。