#ifndef __LINK_LIST_H_
#define __LINK_LIST_H_

#include <stdio.h>
#include <stdlib.h>

#define MAX_LINE 1024

typedef struct tagXLink {
	char 			dir[MAX_LINE];
	char 			linkname[MAX_LINE];
	char 			linktarget[MAX_LINE];
	unsigned int	uid;
	unsigned int	gid;
	unsigned int	mode;
	
	struct tagXLink* pNext;
} XLink;

#define LINK_LIST_FILE_NAME "aml_link_list"

void recordMountPoint(char* mountpoint);
char *get_linklist_file(const char *directory);
int load_linklist_from_file( const char* link_list );
const XLink* getLinkList();


#endif
