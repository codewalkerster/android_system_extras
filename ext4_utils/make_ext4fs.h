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

#ifndef _MAKE_EXT4FS_H_
#define _MAKE_EXT4FS_H_

#ifdef __cplusplus
extern "C" {
#endif

struct selabel_handle;

int make_ext4fs(const char *filename, long long len,
                const char *mountpoint, struct selabel_handle *sehnd);
int make_ext4fs_sparse_fd(int fd, long long len,
                const char *mountpoint, struct selabel_handle *sehnd);

char *canonicalize_abs_slashes(const char *str);
char *canonicalize_rel_slashes(const char *str);

#ifdef USE_MINGW
int ext4_scandir(const char* dirname, struct dirent*** name_list,
            int (*filter)(const struct dirent*),
            int (*comparator)(const struct dirent**, const struct dirent**)) ;
#endif

#ifdef USE_MINGW

#include <winsock2.h>



/* These match the Linux definitions of these flags.
   L_xx is defined to avoid conflicting with the win32 versions.
*/

#define S_IFSOCK 0140000
#define S_IFLNK 0120000
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)
#define S_ISLNK(m) (((m) & S_IFMT) == S_IFLNK)

//#define  lstat    stat 

#define L_S_IRUSR 00400
#define L_S_IWUSR 00200
#define L_S_IXUSR 00100
#define S_IRWXU (L_S_IRUSR | L_S_IWUSR | L_S_IXUSR)
#define S_IRGRP 00040
#define S_IWGRP 00020
#define S_IXGRP 00010
#define S_IRWXG (S_IRGRP | S_IWGRP | S_IXGRP)
#define S_IROTH 00004
#define S_IWOTH 00002
#define S_IXOTH 00001
#define S_IRWXO (S_IROTH | S_IWOTH | S_IXOTH)
#define S_ISUID 0004000
#define S_ISGID 0002000
#define S_ISVTX 0001000

#include "linklist.h"

#else

#include <selinux/selinux.h>
#include <selinux/label.h>
#include <selinux/android.h>

#define O_BINARY 0

#endif



#ifdef __cplusplus
}
#endif

#endif
