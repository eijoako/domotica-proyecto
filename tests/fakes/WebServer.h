#pragma once
#include <Arduino.h>
#include <functional>
#include <map>
#include <algorithm>
#include <string>
#define HTTP_GET 0
#define HTTP_POST 1
#define CONTENT_LENGTH_UNKNOWN size_t(-1)
namespace FakeHTTP {
inline uint32_t streamAdvanceMs=0;
inline bool inHandler=false,reentered=false;
inline unsigned chunks=0;
inline bool clientActive=true,stopped=false,wireChunked=false,stallSocket=false,failSocket=false,zeroSocket=false;
inline bool bypassSocket=false;
inline size_t partialBytes=0,maxBatch=0;
inline unsigned socketCalls=0,interruptRemaining=0;
inline uint32_t stallAdvanceMs=100,sendAdvanceMs=0;
inline std::string wire;
inline void reset(){
 streamAdvanceMs=0;inHandler=reentered=false;chunks=0;clientActive=true;stopped=wireChunked=stallSocket=failSocket=zeroSocket=bypassSocket=false;
 partialBytes=maxBatch=0;socketCalls=interruptRemaining=0;stallAdvanceMs=100;sendAdvanceMs=0;wire.clear();
}
inline std::pair<std::string,bool> decoded(){
 if(!wireChunked)return {wire,!stopped};
 std::string body;size_t at=0;
 while(at<wire.size()){
  const auto end=wire.find("\r\n",at);if(end==std::string::npos)return {body,false};
  size_t length=0;try{length=std::stoul(wire.substr(at,end-at),nullptr,16);}catch(...){return {body,false};}
  at=end+2;if(length==0)return {body,wire.size()>=at+2&&wire.compare(at,2,"\r\n")==0};
  if(wire.size()<at+length+2)return {body,false};
  body.append(wire,at,length);at+=length;
  if(wire.compare(at,2,"\r\n")!=0)return {body,false};
  at+=2;
 }
 return {body,false};
}
}
struct FakeClient {
 int fd()const{return FakeHTTP::bypassSocket||FakeHTTP::clientActive?42:-1;}
 bool connected()const{return FakeHTTP::clientActive;}
 void stop(){FakeHTTP::clientActive=false;FakeHTTP::stopped=true;}
};
class WebServer {
 protected:
  bool _chunked=false;
  size_t contentLength=0;
  virtual size_t _currentClientWrite(const char*,size_t size){return size;}
  virtual size_t _currentClientWrite_P(PGM_P,size_t size){return size;}
  FakeClient connection;
 public:
  std::map<std::string, String> args;
  std::map<std::pair<int, std::string>, std::function<void()>> handlers;
  std::function<void()> notFound;
  std::map<std::string, std::string> headers;
  int code = 0;
  String body;
  explicit WebServer(int) {}
  virtual ~WebServer()=default;
  FakeClient& client(){return connection;}
  template<class F> void on(const char* path, int method, F handler) {
    handlers[{method, path}] = handler;
  }
  template<class F> void onNotFound(F handler) { notFound = handler; }
  void begin() {}
  void handleClient() { if(FakeHTTP::inHandler)FakeHTTP::reentered=true; }
  void sendHeader(const char* key, const char* value) { headers[key] = value; }
  void send(int status, const char*, const String& data) {
   code = status; body = data;_chunked=contentLength==CONTENT_LENGTH_UNKNOWN;contentLength=0;
   FakeHTTP::wire.clear();FakeHTTP::wireChunked=_chunked;
   if(data.length())_currentClientWrite(data.c_str(),data.length());
  }
  void send_P(int status, const char*, const char* data) {
   code=status;body=data;_chunked=false;contentLength=0;FakeHTTP::wire.clear();FakeHTTP::wireChunked=false;
   _currentClientWrite_P(data,body.length());
  }
  void setContentLength(size_t length) {contentLength=length;}
  void sendContent(const String& data) { body += data; Fake::now+=FakeHTTP::streamAdvanceMs; ++FakeHTTP::chunks; }
  String arg(const char* name) { return args[name]; }
  void dispatch(int method, const std::string& path) {
    headers.clear();
    FakeHTTP::wire.clear();FakeHTTP::clientActive=true;FakeHTTP::stopped=false;
    auto found = handlers.find({method, path});
    FakeHTTP::inHandler=true;
    if (found == handlers.end()) notFound();
    else found->second();
    FakeHTTP::inHandler=false;
  }
};
