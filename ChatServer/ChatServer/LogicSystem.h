#pragma once
#include "Singleton.h"
#include <queue>
#include <thread>
#include "CSession.h"
#include <queue>
#include <map>
#include <functional>
#include "const.h"
#include <json/json.h>
#include <json/value.h>
#include <json/reader.h>
#include <unordered_map>
#include "MysqlDao.h"
#include "data.h"

class CServer;
typedef  function<void(shared_ptr<CSession>, const short& msg_id, const string& msg_data)> FunCallBack;
class LogicSystem:public Singleton<LogicSystem>
{
	friend class Singleton<LogicSystem>;
public:
	~LogicSystem();
	void PostMsgToQue(shared_ptr<LogicNode> msg);
private:
	LogicSystem();
	void DealMsg();
	void RegisterCallBacks();
	bool GetBaseInfo(std::string base_key, int uid, std::shared_ptr<UserInfo>& userinfo);
	void LoginHandler(shared_ptr<CSession>, const short& msg_id, const string& msg_data);
	void SearchInfo(shared_ptr<CSession>, const short& msg_id, const string& msg_data);
	void AddFriendApply(shared_ptr<CSession>, const short& msg_id, const string& msg_data);
	void AuthFriendApply(std::shared_ptr<CSession> session, const short& msg_id, const string& msg_data);
	bool isPureDigit(const std::string& str);
	void GetUserByUid(std::string uid_str, Json::Value& rtvalue);
	void GetUserByName(std::string name, Json::Value& rtvalue);
	bool GetFriendApplyInfo(int to_uid, std::vector<std::shared_ptr<ApplyInfo>>& list);
	bool GetFriendList(int self_id, std::vector<std::shared_ptr<UserInfo>>& user_list);
	void DealChatTextMsg(std::shared_ptr<CSession> session,const short&msg_id,const string& msg_data);
	std::thread _worker_thread;
	std::queue<shared_ptr<LogicNode>> _msg_que;
	std::mutex _mutex;
	std::condition_variable _consume;
	bool _b_stop;
	std::map<short, FunCallBack> _fun_callback;
	std::unordered_map<int, std::shared_ptr<UserInfo>> _users;
};

