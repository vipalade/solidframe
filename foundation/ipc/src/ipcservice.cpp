/* Implementation file ipcservice.cpp
	
	Copyright 2007, 2008, 2010 Valentin Palade 
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

#include <map>
#include <vector>
#include <cstring>
#include <ostream>

#include "system/debug.hpp"
#include "system/mutex.hpp"
#include "system/socketdevice.hpp"
#include "system/specific.hpp"
#include "system/exception.hpp"

#include "utility/queue.hpp"

#include "foundation/objectpointer.hpp"
#include "foundation/common.hpp"
#include "foundation/manager.hpp"

#include "foundation/ipc/ipcservice.hpp"
#include "foundation/ipc/ipcconnectionuid.hpp"

#include "ipctalker.hpp"
#include "iodata.hpp"
#include "ipcsession.hpp"

namespace fdt = foundation;

namespace foundation{
namespace ipc{

//*******	Service::Data	******************************************************************

struct Service::Data{
	struct SessionStub{
		SessionStub(Session*	_pses = NULL, uint32 _uid = 0):pses(_pses), uid(_uid){}
		Session*	pses;
		uint32		uid;
	};
	
	typedef std::map<
		const Session::Addr4PairT*, 
		ConnectionUid, 
		Inet4AddrPtrCmp
		>												SessionAddr4MapT;
	
	typedef std::map<
		const Session::Addr6PairT*,
		ConnectionUid,
		Inet6AddrPtrCmp
		>												SessionAddr6MapT;
	
	struct TalkerStub{
		TalkerStub():cnt(0){}
		TalkerStub(const ObjectUidT &_ruid, uint32 _cnt = 0):uid(_ruid), cnt(_cnt){}
		ObjectUidT	uid;
		uint32		cnt;
	};
	typedef std::vector<TalkerStub>						TalkerStubVectorT;
	typedef Queue<uint32>								Uint32QueueT;
	
	Data(
		uint32 _keepalivetout,
		uint32 _sespertkr,
		uint32 _tkrmaxcnt
	);
	
	~Data();
	
	uint32 sessionCount()const{
		return sessionaddr4map.size();
	}
	
	const uint32 			keepalivetout;
	const uint32			sespertkr;
	const uint32			tkrmaxcnt;
	uint32					tkrcrt;
	int						baseport;
	SocketAddress			firstaddr;
	TalkerStubVectorT		tkrvec;
	SessionAddr4MapT		sessionaddr4map;
	Controller				*pc;
	Uint32QueueT			tkrq;
};

//=======	ServiceData		===========================================

Service::Data::Data(
	uint32 _keepalivetout,
	uint32 _sespertkr,
	uint32 _tkrmaxcnt
):
	keepalivetout(_keepalivetout), sespertkr(_sespertkr), tkrmaxcnt(_tkrmaxcnt),
	tkrcrt(0), baseport(-1), pc(NULL)
{
}

Service::Data::~Data(){
}

//=======	Service		===============================================

/*static*/ Service& Service::the(){
	return *m().service<Service>();
}
/*static*/ Service& Service::the(const IndexT &_ridx){
	return *m().service<Service>(_ridx);
}

Service::Service(
	Service::Controller *_pc,
	uint32 _keepalivetout,
	uint32 _sespertkr,
	uint32 _tkrmaxcnt
):d(*(new Data(_keepalivetout, _sespertkr, _tkrmaxcnt))){
	registerObjectType<Talker>(this);
	d.pc = _pc;
}
//---------------------------------------------------------------------
Service::~Service(){
	if(d.pc && d.pc->release()){
		delete d.pc;
	}
	delete &d;
}
//---------------------------------------------------------------------
uint32 Service::keepAliveTimeout()const{
	return d.keepalivetout;
}
//---------------------------------------------------------------------
int Service::sendSignal(
	DynamicPointer<Signal> &_psig,//the signal to be sent
	const ConnectionUid &_rconid,//the id of the process connector
	uint32	_flags
){
	cassert(_rconid.tid < d.tkrvec.size());
	
	Mutex::Locker		lock(serviceMutex());
	IndexT				idx(compute_index(d.tkrvec[_rconid.tid].uid.first));
	Mutex::Locker		lock2(this->mutex(idx));
	Talker				*ptkr(static_cast<Talker*>(this->objectAt(idx)));
	
	cassert(ptkr);
	
	if(ptkr->pushSignal(_psig, _rconid, _flags | SameConnectorFlag)){
		//the talker must be signaled
		if(ptkr->signal(fdt::S_RAISE)){
			Manager::the().raiseObject(*ptkr);
		}
	}
	return OK;
}
//---------------------------------------------------------------------
int Service::basePort()const{
	return d.baseport;
}
//---------------------------------------------------------------------
int Service::doSendSignal(
	DynamicPointer<Signal> &_psig,//the signal to be sent
	const SockAddrPair &_rsap,
	ConnectionUid *_pconid,
	uint32	_flags
){
	
	if(
		_rsap.family() != AddrInfo::Inet4 && 
		_rsap.family() != AddrInfo::Inet6
	) return -1;
	
	Mutex::Locker	lock(serviceMutex());
	
	if(_rsap.family() == AddrInfo::Inet4){
		
		Inet4SockAddrPair 					inaddr(_rsap);
		Session::Addr4PairT					baddr(&inaddr, inaddr.port());
		Data::SessionAddr4MapT::iterator	it(d.sessionaddr4map.find(&baddr));
		
		if(it != d.sessionaddr4map.end()){
		
			vdbgx(Dbg::ipc, "");
			
			ConnectionUid		conid(it->second);
			IndexT				idx(compute_index(d.tkrvec[conid.tid].uid.first));
			Mutex::Locker		lock2(this->mutex(idx));
			Talker				*ptkr(static_cast<Talker*>(this->objectAt(idx)));
			
			cassert(conid.tid < d.tkrvec.size());
			cassert(ptkr);
			
			if(ptkr->pushSignal(_psig, conid, _flags)){
				//the talker must be signaled
				if(ptkr->signal(fdt::S_RAISE)){
					Manager::the().raiseObject(*ptkr);
				}
			}
			if(_pconid){
				*_pconid = conid;
			}
			return OK;
		
		}else{//the connection/session does not exist
			vdbgx(Dbg::ipc, "");
			
			int16	tkrid(allocateTalkerForNewSession());
			IndexT	tkrpos;
			uint32	tkruid;
			
			if(tkrid >= 0){
				//the talker exists
				tkrpos = d.tkrvec[tkrid].uid.first;
				tkruid = d.tkrvec[tkrid].uid.second;
			}else{
				//create new talker
				tkrid = createNewTalker(tkrpos, tkruid);
				if(tkrid < 0){
					tkrid = allocateTalkerForNewSession(true/*force*/);
				}
				tkrpos = d.tkrvec[tkrid].uid.first;
				tkruid = d.tkrvec[tkrid].uid.second;
			}
			
			tkrpos = compute_index(tkrpos);
			Mutex::Locker		lock2(this->mutex(tkrpos));
			Talker				*ptkr(static_cast<Talker*>(this->objectAt(tkrpos)));
			cassert(ptkr);
			Session				*pses(new Session(inaddr, d.keepalivetout));
			ConnectionUid		conid(tkrid);
			
			vdbgx(Dbg::ipc, "");
			ptkr->pushSession(pses, conid);
			d.sessionaddr4map[pses->baseAddr4()] = conid;
			
			ptkr->pushSignal(_psig, conid, _flags);
			
			if(ptkr->signal(fdt::S_RAISE)){
				Manager::the().raiseObject(*ptkr);
			}
			
			if(_pconid){
				*_pconid = conid;
			}
			return OK;
		}
	}else{//inet6
		cassert(false);
		//TODO:
	}
	return OK;
}
//---------------------------------------------------------------------
int Service::allocateTalkerForNewSession(bool _force){
	if(!_force){
		if(d.tkrq.size()){
			int 				rv(d.tkrq.front());
			Data::TalkerStub	&rts(d.tkrvec[rv]);
			++rts.cnt;
			if(rts.cnt == d.sespertkr){
				d.tkrq.pop();
			}
			vdbgx(Dbg::ipc, "non forced allocate talker: "<<rv<<" sessions per talker "<<rts.cnt);
			return rv;
		}
		vdbgx(Dbg::ipc, "non forced allocate talker failed");
		return -1;
	}else{
		int					rv(d.tkrcrt);
		Data::TalkerStub	&rts(d.tkrvec[rv]);
		++rts.cnt;
		cassert(d.tkrq.empty());
		++d.tkrcrt;
		d.tkrcrt %= d.tkrmaxcnt;
		vdbgx(Dbg::ipc, "forced allocate talker: "<<rv<<" sessions per talker "<<rts.cnt);
		return rv;
	}
}
//---------------------------------------------------------------------
int Service::acceptSession(Session *_pses){
	Mutex::Locker	lock(serviceMutex());
	{
		//TODO: see if the locking is ok!!!
		
		Data::SessionAddr4MapT::iterator	it(d.sessionaddr4map.find(_pses->baseAddr4()));
		
		if(it != d.sessionaddr4map.end()){
			//a connection still exists
			IndexT			tkrpos(compute_index(d.tkrvec[it->second.tid].uid.first));
			Mutex::Locker	lock2(this->mutex(tkrpos));
			Talker			*ptkr(static_cast<Talker*>(this->objectAt(tkrpos)));
			
			vdbgx(Dbg::ipc, "");
			
			ptkr->pushSession(_pses, it->second, true);
			
			if(ptkr->signal(fdt::S_RAISE)){
				Manager::the().raiseObject(*ptkr);
			}
			return OK;
		}
	}
	int		tkrid(allocateTalkerForNewSession());
	IndexT	tkrpos;
	uint32	tkruid;
	
	if(tkrid >= 0){
		//the talker exists
		tkrpos = d.tkrvec[tkrid].uid.first;
		tkruid = d.tkrvec[tkrid].uid.second;
	}else{
		//create new talker
		tkrid = createNewTalker(tkrpos, tkruid);
		if(tkrid < 0){
			tkrid = allocateTalkerForNewSession(true/*force*/);
		}
		tkrpos = d.tkrvec[tkrid].uid.first;
		tkruid = d.tkrvec[tkrid].uid.second;
	}
	
	tkrpos = compute_index(tkrpos);
	
	Mutex::Locker	lock2(this->mutex(tkrpos));
	Talker			*ptkr(static_cast<Talker*>(this->objectAt(tkrpos)));
	cassert(ptkr);
	ConnectionUid	conid(tkrid, 0xffff, 0xffff);
	
	vdbgx(Dbg::ipc, "");
	
	ptkr->pushSession(_pses, conid);
	d.sessionaddr4map[_pses->baseAddr4()] = conid;
	
	if(ptkr->signal(fdt::S_RAISE)){
		Manager::the().raiseObject(*ptkr);
	}
	return OK;
}
//---------------------------------------------------------------------
void Service::connectSession(const Inet4SockAddrPair &_raddr){
	Mutex::Locker	lock(serviceMutex());
	int				tkrid(allocateTalkerForNewSession());
	IndexT			tkrpos;
	uint32			tkruid;
	
	if(tkrid >= 0){
		//the talker exists
		tkrpos = d.tkrvec[tkrid].uid.first;
		tkruid = d.tkrvec[tkrid].uid.second;
	}else{
		//create new talker
		tkrid = createNewTalker(tkrpos, tkruid);
		if(tkrid < 0){
			tkrid = allocateTalkerForNewSession(true/*force*/);
		}
		tkrpos = d.tkrvec[tkrid].uid.first;
		tkruid = d.tkrvec[tkrid].uid.second;
	}
	tkrpos = compute_index(tkrpos);
	
	Mutex::Locker		lock2(this->mutex(tkrpos));
	Talker				*ptkr(static_cast<Talker*>(this->objectAt(tkrpos)));
	cassert(ptkr);
	Session				*pses(new Session(_raddr, d.keepalivetout));
	ConnectionUid		conid(tkrid);
	
	vdbgx(Dbg::ipc, "");
	ptkr->pushSession(pses, conid);
	d.sessionaddr4map[pses->baseAddr4()] = conid;
	
	if(ptkr->signal(fdt::S_RAISE)){
		Manager::the().raiseObject(*ptkr);
	}
}
//---------------------------------------------------------------------
void Service::disconnectTalkerSessions(Talker &_rtkr){
	Mutex::Locker	lock(serviceMutex());
	_rtkr.disconnectSessions();
}
//---------------------------------------------------------------------
void Service::disconnectSession(Session *_pses){
	d.sessionaddr4map.erase(_pses->baseAddr4());
	//Use:Context::the().sigctx.connectionuid.tid
	int tkrid(Context::the().sigctx.connectionuid.tid);
	Data::TalkerStub &rts(d.tkrvec[tkrid]);
	--rts.cnt;
	vdbgx(Dbg::ipc, "disconnected session for talker "<<tkrid<<" session count per talker = "<<rts.cnt);
	if(rts.cnt < d.sespertkr){
		d.tkrq.push(tkrid);
	}
}
//---------------------------------------------------------------------
int Service::createNewTalker(IndexT &_tkrpos, uint32 &_tkruid){
	
	if(d.tkrvec.size() >= d.tkrmaxcnt){
		vdbgx(Dbg::ipc, "maximum talker count reached "<<d.tkrvec.size());
		return BAD;
	}
	
	int16			tkrid(d.tkrvec.size());
	
	SocketDevice	sd;
	uint			oldport(d.firstaddr.port());
	
	d.firstaddr.port(0);//bind to any available port
	sd.create(d.firstaddr.family(), AddrInfo::Datagram, 0);
	sd.bind(d.firstaddr);

	if(sd.ok()){
		d.firstaddr.port(oldport);
		vdbgx(Dbg::ipc, "Successful created talker");
		Talker *ptkr(new Talker(sd, *this, tkrid));
		
		ObjectUidT	objuid(this->insertLockless(ptkr));
		d.tkrq.push(d.tkrvec.size());
		d.tkrvec.push_back(objuid);
		d.pc->scheduleTalker(ptkr);
		return tkrid;
	}else{
		edbgx(Dbg::ipc, "Could not bind to random port");
	}
	d.firstaddr.port(oldport);
	return BAD;
}
//---------------------------------------------------------------------
int Service::insertConnection(
	const SocketDevice &_rsd
){
/*	Connection *pcon = new Connection(_pch, 0);
	if(this->insert(*pcon, _serviceid)){
		delete pcon;
		return BAD;
	}
	_rm.pushJob((fdt::tcp::Connection*)pcon);*/
	return OK;
}
//---------------------------------------------------------------------
int Service::insertListener(
	const AddrInfoIterator &_rai
){
/*	test::Listener *plis = new test::Listener(_pst, 100, 0);
	if(this->insert(*plis, _serviceid)){
		delete plis;
		return BAD;
	}	
	_rm.pushJob((fdt::tcp::Listener*)plis);*/
	return OK;
}
//---------------------------------------------------------------------
int Service::insertTalker(
	const AddrInfoIterator &_rai
){	
	SocketDevice	sd;
	sd.create(_rai);
	sd.bind(_rai);
	
	if(!sd.ok()) return BAD;
	SocketAddress sa;
	if(sd.localAddress(sa) != OK){
		return BAD;
	}
	//Mutex::Locker	lock(serviceMutex());
	cassert(!d.tkrvec.size());//only the first tkr must be inserted from outside
	Talker			*ptkr(new Talker(sd, *this, 0));
	
	ObjectUidT		objuid(this->insert(ptkr));
	
	Mutex::Locker	lock(serviceMutex());
	d.firstaddr = _rai;
	d.baseport = sa.port();
	d.tkrvec.push_back(Data::TalkerStub(objuid));
	d.tkrq.push(0);
	d.pc->scheduleTalker(ptkr);
	return OK;
}
//---------------------------------------------------------------------
int Service::insertConnection(
	const AddrInfoIterator &_rai
){
	
/*	Connection *pcon = new Connection(_pch, _node, _svc);
	if(this->insert(*pcon, _serviceid)){
		delete pcon;
		return BAD;
	}
	_rm.pushJob((fdt::tcp::Connection*)pcon);*/
	return OK;
}
//---------------------------------------------------------------------
void Service::insertObject(Talker &_ro, const ObjectUidT &_ruid){
	vdbgx(Dbg::ipc, "inserting talker");
}
//---------------------------------------------------------------------
void Service::eraseObject(const Talker &_ro){
	vdbgx(Dbg::ipc, "erasing talker");
}
//=======	Buffer		=============================================
bool Buffer::check()const{
	cassert(bc >= 32);
	//TODO:
	if(this->pb){
		if(header().size() < sizeof(Header)) return false;
		if(header().size() > ReadCapacity) return false;
		return true;
	}
	return false;
}
//---------------------------------------------------------------------
/*static*/ char* Buffer::allocateDataForReading(){
	return Specific::popBuffer(Specific::capacityToId(ReadCapacity));
}
//---------------------------------------------------------------------
/*static*/ void Buffer::deallocateDataForReading(char *_buf){
	Specific::pushBuffer(_buf, Specific::capacityToId(ReadCapacity));
}
//---------------------------------------------------------------------
void Buffer::clear(){
	if(pb){
		Specific::pushBuffer(pb, Specific::capacityToId(bc));
		pb = NULL;
		bc = 0;
	}
}
//---------------------------------------------------------------------
Buffer::~Buffer(){
	clear();
}
//---------------------------------------------------------------------
void Buffer::optimize(uint16 _cp){
	const uint32	bufsz(this->bufferSize());
	const uint		id(Specific::sizeToId(bufsz));
	const uint		mid(Specific::capacityToId(_cp ? _cp : Buffer::capacityForReading()));
	if(mid > id){
		uint32 datasz = this->dataSize();//the size
		
		char *newbuf(Specific::popBuffer(id));
		memcpy(newbuf, this->buffer(), bufsz);//copy to the new buffer
		
		char *pb = this->release();
		Specific::pushBuffer(pb, mid);
		
		this->pb = newbuf;
		this->dl = datasz;
		this->bc = Specific::idToCapacity(id);
	}
}
//---------------------------------------------------------------------
std::ostream& operator<<(std::ostream &_ros, const Buffer &_rb){
	_ros<<"BUFFER ver = "<<(int)_rb.header().version;
	_ros<<" id = "<<_rb.header().id;
	_ros<<" retransmit = "<<_rb.header().retransid;
	_ros<<" type = ";
	switch(_rb.header().type){
		case Buffer::KeepAliveType: _ros<<"KeepAliveType";break;
		case Buffer::DataType: _ros<<"DataType";break;
		case Buffer::ConnectingType: _ros<<"ConnectingType";break;
		case Buffer::AcceptingType: _ros<<"AcceptingType";break;
		case Buffer::Unknown: _ros<<"Unknown";break;
		default: _ros<<"[INVALID TYPE]";
	}
	_ros<<" buffer_cp = "<<_rb.bc;
	_ros<<" datalen = "<<_rb.dl;
	_ros<<" updatescnt = "<<_rb.header().updatescnt;
	_ros<<" updates [";
	for(int i = 0; i < _rb.header().updatescnt; ++i){
		_ros<<_rb.update(i)<<',';
	}
	_ros<<']';
	return _ros;
	
}

}//namespace ipc
}//namespace foundation
