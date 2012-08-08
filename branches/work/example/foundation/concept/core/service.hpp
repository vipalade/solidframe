/* Declarations file service.hpp
	
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

#ifndef TESTSERVICE_HPP
#define TESTSERVICE_HPP

#include "utility/dynamictype.hpp"
#include "foundation/service.hpp"
#include "signals.hpp"
#include "common.hpp"

struct ResolveIterator;
struct SocketDevice;

namespace foundation{
namespace aio{
namespace openssl{
class Context;
}
}
}

namespace concept{

class Manager;
class Visitor;

class Listener;
class Talker;


class Service: public Dynamic<Service, foundation::Service>{
public:
	enum{
		AddListener = 0,
		AddSslListener,
		AddConnection,
		AddSslConnection,
		AddTalker
	};//use with SocketAddressInfoSignal
	
	static void dynamicRegister();
	
	Service();
	~Service();
	
	void dynamicExecute(DynamicPointer<ResolveDataSignal> &_rsig);
	void insertObject(Listener &_ro, const ObjectUidT &_ruid);
	void eraseObject(const Listener &_ro);
protected:
	friend class Listener;
	ObjectUidT insertListener(
		const ResolveData &_rai,
		bool _secure = false
	);
	virtual ObjectUidT insertConnection(
		const SocketDevice &_rsd,
		foundation::aio::openssl::Context *_pctx = NULL,
		bool _secure = false
	);
	
	virtual ObjectUidT insertTalker(
		const ResolveData &_rai,
		foundation::aio::openssl::Context *_pctx = NULL,
		bool _secure = false
	);
	virtual ObjectUidT insertConnection(
		const ResolveData &_rai,
		foundation::aio::openssl::Context *_pctx = NULL,
		bool _secure = false
	);
};

}//namespace concept
#endif
