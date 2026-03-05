#pragma once
#include <stdio.h>
#include "../../core/utils.h"




enum SOUPParserTypes
{
    SEARCH_S32DEC,
    SEARCH_U32DEC,
    SEARCH_U32HEX,
    SEARCH_S64DEC,
    SEARCH_U64DEC,
    SEARCH_U64HEX,
    SEARCH_STRING,
    SEARCH_EXISTS,
};

FILE* FindFileWithSameName(const char* path, const char* ext, const char* mode);
bool SOUPParser(const char* haystack, const char* needle, const char* cmpstr, const u8 type, void* ret);
