#include "game/message.h"
#include "ntg_log.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <map>
#include <string>

#include "game/gconfig.h"
#include "game/roll.h"
#include "platform_compat.h"
#include "plib/db/db.h"
#include "plib/gnw/debug.h"
#include "plib/gnw/memory.h"

namespace fallout {

#define BADWORD_LENGTH_MAX 80

static bool message_find(MessageList* msg, int num, int* out_index);
static bool message_add(MessageList* msg, MessageListItem* new_entry);
static bool message_parse_number(int* out_num, const char* str);
static int message_load_field(DB_FILE* file, char* str);

// 0x505B10
static char** bad_word = NULL;

// 0x505B14
static int bad_total = 0;

// 0x505B18
static int* bad_len = NULL;

// Temporary message list item text used during filtering badwords.
//
// 0x6305D0
static char bad_copy[MESSAGE_LIST_ITEM_FIELD_MAX_SIZE];

// 0x4764E0
int init_message()
{
    DB_FILE* stream = db_fopen("data\\badwords.txt", "rt");
    if (stream == NULL) {
        return -1;
    }

    char word[BADWORD_LENGTH_MAX];

    bad_total = 0;
    while (db_fgets(word, BADWORD_LENGTH_MAX - 1, stream)) {
        bad_total++;
    }

    bad_word = (char**)mem_malloc(sizeof(*bad_word) * bad_total);
    if (bad_word == NULL) {
        db_fclose(stream);
        return -1;
    }

    bad_len = (int*)mem_malloc(sizeof(*bad_len) * bad_total);
    if (bad_len == NULL) {
        mem_free(bad_word);
        db_fclose(stream);
        return -1;
    }

    db_fseek(stream, 0, SEEK_SET);

    int index = 0;
    for (; index < bad_total; index++) {
        if (!db_fgets(word, BADWORD_LENGTH_MAX - 1, stream)) {
            break;
        }

        int len = strlen(word);
        if (word[len - 1] == '\n') {
            len--;
            word[len] = '\0';
        }

        bad_word[index] = mem_strdup(word);
        if (bad_word[index] == NULL) {
            break;
        }

        compat_strupr(bad_word[index]);

        bad_len[index] = len;
    }

    db_fclose(stream);

    if (index != bad_total) {
        for (; index > 0; index--) {
            mem_free(bad_word[index - 1]);
        }

        mem_free(bad_word);
        mem_free(bad_len);

        return -1;
    }

    return 0;
}

// 0x476660
void exit_message()
{
    for (int index = 0; index < bad_total; index++) {
        mem_free(bad_word[index]);
    }

    if (bad_total != 0) {
        mem_free(bad_word);
        mem_free(bad_len);
    }

    bad_total = 0;
}

// nettoge.com: which file a list was read from, for the log.
static std::map<MessageList*, std::string>& ntg_msg_paths()
{
    static std::map<MessageList*, std::string> paths;
    return paths;
}

const char* message_ntg_path(MessageList* messageList)
{
    auto it = ntg_msg_paths().find(messageList);
    return it != ntg_msg_paths().end() ? it->second.c_str() : "(unknown file)";
}

// 0x4766BC
bool message_init(MessageList* messageList)
{
    if (messageList != NULL) {
        messageList->entries_num = 0;
        messageList->entries = NULL;
    }
    return true;
}

// 0x4766D4
bool message_exit(MessageList* messageList)
{
    int i;
    MessageListItem* entry;

    if (messageList == NULL) {
        return false;
    }

    for (i = 0; i < messageList->entries_num; i++) {
        entry = &(messageList->entries[i]);

        if (entry->audio != NULL) {
            mem_free(entry->audio);
        }

        if (entry->text != NULL) {
            mem_free(entry->text);
        }
    }

    messageList->entries_num = 0;
    ntg_msg_paths().erase(messageList);

    if (messageList->entries != NULL) {
        mem_free(messageList->entries);
        messageList->entries = NULL;
    }

    return true;
}

// nettoge.com: hash of an English line, the same as tools/mkmsg.py src_hash():
// FNV-1a over the bytes, skipping white space and bytes >= 0x80 (the English
// dump the translation was made from is UTF-8, the game files are cp1252).
static unsigned int ntg_hash_text(const char* text)
{
    unsigned int h = 2166136261u;
    for (const unsigned char* p = (const unsigned char*)text; *p != '\0'; p++) {
        if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p >= 0x80) {
            continue;
        }
        h ^= *p;
        h *= 16777619u;
    }
    return h;
}

// nettoge.com: per-number English fallback for mods. After a translated file is
// read, the English file of the same name is read too (a mod's loose file in
// DATA wins over master.dat). Numbers the translation does not have (lines a
// mod added) come in English, and so do numbers whose English is not the one
// the translation was made from (lines a mod changed or renumbered). The
// hashes of the source lines are in text\<language>\<path>.src, written by
// tools/mkmsg.py.
static void ntg_merge_english(MessageList* messageList, const char* path, const char* language)
{
    char en_path[COMPAT_MAX_PATH];
    snprintf(en_path, sizeof(en_path), "text\\%s\\%s", ENGLISH, path);
    DB_FILE* en = db_fopen(en_path, "rt");
    if (en == NULL) {
        return;
    }

    std::map<int, std::string> src; // " h1 h2 ": the English lines the translation fits (the base game, and a mod's lines that only fix spelling)
    char src_path[COMPAT_MAX_PATH];
    snprintf(src_path, sizeof(src_path), "text\\%s\\%s.src", language, path);
    DB_FILE* sf = db_fopen(src_path, "rt");
    if (sf != NULL) {
        char line[64];
        while (db_fgets(line, sizeof(line), sf) != NULL) {
            char* rest;
            long n = strtol(line, &rest, 10);
            if (rest != line) {
                std::string hs = " ";
                for (char* t = rest; *t != '\0'; t++) {
                    hs += (*t == '\r' || *t == '\n' || *t == '\t') ? ' ' : *t;
                }
                src[(int)n] = hs + " ";
            }
        }
        db_fclose(sf);
    }

    // the game keeps the last of a number written twice
    std::map<int, std::pair<std::string, std::string>> lines;
    char* num = (char*)mem_malloc(MESSAGE_LIST_ITEM_FIELD_MAX_SIZE * 3);
    if (num == NULL) {
        db_fclose(en);
        return;
    }
    char* audio = num + MESSAGE_LIST_ITEM_FIELD_MAX_SIZE;
    char* text = audio + MESSAGE_LIST_ITEM_FIELD_MAX_SIZE;
    while (message_load_field(en, num) == 0) {
        if (message_load_field(en, audio) != 0 || message_load_field(en, text) != 0) {
            break;
        }
        int n;
        if (!message_parse_number(&n, num)) {
            break;
        }
        lines[n] = std::make_pair(std::string(audio), std::string(text));
    }
    db_fclose(en);

    int added = 0;
    int changed = 0;
    for (auto& it : lines) {
        int index;
        bool have = message_find(messageList, it.first, &index);
        if (have) {
            auto h = src.find(it.first);
            char hex[16];
            snprintf(hex, sizeof(hex), " %08x ", ntg_hash_text(it.second.second.c_str()));
            if (h == src.end() || h->second.find(hex) != std::string::npos) {
                continue;
            }
        }
        strncpy(audio, it.second.first.c_str(), MESSAGE_LIST_ITEM_FIELD_MAX_SIZE - 1);
        audio[MESSAGE_LIST_ITEM_FIELD_MAX_SIZE - 1] = '\0';
        strncpy(text, it.second.second.c_str(), MESSAGE_LIST_ITEM_FIELD_MAX_SIZE - 1);
        text[MESSAGE_LIST_ITEM_FIELD_MAX_SIZE - 1] = '\0';
        MessageListItem entry;
        entry.num = it.first;
        entry.audio = audio;
        entry.text = text;
        if (!message_add(messageList, &entry)) {
            break;
        }
        if (have) {
            changed++;
            ntg_log(NTG_LOG_TRACE, "msg", "%s %d: changed by a mod, shown in English", path, it.first);
        } else {
            added++;
            ntg_log(NTG_LOG_TRACE, "msg", "%s %d: added by a mod, shown in English", path, it.first);
        }
    }
    mem_free(num);

    if (added != 0 || changed != 0) {
        ntg_log(NTG_LOG_INFO, "msg", "%s: %d lines added and %d lines changed by a mod, shown in English",
            path, added, changed);
    }
}

// 0x476814
bool message_load(MessageList* messageList, const char* path)
{
    char* language;
    char localized_path[COMPAT_MAX_PATH];
    DB_FILE* file_ptr;
    char num[MESSAGE_LIST_ITEM_FIELD_MAX_SIZE];
    char audio[MESSAGE_LIST_ITEM_FIELD_MAX_SIZE];
    char text[MESSAGE_LIST_ITEM_FIELD_MAX_SIZE];
    int rc;
    bool success;
    MessageListItem entry;

    success = false;

    if (messageList == NULL) {
        return false;
    }

    if (path == NULL) {
        return false;
    }

    if (!config_get_string(&game_config, GAME_CONFIG_SYSTEM_KEY, GAME_CONFIG_LANGUAGE_KEY, &language)) {
        return false;
    }

    snprintf(localized_path, sizeof(localized_path), "%s\\%s\\%s", "text", language, path);

    file_ptr = db_fopen(localized_path, "rt");
    bool translated = file_ptr != NULL;

    // nettoge.com: fall back to the English file while a translation is missing.
    if (file_ptr == NULL && compat_stricmp(language, ENGLISH) != 0) {
        snprintf(localized_path, sizeof(localized_path), "%s%c%s%c%s", "text", 92, ENGLISH, 92, path);
        file_ptr = db_fopen(localized_path, "rt");
    }

    if (file_ptr == NULL) {
        ntg_log(NTG_LOG_INFO, "msg", "not found: %s (language %s, and english)", path, language);
        return false;
    }

    ntg_log_ctx_msg(localized_path);
    ntg_msg_paths()[messageList] = localized_path;

    entry.num = 0;
    entry.audio = audio;
    entry.text = text;

    while (1) {
        rc = message_load_field(file_ptr, num);
        if (rc != 0) {
            break;
        }

        if (message_load_field(file_ptr, audio) != 0) {
            debug_printf("\nError loading audio field.\n", localized_path);
            goto err;
        }

        if (message_load_field(file_ptr, text) != 0) {
            debug_printf("\nError loading text field.\n", localized_path);
            goto err;
        }

        if (!message_parse_number(&(entry.num), num)) {
            debug_printf("\nError parsing number.\n", localized_path);
            goto err;
        }

        if (!message_add(messageList, &entry)) {
            debug_printf("\nError adding message.\n", localized_path);
            goto err;
        }
    }

    if (rc == 1) {
        success = true;
    }

err:

    if (!success) {
        debug_printf("Error loading message file %s at offset %x.", localized_path, db_ftell(file_ptr));
    }

    // nettoge.com
    if (success) {
        ntg_log(NTG_LOG_INFO, "msg", "read %s: %d lines", localized_path, messageList->entries_num);
    } else {
        ntg_log(NTG_LOG_ERR, "msg", "BROKEN FILE %s: stopped after %d lines (last number %d), offset 0x%x. Check the braces around there.",
            localized_path, messageList->entries_num, entry.num, (unsigned)db_ftell(file_ptr));
    }

    db_fclose(file_ptr);

    // nettoge.com: lines a mod added or changed come in English
    if (success && compat_stricmp(language, ENGLISH) != 0 && translated) {
        ntg_merge_english(messageList, path, language);
    }

    return success;
}

// 0x476998
bool message_search(MessageList* msg, MessageListItem* entry)
{
    int index;
    MessageListItem* ptr;

    if (msg == NULL) {
        return false;
    }

    if (entry == NULL) {
        return false;
    }

    if (msg->entries_num == 0) {
        return false;
    }

    if (!message_find(msg, entry->num, &index)) {
        return false;
    }

    ptr = &(msg->entries[index]);
    entry->audio = ptr->audio;
    entry->text = ptr->text;

    return true;
}

// Builds language-aware path in "text" subfolder.
//
// 0x476A20
bool message_make_path(char* dest, size_t size, const char* path)
{
    char* language;

    if (dest == NULL) {
        return false;
    }

    if (path == NULL) {
        return false;
    }

    if (!config_get_string(&game_config, GAME_CONFIG_SYSTEM_KEY, GAME_CONFIG_LANGUAGE_KEY, &language)) {
        return false;
    }

    snprintf(dest, size, "%s\\%s\\%s", "text", language, path);

    return true;
}

// 0x476A78
bool message_find(MessageList* msg, int num, int* out_index)
{
    int r, l, mid;
    int cmp;

    if (msg->entries_num == 0) {
        *out_index = 0;
        return false;
    }

    r = msg->entries_num - 1;
    l = 0;

    do {
        mid = (l + r) / 2;
        cmp = num - msg->entries[mid].num;
        if (cmp == 0) {
            *out_index = mid;
            return true;
        }

        if (cmp > 0) {
            l = l + 1;
        } else {
            r = r - 1;
        }
    } while (r >= l);

    if (cmp < 0) {
        *out_index = mid;
    } else {
        *out_index = mid + 1;
    }

    return false;
}

// 0x476AD0
bool message_add(MessageList* msg, MessageListItem* new_entry)
{
    int index;
    MessageListItem* entries;
    MessageListItem* existing_entry;

    if (message_find(msg, new_entry->num, &index)) {
        existing_entry = &(msg->entries[index]);

        if (existing_entry->audio != NULL) {
            mem_free(existing_entry->audio);
        }

        if (existing_entry->text != NULL) {
            mem_free(existing_entry->text);
        }
    } else {
        if (msg->entries != NULL) {
            entries = (MessageListItem*)mem_realloc(msg->entries, sizeof(MessageListItem) * (msg->entries_num + 1));
            if (entries == NULL) {
                return false;
            }

            msg->entries = entries;

            if (index != msg->entries_num) {
                // Move all items below insertion point
                memmove(&(msg->entries[index + 1]), &(msg->entries[index]), sizeof(MessageListItem) * (msg->entries_num - index));
            }
        } else {
            msg->entries = (MessageListItem*)mem_malloc(sizeof(MessageListItem));
            if (msg->entries == NULL) {
                return false;
            }
            msg->entries_num = 0;
            index = 0;
        }

        existing_entry = &(msg->entries[index]);
        existing_entry->audio = 0;
        existing_entry->text = 0;
        msg->entries_num++;
    }

    existing_entry->audio = mem_strdup(new_entry->audio);
    if (existing_entry->audio == NULL) {
        return false;
    }

    existing_entry->text = mem_strdup(new_entry->text);
    if (existing_entry->text == NULL) {
        return false;
    }

    existing_entry->num = new_entry->num;

    return true;
}

// 0x476D80
bool message_parse_number(int* out_num, const char* str)
{
    const char* ch;
    bool success;

    ch = str;
    if (*ch == '\0') {
        return false;
    }

    success = true;
    if (*ch == '+' || *ch == '-') {
        ch++;
    }

    while (*ch != '\0') {
        if (!isdigit(*ch)) {
            success = false;
            break;
        }
        ch++;
    }

    *out_num = atoi(str);
    return success;
}

// Read next message file field, the `str` should be at least
// `MESSAGE_LIST_ITEM_FIELD_MAX_SIZE` bytes long.
//
// Returns:
// 0 - ok
// 1 - eof
// 2 - mismatched delimeters
// 3 - unterminated field
// 4 - limit exceeded (> `MESSAGE_LIST_ITEM_FIELD_MAX_SIZE`)
//
// 0x476DD4
int message_load_field(DB_FILE* file, char* str)
{
    int ch;
    int len;

    len = 0;

    while (1) {
        ch = db_fgetc(file);
        if (ch == -1) {
            return 1;
        }

        if (ch == '}') {
            debug_printf("\nError reading message file - mismatched delimiters.\n");
            return 2;
        }

        if (ch == '{') {
            break;
        }
    }

    while (1) {
        ch = db_fgetc(file);

        if (ch == -1) {
            debug_printf("\nError reading message file - EOF reached.\n");
            return 3;
        }

        if (ch == '}') {
            *(str + len) = '\0';
            return 0;
        }

        if (ch != '\n') {
            *(str + len) = ch;
            len++;

            if (len >= MESSAGE_LIST_ITEM_FIELD_MAX_SIZE) {
                debug_printf("\nError reading message file - text exceeds limit.\n");
                return 4;
            }
        }
    }

    return 0;
}

// 0x476E6C
char* getmsg(MessageList* msg, MessageListItem* entry, int num)
{
    // 0x505B1C
    static char message_error_str[] = "Error";

    entry->num = num;

    if (!message_search(msg, entry)) {
        entry->text = message_error_str;
        ntg_log(NTG_LOG_ERR, "msg", "getmsg: number %d is not in %s -> \"Error\" is shown", num, message_ntg_path(msg));
        debug_printf("\n ** String not found @ getmsg(), MESSAGE.C **\n");
    }

    return entry->text;
}

// 0x476E98
bool message_filter(MessageList* messageList)
{
    // TODO: Check.
    // 0x50B960
    static const char* replacements = "!@#$%&*@#*!&$%#&%#*%!$&%@*$@&";

    if (messageList == NULL) {
        return false;
    }

    if (messageList->entries_num == 0) {
        return true;
    }

    if (bad_total == 0) {
        return true;
    }

    int languageFilter = 0;
    config_get_value(&game_config, GAME_CONFIG_PREFERENCES_KEY, GAME_CONFIG_LANGUAGE_FILTER_KEY, &languageFilter);
    if (languageFilter != 1) {
        return true;
    }

    int replacementsCount = strlen(replacements);
    int replacementsIndex = roll_random(1, replacementsCount) - 1;

    for (int index = 0; index < messageList->entries_num; index++) {
        MessageListItem* item = &(messageList->entries[index]);
        strcpy(bad_copy, item->text);
        compat_strupr(bad_copy);

        for (int badwordIndex = 0; badwordIndex < bad_total; badwordIndex++) {
            // I don't quite understand the loop below. It has no stop
            // condition besides no matching substring. It also overwrites
            // already masked words on every iteration.
            for (char* p = bad_copy;; p++) {
                const char* substr = strstr(p, bad_word[badwordIndex]);
                if (substr == NULL) {
                    break;
                }

                if (substr == bad_copy || (!isalpha(substr[-1]) && !isalpha(substr[bad_len[badwordIndex]]))) {
                    char* ptr = item->text + (substr - bad_copy);

                    for (int j = 0; j < bad_len[badwordIndex]; j++) {
                        *ptr++ = replacements[replacementsIndex++];
                        if (replacementsIndex == replacementsCount) {
                            replacementsIndex = 0;
                        }
                    }
                }
            }
        }
    }

    return true;
}

// nettoge.com: <male text^female text> in dialog messages keeps one side by the
// player's gender. Ported from fallout2-ce (sfall DialogGenderWords); always on here.
void message_filter_gender_words(MessageList* messageList, bool female)
{
    if (messageList == NULL) {
        return;
    }

    for (int index = 0; index < messageList->entries_num; index++) {
        MessageListItem* item = &(messageList->entries[index]);
        char* text = item->text;
        char* sep;

        while ((sep = strchr(text, '^')) != NULL) {
            *sep = '\0';
            char* start = strrchr(text, '<');
            char* end = strchr(sep + 1, '>');
            *sep = '^';

            if (start != NULL && end != NULL) {
                char* src;
                size_t length;
                if (female) {
                    src = sep + 1;
                    length = end - sep - 1;
                } else {
                    src = start + 1;
                    length = sep - start - 1;
                }

                memmove(start, src, length);
                memmove(start + length, end + 1, strlen(end + 1) + 1);
            } else {
                text = sep + 1;
            }
        }
    }
}

} // namespace fallout
