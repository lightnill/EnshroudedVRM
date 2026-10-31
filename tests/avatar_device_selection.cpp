#include "avatar_fixture.hpp"
#include "../src/avatar_device_request.hpp"
#include <windows.h>
#include <iostream>
#include <stdexcept>
void need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
template<class F>void until(F fn,const char* why){const auto end=GetTickCount64()+5000;do{if(fn())return;Sleep(3);}while(GetTickCount64()<end);need(false,why);}
struct Models {std::shared_ptr<const evrm::PreparedAvatar> a,b,c;};
evrm::AvatarLoadResult load(const std::string& path,void* raw){const auto& models=*static_cast<Models*>(raw);return {path.find("B.vrm")!=std::string::npos?models.b:models.c,{}};}
struct Device {std::shared_ptr<const evrm::PreparedAvatar> current;unsigned commits=0;bool accept=true;};
bool activate(void* raw,const evrm::AvatarCandidate& candidate)noexcept{auto& device=*static_cast<Device*>(raw);if(!device.accept)return false;device.current=candidate.model;++device.commits;return true;}
int wmain(int argc,wchar_t** argv){try{
    need(argc==2,"fixture folder");Models models;
    models.a=evrm::prepare_avatar_bytes(avatar_fixture_bytes("A"),"C:\\fixture\\A.vrm").model;
    models.b=evrm::prepare_avatar_bytes(avatar_fixture_bytes("B"),"C:\\fixture\\B.vrm").model;
    models.c=evrm::prepare_avatar_bytes(avatar_fixture_bytes("C"),"C:\\fixture\\C.vrm").model;need(models.a&&models.b&&models.c,"models not prepared");
    evrm::AvatarServiceOptions options;options.directory=std::filesystem::path(argv[1]);options.initial=models.a;options.loader=load;options.context=&models;
    evrm::AvatarService service(options);until([&]{return service.snapshot()->preferences_loaded;},"preferences load");
    Device retiring{models.a},surviving{models.a};need(service.request_model(models.b->path),"B request");until([&]{return bool(service.candidate());},"B candidate");
    const auto proposed=evrm::avatar_device_request(service,retiring.current);need(proposed&&evrm::activate_avatar_device(service,proposed,activate,&retiring),"first device did not commit B");
    need(!service.candidate()&&service.active()==models.b&&surviving.current==models.a,"fixture did not leave second device at A");
    const auto adoption=evrm::avatar_device_request(service,surviving.current);need(adoption&&adoption->model==models.b,"surviving device lost globally committed model after another device consumed proposal");
    retiring.current.reset();
    need(evrm::activate_avatar_device(service,adoption,activate,&surviving)&&surviving.current==models.b,"surviving device could not adopt after committing device retired");
    service.fail_candidate(adoption->generation,"A retired device could not stage graphics");need(service.snapshot()->state==evrm::AvatarLoadState::Ready,"failed adoption overwrote globally committed selection status");
    need(!evrm::avatar_device_request(service,surviving.current),"matching device unnecessarily restaged committed selection");
    Device overlapping{models.a};need(evrm::activate_avatar_device(service,adoption,activate,&overlapping)&&overlapping.current==models.b,"overlapping replacement renderer did not catch up");
    need(service.request_model(models.c->path),"C request");until([&]{return bool(service.candidate());},"C candidate");
    need(evrm::avatar_device_request(service,surviving.current)==service.candidate(),"pending latest proposal did not win device staging");
    need(evrm::activate_avatar_device(service,service.candidate(),activate,&surviving)&&surviving.current==models.c,"C commit");
    need(!evrm::activate_avatar_device(service,adoption,activate,&overlapping)&&overlapping.current==models.b,"obsolete committed model reactivated after newer selection");
    const auto current=evrm::avatar_device_request(service,overlapping.current);overlapping.accept=false;
    need(!evrm::activate_avatar_device(service,current,activate,&overlapping)&&service.active()==models.c&&overlapping.current==models.b,"failed per-device adoption changed committed selection");
    overlapping.accept=true;need(evrm::activate_avatar_device(service,current,activate,&overlapping)&&overlapping.current==models.c,"per-device adoption did not recover");
    std::cout<<"PASS process selection remains available to an already constructed surviving device\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
