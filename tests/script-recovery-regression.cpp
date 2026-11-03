// Script catalog, engine settings, autostart and automatic restart without a VR session.
#include "../src/render_host.h"
#include <cassert>
#include <sys/stat.h>

namespace {
std::filesystem::path root;
void file(const std::filesystem::path &path,const std::string &text){std::filesystem::create_directories(path.parent_path());std::ofstream(path)<<text;}
// Gives each write a distinct modification time, as separate edits would have.
void touchLater(const std::filesystem::path &path,int seconds){std::filesystem::last_write_time(path,std::filesystem::file_time_type::clock::now()+std::chrono::seconds(seconds));}
bool rejects(const std::string &text,const std::string &needle){
  settings::Settings parsed;parsed.autoRestart=true;std::string error;
  const bool ok=settings::parse(text,parsed,error);
  if(ok||error.find(needle)==std::string::npos){std::fprintf(stderr,"expected rejection containing '%s', got ok=%d error='%s'\n",needle.c_str(),ok,error.c_str());return false;}
  return parsed.autoRestart; // the output is untouched on failure
}
}

int main(){
  root=std::filesystem::temp_directory_path()/("pocketspace-recovery-"+std::to_string(getpid()));
  ScopeExit cleanup{[&]{std::filesystem::permissions(root/"catalog/locked.js",std::filesystem::perms::owner_all,std::filesystem::perm_options::add);std::filesystem::remove_all(root);}};
  const auto catalog=root/"catalog";

  // Recursive discovery: readable .js files only, each canonical file once under its own key.
  file(catalog/"a.js","function update(){}");
  file(catalog/"nested/b.js","function update(){}");
  file(catalog/"nested/deep/c.js","function update(){}");
  file(catalog/"notes.txt","not a script");
  file(catalog/"locked.js","function update(){}");
  file(root/"outside.js","function update(){}");
  std::filesystem::permissions(catalog/"locked.js",std::filesystem::perms::none);
  std::filesystem::create_symlink(catalog/"a.js",catalog/"alias.js");
  std::filesystem::create_symlink(root/"outside.js",catalog/"escape.js");
  std::filesystem::create_directory_symlink(catalog,catalog/"nested/loop");
  std::string error;
  auto keys=listCatalogScripts(catalog,error);
  const bool lockedReadable=std::ifstream(catalog/"locked.js").good(); // true only when run as root
  std::vector<std::string> expected{"a.js","nested/b.js","nested/deep/c.js"};
  if(lockedReadable)expected.insert(expected.begin()+1,"locked.js");
  assert(error.empty()&&keys==expected);
  std::filesystem::path resolved;
  assert(resolveCatalogScript(catalog,"alias.js",resolved,error)&&catalogKey(catalog,resolved)=="a.js");
  assert(!resolveCatalogScript(catalog,"escape.js",resolved,error));
  assert(listCatalogScripts(root/"missing",error).empty()&&!error.empty());

  // Settings: strict validation, defaults for omitted fields, round trip.
  settings::Settings parsed;
  assert(settings::parse(R"({"version":1})",parsed,error)&&parsed==settings::Settings{});
  assert(settings::parse(R"({"version":1,"autostart":["clock.js","tools/notes.js"],"autoRestart":true,"engineAutostart":true})",parsed,error));
  assert((parsed.autostart==std::vector<std::string>{"clock.js","tools/notes.js"})&&parsed.autoRestart&&parsed.engineAutostart);
  settings::Settings again;assert(settings::parse(settings::serialize(parsed),again,error)&&again==parsed);
  assert(rejects("{","not valid JSON"));
  assert(rejects("[]","JSON object"));
  assert(rejects(R"({"autostart":[]})","version must be 1"));
  assert(rejects(R"({"version":2})","version must be 1"));
  assert(rejects(R"({"version":1,"autoStart":true})","unknown settings key: autoStart"));
  assert(rejects(R"({"version":1,"autoRestart":"yes"})","autoRestart must be true or false"));
  assert(rejects(R"({"version":1,"autostart":"a.js"})","must be an array"));
  for(const char *entry:{"\"/abs.js\"","\"../up.js\"","\"a/../b.js\"","\"./a.js\"","\"notes.txt\"","\"\"","3"})
    assert(rejects(std::string(R"({"version":1,"autostart":[)")+entry+"]}","relative .js catalog names"));
  assert(rejects(R"({"version":1,"autostart":["a.js","a.js"]})","duplicate autostart entry: a.js"));
  {std::string many=R"({"version":1,"autostart":[)";for(int i=0;i<257;i++)many+=(i?",":"")+std::string("\"s")+std::to_string(i)+".js\"";assert(rejects(many+"]}","more than 256"));}
  assert(rejects("{\"version\":1,\"pad\":\""+std::string(70000,'x')+"\"}","exceeds 64 KiB"));

  // Atomic writes leave only the target file.
  const auto settingsPath=root/"config/pocketspace/settings.json";
  settings::Settings written;written.autostart={"a.js"};written.autoRestart=true;
  assert(settings::write(settingsPath,written,error));
  for(const auto &entry:std::filesystem::directory_iterator(settingsPath.parent_path()))assert(entry.path()==settingsPath);
  // The watcher reads edits, keeps the last good settings on an invalid one, and recovers.
  {
    settings::File watched(root/"absent.json");
    assert(watched.refresh()&&watched.error().empty()&&watched.current()==settings::Settings{});
    assert(!watched.refresh());
  }
  settings::File watched(settingsPath);
  assert(watched.refresh()&&watched.error().empty()&&watched.current()==written);
  assert(!watched.refresh());
  file(settingsPath,R"({"version":1,"autoRestart":"maybe"})");touchLater(settingsPath,1);
  assert(watched.refresh()&&!watched.error().empty()&&watched.current()==written);
  assert(!watched.refresh());
  file(settingsPath,R"({"version":1,"autostart":["nested/b.js"]})");touchLater(settingsPath,2);
  assert(watched.refresh()&&watched.error().empty()&&(watched.current().autostart==std::vector<std::string>{"nested/b.js"})&&!watched.current().autoRestart);
  std::filesystem::remove(settingsPath);
  assert(watched.refresh()&&watched.current()==settings::Settings{});

  auto now=std::chrono::steady_clock::time_point{}+std::chrono::hours(1);
  std::vector<std::string> notified;
  {
    // One failing autostart entry never prevents the others; duplicates of a running file fail alone.
    file(catalog/"bad.js","throw Error('EXPECTED_AUTOSTART_FAILURE')");
    ScriptRegistry registry;registry.setClock([&]{return now;});
    const auto failures=launchAutostart(registry,catalog,{"missing.js","bad.js","a.js","alias.js","../outside.js","nested/b.js"});
    assert(failures.size()==4);
    assert(failures[0].rfind("missing.js: ",0)==0&&failures[1].rfind("bad.js: ",0)==0&&failures[2].rfind("alias.js: script is already running",0)==0&&failures[3].rfind("../outside.js: ",0)==0);
    assert(registry.runningIds().size()==2);
  }

  // Restart is off by default: a crashed script stays failed and is reported once.
  file(catalog/"crash.js","function update(){throw Error('EXPECTED_CRASH')}");
  {
    ScriptRegistry registry;registry.setClock([&]{return now;});
    registry.setOnFailure([&](const ScriptInstance &instance){notified.push_back(instance.lastError);});
    assert(!registry.autoRestart());
    int id=0;assert(registry.launch((catalog/"crash.js").string(),id,error));
    assert(!registry.update(id,1./90)&&registry.find(id)->state==ScriptState::Failed&&!registry.find(id)->restartAt);
    now+=std::chrono::minutes(10);assert(registry.tickRestarts().empty()&&registry.find(id)->state==ScriptState::Failed);
    assert(notified.size()==1&&notified[0].find("EXPECTED_CRASH")!=std::string::npos);
  }

  // Enabled restarts back off 1, 2, 4, 8 and 16 seconds, then stop for good. Ticks never spin.
  {
    ScriptRegistry registry;registry.setClock([&]{return now;});registry.setAutoRestart(true);
    int id=0;assert(registry.launch((catalog/"crash.js").string(),id,error));
    int restarts=0;
    for(int delay:{1,2,4,8,16}){
      assert(!registry.update(id,1./90));
      auto *instance=registry.find(id);assert(instance->state==ScriptState::Failed&&instance->restartAt==now+std::chrono::seconds(delay));
      now+=std::chrono::seconds(delay)-std::chrono::milliseconds(1);
      for(int i=0;i<100;i++)assert(registry.tickRestarts().empty());
      now+=std::chrono::milliseconds(1);
      assert(registry.tickRestarts()==std::vector<int>{id});++restarts;
      assert(registry.tickRestarts().empty()&&instance->state==ScriptState::Running&&instance->generation==uint64_t(restarts+1));
    }
    assert(!registry.update(id,1./90));
    assert(registry.find(id)->state==ScriptState::Failed&&!registry.find(id)->restartAt&&restarts==restart::maxAttempts);
    now+=std::chrono::hours(1);assert(registry.tickRestarts().empty());
  }

  // A script that stays up for a minute earns a fresh set of attempts; a failed reload
  // attempt counts toward the ceiling.
  file(catalog/"flaky.js","function update(){if(globalThis.crash)throw Error('EXPECTED_FLAKE')}");
  {
    ScriptRegistry registry;registry.setClock([&]{return now;});registry.setAutoRestart(true);
    int id=0;assert(registry.launch((catalog/"flaky.js").string(),id,error));
    auto crash=[&]{auto *instance=registry.find(id);{ScriptScope scope(instance->host);JS_Eval(instance->host.ctx,"crash=true",10,"t",JS_EVAL_TYPE_GLOBAL);}return registry.update(id,1./90);};
    assert(!crash());now+=std::chrono::seconds(1);assert(registry.tickRestarts().size()==1);
    assert(registry.find(id)->restartAttempts==1);
    now+=restart::healthyRun;registry.tickRestarts();assert(registry.find(id)->restartAttempts==0);
    assert(!crash());assert(registry.find(id)->restartAt==now+std::chrono::seconds(1));
    file(catalog/"flaky.js","throw Error('EXPECTED_RESTART_LOAD_FAILURE')");
    now+=std::chrono::seconds(1);assert(registry.tickRestarts().empty());
    assert(registry.find(id)->restartAttempts==2&&registry.find(id)->restartAt==now+std::chrono::seconds(2));
    // A manual stop cancels the pending restart for the rest of the session.
    assert(registry.stop(id)&&registry.find(id)->state==ScriptState::Stopped&&registry.find(id)->manuallyStopped);
    now+=std::chrono::hours(1);assert(registry.tickRestarts().empty()&&registry.find(id)->state==ScriptState::Stopped);
  }
  // Turning the setting off cancels pending restarts; scratch is never restarted.
  {
    ScriptRegistry registry;registry.setClock([&]{return now;});registry.setAutoRestart(true);
    int id=0;assert(registry.launch((catalog/"crash.js").string(),id,error));
    assert(!registry.update(id,1./90)&&registry.find(id)->restartAt);
    registry.setAutoRestart(false);assert(!registry.find(id)->restartAt);
    now+=std::chrono::minutes(1);assert(registry.tickRestarts().empty());
    registry.setAutoRestart(true);
    auto *scratch=registry.find(0);
    {ScriptScope scope(scratch->host);assert(loadScript(scratch->host,(catalog/"crash.js").string()));}
    scratch->state=ScriptState::Running;
    assert(!registry.update(0,1./90)&&registry.find(0)->state==ScriptState::Failed&&!registry.find(0)->restartAt);
  }
  std::puts("PASS script recovery: recursive catalog keys and escapes, strict settings with atomic writes and last-good edits, isolated autostart failures, restart off by default, bounded backoff, healthy reset, manual-stop override and scratch exclusion");
}
