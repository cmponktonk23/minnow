#pragma once

#include <string>
#include <cstdint>

using namespace std;

struct Segment
{
public:
    uint64_t first_index_;
    bool SYN_;
    bool FIN_;
    string data_;
};
