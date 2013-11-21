#ifndef __SYSTEM_CHKSUM_H__
#define __SYSTEM_CHKSUM_H__

#include <fcntl.h>
#include <libgen.h>
#include <stdio.h>
#include <unistd.h>
#include "make_ext4fs.h"
#include "ext4_utils.h"
#include<dirent.h>
#include "md5.h"

#if defined(__linux__)
#include <linux/fs.h>
#elif defined(__APPLE__) && defined(__MACH__)
#include <sys/disk.h>
#endif

#ifdef ANDROID
#include <private/android_filesystem_config.h>
#endif

#ifndef USE_MINGW
#include <selinux/selinux.h>
#include <selinux/label.h>
#include <selinux/android.h>
#else
struct selabel_handle;
#endif

#ifdef USE_MINGW
#include "linklist.h"
extern int alphasort(const struct dirent** a, const struct dirent** b);
#endif


extern int filter_dot(const struct dirent *d);
extern int get_md5(const char *path, unsigned char* md5);

#ifndef MD5_DIGEST_LENGTH
#define MD5_DIGEST_LENGTH 16
#endif

void set_checksum_list_path(char *folder);
void set_root_dir(char* dir);
void rm_chksum_file();
void create_checksum_list(const char *_directory, const char* mountpoint);

#endif
