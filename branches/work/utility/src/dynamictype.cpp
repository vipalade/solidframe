#include "system/thread.hpp"
#include "system/debug.hpp"
#include "system/cassert.hpp"
#include "system/mutex.hpp"

#include "system/mutualstore.hpp"
#include "utility/dynamictype.hpp"
#include "utility/dynamicpointer.hpp"
#include "utility/shared.hpp"

#include <vector>

#ifdef HAS_GNU_ATOMIC
#include <ext/atomicity.h>
#endif

#include "system/atomic.hpp"

namespace solid{

namespace{
#ifdef HAS_SAFE_STATIC
size_t specificId(){
	static const size_t id(Thread::specificId());
	return id;
}

#else

MutexStoreT &mutexStoreStub(){
	static MutexStoreT		mtxstore(3, 2, 2);
	return mtxstore;
}

uint32 specificIdStub(){
	static const uint32 id(Thread::specificId());
	return id;
}


void once_cbk_store(){
	mutexStoreStub();
}

void once_cbk_specific(){
	specificIdStub();
}

MutexStoreT &mutexStore(){
	static boost::once_flag once = BOOST_ONCE_INIT;
	boost::call_once(&once_cbk_store, once);
	return mutexStoreStub();
}

uint32 specificId(){
	static boost::once_flag once = BOOST_ONCE_INIT;
	boost::call_once(&once_cbk_specific, once);
	return specificIdStub();
}
	

#endif

//---------------------------------------------------------------------
//----	DynamicPointer	----
//---------------------------------------------------------------------

void DynamicPointerBase::clear(DynamicBase *_pdyn){
	cassert(_pdyn);
	if(!_pdyn->release()) delete _pdyn;
}

void DynamicPointerBase::use(DynamicBase *_pdyn){
	_pdyn->use();
}

void DynamicPointerBase::storeSpecific(void *_pv)const{
	Thread::specific(specificId(), _pv);
}

/*static*/ void* DynamicPointerBase::fetchSpecific(){
	return Thread::specific(specificId());
}

//--------------------------------------------------------------------
//		DynamicBase
//--------------------------------------------------------------------

typedef ATOMIC_NS::atomic<size_t>			AtomicSizeT;

/*static*/ size_t DynamicBase::generateId(){
	static AtomicSizeT u(ATOMIC_VAR_INIT(0));
	return u.fetch_add(1, ATOMIC_NS::memory_order_relaxed);
}


DynamicBase::~DynamicBase(){}

/*virtual*/ void DynamicBase::use(){
	idbgx(Debug::utility, "DynamicBase");
}

//! Used by DynamicPointer to know if the object must be deleted
/*virtual*/ int DynamicBase::release(){
	idbgx(Debug::utility, "DynamicBase");
	return 0;
}

/*virtual*/ bool DynamicBase::isTypeDynamic(uint32 _id)const{
	return false;
}

//--------------------------------------------------------------------
//		DynamicSharedImpl
//--------------------------------------------------------------------


void DynamicSharedImpl::doUse(){
	idbgx(Debug::utility, "DynamicSharedImpl");
#ifdef HAS_GNU_ATOMIC
	__gnu_cxx:: __atomic_add_dispatch(&usecount, 1);
#else
	Locker<Mutex>	lock(this->mutex());
	++usecount;
#endif
}

int DynamicSharedImpl::doRelease(){
	idbgx(Debug::utility, "DynamicSharedImpl");
#ifdef HAS_GNU_ATOMIC
	const int rv = __gnu_cxx::__exchange_and_add_dispatch(&usecount, -1) - 1;
#else
	Locker<Mutex>	lock(this->mutex());
	--usecount;
	const int rv = usecount;
#endif
	return rv;
}

//--------------------------------------------------------------------
//--------------------------------------------------------------------


}//namespace solid
