/* Declarations file debug.hpp
	
	Copyright 2007, 2008 Valentin Palade 
	vipalade@gmail.com

	This file is part of SolidGround framework.

	SolidGround is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	SolidGround is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with SolidGround.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef SYSTEM_DEBUG_HPP
#define SYSTEM_DEBUG_HPP


#ifdef UDEBUG

#define DEBUG_BITSET_SIZE 256
#include <ostream>
#include <string>

struct Dbg{
	static const unsigned any;
	static const unsigned system;
	static const unsigned specific;
	static const unsigned protocol;
	static const unsigned ser_bin;
	static const unsigned utility;
	static const unsigned cs;
	static const unsigned ipc;
	static const unsigned tcp;
	static const unsigned udp;
	static const unsigned filemanager;
	static Dbg& instance();
	
	~Dbg();
	
	void init(std::string &_file, const char * _fname, const char *_opt = 0);
	
	void bits(std::string &_ros);
	void setAllBits();
	void resetAllBits();
	void setBit(unsigned _v);
	void resetBit(unsigned _v);
	unsigned registerModule(const char *_name);
	
	std::ostream& print();
	std::ostream& print(
		const char _t,
		const char *_file,
		const char *_fnc,
		int _line
	);
	std::ostream& print(
		const char _t,
		unsigned _module,
		const char *_file,
		const char *_fnc,
		int _line
	);
	void done();
	bool isSet(unsigned _v);
private:
	Dbg();
	struct Data;
	Data &d;
};

#define idbg(x)\
	if(Dbg::instance().isSet(Dbg::any)){\
	Dbg::instance().print('I', __FILE__, __FUNCTION__, __LINE__)<<x;Dbg::instance().done();}
#define idbgx(a,x)\
	if(Dbg::instance().isSet(a)){\
	Dbg::instance().print('I', a,  __FILE__, __FUNCTION__, __LINE__)<<x;Dbg::instance().done();}
#define edbg(x)\
	if(Dbg::instance().isSet(Dbg::any)){\
	Dbg::instance().print('E', __FILE__, __FUNCTION__, __LINE__)<<x;Dbg::instance().done();}
#define edbgx(a,x)\
	if(Dbg::instance().isSet(a)){\
	Dbg::instance().print('E', a,  __FILE__, __FUNCTION__, __LINE__)<<x;Dbg::instance().done();}
#define wdbg(x)\
	if(Dbg::instance().isSet(Dbg::any)){\
	Dbg::instance().print('W', __FILE__, __FUNCTION__, __LINE__)<<x;Dbg::instance().done();}
#define wdbgx(a,x)\
	if(Dbg::instance().isSet(a)){\
	Dbg::instance().print('W', a,  __FILE__, __FUNCTION__, __LINE__)<<x;Dbg::instance().done();}
#define writedbg(x,sz)
#define writedbgx(a, x, sz)


#else

#define pdbg(x)
#define pdbgx(a,x)
#define idbg(x)
#define idbgx(a,x)
#define edbg(x)
#define edbgx(a,x)
#define wdbg(x)
#define wdbgx(a,x)
#define writedbg(x,sz)
#define writedbgx(a, x, sz)

#endif

#endif

