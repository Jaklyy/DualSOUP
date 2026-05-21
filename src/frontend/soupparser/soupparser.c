#include <SDL3/SDL_error.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_mutex.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include <SDL3/SDL_iostream.h>

#include "../../core/utils.h"
#include "soupparser.h"



FILE* FindFileWithSameName(const char* path, const char* ext, const char* mode)
{
    char newpath[KiB(4)]; // if you make a longer file path i *will* cry.
    size_t extlen = strlen(ext);
    strncpy(newpath, path, KiB(4)-extlen-1);
    char* end = strrchr(newpath, '.'); // find extension marker

    if (end == NULL)
    {
        printf("ERROR: Strange File Path?\n");
        return NULL;
    }

    // add extension.
    strcpy(&end[1], ext);

    FILE* file = fopen(newpath, mode);
    if (file != NULL) LogPrint(LOG_ALWAYS, ".%s located: %s\n", ext, newpath);
    return file;
}

bool SOUPParser(const char* haystack, const char* needle, const char* cmpstr, const u8 type, void* ret)
{
    const char* spoon = strstr(haystack, needle);

    u64 offs = strlen(needle);
    if (spoon != NULL)
    {
        switch(type)
        {
        case SEARCH_S32DEC:
            *(u32*)ret = (u32)strtoll(&spoon[offs], NULL, 10);
            return true;
        case SEARCH_U32DEC:
            *(u32*)ret = (u32)strtoull(&spoon[offs], NULL, 10);
            return true;
        case SEARCH_U32HEX:
            *(u32*)ret = (u32)strtoull(&spoon[offs], NULL, 16);
            return true;
        case SEARCH_S64DEC:
            *(u64*)ret = strtoll(&spoon[offs], NULL, 10);
            return true;
        case SEARCH_U64DEC:
            *(u64*)ret = strtoull(&spoon[offs], NULL, 10);
            return true;
        case SEARCH_U64HEX:
            *(u64*)ret = strtoull(&spoon[offs], NULL, 16);
            return true;
        case SEARCH_STRING:
            while(spoon[offs] == ' ') offs++;
            return memcmp(&spoon[offs], cmpstr, strlen(cmpstr)) == 0;
        case SEARCH_EXISTS:
            return true;
        default:
            CrashSpectacularly("INVALID SOUP PARSER TYPE: %"PRIu8"???\n", type);
        }
    }
    return false;
}


typedef union
{
    void* cfg;
    u8* u8;
    s8* s8;
    u16* u16;
    s16* s16;
    u32* u32;
    s32* s32;
    u64* u64;
    s64* s64;
    int* integer;
    char** str;
    bool* boolean;
    float* floating;
} pun;

// TODO: re-use for ALL config types
void Config_Write(const char* path, void* cfgin, const ConfigEntry* cfgref, const size_t cfgnum, bool* dirtyflag, SDL_Mutex* mutex)
{
    SDL_IOStream* file = SDL_IOFromFile(path, "wt");
    if (mutex != NULL) SDL_LockMutex(mutex);

    for (size_t i = 0; i < cfgnum; i++)
    {
        pun pun = {.cfg = cfgin};

        const ConfigEntry entry = cfgref[i];
        const char* entrystr = entry.Name;
        char* outstr;
        size_t baselen = strlen(entrystr)+sizeof(char);
        size_t fulllen = baselen+sizeof(char[2]);

        bool valstrneedssdlfree = false;
        const char* valstr;
        switch(entry.Type)
        {
        case SEARCH_NULL: CrashSpectacularly("Jakly i am going to kill you. you forgot to add a search type to entry %zu\n", i);
        default: printf("WRITE: UNHANDLED CONFIG ENUM TYPE FOR ENTRY: %zu!!!\n", i); continue;

#define STOREENUM(searchtype, type, prefix, fmt) \
    case SEARCH_##searchtype: \
    { \
        if (entry.EnumNames == NULL) CrashSpectacularly("Jakly you dumb motherfucker you forgot to define literally any enum names for entry %zu\n", i); \
        typeof(pun.type[0]) val = pun.type[entry.Offset/sizeof(pun.type[0])]; \
        if (val >= entry.prefix##MaxVal) \
        { \
            printf("Config Error: value of %s == %"fmt" >= max value of: %zu", entrystr, val, entry.prefix##MaxVal); \
            continue; \
        } \
        valstr = entry.EnumNames[val]; \
        break; \
    }

        STOREENUM(ENUMU8, u8, U, PRIu8)
        STOREENUM(ENUMINT, integer, S, "i")

#undef STOREENUM

#define STOREVAL(searchtype, fmt, type) \
        case SEARCH_##searchtype: \
        { \
            SDL_asprintf((char**)&valstr, fmt, pun.type[entry.Offset/sizeof(pun.type[0])]); \
            break; \
        }

        STOREVAL(INT, "%i", integer)
        STOREVAL(FLOAT, "%f", floating)
        STOREVAL(U16HEX, "%"PRIX16, u16)

#undef STOREVAL

        case SEARCH_STRING:
        {
            valstr = pun.str[entry.Offset/sizeof(pun.str[0])];
            break;
        }
        case SEARCH_BOOL:
        {
            valstr = boolnames[pun.boolean[entry.Offset/sizeof(pun.boolean[0])]];
            break;
        }
        }
        fulllen += strlen(valstr);
        outstr = malloc(fulllen);
        strcpy(outstr, entrystr);
        strcpy(&outstr[baselen], valstr);

        if (valstrneedssdlfree) SDL_free((char**)valstr);

        // add some filler
        // string should now look like: "entrystr=valstr\0\n"
        outstr[baselen-1] = '=';
        outstr[fulllen-1] = '\n'; // string is no longer 0 terminated

        SDL_WriteIO(file, outstr, fulllen);
        free(outstr);
    }

    *dirtyflag = false;
    if (mutex != NULL) SDL_UnlockMutex(mutex);
    if (!SDL_CloseIO(file)) printf("%s\n", SDL_GetError());
}

void Config_Load(const char* path, void* cfgout, const ConfigEntry* cfgref, const size_t cfgnum, bool* dirtyflag, SDL_Mutex** mutex)
{
    SDL_IOStream* file;
    size_t cfgsize;
    char* cfgdat = NULL;
    bool loaddefaults = true;
    bool dirty = false;
    pun pun = {.cfg = cfgout};

    if (mutex == NULL)
    {
        if (*mutex == NULL)
        {
            *mutex = SDL_CreateMutex();
            if (*mutex == NULL)
            {
                printf("ERROR: setting mutex creation failure... %s\n", SDL_GetError());
                exit(EXIT_FAILURE);
            }
        }
        SDL_LockMutex(*mutex);
    }

    if (path != NULL)
    {
        if ((file = SDL_IOFromFile(path, "rt")) != NULL)
        {
            if ((s64)(cfgsize = SDL_SeekIO(file, 0, SDL_IO_SEEK_END)) > 0)
            {
                if ((cfgdat = malloc(cfgsize)) == NULL)
                {
                    printf("FATAL: malloc failed.\n");
                    exit(EXIT_FAILURE);
                }
                if (SDL_SeekIO(file, 0, SDL_IO_SEEK_SET) != -1)
                {
                    if (SDL_ReadIO(file, cfgdat, cfgsize) == cfgsize)
                        loaddefaults = false;
                    else
                    {
                        // todo: actually check error
                        printf("Cfg file read error.\n");
                    }
                }
                else printf("%s\n", SDL_GetError());
            }
            else
            {
                if (cfgsize == 0) printf("Note: config file empty?\n");
                else printf("%s\n", SDL_GetError());
            }
            if (!SDL_CloseIO(file)) printf("%s\n", SDL_GetError());
        }
        else printf("%s\n", SDL_GetError());
    }

    bool initialized[cfgnum] = {};
    for (size_t nument = 0, pos = 0; nument < cfgnum; nument++)
    {
        size_t i = 0;
        if (!loaddefaults)
        {
            while (true)
            {
                i = 0;
                if (pos >= cfgsize)
                {
                    loaddefaults = true;
                    break;
                }
                // find a valid command
                for (; i < cfgnum; i++)
                {
                    if (initialized[i]) continue;
                    size_t namelen = strlen(cfgref[i].Name);
                    if ((pos+namelen) >= cfgsize) continue; // buffer overflow; must be invalid
                    if ((memcmp(&cfgdat[pos], cfgref[i].Name, namelen) == 0) && (cfgdat[pos+namelen] == '='))
                    {
                        // string match found: continue to next step
                        pos += namelen+1;
                        goto found;
                    }
                }

                // try to find the string: "\0\n" (not a valid C string, due to containing a null terminating character, so we search manually)
                for (; pos < cfgsize; pos++)
                {
                    if ((cfgdat[pos] == '\0') && (cfgdat[pos+1] == '\n'))
                    {
                        pos+=2;
                        break; 
                    }
                }
            }
        }
        if (loaddefaults)
        {
            i = 0;
            for (; i < cfgnum; i++)
            {
                if (!initialized[i]) break;
            }
        }
        found:

        const ConfigEntry entry = cfgref[i];

        switch(entry.Type)
        {
        case SEARCH_NULL: CrashSpectacularly("Jakly i am going to kill you. you forgot to add a search type to entry %zu\n", i);
        default: printf("LOAD: UNHANDLED CONFIG ENUM TYPE FOR ENTRY: %zu!!!\n", i); break;

        case SEARCH_ENUMU8:
        case SEARCH_ENUMINT:
        case SEARCH_BOOL:
        {
            if ((entry.Type != SEARCH_BOOL) && (entry.EnumNames == NULL)) CrashSpectacularly("Jakly you dumb motherfucker you forgot to define literally any enum names for entry %zu\n", i);
            bool success = false;
            u8 j = (entry.Type == SEARCH_BOOL) ? 0 : entry.UMinVal;
            if (!loaddefaults)
            {
                u64 max = (entry.Type == SEARCH_BOOL) ? 2 : entry.UMaxVal;
                const char** names = (entry.Type == SEARCH_BOOL) ? boolnames : entry.EnumNames;
                for (; j < max; j++)
                {
                    size_t enumlen = strlen(names[j]);
                    if ((pos+enumlen) >= cfgsize) continue; // buffer overflow; must be invalid
                    if (memcmp(&cfgdat[pos], names[j], enumlen) == 0)
                    {
                        // match found.
                        pos += enumlen + 2;
                        success = true;
                        break;
                    }
                }
            }

            if (loaddefaults || !success)
            {
                j = (entry.Type == SEARCH_BOOL) ? entry.BDefVal : entry.UDefVal;
                dirty = true;
            }

            switch(entry.Type)
            {
            case SEARCH_ENUMU8: pun.u8[entry.Offset/sizeof(pun.u8[0])] = j; break;
            case SEARCH_ENUMINT: pun.integer[entry.Offset/sizeof(pun.integer[0])] = j; break;
            case SEARCH_BOOL: pun.boolean[entry.Offset/sizeof(pun.boolean[0])] = j; break;
            default: CrashSpectacularly("JAKLY THE ENUMS: %zu\n", i);
            }
            break;
        }

        case SEARCH_STRING:
        {
            size_t vallen;
            if (!loaddefaults && ((vallen = strnlen(&cfgdat[pos], cfgsize-pos-1)) != (cfgsize-pos-1)))
            {
                pun.str[entry.Offset/sizeof(pun.str[0])] = malloc(vallen+1);
                strcpy(pun.str[entry.Offset/sizeof(pun.str[0])], &cfgdat[pos]);
                pos += vallen + 2;
            }
            else
            {
                pun.str[entry.Offset/sizeof(pun.str[0])] = malloc(1);
                (pun.str[entry.Offset/sizeof(pun.str[0])])[0] = 0;
                dirty = true;
            }
            break;
        }

#define READNUMBER(search, type, numchk, prefix, numberget, ...) \
    case SEARCH_##search: \
    { \
        typeof(pun.type[0]) val; \
        size_t vallen; \
        if (!loaddefaults && (numchk(cfgdat[pos])) && ((vallen = strnlen(&cfgdat[pos], cfgsize-pos-1)) != (cfgsize-pos-1))) \
        { \
            val = numberget(&cfgdat[pos], __VA_ARGS__); \
            DS_CLAMP(val, <, entry.prefix##MinVal) \
            DS_CLAMP(val, >, entry.prefix##MaxVal) \
        } \
        else \
        { \
            val = entry.prefix##DefVal; \
            dirty = true; \
        } \
        pun.type[entry.Offset/sizeof(pun.type[0])] = val; \
        break; \
    } \

        READNUMBER(INT, integer, isdigit, S, strtoll, NULL, 10)
        READNUMBER(FLOAT, floating, isdigit, F, strtof, NULL)
        READNUMBER(U16HEX, u16, isxdigit, U, strtoll, NULL, 16)

#undef READNUMBER

        }
        initialized[i] = true;
    }

    if (dirtyflag != NULL) *dirtyflag = (dirty || loaddefaults);

    if (mutex != NULL) SDL_UnlockMutex(*mutex);

    if (cfgdat != NULL) free(cfgdat);
}
