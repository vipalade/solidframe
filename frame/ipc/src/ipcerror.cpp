// frame/ipc/src/ipcerror.cpp
//
// Copyright (c) 2015 Valentin Palade (vipalade @ gmail . com) 
//
// This file is part of SolidFrame framework.
//
// Distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt.
//

#include "frame/ipc/ipcerror.hpp"
#include <sstream>

namespace solid{
namespace frame{
namespace ipc{

namespace{
enum {
	ErrorInactivityTimeoutE = 1,
	ErrorTooManyKAPacketsReceivedE,
	ErrorConnectionKilledE,
	ErrorLibraryLogicE
};
class ErrorCategory: public ErrorCategoryT
{     
public: 
	const char* name() const noexcept{
		return "frame::ipc";
	}
	std::string message(int _ev)const;
};

const ErrorCategory category;


std::string ErrorCategory::message(int _ev) const{
	std::ostringstream oss;
	
	oss<<"("<<name()<<":"<<_ev<<"): ";
	switch(_ev){
		case 0:
			oss<<"Success";
			break;
		case ErrorInactivityTimeoutE:
			oss<<"Timeout due to inactivity";
			break;
		case ErrorTooManyKAPacketsReceivedE:
			oss<<"Received too many KeepAlive packets";
			break;
		case ErrorConnectionKilledE:
			oss<<"Connection forcefully killed";
			break;
		case ErrorLibraryLogicE:
			oss<<"Library logic error";
			break;
		default:
			oss<<"Unknown";
			break;
	}
	return oss.str();
}

}//namespace


/*extern*/ const ErrorConditionT error_inactivity_timeout(ErrorInactivityTimeoutE, category);
/*extern*/ const ErrorConditionT error_too_many_keepalive_packets_received(ErrorTooManyKAPacketsReceivedE, category);
/*extern*/ const ErrorConditionT error_connection_killed(ErrorConnectionKilledE, category);
/*extern*/ const ErrorConditionT error_library_logic(ErrorLibraryLogicE, category);

}//namespace ipc
}//namespace frame
}//namespace solid


