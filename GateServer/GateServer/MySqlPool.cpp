#include "MySqlPool.h"

MySqlPool::MySqlPool(const std::string& url, const std::string& user, const std::string& pass, const std::string& schema, int poolSize)
	:url_(url), user_(user), pass_(pass), schema_(schema), poolsize_(poolSize),b_stop_(false)
{
	try
	{
		for (int i = 0; i < poolsize_; i++)
		{
			sql::mysql::MySQL_Driver* drive = sql::mysql::get_mysql_driver_instance();
			std::unique_ptr<sql::Connection>con(drive->connect(url_, user_, pass_));
			con->setSchema(schema_);
			pool_.push(std::move(con));
		}
	}
	catch (sql::SQLException& e)
	{
		//¥¶¿Ì“Ï≥£
		std::cout << "mysql pool init failed" << std::endl;
	}
}

std::unique_ptr<sql::Connection> MySqlPool::getConnection()
{
	std::unique_lock<std::mutex> lock(mutex_);
	cond_.wait(lock, [this] {
		if (b_stop_) {
			return true;
		}
		return !pool_.empty();
		});
	if (b_stop_) {
		return nullptr;
	}
	std::unique_ptr<sql::Connection> con(std::move(pool_.front()));
	pool_.pop();
	return con;
}

void MySqlPool::returnConnection(std::unique_ptr<sql::Connection> con)
{
	std::unique_lock<std::mutex> lock(mutex_);
	if (b_stop_) {
		return;
	}
	pool_.push(std::move(con));
	cond_.notify_one();
}

void MySqlPool::Close()
{
	b_stop_ = true;
	cond_.notify_all();
}

MySqlPool::~MySqlPool()
{
	std::unique_lock<std::mutex> lock(mutex_);
	while (!pool_.empty()) {
		pool_.pop();
	}
}
