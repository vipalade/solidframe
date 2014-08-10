// frame/schedulerbase.hpp
//
// Copyright (c) 2007, 2008 Valentin Palade (vipalade @ gmail . com) 
//
// This file is part of SolidFrame framework.
//
// Distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt.
//
#ifndef SOLID_FRAME_SCHEDULER_BASE_HPP
#define SOLID_FRAME_SCHEDULER_BASE_HPP

#include "frame/common.hpp"
#include "utility/functor.hpp"

namespace solid{
namespace frame{

class Manager;
class ReactorBase;
class ObjectBase;

//! A base class for all schedulers
class SchedulerBase{
protected:
	typedef bool (*CreateWorkerF)(SchedulerBase &_rsch);
	
	typedef FunctorReference<bool, ReactorBase&>	ScheduleFunctorT;
	
	bool doStart(CreateWorkerF _pf, size_t _reactorcnt = 1, size_t _reactorchunkcp = 1024);

	void doStop(bool _wait = true);
	
	bool doSchedule(ObjectBase &_robj, ScheduleFunctorT &_rfct);
protected:
	SchedulerBase(
		Manager &_rm
	);
	~SchedulerBase();
private:
	friend class ReactorBase;
	
	Manager& manager();
	
	bool prepareThread(ReactorBase &_rsel);
	void unprepareThread(ReactorBase &_rsel);
	bool update(ReactorBase &_rsel);
private:
	struct Data;
	Data	&d;
};

}//namespace frame
}//namespace solid

#endif
