/* Implementation file consensusrequest.cpp
	
	Copyright 2011, 2012 Valentin Palade 
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

#include "distributed/consensus/consensusrequest.hpp"

#include "foundation/ipc/ipcservice.hpp"
#include "foundation/manager.hpp"

namespace fdt=foundation;

namespace distributed{
namespace consensus{

bool RequestId::operator<(const RequestId &_rcsi)const{
	if(this->sockaddr < _rcsi.sockaddr){
		return true;
	}else if(_rcsi.sockaddr < this->sockaddr){
		return false;
	}else if(this->senderuid < _rcsi.senderuid){
		return true;
	}else if(_rcsi.senderuid > this->senderuid){
		return false;
	}else return overflow_safe_less(this->requid, _rcsi.requid);
}
bool RequestId::operator==(const RequestId &_rcsi)const{
	return this->sockaddr == _rcsi.sockaddr && 
		this->senderuid == _rcsi.senderuid &&
		this->requid == _rcsi.requid;
}
size_t RequestId::hash()const{
	return sockaddr.hash() ^ this->senderuid.first ^ this->requid;
}
bool RequestId::senderEqual(const RequestId &_rcsi)const{
	return this->sockaddr == _rcsi.sockaddr && 
		this->senderuid == _rcsi.senderuid;
}
bool RequestId::senderLess(const RequestId &_rcsi)const{
	if(this->sockaddr < _rcsi.sockaddr){
		return true;
	}else if(_rcsi.sockaddr < this->sockaddr){
		return false;
	}else return this->senderuid < _rcsi.senderuid;
}
size_t RequestId::senderHash()const{
	return sockaddr.hash() ^ this->senderuid.first;
}
std::ostream &operator<<(std::ostream& _ros, const RequestId &_rreqid){
	_ros<<_rreqid.requid<<','<<' '<<_rreqid.senderuid.first<<','<<' '<<_rreqid.senderuid.second<<','<<' ';
	const SocketAddress4 &ra(_rreqid.sockaddr);
	char				host[SocketAddress::HostNameCapacity];
	char				port[SocketAddress::ServiceNameCapacity];
	ra.name(
		host,
		SocketAddress::HostNameCapacity,
		port,
		SocketAddress::ServiceNameCapacity
		,
		SocketAddress::NumericService | SocketAddress::NumericHost
	);
	_ros<<host<<':'<<port;
	return _ros;
}
//--------------------------------------------------------------
WriteRequestSignal::WriteRequestSignal():waitresponse(false), st(OnSender), sentcount(0){
	idbg("WriteRequestSignal "<<(void*)this);
}
WriteRequestSignal::WriteRequestSignal(const RequestId &_rreqid):waitresponse(false), st(OnSender), sentcount(0),id(_rreqid){
	idbg("WriteRequestSignal "<<(void*)this);
}

WriteRequestSignal::~WriteRequestSignal(){
	idbg("~WriteRequestSignal "<<(void*)this);
	if(waitresponse && !sentcount){
		idbg("failed receiving response "/*<<sentcnt*/);
		fdt::m().signal(fdt::S_KILL | fdt::S_RAISE, id.senderuid);
	}
}

void WriteRequestSignal::ipcReceived(
	foundation::ipc::SignalUid &_rsiguid
){
	//_rsiguid = this->ipcsiguid;
	ipcconid = fdt::ipc::ConnectionContext::the().connectionuid;
	
	char				host[SocketAddress::HostNameCapacity];
	char				port[SocketAddress::ServiceNameCapacity];
	
	id.sockaddr = fdt::ipc::ConnectionContext::the().pairaddr;
	
	id.sockaddr.name(
		host,
		SocketAddress::HostNameCapacity,
		port,
		SocketAddress::ServiceNameCapacity,
		SocketAddress::NumericService | SocketAddress::NumericHost
	);
	
	waitresponse = false;
	
	if(st == OnSender){
		st = OnPeer;
		idbg((void*)this<<" on peer: baseport = "<<fdt::ipc::ConnectionContext::the().baseport<<" host = "<<host<<":"<<port);
		id.sockaddr.port(fdt::ipc::ConnectionContext::the().baseport);
		//fdt::m().signal(sig, serverUid());
		this->sendThisToConsensusObject();
	}else if(st == OnPeer){
		st = BackOnSender;
		idbg((void*)this<<" back on sender: baseport = "<<fdt::ipc::ConnectionContext::the().baseport<<" host = "<<host<<":"<<port);
		
		DynamicPointer<fdt::Signal> sig(this);
		
		fdt::m().signal(sig, id.senderuid);
		_rsiguid = this->ipcsiguid;
	}else{
		cassert(false);
	}
}
// void WriteRequestSignal::sendThisToConsensusObject(){
// }
uint32 WriteRequestSignal::ipcPrepare(){
	uint32	rv(0);
	idbg((void*)this);
	if(st == OnSender){
		if(waitresponse){
			rv |= foundation::ipc::Service::WaitResponseFlag;
		}
		rv |= foundation::ipc::Service::SynchronousSendFlag;
		rv |= foundation::ipc::Service::SameConnectorFlag;
	}
	return rv;
}

void WriteRequestSignal::ipcFail(int _err){
	idbg((void*)this<<" sentcount = "<<(int)sentcount<<" err = "<<_err);
}

void WriteRequestSignal::ipcSuccess(){
	Locker<Mutex> lock(mutex());
	++sentcount;
	idbg((void*)this<<" sentcount = "<<(int)sentcount);
}


void WriteRequestSignal::use(){
	DynamicShared<fdt::Signal>::use();
	idbg((void*)this<<" usecount = "<<usecount);
}
int WriteRequestSignal::release(){
	int rv = DynamicShared<fdt::Signal>::release();
	idbg((void*)this<<" usecount = "<<usecount);
	return rv;
}
//--------------------------------------------------------------
//--------------------------------------------------------------
ReadRequestSignal::ReadRequestSignal():waitresponse(false), st(OnSender), sentcount(0){
	idbg("ReadRequestSignal "<<(void*)this);
}
ReadRequestSignal::ReadRequestSignal(const RequestId &_rreqid):waitresponse(false), st(OnSender), sentcount(0),id(_rreqid){
	idbg("ReadRequestSignal "<<(void*)this);
}

ReadRequestSignal::~ReadRequestSignal(){
	idbg("~ReadRequestSignal "<<(void*)this);
	if(waitresponse && !sentcount){
		idbg("failed receiving response "/*<<sentcnt*/);
		fdt::m().signal(fdt::S_KILL | fdt::S_RAISE, id.senderuid);
	}
}

void ReadRequestSignal::ipcReceived(
	foundation::ipc::SignalUid &_rsiguid
){
	//_rsiguid = this->ipcsiguid;
	ipcconid = fdt::ipc::ConnectionContext::the().connectionuid;
	
	char				host[SocketAddress::HostNameCapacity];
	char				port[SocketAddress::ServiceNameCapacity];
	
	id.sockaddr = fdt::ipc::ConnectionContext::the().pairaddr;
	
	id.sockaddr.name(
		host,
		SocketAddress::HostNameCapacity,
		port,
		SocketAddress::ServiceNameCapacity,
		SocketAddress::NumericService | SocketAddress::NumericHost
	);
	
	waitresponse = false;
	
	if(st == OnSender){
		st = OnPeer;
		idbg((void*)this<<" on peer: baseport = "<<fdt::ipc::ConnectionContext::the().baseport<<" host = "<<host<<":"<<port);
		id.sockaddr.port(fdt::ipc::ConnectionContext::the().baseport);
		//fdt::m().signal(sig, serverUid());
		this->sendThisToConsensusObject();
	}else if(st == OnPeer){
		st = BackOnSender;
		idbg((void*)this<<" back on sender: baseport = "<<fdt::ipc::ConnectionContext::the().baseport<<" host = "<<host<<":"<<port);
		
		DynamicPointer<fdt::Signal> sig(this);
		
		fdt::m().signal(sig, id.senderuid);
		_rsiguid = this->ipcsiguid;
	}else{
		cassert(false);
	}
}
// void ReadRequestSignal::sendThisToConsensusObject(){
// }
uint32 ReadRequestSignal::ipcPrepare(){
	uint32	rv(0);
	idbg((void*)this);
	if(st == OnSender){
		if(waitresponse){
			rv |= foundation::ipc::Service::WaitResponseFlag;
		}
		rv |= foundation::ipc::Service::SynchronousSendFlag;
		rv |= foundation::ipc::Service::SameConnectorFlag;
	}
	return rv;
}

void ReadRequestSignal::ipcFail(int _err){
	idbg((void*)this<<" sentcount = "<<(int)sentcount<<" err = "<<_err);
}

void ReadRequestSignal::ipcSuccess(){
	Locker<Mutex> lock(mutex());
	++sentcount;
	idbg((void*)this<<" sentcount = "<<(int)sentcount);
}


void ReadRequestSignal::use(){
	DynamicShared<fdt::Signal>::use();
	idbg((void*)this<<" usecount = "<<usecount);
}
int ReadRequestSignal::release(){
	int rv = DynamicShared<fdt::Signal>::release();
	idbg((void*)this<<" usecount = "<<usecount);
	return rv;
}
}//namespace consensus
}//namespace distributed
