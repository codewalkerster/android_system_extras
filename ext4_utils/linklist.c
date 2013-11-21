
#ifdef USE_MINGW
#include "linklist.h"
	
XLink* plink_list = NULL;
char chMountPoint[MAX_LINE];
char linklist_file[MAX_LINE];


void recordMountPoint(char* _mountpoint) {
	char* mountpoint = NULL;
	if (_mountpoint == NULL) {
		mountpoint = strdup("");
	} else {
		mountpoint = canonicalize_abs_slashes(_mountpoint);
	}
	
	sprintf(chMountPoint, "%s", mountpoint);
	free(mountpoint);
}

char *search_linklist_file(const char *_directory)
{
	int directory = canonicalize_rel_slashes(_directory);
	printf("get_linklist_file directory:%s", directory);
	int ret;

	int full_len = strlen(directory) + strlen(LINK_LIST_FILE_NAME);
    ret = sprintf(linklist_file, "%s%s", directory, LINK_LIST_FILE_NAME);
	if (ret < 0) {
		printf("get_linklist_file sprintf fail\n");
	}
	
	free(directory);
	return linklist_file;
}

int load_linklist_from_file( const char* link_list ) 
{
	FILE *fdLinklist = fopen(link_list, "r");
	char buffer[MAX_LINE];

	if (!fdLinklist) {
		printf("load_linklist_from_file, open link_list file fail, filepath:%s\n", link_list);
		return -1;
	}

	clean_linklist();
	
	int nIndex = 1;
	while(fgets(buffer, MAX_LINE, fdLinklist)){
		if( !strncmp( buffer, "#", 1 ) ) {
			printf("skip comment:%s\n", buffer);
			continue;
		}
		
		char dir[MAX_LINE];
		char link[MAX_LINE];
		char target[MAX_LINE];
		unsigned int uid;
		unsigned int gid;
		unsigned int mode;
		int nRet = sscanf( buffer, "%s %s %s %lu %lu %lu", target, dir, link, &uid, &gid, &mode );
		if( nRet != 6 ) {
			//printf("load_linklist_from_file sscanf fail, nRet:%d, buffer:%s\n", nRet, buffer);
			continue;
		}
		
		//printf("load_linklist_from_file dir:%s, link:%s, target:%s\n", dir, link, target);

		insert_link( dir, link, target, uid, gid, mode );
        //printf("nIndex:%d,%s", nIndex, buffer);
		nIndex++;
    }

	printf("load_linklist_from_file finish\n");

	fclose(fdLinklist);
	return 0;
}

void insert_link(char *dir, char *name, char* target, unsigned int uid, unsigned int gid, unsigned int mode) {
	XLink *plink = malloc(sizeof(XLink));
	sprintf( plink->dir, "%s%s", chMountPoint, dir );
	sprintf( plink->linkname, "%s", name );
	sprintf( plink->linktarget, "%s", target );
	plink->uid = uid;
	plink->gid = gid;
	plink->mode = mode;
	plink->pNext = plink_list;
	plink_list = plink;
}

void clean_linklist() {
	XLink* p = plink_list;
	
	while( p ) {
		XLink* pCurrent = p;
		p = pCurrent->pNext;
		free(pCurrent);
	}

	plink_list = NULL;
}

void dump_linklist() {
	XLink* p = plink_list;
	while( p ) {
		printf("dump_linklist dir:%s, linkname:%s, linktarget:%s, uid:%lu, gid:%lu, mode:%lu\n",\
			p->dir, p->linkname, p->linktarget, p->uid, p->gid, p->mode);
		p = p->pNext;
	}
}

const XLink* getLinkList() {
	return plink_list;
}

const char* getListFilePath() {
	return linklist_file;
}

#endif

