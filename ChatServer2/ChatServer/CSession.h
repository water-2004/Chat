#pragma once
#include"const.h"
#include "MsgNode.h"
#include <queue>
#include <mutex>
#include <memory>


class CServer;
class CSession : public std::enable_shared_from_this<CSession>
{
public:
	CSession(boost::asio::io_context& io_context, CServer* server);
	~CSession();
	tcp::socket& GetSocket();
	std::string& GetSessionId();
	void SetUserId(int uid);
	int GetUserId();
	void Start();
	void Send(char* msg, short max_length, short msgid);
	void Send(std::string msg, short msgid);
	void Close();
	std::shared_ptr<CSession> SharedSelf();
	void AsyncReadBody(int total_len);
	void AsyncReadHead(int total_len);
	void NotifyOffline(int  uid);
private:
	void asyncReadFull(std::size_t maxLength, std::function<void(const boost::system::error_code&, std::size_t)> handler);
	void asyncReadLen(std::size_t read_len, std::size_t total_len, std::function<void(const boost::system::error_code&, std::size_t)> handler);
	void HandleWrite(const boost::system::error_code& error, std::shared_ptr<CSession> shared_self);
	tcp::socket _socket;
	CServer* _server;
	std::string _session_id;
	bool _b_close;
	char _data[MAX_LENGTH]; //��Ϣ������ֽڴ�С
	std::queue<std::shared_ptr<SendNode> > _send_que;
	std::mutex _send_lock;
	//�յ�����Ϣ�ṹ
	std::shared_ptr<RecvNode> _recv_msg_node;
	bool _b_head_parse;
	//�յ���ͷ���ṹ
	std::shared_ptr<MsgNode> _recv_head_node;
	int _user_uid;
	//��¼�ϴν������ݵ�ʱ��
	std::atomic<time_t> _last_heartbeat;
	//session ��
	std::mutex _session_mtx;
};
class LogicNode {
	friend class LogicSystem;
public:
	LogicNode(shared_ptr<CSession>, shared_ptr<RecvNode>);
private:
	shared_ptr<CSession> _session;
	shared_ptr<RecvNode> _recvnode;
};


