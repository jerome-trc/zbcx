#ifndef ZBCX_H_
#define ZBCX_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _zbcx_ListLink {
	struct _zbcx_ListLink* next;
	void* data;
} zbcx_ListLink;

typedef struct _zbcx_List {
	zbcx_ListLink* head;
	zbcx_ListLink* tail;
	int size;
} zbcx_List;

typedef struct _zbcx_ListIter {
	zbcx_ListLink* prev;
	zbcx_ListLink* link;
} zbcx_ListIter;

void zbcx_list_init(zbcx_List* list);
int zbcx_list_size(zbcx_List* list);
void* zbcx_list_head(zbcx_List* list);
void* zbcx_list_tail(zbcx_List* list);
void zbcx_list_append(zbcx_List*, void* data);
void zbcx_list_prepend(zbcx_List*, void* data);
void zbcx_list_iterate(zbcx_List* list, zbcx_ListIter* iter);
bool zbcx_list_end(zbcx_ListIter* iter);
void zbcx_list_next(zbcx_ListIter* iter);
void* zbcx_list_data(zbcx_ListIter* iter);
void zbcx_list_insert_after(zbcx_List* list, zbcx_ListIter* iter, void* data);
void zbcx_list_insert_before(zbcx_List* list, zbcx_ListIter* iter, void* data);
/// Updates the data at the specified node and returns the old data.
void* zbcx_list_replace(zbcx_List* list, zbcx_ListIter* iter, void* data);
void zbcx_list_merge(zbcx_List* receiver, zbcx_List* giver);
/// Removes the first node of the list and returns the data of the removed node.
void* zbcx_list_shift(zbcx_List* list);
void zbcx_list_deinit(zbcx_List* list);

typedef struct _zbcx_Task zbcx_Task;
typedef struct _zbcx_Options zbcx_Options;

#ifdef __cplusplus
}
#endif

#endif // ifndef ZBCX_H_
