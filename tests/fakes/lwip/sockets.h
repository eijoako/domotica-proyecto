#pragma once
#include <WebServer.h>
#include <cerrno>
#include <sys/types.h>
#ifndef MSG_DONTWAIT
#define MSG_DONTWAIT 0x40
#endif
inline ssize_t send(int fd,const void* data,size_t size,int flags){
 if(FakeHTTP::bypassSocket)return static_cast<ssize_t>(size);
 ++FakeHTTP::socketCalls;++FakeHTTP::chunks;
 FakeHTTP::maxBatch=std::max(FakeHTTP::maxBatch,size);
 if(fd<0||!FakeHTTP::clientActive||FakeHTTP::failSocket){errno=EPIPE;return -1;}
 if(flags!=MSG_DONTWAIT){errno=EINVAL;return -1;}
 if(FakeHTTP::zeroSocket)return 0;
 if(FakeHTTP::stallSocket||FakeHTTP::interruptRemaining){
  Fake::now+=FakeHTTP::stallAdvanceMs;
  errno=FakeHTTP::interruptRemaining?EINTR:EAGAIN;
  if(FakeHTTP::interruptRemaining)--FakeHTTP::interruptRemaining;
  return -1;
 }
 const size_t count=FakeHTTP::partialBytes?std::min(size,FakeHTTP::partialBytes):size;
 FakeHTTP::wire.append(static_cast<const char*>(data),count);
 Fake::now+=FakeHTTP::streamAdvanceMs+FakeHTTP::sendAdvanceMs;
 return static_cast<ssize_t>(count);
}
