#include "debug_report_archive.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <set>
#include <stdexcept>
namespace evrm {namespace {
using Bytes=std::vector<std::uint8_t>;
void require(bool ok){if(!ok)throw std::runtime_error("invalid bounded report data");}
void le16(Bytes& out,std::uint16_t value){out.push_back(value&255);out.push_back(value>>8);}
void le32(Bytes& out,std::uint32_t value){le16(out,value&65535);le16(out,value>>16);}
void be32(Bytes& out,std::uint32_t value){for(int shift=24;shift>=0;shift-=8)out.push_back(std::uint8_t(value>>shift));}
std::uint32_t read_be(const Bytes& bytes,std::size_t at){require(at<=bytes.size()&&bytes.size()-at>=4);return (std::uint32_t(bytes[at])<<24)|(std::uint32_t(bytes[at+1])<<16)|(std::uint32_t(bytes[at+2])<<8)|bytes[at+3];}
std::uint32_t adler(const Bytes& bytes){std::uint32_t a=1,b=0;for(auto value:bytes){a=(a+value)%65521;b=(b+a)%65521;}return (b<<16)|a;}
bool valid_image(const DebugReportImage& image){return image.width&&image.height&&image.width<=1920&&image.height<=1080&&image.rgba.size()==std::uint64_t(image.width)*image.height*4;}
void chunk(Bytes& out,const char* name,const Bytes& data){be32(out,std::uint32_t(data.size()));const auto start=out.size();out.insert(out.end(),name,name+4);out.insert(out.end(),data.begin(),data.end());be32(out,debug_report_crc32(out.data()+start,4+data.size()));}
bool allowed(std::string_view name){constexpr std::string_view names[]={"report.json","system.json","settings.json","model.json","scene.json","diagnostics.log","README.txt","screenshot.png"};for(auto value:names)if(name==value)return true;return false;}
}
std::uint32_t debug_report_crc32(const std::uint8_t* data,std::size_t size)noexcept{
    static const auto table=[](){std::array<std::uint32_t,256> result{};for(unsigned i=0;i<256;++i){auto value=std::uint32_t(i);for(unsigned bit=0;bit<8;++bit)value=(value>>1)^((value&1)?0xedb88320u:0);result[i]=value;}return result;}();
    std::uint32_t value=0xffffffffu;for(std::size_t i=0;i<size;++i)value=table[(value^data[i])&255]^(value>>8);return value^0xffffffffu;
}
bool write_debug_report_zip(const DebugReportContents& files,const DebugReportSink& sink)noexcept{try{
    if(!sink||files.size()<7||files.size()>8)return false;
    std::set<std::string> names;std::size_t total=22;
    for(const auto& file:files){if(!allowed(file.name)||!names.insert(file.name).second||file.bytes.size()>debug_report_archive_limit)return false;
        const auto required=file.bytes.size()+76+file.name.size()*2;if(required>debug_report_archive_limit-total)return false;total+=required;}
    for(const auto* name:{"report.json","system.json","settings.json","model.json","scene.json","diagnostics.log","README.txt"})if(!names.count(name))return false;
    struct Entry {const DebugReportFile* file;std::uint32_t crc,offset;};std::vector<Entry> entries;entries.reserve(files.size());std::uint32_t position=0;
    for(const auto& file:files){const auto crc=debug_report_crc32(file.bytes.data(),file.bytes.size());Bytes header;
        le32(header,0x04034b50);le16(header,20);le16(header,0x800);le16(header,0);le16(header,0);le16(header,0x21);
        le32(header,crc);le32(header,std::uint32_t(file.bytes.size()));le32(header,std::uint32_t(file.bytes.size()));le16(header,std::uint16_t(file.name.size()));le16(header,0);
        header.insert(header.end(),file.name.begin(),file.name.end());entries.push_back({&file,crc,position});
        if(!sink(header.data(),header.size())||(!file.bytes.empty()&&!sink(file.bytes.data(),file.bytes.size())))return false;
        position+=std::uint32_t(header.size()+file.bytes.size());
    }
    const auto central=position;
    for(const auto& entry:entries){const auto& file=*entry.file;Bytes header;
        le32(header,0x02014b50);le16(header,20);le16(header,20);le16(header,0x800);le16(header,0);le16(header,0);le16(header,0x21);
        le32(header,entry.crc);le32(header,std::uint32_t(file.bytes.size()));le32(header,std::uint32_t(file.bytes.size()));le16(header,std::uint16_t(file.name.size()));
        le16(header,0);le16(header,0);le16(header,0);le16(header,0);le32(header,0);le32(header,entry.offset);header.insert(header.end(),file.name.begin(),file.name.end());
        if(!sink(header.data(),header.size()))return false;
        position+=std::uint32_t(header.size());
    }
    Bytes end;le32(end,0x06054b50);le16(end,0);le16(end,0);le16(end,std::uint16_t(entries.size()));le16(end,std::uint16_t(entries.size()));le32(end,position-central);le32(end,central);le16(end,0);
    return position+end.size()==total&&sink(end.data(),end.size());
}catch(...){return false;}}
std::vector<std::uint8_t> encode_debug_report_png(const DebugReportImage& image)noexcept{try{
    if(!valid_image(image))return {};
    const auto stride=std::size_t(image.width)*4;Bytes raw;raw.reserve((stride+1)*image.height);
    for(unsigned y=0;y<image.height;++y){raw.push_back(0);const auto first=image.rgba.begin()+y*stride;raw.insert(raw.end(),first,first+stride);}
    Bytes zlib;zlib.reserve(raw.size()+raw.size()/65535*5+16);zlib.push_back(0x78);zlib.push_back(0x01);
    for(std::size_t offset=0;offset<raw.size();){const auto count=std::min<std::size_t>(65535,raw.size()-offset);zlib.push_back(offset+count==raw.size()?1:0);
        le16(zlib,std::uint16_t(count));le16(zlib,std::uint16_t(~std::uint16_t(count)));zlib.insert(zlib.end(),raw.begin()+offset,raw.begin()+offset+count);offset+=count;}
    be32(zlib,adler(raw));Bytes output={137,80,78,71,13,10,26,10};Bytes header;be32(header,image.width);be32(header,image.height);header.insert(header.end(),{8,6,0,0,0});
    chunk(output,"IHDR",header);chunk(output,"IDAT",zlib);chunk(output,"IEND",{});if(output.size()>debug_report_png_limit)return {};return output;
}catch(...){return {};}}
DebugReportImage decode_debug_report_png(const std::vector<std::uint8_t>& png)noexcept{try{
    constexpr std::uint8_t signature[]={137,80,78,71,13,10,26,10};require(png.size()>=57&&png.size()<=debug_report_png_limit&&std::memcmp(png.data(),signature,8)==0);
    DebugReportImage result;Bytes zlib;std::size_t offset=8;unsigned stage=0;
    while(offset<png.size()){
        require(png.size()-offset>=12);
        const auto count=read_be(png,offset);require(count<=png.size()-offset-12);const auto* name=png.data()+offset+4;
        require(read_be(png,offset+8+count)==debug_report_crc32(name,count+4));const auto first=png.begin()+offset+8;
        if(stage==0){require(std::memcmp(name,"IHDR",4)==0&&count==13);result.width=read_be(png,offset+8);result.height=read_be(png,offset+12);
            const std::array<std::uint8_t,5> format={8,6,0,0,0};require(std::equal(format.begin(),format.end(),first+8));require(result.width&&result.height&&result.width<=1920&&result.height<=1080);}
        else if(stage==1){require(std::memcmp(name,"IDAT",4)==0);zlib.assign(first,first+count);}
        else {require(stage==2&&std::memcmp(name,"IEND",4)==0&&count==0&&offset+12==png.size());}
        ++stage;offset+=count+12;
    }
    require(stage==3&&zlib.size()>=11&&zlib[0]==0x78&&zlib[1]==0x01);Bytes raw;const auto stride=std::size_t(result.width)*4;const auto expected=(stride+1)*result.height;
    raw.reserve(expected);offset=2;bool final=false;
    while(!final){require(offset<=zlib.size()&&zlib.size()-offset>=9);const auto flags=zlib[offset++];require(flags==0||flags==1);final=flags==1;
        const auto count=unsigned(zlib[offset])|(unsigned(zlib[offset+1])<<8),inverse=unsigned(zlib[offset+2])|(unsigned(zlib[offset+3])<<8);offset+=4;
        require((count^inverse)==65535&&count<=zlib.size()-offset-4&&count<=expected-raw.size());raw.insert(raw.end(),zlib.begin()+offset,zlib.begin()+offset+count);offset+=count;
    }
    require(offset+4==zlib.size()&&raw.size()==expected&&read_be(zlib,offset)==adler(raw));result.rgba.reserve(stride*result.height);
    for(unsigned y=0;y<result.height;++y){const auto first=raw.begin()+y*(stride+1);require(*first==0);result.rgba.insert(result.rgba.end(),first+1,first+1+stride);}
    return result;
}catch(...){return {};}}
}
