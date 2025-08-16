# Chatserver 模块技术文档

------

## 1. 模块概述

### 1.1 模块职责

- 本系统主要提供即时聊天相关功能，核心职责如下：

  1. **聊天服务**
     - 支持用户之间的文本消息传输，包括在线消息推送和跨服消息转发。
  2. **好友申请服务**
     - 处理用户发起的好友申请请求，并根据目标用户所在服务器选择本地或跨服通知。
  3. **好友认证服务**
     - 处理好友申请的确认操作，更新好友关系并推送认证结果至目标用户，支持本地及跨服通知。

  > 注：各模块职责清晰划分，业务逻辑层负责处理请求和响应，数据访问层负责缓存与持久化操作，跨服通信层负责不同服务器之间的 RPC 调用。

### 1.2 系统架构与依赖关系

- 系统采用分层架构设计，核心依赖和组件如下：

  1. **网络与异步处理**
     - 基于 `Boost.Asio` 提供异步 IO 支持，使用 `Boost.Beast` 实现 HTTP 服务监听与请求处理。
     - 通过线程池（`AsioIOServicePool`）管理 IO 任务，提高并发处理能力。
  2. **数据缓存与持久化**
     - **缓存层**：使用 `Redis` 存储用户基础信息、Token 和验证码等，缓存访问由 `RedisMgr` 管理。
     - **数据库层**：采用 MySQL 作为持久化存储，数据库连接由 `MySqlPool` 管理，封装为 `MysqlMgr` 提供上层调用接口，具体 SQL 操作由 `MysqlDao` 实现。
     - **缓存策略**：采用 Cache-Aside（先查 Redis，未命中回退至 MySQL 并更新缓存）以提高访问效率。
  3. **跨服务依赖**
     - **VerifyServer**：提供验证码校验服务，通过 gRPC 通信调用。
     - **StatusServer**：负责获取负载最低的 `ChatServer` 节点，通过 gRPC 通信进行请求调度。
  4. **业务逻辑层**
     - 包含聊天服务、好友申请服务和好友认证服务，实现请求解析、身份校验、数据访问、响应封装及跨服消息推送。

  > 总体架构体现了**业务层、数据层、跨服通信层、网络层**的分离，保证系统可扩展性、可维护性及高并发性能。

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
4. 启动TCP聊天服务
5. 初始化redis登录计数
6. 创建`ChatServiceImpl`和`grpc::ServerBuilder`启动grpc服务

示例代码：

```c++
int main()
{
	auto& cfg = ConfigMgr::Inst();
	auto server_name = cfg["SelfServer"]["Name"];
	try
	{
		auto pool = AsioIOServicePool::GetInstance();
		//将登录数设置为0
		RedisMgr::GetInstance()->HSet(LOGIN_COUNT, server_name, "0");
		//定义一个GrpcServer
		std::string server_address(cfg["SelfServer"]["Host"] + ":" + cfg["SelfServer"]["RPCPort"]);
		ChatServiceImpl service;
		grpc::ServerBuilder builder;
		// 监听端口和添加服务
		builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
		builder.RegisterService(&service);
		// 构建并启动gRPC服务器
		std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
		std::cout << "RPC Server listening on " << server_address << std::endl;

		//单独启动一个线程处理grpc服务
		std::thread  grpc_server_thread([&server]() {
			server->Wait();
			});

		boost::asio::io_context  io_context;
		boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
		signals.async_wait([&io_context, pool, &server](auto, auto) {
			io_context.stop();
			pool->Stop();
			server->Shutdown();
			});
		auto port_str = cfg["SelfServer"]["Port"];
		CServer s(io_context, atoi(port_str.c_str()));
		io_context.run();
	}
	catch (const std::exception& e)
	{
		std::cerr << "Exception: " << e.what() << std::endl;
	}
}

```

------

## 3. 核心类设计

### 3.1 CServer

- **职责**：封装服务器监听器逻辑，负责接收客户端 TCP 连接
- **实现**：
  - 获取 IO 事件循环
  - 创建会话对象
  - 异步等待客户端连接，并且回调函数`HandleAccept()`再次调用`StartAccept()`实现循环监听
- **示例代码（TCP 接入机制）**：

```c++
void CServer::StartAccept()
{
	auto& io_context = AsioIOServicePool::GetInstance()->GetIOService();
	std::shared_ptr<CSession> new_session = std::make_shared<CSession>(io_context, this);
	_acceptor.async_accept(new_session->GetSocket(), std::bind(&CServer::HandleAccept, this, new_session, placeholders::_1));
}
```

- **设计要点**：

  - 获取 IO 事件循环,通过 `AsioIOServicePool` 获取一个可用的 `boost::asio::io_context` 实例。

  - 创建会话对象，`CSession` 封装了单个客户端连接的生命周期管理与数据收发逻辑。传入 `io_context`，确保该连接的读写操作绑定在特定的 IO 线程。使用 `std::shared_ptr` 自动管理对象生命周期，连接断开后自动释放资源。

  - 异步等待客户端连接`_acceptor`（`boost::asio::ip::tcp::acceptor`）负责监听端口。`async_accept()` 以异步方式等待新连接到来。连接建立后：`new_session` 的 Socket 会绑定到客户端连接。回调 `HandleAccept()` 进行后续处理（如启动异步读写、注册到连接管理器等）。

  - **循环接收模式**

    `HandleAccept()` 在处理当前连接后，会再次调用 `async_accept()`，确保服务器持续接入新连接。这种递归调用模式是异步服务器的常用设计，保证高并发场景下的连接接入能力。

------

### 3.2 Csession

- **职责**：管理每个TCP请求连接，异步读写请求和响应

  #### 核心方法说明

  ##### 1. `Start()`

  - 作用：开启异步读取流程，首先读取固定长度的消息头。

  - 实现：

    ```c++
    void CSession::Start()
    {
        AsyncReadHead(HEAD_TOTAL_LEN);
    }
    ```

  - 说明：调用 `AsyncReadHead` 从socket异步读取消息头数据。

  ------

  ##### 2. `AsyncReadHead(int total_len)`

  - 作用：异步读取消息头 `total_len` 字节，读取完成后解析消息ID和消息体长度，启动消息体读取。

  - 核心流程：

    - 调用 `asyncReadFull` 读取完整的消息头数据。
    - 检查读取错误和数据长度合法性。
    - 解析消息ID和消息体长度（含网络字节序转主机字节序）。
    - 校验消息ID和长度是否合理，防止恶意数据。
    - 创建消息体缓存 `_recv_msg_node`。
    - 调用 `AsyncReadBody(msg_len)` 读取消息体。

  - 代码示例：

    ```c++
    void CSession::AsyncReadHead(int total_len)
    {
        auto self = shared_from_this();
        asyncReadFull(total_len, [self, this](const boost::system::error_code& ec, std::size_t bytes_transferred) {
            if (ec || bytes_transferred < total_len) {
                // 错误处理
                Close();
                _server->ClearSession(_session_id);
                return;
            }
            // 拷贝消息头数据
            memcpy(_recv_head_node->_data, _data, bytes_transferred);
    
            // 解析消息ID与消息长度
            short msg_id = 0, msg_len = 0;
            memcpy(&msg_id, _recv_head_node->_data, HEAD_ID_LEN);
            msg_id = boost::asio::detail::socket_ops::network_to_host_short(msg_id);
            memcpy(&msg_len, _recv_head_node->_data + HEAD_ID_LEN, HEAD_DATA_LEN);
            msg_len = boost::asio::detail::socket_ops::network_to_host_short(msg_len);
    
            // 校验合法性
            if (msg_id > MAX_LENGTH || msg_len > MAX_LENGTH) {
                _server->ClearSession(_session_id);
                return;
            }
    
            // 初始化消息体缓存
            _recv_msg_node = std::make_shared<RecvNode>(msg_len, msg_id);
    
            // 读取消息体
            AsyncReadBody(msg_len);
        });
    }
    ```

  ------

  ##### 3. `asyncReadFull(size_t maxLength, handler)`

  - 作用：确保异步读取满 `maxLength` 字节数据，调用递归方法 `asyncReadLen`。

  - 说明：先清空读取缓存 `_data`，再调用递归读取接口。

  - 代码示例：

    ```c++
    void CSession::asyncReadFull(std::size_t maxLength, std::function<void(const boost::system::error_code&, std::size_t)> handler)
    {
        memset(_data, 0, MAX_LENGTH);
        asyncReadLen(0, maxLength, handler);
    }
    ```

  ------

  ##### 4. `asyncReadLen(size_t read_len, size_t total_len, handler)`

  - 作用：递归异步读取数据，直到累计读取字节达到 `total_len`。

  - 实现说明：

    - 每次调用 `async_read_some` 读取数据片段。
    - 如果出错或已读取足够字节，则触发回调。
    - 否则递归调用自身继续读取剩余字节。

  - 代码示例：

    ```c++
    void CSession::asyncReadLen(std::size_t read_len, std::size_t total_len,
        std::function<void(const boost::system::error_code&, std::size_t)> handler)
    {
        auto self = shared_from_this();
        _socket.async_read_some(boost::asio::buffer(_data + read_len, total_len - read_len),
            [read_len, total_len, handler, self](const boost::system::error_code& ec, std::size_t bytesTransferred) {
                if (ec || read_len + bytesTransferred >= total_len) {
                    handler(ec, read_len + bytesTransferred);
                    return;
                }
                self->asyncReadLen(read_len + bytesTransferred, total_len, handler);
            });
    }
    ```

  ------

  ##### 5.`AsyncReadBody(int total_len)`

  - 作用：异步读取消息体内容，将消息投递到`LogicSystem`类的逻辑队列中并调用`AsyncReadHead`继续监听头部消息。
  - 流程：
    - 调用 `asyncReadFull` 读取完整的消息体数据。
    - 检查读取错误和数据长度合法性。
    - 将消息体的内容复制给`_recv_msg_node->_data`
    - 将消息投递到`LogicSystem`类的逻辑队列中并调用`AsyncReadHead`继续监听头部消息。

  - 代码示例：

    ```c++
    void CSession::AsyncReadBody(int total_len)
    {
    	auto self = shared_from_this();
    	asyncReadFull(total_len, [self,this,total_len](const boost::system::error_code& ec, std::size_t bytes_transfered) {
    		try
    		{
    			if (ec)
    			{
    				std::cout << "handle read failed, error is " << ec.what() << endl;
    				Close();
    				_server->ClearSession(_session_id);
    				return;
    			}
    			if (bytes_transfered<total_len)
    			{
    				std::cout << "read length not match, read [" << bytes_transfered << "] , total ["
    					<< total_len << "]" << endl;
    				Close();
    				_server->ClearSession(_session_id);
    				return;
    			}
    			memcpy(_recv_msg_node->_data, _data, bytes_transfered);
    			_recv_msg_node->_cur_len += bytes_transfered;
    			_recv_msg_node->_data[_recv_msg_node->_total_len] = '\0';
    			std::cout << "receive data is " << _recv_msg_node->_data << endl;
    			//此处将消息投递到逻辑队列中
    			LogicSystem::GetInstance()->PostMsgToQue(make_shared<LogicNode>(shared_from_this(), _recv_msg_node));
    			//继续监听头部接受事件
    			AsyncReadHead(HEAD_TOTAL_LEN);
    		}
    		catch (const std::exception& e)
    		{
    			std::cout << "Exception code is " << e.what() << endl;
    		}
    		});
    }
    ```

  ------

  ##### 6.`Send(char* msg, short max_length, short msgid)和Send(std::string msg, short msgid)`

  - 作用: 发送消息给客户端或者其他服务

  - 流程：

    - 先用互斥锁 `_send_lock` 保证线程安全，避免多线程同时修改发送队列。

    - 检查当前发送队列大小，避免队列爆满（超过 `MAX_SENDQUE`），防止资源耗尽。

    - 把要发送的消息封装成 `SendNode`（消息节点）放入发送队列 `_send_que`。

    - 如果当前队列只有这条消息，则立即启动异步写操作。

    - 如果队列已有待发送消息，则不立刻发送，等待之前的消息发送完成后自动触发。

  - 代码示例：

    ```c++
    void CSession::Send(char* msg, short max_length, short msgid)
    {
    	std::lock_guard<std::mutex> lock(_send_lock);
    	int send_que_size = _send_que.size();
    	if (send_que_size > MAX_SENDQUE) {
    		std::cout << "session: " << _session_id << " send que fulled ,size is " << MAX_SENDQUE << std::endl;
    		return;
    	}
    	_send_que.push(make_shared<SendNode>(msg, max_length, msgid));
    	if (send_que_size > 0) {
    		return;
    	}
    	auto& msgnode = _send_que.front();
    	boost::asio::async_write(_socket, boost::asio::buffer(msgnode->_data, msgnode->_total_len),
    		std::bind(&CSession::HandleWrite, this, std::placeholders::_1, SharedSelf()));
    }
    ```

  ------

  ##### 7.`HandleWrite(const boost::system::error_code& error, std::shared_ptr<CSession> shared_self)`

  - 作用：将消息队列中的内容发送到客户端或者其他服务中

  - 流程:

    - 判断是否出错
    - 用互斥锁保证线程安全
    - 取出发送队列的首位，调用boost::asio::async_write进行发送信息，回调函数为自身

  - 代码示例：

    ```c++
    void CSession::HandleWrite(const boost::system::error_code& error, std::shared_ptr<CSession> shared_self)
    {
    	try
    	{
    		if (!error) {
    			std::lock_guard<std::mutex> lock(_send_lock);
    			_send_que.pop();
    			if (!_send_que.empty()) {
    				auto& msgnode = _send_que.front();
    				boost::asio::async_write(_socket, boost::asio::buffer(msgnode->_data, msgnode->_total_len),
    					std::bind(&CSession::HandleWrite, this, std::placeholders::_1, shared_self));
    			}
    			std::cout << "回包发送完成" << std::endl;
    		}
    		else {
    			std::cout << "handle write failed, error is " << error.what() << endl;
    			Close();
    			_server->ClearSession(_session_id);
    		}
    	}
    	catch (const std::exception& e)
    	{
    		std::cerr << "Exception code : " << e.what() << endl;
    	}
    }
    ```

  ------

  ## 设计意义与优势

  - **完整消息保证**：通过递归异步读取确保消息头和消息体完整接收，避免半包问题。
  - **异步高性能**：基于 Boost.Asio 异步IO模型，非阻塞高效，支持高并发连接。
  - **异常安全**：错误即关闭连接并清理会话，防止脏数据和资源泄漏。
  - **灵活扩展**：通过消息ID和长度控制，可支持多种消息格式和协议演进。

### 3.3 LogicSystem

- **职责**：逻辑调度中心，管理接口路由与业务处理
- **核心数据**：

```c++
std::map<short, FunCallBack> _fun_callback;
std::queue<shared_ptr<LogicNode>> _msg_que;
```

`_fun_callback`存储对应请求的回调函数
`_msg_que`消息队列，保证消息的有序性

#### 核心方法说明

##### 1.void RegisterCallBacks();

- 将请求所对应的各个回调函数存储到`_fun_callback`中，方便调用

##### 2.void DealMsg()

- 运行在独立工作线程中，负责从逻辑消息队列中持续取出并分发消息，核心流程如下：
  1. **加锁保护**
     - 通过 `std::unique_lock<std::mutex>` 获取 `_mutex`，确保多线程环境下对 `_msg_que` 的安全访问。
  2. **阻塞等待**
     - 当消息队列为空且未收到停止信号（`_b_stop == false`）时，使用条件变量 `_consume.wait(unique_lk)` 阻塞当前线程，并释放锁，避免 CPU 空转。
  3. **停止逻辑处理**
     - 当 `_b_stop == true` 时，进入清理模式：依次取出队列中的剩余消息并调用相应回调函数，处理完毕后跳出循环，线程结束。
  4. **正常消息分发**
     -  从队列头部取出一条消息，解析消息 ID（`_recvnode->_msg_id`），在 `_fun_callback` 映射表中查找对应的回调处理函数：
       - **若未找到**：打印警告日志并丢弃该消息。
       - **若找到**：执行回调，将消息内容传入业务逻辑进行处理。
     - 处理完成后从队列中移除该消息，继续下一轮循环。
  5. **并发模型**
     - 该方法与 `PostMsgToQue()` 方法配合，形成典型的**生产者-消费者模型**：网络线程作为生产者向 `_msg_que` 投递消息，逻辑线程作为消费者按顺序处理消息，避免在 IO 线程中直接执行耗时逻辑。

##### 3.bool GetBaseInfo(std::string base_key, int uid, std::shared_ptr<UserInfo>& userinfo)

- 判断是否有该用户，核心流程如下
  1. 优先查询redis中的用户信息
  2. redis中没有就查询mysql

##### 4.void LoginHandler(shared_ptr<CSession> session, const short& msg_id, const string& msg_data)

- 处理客户端的登录请求，根据 UID 和 Token 校验身份，并返回用户的基础信息、好友列表、申请列表等数据，同时更新 Redis 状态并绑定会话信息。下面是处理流程：

1. **解析客户端请求**
   - 使用 `Json::Reader` 解析 `msg_data`，提取 `uid` 和 `token`。
   - 定义 `Defer` 回调，在函数结束时统一构造并发送登录响应消息（`MSG_CHAT_LOGIN_RSP`）给客户端。
2. **身份校验（Redis）**
   - 根据 UID 拼接 Redis Token Key（`USERTOKENPREFIX + uid`）。
   - 从 Redis 获取存储的 Token 并校验：
     - **不存在** → 返回 `ErrorCodes::UidInvalid`。
     - **不匹配** → 返回 `ErrorCodes::TokenInvalid`。
   - 校验通过后设置 `ErrorCodes::Success`。
3. **加载用户基础信息**
   - 从 Redis（`USER_BASE_INFO + uid`）加载用户基础信息（昵称、性别、头像等）。
   - 若数据缺失 → 返回 `ErrorCodes::UidInvalid`。
4. **加载社交数据**
   - **好友申请列表**：调用 `GetFriendApplyInfo()` 查询，结果写入 `rtvalue["apply_list"]`。
   - **好友列表**：调用 `GetFriendList()` 查询，结果写入 `rtvalue["friend_list"]`。
5. **更新登录统计（Redis）**
   - 从 `LOGIN_COUNT` 哈希表中获取当前服务器的登录人数，计数 +1 后写回。
6. **会话绑定与管理**
   - 调用 `session->SetUserId(uid)` 绑定 UID。
   - 将 UID 对应的登录服务器信息存储到 Redis（`USERIPPREFIX + uid`）。
   - 将 UID 和当前会话对象绑定到 `UserMgr`，方便后续踢人或消息推送。
   - 将 UID 与 SessionId 的映射写入 Redis（`USER_SESSION_PREFIX + uid`）。

**关键点说明**：

- 使用 `Defer` 机制，确保无论函数中途何处 `return`，都会统一发送响应给客户端，避免遗漏。
- 所有 Redis 交互均通过 `RedisMgr::GetInstance()` 封装，保证线程安全和连接复用。
- 好友列表、申请列表等数据以 JSON 数组形式返回，便于客户端直接解析渲染。
- 在登录流程中更新 Redis 的登录统计，有助于实现分布式多节点的实时监控。

##### 5.void SearchInfo(shared_ptr<CSession> session, const short& msg_id, const string& msg_data)

- 处理客户端的用户搜索请求，根据输入（UID 或昵称）查询用户信息，并返回结果。优先从 Redis 缓存读取，缓存未命中时回退到 MySQL 查询并更新缓存。

  1. **解析请求参数**
     - 使用 `Json::Reader` 解析 `msg_data`，获取 `uid_str`（可能是纯数字 UID 或用户名字符串）。
     - 定义 `Defer` 回调，确保函数结束时统一发送响应消息 `ID_SEARCH_USER_RSP` 给客户端。
  2. **判断查询方式**
     - 调用 `isPureDigit(uid_str)` 判断是否为纯数字：
       - **纯数字** → 调用 `GetUserByUid(uid_str, rtvalue)`。
       - **非纯数字** → 调用 `GetUserByName(uid_str, rtvalue)`。
  3. **用户信息查询逻辑**（以 `GetUserByUid` 为例）
     - 构造 Redis Key（`USER_BASE_INFO + uid_str`）。
     - **缓存优先策略**：
       - 若 Redis 中存在 → 直接解析 JSON 并写入 `rtvalue` 返回。
       - 若 Redis 中不存在 → 调用 `MysqlMgr::GetInstance()->GetUser(uid)` 从 MySQL 获取数据。
     - **缓存更新**：
       - 当 MySQL 查询成功时，将用户数据以 JSON 格式写入 Redis 缓存，保证后续访问的性能。
     - 若 MySQL 查询失败 → 返回 `ErrorCodes::UidInvalid`。

  **关键点说明**：

  - 采用**缓存优先**（Cache-Aside）策略，减少数据库访问压力。
  - 使用 `Defer` 确保即使中途 return，也能向客户端发送响应，避免消息丢失。
  - 结构设计上将按 UID 和按 Name 查询分离成两个函数，便于扩展和维护。
  - 查询结果包含用户的所有基础信息字段（UID、密码、昵称、性别、头像等），便于前端直接渲染。

##### 6.AddFriendApply(shared_ptr<CSession> session, const short& msg_id, const string& msg_data)

- 处理客户端“添加好友申请”请求，根据申请人 `uid` 与目标用户 `touid` 建立好友申请关系，并根据目标用户所在的服务器位置选择本地通知或跨服通知。具体流程如下：

  1. **解析请求参数**

     - 使用 `Json::Reader` 解析 `msg_data`，提取：
       - `uid`：申请人用户 ID
       - `applyname`：申请人名称
       - `bakname`：申请时的备注名
       - `touid`：被申请人用户 ID
     - 定义 `Defer` 回调，保证函数结束时统一向客户端返回结果（消息类型 `ID_ADD_FRIEND_RSP`）。

  2. **在 MySQL 中记录好友申请**

     - 调用 `MysqlMgr::GetInstance()->AddFriendApply(uid, touid)`，向数据库插入好友申请记录（使用 `ON DUPLICATE KEY UPDATE` 保证幂等性，避免重复申请时插入失败）。

  3. **定位目标用户所在服务器**

     - 从 Redis 查询 `touid` 对应的服务器名称（键 `USERIPPREFIX + touid`）。

     - 若查询失败（目标用户不在线或无记录），直接返回。

  4. **获取申请人基础信息**

     - 从 Redis（或后备数据源）读取 `uid` 对应的用户基本信息，用于后续通知消息中的头像、性别、昵称等字段填充。

  5. **发送好友申请通知**

     - **本服用户**：如果 `touid` 所在服务器与当前服务器相同，则直接从内存会话管理中获取对方 `session`，实时推送 `ID_NOTIFY_ADD_FRIEND_REQ` 消息。
     - **跨服用户**：如果 `touid` 所在服务器不同，则构造 `AddFriendReq` 请求，通过 gRPC 调用 `ChatGrpcClient::GetInstance()->NotifyAddFriend()` 将申请消息发送到目标服务器。

##### 7.AuthFriendApply(std::shared_ptr<CSession> session, const short& msg_id, const string& msg_data)

- 处理客户端“好友申请认证”请求，在申请人同意好友请求后，更新数据库好友关系，并根据好友所在服务器位置，选择本地通知或跨服通知。具体流程如下：

  1. **解析请求参数**
     使用 `Json::Reader` 解析 `msg_data`，提取：

     - `uid` (`fromuid`)：申请发起方用户 ID
     - `touid`：被申请方用户 ID
     - `back_name` (`back`)：备注名

     定义 `Defer` 回调，保证函数结束时统一向客户端返回认证结果（消息类型 `ID_AUTH_FRIEND_RSP`）。

  2. **获取被申请方基础信息**

     - 从 Redis（键 `USER_BASE_INFO + touid`）获取被申请方的基础信息（昵称、头像、性别等）。
     - 如果获取失败，则将返回值 `error` 设置为 `ErrorCodes::UidInvalid`。

  3. **更新数据库好友关系**

     - 调用 `MysqlMgr::GetInstance()->ConfirmFriendApply(uid, touid, back_name)`，在 MySQL 中确认好友申请并建立好友关系。

  4. **定位目标用户所在服务器**

     - 从 Redis 查询 `touid` 对应的服务器名称（键 `USERIPPREFIX + touid`）。
     - 若查询失败（目标用户不在线或无记录），直接返回。

  5. **发送好友认证通过通知**

     - **本服用户**：如果 `touid` 所在服务器与当前服务器相同，则从内存会话管理中获取对方 `session`，构造 `ID_NOTIFY_AUTH_FRIEND_REQ` 消息（附带 `fromuid` 基础信息），直接推送给对方。
     - **跨服用户**：如果 `touid` 所在服务器不同，则构造 `AuthFriendReq` 请求，通过 gRPC 调用 `ChatGrpcClient::GetInstance()->NotifyAuthFriend()` 将通知发送到目标服务器。

### 3.4 ChatGrpcClient

- 职责：跨服务器 RPC 通信客户端管理类，用于分布式聊天系统中不同服务器之间的消息传递（好友申请、好友认证、文本消息等）。
   它内部维护了一组 **gRPC 连接池** `_pools`，按目标服务器名分配，每个连接池中保存多个 `Stub`（客户端连接实例），实现跨服调用的复用与性能优化。

- ## 核心成员

  - **`_pools`**：
     `std::unordered_map<std::string, std::unique_ptr<ChatConPool>>`
    - key：目标服务器的名称（配置文件中的 `"Name"`）
    - value：对应服务器的连接池（`ChatConPool`），内部维护多个 `Stub` 用于 RPC 调用
    - 作用：避免频繁建立 gRPC 连接，提高跨服通信效率

#### 核心方法解析

##### 1. 构造函数 `ChatGrpcClient()`

- 从配置文件 `ConfigMgr` 中读取 `PeerServer.Servers` 列表（逗号分隔）
- 解析出服务器标识（`word`），检查配置中是否有对应的 `"Name"`
- 为每个服务器创建一个连接池 `ChatConPool(5, host, port)`，初始连接数为 5
- 存入 `_pools` 中
   **意义**：初始化时就建立好连接池，后续 RPC 调用可直接取连接

------

##### 2. `NotifyAddFriend(std::string server_ip, const AddFriendReq& req)`

- 用于跨服通知 **好友申请**
- 步骤：
  1. 默认构造 `AddFriendRsp`，用 `Defer` 在函数结束时补全 `applyuid`、`touid`
  2. 在 `_pools` 中查找 `server_ip` 对应的连接池
  3. 从连接池取 `Stub`，调用远程服务 `NotifyAddFriend`
  4. 用 `Defer` 保证 RPC 结束后 `Stub` 归还到连接池
  5. 如果 RPC 调用失败，设置 `ErrorCodes::RPCFailed`

------

##### 3. `NotifyAuthFriend(std::string server_ip, const AuthFriendReq& req)`

- 用于跨服通知 **好友认证通过**
- 与 `NotifyAddFriend` 逻辑类似，只是调用远程的 `NotifyAuthFriend` 方法
- 结束时将 `fromuid`、`touid` 填充到响应中

------

##### 4. `GetBaseInfo(std::string base_key, int uid, std::shared_ptr<UserInfo>& userinfo)`

- 获取用户基础信息（优先 Redis，备用 MySQL）
- 步骤：
  1. 从 Redis 查 `base_key`，如果有则解析 JSON 填充 `userinfo`
  2. 如果 Redis 没有，则调用 `MysqlMgr::GetUser(uid)` 从 MySQL 读取
  3. 读取成功后，将结果写入 Redis 缓存
- **意义**：保证跨服调用时可以快速拿到用户信息，减轻数据库压力

------

##### 5. `NotifyTextChatMsg(std::string server_ip, const TextChatMsgReq& req, const Json::Value& rtvalue)`

- 跨服通知 **文本聊天消息**
- 步骤：
  1. 默认构造 `TextChatMsgRsp` 并设置 `error` 为成功
  2. 用 `Defer` 在函数结束时将 `fromuid`、`touid` 及消息内容填充到响应中
  3. 查找 `_pools` 中目标服务器连接池，取出 `Stub`
  4. 调用 `NotifyTextChatMsg` 远程服务
  5. 结束后将 `Stub` 归还到连接池
  6. 如果调用失败，设置 `ErrorCodes::RPCFailed`

------

#### 设计亮点

1. **连接池复用**
   - 通过 `ChatConPool` 维护多个 Stub，减少频繁的 gRPC 建连开销
2. **统一的跨服通信接口**
   - 所有跨服操作（好友申请、认证、消息）都走这个类，逻辑集中、易维护
3. **缓存优先**
   - `GetBaseInfo` 优先 Redis，减少数据库压力
4. **RAII 资源管理**
   - `Defer` 用于保证响应填充、连接归还，减少遗漏和资源泄漏风险

### 3.5 ChatServiceImpl

**职责**：聊天服务端 gRPC 接口的实现类，负责处理来自其他服务器（或本地服务）的聊天相关 RPC 请求，包括好友申请通知、好友认证通知、文本消息推送等。该类直接与用户会话管理模块 (`UserMgr`)、缓存层 (`RedisMgr`)、数据库层 (`MysqlMgr`) 交互，完成即时消息和用户信息的跨模块传递

#### 核心成员

> 本类主要是 gRPC 服务的实现类，没有额外的大量成员变量，核心依赖通过调用单例管理类获取：

- **`UserMgr::GetInstance()`**
  - 用于获取目标用户的会话 `Session`，判断其是否在线。
- **`RedisMgr::GetInstance()`**
  - 用于缓存和读取用户基础信息，减少数据库访问。
- **`MysqlMgr::GetInstance()`**
  - 当 Redis 中无数据时，访问 MySQL 获取用户基础信息。

#### 核心方法解析

##### 1. `NotifyAddFriend(ServerContext* context, const AddFriendReq* request, AddFriendRsp* reply)`

- 处理好友申请通知的 RPC 调用
- 步骤：
  1. 从请求中获取 `touid`，在 `UserMgr` 中查找是否有在线会话
  2. 如果用户不在线，直接返回成功状态（无需发送实时消息）
  3. 若在线，构造 JSON 消息，包含 `applyuid`、申请人信息（昵称、头像、性别等）
  4. 调用 `session->Send()` 将通知推送给目标用户客户端
  5. 通过 `Defer` 确保 `reply` 中基础信息正确填充

##### 2. `NotifyAuthFriend(ServerContext* context, const AuthFriendReq* request, AuthFriendRsp* reply)`

- 处理好友认证通过通知的 RPC 调用
- 步骤：
  1. 获取 `touid`，在 `UserMgr` 查找在线会话
  2. 若用户不在线，直接返回
  3. 若在线，先调用 `GetBaseInfo()` 获取 `fromuid` 的基础信息（优先 Redis，备用 MySQL）
  4. 构造 JSON 消息，填充好友信息（昵称、头像等）并推送给对方
  5. 如果基础信息获取失败，设置错误码为 `ErrorCodes::UidInvalid`

##### 3. `NotifyTextChatMsg(ServerContext* context, const TextChatMsgReq* request, TextChatMsgRsp* reply)`

- 推送文本聊天消息
- 步骤：
  1. 从 `touid` 查找会话，若不在线直接返回
  2. 若在线，将 `fromuid`、`touid` 以及消息内容组织成 JSON，其中 `textmsgs` 会被组织成 JSON 数组
  3. 调用 `session->Send()` 将消息推送给目标用户客户端

##### 4. `GetBaseInfo(std::string base_key, int uid, std::shared_ptr<UserInfo>& userinfo)`

- 获取用户基础信息（**缓存优先**策略）
- 步骤：
  1. 从 Redis 获取 `base_key` 对应的 JSON，若存在则直接解析到 `userinfo`
  2. 若不存在，调用 MySQL 接口 `GetUser(uid)` 获取数据
  3. 成功获取后，将用户基础信息写入 Redis 缓存
- **意义**：减少数据库访问，提升跨模块调用效率

#### 设计亮点

1. **在线检测 + 条件推送**
   - 所有通知方法都会先判断用户是否在线，避免无效的网络发送
2. **缓存优先策略**
   - 先查 Redis，未命中再查 MySQL，保证性能与一致性平衡
3. **模块解耦**
   - 通过 `UserMgr`、`RedisMgr`、`MysqlMgr` 访问会话与数据，逻辑清晰，依赖集中
4. **JSON 统一封装**
   - 所有跨模块/跨网络的消息都序列化为 JSON，便于调试与扩展
5. **RAII + Defer 资源管理**
   - 确保 RPC 回复字段和资源释放不被遗漏

------

## 4.宏定义

### 1. 错误码

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
| `TokenInvalid` = 1010 | Token失效     |
| `UidInvalid` = 1011   | uid无效       |

### 2.协议消息 ID

| 消息 ID 宏定义                | 数值 | 含义说明                 | 请求/回复/通知 |
| ----------------------------- | ---- | ------------------------ | -------------- |
| `MSG_CHAT_LOGIN`              | 1005 | 用户登录请求             | 请求           |
| `MSG_CHAT_LOGIN_RSP`          | 1006 | 用户登录回包             | 回复           |
| `ID_SEARCH_USER_REQ`          | 1007 | 搜索用户请求             | 请求           |
| `ID_SEARCH_USER_RSP`          | 1008 | 搜索用户回包             | 回复           |
| `ID_ADD_FRIEND_REQ`           | 1009 | 申请添加好友             | 请求           |
| `ID_ADD_FRIEND_RSP`           | 1010 | 添加好友结果             | 回复           |
| `ID_NOTIFY_ADD_FRIEND_REQ`    | 1011 | 通知用户有好友申请       | 通知           |
| `ID_AUTH_FRIEND_REQ`          | 1013 | 好友认证请求             | 请求           |
| `ID_AUTH_FRIEND_RSP`          | 1014 | 好友认证回复             | 回复           |
| `ID_NOTIFY_AUTH_FRIEND_REQ`   | 1015 | 通知用户好友认证申请     | 通知           |
| `ID_TEXT_CHAT_MSG_REQ`        | 1017 | 文本聊天消息             | 请求           |
| `ID_TEXT_CHAT_MSG_RSP`        | 1018 | 文本聊天消息回执         | 回复           |
| `ID_NOTIFY_TEXT_CHAT_MSG_REQ` | 1019 | 通知用户收到文本聊天消息 | 通知           |
| `ID_NOTIFY_OFF_LINE_REQ`      | 1021 | 通知用户下线             | 通知           |
| `ID_HEART_BEAT_REQ`           | 1023 | 心跳检测                 | 请求           |
| `ID_HEARTBEAT_RSP`            | 1024 | 心跳检测回包             | 回复           |

### 3.Redis Key 的前缀（固定键名）定义

| 宏名                  | 值             | 含义             | 使用场景                                  |
| --------------------- | -------------- | ---------------- | ----------------------------------------- |
| `USERIPPREFIX`        | `"uip_"`       | 用户 IP 记录前缀 | 用于存储用户 IP 信息，如限制登录、IP 统计 |
| `USERTOKENPREFIX`     | `"utoken_"`    | 用户 Token 前缀  | 存储用户登录的 Token                      |
| `IPCOUNTPREFIX`       | `"ipcount_"`   | IP 登录次数前缀  | 用于统计某个 IP 的登录次数                |
| `USER_BASE_INFO`      | `"ubaseinfo_"` | 用户基础信息前缀 | 存储用户昵称、头像、性别等基础信息        |
| `LOGIN_COUNT`         | `"logincount"` | 登录总次数键名   | 存储全局登录次数                          |
| `NAME_INFO`           | `"nameinfo_"`  | 用户名信息前缀   | 存储用户名相关信息（用于搜索或展示）      |
| `LOCK_PREFIX`         | `"lock_"`      | 分布式锁前缀     | 用于 Redis 分布式锁功能，防止并发冲突     |
| `USER_SESSION_PREFIX` | `"usession_"`  | 用户会话信息前缀 | 存储用户当前会话信息                      |
| `LOCK_COUNT`          | `"lockcount"`  | 锁计数键名       | 存储锁使用次数（可能用于统计或限流）      |

------

## 5.性能与并发设计

- **多线程 IO 处理**：使用 `AsioIOServicePool` 将异步事件分配到线程池，充分利用多核资源，提升请求吞吐。
- **连接池优化**：数据库和缓存使用连接池，减少连接建立开销，提高并发性能和系统稳定性。
- **异步非阻塞调用**：所有网络通信均使用异步方式，避免同步阻塞导致线程饥饿。
- **资源限制与超时机制（待优化）**：目前缺少连接数限制、请求超时和恶意连接拦截策略，需后续完善防护。