#include "CServer.h"
#include "HttpConnection.h"
#include "AsioIOServicePool.h"
CServer::CServer(boost::asio::io_context& ioc, unsigned short& port)
	:_ioc(ioc),_acceptor(ioc,tcp::endpoint(tcp::v4(),port))
{
}

void CServer::Start() {
	auto self = shared_from_this();
	auto& io_context = AsioIOServicePool::GetInstance()->GetIOService();
	std::shared_ptr<HttpConnection> new_con = std::make_shared<HttpConnection>(io_context);
	_acceptor.async_accept(new_con->GetSocket(), [self, new_con](beast::error_code ec) {
		try {
			//出错放弃这链接，继续监听其他链接
			if (ec) {
				std::cout << "Accept failed: " << ec.message() << std::endl;
				self->Start();
				return;
			}
			std::cout << "Accept success, new connection established" << std::endl;
			//创建新连接，并且创建httpconnection类管理链接
			new_con->Start();
			//继续监听
			self->Start();
		}
		catch (std::exception& exp) {
			std::cout << "exception is " << exp.what() << std::endl;
			self->Start();
		}
	});
}
