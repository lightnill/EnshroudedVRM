#include "native_fixture_compat.hpp"
// Whole-GLB import checks; encoded 16-bit input reaches the real image decoder.
#include "../src/vrm_asset.hpp"
#include "avatar_fixture.hpp"
#include "texture_depth_pngs.hpp"
#include <iostream>
#include <cstdlib>
#include <new>
#include <stdexcept>
#include <string>
#include <fstream>
#include <csignal>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
using Bytes=std::vector<std::uint8_t>;
using Json=nlohmann::json;
namespace allocation_control {
struct Entry {void* pointer=nullptr;std::size_t bytes=0;};
Entry live[128];
bool reject_stb_eight=false,arm_vector_failure=false,arm_ordinary_vector_failure=false,reject_vector_eight=false;
std::size_t active_bytes(){std::size_t n=0;for(const auto& entry:live)if(entry.pointer)n+=entry.bytes;return n;}
void remember(void* pointer,std::size_t bytes){
    if(!pointer)return;
    for(auto& entry:live)if(!entry.pointer){entry={pointer,bytes};return;}
    std::abort();
}
void forget(void* pointer){
    if(!pointer)return;
    for(auto& entry:live)if(entry.pointer==pointer){entry={};return;}
    std::abort();
}
}
void* texture_fixture_malloc(std::size_t bytes){
    if(allocation_control::reject_stb_eight&&bytes==8)return nullptr;
    auto* pointer=std::malloc(bytes);allocation_control::remember(pointer,bytes);
    if(pointer&&bytes==16&&allocation_control::arm_vector_failure)allocation_control::reject_vector_eight=true;
    if(pointer&&bytes==8&&allocation_control::arm_ordinary_vector_failure)allocation_control::reject_vector_eight=true;
    return pointer;
}
void* texture_fixture_realloc(void* pointer,std::size_t bytes){
    if(allocation_control::reject_stb_eight&&bytes==8)return nullptr;
    int slot=-1;
    if(pointer)for(int index=0;index<128;++index)if(allocation_control::live[index].pointer==pointer){slot=index;break;}
    auto* replacement=std::realloc(pointer,bytes);
    if(replacement){
        if(slot>=0)allocation_control::live[slot]={replacement,bytes};
        else allocation_control::remember(replacement,bytes);
    }
    return replacement;
}
void texture_fixture_free(void* pointer){allocation_control::forget(pointer);std::free(pointer);}
EVRM_FIXTURE_NOINLINE void* operator new(std::size_t bytes){
    if(allocation_control::reject_vector_eight&&bytes==8){allocation_control::reject_vector_eight=false;throw std::bad_alloc();}
    if(auto* pointer=std::malloc(bytes?bytes:1))return pointer;
    throw std::bad_alloc();
}
EVRM_FIXTURE_NOINLINE void operator delete(void* pointer)noexcept{std::free(pointer);}
EVRM_FIXTURE_NOINLINE void operator delete(void* pointer,std::size_t)noexcept{std::free(pointer);}
namespace {
void require(bool condition,const std::string& message){if(!condition)throw std::runtime_error(message);}
std::uint32_t word(const Bytes& data,std::size_t offset){std::uint32_t value;std::memcpy(&value,data.data()+offset,4);return value;}
void append_word(Bytes& data,std::uint32_t value){const auto* p=reinterpret_cast<const std::uint8_t*>(&value);data.insert(data.end(),p,p+4);}
Bytes fixture(const Bytes& png,bool needed=true,int image_count=1){
    const auto original=avatar_fixture_bytes("Texture depth control");
    const auto json_size=word(original,12);
    auto doc=Json::parse(original.begin()+20,original.begin()+20+json_size);
    Bytes bin(original.begin()+28+json_size,original.end());
    const float uv[]={0,0,1,0,0,1};
    const auto uv_offset=bin.size();const auto* p=reinterpret_cast<const std::uint8_t*>(uv);
    bin.insert(bin.end(),p,p+sizeof uv);
    const auto uv_view=doc["bufferViews"].size();
    doc["bufferViews"].push_back({{"buffer",0},{"byteOffset",uv_offset},{"byteLength",sizeof uv}});
    const auto uv_accessor=doc["accessors"].size();
    doc["accessors"].push_back({{"bufferView",uv_view},{"componentType",5126},{"count",3},{"type","VEC2"}});
    doc["meshes"][0]["primitives"][0]["attributes"]["TEXCOORD_0"]=uv_accessor;
    doc["images"]=Json::array();doc["textures"]=Json::array();
    for(int index=0;index<image_count;++index){
        const auto offset=bin.size();bin.insert(bin.end(),png.begin(),png.end());while(bin.size()%4)bin.push_back(0);
        const auto view=doc["bufferViews"].size();
        doc["bufferViews"].push_back({{"buffer",0},{"byteOffset",offset},{"byteLength",png.size()}});
        doc["images"].push_back({{"bufferView",view},{"mimeType","image/png"}});
        doc["textures"].push_back({{"source",index}});
        if(index)doc["materials"].push_back({{"name","Additional used image"}});
        if(needed)doc["materials"][index]["pbrMetallicRoughness"]["baseColorTexture"]={{"index",index}};
    }
    doc["buffers"][0]["byteLength"]=bin.size();
    auto text=doc.dump();while(text.size()%4)text+=' ';
    Bytes result;append_word(result,0x46546c67);append_word(result,2);
    append_word(result,std::uint32_t(28+text.size()+bin.size()));append_word(result,std::uint32_t(text.size()));
    append_word(result,0x4e4f534a);result.insert(result.end(),text.begin(),text.end());
    append_word(result,std::uint32_t(bin.size()));append_word(result,0x004e4942);result.insert(result.end(),bin.begin(),bin.end());
    return result;
}
void check(const Bytes& png,const Bytes& expected){
    const auto bytes=fixture(png);const auto copy=bytes;const auto result=vrm::load_vrm_bytes(bytes);
    require(bool(result),"Real high-depth VRM import failed: "+result.error);
    require(result.avatar.images.size()==1,"image was lost");
    const auto& image=result.avatar.images[0];
    require(image.width==1&&image.height==2,"image dimensions changed");
    require(image.rgba==expected,"RGBA8 normalization changed channel/alpha values");
    require(result.avatar.materials[0].base_texture==0&&result.avatar.textures[0].image==0,"texture indices changed");
    require(bytes==copy,"source GLB bytes changed");
}
void declined(const vrm::ImportResult& result,const std::string& reason){
    require(!result,"expected importer rejection");
    require(result.error.find(reason)!=std::string::npos,"wrong rejection: "+result.error);
    require(result.avatar.images.empty()&&result.avatar.nodes.empty(),"failed import leaked a partial avatar");
}
}
// Includes the actual callback so cumulative policy can be tested with a seeded
// counter and tiny PNGs, without allocating hundreds of MiB of image storage.
#include "../src/vrm_asset.cpp"
int main(int argc,char** argv){
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    SetUnhandledExceptionFilter([](EXCEPTION_POINTERS*)->LONG{std::_Exit(97);});
#endif
    std::set_terminate([]{std::_Exit(98);});std::signal(SIGABRT,[](int){std::_Exit(99);});try{
    require(argc>=2&&argc<=3,"one owned test mode and optional generated PNG required");const std::string mode=argv[1];
    if(mode=="rgba16")check(png_rgba16,{0,128,255,18,1,127,255,255});
    else if(mode=="rgb16")check(png_rgb16,{18,128,255,255,1,171,0,255});
    else if(mode=="gray16")check(png_gray16,{18,18,18,255,254,254,254,255});
    else if(mode=="grayalpha16")check(png_grayalpha16,{128,128,128,67,255,255,255,0});
    else if(mode=="rgb16_transparency")check(png_rgb16_transparency,{18,128,255,0,1,171,0,255});
    else if(mode=="rgba8"){
        check(png_rgba8,{1,2,3,4,239,240,241,242});
        vrm::ImportLimits limits;limits.decoded_image_bytes=8;
        require(bool(vrm::load_vrm_bytes(fixture(png_rgba8),limits)),"existing tight RGBA8 budget changed");
        limits.decoded_image_bytes=7;declined(vrm::load_vrm_bytes(fixture(png_rgba8),limits),"memory");
    }else if(mode=="unused16"){
        vrm::ImportLimits limits;limits.decoded_image_bytes=4;
        const auto result=vrm::load_vrm_bytes(fixture(png_rgba16,false),limits);
        require(bool(result),"unused high-depth map should retain its placeholder: "+result.error);
        require(result.avatar.images[0].rgba==Bytes({255,255,255,255}),"unused map was decoded");
        require(result.avatar.images[0].width==1&&result.avatar.images[0].height==1,"placeholder grew");
    }else if(mode=="default_cumulative"){
        vrm::ImportLimits limits;Json doc={{"images",Json::array({Json::object()})}};
        vrm::ImageBudget budget(limits,doc);budget.needed[0]=true;budget.used=300u*1024u*1024u;
        tinygltf::Image image;std::string error,warning;
        require(vrm::load_image(&image,0,&error,&warning,0,0,png_rgba8.data(),int(png_rgba8.size()),&budget),
                "default importer still refuses retained textures above 256 MiB: "+error);
        require(image.image==Bytes({1,2,3,4,239,240,241,242})&&budget.used==314572808,
                "default cumulative decode lost pixels or retained-byte accounting");
    }else if(mode=="cumulative_overflow"){
        vrm::ImportLimits limits;Json doc={{"images",Json::array({Json::object()})}};
        vrm::ImageBudget budget(limits,doc);budget.needed[0]=true;budget.used=std::numeric_limits<std::size_t>::max()-7;
        tinygltf::Image image;std::string error,warning;
        const bool loaded=vrm::load_image(&image,0,&error,&warning,0,0,png_rgba8.data(),int(png_rgba8.size()),&budget);
        require(!loaded&&error.find("overflow")!=std::string::npos&&image.image.empty()&&allocation_control::active_bytes()==0,
                "cumulative size overflow did not reject before decoding");
    }else if(mode=="file_budget"){
        const auto bytes=fixture(png_rgba8);vrm::ImportLimits limits;limits.file_bytes=bytes.size();
        require(bool(vrm::load_vrm_bytes(bytes,limits)),"exact caller file-byte budget refused");
        --limits.file_bytes;declined(vrm::load_vrm_bytes(bytes,limits),"size limit");
    }else if(mode=="ordinary_vector_failure"){
        const auto bytes=fixture(png_rgba8);
        allocation_control::arm_ordinary_vector_failure=true;
        const auto result=vrm::load_vrm_bytes(bytes);
        allocation_control::arm_ordinary_vector_failure=false;
        allocation_control::reject_vector_eight=false;
        require(allocation_control::active_bytes()==0,"ordinary decoder allocation leaked when RGBA vector allocation failed");
        require(!result&&result.error.find("memory")!=std::string::npos,"ordinary allocation failure was not a friendly memory error");
        require(result.avatar.images.empty()&&result.avatar.nodes.empty(),"ordinary allocation failure retained a partial avatar");
    }else if(mode=="wide_image"){
        require(argc==3,"generated wide PNG path required");std::ifstream file(argv[2],std::ios::binary);
        const Bytes png((std::istreambuf_iterator<char>(file)),{});
        const auto result=vrm::load_vrm_bytes(fixture(png));
        require(bool(result),"decoder-supported 8193x1 image refused: "+result.error);
        require(result.avatar.images.size()==1&&result.avatar.images[0].width==8193&&result.avatar.images[0].height==1&&result.avatar.images[0].rgba.size()==32772,
                "wide image dimensions or RGBA8 payload changed");
    }else if(mode=="working_budget"){
        vrm::ImportLimits limits;limits.decoded_image_bytes=64;
        require(bool(vrm::load_vrm_bytes(fixture(png_rgba16),limits)),"bounded high-depth decode declined");
        limits.decoded_image_bytes=63;declined(vrm::load_vrm_bytes(fixture(png_rgba16),limits),"working memory");
    }else if(mode=="cumulative_budget"){
        vrm::ImportLimits limits;limits.decoded_image_bytes=72;
        const auto result=vrm::load_vrm_bytes(fixture(png_rgba16,true,2),limits);
        require(bool(result)&&result.avatar.images.size()==2,"bounded cumulative decode declined: "+result.error);
        require(result.avatar.images[0].rgba.size()+result.avatar.images[1].rgba.size()==16,"retained high-depth storage grew");
        limits.decoded_image_bytes=71;declined(vrm::load_vrm_bytes(fixture(png_rgba16,true,2),limits),"working memory");
    }else if(mode=="adam7"){
        const auto bytes=fixture(png_adam7_rgba16);vrm::ImportLimits limits;
        limits.decoded_image_bytes=48;declined(vrm::load_vrm_bytes(bytes,limits),"working memory");
        limits.decoded_image_bytes=64;const auto result=vrm::load_vrm_bytes(bytes,limits);
        require(bool(result),"bounded Adam7 RGBA16 rejected: "+result.error);
        require(result.avatar.images[0].width==2&&result.avatar.images[0].height==1,"Adam7 dimensions changed");
        require(result.avatar.images[0].rgba==Bytes({0,128,255,18,1,127,255,255}),"Adam7 pixels or alpha changed");
    }else if(mode=="no_stb_narrow_allocation"){
        const auto bytes=fixture(png_rgba16);
        require(allocation_control::active_bytes()==0,"decoder allocation already live");
        allocation_control::reject_stb_eight=true;
        const auto result=vrm::load_vrm_bytes(bytes);
        allocation_control::reject_stb_eight=false;
        const auto retained=allocation_control::active_bytes();
        require(bool(result)&&retained==0,"decoder8-bit narrowing failed or leaked "+std::to_string(retained)+" bytes: "+result.error);
        require(result.avatar.images[0].rgba==Bytes({0,128,255,18,1,127,255,255}),"owned narrowing changed pixels");
    }else if(mode=="vector_allocation_failure"){
        const auto bytes=fixture(png_rgba16);
        require(allocation_control::active_bytes()==0,"decoder allocation already live");
        allocation_control::arm_vector_failure=true;
        const auto result=vrm::load_vrm_bytes(bytes);
        allocation_control::arm_vector_failure=false;
        allocation_control::reject_vector_eight=false;
        declined(result,"memory");
        require(allocation_control::active_bytes()==0,"RGBA16 owner leaked after RGBA8 allocation failure");
    }else if(mode=="dimensions"){
        vrm::ImportLimits limits;limits.image_dimension=1;
        declined(vrm::load_vrm_bytes(fixture(png_rgba16),limits),"dimensions");
    }else if(mode=="truncated16"){
        Bytes truncated(png_rgba16.begin(),png_rgba16.begin()+33);
        const auto result=vrm::load_vrm_bytes(fixture(truncated));
        require(!result&&!result.error.empty(),"truncated high-depth PNG accepted");
        require(result.avatar.images.empty()&&result.avatar.nodes.empty(),"decoder failure retained a partial avatar");
    }else throw std::runtime_error("unknown owned test mode");
    std::cout<<"PASS real GLB texture depth: "<<mode<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
