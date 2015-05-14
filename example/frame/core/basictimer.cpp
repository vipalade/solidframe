#include "frame/manager.hpp"
#include "frame/scheduler.hpp"
#include "frame/reactor.hpp"
#include "frame/object.hpp"
#include "frame/timer.hpp"
#include "frame/service.hpp"

#include "system/condition.hpp"
#include "system/mutex.hpp"
#include "system/cassert.hpp"
#include "system/debug.hpp"

#include <iostream>


using namespace solid;
using namespace std;


namespace {
	Condition	cnd;
	bool		running = true;
	Mutex		mtx;
}

enum Events{
	EventStartE = 0,
	EventRunE,
	EventStopE,
	EventSendE,
};

typedef frame::Scheduler<frame::Reactor>	SchedulerT;

class BasicObject: public Dynamic<BasicObject, frame::Object>{
public:
	BasicObject(size_t _repeat = 10):repeat(_repeat), t1(proxy()), t2(proxy()){}
private:
	/*virtual*/ void onEvent(frame::ReactorContext &_rctx, frame::Event const &_revent);
	void onTimer(frame::ReactorContext &_rctx, size_t _idx);
private:
	size_t			repeat;
	frame::Timer	t1;
	frame::Timer	t2;
};

int main(int argc, char *argv[]){
#ifdef UDEBUG
	{
	string dbgout;
	Debug::the().levelMask("view");
	Debug::the().moduleMask("all");
	
	Debug::the().initStdErr(
		false,
		&dbgout
	);
	
	cout<<"Debug output: "<<dbgout<<endl;
	dbgout.clear();
	Debug::the().moduleNames(dbgout);
	cout<<"Debug modules: "<<dbgout<<endl;
	}
#endif

	{
		SchedulerT			s;
		
		frame::Manager		m;
		frame::Service		svc(m, frame::Event(EventStopE));
		
		if(!s.start(1)){
			const size_t	cnt = argc == 2 ? atoi(argv[1]) : 1;
			
			for(size_t i = 0; i < cnt; ++i){
				DynamicPointer<frame::Object>	objptr(new BasicObject(10));
				solid::ErrorConditionT			err;
				solid::frame::ObjectUidT		objuid;
				
				objuid = s.startObject(objptr, svc, frame::Event(EventStartE), err);
				idbg("Started BasicObject: "<<objuid.index<<','<<objuid.unique);
			}
			
			{
				Locker<Mutex>	lock(mtx);
				while(running){
					cnd.wait(lock);
				}
			}
		}else{
			cout<<"Error starting scheduler"<<endl;
		}
		m.stop();
		
	}
	Thread::waitAll();
	return 0;
}

/*virtual*/ void BasicObject::onEvent(frame::ReactorContext &_rctx, frame::Event const &_revent){
	idbg("event = "<<_revent.id);
	if(_revent.id == EventStartE){
		t1.waitUntil(_rctx, _rctx.time() + 5 * 1000, [this](frame::ReactorContext &_rctx){return onTimer(_rctx, 0);});
		t2.waitUntil(_rctx, _rctx.time() + 10 * 1000, [this](frame::ReactorContext &_rctx){return onTimer(_rctx, 1);});
	}else if(_revent.id == EventStopE){
		postStop(_rctx);
	}
}

void BasicObject::onTimer(frame::ReactorContext &_rctx, size_t _idx){
	idbg("timer = "<<_idx);
	if(_idx == 0){
		if(repeat--){
			t2.cancel(_rctx);
			t1.waitUntil(_rctx, _rctx.time() + 1000 * 5, [this](frame::ReactorContext &_rctx){return onTimer(_rctx, 0);}); 
			cassert(!_rctx.error());
			t2.waitUntil(_rctx, _rctx.time() + 1000 * 10, [this](frame::ReactorContext &_rctx){return onTimer(_rctx, 1);});
			cassert(!_rctx.error());
		}else{
			t2.cancel(_rctx);
			Locker<Mutex>	lock(mtx);
			running = false;
			cnd.signal();
			postStop(_rctx);
		}
	}else if(_idx == 1){
		cout<<"ERROR: second timer should never fire"<<endl;
		cassert(false);
	}else{
		cout<<"ERROR: unknown timer index: "<<_idx<<endl;
		cassert(false);
	}
}

/*virtual*/ /*void BasicObject::execute(frame::ExecuteContext &_rexectx){
	switch(_rexectx.event().id){
		case frame::EventInit:
			cout<<"EventInit("<<_rexectx.event().index<<") at "<<_rexectx.time()<<endl;
			//t1 should fire first
			t1.waitUntil(_rexectx, _rexectx.time() + 1000 * 5, frame::EventTimer, 1); 
			cassert(!_rexectx.error());
			t2.waitUntil(_rexectx, _rexectx.time() + 1000 * 10, frame::EventTimer, 2);
			cassert(!_rexectx.error());
			break;
		case frame::EventTimer:
			cout<<"EventTimer("<<_rexectx.event().index<<") at "<<_rexectx.time()<<endl;
			if(_rexectx.event().index == 1){
				if(repeat--){
					t2.cancel(_rexectx);
					t1.waitUntil(_rexectx, _rexectx.time() + 1000 * 5, frame::EventTimer, 1); 
					cassert(!_rexectx.error());
					t2.waitUntil(_rexectx, _rexectx.time() + 1000 * 10, frame::EventTimer, 2);
					cassert(!_rexectx.error());
				}else{
					t2.cancel(_rexectx);
					Locker<Mutex>	lock(mtx);
					running = false;
					cnd.signal();
				}
			}else if(_rexectx.event().index == 2){
				cout<<"ERROR: second timer should never fire"<<endl;
				cassert(false);
			}else{
				cout<<"ERROR: unknown timer index"<<endl;
				cassert(false);
			}
		case frame::EventDie:
			_rexectx.die();
			break;
		default:
			break;
	}
	if(_rexectx.error()){
		_rexectx.die();
	}
}*/
