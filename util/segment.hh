#pragma once

#include <string>
#include <cstdint>

using namespace std;

struct Segment
{
public:
    uint64_t first_index;
    bool SYN;
    bool FIN;
    string payload;

    size_t sequence_length() const { return SYN + payload.size() + FIN; }
};
