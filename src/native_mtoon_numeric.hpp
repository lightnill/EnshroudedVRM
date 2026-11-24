#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
namespace evrm {
// Private data-only capture contract. No native source or rendering authority.
struct MToonNumeric {
    static constexpr std::uint32_t magic=0x4d544e43u,version=1;
    static constexpr std::uint32_t draw_limit=32,probe_count=16,record_words=384,record_bytes=record_words*4;
    static constexpr std::uint32_t probes_offset=64,draws_offset=320,records_offset=832;
    // Longest valid arming file: the 15-byte header and 16 lines of at most 15 bytes.
    static constexpr std::size_t arming_header=15,arming_limit=arming_header+probe_count*15;
};
struct MToonNumericProbe {std::uint32_t x=0,y=0,radius=14,id=0;};
struct MToonNumericConfig {bool enabled=false;std::array<MToonNumericProbe,MToonNumeric::probe_count> probes{};};
// The arming file, Development/lighting-capture.txt. Capture stays opt-in: no
// file, or any file not exactly one of these two forms, leaves it disarmed.
//
//   "EVRM-NUMERIC-1\n"              the measured 1920x1027 close-up probes below.
//   "EVRM-NUMERIC-2\n" then 1 to 16 lines "x y r\n"
//       x, y  probe centre as a fraction of the MToon work extent, 0..65535
//             (pixel*65535/(extent-1), rounded)
//       r     search radius in pixels of that extent, 1..64
//
// Numbers are decimal digits only: no sign, no leading zero, one space between
// them, LF line ends, and nothing after the last line's LF. Line n is label n.
// The shader and the frame take sixteen valid probes, so with fewer lines the
// remaining slots repeat the listed ones in order (slot s holds line s%count,
// with that line's label): they record further pixels near the same centres.
struct MToonNumericArming {MToonNumericConfig config;std::uint32_t version=0,count=0;bool rejected=false;};
inline MToonNumericConfig mtoon_numeric_legacy_config()noexcept{
    constexpr unsigned xy[][2]{{750,520},{727,537},{778,536},{725,549},{782,548},{727,580},{782,580},{752,596},
        {750,480},{710,515},{802,515},{752,630},{700,700},{800,700},{650,760},{900,760}};
    MToonNumericConfig out;out.enabled=true;
    for(unsigned n=0;n<MToonNumeric::probe_count;++n)out.probes[n]={unsigned((std::uint64_t(xy[n][0])*65535+959)/1919),unsigned((std::uint64_t(xy[n][1])*65535+513)/1026),14,n+1};
    return out;
}
// One number and the single character that must follow it.
inline bool mtoon_numeric_field(const char*& at,const char* end,char terminator,std::uint32_t limit,std::uint32_t& value)noexcept{
    const char* const first=at;value=0;
    while(at<end&&*at>='0'&&*at<='9'&&at-first<5){value=value*10u+std::uint32_t(*at-'0');++at;}
    if(at==first||(*first=='0'&&at-first>1)||at==end||*at!=terminator||value>limit)return false;
    ++at;return true;
}
// Fixed work and no allocation. `rejected` marks a non-empty file that armed
// nothing, so the caller can say so once instead of capturing nothing silently.
inline MToonNumericArming mtoon_numeric_arming(const char* data,std::size_t size)noexcept{
    MToonNumericArming out;out.rejected=size!=0;
    constexpr char legacy[]="EVRM-NUMERIC-1\n",listed[]="EVRM-NUMERIC-2\n";
    static_assert(sizeof(legacy)-1==MToonNumeric::arming_header&&sizeof(listed)-1==MToonNumeric::arming_header);
    if(!data||size<MToonNumeric::arming_header||size>MToonNumeric::arming_limit)return out;
    if(size==MToonNumeric::arming_header&&!std::memcmp(data,legacy,MToonNumeric::arming_header)){
        out.config=mtoon_numeric_legacy_config();out.version=1;out.count=MToonNumeric::probe_count;out.rejected=false;return out;}
    if(std::memcmp(data,listed,MToonNumeric::arming_header))return out;
    std::array<MToonNumericProbe,MToonNumeric::probe_count> lines{};std::uint32_t count=0;
    const char* at=data+MToonNumeric::arming_header;const char* const end=data+size;
    while(at<end){
        std::uint32_t x,y,radius;
        if(count==MToonNumeric::probe_count||!mtoon_numeric_field(at,end,' ',65535,x)||!mtoon_numeric_field(at,end,' ',65535,y)||
           !mtoon_numeric_field(at,end,'\n',64,radius)||!radius)return out;
        lines[count]={x,y,radius,count+1};++count;
    }
    if(!count)return out;
    for(std::uint32_t slot=0;slot<MToonNumeric::probe_count;++slot)out.config.probes[slot]=lines[slot%count];
    out.config.enabled=true;out.version=2;out.count=count;out.rejected=false;return out;
}
struct MToonNumericLayout {std::uint32_t draws=0;std::uint64_t bytes=0;explicit operator bool()const noexcept{return bytes!=0;}};
inline MToonNumericLayout mtoon_numeric_layout(std::uint32_t draws,bool enabled)noexcept{
    if(!enabled||!draws||draws>4096)return {};
    const auto count=draws<MToonNumeric::draw_limit?draws:MToonNumeric::draw_limit;
    return {count,MToonNumeric::records_offset+std::uint64_t(count)*MToonNumeric::probe_count*MToonNumeric::record_bytes};
}
struct MToonNumericBudget {
    unsigned count=0;std::uint64_t last=0;
    bool due(std::uint64_t now,bool armed)const noexcept{return armed&&count<128&&(!count||(now>=last&&now-last>=2000));}
    bool commit(std::uint64_t now,bool armed)noexcept{if(!due(now,armed))return false;++count;last=now;return true;}
};
struct MToonNumericSample {std::array<std::uint32_t,MToonNumeric::record_words> words{};};
inline bool mtoon_numeric_sample_valid(const MToonNumericSample& sample,std::uint32_t generation,
    std::uint32_t width,std::uint32_t height,std::uint32_t draws,std::uint32_t path)noexcept{
    const auto& w=sample.words;
    return generation&&width&&height&&draws&&draws<=MToonNumeric::draw_limit&&path<=1&&
        w[0]&&(w[0]>>1)&&w[8]==w[0]&&w[2]==generation&&w[3]<draws&&w[4]==path&&w[5]<MToonNumeric::probe_count&&
        w[6]<width&&w[7]<height&&((path==0&&w[1]==1)||(path==1&&(w[1]==2||w[1]==3)));
}
}
