#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "steam_game_build.hpp"
#include <limits>

namespace evrm {namespace {
constexpr std::size_t byte_cap=65536,token_cap=4096,string_cap=4096;
bool same_ascii(std::string_view a,std::string_view b)noexcept{
    if(a.size()!=b.size())return false;
    for(std::size_t i=0;i<a.size();++i){const auto lower=[](char c){return c>='A'&&c<='Z'?char(c-'A'+'a'):c;};if(lower(a[i])!=lower(b[i]))return false;}
    return true;
}
bool decimal(std::string_view text,std::uint32_t& value)noexcept{
    if(text.empty()||text.size()>10)return false;value=0;
    for(const auto c:text){if(c<'0'||c>'9'||value>(std::numeric_limits<std::uint32_t>::max()-std::uint32_t(c-'0'))/10)return false;value=value*10+std::uint32_t(c-'0');}
    return value!=0;
}
bool directory(std::string_view text)noexcept{
    if(text.empty()||text.size()>255||text=="."||text==".."||text.back()=='.'||text.back()==' ')return false;
    for(const auto c:text)if(static_cast<unsigned char>(c)<32||std::string_view("/\\:\"<>|?*").find(c)!=std::string_view::npos)return false;
    return true;
}
enum class Kind {end,string,open,close,error};
struct Parser {
    std::string_view text;std::size_t at=0,tokens=0;unsigned seen=0;SteamGameBuild result;
    Kind next(std::string& value){
        value.clear();if(++tokens>token_cap)return Kind::error;
        for(;;){
            while(at<text.size()&&(text[at]==' '||text[at]=='\t'||text[at]=='\r'||text[at]=='\n'))++at;
            if(at+1<text.size()&&text[at]=='/'&&text[at+1]=='/'){while(at<text.size()&&text[at]!='\n')++at;continue;}break;
        }
        if(at==text.size())return Kind::end;
        const auto c=text[at++];if(c=='{')return Kind::open;if(c=='}')return Kind::close;if(c!='"')return Kind::error;
        while(at<text.size()){
            const auto item=text[at++];if(item=='"')return Kind::string;
            if(item=='\\'){
                if(at==text.size())return Kind::error;const auto escaped=text[at++];
                if(escaped!='\\'&&escaped!='"')value+='\\';value+=escaped;
            }else value+=item;
            if(value.size()>string_cap)return Kind::error;
        }
        return Kind::error;
    }
    bool object(unsigned depth){
        if(depth>16)return false;std::string key,value;
        for(;;){
            const auto kind=next(key);if(kind==Kind::close)return true;if(kind!=Kind::string)return false;
            unsigned bit=0;if(depth==1){if(same_ascii(key,"appid"))bit=1;else if(same_ascii(key,"buildid"))bit=2;else if(same_ascii(key,"installdir"))bit=4;}
            if(bit&&(seen&bit))return false;
            const auto value_kind=next(value);
            if(value_kind==Kind::open){if(bit||!object(depth+1))return false;}
            else if(value_kind!=Kind::string)return false;
            else if(bit){
                seen|=bit;
                if(bit==1&&!decimal(value,result.app_id))return false;
                if(bit==2&&!decimal(value,result.build_id))return false;
                if(bit==4){if(!directory(value))return false;result.install_directory=value;}
            }
        }
    }
};
bool same_path_part(const std::filesystem::path& a,const std::filesystem::path& b)noexcept{
    const auto& x=a.native();const auto& y=b.native();
    return x.size()<=32767&&y.size()<=32767&&CompareStringOrdinal(x.data(),int(x.size()),y.data(),int(y.size()),TRUE)==CSTR_EQUAL;
}
struct File {HANDLE handle=INVALID_HANDLE_VALUE;~File(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}};
}
SteamGameBuild parse_steam_game_build(std::string_view text)noexcept{
    try{
        if(text.empty()||text.size()>byte_cap||text.find('\0')!=text.npos)return {};
        if(text.substr(0,3)=="\xef\xbb\xbf")text.remove_prefix(3);
        Parser p{text};std::string token;
        if(p.next(token)!=Kind::string||!same_ascii(token,"AppState")||p.next(token)!=Kind::open||!p.object(1)||
            p.next(token)!=Kind::end||p.seen!=7||!p.result)return {};
        return p.result;
    }catch(...){return {};}
}
SteamGameBuild read_steam_game_build(const std::filesystem::path& executable)noexcept{
    try{
        if(!executable.is_absolute()||!same_path_part(executable.filename(),L"enshrouded.exe"))return {};
        const auto path=executable.lexically_normal(),game=path.parent_path(),common=game.parent_path(),apps=common.parent_path();
        if(!same_path_part(common.filename(),L"common")||!same_path_part(apps.filename(),L"steamapps"))return {};
        File file;file.handle=CreateFileW((apps/L"appmanifest_1203620.acf").c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
            nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        BY_HANDLE_FILE_INFORMATION before{},after{};
        if(file.handle==INVALID_HANDLE_VALUE||!GetFileInformationByHandle(file.handle,&before)||before.nFileSizeHigh||
            !before.nFileSizeLow||before.nFileSizeLow>byte_cap||(before.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))return {};
        std::string bytes(before.nFileSizeLow,'\0');DWORD got=0;
        if(!ReadFile(file.handle,bytes.data(),before.nFileSizeLow,&got,nullptr)||got!=before.nFileSizeLow||!GetFileInformationByHandle(file.handle,&after)||
            before.dwVolumeSerialNumber!=after.dwVolumeSerialNumber||before.nFileIndexHigh!=after.nFileIndexHigh||before.nFileIndexLow!=after.nFileIndexLow||
            before.nFileSizeHigh!=after.nFileSizeHigh||before.nFileSizeLow!=after.nFileSizeLow||CompareFileTime(&before.ftLastWriteTime,&after.ftLastWriteTime)!=0)return {};
        auto parsed=parse_steam_game_build(bytes);
        if(!parsed||!same_path_part(game.filename(),std::filesystem::u8path(parsed.install_directory)))return {};
        return parsed;
    }catch(...){return {};}
}
}
