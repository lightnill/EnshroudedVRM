#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
namespace evrm {
namespace report_log_detail {
inline bool numeric(std::string_view text,double& result)noexcept {
    if(text.empty()||text.size()>32)return false;
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),result);
    return parsed.ec==std::errc{}&&parsed.ptr==text.data()+text.size()&&std::isfinite(result)&&result>=0&&result<=1e12;
}
inline std::string_view field(std::string_view text,std::string_view name)noexcept {
    std::size_t at=0;
    while((at=text.find(name,at))!=std::string_view::npos){
        const auto end=at+name.size();if((at==0||text[at-1]==' ')&&end<text.size()&&text[end]=='='){
            const auto first=end+1,last=text.find_first_of(" ,}\r\n\t",first);return text.substr(first,last==std::string_view::npos?last:last-first);}
        at=end;
    }return {};
}
inline bool known(std::string_view name)noexcept {
    constexpr std::string_view names[]={"native-submit-stages","native-replacement.coverage","native-replacement.owner-census-refreshed",
        "native-replacement.driver-present-ms","tool-slots.applied","avatar.native-pose-published","probe.async.stats",
        "avatar-fallback.pose-gap","avatar-fallback.pose-recovered","avatar-fallback.gpu-gap","avatar-fallback.gpu-recovered",
        "avatar-fallback.submit-gap","avatar-fallback.submit-recovered","standalone.startup","graphics.swapchain","engine.graphics"};
    for(auto value:names)if(value==name)return true;
    return false;
}
struct Row {std::uint64_t tick=0;std::array<char,2040> text{};};
static_assert(sizeof(Row)==2048,"bounded safe-event storage");
struct Ring {std::mutex mutex;std::array<Row,512> rows{};std::size_t cursor=0,count=0;std::uint64_t overwritten=0;std::atomic<std::uint64_t> dropped{0};};
inline Ring& ring(){static Ring value;return value;}
}
// Unknown text is never copied. Only fixed event names and complete numeric
// tokens from explicitly approved fields can reach the recent/export log.
inline std::string debug_report_safe_event(std::string_view source)noexcept {
    try{
        if(source.size()>4096)source=source.substr(0,4096);
        auto end=source.find(' ');auto name=source.substr(0,end);
        if(!name.empty()&&name.front()>='0'&&name.front()<='9'){
            if(end==std::string_view::npos)return {};
            source.remove_prefix(end+1);end=source.find(' ');
            if(source.substr(0,4)=="pid="){if(end==std::string_view::npos)return {};source.remove_prefix(end+1);}
            end=source.find(' ');name=source.substr(0,end);
        }
        if(name=="native-prepare-compile"){
            const auto key=report_log_detail::field(source,"key"),ready=report_log_detail::field(source,"ready");
            const auto elapsed=report_log_detail::field(source,"elapsed-ms");double duration=0;
            if((key!="menu"&&key!="surface-world"&&key!="surface-menu")||(ready!="0"&&ready!="1")||
               !report_log_detail::numeric(elapsed,duration)||duration>600000)return {};
            // Fixed pipeline kinds and bounded timing only; no driver text,
            // paths, model names, native handles or player identifiers.
            return std::string(name)+" key="+std::string(key)+" ready="+std::string(ready)+" elapsed-ms="+std::string(elapsed);
        }
        if(name=="avatar-fallback.player-owner-gap"||name=="avatar-fallback.player-owner-recovered"){
            constexpr std::string_view keys[]={"episode","failures","gap-ms","stage","requested","selected","local-requested","local-selected",
                "owner-phase","owner-failure","captured-phase","detail-code","calls","bytes","elapsed-ms"};
            constexpr std::uint64_t maxima[]={1000000000000ull,1000000000000ull,1000000000000ull,5,16,16,1,1,10,9,10,5,100000,67108864,60000};
            std::array<std::uint64_t,std::size(keys)> values{};std::string output(name);
            for(std::size_t i=0;i<std::size(keys);++i){const auto token=report_log_detail::field(source,keys[i]);if(token.empty())return {};
                const auto parsed=std::from_chars(token.data(),token.data()+token.size(),values[i]);
                if(parsed.ec!=std::errc{}||parsed.ptr!=token.data()+token.size()||values[i]>maxima[i])return {};
                output+=' ';output+=keys[i];output+='=';output+=std::to_string(values[i]);}
            if(!values[0]||!values[1]||!values[4]||values[5]>values[4]||values[6]>values[4]||values[7]>values[5]||
                values[7]>values[6]||values[5]-values[7]>values[4]-values[6])return {};
            return output;
        }
        if(name=="avatar-fallback.player-preparation"){
            constexpr std::string_view keys[]={"subjects","local-prepared","remote-prepared","local-unprepared","remote-unprepared",
                "unavailable","oit-near-plane","oit-other","before-oit"};
            std::array<std::uint32_t,std::size(keys)> values{};std::string output(name);
            for(std::size_t i=0;i<std::size(keys);++i){
                const auto token=report_log_detail::field(source,keys[i]);if(token.empty())return {};
                const auto parsed=std::from_chars(token.data(),token.data()+token.size(),values[i]);
                if(parsed.ec!=std::errc{}||parsed.ptr!=token.data()+token.size()||values[i]>16)return {};
                output+=' ';output+=keys[i];output+='=';output+=std::to_string(values[i]);
            }
            if(values[0]<2||values[1]+values[2]+values[3]+values[4]+values[5]!=values[0]||
               values[6]+values[7]+values[8]!=values[3]+values[4])return {};
            return output;
        }
        // Code-group numbers are ordinals in the pinned original game's
        // exception table. They are not addresses or player identifiers.
        if(name=="preview-ownership.sample"){
            constexpr std::string_view keys[]={"stage","path-mask","stack-depth","game-depth","group-0","group-1","group-2","group-3","group-4","group-5","elapsed-us","age-ms"};
            constexpr std::uint32_t maxima[]={3,15,64,6,72878,72878,72878,72878,72878,72878,1000000,120000};
            std::string output(name);bool stage=false,path=false;
            for(std::size_t i=0;i<std::size(keys);++i){const auto token=report_log_detail::field(source,keys[i]);if(token.empty())continue;
                std::uint32_t value=0;const auto parsed=std::from_chars(token.data(),token.data()+token.size(),value);
                if(parsed.ec!=std::errc{}||parsed.ptr!=token.data()+token.size()||value>maxima[i]||(i==0&&!value))return {};
                stage=stage||i==0;path=path||i==1;output+=' ';output+=keys[i];output+='=';output+=std::to_string(value);}
            return stage&&path?output:std::string{};
        }
        if(name=="preview-handoff.sample"){
            constexpr std::string_view keys[]={"stage","origin","request","flags","age-ms"};
            constexpr std::uint32_t maxima[]={6,6,384,255,120000};
            std::string output(name);
            for(std::size_t i=0;i<std::size(keys);++i){const auto token=report_log_detail::field(source,keys[i]);if(token.empty())return {};
                std::uint32_t value=0;const auto parsed=std::from_chars(token.data(),token.data()+token.size(),value);
                if(parsed.ec!=std::errc{}||parsed.ptr!=token.data()+token.size()||value>maxima[i]||(i==0&&!value))return {};
                output+=' ';output+=keys[i];output+='=';output+=std::to_string(value);}
            return output;
        }
        if(!report_log_detail::known(name))return {};
        std::string output(name);
        constexpr std::string_view fields[]={"samples","mean-ms","worst-ms","average","maximum","count","elapsed-ms","guards",
            "common-accepted","common-declined","ring-full","rejected","normal-host-waits","episode","failures","gap-ms",
            "roles","view","publication","partial","retained-views","owner-filter","account-present","association-valid","preview-tagged","partial-policy","partial-eligible","owner-models","submitted-records",
            "selected-records","unique-selected","catalog-misses","age-ms","files","hashed-bytes","format","usage","code","reason-mask","suppressed-episodes","reason-overflow","cpu-permit","eligible","tainted","invalid","ended","refresh-attempted","refresh-verified"};
        for(auto key:fields){const auto token=report_log_detail::field(source,key);double number=0;
            if(!report_log_detail::numeric(token,number))continue;
            if((key=="owner-filter"||key=="account-present"||key=="association-valid"||key=="preview-tagged"||key=="partial-policy"||key=="partial-eligible")&&
                (number>1||std::floor(number)!=number))continue;
            if(key=="code"&&(name!="engine.graphics"||number<1||number>6||std::floor(number)!=number))continue;
            if(output.size()+key.size()+token.size()+2>=1900)break;
            output+=' ';output+=key;output+='=';output+=token;
        }
        if(name=="native-submit-stages")for(auto key:{"wait","retry","views","history","gates","owner-verify","refresh","capture","refresh-verify","final-checks","finalize"}){
            const auto prefix=" "+std::string(key)+"={mean:";const auto first=source.find(prefix);if(first==std::string_view::npos)continue;
            const auto middle=source.find(",worst:",first+prefix.size()),last=source.find('}',first+prefix.size());if(middle==std::string_view::npos||last==std::string_view::npos||middle>=last||last-first>128)continue;
            const auto mean=source.substr(first+prefix.size(),middle-first-prefix.size()),worst=source.substr(middle+7,last-middle-7);double number=0;
            if(!report_log_detail::numeric(mean,number)||!report_log_detail::numeric(worst,number)||output.size()+last-first+1>=2039)continue;
            output+=prefix;output+=mean;output+=",worst:";output+=worst;output+='}';}
        if(name.substr(0,16)=="avatar-fallback."){
            const auto phase=report_log_detail::field(source,"phase");for(auto allowed:{"begin","presentation","async-capture","sync-capture","held-pending","view-budget","view-admission","view-pose","retain-views","cpu-admitted-submit"})if(phase==allowed){output+=" phase=";output+=phase;break;}
            const auto decision=report_log_detail::field(source,"decision");if(decision=="accepted"||decision=="declined"){output+=" decision=";output+=decision;}
        }
        return output.size()>name.size()?output:std::string{};
    }catch(...){return {};}
}
inline void debug_report_log_event(std::string_view event,std::uint64_t tick)noexcept {
    try{const auto safe=debug_report_safe_event(event);if(safe.empty())return;auto& ring=report_log_detail::ring();
        std::unique_lock<std::mutex> lock(ring.mutex,std::try_to_lock);if(!lock.owns_lock()){++ring.dropped;return;}
        auto& row=ring.rows[ring.cursor];row.tick=tick;row.text.fill(0);std::memcpy(row.text.data(),safe.data(),safe.size());
        if(ring.count==ring.rows.size())++ring.overwritten;
        ring.cursor=(ring.cursor+1)%ring.rows.size();ring.count=std::min(ring.count+1,ring.rows.size());
    }catch(...){}
}
inline std::array<std::uint64_t,2> debug_report_log_omissions(){auto& ring=report_log_detail::ring();std::lock_guard<std::mutex> lock(ring.mutex);return {ring.dropped.load(),ring.overwritten};}
inline std::string debug_report_recent_log(std::uint64_t request_tick) {
    auto& ring=report_log_detail::ring();std::lock_guard<std::mutex> lock(ring.mutex);std::string result;result.reserve(ring.count*128);
    const auto first=(ring.cursor+ring.rows.size()-ring.count)%ring.rows.size();
    for(std::size_t i=0;i<ring.count;++i){const auto& row=ring.rows[(first+i)%ring.rows.size()];
        if(row.tick>request_tick||request_tick-row.tick>120000)continue;
        const auto text=std::string_view(row.text.data());
        if(result.size()+text.size()+32>1024*1024)break;
        result+=text;result+=" age-ms=";result+=std::to_string(request_tick-row.tick);result+='\n';}
    return result;
}
}
