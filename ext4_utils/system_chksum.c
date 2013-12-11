#include "system_chksum.h"

char root_dir[256];

//rm the root_dir
char* get_sub_dir(const char *whole_dir) {
	int len = strlen(whole_dir) - strlen(root_dir) + 1;
	char* sub_dir = malloc(len);
	*(sub_dir + len - 1) = 0;
	strncpy( sub_dir, whole_dir + strlen(root_dir), len - 1 );
	return sub_dir;
}

#ifdef USE_MINGW
char* get_md5_from_chksum_list(char* chdsum_list_file, char *targetpath) {
	char* strMd5 = NULL;
	FILE *fdLinklist = fopen(chdsum_list_file, "r");
	char buffer[MAX_LINE];
	
	//printf("\nget_md5_from_chksum_list chdsum_list_file:%s, targetpath:%s\n", chdsum_list_file, targetpath);
	
	if (!fdLinklist) {
		printf("get_md5_from_chksum_list, open chdsum_list_file file fail, filepath:%s\n", chdsum_list_file);
		return strMd5;
	}
	
	while(fgets(buffer, MAX_LINE, fdLinklist)) {
		//printf("get_md5_from_chksum_list flag1\n");
		char md5[MAX_LINE];
		char target[MAX_LINE];
		int nRet = sscanf( buffer, "%s  %s", md5, target );
		if( nRet != 2 ) {
			//printf("get_md5_from_chksum_list flag2\n");
			continue;
		}
		//printf("get_md5_from_chksum_list flag3, targetpath:%s, target:%s\n", targetpath, target);
		if( !strcmp( targetpath, target ) ) {
			strMd5 = malloc( strlen(md5) + 1 );
			sprintf( strMd5, "%s", md5 );
			//printf("get_md5_from_chksum_list flag4, targetpath:%s, target:%s, md5:%s\n", targetpath, target, md5);
			break;
		}
	}
	
	fclose(fdLinklist);
	return strMd5;
}
#endif

void create_checksum_list(const char *_directory, const char* mountpoint) 
{
	int entries = 0;
	struct dentry *dentries;
	struct dirent **namelist = NULL;
	char *directory = NULL;
	int i;

	directory = canonicalize_rel_slashes(_directory);

	//printf("create_checksum_list directory:%s\n", directory);

#ifdef USE_MINGW
	entries = ext4_scandir(directory, &namelist, filter_dot, (void*)alphasort);
#else
	entries = scandir(directory, &namelist, filter_dot, (void*)alphasort);
#endif
	for (i = 0; i < entries; i++) {
		//printf( "i: %d, d_name:%s\n", i, namelist[i]->d_name );
		
		char *subdir_full_path = NULL;
                #ifdef USE_MINGW
                        int length = strlen(directory) + strlen(namelist[i]->d_name);
                        subdir_full_path = malloc(length+1);
                        sprintf(subdir_full_path, "%s%s", directory, namelist[i]->d_name);
                #else
                        asprintf(&subdir_full_path, "%s%s", directory, namelist[i]->d_name);
                #endif
		
		struct stat f_stat;
		if( get_status( subdir_full_path, &f_stat) < 0 ) {
			continue;
		}

		//printf( "i: %d, d_name:%s, f_stat.st_mode:%d\n", i, namelist[i]->d_name, f_stat.st_mode );
		if (S_ISDIR(f_stat.st_mode))
		{
			create_checksum_list(subdir_full_path, mountpoint);
		} 
		else if( S_ISREG(f_stat.st_mode) || S_ISLNK(f_stat.st_mode) ) 
		{
			char *file_mount_path = NULL;
			char* subdir = get_sub_dir(subdir_full_path);
            		#ifdef USE_MINGW
			int length = strlen(mountpoint) + strlen(subdir);
			file_mount_path = malloc(length+2);
			sprintf(file_mount_path, "\\%s%s", mountpoint, subdir);
			#else
			asprintf(&file_mount_path, "/%s%s", mountpoint, subdir);
			#endif
			if(subdir) {
				free(subdir);
			}

			//replace '\\' -> '/'
			char* ch = file_mount_path;
			while(*ch) {
				if( *ch == '\\' ) {
					*ch = '/';
				}
				ch++;
			}

			//printf( "DT_REG or DT_LNK subdir_full_path:%s, file_mount_path:%s, f_stat.st_mode:%d, mountpoint:%s\n", subdir_full_path, file_mount_path, f_stat.st_mode, mountpoint );
			
			unsigned char md5[MD5_DIGEST_LENGTH];
			if( get_md5(subdir_full_path, md5) == 0 ) {
				char md5_str[2*MD5_DIGEST_LENGTH+1] = {0};
				hextoa( md5_str, md5, MD5_DIGEST_LENGTH );
				
				char *str_append = NULL;
				#ifdef USE_MINGW
					int length = strlen(md5_str) + strlen(file_mount_path);
					str_append = malloc(length+4);
					sprintf( str_append, "%s  %s\n", md5_str, file_mount_path);
                #else	
					asprintf( &str_append, "%s  %s\n", md5_str, file_mount_path);
				#endif
				//printf( "get_md5 %s", str_append);
				append_to_chksum_file(str_append, strlen(str_append));
				if(str_append) {
					free(str_append);
				}
			} else {
				printf( "get_md5 error, subdir_full_path:%s", subdir_full_path );
			}
			free(file_mount_path);
		} else {
			printf( "DT_OTHER!!! i: %d, d_name:%s, f_stat.st_mode:%d\n", i, namelist[i]->d_name, f_stat.st_mode );
		}

		if(subdir_full_path) {
                        free(subdir_full_path);
                }
	}

	if(directory) {
		free(directory);
	}
}

#ifdef USE_MINGW
//create according aml_link_list
void create_link_chksum() {
	XLink* plink = getLinkList();
	while( plink ) {
		//printf("create chksum according aml_link_list, plink->dir:%s, plink->linkname:%s, plink->linktarget:%s\n", plink->dir, plink->linkname, plink->linktarget);
		int len;
		char *targetpath = NULL;
		if( strchr( plink->linktarget, '/' ) != NULL ) {//use full path
			targetpath = malloc(strlen(plink->linktarget)+1);
			sprintf( targetpath, "%s", plink->linktarget );
		} else {//Combinat to full path
			len = strlen(plink->dir) + strlen(plink->linktarget) + 1;
			targetpath = malloc(len);
			sprintf( targetpath, "%s%s", plink->dir, plink->linktarget );
		}
		char *chksum_list_file = NULL;
		len = strlen(root_dir) + strlen("chksum_list") + 1;
		chksum_list_file = malloc(len);
		sprintf( chksum_list_file, "%s%s", root_dir, "chksum_list" );
		char* md5_str = get_md5_from_chksum_list( chksum_list_file, targetpath );
		if( !md5_str ) {
			printf("get_md5_from_chksum_list targetpath:%s md5_str is null\n", targetpath);
			continue;
		}
		
		char* file_mount_path = NULL;
		len = strlen(plink->dir) + strlen(plink->linkname) + 1;
		file_mount_path = malloc(len);
		sprintf( file_mount_path, "%s%s", plink->dir, plink->linkname );
		
		//printf( "create chksum according aml_link_list, md5_str:%s, file_mount_path:%s", md5_str, file_mount_path );
		
		char *str_append = NULL;
		len = strlen(md5_str) + strlen(file_mount_path);
		str_append = malloc(len+4);
		sprintf( str_append, "%s  %s\n", md5_str, file_mount_path);
		append_to_chksum_file(str_append, strlen(str_append));
		
		plink = plink->pNext;
		
		if(targetpath) {
			free(targetpath);
			targetpath = NULL;
		}
		
		if(md5_str) {
			free(md5_str);
			md5_str = NULL;
		}
		
		if(file_mount_path) {
			free(file_mount_path);
			file_mount_path = NULL;
		}
		
		if(str_append) {
			free(str_append);
			str_append = NULL;
		}
		
		if(chksum_list_file) {
			free(chksum_list_file);
			chksum_list_file = NULL;
		}
	}
}
#endif

char chksum_file_path[256];
void set_checksum_list_path(char *folder) {
	sprintf(chksum_file_path, "%s/%s", folder, "chksum_list");
	//printf("set_checksum_list_path chksum_file_path:%s\n", chksum_file_path);
}

void set_root_dir(char* dir) {
	sprintf(root_dir, "%s", dir);
}

int get_status( const char* path, struct stat* pStat ) {
	int ret;
	#ifdef USE_MINGW
                ret = stat(path, pStat);
	#else
                ret = lstat(path, pStat);
	#endif

	if (ret < 0) {
		printf("get_status fail, path:%s\n", path);
	}
		
	return ret;
}

void rm_chksum_file() {
	unlink(chksum_file_path);	
}

void append_to_chksum_file(char *str, int len) {
	append_to_file( chksum_file_path, str, len);	
}

void append_to_file(char* file, char* str, int len) {
	FILE* pAppendFile = fopen(file,"at");
        fwrite(str, 1, len, pAppendFile);
        fclose(pAppendFile);
}

void hextoa(char *szBuf, unsigned char nData[], int len)
{
	int i;
	for( i = 0; i < len; i++,szBuf+=2 ) {
		sprintf(szBuf,"%02x",nData[i]);
	}
}

int get_md5(const char *path, unsigned char* md5)
{
    unsigned int i;
    int fd;
    MD5_CTX md5_ctx;

    fd = open(path, O_RDONLY | O_BINARY);
    if (fd < 0) {
        fprintf(stderr,"could not open %s, %s\n", path, strerror(errno));
        return -1;
    }

    /* Note that bionic's MD5_* functions return void. */
    MD5_Init(&md5_ctx);

    while (1) {
        char buf[4096];
        ssize_t rlen;
        rlen = read(fd, buf, sizeof(buf));
        if (rlen == 0)
            break;
        else if (rlen < 0) {
            (void)close(fd);
            fprintf(stderr,"could not read %s, %s\n", path, strerror(errno));
            return -1;
        }
        MD5_Update(&md5_ctx, buf, rlen);
    }
    if (close(fd)) {
        fprintf(stderr,"could not close %s, %s\n", path, strerror(errno));
        return -1;
    }

    MD5_Final(md5, &md5_ctx);
/*
    for (i = 0; i < (int)sizeof(md5); i++)
        printf("%02x", md5[i]);
    printf("  %s\n", path);
*/
    return 0;
}
