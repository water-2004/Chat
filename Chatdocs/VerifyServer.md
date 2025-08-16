# VerifyServer 模块

## 1. 模块职责

- 提供用户验证码生成与发送服务，用于用户注册或身份验证。
- 管理验证码缓存（Redis）及过期策略，确保验证码安全有效。
- 封装邮箱发送功能，将验证码通过邮件发送给用户。
- 对外通过 gRPC 提供统一接口，方便其他服务调用。

------

## 2. 系统架构与依赖关系

- **gRPC 服务**：使用 `@grpc/grpc-js` 提供 `VarifyService` 接口，客户端通过 RPC 调用 `GetVarifyCode` 获取验证码。
- **Redis 缓存**：使用 `ioredis` 作为缓存层，存储验证码及过期时间。
- **邮箱服务**：使用 `nodemailer` 发送邮件，SMTP 配置从 `config.js` 获取。
- **UUID 生成**：使用 `uuid` 生成唯一验证码。
- **配置管理**：通过 `config.js` 和 `config.json` 管理 Redis、邮箱及 MySQL 等配置。
- **错误码统一管理**：使用 `const.js` 定义模块内部错误码。
- **异步处理**：Redis 查询、设置与邮件发送均采用 `async/await` 异步操作。

------

## 3. 核心接口与方法

### 3.1 `GetVarifyCode(call, callback)`

- **功能**：根据用户邮箱生成验证码并发送邮件，同时缓存验证码到 Redis。
- **流程**：
  1. 查询 Redis 是否已有该邮箱的验证码。
  2. 若缓存不存在，则生成 4 位随机 UUID 作为验证码，并写入 Redis，设置过期时间 600 秒。
  3. 构造邮件内容并通过 `SendMail` 发送至用户邮箱。
  4. 通过 gRPC 回调返回操作结果及错误码。
- **异常处理**：
  - Redis 操作失败 → 返回 `Errors.RedisErr`。
  - 邮件发送或其他异常 → 返回 `Errors.Exception`。
  - 成功 → 返回 `Errors.Success`。

------

## 4. Redis 操作封装 (`redis.js`)

| 方法名                                | 功能描述                                         |
| ------------------------------------- | ------------------------------------------------ |
| `GetRedis(key)`                       | 根据 key 获取值，若不存在返回 `null`             |
| `QueryRedis(key)`                     | 检查 key 是否存在，存在返回结果，否则返回 `null` |
| `SetRedisExpire(key, value, exptime)` | 设置键值并指定过期时间（秒）                     |
| `Quit()`                              | 退出 Redis 连接                                  |



------

## 5. 邮箱发送封装 (`email.js`)

- 使用 `nodemailer` 创建 SMTP 连接池 `transport`。
- `SendMail(mailOptions)`：异步发送邮件，返回 Promise。
  - 成功 → resolve 邮件发送响应
  - 失败 → reject 异常信息

------

## 6. 配置管理 (`config.js` / `config.json`)

- 管理 Redis、MySQL、邮箱配置信息及验证码前缀。

- 示例：

  ```
  js
  
  
  复制编辑
  let code_prefix = "code_"; // Redis 存储验证码的键前缀
  ```

------

## 7. 错误码 (`const.js`)

| 错误码        | 含义           |
| ------------- | -------------- |
| `Success`=0   | 成功           |
| `RedisErr`=1  | Redis 操作失败 |
| `Exception`=2 | 其他异常       |



------

## 8. 项目依赖 (`package.json`)

- `@grpc/grpc-js`：提供 gRPC 服务
- `ioredis` / `redis`：Redis 客户端
- `nodemailer`：发送邮件
- `uuid`：生成随机验证码
- Node.js 异步处理确保高并发性能

------

## 9. 设计亮点

1. **缓存优先策略**：Redis 中优先查询验证码，减少重复生成。
2. **验证码安全性**：随机 UUID 生成验证码，并设置 10 分钟有效期。
3. **模块解耦**：Redis、邮件发送、配置管理、错误码独立封装，逻辑清晰。
4. **异步处理**：保证 Redis 操作和邮件发送在高并发下性能稳定。
5. **统一 gRPC 接口**：通过 `VarifyService` 对外提供服务，便于 ChatServer、GateServer 等模块集成。
6. **易扩展**：邮件模板、验证码规则和缓存策略可独立修改，不影响核心逻辑。