/* Implementation file open.cpp
	
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

#include <iostream>
#include <fstream>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/exception.hpp>
#include <boost/utility.hpp>
#include "utility/workpool.hpp"
#include "system/filedevice.hpp"
#include <deque>
#include <cstring>
#include <cerrno>
using namespace std;

///\cond 0
typedef std::deque<FileDevice>	FileDeuqeTp;
typedef std::deque<auto_ptr<FileDevice> >	AutoFileDequeTp;
///\endcond

///\cond 0
class MyWorkPool: public WorkPool<FileDevice*>{
public:
	void run(Worker &);
	void prepareWorker(){}
	void unprepareWorker(){}
protected:
	int createWorkers(uint);
};
///\endcond
void MyWorkPool::run(Worker &_wk){
	FileDevice* pfile;
	char buf[1024 * 4];
	while(pop(_wk.wid(), pfile) != BAD){
		idbg(_wk.wid()<<" is processing");
		int64 sz = pfile->size();
		int toread;
		int cnt = 0;
		while(sz > 0){
			toread = 4 * 1024;
			if(toread > sz) toread = sz;
			int rv = pfile->read(buf, toread);
			cnt += rv;
			sz -= rv;
		}
		cout<<"read count "<<cnt<<endl;
		//Thread::sleep(100);
	}
}

int MyWorkPool::createWorkers(uint _cnt){
	for(uint i = 0; i < _cnt; ++i){
		Worker *pw = createWorker<Worker>(*this);
		pw->start(true);
	}
	return _cnt;
}


int main(int argc, char *argv[]){

// 	AutoFileDequeTp aq;
// 	aq.push_back(auto_ptr<FileDevice>());
	Thread::init();
	if(argc != 4){
		cout<<"./file_open_pool /path/to/folder file-count folder-count"<<endl;
		return 0;
	}
	char c;
	char name[1024];
	//int minsz = atoi(argv[2]);
	//int maxsz = atoi(argv[3]);
	int filecnt   = atoi(argv[2]);
	int foldercnt   = atoi(argv[3]);
	string line;
	for(char c = '0'; c <= '9'; ++c) line += c;
	for(char c = 'a'; c <= 'z'; ++c) line += c;
	for(char c = 'A'; c <= 'Z'; ++c) line += c;
	cout<<"line"<<endl<<line<<endl;
	sprintf(name, "%s", argv[1]);
	char *fldname = name + strlen(argv[1]);
	char *fname = name + strlen(argv[1]) + 1 + 8;
	FileDeuqeTp	fdq;
	int cnt = 0;
	//cout<<"insert a char: ";cin>>c;
	for(int i = foldercnt; i; --i){
		sprintf(fldname, "/%08u", i);
		*fname = 0;
		for(int j = filecnt; j; --j){
			sprintf(fname, "/%08u.txt", j);
			++cnt;
			//ulong sz = (filecnt * minsz + (j * (maxsz - minsz)))/filecnt;
			fdq.push_back(FileDevice());
			if(fdq.back().open(name, FileDevice::RW)){
				cout<<"error "<<strerror(errno)<<" "<<cnt<<endl;
				cout<<"failed to open file "<<name<<endl;
				return 0;
			}else{
				cout<<"name = "<<name<<" size = "<<fdq.back().size()<<endl;
			}
		}
	}
	cout<<"fdq size = "<<fdq.size()<<endl;
	MyWorkPool wp;
	wp.start(4);
	for(FileDeuqeTp::iterator it(fdq.begin()); it != fdq.end(); ++it){
		wp.push(&(*it));
	}
	//cout<<"insert a char: ";cin>>c;
	wp.stop(true);
	Thread::waitAll();
	return 0;
}

