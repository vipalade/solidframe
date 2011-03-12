/* Declarations file object.hpp
	
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

#ifndef TESTOBJECT_HPP
#define TESTOBJECT_HPP

#include "utility/streampointer.hpp"
#include "foundation/object.hpp"
#include "common.hpp"
#include "tstring.hpp"

namespace concept{

//! A concept variant of foundation::Object
/*!
	For now all it knows is to receive things.
*/
class Object: public foundation::Object{
public:
	//typedef std::pair<uint32, uint32>	FromPairT;
	//typedef std::pair<uint32, uint32>	FileUidT;
	//typedef std::pair<uint32, uint32>	RequestUidT;
protected:
	Object(uint32 _fullid = 0):foundation::Object(_fullid){}
};

}//namespace concept

#endif