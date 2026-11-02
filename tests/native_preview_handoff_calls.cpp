#include "../src/native_preview_handoff_calls.hpp"
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>
using U=std::uint64_t;
static void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
struct Memory {
    struct Block {U address;std::vector<unsigned char> bytes;};
    std::vector<Block> blocks;bool fail=false;
    void add(U p,std::size_t n){blocks.push_back({p,std::vector<unsigned char>(n)});}
    unsigned char* at(U p,std::size_t n){for(auto& b:blocks)if(p>=b.address&&p-b.address<=b.bytes.size()&&n<=b.bytes.size()-(p-b.address))return b.bytes.data()+p-b.address;throw std::runtime_error("fixture address missing");}
    template<class T>void put(U p,T value){std::memcpy(at(p,sizeof value),&value,sizeof value);}
    static bool read(void* data,U p,void* out,std::size_t n)noexcept {auto& m=*static_cast<Memory*>(data);if(m.fail)return false;try{std::memcpy(out,m.at(p,n),n);return true;}catch(...){return false;}}
};
int main(){try{
    constexpr U base=0x140000000,client=0x1000000,main=0x2000000,renderer=0x3000000,buffer=0x4000000,app=0x5000000,request=0x6000000,packet=0x7000000,builder=0x8000000,task=0x9000000;
    constexpr U ui=client+0x730,slot=client+0x34428;
    Memory m;m.add(app,0xd8);m.add(ui,0x345d0);m.add(request,0x8d4);m.add(packet,0x8d4);m.add(builder,24);m.put(app+0xd0,ui);
    evrm::NativePreviewHandoff history;history.configure(client,main,renderer,buffer);evrm::NativePreviewHandoffThread thread;
    evrm::NativePreviewHandoffCalls calls(history,{&m,&Memory::read},thread,base);
    unsigned requests=0,renders=0,constructs=0,executes=0;
    auto original_request=[&](U a,U p,U color){require(a==app&&p==request&&color==0x12345678,"request arguments changed");++requests;
        if(*m.at(slot+0x8d0,1)==0){std::memcpy(m.at(slot,0x8d0),m.at(p,0x8d0),0x8d0);m.put<unsigned char>(slot+0x8d0,1);}};
    calls.request(original_request,app,request,0x12345678,base+0xbadb95,100);
    std::memcpy(m.at(packet,0x8d4),m.at(slot,0x8d4),0x8d4);
    auto original_construct=[&](U b,U c,U l,U f,U d,U t){require(b==builder&&c==11&&l==22&&f==33&&d==44&&t==55,"constructor ABI arguments changed");++constructs;m.put(builder+0x10,task);};
    auto original_render=[&](U r,U v,U f,U t,U p){require(r==renderer&&v==1&&f==2&&t==3&&p==packet,"renderer ABI arguments changed");++renders;
        require(bool(thread.rendering)&&thread.rendering.origin==1,"actual renderer scope lost accepted request");
        calls.construct(original_construct,builder,11,22,33,44,55,102);};
    calls.render(original_render,renderer,1,2,3,packet,base+0x861d49,101);
    require(!thread.rendering,"renderer scope leaked after original return");
    calls.execute([&](U b,U c,U t,U h){require(b==11&&c==22&&t==task&&h==44,"worker ABI arguments changed");++executes;
        require(history.gpu(buffer,thread.working,5,104),"actual worker scope lost exact task");},11,22,task,44,103);
    require(!thread.working,"worker scope leaked after return");
    m.fail=true;
    calls.request(original_request,app,request,0x12345678,base+0xc6cda9,110);
    calls.render([&](U,U,U,U,U){++renders;require(!thread.rendering,"failed read reused an earlier request");},renderer,1,2,3,packet,base+0x861d49,111);
    m.fail=false;
    calls.render([&](U,U,U,U,U){++renders;require(!thread.rendering,"unsupported caller inherited renderer scope");},renderer,1,2,3,packet,base+0x857a33,112);
    try{calls.execute([&](U,U,U,U){++executes;throw std::runtime_error("original worker exception");},11,22,task,44,113);require(false,"original exception swallowed");}catch(const std::runtime_error& error){require(std::string(error.what())=="original worker exception","wrong exception");}
    require(!thread.working&&requests==2&&renders==3&&constructs==1&&executes==2,"original forwarding count or scope restoration changed");
    std::cout<<"actual request/render/task/worker observation forwards complete ABIs once and restores scopes on failure/exception\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}return 0;}
