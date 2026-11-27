#ifndef FALLOUT_GAME_MESSAGE_H_
#define FALLOUT_GAME_MESSAGE_H_

#include <stddef.h>

namespace fallout {

// TODO: Probably should be private.
// nettoge.com: 1024 -> 4096 (UTF-8 Japanese uses 3 bytes per character).
#define MESSAGE_LIST_ITEM_FIELD_MAX_SIZE 4096

typedef struct MessageListItem {
    int num;
    char* audio;
    char* text;
} MessageListItem;

typedef struct MessageList {
    int entries_num;
    MessageListItem* entries;
} MessageList;

int init_message();
void exit_message();
bool message_init(MessageList* msg);
bool message_exit(MessageList* msg);
bool message_load(MessageList* msg, const char* path);
// nettoge.com: the file a list was read from (for the log)
const char* message_ntg_path(MessageList* msg);
bool message_search(MessageList* msg, MessageListItem* entry);
bool message_make_path(char* dest, size_t size, const char* path);
char* getmsg(MessageList* msg, MessageListItem* entry, int num);
bool message_filter(MessageList* messageList);
// nettoge.com: <male^female> words in dialog messages (see message.cc).
void message_filter_gender_words(MessageList* messageList, bool female);

} // namespace fallout

#endif /* FALLOUT_GAME_MESSAGE_H_ */
