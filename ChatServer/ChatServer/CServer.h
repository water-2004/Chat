#pragma once
#include <boost/asio.hpp>
#include "CSession.h"
#include <memory.h>
#include <map>
#include <mutex>
using boost::asio::ip::tcp;
class CServer
{
public:
	CServer(boost::asio::io_context& io_context, short _port);
	~CServer();
	void ClearSession(std::string);
private:
	void HandleAccept(std::shared_ptr<CSession>, const boost::system::error_code& error);
	void StartAccept();
	boost::asio::io_context& _io_context;
	short _port;
	tcp::acceptor _acceptor;
	std::map<std::string, std::shared_ptr<CSession>> _session;
	std::mutex _mutex;
};

