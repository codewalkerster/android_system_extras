/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "make_ext4fs.h"
#include "ext4_utils.h"
#include "allocate.h"
#include "contents.h"
#include "uuid.h"
#include "wipe.h"

#include <sparse/sparse.h>

#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

/* TODO: Not implemented:
   Allocating blocks in the same block group as the file inode
   Hash or binary tree directories
   Special files: sockets, devices, fifos
 */

int filter_dot(const struct dirent *d)
{
	return (strcmp(d->d_name, "..") && strcmp(d->d_name, "."));
}

static u32 build_default_directory_structure()
{
	u32 inode;
	u32 root_inode;
	struct dentry dentries = {
			.filename = "lost+found",
			.file_type = EXT4_FT_DIR,
			.mode = S_IRWXU,
			.uid = 0,
			.gid = 0,
			.mtime = 0,
	};
	root_inode = make_directory(0, 1, &dentries, 1);
	inode = make_directory(root_inode, 0, NULL, 0);
	*dentries.inode = inode;
	inode_set_permissions(inode, dentries.mode,
		dentries.uid, dentries.gid, dentries.mtime);

	return root_inode;
}


#ifdef USE_MINGW
int alphasort(const struct dirent** a, const struct dirent** b) {
  return strcoll((*a)->d_name, (*b)->d_name);
}
#endif

/* Read a local directory and create the same tree in the generated filesystem.
   Calls itself recursively with each directory in the given directory.
   full_path is an absolute or relative path, with a trailing slash, to the
   directory on disk that should be copied, or NULL if this is a directory
   that does not exist on disk (e.g. lost+found).
   dir_path is an absolute path, with trailing slash, to the same directory
   if the image were mounted at the specified mount point */
static u32 build_directory_structure(const char *full_path, const char *dir_path,
		u32 dir_inode, fs_config_func_t fs_config_func,
		struct selabel_handle *sehnd, int verbose)
{
	int entries = 0;
	struct dentry *dentries;
	struct dirent **namelist = NULL;
	struct stat f_stat;
	int ret;
	int i;
	u32 inode;
	u32 entry_inode;
	u32 dirs = 0;
	bool needs_lost_and_found = false;

    //printf("full_path:%s, dir_path:%s\n", full_path, dir_path);
    
	if (full_path) {
#ifdef USE_MINGW
		entries = ext4_scandir(full_path, &namelist, filter_dot, (void*)alphasort);
#else
		entries = scandir(full_path, &namelist, filter_dot, (void*)alphasort);
#endif
		if (entries < 0) {
			error_errno("scandir");
			return EXT4_ALLOCATE_FAILED;
		}
	}

	if (dir_inode == 0) {
		/* root directory, check if lost+found already exists */
		for (i = 0; i < entries; i++)
			if (strcmp(namelist[i]->d_name, "lost+found") == 0)
				break;
		if (i == entries)
			needs_lost_and_found = true;
	}

	dentries = calloc(entries, sizeof(struct dentry));
	if (dentries == NULL)
		critical_error_errno("malloc");

	for (i = 0; i < entries; i++) {
		dentries[i].filename = strdup(namelist[i]->d_name);
		if (dentries[i].filename == NULL)
			critical_error_errno("strdup");

        //printf("dir_path:%s, full_path:%s, d_name:%s\n", dir_path, full_path, namelist[i]->d_name);

        
    #ifndef USE_MINGW
		asprintf(&dentries[i].path, "%s%s", dir_path, namelist[i]->d_name);
		asprintf(&dentries[i].full_path, "%s%s", full_path, namelist[i]->d_name);
    #else
        int dir_len = strlen(dir_path);
        int full_len = strlen(full_path);
        int name_len = strlen(namelist[i]->d_name);

        dentries[i].path = (char *)malloc(dir_len + name_len + 2);
        dentries[i].full_path = (char *)malloc(full_len + name_len + 2);
        sprintf(dentries[i].path, "%s%s", dir_path, namelist[i]->d_name);
		sprintf(dentries[i].full_path, "%s%s", full_path, namelist[i]->d_name);
    #endif

		free(namelist[i]);

#ifdef USE_MINGW
        	ret = stat(dentries[i].full_path, &f_stat);
#else
		ret = lstat(dentries[i].full_path, &f_stat);
#endif
		if (ret < 0) {
			error_errno("lstat path:%s", dentries[i].full_path);
			i--;
			entries--;
			continue;
		}

		dentries[i].size = f_stat.st_size;
		dentries[i].mode = f_stat.st_mode & (S_ISUID|S_ISGID|S_ISVTX|S_IRWXU|S_IRWXG|S_IRWXO);
		dentries[i].mtime = f_stat.st_mtime;
		uint64_t capabilities;
		if (fs_config_func != NULL) {
#ifdef ANDROID
			unsigned int mode = 0;
			unsigned int uid = 0;
			unsigned int gid = 0;
			int dir = S_ISDIR(f_stat.st_mode);
			fs_config_func(dentries[i].path, dir, &uid, &gid, &mode, &capabilities);
			dentries[i].mode = mode;
			dentries[i].uid = uid;
			dentries[i].gid = gid;
			dentries[i].capabilities = capabilities;
#else
			error("can't set android permissions - built without android support");
#endif
		}
#ifndef USE_MINGW
		if (sehnd) {
			if (selabel_lookup(sehnd, &dentries[i].secon, dentries[i].path, f_stat.st_mode) < 0) {
				error("cannot lookup security context for %s", dentries[i].path);
			}

			if (dentries[i].secon && verbose)
				printf("Labeling %s as %s\n", dentries[i].path, dentries[i].secon);
		}
#endif

		if (S_ISREG(f_stat.st_mode)) {
			dentries[i].file_type = EXT4_FT_REG_FILE;
#ifdef USE_MINGW
			if( !strcmp(LINK_LIST_FILE_NAME, dentries[i].filename) ) {
				printf("is linklist file,skip, dir_path:%s, dentries[i].filename:%s\n", dir_path, dentries[i].filename );
				continue;
			}			
#endif
		} else if (S_ISDIR(f_stat.st_mode)) {
			dentries[i].file_type = EXT4_FT_DIR;
			dirs++;
		} else if (S_ISCHR(f_stat.st_mode)) {
			dentries[i].file_type = EXT4_FT_CHRDEV;
		} else if (S_ISBLK(f_stat.st_mode)) {
			dentries[i].file_type = EXT4_FT_BLKDEV;
		} else if (S_ISFIFO(f_stat.st_mode)) {
			dentries[i].file_type = EXT4_FT_FIFO;
		} else if (S_ISSOCK(f_stat.st_mode)) {
			dentries[i].file_type = EXT4_FT_SOCK;
		} else if (S_ISLNK(f_stat.st_mode)) {
		#ifndef USE_MINGW
			dentries[i].file_type = EXT4_FT_SYMLINK;
			dentries[i].link = calloc(info.block_size, 1);
			readlink(dentries[i].full_path, dentries[i].link, info.block_size - 1);
		#endif
		} else {
			error("unknown file type on %s", dentries[i].path);
			i--;
			entries--;
		}
        //printf("dentries[%d].path:%s\n", i, dentries[i].path);
	}
	free(namelist);

	if (needs_lost_and_found) {
		/* insert a lost+found directory at the beginning of the dentries */
		struct dentry *tmp = calloc(entries + 1, sizeof(struct dentry));
		memset(tmp, 0, sizeof(struct dentry));
		memcpy(tmp + 1, dentries, entries * sizeof(struct dentry));
		dentries = tmp;

		dentries[0].filename = strdup("lost+found");
    #ifndef USE_MINGW
		asprintf(&dentries[0].path, "%slost+found", dir_path);
    #else
        int dir_len = strlen(dir_path) + strlen("slost+found");

        dentries[0].path = (char *)malloc(dir_len + 2);
        sprintf(dentries[0].path, "%slost+found", dir_path);
    #endif
		dentries[0].full_path = NULL;
		dentries[0].size = 0;
		dentries[0].mode = S_IRWXU;
		dentries[0].file_type = EXT4_FT_DIR;
		dentries[0].uid = 0;
		dentries[0].gid = 0;
    #ifndef USE_MINGW
		if (sehnd) {
			if (selabel_lookup(sehnd, &dentries[0].secon, dentries[0].path, dentries[0].mode) < 0)
				error("cannot lookup security context for %s", dentries[0].path);
		}
    #endif
		entries++;
		dirs++;
	}

#ifdef USE_MINGW

#if 0
	//test create link
	 if (needs_lost_and_found){
                struct dentry *tmp = calloc(entries + 1, sizeof(struct dentry));
                memset(tmp, 0, sizeof(struct dentry));
                memcpy(tmp + 1, dentries, entries * sizeof(struct dentry));
                dentries = tmp;

                dentries[0].filename = strdup("test123456");
                dentries[0].path = strdup("/system/bin/test123456");
                dentries[0].full_path = NULL;
                dentries[0].size = 0;
                dentries[0].mode = S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
                dentries[0].file_type = EXT4_FT_SYMLINK;
                dentries[0].uid = 0;
                dentries[0].gid = 0;
		dentries[0].mtime = 0,
                dentries[0].link = strdup("/system/bin/toolbox"),
                entries++;
        }
#endif

	//create link node with dir match
	XLink* plink = getLinkList();
	while( plink ) {
		if( !strcmp( plink->dir, dir_path ) ) {
			struct dentry *tmp = calloc(entries + 1, sizeof(struct dentry));
            memset(tmp, 0, sizeof(struct dentry));
            memcpy(tmp + 1, dentries, entries * sizeof(struct dentry));
            dentries = tmp;

			dentries[0].filename = strdup(plink->linkname);			
			int dir_len = strlen(dir_path) + strlen(plink->linkname);
        	dentries[0].path = (char *)malloc(dir_len + 1);
        	sprintf(dentries[0].path, "%s%s", dir_path, plink->linkname);
            dentries[0].full_path = NULL;
            dentries[0].size = 0;
            //dentries[0].mode = S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
            dentries[0].mode = plink->mode;
            dentries[0].file_type = EXT4_FT_SYMLINK;
            dentries[0].uid = plink->uid;
            dentries[0].gid = plink->gid;
			dentries[0].mtime = 0,
            dentries[0].link = strdup(plink->linktarget),
            entries++;

			/*printf("create link node with dir match plink->dir:%s, plink->linkname:%s, \
				plink->linktarget:%s\n", plink->dir, plink->linkname, plink->linktarget);*/
		}
		plink = plink->pNext;
	}
#endif

	inode = make_directory(dir_inode, entries, dentries, dirs);

	for (i = 0; i < entries; i++) {
        //printf("dentries[%d] file_type:%d\n", i, dentries[i].file_type);
        
		if (dentries[i].file_type == EXT4_FT_REG_FILE) {
			entry_inode = make_file(dentries[i].full_path, dentries[i].size);
		} else if (dentries[i].file_type == EXT4_FT_DIR) {
			char *subdir_full_path = NULL;
			char *subdir_dir_path;
			if (dentries[i].full_path) {
				
            #ifndef USE_MINGW
        		ret = asprintf(&subdir_full_path, "%s/", dentries[i].full_path);
            #else
                int full_len = strlen(dentries[i].full_path) + strlen("/");

                subdir_full_path = (char *)malloc(full_len + 2);
                ret = sprintf(subdir_full_path, "%s/", dentries[i].full_path);
            #endif
				if (ret < 0)
					critical_error_errno("asprintf");
			}

        #ifndef USE_MINGW
        	ret = asprintf(&subdir_dir_path, "%s/", dentries[i].path);
        #else
            int sub_len = strlen(dentries[i].path) + strlen("/");

            subdir_dir_path = (char *)malloc(sub_len + 2);
            ret = sprintf(subdir_dir_path, "%s/", dentries[i].path);
        #endif

			if (ret < 0)
				critical_error_errno("asprintf");
			entry_inode = build_directory_structure(subdir_full_path,
					subdir_dir_path, inode, fs_config_func, sehnd, verbose);
			free(subdir_full_path);
			free(subdir_dir_path);
		} else if (dentries[i].file_type == EXT4_FT_SYMLINK) {
		    //printf("make link :%s\n", dentries[i].link);
			entry_inode = make_link(dentries[i].link);
		} else {
			error("unknown file type on %s", dentries[i].path);
			entry_inode = 0;
		}
		*dentries[i].inode = entry_inode;

		ret = inode_set_permissions(entry_inode, dentries[i].mode,
			dentries[i].uid, dentries[i].gid,
			dentries[i].mtime);
		if (ret)
			error("failed to set permissions on %s\n", dentries[i].path);

		/*
		 * It's important to call inode_set_selinux() before
		 * inode_set_capabilities(). Extended attributes need to
		 * be stored sorted order, and we guarantee this by making
		 * the calls in the proper order.
		 * Please see xattr_assert_sane() in contents.c
		 */
		ret = inode_set_selinux(entry_inode, dentries[i].secon);
		if (ret)
			error("failed to set SELinux context on %s\n", dentries[i].path);
		ret = inode_set_capabilities(entry_inode, dentries[i].capabilities);
		if (ret)
			error("failed to set capability on %s\n", dentries[i].path);

		free(dentries[i].path);
		free(dentries[i].full_path);
		free(dentries[i].link);
		free((void *)dentries[i].filename);
		free(dentries[i].secon);
	}

	free(dentries);
	return inode;
}

static u32 compute_block_size()
{
	return 4096;
}

static u32 compute_journal_blocks()
{
	u32 journal_blocks = DIV_ROUND_UP(info.len, info.block_size) / 64;
	if (journal_blocks < 1024)
		journal_blocks = 1024;
	if (journal_blocks > 32768)
		journal_blocks = 32768;
	return journal_blocks;
}

static u32 compute_blocks_per_group()
{
	return info.block_size * 8;
}

static u32 compute_inodes()
{
	return DIV_ROUND_UP(info.len, info.block_size) / 4;
}

static u32 compute_inodes_per_group()
{
	u32 blocks = DIV_ROUND_UP(info.len, info.block_size);
	u32 block_groups = DIV_ROUND_UP(blocks, info.blocks_per_group);
	u32 inodes = DIV_ROUND_UP(info.inodes, block_groups);
	inodes = ALIGN(inodes, (info.block_size / info.inode_size));

	/* After properly rounding up the number of inodes/group,
	 * make sure to update the total inodes field in the info struct.
	 */
	info.inodes = inodes * block_groups;

	return inodes;
}

static u32 compute_bg_desc_reserve_blocks()
{
	u32 blocks = DIV_ROUND_UP(info.len, info.block_size);
	u32 block_groups = DIV_ROUND_UP(blocks, info.blocks_per_group);
	u32 bg_desc_blocks = DIV_ROUND_UP(block_groups * sizeof(struct ext2_group_desc),
			info.block_size);

	u32 bg_desc_reserve_blocks =
			DIV_ROUND_UP(block_groups * 1024 * sizeof(struct ext2_group_desc),
					info.block_size) - bg_desc_blocks;

	if (bg_desc_reserve_blocks > info.block_size / sizeof(u32))
		bg_desc_reserve_blocks = info.block_size / sizeof(u32);

	return bg_desc_reserve_blocks;
}

void reset_ext4fs_info() {
    // Reset all the global data structures used by make_ext4fs so it
    // can be called again.
    memset(&info, 0, sizeof(info));
    memset(&aux_info, 0, sizeof(aux_info));

    if (info.sparse_file) {
        sparse_file_destroy(info.sparse_file);
        info.sparse_file = NULL;
    }
}

int make_ext4fs_sparse_fd(int fd, long long len,
                const char *mountpoint, struct selabel_handle *sehnd)
{
	reset_ext4fs_info();
	info.len = len;

	return make_ext4fs_internal(fd, NULL, mountpoint, NULL, 0, 1, 0, 0, sehnd, 0);
}

int make_ext4fs(const char *filename, long long len,
                const char *mountpoint, struct selabel_handle *sehnd)
{
	int fd;
	int status;

	reset_ext4fs_info();
	info.len = len;

	fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0644);
	if (fd < 0) {
		error_errno("open");
		return EXIT_FAILURE;
	}

	status = make_ext4fs_internal(fd, NULL, mountpoint, NULL, 0, 0, 0, 1, sehnd, 0);
	close(fd);

	return status;
}

/* return a newly-malloc'd string that is a copy of str.  The new string
   is guaranteed to have a trailing slash.  If absolute is true, the new string
   is also guaranteed to have a leading slash.
*/
char *canonicalize_slashes(const char *str, bool absolute)
{
	char *ret;
	int len = strlen(str);
	int newlen = len;
	char *ptr;

	if (len == 0 && absolute) {
		return strdup("/");
	}

	if (str[0] != '/' && absolute) {
		newlen++;
	}

#ifdef USE_MINGW
    if(absolute){
        if (str[len - 1] != '/') {
    		newlen++;
    	}
    }
    else{
        if (str[len - 1] != '\\') {
    		newlen++;
    	}
    }
#else
	if (str[len - 1] != '/') {
		newlen++;
	}
#endif
	ret = malloc(newlen + 1);
	if (!ret) {
		critical_error("malloc");
	}

	ptr = ret;
	if (str[0] != '/' && absolute) {
		*ptr++ = '/';
	}

	strcpy(ptr, str);
	ptr += len;

#ifdef USE_MINGW
    if(absolute){
        if (str[len - 1] != '/') {
    		*ptr++ = '/';
    	}
    }
    else{
        if (str[len - 1] != '\\') {
    		*ptr++ = '\\';
    	}
    }  
#else
    if (str[len - 1] != '/') {
		*ptr++ = '/';
	}
#endif

	if (ptr != ret + newlen) {
		critical_error("assertion failed\n");
	}

	*ptr = '\0';

	return ret;
}

char *canonicalize_abs_slashes(const char *str)
{
	return canonicalize_slashes(str, true);
}

char *canonicalize_rel_slashes(const char *str)
{
	return canonicalize_slashes(str, false);
}

int make_ext4fs_internal(int fd, const char *_directory,
                         const char *_mountpoint, fs_config_func_t fs_config_func, int gzip,
                         int sparse, int crc, int wipe,
                         struct selabel_handle *sehnd, int verbose)
{
	u32 root_inode_num;
	u16 root_mode;
	char *mountpoint;
	char *directory = NULL;

	if (setjmp(setjmp_env))
		return EXIT_FAILURE; /* Handle a call to longjmp() */

	if (_mountpoint == NULL) {
		mountpoint = strdup("");
	} else {
		mountpoint = canonicalize_abs_slashes(_mountpoint);
	}

	if (_directory) {
		directory = canonicalize_rel_slashes(_directory);
	}

	printf("make_ext4fs_internal _directory:%s directory:%s\n", _directory, directory);

	if (info.len <= 0)
		info.len = get_file_size(fd);

	if (info.len <= 0) {
		fprintf(stderr, "Need size of filesystem\n");
		return EXIT_FAILURE;
	}

	if (info.block_size <= 0)
		info.block_size = compute_block_size();

	/* Round down the filesystem length to be a multiple of the block size */
	info.len &= ~((u64)info.block_size - 1);

	if (info.journal_blocks == 0)
		info.journal_blocks = compute_journal_blocks();

	if (info.no_journal == 0)
		info.feat_compat = EXT4_FEATURE_COMPAT_HAS_JOURNAL;
	else
		info.journal_blocks = 0;

	if (info.blocks_per_group <= 0)
		info.blocks_per_group = compute_blocks_per_group();

	if (info.inodes <= 0)
		info.inodes = compute_inodes();

	if (info.inode_size <= 0)
		info.inode_size = 256;

	if (info.label == NULL)
		info.label = "";

	info.inodes_per_group = compute_inodes_per_group();

	info.feat_compat |=
			EXT4_FEATURE_COMPAT_RESIZE_INODE |
			EXT4_FEATURE_COMPAT_EXT_ATTR;

	info.feat_ro_compat |=
			EXT4_FEATURE_RO_COMPAT_SPARSE_SUPER |
			EXT4_FEATURE_RO_COMPAT_LARGE_FILE |
			EXT4_FEATURE_RO_COMPAT_GDT_CSUM;

	info.feat_incompat |=
			EXT4_FEATURE_INCOMPAT_EXTENTS |
			EXT4_FEATURE_INCOMPAT_FILETYPE;


	info.bg_desc_reserve_blocks = compute_bg_desc_reserve_blocks();

	printf("Creating filesystem with parameters:\n");
	printf("    Size: %llu\n", info.len);
	printf("    Block size: %d\n", info.block_size);
	printf("    Blocks per group: %d\n", info.blocks_per_group);
	printf("    Inodes per group: %d\n", info.inodes_per_group);
	printf("    Inode size: %d\n", info.inode_size);
	printf("    Journal blocks: %d\n", info.journal_blocks);
	printf("    Label: %s\n", info.label);

	ext4_create_fs_aux_info();

	printf("    Blocks: %llu\n", aux_info.len_blocks);
	printf("    Block groups: %d\n", aux_info.groups);
	printf("    Reserved block group size: %d\n", info.bg_desc_reserve_blocks);

	info.sparse_file = sparse_file_new(info.block_size, info.len);

	block_allocator_init();

	ext4_fill_in_sb();

	if (reserve_inodes(0, 10) == EXT4_ALLOCATE_FAILED)
		error("failed to reserve first 10 inodes");

	if (info.feat_compat & EXT4_FEATURE_COMPAT_HAS_JOURNAL)
		ext4_create_journal_inode();

	if (info.feat_compat & EXT4_FEATURE_COMPAT_RESIZE_INODE)
		ext4_create_resize_inode();

//#ifdef USE_MINGW
	// Windows needs only 'create an empty fs image' functionality
//	assert(!directory);
//	root_inode_num = build_default_directory_structure();
//#else
	if (directory)
		root_inode_num = build_directory_structure(directory, mountpoint, 0,
                        fs_config_func, sehnd, verbose);
	else
		root_inode_num = build_default_directory_structure();
//#endif

	root_mode = S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
	inode_set_permissions(root_inode_num, root_mode, 0, 0, 0);

#ifndef USE_MINGW
	if (sehnd) {
		char *secontext = NULL;

		if (selabel_lookup(sehnd, &secontext, mountpoint, S_IFDIR) < 0) {
			error("cannot lookup security context for %s", mountpoint);
		}
		if (secontext) {
			if (verbose) {
				printf("Labeling %s as %s\n", mountpoint, secontext);
			}
			inode_set_selinux(root_inode_num, secontext);
		}
		freecon(secontext);
	}
#endif

	ext4_update_free();

	ext4_queue_sb();

	printf("Created filesystem with %d/%d inodes and %d/%d blocks\n",
			aux_info.sb->s_inodes_count - aux_info.sb->s_free_inodes_count,
			aux_info.sb->s_inodes_count,
			aux_info.sb->s_blocks_count_lo - aux_info.sb->s_free_blocks_count_lo,
			aux_info.sb->s_blocks_count_lo);

	if (wipe)
		wipe_block_device(fd, info.len);

	write_ext4_image(fd, gzip, sparse, crc);

	sparse_file_destroy(info.sparse_file);
	info.sparse_file = NULL;

	free(mountpoint);
	free(directory);

#ifdef USE_MINGW
	clean_linklist();
#endif

	return 0;
}
