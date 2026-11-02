#include "../src/native_preview_handoff.hpp"
#include <iostream>
#include <stdexcept>
static void require(bool condition,const char* reason){if(!condition)throw std::runtime_error(reason);}
int main(){try{
    evrm::NativePreviewHandoff history;
    auto source=history.configure(0x10000,0x20000,0x30000,0x40000);
    history.request(source,1,true,100);
    const auto copied=history.render(source,true,true,101);
    history.enqueue(0x50000,copied,102);
    const auto consumed=history.execute(0x50000,103);
    require(bool(consumed)&&consumed.origin==1,"accepted request lost before queued task worker");
    require(history.gpu(0x40000,consumed,5,104),"matching GPU buffer lost exact worker association");
    require(!history.execute(0x50000,105),"task association was consumed twice");
    history.unavailable(source);
    require(!history.render(source,true,true,106),"unreadable request reused an earlier accepted origin");
    history.request(source,1,true,107);
    const auto fresh=history.render(source,true,true,108);
    history.request(source,2,false,200);
    const auto first_writer=history.render(source,true,true,201);
    require(first_writer.request==fresh.request&&first_writer.origin==1,"rejected request relabeled first accepted request");
    history.request(source,0,true,300);
    const auto unknown=history.render(source,true,true,301);
    require(bool(unknown)&&unknown.origin==0&&unknown.request!=copied.request,"unknown accepted writer reused known origin");
    require(!history.render(source,false,true,302),"unsupported consumer caller acquired a correlation");
    require(!history.render(source,true,false,303),"different request bytes acquired a correlation");
    history.enqueue(0x60000,unknown,304);
    history.enqueue(0x60000,copied,305);
    require(!history.execute(0x60000,306),"unconsumed task reuse retained an ambiguous association");
    history.enqueue(0x70000,unknown,307);
    const auto changed=history.configure(0x10000,0x20001,0x30000,0x40000);
    require(changed.epoch!=source.epoch,"source context change preserved its epoch");
    require(!history.execute(0x70000,308)&&!history.gpu(0x40000,consumed,5,309),"old task survived source context change");
    history.request(changed,6,true,400);
    const auto current=history.render(changed,true,true,401);
    history.enqueue(0x80000,current,402);
    const auto active=history.execute(0x80000,403);
    require(bool(active)&&!history.gpu(0x40001,active,5,404),"unrelated buffer acquired preview association");
    history.enqueue(0x80800,{},404);
    require(history.gpu(0x40000,active,5,404),"unrelated task revoked a live worker correlation");
    history.enqueue(0x80000,{},404);
    require(!history.gpu(0x40000,active,5,404),"task reused outside preview scope retained old GPU association");
    history.enqueue(0xa0000,current,405);const auto finished=history.execute(0xa0000,406);history.finish(finished);
    require(!history.gpu(0x40000,finished,5,407),"returned worker retained a live task association");
    for(unsigned i=0;i<256;++i){history.enqueue(0xb0000+i*16,current,408+i);const auto item=history.execute(0xb0000+i*16,408+i);
        require(bool(item),"finished task entries exhausted the fixed budget");history.finish(item);}
    history.retire(0x40000);
    require(!history.gpu(0x40000,active,5,700),"retired source retained a worker association");
    const auto first_device=history.configure(0x10000,0x20000,0x30000,0x40000,111);
    const auto second_device=history.configure(0x10000,0x20000,0x30000,0x40000,222);
    require(first_device.epoch!=second_device.epoch,"same-valued buffer from another device reused its source epoch");
    require(!history.retire_owner(111)&&history.source().epoch==second_device.epoch,"old device retired another device's diagnostic source");
    require(history.retire_owner(222)==0x40000&&!history.source().epoch,"device retirement left a diagnostic source active");
    std::cout<<"queued request correlation, competing writer, unavailable request, one-shot task, context and buffer retirement passed\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}return 0;}
