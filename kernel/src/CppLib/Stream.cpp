/*
    * stream.cpp
    * String stream/string writer
    * Copyright (c) 2025 Daniel Hammer
*/

#include "Stream.hpp"
#include <Memory/Heap.hpp>
#include <Terminal/Terminal.hpp>
#include <Libraries/String.hpp>
#include <Gui/DebugGui.hpp>

kcp::cstringstream::cstringstream()
{
    this->string = nullptr;
    this->size = 0;
}

kcp::cstringstream::~cstringstream()
{
    delete this->string;
}

kcp::cstringstream& kcp::cstringstream::operator<<(char c) {
    this->string = (char *)Memory::g_heap->Realloc((void *)this->string, this->size + 2);

    if (this->string == nullptr)
    {
        Gui::Log(Gui::LogLevel::Error, "kcp::cstringstream", "Character streaming failed due to failed allocation.");
        return *this;
    }
    
    char* ref = (char *)&string[size];
    *ref = c;

    ref++;
    *ref = '\0';

    this->size++;

    return *this;
}

kcp::cstringstream& kcp::cstringstream::operator<<(char* str) {
    while (*str != '\0')
    {
        *this << *str;
        str++;
    }

    return *this;
}

kcp::cstringstream& kcp::cstringstream::operator<<(const char* str) {
    *this << (char*)str;

    return *this;
}


kcp::cstringstream& kcp::cstringstream::operator<<(int num) {
    char* out_str = Lib::int2basestr(num, current_base);

    *this << out_str;
    return *this;
}

kcp::cstringstream& kcp::cstringstream::operator<<(uint32_t val) {
    char* out_str = Lib::uint2basestr(val, current_base);

    *this << out_str;
    return *this;
}

kcp::cstringstream& kcp::cstringstream::operator<<(uint64_t val) {
    char* out_str = Lib::u64_2_basestr(val, current_base);

    *this << out_str;
    return *this;
}

kcp::cstringstream& kcp::cstringstream::operator<<(base nb)
{
    current_base = nb;
    
    return *this;
}

const char* kcp::cstringstream::c_str() {
    return this->string;
}