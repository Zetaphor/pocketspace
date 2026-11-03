#pragma once
// Engine settings shared by the engine, CLI and dashboard: one validated JSON file,
// replaced atomically. An invalid edit keeps the last good settings.
#include <quickjs.h>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <fcntl.h>
#include <unistd.h>

namespace settings {
constexpr size_t maxAutostart=256,maxFileBytes=64*1024;
struct Settings {
  // Catalog-relative script names launched at engine start, in order.
  std::vector<std::string> autostart;
  bool autoRestart=false,engineAutostart=false;
  bool operator==(const Settings&)const=default;
};
// Always under $HOME/.config, so a shell with another XDG_CONFIG_HOME and the user
// service read the same file. --settings overrides it.
inline std::filesystem::path defaultPath(){
  const char *home=std::getenv("HOME");
  return home?std::filesystem::path(home)/".config/pocketspace/settings.json":std::filesystem::path("settings.json");
}
// Syntax only; whether an entry exists in the catalog is checked when it launches.
inline bool validAutostartName(const std::string &name){
  if(name.empty()||name.size()>1024||name.find('\0')!=std::string::npos)return false;
  const std::filesystem::path path(name);
  if(path.is_absolute()||path.extension()!=".js")return false;
  for(const auto &part:path)if(part==".."||part==".")return false;
  return true;
}
inline std::string jsonString(const std::string &value){
  std::string out="\"";
  for(unsigned char c:value){
    if(c=='"'||c=='\\'){out+='\\';out+=char(c);}
    else if(c=='\n')out+="\\n";else if(c=='\r')out+="\\r";else if(c=='\t')out+="\\t";
    else if(c<0x20){static const char *hex="0123456789abcdef";out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}
    else out+=char(c);
  }
  return out+'"';
}
// Pretty for the file people edit; compact for single-line protocol messages.
inline std::string serialize(const Settings &value,bool pretty=true){
  const std::string field=pretty?",\n  ":",",colon=pretty?": ":":";
  std::string out=std::string("{")+(pretty?"\n  ":"")+"\"version\""+colon+"1"+field+"\"autostart\""+colon+"[";
  for(size_t i=0;i<value.autostart.size();i++)out+=(i?(pretty?", ":","):"")+jsonString(value.autostart[i]);
  out+="]"+field+"\"autoRestart\""+colon+(value.autoRestart?"true":"false");
  out+=field+"\"engineAutostart\""+colon+(value.engineAutostart?"true":"false");
  return out+(pretty?"\n}\n":"}");
}
// Strict: version 1, known keys only, correct types, unique valid autostart names.
inline bool parse(const std::string &text,Settings &result,std::string &error){
  if(text.size()>maxFileBytes){error="settings file exceeds 64 KiB";return false;}
  JSRuntime *rt=JS_NewRuntime();if(!rt){error="settings parser unavailable";return false;}
  JSContext *ctx=JS_NewContext(rt);if(!ctx){JS_FreeRuntime(rt);error="settings parser unavailable";return false;}
  Settings parsed;bool ok=true;
  auto fail=[&](const std::string &message){if(ok)error=message;ok=false;};
  JSValue root=JS_ParseJSON(ctx,text.c_str(),text.size(),"settings.json");
  if(JS_IsException(root)){JSValue e=JS_GetException(ctx);JS_FreeValue(ctx,e);fail("settings file is not valid JSON");}
  else if(!JS_IsObject(root)||JS_IsArray(root))fail("settings must be a JSON object");
  else{
    JSPropertyEnum *properties=nullptr;uint32_t count=0;
    if(JS_GetOwnPropertyNames(ctx,&properties,&count,root,JS_GPN_STRING_MASK|JS_GPN_ENUM_ONLY)<0)fail("settings keys are unreadable");
    for(uint32_t i=0;ok&&i<count;i++){
      const char *key=JS_AtomToCString(ctx,properties[i].atom);const std::string name=key?key:"";if(key)JS_FreeCString(ctx,key);
      if(name!="version"&&name!="autostart"&&name!="autoRestart"&&name!="engineAutostart")fail("unknown settings key: "+name);
    }
    if(properties)JS_FreePropertyEnum(ctx,properties,count);
    JSValue version=JS_GetPropertyStr(ctx,root,"version");double v=0;
    if(ok&&!(JS_IsNumber(version)&&JS_ToFloat64(ctx,&v,version)==0&&v==1))fail("settings version must be 1");
    JS_FreeValue(ctx,version);
    for(auto [name,target]:{std::pair{"autoRestart",&parsed.autoRestart},std::pair{"engineAutostart",&parsed.engineAutostart}}){
      JSValue field=JS_GetPropertyStr(ctx,root,name);
      if(!JS_IsUndefined(field)){if(JS_IsBool(field))*target=JS_ToBool(ctx,field)>0;else fail(std::string(name)+" must be true or false");}
      JS_FreeValue(ctx,field);
    }
    JSValue list=JS_GetPropertyStr(ctx,root,"autostart");
    if(ok&&!JS_IsUndefined(list)){
      if(!JS_IsArray(list))fail("autostart must be an array of catalog script names");
      else{
        JSValue lengthValue=JS_GetPropertyStr(ctx,list,"length");uint32_t length=0;JS_ToUint32(ctx,&length,lengthValue);JS_FreeValue(ctx,lengthValue);
        if(length>maxAutostart)fail("autostart has more than 256 entries");
        for(uint32_t i=0;ok&&i<length;i++){
          JSValue item=JS_GetPropertyUint32(ctx,list,i);
          const char *name=JS_IsString(item)?JS_ToCString(ctx,item):nullptr;
          const std::string entry=name?name:"";if(name)JS_FreeCString(ctx,name);JS_FreeValue(ctx,item);
          if(!name||!validAutostartName(entry))fail("autostart entries must be relative .js catalog names");
          else if(std::find(parsed.autostart.begin(),parsed.autostart.end(),entry)!=parsed.autostart.end())fail("duplicate autostart entry: "+entry);
          else parsed.autostart.push_back(entry);
        }
      }
    }
    JS_FreeValue(ctx,list);
  }
  JS_FreeValue(ctx,root);JS_FreeContext(ctx);JS_FreeRuntime(rt);
  if(ok){result=std::move(parsed);error.clear();}
  return ok;
}
// Writes a temporary file beside the target, flushes it, then renames it over the target.
inline bool write(const std::filesystem::path &path,const Settings &value,std::string &error){
  std::error_code ec;std::filesystem::create_directories(path.parent_path().empty()?".":path.parent_path(),ec);
  if(ec){error="settings directory cannot be created";return false;}
  const auto temporary=path.string()+".tmp."+std::to_string(getpid());
  const std::string text=serialize(value);
  const int fd=::open(temporary.c_str(),O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC,0644);
  if(fd<0){error="settings file cannot be written";return false;}
  size_t written=0;while(written<text.size()){const auto n=::write(fd,text.data()+written,text.size()-written);if(n<=0)break;written+=size_t(n);}
  const bool flushed=written==text.size()&&::fsync(fd)==0;::close(fd);
  if(!flushed||std::rename(temporary.c_str(),path.c_str())!=0){std::filesystem::remove(temporary,ec);error="settings file cannot be replaced";return false;}
  error.clear();return true;
}

// Follows one settings file. A missing file means defaults; an invalid file keeps
// the previous settings and reports the error once per change.
class File {
  std::filesystem::path path_;
  Settings current_;
  std::filesystem::file_time_type mtime_{};bool exists_=false,loaded_=false;uintmax_t size_=0;
  std::string error_;
public:
  explicit File(std::filesystem::path path):path_(std::move(path)){}
  const std::filesystem::path &path()const{return path_;}
  const Settings &current()const{return current_;}
  const std::string &error()const{return error_;}
  // True when the file changed since the last call; current() and error() then describe it.
  bool refresh(){
    std::error_code ec;const bool exists=std::filesystem::is_regular_file(path_,ec);
    const auto mtime=exists?std::filesystem::last_write_time(path_,ec):std::filesystem::file_time_type{};
    const auto size=exists?std::filesystem::file_size(path_,ec):0;
    if(loaded_&&exists==exists_&&mtime==mtime_&&size==size_)return false;
    loaded_=true;exists_=exists;mtime_=mtime;size_=size;
    if(!exists){current_={};error_.clear();return true;}
    std::ifstream file(path_,std::ios::binary);std::ostringstream text;text<<file.rdbuf();
    Settings parsed;std::string error;
    if(!file.good()&&!file.eof())error="settings file is unreadable";
    else if(parse(text.str(),parsed,error)){current_=std::move(parsed);error_.clear();return true;}
    error_=error;return true;
  }
};
}
