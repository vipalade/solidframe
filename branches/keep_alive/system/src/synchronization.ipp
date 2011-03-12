/* Inline implementation file mutex.ipp
	
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

#ifndef UINLINES
#define inline
#include "system/synchronization.hpp"
#endif

inline Semaphore::Semaphore(int _cnt){
	sem_init(&sem,0,_cnt);
}
inline Semaphore::~Semaphore(){
	sem_destroy(&sem);
}
inline void Semaphore::wait(){
	sem_wait(&sem);
}
inline Semaphore::operator int () {	
	int v;
	sem_getvalue(&sem,&v);
	return v;
}
inline Semaphore &Semaphore::operator++(){
	sem_post(&sem);
	return *this;
}
inline int Semaphore::tryWait(){
	return sem_trywait(&sem);
}

#ifndef UINLINES
#undef inline
#endif
