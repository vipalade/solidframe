// frame/file/src/filemanager.cpp
//
// Copyright (c) 2010 Valentin Palade (vipalade @ gmail . com) 
//
// This file is part of SolidFrame framework.
//
// Distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt.
//
#include <deque>
#include <vector>

#include "system/debug.hpp"
#include "system/timespec.hpp"
#include "system/filedevice.hpp"
#include "system/mutex.hpp"
#include "system/thread.hpp"
#include "system/timespec.hpp"

#include "system/mutualstore.hpp"
#include "utility/iostream.hpp"
#include "utility/queue.hpp"
#include "utility/stack.hpp"


#include "frame/manager.hpp"

#include "frame/file/filemanager.hpp"
#include "frame/file/filekeys.hpp"
#include "frame/file/filebase.hpp"
#include "frame/file/filemapper.hpp"


namespace solid{
namespace frame{
namespace file{

//------------------------------------------------------------------

struct Manager::Data{
	enum {Running = 1, Stopping, Stopped = -1};
	
	typedef MutualStore<Mutex>		MutexStoreT;
	typedef Stack<IndexT	>		FreeStackT;
	struct FileData{
		FileData(
			File *_pfile = NULL
		):pfile(_pfile), /*toutidx(-1), */uid(0), tout(TimeSpec::maximum),inexecq(false), events(0){}
		void clear(){
			pfile = NULL;
			++uid;
			tout = TimeSpec::maximum;
			inexecq = false;
		}
		File		*pfile;
		//int32		toutidx;
		uint32		uid;
		TimeSpec	tout;
		bool		inexecq;
		uint16		events;
	};
	template <class Str>
	struct SendStreamData{
		SendStreamData(){}
		SendStreamData(
			const StreamPointer<Str> &_rs,
			const FileUidT	&_rfuid,
			const RequestUid _rrequid
		):s(_rs), fuid(_rfuid), requid(_rrequid){}
		StreamPointer<Str>	s;
		FileUidT			fuid;
		RequestUid			requid;
	};
	
	typedef SendStreamData<OutputStream>		SendOutputStreamDataT;
	typedef SendStreamData<InputStream>		SendInputStreamDataT;
	typedef SendStreamData<InputOutputStream>	SendInputOutputStreamDataT;
	typedef std::pair<int, RequestUid>	SendErrorDataT;
	
	typedef std::deque<FileData>		FileVectorT;
	typedef std::deque<int32>			TimeoutVectorT;
	typedef std::vector<Mapper*>		MapperVectorT;
	typedef Queue<uint32>				Index32QueueT;
	typedef Queue<IndexT>				IndexQueueT;
	typedef Queue<File*>				FileQueueT;
	typedef Queue<SendOutputStreamDataT>		SendOutputStreamQueueT;
	typedef Queue<SendInputOutputStreamDataT>	SendInputOutputStreamQueueT;
	typedef Queue<SendInputStreamDataT>		SendInputStreamQueueT;
	typedef Queue<SendErrorDataT>		SendErrorQueueT;
	
	
	Data(Controller *_pc):pc(_pc), sz(0), mtxstore(0, 3, 5){}
	~Data(){}
	void pushFileInTemp(File *_pf);
	int state()const{
		return st;
	}
	void state(int _st){
		st = _st;
	}
//data:
	Controller						*pc;//pointer to controller
	uint32							sz;
	int								st;
//	Mutex							*mtx;
	MutexStoreT						mtxstore;
	FileVectorT						fv;//file vector
	MapperVectorT					mv;//mapper vector
	Index32QueueT					meq;//mapper execution queue
	FileQueueT						feq;//file execution q
	IndexQueueT						tmpfeq;//temp file execution q
	IndexQueueT						delfq;//file delete  q
	FreeStackT						fs;//free stack
	TimeSpec						tout;
	SendOutputStreamQueueT			sndosq;
	SendInputOutputStreamQueueT		sndiosq;
	SendInputStreamQueueT			sndisq;
	SendErrorQueueT					snderrq;
};
//------------------------------------------------------------------
void Manager::Data::pushFileInTemp(File *_pf){
	IndexT idx;
	if(_pf->isRegistered()){
		idx = _pf->id();
	}else{
		if(fs.size()){
			idx = fs.top();fs.pop();
			Locker<Mutex> lock(mtxstore.safeAt(idx));
			fv[idx].pfile = _pf;
		}else{
			idx = fv.size();
			Locker<Mutex> lock(mtxstore.safeAt(idx));
			fv.push_back(FileData(_pf));
		}
		_pf->id(idx);
		++sz;
	}
	FileData &rfd(fv[idx]);
	if(!rfd.inexecq){
		rfd.inexecq = true;
		tmpfeq.push(idx);
	}
}
//------------------------------------------------------------------
Manager::Controller::~Controller(){
}
//------------------------------------------------------------------
//------------------------------------------------------------------

Manager::Manager(Controller *_pc):d(*(new Data(_pc))){
	_pc->init(InitStub(*this));
	d.state(Data::Running);
}

Manager::~Manager(){
	idbgx(Debug::file, "");
	for(Data::MapperVectorT::const_iterator it(d.mv.begin()); it != d.mv.end(); ++it){
		delete *it;
	}
	if(d.pc && d.pc->release()){
		delete d.pc;
	}
	delete &d;
}
//------------------------------------------------------------------

void Manager::execute(ExecuteContext &_rexectx){
	Mutex &rmtx = frame::Manager::specific().mutex(*this);
	rmtx.lock();
	//idbgx(Debug::file, "signalmask "<<_evs);
	if(notified()){
		ulong sm = grabSignalMask(0);
		//idbgx(Debug::file, "signalmask "<<sm);
		if(sm & frame::S_KILL){
			d.state(Data::Stopping);
			vdbgx(Debug::file, "kill "<<d.sz);
			if(!d.sz){//no file
				d.state(-1);
				rmtx.unlock();
				vdbgx(Debug::file, "");
				_rexectx.close();
				return;
			}
			doPrepareStop();
		}
	}
	Stub s(*this);
	
	//copy the queued files into a temp queue - to be used outside lock
	while(d.feq.size()){
		d.pushFileInTemp(d.feq.front());
		d.feq.pop();
	}
	
	doDeleteFiles();
	
	rmtx.unlock();//done with the locking
	
	doScanTimeout(_rexectx.currentTime());
	
	doExecuteMappers();
	
	uint32 tmpfeqsz(d.tmpfeq.size());
	
	while(tmpfeqsz--){
		IndexT idx(d.tmpfeq.front());
		
		d.tmpfeq.pop();
		
		doExecuteFile(idx, _rexectx.currentTime());
	}
	doSendStreams();
	if(d.meq.size() || d.delfq.size() || d.tmpfeq.size()){
		_rexectx.reschedule();
		return;
	}
	
	if(!d.sz && d.state() == Data::Stopping){
		d.state(-1);
		_rexectx.close();
		return;
	}
	
	if(d.tout > _rexectx.currentTime()){
		_rexectx.waitUntil(d.tout);
	}
	return;
}
//------------------------------------------------------------------
void Manager::doSendStreams(){
	while(d.sndisq.size()){
		d.pc->sendStream(
			d.sndisq.front().s,
			d.sndisq.front().fuid,
			d.sndisq.front().requid
		);
		d.sndisq.pop();
	}
	while(d.sndiosq.size()){
		d.pc->sendStream(
			d.sndiosq.front().s,
			d.sndiosq.front().fuid,
			d.sndiosq.front().requid
		);
		d.sndiosq.pop();
	}
	while(d.sndosq.size()){
		d.pc->sendStream(
			d.sndosq.front().s,
			d.sndosq.front().fuid,
			d.sndosq.front().requid
		);
		d.sndosq.pop();
	}
	while(d.snderrq.size()){
		d.pc->sendError(
			d.snderrq.front().first,
			d.snderrq.front().second
		);
		d.snderrq.pop();
	}
}
//------------------------------------------------------------------
void Manager::doPrepareStop(){
	idbgx(Debug::file, "");
	//call stop for all mappers:
	for(Data::MapperVectorT::const_iterator it(d.mv.begin()); it != d.mv.end(); ++it){
		(*it)->stop();
	}
	//empty the mapper q
	while(d.meq.size()) d.meq.pop();
	
	//move the incomming files to temp, before iterating through all the files
	while(d.feq.size()){
		d.pushFileInTemp(d.feq.front());
		d.feq.pop();
	}
	//signall all existing files to stop;
	for(Data::FileVectorT::iterator it(d.fv.begin()); it != d.fv.end(); ++it){
		Data::FileData &rfd(*it);
		if(!rfd.pfile) continue;
		rfd.events |= File::MustDie;
		if(!rfd.inexecq){
			rfd.inexecq = true;
			d.tmpfeq.push(rfd.pfile->id());
		}
	}
}
//------------------------------------------------------------------
void Manager::doExecuteMappers(){
	//idbgx(Debug::file, "");
	uint32	tmpqsz(d.meq.size());
	Stub 	s(*this);
	cassert(!(tmpqsz && d.state() != Data::Running));
	//execute the enqued mappers
	while(tmpqsz--){
		const ulong 	v(d.meq.front());
		Mapper			*pm(d.mv[v]);
		
		d.meq.pop();
		pm->execute(s);
	}
}
//------------------------------------------------------------------
void Manager::doExecuteFile(const IndexT &_idx, const TimeSpec &_rtout){
	idbgx(Debug::file, "");
	Mutex			&rm(d.mtxstore.at(_idx));
	Data::FileData	&rfd(d.fv[_idx]);
	TimeSpec 		ts(_rtout);
	Stub 			s(*this);
	uint16			evs(rfd.events);
	
	rfd.tout = TimeSpec::maximum;
	rfd.inexecq = false;
	rfd.events = 0;
	
	switch(rfd.pfile->execute(s, evs, ts, rm)){
		case File::Bad:
			d.delfq.push(_idx);
			/*NOTE:
				The situation is spooky:
				- the file is closed but it is not unregistered from the mapper
				- we cannot close the file in the doDeleteFiles method, because
				closing may mean flushing an will take a lot of time.
				- so there will be a method tryRevive in file, which 
				will revive the file if there are incomming requests.
			*/
			break;
		case File::Ok:
			d.tmpfeq.push(_idx);
			rfd.inexecq = true;
			break;//reschedule
		case File::No:
			if(ts != _rtout){
				rfd.tout = ts;
				if(d.tout > rfd.tout){
					d.tout = rfd.tout;
				}
			}else{
				rfd.tout = TimeSpec::maximum;
			}
			break;
	}
}
//------------------------------------------------------------------
void Manager::doDeleteFiles(){
	Stub	s(*this);
	
	while(d.delfq.size()){
		const IndexT	idx(d.delfq.front());
		Data::FileData	&rfd(d.fv[d.delfq.front()]);
		File			*pfile;
		
		d.delfq.pop();
		
		{
			
			//Locker<Mutex> lock(d.mtxstore.at(idx));
			
			if(rfd.pfile->tryRevive()){
				if(!rfd.inexecq){
					rfd.inexecq = true;
					d.tmpfeq.push(idx);
				}
				continue;
			}
			d.fs.push(rfd.pfile->id());
			--d.sz;
			pfile = rfd.pfile;
			rfd.clear();
		}
		
		uint32 mid(pfile->key().mapperId());
		//mapper creates - mapper destroys files
		if(s.mapper(mid).erase(pfile)){
			cassert(d.state() == Data::Running);
			d.meq.push(mid);
		}
	}
}
//------------------------------------------------------------------
void Manager::doScanTimeout(const TimeSpec &_rtout){
	if(_rtout < d.tout) return;
	d.tout = TimeSpec::maximum;
	for(Data::FileVectorT::iterator it(d.fv.begin()); it != d.fv.end(); ++it){
		Data::FileData &rfd(*it);
		if(rfd.pfile){
			if(rfd.tout <= _rtout){
				if(!rfd.inexecq){
					rfd.inexecq = true;
					d.tmpfeq.push(rfd.pfile->id());
				}
				rfd.events |= File::Timeout;
			}else if(d.tout > rfd.tout){
				d.tout = rfd.tout;
			}
		}
	}
}
//------------------------------------------------------------------
void Manager::init(Mutex *_pmtx){
}
//------------------------------------------------------------------
void Manager::releaseInputStream(IndexT _fileid){
	bool b = false;
	{
		Locker<Mutex> lock(d.mtxstore.at(_fileid));
		b = d.fv[_fileid].pfile->decreaseInCount();
	}
	if(b){
		Mutex 			&rmtx = frame::Manager::specific().mutex(*this);
		Locker<Mutex>	lock(rmtx);
		//we must signal the filemanager
		d.feq.push(d.fv[_fileid].pfile);
		vdbgx(Debug::file, "sq.push "<<_fileid);
		//if(static_cast<fdt::Object*>(this)->signal((int)fdt::S_RAISE)){
		if(this->notify(frame::S_RAISE)){
			frame::Manager::specific().raise(*this);
		}
	}
}
//------------------------------------------------------------------
void Manager::releaseOutputStream(IndexT _fileid){
	bool b = false;
	{
		Locker<Mutex> lock(d.mtxstore.at(_fileid));
		b = d.fv[_fileid].pfile->decreaseOutCount();
	}
	if(b){
		Mutex			&rmtx = frame::Manager::specific().mutex(*this);
		Locker<Mutex>	lock(rmtx);
		//we must signal the filemanager
		d.feq.push(d.fv[_fileid].pfile);
		vdbgx(Debug::file, "sq.push "<<_fileid);
		if(this->notify(frame::S_RAISE)){
			frame::Manager::specific().raise(*this);
		}
	}
}
//------------------------------------------------------------------
void Manager::releaseInputOutputStream(IndexT _fileid){
	releaseOutputStream(_fileid);
}
//------------------------------------------------------------------
int Manager::fileWrite(
	IndexT _fileid,
	const char *_pb,
	uint32 _bl,
	const int64 &_off,
	uint32 _flags
){
	Locker<Mutex> lock(d.mtxstore.at(_fileid));
	return d.fv[_fileid].pfile->write(_pb, _bl, _off, _flags);
}
//------------------------------------------------------------------
int Manager::fileRead(
	IndexT _fileid,
	char *_pb,
	uint32 _bl,
	const int64 &_off,
	uint32 _flags
){
	Locker<Mutex> lock(d.mtxstore.at(_fileid));
	int rv = d.fv[_fileid].pfile->read(_pb, _bl, _off, _flags);
	if(rv == 0){
		vdbgx(Debug::file, ""<<_fileid<<" "<<(void*)_pb<<' '<<_bl<<' '<<_off<<' '<<rv<<' '<<d.fv[_fileid].pfile->size());
	}else{
		vdbgx(Debug::file, ""<<_fileid<<" "<<(void*)_pb<<' '<<_bl<<' '<<_off<<' '<<rv);
	}
	return rv;
}
//------------------------------------------------------------------
int64 Manager::fileSize(IndexT _fileid){
	Locker<Mutex> lock(d.mtxstore.at(_fileid));
	return d.fv[_fileid].pfile->size();
}
//===================================================================
// stream method - the basis for all stream methods
template <typename StreamP>
int Manager::doGetStream(
	StreamP &_sptr,
	FileUidT &_rfuid,
	const RequestUid &_requid,
	const Key &_rk,
	uint32 _flags
){
	Mutex 			&rmtx = frame::Manager::specific().mutex(*this);
	Locker<Mutex>	lock1(rmtx);
	
	if(d.state() != Data::Running) return AsyncError;
	
	Stub			s(*this);
	ulong			mid(_rk.mapperId());
	Mapper			&rm(*d.mv[mid]);
	File			*pf = rm.findOrCreateFile(_rk);
	int				rv(AsyncError);
	
	if(!pf) return AsyncError;
	
	if(pf->isRegistered()){//
		const IndexT	fid = pf->id();
		Locker<Mutex>	lock2(d.mtxstore.at(fid));
		_rfuid.first = fid;
		_rfuid.second = d.fv[fid].uid; 
		rv = pf->stream(s, _sptr, _requid, _flags);
		//idbgx(Debug::file, ""<<_rk.path());
	}else{
		//delay stream creation until successfull file open
		rv = pf->stream(s, _sptr, _requid, _flags | Manager::ForcePending);
		//idbgx(Debug::file, ""<<_rk.path());
	}
	
	switch(rv){
		case File::MustSignal:
			d.feq.push(pf);
			if(this->notify(frame::S_RAISE)){
				frame::Manager::specific().raise(*this);
			}
		case File::MustWait:
			return AsyncWait;
		default: return rv;
	}
}
//===================================================================
// Most general stream methods
int Manager::stream(
	StreamPointer<InputStream> &_sptr,
	FileUidT &_rfuid,
	const RequestUid &_requid,
	const Key &_rk,
	uint32 _flags
){
	if(d.state() != Data::Running) return AsyncError;
	return doGetStream(_sptr, _rfuid, _requid, _rk, _flags);
}
int Manager::stream(
	StreamPointer<OutputStream> &_sptr,
	FileUidT &_rfuid,
	const RequestUid &_requid,
	const Key &_rk,
	uint32 _flags
){	
	if(d.state() != Data::Running) return AsyncError;
	return doGetStream(_sptr, _rfuid, _requid, _rk, _flags);
}

int Manager::stream(
	StreamPointer<InputOutputStream> &_sptr,
	FileUidT &_rfuid,
	const RequestUid &_requid,
	const Key &_rk,
	uint32 _flags
){	
	if(d.state() != Data::Running) return AsyncError;
	return doGetStream(_sptr, _rfuid, _requid, _rk, _flags);
}


//====================== stream method proxies =========================
// proxies for the above methods
int Manager::stream(
	StreamPointer<InputStream> &_sptr,
	const RequestUid &_rrequid,
	const char *_fn,
	uint32 _flags
){
	FileUidT fuid;
	return stream(_sptr, fuid, _rrequid, _fn, _flags);
}

int Manager::stream(
	StreamPointer<OutputStream> &_sptr,
	const RequestUid &_rrequid,
	const char *_fn,
	uint32 _flags
){
	FileUidT fuid;
	return stream(_sptr, fuid, _rrequid, _fn, _flags);
}

int Manager::stream(
	StreamPointer<InputOutputStream> &_sptr,
	const RequestUid &_rrequid,
	const char *_fn,
	uint32 _flags
){
	FileUidT fuid;
	return stream(_sptr, fuid, _rrequid, _fn, _flags);
}
//---------------------------------------------------------------
int Manager::streamSpecific(
	StreamPointer<InputStream> &_sptr,
	const char *_fn,
	uint32 _flags
){
	return stream(_sptr, *requestuidptr, _fn, _flags);
}
int Manager::streamSpecific(
	StreamPointer<OutputStream> &_sptr,
	const char *_fn,
	uint32 _flags
){
	return stream(_sptr, *requestuidptr, _fn, _flags);
}
int Manager::streamSpecific(
	StreamPointer<InputOutputStream> &_sptr,
	const char *_fn,
	uint32 _flags
){
	return stream(_sptr, *requestuidptr, _fn, _flags);
}
//---------------------------------------------------------------
int Manager::stream(
	StreamPointer<InputStream> &_sptr,
	FileUidT &_rfuid,
	const RequestUid &_rrequid,
	const char *_fn,
	uint32 _flags
){
	if(_fn){
		FastNameKey k(_fn);
		return stream(_sptr, _rfuid, _rrequid, k, _flags);
	}
	return AsyncError;
}

int Manager::stream(
	StreamPointer<OutputStream> &_sptr,
	FileUidT &_rfuid,
	const RequestUid &_rrequid,
	const char *_fn,
	uint32 _flags
){
	if(_fn){
		FastNameKey k(_fn);
		return stream(_sptr, _rfuid, _rrequid, k, _flags);
	}
	return AsyncError;
}

int Manager::stream(
	StreamPointer<InputOutputStream> &_sptr,
	FileUidT &_rfuid,
	const RequestUid &_rrequid,
	const char *_fn,
	uint32 _flags
){
	if(_fn){
		FastNameKey k(_fn);
		return stream(_sptr, _rfuid, _rrequid, k, _flags);
	}
	return AsyncError;
}
//---------------------------------------------------------------
int Manager::streamSpecific(
	StreamPointer<InputStream> &_sptr,
	FileUidT &_rfuid,
	const char *_fn,
	uint32 _flags
){
	return stream(_sptr, _rfuid, *requestuidptr, _fn, _flags);
}
int Manager::streamSpecific(
	StreamPointer<OutputStream> &_sptr,
	FileUidT &_rfuid,
	const char *_fn,
	uint32 _flags
){
	return stream(_sptr, _rfuid, *requestuidptr, _fn, _flags);
}
int Manager::streamSpecific(
	StreamPointer<InputOutputStream> &_sptr,
	FileUidT &_rfuid,
	const char *_fn,
	uint32 _flags
){
	return stream(_sptr, _rfuid, *requestuidptr, _fn, _flags);
}
//---------------------------------------------------------------
int Manager::stream(StreamPointer<InputStream> &_sptr, const char *_fn, uint32 _flags){
	FileUidT	fuid;
	RequestUid	requid;
	if(_fn){
		FastNameKey k(_fn);
		return stream(_sptr, fuid, requid, k, _flags | NoWait);
	}
	return AsyncError;
}

int Manager::stream(StreamPointer<OutputStream> &_sptr, const char *_fn, uint32 _flags){
	FileUidT	fuid;
	RequestUid	requid;
	if(_fn){
		FastNameKey k(_fn);
		return stream(_sptr, fuid, requid, k, _flags | NoWait);
	}
	return AsyncError;
}

int Manager::stream(StreamPointer<InputOutputStream> &_sptr, const char *_fn, uint32 _flags){
	FileUidT	fuid;
	RequestUid	requid;
	if(_fn){
		FastNameKey k(_fn);
		return stream(_sptr, fuid, requid, k, _flags | NoWait);
	}
	return AsyncError;
}
//-------------------------------------------------------------------------------------
int Manager::stream(StreamPointer<InputStream> &_sptr, const Key &_rk, uint32 _flags){
	FileUidT	fuid;
	RequestUid	requid;
	return stream(_sptr, fuid, requid, _rk, _flags | NoWait);
}

int Manager::stream(StreamPointer<OutputStream> &_sptr, const Key &_rk, uint32 _flags){
	FileUidT	fuid;
	RequestUid	requid;
	return stream(_sptr, fuid, requid, _rk, _flags | NoWait);
}

int Manager::stream(StreamPointer<InputOutputStream> &_sptr, const Key &_rk, uint32 _flags){
	FileUidT	fuid;
	RequestUid	requid;
	return stream(_sptr, fuid, requid, _rk, _flags | NoWait);
}
//=======================================================================
int Manager::stream(
	StreamPointer<InputStream> &_sptr,
	const FileUidT &_rfuid,
	const RequestUid &_requid,
	uint32 _flags
){
	Mutex			&rmtx = frame::Manager::specific().mutex(*this);
	Locker<Mutex>	lock1(rmtx);
	if(_rfuid.first < d.fv.size() && d.fv[_rfuid.first].uid == _rfuid.second){
		Locker<Mutex>	lock2(d.mtxstore.at(_rfuid.first));
		Data::FileData	&rfd(d.fv[_rfuid.first]);
		if(rfd.pfile){
			Stub	s(*this);
			return rfd.pfile->stream(s, _sptr, _requid, _flags);
		}
	}
	return AsyncError;
}

int Manager::stream(
	StreamPointer<OutputStream> &_sptr,
	const FileUidT &_rfuid,
	const RequestUid &_requid,
	uint32 _flags
){
	Mutex			&rmtx = frame::Manager::specific().mutex(*this);
	Locker<Mutex>	lock1(rmtx);
	if(_rfuid.first < d.fv.size() && d.fv[_rfuid.first].uid == _rfuid.second){
		Locker<Mutex>	lock2(d.mtxstore.at(_rfuid.first));
		Data::FileData	&rfd(d.fv[_rfuid.first]);
		if(rfd.pfile){
			Stub	s(*this);
			return rfd.pfile->stream(s, _sptr, _requid, _flags);
		}
	}
	return AsyncError;
}

int Manager::stream(
	StreamPointer<InputOutputStream> &_sptr,
	const FileUidT &_rfuid,
	const RequestUid &_requid,
	uint32 _flags
){
	Mutex			&rmtx = frame::Manager::specific().mutex(*this);
	Locker<Mutex>	lock1(rmtx);
	if(_rfuid.first < d.fv.size() && d.fv[_rfuid.first].uid == _rfuid.second){
		Locker<Mutex>	lock2(d.mtxstore.at(_rfuid.first));
		Data::FileData	&rfd(d.fv[_rfuid.first]);
		if(d.fv[_rfuid.first].pfile){
			Stub	s(*this);
			return rfd.pfile->stream(s, _sptr, _requid, _flags);
		}
	}
	return AsyncError;
}
//=======================================================================
// The internal implementation of streams streams
//=======================================================================

class FileInputStream: public InputStream{
public:
	FileInputStream(Manager &_rm, IndexT _fileid):rm(_rm), fileid(_fileid), off(0){}
	~FileInputStream();
	int read(char *, uint32, uint32 _flags = 0);
	int read(uint64 _offset, char*, uint32, uint32 _flags = 0);
	int release();
	int64 seek(int64, SeekRef);
	int64 size()const;
private:
	Manager			&rm;
	const IndexT	fileid;
	int64			off;
};
//------------------------------------------------------------------------------
class FileOutputStream: public OutputStream{
public:
	FileOutputStream(Manager &_rm, IndexT _fileid):rm(_rm), fileid(_fileid), off(0){}
	~FileOutputStream();
	int write(const char *, uint32, uint32 _flags = 0);
	int write(uint64 _offset, const char *_pbuf, uint32 _blen, uint32 _flags = 0);
	int release();
	int64 seek(int64, SeekRef);
	int64 size()const;
private:
	Manager			&rm;
	const IndexT	fileid;
	int64			off;
};
//------------------------------------------------------------------------------
class FileInputOutputStream: public InputOutputStream{
public:
	FileInputOutputStream(Manager &_rm, IndexT _fileid):rm(_rm), fileid(_fileid), off(0){}
	~FileInputOutputStream();
	int read(char *, uint32, uint32 _flags = 0);
	int read(uint64 _offset, char*, uint32, uint32 _flags = 0);
	int write(const char *, uint32, uint32 _flags = 0);
	int write(uint64 _offset, const char *_pbuf, uint32 _blen, uint32 _flags = 0);
	int release();
	int64 seek(int64, SeekRef);
	int64 size()const;
private:
	Manager			&rm;
	const IndexT	fileid;
	int64			off;
};


//--------------------------------------------------------------------------
// FileInputStream
FileInputStream::~FileInputStream(){
	rm.releaseInputStream(fileid);
}

int FileInputStream::read(char * _pb, uint32 _bl, uint32 _flags){
	int rv = rm.fileRead(fileid, _pb, _bl, off, _flags);
	if(rv > 0) off += rv;
	return rv;
}
int FileInputStream::read(uint64 _offset, char* _pb, uint32 _bl, uint32 _flags){
	return rm.fileRead(fileid, _pb, _bl, _offset, _flags);
}
int FileInputStream::release(){	
	return AsyncError;
}

int64 FileInputStream::seek(int64 _off, SeekRef _ref){
	cassert(_ref == SeekBeg);
	off = _off;
	return off;
}
int64 FileInputStream::size()const{
	return rm.fileSize(fileid);
}

//--------------------------------------------------------------------------
// FileOutputStream
FileOutputStream::~FileOutputStream(){
	rm.releaseOutputStream(fileid);
}
int  FileOutputStream::write(const char *_pb, uint32 _bl, uint32 _flags){
	int rv = rm.fileWrite(fileid, _pb, _bl, off, _flags);
	if(rv > 0) off += rv;
	return rv;
}
int FileOutputStream::write(uint64 _offset, const char* _pb, uint32 _bl, uint32 _flags){
	return rm.fileWrite(fileid, _pb, _bl, _offset, _flags);
}
int FileOutputStream::release(){
	return -1;
}

int64 FileOutputStream::seek(int64 _off, SeekRef _ref){
	cassert(_ref == SeekBeg);
	off = _off;
	return off;
}

int64 FileOutputStream::size()const{
	return rm.fileSize(fileid);
}

//--------------------------------------------------------------------------
// FileInputOutputStream
FileInputOutputStream::~FileInputOutputStream(){
	rm.releaseInputOutputStream(fileid);
}

int FileInputOutputStream::read(char * _pb, uint32 _bl, uint32 _flags){
	int rv = rm.fileRead(fileid, _pb, _bl, off, _flags);
	if(rv > 0) off += rv;
	return rv;
}

int  FileInputOutputStream::write(const char *_pb, uint32 _bl, uint32 _flags){
	int rv = rm.fileWrite(fileid, _pb, _bl, off, _flags);
	if(rv > 0) off += rv;
	return rv;
}

int FileInputOutputStream::read(uint64 _offset, char*_pb, uint32 _bl, uint32 _flags){
	return rm.fileRead(fileid, _pb, _bl, _offset, _flags);
}
int FileInputOutputStream::write(uint64 _offset, const char* _pb, uint32 _bl, uint32 _flags){
	return rm.fileWrite(fileid, _pb, _bl, _offset, _flags);
}

int FileInputOutputStream::release(){
	return -1;
}

int64 FileInputOutputStream::seek(int64 _off, SeekRef _ref){
	cassert(_ref == SeekBeg);
	off = _off;
	return off;
}

int64 FileInputOutputStream::size()const{
	return rm.fileSize(fileid);
}

//=======================================================================
// The Manager stub:
//=======================================================================
InputStream* Manager::Stub::createInputStream(IndexT _fileid){
	return new FileInputStream(rm, _fileid);
}
OutputStream* Manager::Stub::createOutputStream(IndexT _fileid){
	return new FileOutputStream(rm, _fileid);
}
InputOutputStream* Manager::Stub::createInputOutputStream(IndexT _fileid){
	return new FileInputOutputStream(rm, _fileid);
}
void Manager::Stub::pushFileTempExecQueue(const IndexT &_idx, uint16 _evs){
	Data::FileData &rfd(rm.d.fv[_idx]);
	rfd.events |= _evs;
	if(!rfd.inexecq){
		rm.d.tmpfeq.push(_idx);
		rfd.inexecq = true;
	}
}
Mapper &Manager::Stub::mapper(ulong _id){
	cassert(_id < rm.d.mv.size());
	return *rm.d.mv[_id];
}
uint32	Manager::Stub::fileUid(IndexT _uid)const{
	return rm.d.fv[_uid].uid;
}
Manager::Controller& Manager::Stub::controller(){
	return *rm.d.pc;
}
void Manager::Stub::push(
	const StreamPointer<InputStream> &_rsp,
	const FileUidT &_rfuid,
	const RequestUid &_rrequid
	
){
	rm.d.sndisq.push(Manager::Data::SendInputStreamDataT(_rsp, _rfuid, _rrequid));
}
void Manager::Stub::push(
	const StreamPointer<InputOutputStream> &_rsp,
	const FileUidT &_rfuid,
	const RequestUid &_rrequid
){
	rm.d.sndiosq.push(Manager::Data::SendInputOutputStreamDataT(_rsp, _rfuid, _rrequid));
}
void Manager::Stub::push(
	const StreamPointer<OutputStream> &_rsp,
	const FileUidT &_rfuid,
	const RequestUid &_rrequid
){
	rm.d.sndosq.push(Manager::Data::SendOutputStreamDataT(_rsp, _rfuid, _rrequid));
}
void Manager::Stub::push(
	int _err,
	const RequestUid& _rrequid
){
	rm.d.snderrq.push(Manager::Data::SendErrorDataT(_err, _rrequid));
}
//=======================================================================
uint32 Manager::InitStub::registerMapper(Mapper *_pm)const{
	rm.d.mv.push_back(_pm);
	_pm->id(rm.d.mv.size() - 1);
	return rm.d.mv.size() - 1;
}
//=======================================================================

}//namespace file
}//namespace frame
}//namespace solid


