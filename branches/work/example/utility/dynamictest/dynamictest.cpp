#include <iostream>
#include "system/debug.hpp"
#include "utility/dynamictyph.hpp"

using namespace std;
using namespace solid;

struct AObject: Dynamic<AObject, DynamicShared<> >{
	AObject(int _v):v(_v){}
	int v;
};

struct BObject: Dynamic<BObject>{
	BObject(int _v1, int _v2):v1(_v1), v2(_v2){}
	int v1;
	int v2;
};

struct CObject: Dynamic<CObject, BObject>{
	CObject(int _v1, int _v2, int _v3):Dynamic<CObject, BObject>(_v1, _v2), v3(_v3){}
	int v3;
};

struct DObject: Dynamic<DObject, AObject>{
	DObject(const string& _s, int _v): Dynamic<DObject, AObject>(_v), s(_s){}
	string s;
};

struct Context{
	uint32 idx;
};

class FirstHandler{
protected:
	typedef DynamicMapper<int, FirstHandler, Context>	DynamicMapperT;
	
	typedef DynamicHandler<DynamicMapperT>				DynamicHandlerT;
public:
	FirstHandler(){
	}
	static void init(){
		dm.registerDynamic<AObject, FirstHandler>();
		dm.registerDynamic<BObject, FirstHandler>();
	}
	void push(const DynamicPointer<> &_dp){
		dh.push(dm, *this, _dp);
	}
	int dynamicHandle(DynamicPointer<> &_dp);
	int dynamicHandle(const DynamicPointer<AObject> &_rdp);
	int dynamicHandle(const DynamicPointer<BObject> &_rdp);
	
protected:
	static DynamicMapperT	dm;
	DynamicHandlerT			dh;
};

/*static*/ FirstHandler::DynamicMapperT FirstHandler::dm;

int FirstHandler::dynamicHandle(DynamicPointer<> &_dp){
	idbg("");
	return -1;
}

int FirstHandler::dynamicHandle(const DynamicPointer<AObject> &_dp){
	idbg("v = "<<_dp->v);
	return _dp->v;
}

int FirstHandler::dynamicHandle(const DynamicPointer<BObject> &_dp){
	idbg("v1 = "<<_dp->v1<<" v2 = "<<_dp->v2);
	return _dp->v2;
}

class SecondHandler: public FirstHandler{
public:
	SecondHandler(){
	}
	
	static void init(){
		FirstHandler::init();
		dm.registerDynamic<BObject, SecondHandler>();
		dm.registerDynamic<CObject, SecondHandler>();
		dm.registerDynamic<DObject, SecondHandler>();
	}
	
	void run();
	
	int dynamicHandle(const DynamicPointer<BObject> &_dp);
	int dynamicHandle(const DynamicPointer<CObject> &_dp);
	int dynamicHandle(const DynamicPointer<DObject> &_dp);
};


void SecondHandler::run(){
	int rv = dh.prepareHandle(dm, *this);
	idbg("Executing "<<rv<<" calls");
	while(dh.hasCurrent(dm, *this)){
		rv = dh.handleCurrent(dm, *this);
		idbg("call returned "<<rv);
		dh.next(dm, *this);
	}
	//dr.executeCurrent(*this);
}

int SecondHandler::dynamicHandle(const DynamicPointer<BObject> &_dp){
	idbg("v1 = "<<_dp->v1<<" v2 = "<<_dp->v2);
	return _dp->v1;
}
int SecondHandler::dynamicHandle(const DynamicPointer<CObject> &_dp){
	idbg("v1 = "<<_dp->v1<<" v2 = "<<_dp->v2<<" v3 "<<_dp->v3);
	return _dp->v1;
}
int SecondHandler::dynamicHandle(const DynamicPointer<DObject> &_dp){
	idbg("s = "<<_dp->s<<" v = "<<_dp->v);
	return _dp->v;
}

int main(){
#ifdef UDEBUG
	Debug::the().levelMask("view");
	Debug::the().moduleMask("any");
	Debug::the().initStdErr(false);
#endif
	
	{
		DynamicSharedPointer<AObject> 	dsap(new AObject(1));
		DynamicSharedPointer<>			dsbp;
		dsbp = dsap;
		{
			DynamicPointer<AObject>			dap(dsap);
			DynamicSharedPointer<>			dsaap(dap);
			idbg("ptr = "<<(void*)dap.get()<<" ptr = "<<(void*)dsaap.get());
		}
		idbg("ptr = "<<(void*)dsap.get()<<" ptr = "<<(void*)dsbp.get());
	}
	
	SecondHandler::init();
	
	SecondHandler	h;
	
	h.push(DynamicPointer<>(new AObject(1)));
	h.push(DynamicPointer<>(new BObject(2,3)));
	h.push(DynamicPointer<>(new CObject(1,2,3)));
	h.push(DynamicPointer<>(new DObject("hello1", 4)));
	
	h.run();
	
	h.push(DynamicPointer<>(new AObject(11)));
	h.push(DynamicPointer<>(new BObject(22,33)));
	h.push(DynamicPointer<>(new CObject(11,22,33)));
	h.push(DynamicPointer<>(new DObject("hello2", 44)));
	
	h.run();
	
	h.push(DynamicPointer<>(new AObject(111)));
	h.push(DynamicPointer<>(new BObject(222,333)));
	h.push(DynamicPointer<>(new CObject(111,222,333)));
	h.push(DynamicPointer<>(new DObject("hello3", 444)));
	
	h.run();
	
	return 0;
}
