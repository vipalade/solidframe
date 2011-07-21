/* Implementation file specific.cpp
	
	Copyright 2007, 2008 Valentin Palade 
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

#include <stack>
#include <vector>
#include "system/common.hpp"
#include "system/cassert.hpp"
#include "system/specific.hpp"
#include "system/thread.hpp"
#include "system/debug.hpp"
#include "system/mutex.hpp"

#ifndef OBJ_CACHE_CAP
#define OBJ_CACHE_CAP 4096*2
#endif

static const unsigned specificPosition(){
	//TODO: staticproblem
	static const unsigned	thrspecpos = Thread::specificId();
	return thrspecpos;
}
//static unsigned		stkid = 0;
struct CleanVector: std::vector<Specific::FncT>{
	CleanVector(){
		this->reserve(OBJ_CACHE_CAP);
	}
};

static CleanVector	cv;

typedef std::stack<void*,std::vector<void*> > StackT;

//This is what is holded on a thread
struct SpecificData{
	struct CachePoint{
		CachePoint():cp(0){}
		typedef std::stack<char*, std::vector<char*> > CStackT;
		CStackT		s;
		uint32			cp;
	};
	struct ObjectCachePoint{
		ObjectCachePoint(Specific::FncT _pf = NULL):ps(NULL),cp(0){}
		StackT		*ps;
		uint32		cp;
	};
	typedef std::vector<ObjectCachePoint>	ObjCachePointVecT;
	SpecificData(SpecificCacheControl *_pcc);
	~SpecificData();
	inline static SpecificData& current(){
		return *reinterpret_cast<SpecificData*>(Thread::specific(specificPosition()));
	}
	CachePoint 				cps[Specific::Count];
	ObjCachePointVecT		ops;
	SpecificCacheControl	*pcc;
};

struct BasicCacheControl:SpecificCacheControl{
	/*virtual*/ bool release();
};

//----------------------------------------------------------------------------------------------------
void SpecificObject::operator delete (void *_pv, std::size_t _sz){
	unsigned id = Specific::sizeToId(_sz);
	if(id != (unsigned)-1){
		char * pc = (char*)_pv;
		Specific::pushBuffer(pc, id);
	}else{
		delete []reinterpret_cast<char*>(_pv);
	}
}
void* SpecificObject::operator new (std::size_t _sz){
	unsigned id = Specific::sizeToId(_sz);
	if(id != (unsigned)-1){
		return Specific::popBuffer(id);
	}else{
		return new char[_sz];
	}
}
//----------------------------------------------------------------------------------------------------
// 32 * 4096 per every id
unsigned SpecificCacheControl::stackCapacity(unsigned _bufid)const{
	return 32 * 4096 / (1 << _bufid);
}
bool BasicCacheControl::release(){
	return false;//do not delete this object
}
//****************************************************************************************************
//		SpecificData
//****************************************************************************************************
SpecificData::SpecificData(SpecificCacheControl *_pcc):pcc(_pcc){
	ops.reserve(OBJ_CACHE_CAP);
}
SpecificData::~SpecificData(){
	if(pcc->release()) delete pcc;
	idbgx(Dbg::specific, "destroy all cached buffers");
	for(int i(0); i < Specific::Count; ++i){
		vdbgx(Dbg::specific, i<<" cp = "<<cps[i].cp<<" sz = "<<cps[i].s.size()<<" specific_id = "<<Specific::sizeToId((1<<i)));
		cassert(!(cps[i].cp - cps[i].s.size()));
		while(cps[i].s.size()){
			delete []cps[i].s.top();
			cps[i].s.pop();
		}
	}
	
	idbgx(Dbg::specific, "destroy all cached objects");
	Mutex::Locker lock(Thread::gmutex());
	for(ObjCachePointVecT::iterator it(ops.begin()); it != ops.end(); ++it){
		vdbgx(Dbg::specific, "it->cp = "<<it->cp);
		if(it->ps){
			cassert(!(it->cp - it->ps->size()));
			while(it->ps->size()){
				(*cv[it - ops.begin()])(it->ps->top());
				it->ps->pop();
			}
			delete it->ps;
		}
	}
}
//****************************************************************************************************
//		Specific
//****************************************************************************************************
static BasicCacheControl bcc;

void destroy(void *_pv){
	SpecificData *psd = reinterpret_cast<SpecificData*>(_pv);
	delete psd;
}
/*static*/ void Specific::prepareThread(SpecificCacheControl *_pcc){
	if(!_pcc) _pcc = static_cast<SpecificCacheControl*>(&bcc);
	Thread::specific(specificPosition(), new SpecificData(_pcc), &destroy);
}
//----------------------------------------------------------------------------------------------------
/*static*/ unsigned Specific::sizeToId(unsigned _sz){
	if(_sz > 4096)	return -1;
	if(_sz > 2048)	return 10;
	if(_sz > 1024)	return 9;
	if(_sz > 512)	return 8;
	if(_sz > 256)	return 7;
	if(_sz > 128)	return 6;
	if(_sz > 64)	return 5;
	if(_sz > 32)	return 4;
	if(_sz > 16)	return 3;
	if(_sz > 8)		return 2;
	if(_sz > 4)		return 1;
	return 0;
}
/*static*/ unsigned Specific::capacityToId(unsigned _sz){
	switch(_sz){
		case 4:		return 0;
		case 8:		return 1;
		case 16:	return 2;
		case 32:	return 3;
		case 64:	return 4;
		case 128:	return 5;
		case 256:	return 6;
		case 512:	return 7;
		case 1024:	return 8;
		case 2048:	return 9;
		case 4096:	return 10;
		default:{ cassert(false);}
	}
}
/*static*/ char* Specific::popBuffer(unsigned _id){
	cassert(_id < Count);
	cassert(_id != 0);
	SpecificData &rsd(SpecificData::current());
	SpecificData::CachePoint &rcp(rsd.cps[_id]);
	char *tb;
	if(rcp.s.size()){
		tb = rcp.s.top();
		rcp.s.pop();
	}else{
		++rcp.cp;
		tb = new char[idToCapacity(_id)];
	}
	idbgx(Dbg::specific,"popBuffer "<<_id<<" cp "<<rcp.cp<<' '<<(void*)tb);
	return tb;
}
/*static*/ void Specific::pushBuffer(char *&_pb, unsigned _id){
	cassert(_pb);
	cassert(_id < Count);
	SpecificData &rsd(SpecificData::current());
	SpecificData::CachePoint &rcp(rsd.cps[_id]);
	idbgx(Dbg::specific,"pushBuffer "<<_id<<" cp "<<rcp.cp<<" stackCap "<<rsd.pcc->stackCapacity(_id)<<' '<<(void*)_pb);
	if(rcp.s.size() < rsd.pcc->stackCapacity(_id)){
		rcp.s.push(_pb);
	}else{
		--rcp.cp;
		delete []_pb;
	}
	_pb = NULL;
}
//----------------------------------------------------------------------------------------------------
//for caching objects
/*static*/ unsigned Specific::stackid(FncT _pf){
	Mutex::Locker lock(Thread::gmutex());
	SpecificData &rsd(SpecificData::current());
	cassert(rsd.ops.size() < rsd.ops.capacity());
	cv.push_back(_pf);
	return cv.size() - 1;
}
/*static*/ int Specific::push(void *_pv, unsigned _id, unsigned _maxcp){
	SpecificData &rsd(SpecificData::current());
	cassert(_pv);
	cassert(_id < rsd.ops.size());
	cassert(rsd.ops[_id].ps);
	if(rsd.ops[_id].ps->size() < _maxcp){
		rsd.ops[_id].ps->push(_pv);
		return 0;
	}
	--rsd.ops[_id].cp;
	return -1;
}
/*static*/ void* Specific::pop(unsigned _id){
	SpecificData &rsd(SpecificData::current());
	if(_id >= rsd.ops.size()){
		rsd.ops.resize(_id + 1);
		rsd.ops[_id].ps = new StackT;
	}else if(rsd.ops[_id].ps){
		if(rsd.ops[_id].ps->size()){
			void *pv = rsd.ops[_id].ps->top();
			rsd.ops[_id].ps->pop();
			return pv;
		}
	}else{
		rsd.ops[_id].ps = new StackT;
	}
	++rsd.ops[_id].cp;
	return NULL;
}
