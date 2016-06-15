#include "../betamessages.hpp"
#include "betaserver.hpp"

#include "utility/dynamicpointer.hpp"
#include "serialization/typeidmap.hpp"
#include "system/debug.hpp"
#include "frame/ipc/ipcservice.hpp"

using namespace solid;
using namespace std;

namespace beta_server{


template <class M>
void complete_message(
	frame::ipc::ConnectionContext &_rctx,
	std::shared_ptr<M> &_rsent_msg_ptr,
	std::shared_ptr<M> &_rrecv_msg_ptr,
	ErrorConditionT const &_rerror
);

template <>
void complete_message<beta_protocol::FirstMessage>(
	frame::ipc::ConnectionContext &_rctx,
	std::shared_ptr<beta_protocol::FirstMessage> &_rsent_msg_ptr,
	std::shared_ptr<beta_protocol::FirstMessage> &_rrecv_msg_ptr,
	ErrorConditionT const &_rerror
){
	idbg("");
	SOLID_CHECK(not _rerror);
	if(_rrecv_msg_ptr){
		SOLID_CHECK(not _rsent_msg_ptr);
		
		ErrorConditionT err = _rctx.service().sendMessage(
			_rctx.recipientId(),
			std::make_shared<beta_protocol::SecondMessage>(std::move(*_rrecv_msg_ptr))
		);
		
		if(err){
			SOLID_THROW_EX("Connection id should not be invalid!", err.message());
		}
	}
	if(_rsent_msg_ptr){
		SOLID_CHECK(not _rrecv_msg_ptr);
	}
}

template <>
void complete_message<beta_protocol::SecondMessage>(
	frame::ipc::ConnectionContext &_rctx,
	std::shared_ptr<beta_protocol::SecondMessage> &_rsent_msg_ptr,
	std::shared_ptr<beta_protocol::SecondMessage> &_rrecv_msg_ptr,
	ErrorConditionT const &_rerror
){
	idbg("");
	SOLID_CHECK(not _rerror);
	if(_rrecv_msg_ptr){
		SOLID_CHECK(not _rsent_msg_ptr);
		ErrorConditionT err = _rctx.service().sendMessage(_rctx.recipientId(), std::move(_rrecv_msg_ptr));
		
		if(err){
			SOLID_THROW_EX("Connection id should not be invalid!", err.message());
		}
	}
	if(_rsent_msg_ptr){
		SOLID_CHECK(not _rrecv_msg_ptr);
	}
}

template <>
void complete_message<beta_protocol::ThirdMessage>(
	frame::ipc::ConnectionContext &_rctx,
	std::shared_ptr<beta_protocol::ThirdMessage> &_rsent_msg_ptr,
	std::shared_ptr<beta_protocol::ThirdMessage> &_rrecv_msg_ptr,
	ErrorConditionT const &_rerror
){
	idbg("");
	SOLID_CHECK(not _rerror);
	if(_rrecv_msg_ptr){
		SOLID_CHECK(not _rsent_msg_ptr);
		
		ErrorConditionT err = _rctx.service().sendMessage(
			_rctx.recipientId(),
			std::make_shared<beta_protocol::FirstMessage>(std::move(*_rrecv_msg_ptr))
		);
		
		if(err){
			SOLID_THROW_EX("Connection id should not be invalid!", err.message());
		}
	}
	if(_rsent_msg_ptr){
		SOLID_CHECK(not _rrecv_msg_ptr);
	}
}


template <typename T>
struct MessageSetup{
	void operator()(frame::ipc::serialization_v1::Protocol &_rprotocol, const size_t _protocol_idx, const size_t _message_idx){
		_rprotocol.registerType<T>(complete_message<T>, _protocol_idx, _message_idx);
	}
};

void register_messages(solid::frame::ipc::serialization_v1::Protocol &_rprotocol){
	beta_protocol::ProtoSpecT::setup<MessageSetup>(_rprotocol);
}

}