/* Implementation file aioselector_lin.cpp
	
	Copyright 2007, 2008, 2013 Valentin Palade 
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

//#include <unistd.h>
#include "system/common.hpp"
#include <fcntl.h>
#include <sys/epoll.h>
#include "system/exception.hpp"

#ifndef UPIPESIGNAL
	#ifdef HAS_EVENTFD_H
		#include <sys/eventfd.h>
	#else 
		#define UPIPESIGNAL
	#endif
#endif

#include <vector>
#include <cerrno>
#include <cstring>

#include "system/debug.hpp"
#include "system/timespec.hpp"
#include "system/mutex.hpp"

#include "utility/queue.hpp"
#include "utility/stack.hpp"

#include "frame/object.hpp"
#include "frame/common.hpp"

#include "frame/aio/aioselector.hpp"
#include "frame/aio/aioobject.hpp"

#include "aiosocket.hpp"


namespace solid{
namespace frame{
namespace aio{
//=============================================================
// TODO:
// - investigate if you can use timerfd_create (man timerfd_create)
//
//=============================================================

struct Selector::Stub{
	enum State{
		InExecQueue,
		OutExecQueue
	};
	Stub():
		timepos(TimeSpec::maximum),
		itimepos(TimeSpec::maximum),
		otimepos(TimeSpec::maximum),
		state(OutExecQueue),
		events(0){
	}
	void reset(){
		timepos = TimeSpec::maximum; 
		state = OutExecQueue;
		events = 0;
	}
	ObjectPointerT	objptr;
	TimeSpec		timepos;//object timepos
	TimeSpec		itimepos;//input timepos
	TimeSpec		otimepos;//output timepos
	State			state;
	uint			events;
};

struct Selector::Data{
	enum{
		EPOLLMASK = EPOLLET | EPOLLIN | EPOLLOUT,
		MAXPOLLWAIT = 0x7FFFFFFF,
		EXIT_LOOP = 1,
		FULL_SCAN = 2,
		READ_PIPE = 4,
		MAX_EVENTS_COUNT = 1024 * 2,
	};
	typedef Stack<uint32>			Uint32StackT;
	typedef Queue<uint32>			Uint32QueueT;
	typedef std::vector<Stub>		StubVectorT;
	
	ulong				objcp;
	ulong				objsz;
//	ulong				sockcp;
	ulong				socksz;
	int					selcnt;
	int					epollfd;
	epoll_event 		events[MAX_EVENTS_COUNT];
	StubVectorT			stubs;
	Uint32QueueT		execq;
	Uint32StackT		freestubsstk;
#ifdef UPIPESIGNAL
	int					pipefds[2];
#else
	int					efd;//eventfd
	Mutex				m;
	Uint32QueueT		sigq;//a signal queue
	uint64				efdv;//the eventfd value
#endif
	TimeSpec			ntimepos;//next timepos == next timeout
	TimeSpec			ctimepos;//current time pos
	
//reporting data:
	uint				rep_fullscancount;
	
public://methods:
	Data();
	~Data();
	int computeWaitTimeout()const;
	void addNewSocket();
	epoll_event* eventPrepare(epoll_event &_ev, const uint32 _objpos, const uint32 _sockpos);
	void stub(uint32 &_objpos, uint32 &_sockpos, const epoll_event &_ev);
};
//-------------------------------------------------------------
Selector::Data::Data():
	objcp(0), objsz(0), /*sockcp(0),*/ socksz(0), selcnt(0), epollfd(-1),
	rep_fullscancount(0){
#ifdef UPIPESIGNAL
	pipefds[0] = -1;
	pipefds[1] = -1;
#else
	efd = -1;
	efdv = 0;
#endif
}

Selector::Data::~Data(){
	if(epollfd >= 0){
		close(epollfd);
	}
#ifdef UPIPESIGNAL
	if(pipefds[0] >= 0){
		close(pipefds[0]);
	}
	if(pipefds[1] >= 0){
		close(pipefds[1]);
	}
#else
	close(efd);
#endif
}

int Selector::Data::computeWaitTimeout()const{
	if(ntimepos.isMax()) return -1;//return MAXPOLLWAIT;
	int rv = (ntimepos.seconds() - ctimepos.seconds()) * 1000;
	rv += ((long)ntimepos.nanoSeconds() - ctimepos.nanoSeconds()) / 1000000;
	if(rv < 0) return MAXPOLLWAIT;
	return rv;
}
void Selector::Data::addNewSocket(){
	++socksz;
// 	if(socksz > sockcp){
// 		uint oldcp = sockcp;
// 		sockcp += 64;//TODO: improve!!
// 		epoll_event *pevs = new epoll_event[sockcp];
// 		memcpy(pevs, events, oldcp * sizeof(epoll_event));
// 		delete []events;
// 		for(uint i = 0; i < sockcp; ++i){
// 			pevs[i].events = 0;
// 			pevs[i].data.u64 = 0;
// 		}
//	}
}
inline epoll_event* Selector::Data::eventPrepare(
	epoll_event &_ev, const uint32 _objpos, const uint32 _sockpos
){
	_ev.data.u64 = _objpos;
	_ev.data.u64 <<= 32;
	_ev.data.u64 |= _sockpos;
	return &_ev;
}
void Selector::Data::stub(uint32 &_objpos, uint32 &_sockpos, const epoll_event &_ev){
	_objpos = (_ev.data.u64 >> 32);
	_sockpos = (_ev.data.u64 & 0xffffffff);
}
//=============================================================
Selector::Selector():d(*(new Data)){
}
Selector::~Selector(){
	delete &d;
}
int Selector::init(ulong _cp){
	idbgx(Dbg::aio, "aio::Selector "<<(void*)this);
	cassert(_cp);
	d.objcp = _cp;
	//d.sockcp = _cp;
	
	setCurrentTimeSpecific(d.ctimepos);
	
	//first create the epoll descriptor:
	cassert(d.epollfd < 0);
	d.epollfd = epoll_create(_cp);
	if(d.epollfd < 0){
		edbgx(Dbg::aio, "epoll_create: "<<strerror(errno));
		cassert(false);
		return BAD;
	}
#ifdef UPIPESIGNAL
	//next create the pipefds:
	cassert(d.pipefds[0] < 0 && d.pipefds[1] < 0);
	if(pipe(d.pipefds)){
		edbgx(Dbg::aio, "pipe: "<<strerror(errno));
		cassert(false);
		return BAD;
	}
	
	//make the pipes nonblocking
	fcntl(d.pipefds[0], F_SETFL, O_NONBLOCK);
	fcntl(d.pipefds[1], F_SETFL, O_NONBLOCK);
	
	//register the pipes onto epoll
	epoll_event ev;
	ev.data.u64 = 0;
	ev.events = EPOLLIN | EPOLLPRI;//must be LevelTriggered
	if(epoll_ctl(d.epollfd, EPOLL_CTL_ADD, d.pipefds[0], &ev)){
		edbgx(Dbg::aio, "epoll_ctl: "<<strerror(errno));
		cassert(false);
		return BAD;
	}
#else
	cassert(d.efd < 0);
	d.efd = eventfd(0, EFD_NONBLOCK);
	//register the pipes onto epoll
	epoll_event ev;
	ev.data.u64 = 0;
	ev.events = EPOLLIN | EPOLLPRI;//must be LevelTriggered
	if(epoll_ctl(d.epollfd, EPOLL_CTL_ADD, d.efd, &ev)){
		edbgx(Dbg::aio, "epoll_ctl: "<<strerror(errno));
		cassert(false);
		return BAD;
	}
#endif
	//allocate the events
	//d.events = new epoll_event[d.sockcp];
	for(ulong i = 0; i < Data::MAX_EVENTS_COUNT; ++i){
		d.events[i].events = 0;
		d.events[i].data.u64 = 0L;
	}
	//We need to have the stubs preallocated
	//because of aio::Object::ptimeout
	d.stubs.reserve(d.objcp);
	//add the pipe stub:
	doAddNewStub();
	
	d.ctimepos.set(0);
	d.ntimepos = TimeSpec::maximum;
	d.objsz = 1;
	d.socksz = 1;
	return OK;
}

void Selector::prepare(){
	setCurrentTimeSpecific(d.ctimepos);
}
void Selector::unprepare(){
}

void Selector::raise(uint32 _pos){
#ifdef UPIPESIGNAL
	idbgx(Dbg::aio, "signal connection pipe: "<<_pos<<" this "<<(void*)this);
	write(d.pipefds[1], &_pos, sizeof(uint32));
#else
	idbgx(Dbg::aio, "signal connection evnt: "<<_pos<<" this "<<(void*)this);
	uint64 v(1);
	{
		Locker<Mutex> lock(d.m);
		d.sigq.push(_pos);
	}
	int rv = write(d.efd, &v, sizeof(v));
	cassert(rv == sizeof(v));
#endif
}

ulong Selector::capacity()const{
	return d.objcp;
}
ulong Selector::size() const{
	return d.objsz;
}
bool Selector::empty()const{
	return d.objsz == 1;
}
bool Selector::full()const{
	return d.objsz == d.objcp;
}

bool Selector::push(JobT &_objptr){
	if(full()){
		//NOTE:we cannot increase selvec because, objects keep pointers to Stub structures from vector
		//if we'd use deque instead, we'd have a performance penalty 
		THROW_EXCEPTION("Selector full");
	}
	uint stubpos = doAddNewStub();
	Stub &stub = d.stubs[stubpos];
	
	if(!this->setObjectThread(*_objptr, stubpos)){
		return false;
	}
	
	stub.timepos  = TimeSpec::maximum;
	stub.itimepos = TimeSpec::maximum;
	stub.otimepos = TimeSpec::maximum;
	
	_objptr->doPrepare(&stub.itimepos, &stub.otimepos);
	
	//add events for every socket
	bool fail = false;
	uint failpos = 0;
	{
		epoll_event ev;
		ev.data.u64 = 0L;
		ev.events = 0;
		
		Object::SocketStub *psockstub = _objptr->pstubs;
		for(uint i = 0; i < _objptr->stubcp; ++i, ++psockstub){
			Socket *psock = psockstub->psock;
			if(psock && psock->descriptor() >= 0){
				if(
					epoll_ctl(d.epollfd, EPOLL_CTL_ADD, psock->descriptor(), d.eventPrepare(ev, stubpos, i))
				){
					edbgx(Dbg::aio, "epoll_ctl adding filedesc "<<psock->descriptor()<<" stubpos = "<<stubpos<<" pos = "<<i<<" err = "<<strerror(errno));
					fail = true;
					failpos = i;
					break;
				}else{
					//success adding new
					d.addNewSocket();
					psock->doPrepare();
				}
			}
		}
	}
	if(fail){
		doUnregisterObject(*_objptr, failpos);
		stub.reset();
		d.freestubsstk.push(stubpos);
		return false;
	}else{
		++d.objsz;
		stub.objptr = _objptr;
		stub.objptr->doPrepare(&stub.itimepos, &stub.otimepos);
		vdbgx(Dbg::aio, "pushing object "<<&(*(stub.objptr))<<" on position "<<stubpos);
		stub.state = Stub::InExecQueue;
		d.execq.push(stubpos);
	}
	return true;
}

inline ulong Selector::doExecuteQueue(){
	ulong		flags = 0;
	ulong		qsz(d.execq.size());
	while(qsz){//we only do a single scan:
		const uint pos = d.execq.front();
		d.execq.pop();
		--qsz;
		flags |= doExecute(pos);
	}
	return flags;
}


void Selector::run(){
	static const int	maxnbcnt = 16;
	uint 				flags;
	int					nbcnt = -1;	//non blocking opperations count,
									//used to reduce the number of calls for the system time.
	int 				pollwait = 0;
	
	do{
		flags = 0;
		if(nbcnt < 0){
			d.ctimepos.currentMonotonic();
			nbcnt = maxnbcnt;
		}
		
		if(d.selcnt){
			--nbcnt;
			flags |= doAllIo();
		}
		
		if(flags & Data::READ_PIPE){
			--nbcnt;
			flags |= doReadPipe();
		}
		
		if(d.ctimepos >= d.ntimepos || (flags & Data::FULL_SCAN)){
			nbcnt -= 4;
			flags |= doFullScan();
		}
		
		if(d.execq.size()){
			nbcnt -= d.execq.size();
			flags |= doExecuteQueue();
		}
		
		if(empty()) flags |= Data::EXIT_LOOP;
		
		if(flags || d.execq.size()){
			pollwait = 0;
			--nbcnt;
		}else{
			pollwait = d.computeWaitTimeout();
			vdbgx(Dbg::aio, "pollwait "<<pollwait<<" ntimepos.s = "<<d.ntimepos.seconds()<<" ntimepos.ns = "<<d.ntimepos.nanoSeconds());
			vdbgx(Dbg::aio, "ctimepos.s = "<<d.ctimepos.seconds()<<" ctimepos.ns = "<<d.ctimepos.nanoSeconds());
			nbcnt = -1;
        }
		
		d.selcnt = epoll_wait(d.epollfd, d.events, Data::MAX_EVENTS_COUNT, pollwait);
		vdbgx(Dbg::aio, "epollwait = "<<d.selcnt);
		if(d.selcnt < 0) d.selcnt = 0;
	}while(!(flags & Data::EXIT_LOOP));
}

//-------------------------------------------------------------
ulong Selector::doReadPipe(){
#ifdef UPIPESIGNAL
	enum {BUFSZ = 128, BUFLEN = BUFSZ * sizeof(uint32)};
	uint32		buf[128];
	ulong		rv(0);//no
	long		rsz(0);
	long		j(0);
	long		maxcnt((d.objcp / BUFSZ) + 1);
	Stub		*pstub(NULL);
	
	while((++j <= maxcnt) && ((rsz = read(d.pipefds[0], buf, BUFLEN)) == BUFLEN)){
		for(int i = 0; i < BUFSZ; ++i){
			uint pos(buf[i]);
			if(pos){
				if(pos < d.stubs.size() && !(pstub = &d.stubs[pos])->objptr.empty() && pstub->objptr->notified(S_RAISE)){
					pstub->events |= SIGNALED;
					if(pstub->state == Stub::OutExecQueue){
						d.execq.push(pos);
						pstub->state = Stub::InExecQueue;
					}
				}
			}else rv = Data::EXIT_LOOP;
		}
	}
	if(rsz){
		rsz >>= 2;
		for(int i = 0; i < rsz; ++i){	
			uint pos(buf[i]);
			if(pos){
				if(pos < d.stubs.size() && !(pstub = &d.stubs[pos])->objptr.empty() && pstub->objptr->notified(S_RAISE)){
					pstub->events |= SIGNALED;
					if(pstub->state == Stub::OutExecQueue){
						d.execq.push(pos);
						pstub->state = Stub::InExecQueue;
					}
				}
			}else rv = Data::EXIT_LOOP;
		}
	}
	if(j > maxcnt){
		//dummy read:
		rv = Data::EXIT_LOOP | Data::FULL_SCAN;//scan all filedescriptors for events
		wdbgx(Dbg::aio, "reading pipe dummy");
		while((rsz = read(d.pipefds[0], buf, BUFSZ)) > 0);
	}
	return rv;
#else
	//using eventfd
	int		rv = 0;
	uint64	v = 0;
	bool mustempty = false;
	Stub		*pstub(NULL);
	
	while(read(d.efd, &v, sizeof(v)) == sizeof(v)){
		Locker<Mutex> lock(d.m);
		uint	limiter = 16;
		while(d.sigq.size() && --limiter){
			uint pos(d.sigq.front());
			d.sigq.pop();
			if(pos){
				idbgx(Dbg::aio, "signaling object on pos "<<pos);
				if(pos < d.stubs.size() && (pstub = &d.stubs[pos])->objptr && pstub->objptr->signaled(S_RAISE)){
					idbgx(Dbg::aio, "signaled object on pos "<<pos);
					pstub->events |= SIGNALED;
					if(pstub->state == Stub::OutExecQueue){
						d.execq.push(pos);
						pstub->state = Stub::InExecQueue;
					}
				}
			}else rv = Data::EXIT_LOOP;
		}
		if(limiter == 0){
			//d.sigq.size() != 0
			mustempty = true;
		}else{
			mustempty = false;
		}
	}
	if(mustempty){
		Locker<Mutex> lock(d.m);
		while(d.sigq.size()){
			uint pos(d.sigq.front());
			d.sigq.pop();
			if(pos){
				if(pos < d.stubs.size() && (pstub = &d.stubs[pos])->objptr && pstub->objptr->signaled(S_RAISE)){
					pstub->events |= SIGNALED;
					if(pstub->state == Stub::OutExecQueue){
						d.execq.push(pos);
						pstub->state = Stub::InExecQueue;
					}
				}
			}else rv = Data::EXIT_LOOP;
		}
	}
	return rv;
#endif
}
void Selector::doUnregisterObject(Object &_robj, int _lastfailpos){
	Object::SocketStub *psockstub = _robj.pstubs;
	uint to = _robj.stubcp;
	if(_lastfailpos >= 0){
		to = _lastfailpos + 1;
	}
	for(uint i = 0; i < to; ++i, ++psockstub){
		Socket *psock = psockstub->psock;
		if(psock && psock->descriptor() >= 0){
			check_call(Dbg::aio, 0, epoll_ctl(d.epollfd, EPOLL_CTL_DEL, psock->descriptor(), NULL));
			--d.socksz;
			psock->doUnprepare();
		}
	}
}

inline ulong Selector::doIo(Socket &_rsock, ulong _evs, ulong){
	if(_evs & (EPOLLERR | EPOLLHUP)){
		_rsock.doClear();
		int err(0);
		socklen_t len(sizeof(err));
		int rv = getsockopt(_rsock.descriptor(), SOL_SOCKET, SO_ERROR, &err, &len);
		wdbgx(Dbg::aio, "sock error evs = "<<_evs<<" err = "<<err<<" errstr = "<<strerror(err));
		wdbgx(Dbg::aio, "rv = "<<rv<<" "<<strerror(errno)<<" desc"<<_rsock.descriptor());
		return ERRDONE;
	}
	ulong rv = 0;
	if(_evs & EPOLLIN){
		rv = _rsock.doRecv();
	}
	if(!(rv & ERRDONE) && (_evs & EPOLLOUT)){
		rv |= _rsock.doSend();
	}
	return rv;
}

ulong Selector::doAllIo(){
	ulong		flags = 0;
	TimeSpec	crttout;
	uint32		evs;
	uint32		stubpos;
	uint32		sockpos;
	const ulong	selcnt = d.selcnt;
	for(ulong i = 0; i < selcnt; ++i){
		d.stub(stubpos, sockpos, d.events[i]);
		vdbgx(Dbg::aio, "stubpos = "<<stubpos);
		if(stubpos){
			cassert(stubpos < d.stubs.size());
			Stub				&stub(d.stubs[stubpos]);
			Object::SocketStub	&sockstub(stub.objptr->pstubs[sockpos]);
			Socket				&sock(*sockstub.psock);
			
			cassert(sockpos < stub.objptr->stubcp);
			cassert(stub.objptr->pstubs[sockpos].psock);
			
			vdbgx(Dbg::aio, "io events stubpos = "<<stubpos<<" events = "<<d.events[i].events);
			evs = doIo(sock, d.events[i].events);
			{
				const uint t = sockstub.psock->ioRequest();
				if((sockstub.selevents & Data::EPOLLMASK) != t){
					sockstub.selevents = t;
					
					epoll_event ev;
					ev.events = t | EPOLLET;
					ev.data.u64 = d.events[i].data.u64;
					check_call(Dbg::aio, 0, epoll_ctl(d.epollfd, EPOLL_CTL_MOD, sockstub.psock->descriptor(), &ev));
				}
			}
			if(evs){
				//first mark the socket in connection
				vdbgx(Dbg::aio, "evs = "<<evs<<" indone = "<<INDONE<<" stubpos = "<<stubpos);
				stub.objptr->socketPostEvents(sockpos, evs);
				stub.events |= evs;
				//push channel execqueue
				if(stub.state == Stub::OutExecQueue){
					d.execq.push(stubpos);
					stub.state = Stub::InExecQueue;
				}
			}
		}else{//the pipe stub
			flags |= Data::READ_PIPE;
		}
	}
	return flags;
}
void Selector::doFullScanCheck(Stub &_rstub, const ulong _pos){
	ulong	evs = 0;
	if(d.ctimepos >= _rstub.itimepos){
		evs |= _rstub.objptr->doOnTimeoutRecv(d.ctimepos);
		if(d.ntimepos > _rstub.itimepos){
			d.ntimepos = _rstub.itimepos;
		}
	}else if(d.ntimepos > _rstub.itimepos){
		d.ntimepos = _rstub.itimepos;
	}
	
	if(d.ctimepos >= _rstub.otimepos){
		evs |= _rstub.objptr->doOnTimeoutSend(d.ctimepos);
		if(d.ntimepos > _rstub.otimepos){
			d.ntimepos = _rstub.otimepos;
		}
	}else if(d.ntimepos > _rstub.otimepos){
		d.ntimepos = _rstub.otimepos;
	}
	
	if(d.ctimepos >= _rstub.timepos){
		evs |= TIMEOUT;
	}else if(d.ntimepos > _rstub.timepos){
		d.ntimepos = _rstub.timepos;
	}
	
	if(_rstub.objptr->notified(S_RAISE)){
		evs |= SIGNALED;//should not be checked by objs
	}
	if(evs){
		_rstub.events |= evs;
		if(_rstub.state == Stub::OutExecQueue){
			d.execq.push(_pos);
			_rstub.state = Stub::InExecQueue;
		}
	}
}
ulong Selector::doFullScan(){
	++d.rep_fullscancount;
	idbgx(Dbg::aio, "fullscan count "<<d.rep_fullscancount);
	d.ntimepos = TimeSpec::maximum;
	for(Data::StubVectorT::iterator it(d.stubs.begin()); it != d.stubs.end(); it += 4){
		if(!it->objptr.empty()){
			doFullScanCheck(*it, it - d.stubs.begin());
		}
		if(!(it + 1)->objptr.empty()){
			doFullScanCheck(*(it + 1), it - d.stubs.begin() + 1);
		}
		if(!(it + 2)->objptr.empty()){
			doFullScanCheck(*(it + 2), it - d.stubs.begin() + 2);
		}
		if(!(it + 3)->objptr.empty()){
			doFullScanCheck(*(it + 3), it - d.stubs.begin() + 3);
		}
	}
	return 0;
}
ulong Selector::doExecute(const ulong _pos){
	Stub &stub(d.stubs[_pos]);
	cassert(stub.state == Stub::InExecQueue);
	stub.state = Stub::OutExecQueue;
	ulong	rv(0);
	stub.timepos = TimeSpec::maximum;
	TimeSpec timepos(d.ctimepos);
	ulong evs = stub.events;
	stub.events = 0;
	stub.objptr->doClearRequests();//clears the requests from object to selector
	idbgx(Dbg::aio, "execute object "<<_pos);
	this->associateObjectToCurrentThread(*stub.objptr);
	switch(this->executeObject(*stub.objptr, evs, timepos)){
		case BAD:
			idbgx(Dbg::aio, "BAD: removing the connection");
			d.freestubsstk.push(_pos);
			//unregister all channels
			doUnregisterObject(*stub.objptr);
			stub.objptr->doUnprepare();
			stub.objptr.clear();
			stub.timepos = TimeSpec::maximum;
			--d.objsz;
			rv = Data::EXIT_LOOP;
			break;
		case OK:
			d.execq.push(_pos);
			stub.state = Stub::InExecQueue;
			stub.events |= RESCHEDULED;
		case NOK:
			doPrepareObjectWait(_pos, timepos);
			break;
		case LEAVE:
			d.freestubsstk.push(_pos);
			doUnregisterObject(*stub.objptr);
			stub.timepos = TimeSpec::maximum;
			--d.objsz;
			stub.objptr->doUnprepare();
			stub.objptr.release();
			rv = Data::EXIT_LOOP;
		default:
			cassert(false);
	}
	return rv;
}
void Selector::doPrepareObjectWait(const ulong _pos, const TimeSpec &_timepos){
	Stub &stub(d.stubs[_pos]);
	const int32 * const pend(stub.objptr->reqpos);
	bool mustwait = true;
	vdbgx(Dbg::aio, "stub "<<_pos);
	for(const int32 *pit(stub.objptr->reqbeg); pit != pend; ++pit){
		Object::SocketStub &sockstub(stub.objptr->pstubs[*pit]);
		//sockstub.chnevents = 0;
		const uint8 reqtp = sockstub.requesttype;
		sockstub.requesttype = 0;
		switch(reqtp){
			case Object::SocketStub::IORequest:{
				uint t = sockstub.psock->ioRequest();
				vdbgx(Dbg::aio, "sockstub "<<*pit<<" ioreq "<<t);
				if((sockstub.selevents & Data::EPOLLMASK) != t){
					vdbgx(Dbg::aio, "sockstub "<<*pit);
					epoll_event ev;
					sockstub.selevents = t;
					ev.events = t | EPOLLET;
					check_call(Dbg::aio, 0, epoll_ctl(d.epollfd, EPOLL_CTL_MOD, sockstub.psock->descriptor(), d.eventPrepare(ev, _pos, *pit)));
				}
			}break;
			case Object::SocketStub::RegisterRequest:{
				vdbgx(Dbg::aio, "sockstub "<<*pit<<" regreq");
				/*
					NOTE: may be a good ideea to add RegisterAndIORequest
					Epoll doesn't like sockets that are only created, it signals
					EPOLLET on them.
				*/
				epoll_event ev;
				sockstub.psock->doPrepare();
				sockstub.selevents = 0;
				ev.events = (EPOLLET);
				check_call(Dbg::aio, 0, epoll_ctl(d.epollfd, EPOLL_CTL_ADD, sockstub.psock->descriptor(), d.eventPrepare(ev, _pos, *pit)));
				stub.objptr->socketPostEvents(*pit, OKDONE);
				d.addNewSocket();
				mustwait = false;
			}break;
			case Object::SocketStub::UnregisterRequest:{
				vdbgx(Dbg::aio, "sockstub "<<*pit<<" unregreq");
				if(sockstub.psock->ok()){
					check_call(Dbg::aio, 0, epoll_ctl(d.epollfd, EPOLL_CTL_DEL, sockstub.psock->descriptor(), NULL));
					--d.socksz;
					sockstub.psock->doUnprepare();
					stub.objptr->socketPostEvents(*pit, OKDONE);
					mustwait = false;
				}
			}break;
			default:
				cassert(false);
		}
	}
	if(mustwait){
		//will step here when, for example, the object waits for an external signal.
		if(_timepos < stub.timepos && _timepos != d.ctimepos){
			stub.timepos = _timepos;
		}
		
		if(stub.timepos == d.ctimepos){
			stub.timepos = TimeSpec::maximum;
		}else if(d.ntimepos > stub.timepos){
			d.ntimepos = stub.timepos;
		}
		
		if(d.ntimepos > stub.itimepos){
			d.ntimepos = stub.itimepos;
		}
		if(d.ntimepos > stub.otimepos){
			d.ntimepos = stub.otimepos;
		}
		
	}else if(stub.state != Stub::InExecQueue){
		d.execq.push(_pos);
		stub.state = Stub::InExecQueue;
	}
}

ulong Selector::doAddNewStub(){
	ulong pos = 0;
	if(d.freestubsstk.size()){
		pos = d.freestubsstk.top();
		d.freestubsstk.pop();
	}else{
		size_t cp = d.stubs.capacity();
		pos = d.stubs.size();
		size_t nextsize(fast_padding_size(pos, 2));
		d.stubs.push_back(Stub());
		for(size_t i(pos + 1); i < nextsize; ++i){
			d.stubs.push_back(Stub());
			d.freestubsstk.push(i);
		}
		if(cp != d.stubs.capacity()){
			//we need to reset the aioobject's pointer to timepos
			for(Data::StubVectorT::iterator it(d.stubs.begin()); it != d.stubs.end(); it += 4){
				if(!it->objptr.empty()){
					//TODO: is it ok commenting the following lines?!
					//it->timepos  = TimeSpec::maximum;
					//it->itimepos = TimeSpec::maximum;
					//it->otimepos = TimeSpec::maximum;
					it->objptr->doPrepare(&it->itimepos, &it->otimepos);
				}
				if(!(it + 1)->objptr.empty()){
					//TODO: see above
					(it + 1)->objptr->doPrepare(&(it + 1)->itimepos, &(it + 1)->otimepos);
				}
				if(!(it + 2)->objptr.empty()){
					//TODO: see above
					(it + 2)->objptr->doPrepare(&(it + 2)->itimepos, &(it + 2)->otimepos);
				}
				if(!(it + 3)->objptr.empty()){
					//TODO: see above
					(it + 3)->objptr->doPrepare(&(it + 3)->itimepos, &(it + 3)->otimepos);
				}
			}
		}
	}
	return pos;
}

//-------------------------------------------------------------
}//namespace aio
}//namespace frame
}//namespace solid

