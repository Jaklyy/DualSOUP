#include <SDL3/SDL_error.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_mutex.h>
#include <string.h>
#include <stdlib.h>

#include <SDL3/SDL_iostream.h>

#include "../../core/utils.h"
#include "soupparser.h"



FILE* FindFileWithSameName(const char* path, const char* ext, const char* mode)
{
    char newpath[FileLengthMax]; // if you make a longer file path i *will* cry.
    size_t extlen = strlen(ext);
    strncpy(newpath, path, FileLengthMax-extlen-1);
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
            CrashSpectacularly("INVALID SOUP PARSER TYPE: %i???\n", type);
        }
    }
    return false;
}

// TODO: re-use for ALL config types
void Config_Write(const char* path, CoreCfg* corecfg)
{
    char* savpath = malloc(strlen(path)+sizeof("DualSOUP.ini"));
    strcpy(savpath, path);
    strcat(savpath, "DualSOUP.ini");
    SDL_IOStream* file = SDL_IOFromFile(savpath, "wt");
    free(savpath);
    SDL_LockMutex(corecfg->Mutex);

    for (size_t i = 0; i < (sizeof(MainCfg) / sizeof(MainCfg[0])); i++)
    {
        const union
        {
            CoreCfg cfg;
            u8 byte[sizeof(CoreCfg)/sizeof(u8)];
            char* str[sizeof(CoreCfg)/sizeof(char*)];
        } pun = {.cfg = *corecfg};

        const ConfigEntry entry = MainCfg[i];
        char* entrystr = entry.Name;
        char* outstr;
        size_t baselen = strlen(entrystr)+sizeof(char);
        size_t fulllen = baselen+sizeof(char[2]);

        switch(entry.Type)
        {
        case SEARCH_ENUMU8:
        {
            u8 val = pun.byte[entry.Offset/sizeof(pun.byte[0])];
            if (val >= entry.MaxNum)
            {
                printf("Config Error: value of %s == %u >= max value of: %u", entrystr, val, entry.MaxNum);
                continue;
            }
            char* valstr = entry.EnumNames[val];
            fulllen += strlen(valstr);
            outstr = malloc(fulllen);
            strcpy(outstr, entrystr);
            strcpy(&outstr[baselen], valstr);
            break;
        }

        case SEARCH_STRING:
        {
            char* valstr = pun.str[entry.Offset/sizeof(pun.str[0])];
            fulllen += strlen(valstr);
            outstr = malloc(fulllen);
            strcpy(outstr, entrystr);
            strcpy(&outstr[baselen], valstr);
            break;
        }

        default: printf("WRITE: UNHANDLED CONFIG ENUM TYPE!!!\n"); continue;
        }

        // add some filler
        // string should now look like: "entrystr=valstr\0\n"
        outstr[baselen-1] = '=';
        outstr[fulllen-1] = '\n'; // string is no longer 0 terminated

        SDL_WriteIO(file, outstr, fulllen);
        free (outstr);
    }
    corecfg->Dirty = false;
    SDL_UnlockMutex(corecfg->Mutex);
    if (!SDL_CloseIO(file)) printf("%s\n", SDL_GetError());
}

CoreCfg Config_Load(const char* path)
{
    char* savpath = malloc(strlen(path)+sizeof("DualSOUP.ini"));
    strcpy(savpath, path);
    strcat(savpath, "DualSOUP.ini");
    SDL_IOStream* file;
    size_t cfgsize;
    char* cfgdat;
    bool loaddefaults = true;
    bool dirty = false;

    union
    {
        CoreCfg cfg;
        u8 byte[sizeof(CoreCfg)/sizeof(u8)];
        char* str[sizeof(CoreCfg)/sizeof(char*)];
    } pun = {};

    pun.cfg.Mutex = SDL_CreateMutex();
    if (pun.cfg.Mutex == NULL)
    {
        printf("ERROR: setting mutex creation failure... %s\n", SDL_GetError());
        exit(EXIT_FAILURE);
    }

    if ((file = SDL_IOFromFile(savpath, "rt")) != NULL)
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
    free(savpath);

    bool initialized[sizeof(MainCfg) / sizeof(MainCfg[0])] = {};
    for (size_t nument = 0, pos = 0; nument < (sizeof(MainCfg) / sizeof(MainCfg[0])); nument++)
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
                for (; i < (sizeof(MainCfg) / sizeof(MainCfg[0])); i++)
                {
                    if (initialized[i]) continue;
                    size_t namelen = strlen(MainCfg[i].Name);
                    if ((pos+namelen) >= cfgsize) continue; // buffer overflow; must be invalid
                    if ((memcmp(&cfgdat[pos], MainCfg[i].Name, namelen) == 0) && (cfgdat[pos+namelen] == '='))
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
            for (; i < (sizeof(MainCfg) / sizeof(MainCfg[0])); i++)
            {
                if (!initialized[i]) break;
            }
        }
        found:

        printf("entries: %lu\n", i);
        const ConfigEntry entry = MainCfg[i];

        switch(entry.Type)
        {
        case SEARCH_ENUMU8:
        {
            bool success = false;
            u8 j = 0;
            if (!loaddefaults)
            {
                for (; j < entry.MaxNum; j++)
                {
                    size_t enumlen = strlen(entry.EnumNames[j]);
                    if ((pos+enumlen) >= cfgsize) continue; // buffer overflow; must be invalid
                    if (memcmp(&cfgdat[pos], entry.EnumNames[j], enumlen) == 0)
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
                j = entry.Default;
                dirty = true;
            }

            pun.byte[entry.Offset/sizeof(pun.byte[0])] = j;
            break;
        }

        case SEARCH_STRING:
        {
            size_t vallen;
            if (!loaddefaults && ((vallen = strnlen(&cfgdat[pos], cfgsize-pos-1)) != (cfgsize-pos-1)))
            {
                pun.str[entry.Offset/sizeof(pun.str[0])] = malloc(vallen);
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

        default: printf("LOAD: UNHANDLED CONFIG ENUM TYPE!!!\n"); break;
        }
        initialized[i] = true;
    }

    pun.cfg.Dirty = (dirty || loaddefaults);

    return pun.cfg;
}
