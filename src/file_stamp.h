#pragma once
#include <cstdint>
#include <string>
#include <windows.h>

namespace horse_races::file_stamp {
// A file's write time and size: two words that answer "has this changed?" without reading it. The
// reader that reads a whole file every second to compare its bytes was the single worst frame the mod
// costs outside a race (2026-09-21: 14 route files 5-10.5 ms, ArthurCamp.ini 7.9 ms, in one frame).
struct Stamp {
    std::uint64_t written=0,size=0;
    bool operator==(const Stamp&) const=default;
    bool Known() const {return written || size;}
};
inline Stamp Of(const std::string& path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if(!GetFileAttributesExA(path.c_str(),GetFileExInfoStandard,&data))return {};
    Stamp stamp;
    stamp.written=(static_cast<std::uint64_t>(data.ftLastWriteTime.dwHighDateTime)<<32)|data.ftLastWriteTime.dwLowDateTime;
    stamp.size=(static_cast<std::uint64_t>(data.nFileSizeHigh)<<32)|data.nFileSizeLow;
    // A file whose stamp cannot be read at all is reported as unknown, and an unknown stamp never
    // matches a remembered one - so a reader falls back to reading, never to skipping.
    if(!stamp.Known())stamp.written=1;
    return stamp;
}
}
