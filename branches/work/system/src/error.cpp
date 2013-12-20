// system/src/error.cpp
//
// Copyright (c) 2013,2014 Valentin Palade (vipalade @ gmail . com) 
//
// This file is part of SolidFrame framework.
//
// Distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt.
//

#include "system/error.hpp"
#include "system/thread.hpp"

namespace solid{

ERROR_NS::error_code last_system_error(){
#ifdef ON_WINDOWS
	const DWORD err = GetLastError();
	return ERROR_NS::error_code(err, ERROR_NS::system_category());
#else
	return ERROR_NS::error_code(errno, ERROR_NS::system_category());
#endif
}

ERROR_NS::error_category const	&error_category_get(){
	//TODO: implement an error_category
	return ERROR_NS::generic_category();
}

void specific_error_clear(){
	Thread::current().specificErrorClear();
}

void specific_error_push(
	int _value,
	ERROR_NS::error_category const	*_category,
	unsigned _line = -1,
	const char *_file = NULL
){
	Thread::current().specificErrorPush(ErrorStub(_value, _category, _line, _file));
}

void specific_error_push(
	ERROR_NS::error_code const	&_code,
	unsigned _line = -1,
	const char *_file = NULL
){
	Thread::current().specificErrorPush(ErrorStub(_code, _line, _file));
}


ErrorVectorT const & specific_error_get(){
	return Thread::current().specificErrorGet();
}
void specific_error_print(std::ostream &_ros, const bool _withcodeinfo = true){
	const ErrorVectorT &rerrvec = Thread::current().specificErrorGet();
	for(ErrorVectorT::const_reverse_iterator it(rerrvec.rbegin()); it != rerrvec.rend(); ++it){
		ERROR_NS::error_code err = it->errorCode();
		_ros<<err.category().name();
		if(_withcodeinfo){
			_ros<<'('<<it->file<<':'<<it->line<<')';
		}
		_ros<<':'<<' '<<err.message()<<';'<<' ';
	}
}

}//namespace solid
