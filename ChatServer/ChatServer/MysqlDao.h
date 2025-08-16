#pragma once
#include"const.h"
#include"MySqlPool.h"
#include "data.h"


class MysqlDao
{
public:
	MysqlDao();
	~MysqlDao();
	int RegUser(const std::string& name, const std::string& email, const std::string& pwd);
	bool CheckEmail(const std::string& name, const std::string& email);
	bool UpdatePwd(const std::string& email, const std::string& newpwd);
	bool CheckPwd(const std::string& email, const std::string& pwd, UserInfo& userInfo);
	bool AddFriendApply(const int& from, const int& to);
	bool GetApplyList(int touid, std::vector<std::shared_ptr<ApplyInfo>>& applyList, int begin, int limit);
	bool ConfirmFriendApply(const int& from, const int& to, const std::string& back_name);
	std::shared_ptr<UserInfo> GetUser(int uid);
	std::shared_ptr<UserInfo> GetUser(std::string name);
	bool GetFriendList(int self_id, std::vector<std::shared_ptr<UserInfo> >& user_info);
private:
	std::unique_ptr<MySqlPool> pool_;
};

