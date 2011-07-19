#include <deque>
#include <queue>
#include <vector>

#include "system/common.hpp"
#include "system/exception.hpp"

#ifdef HAVE_UNORDERED_MAP
#include <unordered_map>
#include <unordered_set>
#else
#include <map>
#endif

#include "system/timespec.hpp"

#include "foundation/object.hpp"
#include "foundation/manager.hpp"
#include "foundation/signal.hpp"
#include "utility/stack.hpp"
#include "utility/queue.hpp"

#include "example/distributed/consensus/core/consensusrequest.hpp"
#include "example/distributed/consensus/server/consensusobject.hpp"
#include "timerqueue.hpp"

namespace fdt=foundation;
using namespace std;

namespace consensus{


/*static*/ Parameters& Parameters::the(Parameters *_p){
	static Parameters &r(*_p);
	return r;
}

Parameters::Parameters(){
	the(this);
}


//========================================================
struct Object::Data{
	struct RequestStub{
		enum{
			InitState = 0,
			WaitProposeState,
			WaitProposeAcceptState,
		};
		enum{
			SignaledEvent = 1,
			TimeoutEvent = 2,
			
		};
		RequestStub(): evs(0), proposeid(-1), timerid(0), state(0){}
		RequestStub(
			DynamicPointer<consensus::RequestSignal> &_rsig
		):sig(_rsig), proposeid(-1), timerid(0), state(0){}
		DynamicPointer<consensus::RequestSignal>	sig;
		uint32										evs;
		uint32										proposeid;
		uint16										timerid;
		uint16										state;
	};
	
	struct ReqCmpEqual{
		bool operator()(const consensus::RequestId* const & _req1, const consensus::RequestId* const & _req2)const;
	};
	struct ReqCmpLess{
		bool operator()(const consensus::RequestId* const & _req1, const consensus::RequestId* const & _req2)const;
	};
	struct ReqHash{
		size_t operator()(const consensus::RequestId* const & _req1)const;
	};
	struct SenderCmpEqual{
		bool operator()(const consensus::RequestId & _req1, const consensus::RequestId& _req2)const;
	};
	struct SenderCmpLess{
		bool operator()(const consensus::RequestId& _req1, const consensus::RequestId& _req2)const;
	};
	struct SenderHash{
		size_t operator()(const consensus::RequestId& _req1)const;
	};
	typedef std::deque<RequestStub>																RequestStubVectorT;
#ifdef HAVE_UNORDERED_MAP
	typedef std::unordered_map<const consensus::RequestId*, size_t, ReqHash, ReqCmpEqual>		RequestStubMapT;
	typedef std::unordered_set<consensus::RequestId, SenderHash, SenderCmpEqual>				SenderSetT;
#else
	typedef std::map<const consensus::RequestId*, size_t, ReqCmpLess>							RequestStubMapT;
	typedef std::set<consensus::RequestId, SenderCmpLess>										SenderSetT;
#endif
	typedef Stack<size_t>																		SizeTStackT;
	typedef Queue<size_t>																		SizeTQueueT;

//methods:
	Data();
	~Data();
	
	size_t insertRequestStub(DynamicPointer<RequestSignal> &_rsig);
	void eraseRequestStub(size_t _idx);
	RequestStub& requestStub(size_t _idx);
	const RequestStub& requestStub(size_t _idx)const;
	bool checkAlreadyReceived(DynamicPointer<RequestSignal> &_rsig);
	bool isCoordinator()const;
	
	
//data:	
	DynamicExecuterT		exe;
	uint32					proposeid;
		
	uint32					acceptid;
	
	int16					coordinatorid;
	
	RequestStubMapT			reqmap;
	RequestStubVectorT		reqvec;
	SizeTQueueT				reqq;
	SizeTStackT				freeposstk;
	SenderSetT				senderset;
	TimerQueue				timerq;
};

inline bool Object::Data::ReqCmpEqual::operator()(
	const consensus::RequestId* const & _req1,
	const consensus::RequestId* const & _req2
)const{
	return *_req1 == *_req2;
}
inline bool Object::Data::ReqCmpLess::operator()(
	const consensus::RequestId* const & _req1,
	const consensus::RequestId* const & _req2
)const{
	return *_req1 < *_req2;
}
inline size_t Object::Data::ReqHash::operator()(
		const consensus::RequestId* const & _req1
)const{
	return _req1->hash();
}
inline bool Object::Data::SenderCmpEqual::operator()(
	const consensus::RequestId & _req1,
	const consensus::RequestId& _req2
)const{
	return _req1.senderEqual(_req2);
}
inline bool Object::Data::SenderCmpLess::operator()(
	const consensus::RequestId& _req1,
	const consensus::RequestId& _req2
)const{
	return _req1.senderLess(_req2);
}
inline size_t Object::Data::SenderHash::operator()(
	const consensus::RequestId& _req1
)const{
	return _req1.senderHash();
}


Object::Data::Data():proposeid(0), acceptid(0), coordinatorid(-2){
	
}
Object::Data::~Data(){
	
}

size_t Object::Data::insertRequestStub(DynamicPointer<RequestSignal> &_rsig){
	if(freeposstk.size()){
		size_t idx(freeposstk.top());
		freeposstk.pop();
		RequestStub &rcr(requestStub(idx));
		rcr.sig = _rsig;
		return idx;
	}else{
		size_t idx(reqvec.size());
		reqvec.push_back(RequestStub(_rsig));
		return idx;
	}
}
void Object::Data::eraseRequestStub(size_t _idx){
	RequestStub &rcr(requestStub(_idx));
	cassert(rcr.sig.ptr());
	rcr.sig.clear();
	rcr.state = 0;
	rcr.evs = 0;
	++rcr.timerid;
	freeposstk.push(_idx);
}
inline Object::Data::RequestStub& Object::Data::requestStub(size_t _idx){
	cassert(_idx < reqvec.size());
	return reqvec[_idx];
}
inline const Object::Data::RequestStub& Object::Data::requestStub(size_t _idx)const{
	cassert(_idx < reqvec.size());
	return reqvec[_idx];
}
inline bool Object::Data::isCoordinator()const{
	return coordinatorid == -1;
}
bool Object::Data::checkAlreadyReceived(DynamicPointer<RequestSignal> &_rsig){
	SenderSetT::iterator it(senderset.find(_rsig->id));
	if(it != senderset.end()){
		if(overflowSafeLess(_rsig->id.requid, it->requid)){
			return true;
		}
		senderset.erase(it);
		senderset.insert(_rsig->id);
	}else{
		senderset.insert(_rsig->id);
	}
	return false;
}

//========================================================
namespace{
static const DynamicRegisterer<Object>	dre;
}

/*static*/ void Object::dynamicRegister(){
	DynamicExecuterT::registerDynamic<RequestSignal, Object>();
	//TODO: add here the other consensus Signals
}
Object::Object():d(*(new Data)){
	
}
//---------------------------------------------------------
Object::~Object(){
	delete &d;
}
//---------------------------------------------------------
void Object::dynamicExecute(DynamicPointer<> &_dp){
	
}
//---------------------------------------------------------
void Object::dynamicExecute(DynamicPointer<RequestSignal> &_rsig){
	if(d.checkAlreadyReceived(_rsig)) return;
// 	++d.acceptid;
// 	doAccept(_rsig);
// 	_rsig.clear();
	//we've received a requestsignal
}
//---------------------------------------------------------
/*virtual*/ bool Object::signal(DynamicPointer<foundation::Signal> &_sig){
	if(this->state() < 0){
		_sig.clear();
		return false;//no reason to raise the pool thread!!
	}
	d.exe.push(DynamicPointer<>(_sig));
	return fdt::Object::signal(fdt::S_SIG | fdt::S_RAISE);
}
//---------------------------------------------------------
int Object::execute(ulong _sig, TimeSpec &_tout){
	foundation::Manager &rm(fdt::m());
	
	if(signaled()){//we've received a signal
		ulong sm(0);
		{
			Mutex::Locker	lock(rm.mutex(*this));
			sm = grabSignalMask(0);//grab all bits of the signal mask
			if(sm & fdt::S_KILL) return BAD;
			if(sm & fdt::S_SIG){//we have signals
				d.exe.prepareExecute();
			}
		}
		if(sm & fdt::S_SIG){//we've grabed signals, execute them
			while(d.exe.hasCurrent()){
				d.exe.executeCurrent(*this);
				d.exe.next();
			}
		}
	}
	switch(state()){
		case Init:		return doInit(_sig, _tout);
		case Run:		return doRun(_sig, _tout);
		case Update:	return doUpdate(_sig, _tout);
	}
	return NOK;
}
//---------------------------------------------------------
bool Object::isCoordinator()const{
	return d.isCoordinator();
}
uint32 Object::acceptId()const{
	return d.acceptid;
}
uint32 Object::proposeId()const{
	return d.proposeid;
}
//---------------------------------------------------------
int Object::doInit(ulong _sig, TimeSpec &_tout){
	state(Run);
	return OK;
}
//---------------------------------------------------------
int Object::doRun(ulong _sig, TimeSpec &_tout){
	//first we scan for timeout:
	while(d.timerq.hitted(_tout)){
		Data::RequestStub &rreq(d.requestStub(d.timerq.frontIndex()));
		
		if(rreq.timerid == d.timerq.frontUid()){
			rreq.evs |= Data::RequestStub::TimeoutEvent;
			if(!(rreq.evs & Data::RequestStub::SignaledEvent)){
				rreq.evs |= Data::RequestStub::SignaledEvent;
				d.reqq.push(d.timerq.frontIndex());
			}
		}
		
		d.timerq.pop();
	}
	
	//next we process all request
	size_t cnt(d.reqq.size());
	while(cnt--){
		size_t pos(d.reqq.front());
		d.reqq.pop();
		doProcessRequest(pos);
	}
	if(d.reqq.size()) return OK;
	
	//set the timer value and exit
	
	return NOK;
}
//---------------------------------------------------------
int Object::doUpdate(ulong _sig, TimeSpec &_tout){
	state(Run);
	return OK;
}

void Object::doProcessRequest(size_t _pos){
	Data::RequestStub &rreq(d.requestStub(_pos));
	switch(rreq.state){
		case Data::RequestStub::InitState:
			break;
		case Data::RequestStub::WaitProposeState:
			break;
		case Data::RequestStub::WaitProposeAcceptState:
			break;
		default:
			THROW_EXCEPTION_EX("Unknown state ",rreq.state);
	}
}
//========================================================
}//namespace consensus
