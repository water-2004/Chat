#pragma once
#include"const.h"
#include <queue>
#include<thread>
#include<jdbc/mysql_driver.h>
#include<jdbc/mysql_connection.h>
#include<jdbc/cppconn/prepared_statement.h>
#include<jdbc/cppconn/resultset.h>
#include<jdbc/cppconn/statement.h>
#include<jdbc/cppconn/exception.h>
class MySqlPool
{
public:
	MySqlPool(const std::string& url, const std::string& user, const std::string& pass, const std::string& schema, int poolSize);
	std::unique_ptr<sql::Connection>getConnection();
	void returnConnection(std::unique_ptr<sql::Connection> con);
	void Close();
	~MySqlPool();
private:
	std::string url_;
	std::string user_;
	std::string pass_;
	std::string schema_;
	int poolsize_;
	std::queue<std::unique_ptr<sql::Connection>> pool_;
	std::mutex mutex_;
	std::condition_variable cond_;
	std::atomic<bool> b_stop_;

};

