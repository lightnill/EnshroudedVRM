#include "../src/mod_input_state.hpp"
#include <iostream>
#include <stdexcept>
namespace {void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}}
int main(){try{
    evrm::ModInputState s;require(!s.open(),"closed launch");
    require(!s.key('W',true,10,45).suppress&&!s.button(0,true,11).suppress,"game controls forwarded");
    const auto opening=s.key(45,true,20,45);require(opening.opened&&opening.suppress&&s.open(),"Insert opens");
    require(opening.release_keys['W']&&opening.release_buttons[0],"held controls released on original path");
    require(!s.key(45,true,21,45).closed&&s.open(),"repeat never toggles");
    require(s.key(45,false,22,45).suppress,"opening up swallowed");
    require(s.key('A',true,23,45).suppress&&s.button(1,true,24).suppress,"menu controls suppressed");
    auto events=s.drain();require(events.count>=3&&events.events[0].kind==evrm::ModInputEventKind::Reset,"owned UI events");
    const auto closing=s.key(27,true,30,45);require(closing.closed&&!s.open()&&closing.suppress,"Escape closes");
    require(s.key('A',true,31,45).suppress&&s.button(1,true,31).suppress,"held closing tails blocked");
    require(s.key('A',false,32,45).suppress&&s.button(1,false,32).suppress,"closing releases blocked");
    require(!s.key('A',true,33,45).suppress,"fresh movement resumes");
    require(s.key(27,false,34,45).suppress,"Escape release blocked");
    require(s.key(45,true,40,45).opened,"reopen");s.key(45,false,41,45);s.button(0,true,42);
    require(s.close(45).closed&&!s.open(),"close click");require(s.button(0,false,46).suppress,"close click cannot leak");
    require(s.motion(44).suppress&&!s.motion(46).suppress,"old raw movement cannot reach camera after close");
    require(s.key(45,true,44,45).suppress&&!s.open(),"queued old hotkey cannot reopen");s.key(45,false,47,45);
    require(s.key(45,true,48,45).opened,"fresh hotkey can reopen");
    const auto lost=s.focus_lost(50);require(lost.closed&&!s.open(),"focus loss releases ownership");
    require(s.key(45,true,51,45).suppress&&!s.open(),"held hotkey after focus loss cannot open");s.key(45,false,52,45);
    require(s.key(0x70,true,53,0x70).opened,"configured hotkey");s.key(0x70,false,54,0x70);s.drain();
    for(unsigned i=0;i<300&&s.open();++i)s.key('B',!(i&1),60+i,0x70);
    require(!s.open()&&s.drain().events[0].kind==evrm::ModInputEventKind::Reset,"queue overflow closes safely");
    evrm::ModInputState wrap;wrap.key(45,true,0xfffffff0u,45);wrap.key(45,false,0xfffffff1u,45);wrap.close(0xfffffff2u);
    require(wrap.key(45,true,5,45).opened,"message timestamp wrap");
    std::cout<<"PASS menu input held releases, repeats, tails, focus, overflow, hotkey and timestamp wrap\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
