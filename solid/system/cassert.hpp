// solid/system/cassert.hpp
//
// Copyright (c) 2007, 2008 Valentin Palade (vipalade @ gmail . com)
//
// This file is part of SolidFrame framework.
//
// Distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt.
//
#ifndef SYSTEM_CASSERT_HPP
#define SYSTEM_CASSERT_HPP

#include "solid/system/common.hpp"


#ifdef SOLID_HAS_ASSERT

#include <cassert>
#define SOLID_ASSERT(a) assert((a))
#define SOLID_VERIFY(a) assert((a))

#else
inline bool dummy(bool _b){
    return _b;
}
#define SOLID_ASSERT(a)
#define SOLID_VERIFY(a) dummy((a))

#endif


namespace solid{

template <bool B>
struct static_test;

template <>
struct static_test<true>{
    static void ok(){
    }
};

template <>
struct static_test<false>{
};

}//namespace solid

#define cstatic_assert(e) solid::static_test<(e)>::ok()

#endif
