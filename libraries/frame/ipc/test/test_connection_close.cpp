#include "frame/manager.hpp"
#include "frame/scheduler.hpp"
#include "frame/service.hpp"

#include "frame/aio/aioreactor.hpp"
#include "frame/aio/aioobject.hpp"
#include "frame/aio/aiolistener.hpp"
#include "frame/aio/aiotimer.hpp"
#include "frame/aio/aioresolver.hpp"

#include "frame/aio/openssl/aiosecurecontext.hpp"
#include "frame/aio/openssl/aiosecuresocket.hpp"

#include "frame/ipc/ipcservice.hpp"
#include "frame/ipc/ipcconfiguration.hpp"
#include "frame/ipc/ipcprotocol_serialization_v1.hpp"


#include "system/thread.hpp"
#include "system/mutex.hpp"
#include "system/condition.hpp"
#include "system/exception.hpp"

#include "system/debug.hpp"

#include <iostream>

using namespace std;
using namespace solid;

typedef frame::Scheduler<frame::aio::Reactor>	AioSchedulerT;
typedef frame::aio::openssl::Context			SecureContextT;

namespace{
	
struct InitStub{
	size_t		size;
	ulong		flags;
};

InitStub initarray[] = {
	{16384000, 0}
};

std::string						pattern;
const size_t					initarraysize = sizeof(initarray)/sizeof(InitStub);

size_t							connection_count(0);

bool							running = true;
bool							client_received_logout = false;
bool							client_received_message = false;
Mutex							mtx;
Condition						cnd;
frame::ipc::Service				*pipcclient = nullptr;
std::atomic<uint64_t>				transfered_size(0);
std::atomic<size_t>				transfered_count(0);


size_t real_size(size_t _sz){
	//offset + (align - (offset mod align)) mod align
	return _sz + ((sizeof(uint64_t) - (_sz % sizeof(uint64_t))) % sizeof(uint64_t));
}

struct Message: frame::ipc::Message{
	uint32_t							idx;
    std::string						str;
	bool							serialized;
	
	Message(uint32_t _idx):idx(_idx), serialized(false){
		idbg("CREATE ---------------- "<<(void*)this<<" idx = "<<idx);
		init();
		
	}
	Message(): serialized(false){
		idbg("CREATE ---------------- "<<(void*)this);
	}
	~Message(){
		idbg("DELETE ---------------- "<<(void*)this);
		if(not serialized and not this->isBackOnSender()){
			SOLID_THROW("Message not serialized.");
		}
	}

	template <class S>
	void serialize(S &_s, frame::ipc::ConnectionContext &_rctx){
		_s.push(str, "str");
		_s.push(idx, "idx");
		
		if(S::IsSerializer){
			serialized = true;
		}
	}
	
	void init(){
		const size_t	sz = real_size(initarray[idx % initarraysize].size);
		str.resize(sz);
		const size_t	count = sz / sizeof(uint64_t);
		uint64_t			*pu  = reinterpret_cast<uint64_t*>(const_cast<char*>(str.data()));
		const uint64_t	*pup = reinterpret_cast<const uint64_t*>(pattern.data());
		const size_t	pattern_size = pattern.size() / sizeof(uint64_t);
		for(uint64_t i = 0; i < count; ++i){
			pu[i] = pup[(idx + i) % pattern_size];//pattern[i % pattern.size()];
		}
	}
	
	bool check()const{
		const size_t	sz = real_size(initarray[idx % initarraysize].size);
		idbg("str.size = "<<str.size()<<" should be equal to "<<sz);
		if(sz != str.size()){
			return false;
		}
		//return true;
		const size_t	count = sz / sizeof(uint64_t);
		const uint64_t	*pu = reinterpret_cast<const uint64_t*>(str.data());
		const uint64_t	*pup = reinterpret_cast<const uint64_t*>(pattern.data());
		const size_t	pattern_size = pattern.size() / sizeof(uint64_t);
		
		for(uint64_t i = 0; i < count; ++i){
			if(pu[i] != pup[(i + idx) % pattern_size]){
				SOLID_THROW("Message check failed.");
				return false;
			}
		}
		return true;
	}
	
};

struct Logout: frame::ipc::Message{
	template <class S>
	void serialize(S &_s, frame::ipc::ConnectionContext &_rctx){
	}
};

void client_connection_stop(frame::ipc::ConnectionContext &_rctx, ErrorConditionT const&){
	idbg(_rctx.recipientId());
	if(_rctx.isConnectionActive()){
		//NOTE: (***) in order for client_received_logout check to work, one shoud better
		// use delayCloseConnectionPool instead of closeConnection
		// in server_complete_logout
		SOLID_CHECK(client_received_message and client_received_logout);
		
		++connection_count;
		running = false;
		cnd.broadcast();
	}
	
}

void client_connection_start(frame::ipc::ConnectionContext &_rctx){
	idbg(_rctx.recipientId());
	auto lambda =  [](frame::ipc::ConnectionContext&, ErrorConditionT const& _rerror){
		idbg("enter active error: "<<_rerror.message());
		return frame::ipc::MessagePointerT();
	};
	_rctx.service().connectionNotifyEnterActiveState(_rctx.recipientId(), lambda);
}

void server_connection_stop(frame::ipc::ConnectionContext &_rctx, ErrorConditionT const&){
	idbg(_rctx.recipientId());
}

void server_connection_start(frame::ipc::ConnectionContext &_rctx){
	idbg(_rctx.recipientId());
	auto lambda =  [](frame::ipc::ConnectionContext&, ErrorConditionT const& _rerror){
		idbg("enter active error: "<<_rerror.message());
		return frame::ipc::MessagePointerT();
	};
	_rctx.service().connectionNotifyEnterActiveState(_rctx.recipientId(), lambda);
}


void client_complete_message(
	frame::ipc::ConnectionContext &_rctx,
	std::shared_ptr<Message> &_rsent_msg_ptr, std::shared_ptr<Message> &_rrecv_msg_ptr,
	ErrorConditionT const &_rerror
){
	idbg(_rctx.recipientId());
	
	SOLID_CHECK(!_rerror);
	SOLID_CHECK(_rsent_msg_ptr.get() and _rrecv_msg_ptr.get());
	
	if(_rrecv_msg_ptr.get()){
		if(not _rrecv_msg_ptr->check()){
			SOLID_THROW("Message check failed.");
		}
		
		//cout<< _rmsgptr->str.size()<<'\n';
		transfered_size += _rrecv_msg_ptr->str.size();
		++transfered_count;
		
		if(!_rrecv_msg_ptr->isBackOnSender()){
			SOLID_THROW("Message not back on sender!.");
		}
		
		{
			frame::ipc::MessagePointerT	msgptr(new Logout);
			
			pipcclient->sendMessage(
				_rctx.recipientId(), msgptr,
				0|frame::ipc::MessageFlags::WaitResponse
			);
		}
		
		client_received_message = true;
	}
}

void client_complete_logout(
	frame::ipc::ConnectionContext &_rctx,
	std::shared_ptr<Logout> &_rsent_msg_ptr, std::shared_ptr<Logout> &_rrecv_msg_ptr,
	ErrorConditionT const &_rerror
){
	SOLID_CHECK(!_rerror);
	SOLID_CHECK(_rsent_msg_ptr.get() and _rrecv_msg_ptr.get());
	client_received_logout = true;
}

void server_complete_message(
	frame::ipc::ConnectionContext &_rctx,
	std::shared_ptr<Message> &_rsent_msg_ptr, std::shared_ptr<Message> &_rrecv_msg_ptr,
	ErrorConditionT const &_rerror
){
	if(_rrecv_msg_ptr.get()){
		idbg(_rctx.recipientId()<<" received message with id on sender "<<_rrecv_msg_ptr->requestId());
		
		if(not _rrecv_msg_ptr->check()){
			SOLID_THROW("Message check failed.");
		}
		
		if(!_rrecv_msg_ptr->isOnPeer()){
			SOLID_THROW("Message not on peer!.");
		}
		
		//send message back
		if(_rctx.recipientId().isInvalidConnection()){
			SOLID_THROW("Connection id should not be invalid!");
		}
		ErrorConditionT err = _rctx.service().sendMessage(_rctx.recipientId(), std::move(_rrecv_msg_ptr));
		
		if(err){
			SOLID_THROW_EX("Connection id should not be invalid!", err.message());
		}
	}
	if(_rsent_msg_ptr.get()){
		idbg(_rctx.recipientId()<<" done sent message "<<_rsent_msg_ptr.get());
	}
}


void server_complete_logout(
	frame::ipc::ConnectionContext &_rctx,
	std::shared_ptr<Logout> &_rsent_msg_ptr, std::shared_ptr<Logout> &_rrecv_msg_ptr,
	ErrorConditionT const &_rerror
){
	if(_rrecv_msg_ptr.get()){
		if(!_rrecv_msg_ptr->isOnPeer()){
			SOLID_THROW("Message not on peer!.");
		}
		
		//send message back
		if(_rctx.recipientId().isInvalidConnection()){
			SOLID_THROW("Connection id should not be invalid!");
		}
		
		idbg("send back logout");
		
		ErrorConditionT err = _rctx.service().sendMessage(_rctx.recipientId(), std::move(_rrecv_msg_ptr));
		
		if(err){
			SOLID_THROW_EX("Connection id should not be invalid!", err.message());
		}
	}
	
	if(_rsent_msg_ptr.get()){
		idbg("close connection");
		//see NOTE (***) above
		SOLID_CHECK(_rctx.service().closeConnection(_rctx.recipientId()));
#if 0
		_rctx.service().delayCloseConnectionPool(
			_rctx.recipientId(),
			[](frame::ipc::ConnectionContext &_rctx){
				idbg("------------------");
			}
		);
#endif
	}
}

}//namespace


int test_connection_close(int argc, char **argv){
	Thread::init();
#ifdef SOLID_HAS_DEBUG
	Debug::the().levelMask("ew");
	Debug::the().moduleMask("frame_ipc:ew any:ew");
	Debug::the().initStdErr(false, nullptr);
	//Debug::the().initFile("test_clientserver_basic", false);
#endif
	
	for(int j = 0; j < 1; ++j){
		for(int i = 0; i < 127; ++i){
			int c = (i + j) % 127;
			if(isprint(c) and !isblank(c)){
				pattern += static_cast<char>(c);
			}
		}
	}
	
	size_t	sz = real_size(pattern.size());
	
	if(sz > pattern.size()){
		pattern.resize(sz - sizeof(uint64_t));
	}else if(sz < pattern.size()){
		pattern.resize(sz);
	}
	
	{
		AioSchedulerT			sch_client;
		AioSchedulerT			sch_server;
			
			
		frame::Manager			m;
		frame::ipc::Service		ipcserver(m);
		frame::ipc::Service		ipcclient(m);
		ErrorConditionT			err;
		
		frame::aio::Resolver	resolver;
		
		err = sch_client.start(1);
		
		if(err){
			edbg("starting aio client scheduler: "<<err.message());
			return 1;
		}
		
		err = sch_server.start(1);
		
		if(err){
			edbg("starting aio server scheduler: "<<err.message());
			return 1;
		}
		
		err = resolver.start(1);
		
		if(err){
			edbg("starting aio resolver: "<<err.message());
			return 1;
		}
		
		std::string		server_port;
		
		{//ipc server initialization
			frame::ipc::serialization_v1::Protocol	*proto = new frame::ipc::serialization_v1::Protocol;
			frame::ipc::Configuration				cfg(sch_server, proto);
			
			proto->registerType<Message>(
				server_complete_message
			);
			
			proto->registerType<Logout>(
				server_complete_logout
			);
			
			//cfg.recv_buffer_capacity = 1024;
			//cfg.send_buffer_capacity = 1024;
			
			cfg.connection_stop_fnc = server_connection_stop;
			cfg.connection_start_incoming_fnc = server_connection_start;
			
			cfg.listener_address_str = "0.0.0.0:0";
			
			err = ipcserver.reconfigure(std::move(cfg));
			
			if(err){
				edbg("starting server ipcservice: "<<err.message());
				Thread::waitAll();
				return 1;
			}
			
			{
				std::ostringstream oss;
				oss<<ipcserver.configuration().listenerPort();
				server_port = oss.str();
				idbg("server listens on port: "<<server_port);
			}
		}
		
		{//ipc client initialization
			frame::ipc::serialization_v1::Protocol	*proto = new frame::ipc::serialization_v1::Protocol;
			frame::ipc::Configuration				cfg(sch_client, proto);
			
			proto->registerType<Message>(
				client_complete_message
			);
			
			proto->registerType<Logout>(
				client_complete_logout
			);
			
			//cfg.recv_buffer_capacity = 1024;
			//cfg.send_buffer_capacity = 1024;
			
			cfg.connection_stop_fnc = client_connection_stop;
			cfg.connection_start_outgoing_fnc = client_connection_start;
			
			cfg.pool_max_active_connection_count = 1;
			
			cfg.name_resolve_fnc = frame::ipc::InternetResolverF(resolver, server_port.c_str()/*, SocketInfo::Inet4*/);
			
			err = ipcclient.reconfigure(std::move(cfg));
			
			if(err){
				edbg("starting client ipcservice: "<<err.message());
				Thread::waitAll();
				return 1;
			}
		}
		
		pipcclient  = &ipcclient;
		
		{
			frame::ipc::MessagePointerT	msgptr(new Message(0));
			ipcclient.sendMessage(
				"localhost", msgptr,
				initarray[0].flags | frame::ipc::MessageFlags::WaitResponse
			);
		}
		
		Locker<Mutex>	lock(mtx);
		
		while(running){
			//cnd.wait(lock);
			TimeSpec	abstime = TimeSpec::createRealTime();
			abstime += (120 * 1000);//ten seconds
			cnd.wait(lock);
			bool b = true;//cnd.wait(lock, abstime);
			if(!b){
				//timeout expired
				SOLID_THROW("Process is taking too long.");
			}
		}
		
		m.stop();
	}
	
	Thread::waitAll();
	
	std::cout<<"Transfered size = "<<(transfered_size * 2)/1024<<"KB"<<endl;
	std::cout<<"Transfered count = "<<transfered_count<<endl;
	std::cout<<"Connection count = "<<connection_count<<endl;
	
	return 0;
}
