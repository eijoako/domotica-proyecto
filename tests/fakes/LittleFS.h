#pragma once
#include <Arduino.h>
#include <map>
#include <memory>
#include <vector>
#include <cstring>
namespace FakeFS {
inline std::map<std::string,std::vector<byte>> files;
inline bool mounted=true, failWrite=false, failRename=false;
inline int formats=0;
inline void reset(){files.clear();mounted=true;failWrite=failRename=false;formats=0;}
}
class File {
 std::string filePath;bool directory=false,open=false;size_t position=0,index=0;
 std::vector<std::string> children;
 public:
 File()=default;
 File(std::string p,bool dir):filePath(p),directory(dir),open(true){
  if(dir)for(auto& entry:FakeFS::files)if(entry.first.find(p+"/")==0)children.push_back(entry.first);
 }
 explicit operator bool()const{return open;}
 bool isDirectory()const{return directory;}
 const char* path()const{return filePath.c_str();}
 const char* name()const{auto slash=filePath.rfind('/');return filePath.c_str()+(slash==filePath.npos?0:slash+1);}
 size_t size()const{return FakeFS::files[filePath].size();}
 size_t write(const byte* data,size_t count){
  size_t n=FakeFS::failWrite?count-1:count;
  FakeFS::files[filePath].assign(data,data+n);return n;
 }
 size_t read(byte* data,size_t count){
  auto& bytes=FakeFS::files[filePath];size_t n=std::min(count,bytes.size()-position);
  memcpy(data,bytes.data()+position,n);position+=n;return n;
 }
 void flush(){}
 void close(){open=false;}
 File openNextFile(){if(index>=children.size())return {};return File(children[index++],false);}
};
struct LittleFSFake {
 bool begin(bool=false){return FakeFS::mounted;}
 bool format(){++FakeFS::formats;FakeFS::files.clear();FakeFS::mounted=true;return true;}
 bool exists(const String& p){return FakeFS::files.count(p.c_str())>0;}
 bool mkdir(const char*){return true;}
 File open(const String& path,const char* mode="r"){
  const std::string p=path.c_str();
  if(p=="/sessions")return File(p,true);
  if(mode[0]=='w'){FakeFS::files[p].clear();return File(p,false);}
  return FakeFS::files.count(p)?File(p,false):File();
 }
 bool remove(const String& p){return FakeFS::files.erase(p.c_str());}
 bool rename(const String& from,const String& to){
  if(FakeFS::failRename||!exists(from))return false;
  FakeFS::files[to.c_str()]=FakeFS::files[from.c_str()];FakeFS::files.erase(from.c_str());return true;
 }
};
inline LittleFSFake LittleFS;
