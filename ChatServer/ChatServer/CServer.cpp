#include "CServer.h"
#include <iostream>
#include"AsioIOServicePool.h"
#include "UserMgr.h"
CServer::CServer(boost::asio::io_context& io_context, short port)
	:_io_context(io_context), _port(port), _acceptor(io_context, tcp::endpoint(tcp::v4(), port))
{
	std::cout << "Server start success, listen on port : " << _port << std::endl;
	StartAccept();
}

CServer::~CServer()
{
	cout << "Server destruct listen on port : " << _port << endl;
}

void CServer::ClearSession(std::string session_id)
{
	lock_guard<mutex> lock(_mutex);
	if (_session.find(session_id) != _session.end()) {
		auto uid = _session[session_id]->GetUserId();

		//移除用户和session的关联
		UserMgr::GetInstance()->RmvUserSession(uid, session_id);
	}

	_session.erase(session_id);

}

void CServer::HandleAccept(std::shared_ptr<CSession> new_session, const boost::system::error_code& error)
{
	if (!error) {
		new_session->Start();
		std::lock_guard<std::mutex> lock(_mutex);
		_session.insert(make_pair(new_session->GetSessionId(), new_session));
	}
	else {
		std::cout << "session accept failed, error is " << error.what() << std::endl;
	}
	StartAccept();
}

void CServer::StartAccept()
{
	auto& io_context = AsioIOServicePool::GetInstance()->GetIOService();
	std::shared_ptr<CSession> new_session = std::make_shared<CSession>(io_context, this);
	_acceptor.async_accept(new_session->GetSocket(), std::bind(&CServer::HandleAccept, this, new_session, placeholders::_1));
}
