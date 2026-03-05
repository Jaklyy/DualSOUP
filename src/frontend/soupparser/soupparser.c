#include <string.h>
#include <stdlib.h>
#include "../../core/utils.h"
#include "soupparser.h"



FILE* FindFileWithSameName(const char* path, const char* ext, const char* mode)
{
    char newpath[512]; // if you make a longer file path i *will* cry.
    u64 extlen = strlen(ext);
    strncpy(newpath, path, 512-extlen);
    char* end = strrchr(newpath, '.'); // find extension marker

    if (end == NULL)
    {
        printf("ERROR: Strange File Path?\n");
        return NULL;
    }

    // add extension.
    strcpy(end+1, ext);

    FILE* file = fopen(newpath, mode);
    if (file != NULL) LogPrint(LOG_ALWAYS, ".%s located: %s\n", ext, newpath);
    return file;
}

bool SOUPParser(const char* haystack, const char* needle, const char* cmpstr, const u8 type, void* ret)
{
    const char* spoon = strstr(haystack, needle) + strlen(needle);

    if (spoon != NULL)
    {
        switch (type)
        {
        case SEARCH_S32DEC:
            *(u32*)ret = strtol(spoon, NULL, 10);
            return true;
        case SEARCH_U32DEC:
            *(u32*)ret = strtoul(spoon, NULL, 10);
            return true;
        case SEARCH_U32HEX:
            *(u32*)ret = strtoul(spoon, NULL, 16);
            return true;
        case SEARCH_S64DEC:
            *(u64*)ret = strtol(spoon, NULL, 10);
            return true;
        case SEARCH_U64DEC:
            *(u64*)ret = strtoul(spoon, NULL, 10);
            return true;
        case SEARCH_U64HEX:
            *(u64*)ret = strtoul(spoon, NULL, 16);
            return true;
        case SEARCH_STRING:
            int i = 0;
            while(spoon[i] == ' ') i++;
            return memcmp(&spoon[i], cmpstr, strlen(cmpstr)) == 0;
        case SEARCH_EXISTS:
            return true;
        default:
            CrashSpectacularly("INVALID SOUP PARSER TYPE: %i???\n", type);
        }
    }
    return false;
}
