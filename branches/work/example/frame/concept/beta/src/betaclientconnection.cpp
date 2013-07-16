/* Implementation file betaclientconnection.cpp
	
	Copyright 2012 Valentin Palade 
	vipalade@gmail.com

	This file is part of SolidFrame framework.

	SolidFrame is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	SolidFrame is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with SolidFrame.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "system/debug.hpp"
#include "system/timespec.hpp"
#include "system/mutex.hpp"
#include "system/socketaddress.hpp"
#include "system/socketdevice.hpp"
#include "system/specific.hpp"
#include "system/exception.hpp"

#include "utility/ostream.hpp"
#include "utility/istream.hpp"
#include "utility/iostream.hpp"

#include "frame/ipc/ipcservice.hpp"
#include "frame/requestuid.hpp"

#include "betabuffer.hpp"

#include "core/manager.hpp"
#include "core/messages.hpp"

#include "serialization/typemapperbase.hpp"

#include "beta/betamessages.hpp"

#include "betaclientconnection.hpp"
#include "betaclientcommands.hpp"

using namespace solid;

namespace concept{
namespace beta{
namespace client{


/*static*/ void Connection::initStatic(Manager &_rm){
	dynamicRegister();
}

/*static*/ Connection::DynamicMapperT		Connection::dm;

/*static*/ void Connection::dynamicRegister(){
	//dm.insert<SendStreamSignal, Connection>();
	dm.insert<LoginMessage, Connection>();
	dm.insert<CancelMessage, Connection>();
}

Connection::Connection(
	const ResolveData &_raddrinfo
):	addrinfo(_raddrinfo), reqid(1){
	
	state(Init);
	addrit = addrinfo.begin();
	while(addrit && addrit.type() != SocketInfo::Stream){
		++addrit;
	}
}


/*
NOTE: 
* Releasing the connection here and not in a base class destructor because
here we know the exact type of the object - so the service can do other things 
based on the type.
* Also it ensures a proper/safe visiting. Suppose the unregister would have taken 
place in a base destructor. If the visited object is a leaf, one may visit
destroyed data.
NOTE: 
* Visitable data must be destroyed after releasing the connection!!!
*/

Connection::~Connection(){
	idbg("destroy connection id "<<this->id());
	for(CommandVectorT::const_iterator it(cmdvec.begin()); it != cmdvec.end(); ++it){
		delete it->pcmd;
	}
}


/*virtual*/ bool Connection::notify(DynamicPointer<frame::Message> &_rmsgptr){
	if(this->state() < 0){
		_rmsgptr.clear();
		return false;//no reason to raise the pool thread!!
	}
	
	return frame::Object::notify(frame::S_SIG | frame::S_RAISE);
}


/*
	The main loop with the implementation of the alpha protocol's
	state machine. We dont need a loop, we use the ConnectionSelector's
	loop - returning OK.
	The state machine is a simple switch
*/

int Connection::execute(ulong _sig, TimeSpec &_tout){
	frame::requestuidptr->set(frame::Manager::specific().id(*this));
	
	if(notified()){//we've received a signal
		ulong 							sm(0);
		DynamicHandler<DynamicMapperT>	dh(dm);
		{
			Locker<Mutex>	lock(frame::Manager::specific().mutex(*this));
			sm = grabSignalMask(0);//grab all bits of the signal mask
			if(sm & frame::S_KILL) return BAD;
			if(sm & frame::S_SIG){//we have signals
				dh.init(dv.begin(), dv.end());
				dv.clear();
			}
		}
		if(sm & frame::S_SIG){//we've grabed signals, execute them
			for(size_t i = 0; i < dh.size(); ++i){
				dh.handle(*this, i);
			}
		}
		//now we determine if we return with NOK or we continue
		if(!_sig) return NOK;
	}
	const uint32 sevs = socketEventsGrab();
	switch(state()){
		case Init:{
			if(!addrit) return BAD;
			SocketDevice	sd;
			sd.create(addrit);
			sd.makeNonBlocking();
			socketInsert(sd);
			socketRequestRegister();
			state(Connect);
			
		}return NOK;
		case ConnectNext:
			while(addrit && addrit.type() != SocketInfo::Stream){
				++addrit;
			}
			if(!addrit) return BAD;
			state(Connect);
		case Connect:
			switch(socketConnect(addrit)){
				case BAD:
					state(ConnectPrepare);
					return OK;
				case OK:
					idbg("");
					doPrepareRun();
					idbg("");
					return OK;//for register
				case NOK:
					state(ConnectWait);
					idbg("");
					return NOK;
			}
			break;
		case ConnectWait:
			if(_sig & (frame::TIMEOUT | frame::ERRDONE)){
				state(ConnectNext);
			}else if((sevs & frame::OUTDONE) != 0){
				doPrepareRun();
			}else{
				return NOK;
			}
			return OK;
		case Run:{
			idbg("");
			if(_sig & (frame::TIMEOUT | frame::ERRDONE)){
				return BAD;
			}
			bool reenter = false;
			if(!socketHasPendingSend()){
				int sz = doFillSendBuffer(useCompression(), useEncryption());
				if(sz > 0){
					switch(socketSend(sendbufbeg, sz)){
						case BAD: return BAD;
						case OK:
							reenter = true;
							break;
						case NOK:
							break;
					}
				}else if(sz < 0){
					return BAD;
				}else if(_sig & frame::OUTDONE){
					doReleaseSendBuffer();
				}
			}
			if(!socketHasPendingRecv()){
				if(_sig & frame::INDONE){
					doParseBuffer(socketRecvCount());
					if(cmdque.size()){
						reenter = true;
					}
				}
				switch(socketRecv(recvbufwr, recvbufend - recvbufwr)){
					case BAD: return BAD;
					case OK:
						doParseBuffer(socketRecvCount());
						reenter = true;
						break;
					case NOK:
						break;
				}
			}
			if(reenter) return OK;
		}break;
	}
	idbg("");
	return NOK;
}

void Connection::doPrepareRun(){
	state(Run);
	BaseT::doPrepareRun();
}

int Connection::doFillSendBufferData(char *_sendbufpos){
	int		writesize = 0;
	char	*sendbufpos = _sendbufpos;
	while(true){
		if(!ser.empty()){
			int rv = ser.run(
				sendbufpos,
				sendbufend - sendbufpos
			);
			if(rv < 0){
				idbg("Serialization error: "<<ser.errorString());
				return BAD;
			}
			sendbufpos += rv;
			writesize += rv;
			if((sendbufend - sendbufpos) < minsendsize){
				break;
			}
		}
		
		cassert(ser.empty());
		
		if(cmdque.empty()){
			break;
		}
		
		uint32		cmdidx = cmdque.front();
		
		cmdque.pop();
		
		CommandStub	&rcmdstub(cmdvec[cmdidx]);
		
		const int	rv = rcmdstub.pcmd->executeSend(cmdidx);
		
		if(rv == OK){
			//no reason to send the request
			delete rcmdstub.pcmd;
			rcmdstub.pcmd = NULL;
			++rcmdstub.uid;
			cmdvecfreestk.push(cmdidx);
			continue;
		}else if(rv == CONTINUE){
			
		}else{
			THROW_EXCEPTION_EX("Unknown executeStart return value", rv);
		}
		
		crtcmdsendidx = cmdidx;
		crtcmdsendtype = rcmdstub.pcmd->dynamicType();
		
		rcmdstub.pcmd->prepareSerialization(ser);
		
		ser.push(crtcmdsendtype, "command_type");
		if(rcmdstub.sendtype){
			ser.push(crtcmdsendidx, "command_tag");
			rcmdstub.sendtype = false;
		}
	}
	
	return writesize;
}


bool Connection::doParseBufferData(const char *_pbuf, ulong _len){
	do{
		if(des.empty()){
			des.pushReinit<Connection, 0>(this, 0);
			des.push(crtcmdrecvidx, "command_tag");
		}
		int rv = des.run(_pbuf, _len);
		if(rv < 0){
			return false;
		}
		
		if(des.empty()){
			CommandStub	&rcmdstub(cmdvec[crtcmdrecvidx]);
			const int	rv = rcmdstub.pcmd->executeRecv(crtcmdrecvidx);
			switch(rv){
				case BAD:
					delete rcmdstub.pcmd;
					rcmdstub.pcmd = NULL;
					++rcmdstub.uid;
					cmdvecfreestk.push(crtcmdrecvidx);
					return false;
				case OK:
					delete rcmdstub.pcmd;
					rcmdstub.pcmd = NULL;
					++rcmdstub.uid;
					cmdvecfreestk.push(crtcmdrecvidx);
					break;
				case NOK:
					break;
				case CONTINUE:
					cmdque.push(crtcmdrecvidx);
					break;
				default:
					THROW_EXCEPTION_EX("Unknown command execute return value ", rv);
					break;
			}
		}
		
		_len  -= rv;
		_pbuf += rv;
	}while(_len != 0);
	return true;
}

int Connection::doParseBufferException(const char *_pbuf, ulong _len){
	exception = 0;
	uint32	error = 0;
	uint32	recvcmdidx = 0;
	uint32	sendcmdidx = 0;
	uint32	ser_err = 0;
	uint32	des_err = 0;
	const bool rv = BaseT::doParseBufferException(
		_pbuf, _len,
		error,
		recvcmdidx,
		sendcmdidx,
		ser_err,
		des_err
	);
	if(rv){
		edbg("Exception on server side 1: "<<error<<" recvcmdidx = "<<recvcmdidx<<" sendcmdidx = "<<sendcmdidx);
		edbg("Exception on server side 2: ["<<SerializerT::errorString(ser_err)<<"] ["<<DeserializerT::errorString(des_err)<<']');
	}else{
		edbg("Exception - unable to parse exception buffer");
	}
	return BAD;
}

bool Connection::useEncryption()const{
	return false;
}
bool Connection::useCompression()const{
	return false;
}

template <>
int Connection::serializationReinit<Connection::DeserializerT, 0>(
	Connection::DeserializerT &_rdes, const uint64 &_rv
){
	if(crtcmdrecvidx < cmdvec.size()){
		CommandStub	&rcmdstub(cmdvec[crtcmdrecvidx]);
		if(rcmdstub.pcmd){
			_rdes.pop();
			rcmdstub.pcmd->prepareDeserialization(_rdes);
			return CONTINUE;
		}
	}
	
	return BAD;
}

void Connection::pushCommand(Command *_pcmd){
	uint32	cmdidx;
	if(cmdvecfreestk.size()){
		cmdidx = cmdvecfreestk.top();
		cmdvecfreestk.pop();
		cmdvec[cmdidx].pcmd = _pcmd;
	}else{
		cmdidx = cmdvec.size();
		cmdvec.push_back(CommandStub(_pcmd));
	}
	cmdque.push(cmdidx);
	
}
void Connection::dynamicHandle(solid::DynamicPointer<> &_dp){
	
}
void Connection::dynamicHandle(DynamicPointer<LoginMessage> &_rmsgptr){
	command::Login *pcmd = new command::Login(_rmsgptr->user, _rmsgptr->pass);
	pcmd->msgptr = _rmsgptr;
	pushCommand(pcmd);
}
void Connection::dynamicHandle(DynamicPointer<CancelMessage> &_rmsgptr){
	
}

}//namespace client
}//namespace beta
}//namespace concept
