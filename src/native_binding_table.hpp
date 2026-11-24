#pragma once
#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace evrm {
// Small per-set payload table, not a lifetime/acceptance cache. Empty creation
// and moves allocate nothing. Inserts/erases invalidate references as in vector;
// callers must retain source values before mutating the same table.
template<class T>class NativeBindingTable {
    using Entry=std::pair<std::uint32_t,T>;
    std::vector<Entry> entries_;
    auto lower(std::uint32_t key){return std::lower_bound(entries_.begin(),entries_.end(),key,[](const Entry& entry,std::uint32_t value){return entry.first<value;});}
    auto lower(std::uint32_t key)const{return std::lower_bound(entries_.begin(),entries_.end(),key,[](const Entry& entry,std::uint32_t value){return entry.first<value;});}
public:
    using iterator=typename std::vector<Entry>::iterator;
    using const_iterator=typename std::vector<Entry>::const_iterator;
    iterator begin()noexcept{return entries_.begin();}
    const_iterator begin()const noexcept{return entries_.begin();}
    iterator end()noexcept{return entries_.end();}
    const_iterator end()const noexcept{return entries_.end();}
    std::size_t size()const noexcept{return entries_.size();}
    bool empty()const noexcept{return entries_.empty();}
    void clear()noexcept{std::vector<Entry>().swap(entries_);}
    iterator find(std::uint32_t key){const auto it=lower(key);return it!=end()&&it->first==key?it:end();}
    const_iterator find(std::uint32_t key)const{const auto it=lower(key);return it!=end()&&it->first==key?it:end();}
    std::pair<iterator,bool> emplace(std::uint32_t key,T value){
        auto it=lower(key);if(it!=end()&&it->first==key)return {it,false};
        return {entries_.emplace(it,key,std::move(value)),true};
    }
    T& operator[](std::uint32_t key){
        auto it=lower(key);if(it==end()||it->first!=key)it=entries_.emplace(it,key,T{});
        return it->second;
    }
    iterator erase(iterator it){return entries_.erase(it);}
    std::size_t erase(std::uint32_t key){const auto it=find(key);if(it==end())return 0;entries_.erase(it);return 1;}
};
}
