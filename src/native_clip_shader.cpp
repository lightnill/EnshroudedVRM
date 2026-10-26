#include "native_clip_shader.hpp"
#include "sha256.hpp"
#include <algorithm>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <map>
#include <stdexcept>

namespace evrm {
namespace {
using Word=std::uint32_t;
using Words=std::vector<Word>;
struct Instruction {std::size_t at;Word count,op;};
struct Contract {
    std::size_t bytes,store;
    const char* digest;
    NativeClipKind kind;
    Word actual_instance,instances,culled,position;
    Word position_variable=5,instances_binding=4,culled_binding=5;
    Word execution_model=0,position_pointer=0,inject_value_word=0;
};
constexpr Contract contracts[]={
    {26060,6510,"a109d726a6c928a89eb9eaf15dc91c2ca2f84a1e663296fd375db09386537002",NativeClipKind::shadow,464,13,14,1001},
    {26928,6721,"b03b19480b45cdde956cf36d1fe591d68465bda7dbe97b4a454477b5c2b2c86d",NativeClipKind::visibility,477,15,16,1031},
    {27376,6827,"a2491bcb6d0172e72a49c64ef2bae2b75e1f2ecf42cf28a2be64af408e9a5f89",NativeClipKind::visibility_alpha,485,17,18,1049},
    {48504,12091,"e68043b76b025782c798105809be0f3e30635a1fb73bcabdbccb6828dfed9f64",NativeClipKind::gbuffer,785,26,27,1836},
    {7380,1840,"0cde43e26b70fe3b664831c0d61361113d9d961b6fc84b3f39a00e3849d65178",NativeClipKind::billboard_visibility,152,13,14,241,9,3,4},
    {9556,2384,"edeb4bd938c4520b312dc08290b187f1de340974f69717ffcd5e3d3513a9b2e8",NativeClipKind::billboard_gbuffer,184,15,16,289,11,3,4},
#include "native_clip_variants.generated.inl"
};
void require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
bool matches(const detail::Sha256Digest& digest,const char* text){
    constexpr char hex[]="0123456789abcdef";
    for(std::size_t i=0;i<digest.size();++i)if(text[i*2]!=hex[digest[i]>>4]||text[i*2+1]!=hex[digest[i]&15])return false;
    return text[64]=='\0';
}
void emit(Words& out,Word op,std::initializer_list<Word> args){
    require(args.size()<65535,"native clip instruction too large");out.push_back((static_cast<Word>(args.size()+1)<<16)|op);out.insert(out.end(),args);
}
struct Module {
    Words words;
    std::vector<Instruction> code;
    std::map<Word,const Instruction*> definitions;
    explicit Module(const void* bytes,std::size_t size):words(size/4){
        std::memcpy(words.data(),bytes,size);
        require(words.size()>=5&&words[0]==0x07230203&&words[1]==0x00010500&&words[3]>0&&words[3]<=65536&&words[4]==0,"native clip SPIR-V header mismatch");
        for(std::size_t at=5;at<words.size();){const auto count=words[at]>>16,op=words[at]&65535;require(count&&count<=words.size()-at,"native clip instruction range invalid");code.push_back({at,count,op});at+=count;}
        for(const auto& i:code){
            Word id=0;
            if(i.op>=19&&i.op<=38){require(i.count>=2,"native clip type truncated");id=word(i,1);}
            else if(i.op==43||i.op==44||i.op==54||i.op==59||i.op==61||i.op==65||i.op==80||i.op==81||i.op==82||i.op==83||i.op==245||i.op==400){require(i.count>=3,"native clip definition truncated");id=word(i,2);}
            if(id){require(id<words[3]&&definitions.emplace(id,&i).second,"native clip duplicate/bad result ID");}
        }
    }
    Word word(const Instruction& i,std::size_t n)const {require(n<i.count,"native clip operand missing");return words[i.at+n];}
    const Instruction& definition(Word id,Word op)const {
        const auto found=definitions.find(id);require(found!=definitions.end()&&found->second->op==op,"native clip dataflow definition mismatch");return *found->second;
    }
    bool decorated(Word id,Word decoration,Word value)const {
        unsigned matches=0;
        for(const auto& i:code)if(i.op==71&&i.count==4&&word(i,1)==id&&word(i,2)==decoration){require(word(i,3)==value,"native clip decoration mismatch");++matches;}
        return matches==1;
    }
    void buffer(Word id,Word binding,Word stride)const {
        require(decorated(id,34,2)&&decorated(id,33,binding),"native clip native buffer binding mismatch");
        const auto& variable=definition(id,59);require(variable.count==4&&word(variable,3)==12,"native clip native buffer storage mismatch");
        const auto& pointer=definition(word(variable,1),32);require(pointer.count==4&&word(pointer,2)==12,"native clip native buffer pointer mismatch");
        const auto& structure=definition(word(pointer,3),30);require(structure.count==3,"native clip native buffer block mismatch");
        const auto& array=definition(word(structure,2),29);require(array.count==3&&decorated(word(array,1),6,stride),"native clip native buffer stride mismatch");
    }
};
struct Anchors {const Instruction* entry{};std::size_t annotations=0,declarations=0;Word uint_type=0,float_type=0,position_type=0;};
Anchors anchors(const Module& m,const Contract& c){
    Anchors out;unsigned entries=0,functions=0,stores=0,instance_reads=0;Word function=0;
    for(const auto& i:m.code){
        if(i.op==15){
            ++entries;require(i.count>=6&&m.word(i,1)==c.execution_model,"native clip execution model mismatch");
            const auto* name=reinterpret_cast<const char*>(m.words.data()+i.at+3);
            const auto name_bytes=(i.count-3)*4;const auto* end=static_cast<const char*>(std::memchr(name,0,name_bytes));
            require(end&&end-name==7&&std::memcmp(name,c.execution_model?"ms_main":"vs_main",7)==0,"native clip entry name mismatch");out.entry=&i;function=m.word(i,2);
        }
        if(i.op==54){++functions;require(m.word(i,2)==function,"native clip entry/function mismatch");if(!out.declarations)out.declarations=i.at;}
        if(i.op>=19&&i.op<=39&&!out.annotations)out.annotations=i.at;
        if(i.op==71&&i.count==4&&m.word(i,2)==34)require(m.word(i,3)<5,"native clip private descriptor set collision");
        if(i.op==62&&i.count>=3&&m.word(i,1)==(c.position_pointer?c.position_pointer:c.position_variable)){
            require(i.at==c.store&&i.count==3&&(c.inject_value_word||m.word(i,2)==c.position),"native clip Position store mismatch");++stores;}
        if(i.op==65&&i.count==6&&m.word(i,3)==c.instances&&m.word(i,5)==c.actual_instance)++instance_reads;
    }
    require(entries==1&&functions==1&&stores==1&&instance_reads==1&&out.entry&&out.annotations&&out.declarations,"native clip ambiguous structural anchors");
    require(c.store>out.declarations&&out.annotations<out.declarations,"native clip declaration/store ordering mismatch");
    m.buffer(c.instances,c.instances_binding,128);m.buffer(c.culled,c.culled_binding,8);
    const auto& actual=m.definition(c.actual_instance,81);require(actual.count==5&&m.word(actual,4)==1&&actual.at<c.store,"native clip actual instance extraction mismatch");
    out.uint_type=m.word(actual,1);const auto& integer=m.definition(out.uint_type,21);require(integer.count==4&&m.word(integer,2)==32&&m.word(integer,3)==0,"native clip actual instance is not uint32");
    Word source=m.word(actual,3);
    const auto copy=m.definitions.find(source);require(copy!=m.definitions.end(),"native clip culled copy missing");
    if(copy->second->op==400||copy->second->op==83){require(copy->second->count==4,"native clip culled copy malformed");source=m.word(*copy->second,3);}
    const auto& load=m.definition(source,61);require(load.count==4,"native clip culled load malformed");
    const auto& access=m.definition(m.word(load,3),65);require(access.count==6&&m.word(access,3)==c.culled,"native clip culled source mismatch");
    if(c.culled_binding!=6){const auto& input=m.definition(m.word(access,5),61);require(input.count==4&&m.decorated(m.word(input,3),11,43),"native clip culled index is not InstanceIndex");}
    // Exact meshlet modules use a workgroup/meshlet lookup instead of
    // InstanceIndex. Fingerprints and the culled.member1 -> Instance128
    // access above pin that different index domain.
    require(m.decorated(c.position_variable,11,0),"native clip Position decoration mismatch");
    const auto& position=m.definition(c.position_variable,59);require(position.count==4&&m.word(position,3)==3,"native clip Position storage mismatch");
    const auto& pointer=m.definition(m.word(position,1),32);require(pointer.count==4&&m.word(pointer,2)==3,"native clip Position pointer mismatch");
    out.position_type=m.word(pointer,3);
    if(c.execution_model){const auto& array=m.definition(out.position_type,28);require(array.count==4,"native clip mesh Position array mismatch");out.position_type=m.word(array,2);
        const auto& output=m.definition(c.position_pointer,65);require(output.count==5&&m.word(output,3)==c.position_variable&&output.at<c.store,"native clip mesh Position pointer mismatch");}
    if(c.inject_value_word){const auto& value=m.definition(c.position,80);require(value.at==c.inject_value_word&&value.at>actual.at&&value.at<c.store,"native clip emulated Position anchor mismatch");}
    const auto& vector=m.definition(out.position_type,23);require(vector.count==4&&m.word(vector,3)==4,"native clip Position is not vec4");
    out.float_type=m.word(vector,2);const auto& scalar=m.definition(out.float_type,22);require(scalar.count==3&&m.word(scalar,2)==32,"native clip Position is not float32");return out;
}
Words transform(const Module& m,const Contract& c,const Anchors& a){
    Word next=m.words[3];Words annotations,declarations,instructions;
    const auto id=[&](){require(next<std::numeric_limits<Word>::max(),"native clip ID bound overflow");return next++;};
    std::map<Words,Word> types;std::map<std::pair<Word,Word>,Word> constants;
    for(const auto& i:m.code){
        if(i.op==20||i.op==23||i.op==32){Words key{i.op};key.insert(key.end(),m.words.begin()+i.at+2,m.words.begin()+i.at+i.count);types.emplace(std::move(key),m.word(i,1));}
        if(i.op==43&&i.count==4)constants.emplace(std::make_pair(m.word(i,1),m.word(i,3)),m.word(i,2));
    }
    const auto type=[&](Word op,std::initializer_list<Word> args){Words key{op};key.insert(key.end(),args);const auto found=types.find(key);if(found!=types.end())return found->second;const auto result=id();declarations.push_back((static_cast<Word>(args.size()+2)<<16)|op);declarations.push_back(result);declarations.insert(declarations.end(),args);types.emplace(std::move(key),result);return result;};
    const auto constant=[&](Word type_id,Word bits){const auto key=std::make_pair(type_id,bits);const auto found=constants.find(key);if(found!=constants.end())return found->second;const auto result=id();emit(declarations,43,{type_id,result,bits});constants.emplace(key,result);return result;};
    const auto zero=constant(a.uint_type,0),one=constant(a.uint_type,1),five=constant(a.uint_type,5),thirty_one=constant(a.uint_type,31);
    const auto word_mask=constant(a.uint_type,4095),word_count=constant(a.uint_type,4096),instance_count=constant(a.uint_type,131072);
    const auto float_zero=constant(a.float_type,0),float_one=constant(a.float_type,0x3f800000),float_two=constant(a.float_type,0x40000000);
    const auto bool_type=type(20,{}),bool4=type(23,{bool_type,4});
    const auto commit_block=id();emit(declarations,30,{commit_block,a.uint_type});emit(annotations,71,{commit_block,2});emit(annotations,72,{commit_block,0,35,192});
    const auto uniform_block_pointer=type(32,{2,commit_block}),uniform_uint_pointer=type(32,{2,a.uint_type}),commit=id();emit(declarations,59,{uniform_block_pointer,commit,2});emit(annotations,71,{commit,34,5});emit(annotations,71,{commit,33,0});
    const auto array=type(28,{a.uint_type,word_count}),bitset_block=id();emit(annotations,71,{array,6,4});emit(declarations,30,{bitset_block,array});emit(annotations,71,{bitset_block,2});emit(annotations,72,{bitset_block,0,35,0});emit(annotations,72,{bitset_block,0,24});
    const auto storage_block_pointer=type(32,{12,bitset_block}),storage_uint_pointer=type(32,{12,a.uint_type}),bitset=id();emit(declarations,59,{storage_block_pointer,bitset,12});emit(annotations,71,{bitset,34,5});emit(annotations,71,{bitset,33,1});
    const auto off_clip=id();emit(declarations,44,{a.position_type,off_clip,float_two,float_two,float_zero,float_one});
    const auto raw_word=id(),bounded_word=id(),bit_pointer=id(),bits=id(),bit_offset=id(),bit_mask=id(),hit_bits=id(),hit=id(),in_range=id(),commit_pointer=id(),commit_value=id(),enabled=id(),member=id(),selected=id(),selected4=id(),position=id();
    emit(instructions,194,{a.uint_type,raw_word,c.actual_instance,five});emit(instructions,199,{a.uint_type,bounded_word,raw_word,word_mask});
    emit(instructions,65,{storage_uint_pointer,bit_pointer,bitset,zero,bounded_word});emit(instructions,61,{a.uint_type,bits,bit_pointer});
    emit(instructions,199,{a.uint_type,bit_offset,c.actual_instance,thirty_one});emit(instructions,196,{a.uint_type,bit_mask,one,bit_offset});emit(instructions,199,{a.uint_type,hit_bits,bits,bit_mask});
    emit(instructions,171,{bool_type,hit,hit_bits,zero});emit(instructions,176,{bool_type,in_range,c.actual_instance,instance_count});
    emit(instructions,65,{uniform_uint_pointer,commit_pointer,commit,zero});emit(instructions,61,{a.uint_type,commit_value,commit_pointer});emit(instructions,171,{bool_type,enabled,commit_value,zero});
    emit(instructions,167,{bool_type,member,hit,in_range});emit(instructions,167,{bool_type,selected,member,enabled});emit(instructions,80,{bool4,selected4,selected,selected,selected,selected});
    emit(instructions,169,{a.position_type,c.inject_value_word?c.position:position,selected4,off_clip,c.inject_value_word?position:c.position});
    Words result(m.words.begin(),m.words.begin()+5);result[3]=next;
    for(const auto& i:m.code){
        if(i.at==a.annotations)result.insert(result.end(),annotations.begin(),annotations.end());
        if(i.at==a.declarations)result.insert(result.end(),declarations.begin(),declarations.end());
        if(i.at==c.inject_value_word){const auto at=result.size();result.insert(result.end(),m.words.begin()+i.at,m.words.begin()+i.at+i.count);result[at+2]=position;result.insert(result.end(),instructions.begin(),instructions.end());}
        else if(i.at==c.store&&!c.inject_value_word){result.insert(result.end(),instructions.begin(),instructions.end());emit(result,62,{c.position_pointer?c.position_pointer:c.position_variable,position});}
        else if(&i==a.entry){require(i.count<=65533,"native clip entry interface overflow");const auto at=result.size();result.insert(result.end(),m.words.begin()+i.at,m.words.begin()+i.at+i.count);result[at]=((i.count+2)<<16)|15;result.push_back(commit);result.push_back(bitset);}
        else result.insert(result.end(),m.words.begin()+i.at,m.words.begin()+i.at+i.count);
    }return result;
}
}
bool patch_native_clip_shader(const void* bytes,std::size_t size,NativeClipModule& out,std::string& reason) noexcept {
    out={};reason.clear();
    try{
        require(bytes&&size>=20&&size%4==0&&size<=131072,"native clip input span invalid");
        const Contract* selected=nullptr;for(const auto& c:contracts)if(c.bytes==size)selected=&c;
        require(selected,"native clip shader size unknown");const auto digest=detail::sha256(bytes,size);require(matches(digest,selected->digest),"native clip shader fingerprint unknown");
        const Module module(bytes,size);const auto admitted=anchors(module,*selected);auto words=transform(module,*selected,admitted);
        out.words=std::move(words);out.original_digest=digest;out.kind=selected->kind;out.mesh=selected->execution_model!=0;return true;
    }catch(const std::exception& error){try{reason=error.what();}catch(...){}}catch(...){try{reason="native clip transform unavailable";}catch(...){}}return false;
}
#ifdef EVRM_NATIVE_CLIP_TESTING
bool patch_native_clip_fixture_shader(const void* bytes,std::size_t size,NativeClipModule& out,std::string& reason) noexcept {
    out={};reason.clear();
    try{
        require(bytes&&size>=20&&size%4==0&&size<=131072,"native clip fixture span invalid");
        const Module module(bytes,size);std::size_t store=0;bool mesh=false;
        for(const auto& i:module.code)if(i.op==15)mesh=module.word(i,1)==5365;
        for(const auto& i:module.code)if(i.op==62&&i.count==3&&module.word(i,1)==(mesh?3006u:5u)){require(!store,"native clip fixture Position ambiguous");store=i.at;}
        Contract contract{size,store,"test-only",NativeClipKind::visibility,477,15,16,1031};
        if(mesh){contract.execution_model=5365;contract.position_pointer=3006;contract.culled_binding=6;}
        const auto admitted=anchors(module,contract);auto words=transform(module,contract,admitted);
        out.words=std::move(words);out.original_digest=detail::sha256(bytes,size);out.kind=contract.kind;out.mesh=mesh;return true;
    }catch(const std::exception& error){try{reason=error.what();}catch(...){}}catch(...){try{reason="native clip fixture transform unavailable";}catch(...){}}return false;
}
#endif
}
