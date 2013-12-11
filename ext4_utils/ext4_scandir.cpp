/*
 * Copyright (C) 2013 The Android Open Source Project
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

#ifdef USE_MINGW

#include <dirent.h>

#include <errno.h>
#include <stdlib.h>

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

class ScopedReaddir {
 public:
  ScopedReaddir(const char* path) {
    dir_ = opendir(path);
  }

  ~ScopedReaddir() {
    if (dir_ != NULL) {
      closedir(dir_);
    }
  }

  bool IsBad() {
    return dir_ == NULL;
  }

  dirent* ReadEntry() {
    return readdir(dir_);
  }

 private:
  DIR* dir_;

  // Disallow copy and assignment.
  ScopedReaddir(const ScopedReaddir&);
  void operator=(const ScopedReaddir&);
};

// A smart pointer to the scandir dirent**.
class ScandirResult {
 public:
  ScandirResult() : names_(NULL), size_(0), capacity_(0) {
  }

  ~ScandirResult() {
    while (size_ > 0) {
      free(names_[--size_]);
    }
    free(names_);
  }

  size_t size() {
    return size_;
  }

  dirent** release() {
    dirent** result = names_;
    names_ = NULL;
    size_ = capacity_ = 0;
    return result;
  }

  bool Add(dirent* entry) {
    if (size_ >= capacity_) {
      size_t new_capacity = capacity_ + 32;
      dirent** new_names = (dirent**) realloc(names_, new_capacity * sizeof(dirent*));
      if (new_names == NULL) {
        return false;
      }
      names_ = new_names;
      capacity_ = new_capacity;
    }

    dirent* copy = CopyDirent(entry);
    if (copy == NULL) {
      return false;
    }

    //printf("copy name:%s\n", copy->d_name);
    
    names_[size_++] = copy;
    return true;
  }

  void Sort(int (*comparator)(const dirent**, const dirent**)) {
    // If we have entries and a comparator, sort them.
    if (size_ > 0 && comparator != NULL) {
      qsort(names_, size_, sizeof(dirent*), (int (*)(const void*, const void*)) comparator);
    }
  }

 private:
  dirent** names_;
  size_t size_;
  size_t capacity_;

  static dirent* CopyDirent(dirent* original) {
#ifdef USE_MINGW
    size_t size = ((sizeof(dirent) + 3) & ~3);
    
    dirent* copy = (dirent*) malloc(size);
    //memcpy(copy, original, original->d_reclen);
    strcpy(copy->d_name, original->d_name);
    return copy;
#else
    // Allocate the minimum number of bytes necessary, rounded up to a 4-byte boundary.
    size_t size = ((original->d_reclen + 3) & ~3);
    
    dirent* copy = (dirent*) malloc(size);
    memcpy(copy, original, original->d_reclen);
    return copy;
#endif
    
  }

  // Disallow copy and assignment.
  ScandirResult(const ScandirResult&);
  void operator=(const ScandirResult&);
};

extern "C" int ext4_scandir(const char* dirname, dirent*** name_list,
            int (*filter)(const dirent*),
            int (*comparator)(const dirent**, const dirent**)) {
  ScopedReaddir reader(dirname);
  if (reader.IsBad()) {
    return -1;
  }

  ScandirResult names;
  dirent* entry;
  while ((entry = reader.ReadEntry()) != NULL) {
    // If we have a filter, skip names that don't match.
    if (filter != NULL && !(*filter)(entry)) {
      continue;
    }

    //printf("entry name:%s\n", entry->d_name);
    
    names.Add(entry);
  }

  names.Sort(comparator);

  size_t size = names.size();
  *name_list = names.release();
  return size;
}

#if 0
#define BUFFER_SIZE 1048576
void copy_file(char *src_path, char *dst_path)
{
    void copy_dir();
    int from_fd;
    int to_fd;
    int bytes_read;
    int bytes_write;
    char buffer[BUFFER_SIZE];
    char *p;

    if((from_fd = open(src_path, O_RDONLY)) == -1) {
        fprintf(stderr, "Open %s Error:%s\n", src_path, strerror(errno));
        exit(1);
    }

    if((to_fd = open(dst_path, O_WRONLY|O_CREAT, S_IRUSR|S_IWUSR)) == -1) {
        fprintf(stderr,"Open %s Error:%s\n", dst_path, strerror(errno));
        exit(1);
    }

    while(bytes_read = read(from_fd, buffer, BUFFER_SIZE)) {
        if((bytes_read == -1) && (errno != EINTR)) {
            fprintf(stderr,"read  %s Error:%s\n", src_path, strerror(errno));
            break;
        }
        else if(bytes_read > 0) {
            p = buffer;
            while(bytes_write = write(to_fd, p, bytes_read)) {
                if((bytes_write == -1) && (errno != EINTR)) {
                    fprintf(stderr,"write %s Error:%s\n", dst_path, strerror(errno));
                    break;
                }
                else if(bytes_write == bytes_read)
                    break;
                else if(bytes_write > 0) {
                    p += bytes_write;
                    bytes_read -= bytes_write;
                }
            }
            if(bytes_write == -1) {
                fprintf(stderr,"write %s Error:%s\n", dst_path, strerror(errno));
                break;
            }
        }
    }
    close(from_fd);
    close(to_fd);
}

#define S_IFLNK 0120000
void scan_dir(const char* dirname, dirent*** name_list,
            int (*filter)(const dirent*),
            int (*comparator)(const dirent**, const dirent**))
{
    DIR *dp;
    DIR *dp1;
    char pp[255];
    char pn[255];
    int i;
    struct stat sbuf;
    struct dirent *dir;

    ScandirResult names;
    
    if((dp = opendir(dirname)) == NULL) {
        printf("Open Directory %s Error:%s\n", dirname, strerror(errno));
        return;
    }
    
    while ((dir = readdir(dp)) != NULL) {
        if (dir->d_ino == 0)
            continue;

        if (filter != NULL && !(*filter)(dir)) {
            continue;
        }
        
        strcpy(pn, dirname);
        strcat(pn, "/");
        strcat(pn, dir->d_name);
        if (stat(pn,&sbuf) < 0) {
            perror(pn);
            closedir(dp);
            return;
        }

        if ( ((sbuf.st_mode & S_IFMT) != S_IFLNK) &&
            ((sbuf.st_mode & S_IFMT) == S_IFDIR) &&
            (strcmp(dir->d_name, ".") != 0) &&
            (strcmp(dir->d_name, "..") != 0) ) {
            
            if((dp1 = opendir(pn)) == NULL) {
                printf("Open Directory %s Error:%s\n",pn,strerror(errno));
                return;
            }
            /*
            if((mkdir(pp, 0700)))
                break;*/
            scan_dir(pn, name_list, filter, comparator);
        }
        else if ( ((sbuf.st_mode & S_IFMT) != S_IFLNK) &&
            ((sbuf.st_mode & S_IFMT) == S_IFREG) &&
            (strcmp(dir->d_name,".") != 0) &&
            (strcmp(dir->d_name, "..") != 0) ) {
            
            //copy_file(pn,pp);
            names.Add(dir);
        }
    }

    closedir(dp);
}
#endif

#endif
