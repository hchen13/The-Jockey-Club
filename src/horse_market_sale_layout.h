#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
namespace horse_races::horse_market_sale::detail {
using Cell=std::uint64_t;
using Saved=std::array<Cell,436>;
using Active=std::array<Cell,45>;
// Literal writes in shop_horse_shop_sp.c func_823 / func_1398..1406 /
// func_2116. Script array count cells and omitted fields remain intact.
inline Saved ResetSaved(Saved row){
    const auto original=row;
    std::fill_n(row.begin(),8,0);row[8]=row[9]=row[10]=row[11]=0;
    row[12]=static_cast<std::uint32_t>(-1);row[13]=static_cast<std::uint32_t>(-15);row[14]=0;
    for(int i=0;i<3;++i){int b=16+i*94;for(int j:{0,2,3,6,7,8,9,91,92,93})row[b+j]=0;row[b+4]=static_cast<std::uint32_t>(-1);std::fill_n(row.begin()+b+11,80,0);}
    std::fill_n(row.begin()+299,57,0);std::fill_n(row.begin()+357,15,0);
    row[372]=row[373]=0;std::fill_n(row.begin()+375,23,0);
    for(int i=0;i<2;++i){int b=399+i*4;std::fill_n(row.begin()+b,4,0);row[b]=100;}
    for(int i=0;i<3;++i){int b=408+i*4;std::fill_n(row.begin()+b,4,0);if(i==2)row[b]=50;}
    row[420]=0;row[421]=std::bit_cast<std::uint32_t>(-1.0f);
    row[422]=0;row[423]=static_cast<std::uint32_t>(-1);row[424]=static_cast<std::uint32_t>(-15);
    row[425]=0;row[426]=static_cast<std::uint32_t>(-15);
    std::fill_n(row.begin()+427,4,0);row[431]=row[432]=static_cast<std::uint32_t>(-1);
    row[433]=row[434]=row[435]=0;
    // Script integer/float assignments address the low word. The name clear is
    // an actual 64-byte string operation; preserve other upper-cell bits.
    for(std::size_t i=8;i<row.size();++i)row[i]=(original[i]&0xffffffff00000000ULL)|static_cast<std::uint32_t>(row[i]);
    return row;
}
inline Active ResetActive(Active row){
    const auto original=row;
    std::fill_n(row.begin(),4,0);
    std::fill(row.begin()+19,row.end(),0);
    row[44]=static_cast<std::uint32_t>(-1);
    for(std::size_t i=0;i<row.size();++i)row[i]=(original[i]&0xffffffff00000000ULL)|static_cast<std::uint32_t>(row[i]);
    return row;
}
}
