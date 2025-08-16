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

class SqlConnection {
public:
	SqlConnection(sql::Connection* con, int64_t lasttime) :_con(con), _last_oper_time(lasttime) {}
	std::unique_ptr<sql::Connection> _con;
	int64_t _last_oper_time;
};

class MySqlPool
{
public:
	MySqlPool(const std::string& url, const std::string& user, const std::string& pass, const std::string& schema, int poolSize);
	void checkConnectionPro();
	bool reconnect(long long timestamp);
	void checkConnection();
	std::unique_ptr<SqlConnection>getConnection();
	void returnConnection(std::unique_ptr<SqlConnection> con);
	void Close();
	~MySqlPool();
private:
	std::string url_;
	std::string user_;
	std::string pass_;
	std::string schema_;
	int poolsize_;
	std::queue<std::unique_ptr<SqlConnection>> pool_;
	std::mutex mutex_;
	std::condition_variable cond_;
	std::atomic<bool> b_stop_;
	std::thread _check_thread;
	std::atomic<int> _fail_count;
};

