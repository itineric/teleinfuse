/*
 * teleinfuse is a FUSE module to access to the Télé information of linky electric meter running in standard mode
 * Télé info data are transmitted by french electric meters (EDF/ERDF)
 * [FR] Permet de lire la téléinformation cliente (TIC) d'un compteur linky en mode standard.
 * [FR] Pour le mode TIC historique, voir le projet original
 *
 * Based on https://github.com/neomilium/teleinfuse project by Romuald Conty
 *
 * Copyright (C) 2020 itineric
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>

#include <pthread.h>

#define FUSE_USE_VERSION 26
#include <fuse.h>
#include <fuse/fuse_opt.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

#include <syslog.h>
#include <unistd.h>
#include <time.h>

#include "teleinfo.h"

#include <time.h>

typedef struct {
  char filename[18];
  char content[99];
  time_t time;
} teleinfuse_file;

typedef struct {
  uint interval;
  int with_datetime;
  const char* port;
} teleinfuse_args;

static pthread_mutex_t teleinfuse_files_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_t teleinfuse_thread;
teleinfuse_args teleinfuse_thread_args;

static teleinfuse_file teleinfuse_files[TI_MESSAGE_COUNT_MAX + 1]; // (+ 1 -> fake status file)
static size_t teleinfuse_files_count = 0;

teleinfuse_file* teleinfuse_find_file(const char* label)
{
  teleinfuse_file * file = NULL;
  for (size_t n=0; n<teleinfuse_files_count; n++) {
    if (0==strcmp(label, teleinfuse_files[n].filename)) {
      file = &(teleinfuse_files[n]);
      break;
    }
  }
  return file;
}

void teleinfuse_update_file (const char* name, const char* content)
{
  time_t now = time(NULL);
  teleinfuse_file * file;

  if ( (file=teleinfuse_find_file (name)) ) {
    if (0!=strcmp(content, file->content)) {
      strcpy (file->content, content);
      file->time = now;
    } // else do nothing
  } else {
    // New file
    file = &(teleinfuse_files[teleinfuse_files_count]);
    strcpy(file->filename, name);
    strcpy(file->content,  content);
    teleinfuse_files[teleinfuse_files_count].time = now;
    teleinfuse_files_count++;
  }
}

void teleinfuse_update (const teleinfo_data dataset[], size_t datasetlen)
{
  char datetime_name[20];

  pthread_mutex_lock( &teleinfuse_files_mutex );
  for (int n=0; n<datasetlen; n++) {
    teleinfuse_update_file(dataset[n].label, dataset[n].value);

    if (teleinfuse_thread_args.with_datetime && strlen(dataset[n].datetime) > 0) {
      strcpy(datetime_name, dataset[n].label);
      strcat(datetime_name, DATETIME_FILENAME_SUFFIX);
      teleinfuse_update_file(datetime_name, dataset[n].datetime);
    }
  }
  pthread_mutex_unlock( &teleinfuse_files_mutex );
}

enum status { ONLINE, OFFLINE, DISCONNECTED, ERROR };
const char * status_str(enum status s)
{
  switch (s) {
    case ONLINE:
      return "online";
      break;
    case OFFLINE:
      return "offline";
      break;
    case DISCONNECTED:
      return "disconnected";
      break;
    case ERROR:
      return "error";
      break;
  }
  return "";
}

void* teleinfuse_process(void * userdata)
{
  int     err ;
  int teleinfo_serial_fd ;
  char teleinfo_buffer[TI_FRAME_LENGTH_MAX];
  enum status current_status = DISCONNECTED;
  enum status previous_status = DISCONNECTED;

  for(;;) {
    teleinfo_data teleinfo_dataset[TI_MESSAGE_COUNT_MAX];
    size_t teleinfo_data_count = 0;

    teleinfo_serial_fd = teleinfo_open(teleinfuse_thread_args.port);
    if (teleinfo_serial_fd) {
      err = teleinfo_read_frame ( teleinfo_serial_fd, teleinfo_buffer, sizeof(teleinfo_buffer));
      teleinfo_close (teleinfo_serial_fd);
      if (!err) {
        err = teleinfo_decode (teleinfo_buffer, teleinfo_dataset, &teleinfo_data_count);
      }
      if (!err) {
        current_status = ONLINE;
      } else if (err==EBADMSG){
        current_status = ERROR;
      } else {
        current_status = OFFLINE;
      }
    } else {
      current_status = DISCONNECTED;
    }
    // Add a fake teleinfo file to show status
    strcpy(teleinfo_dataset[teleinfo_data_count].label, "status");
    strcpy(teleinfo_dataset[teleinfo_data_count].value, status_str(current_status));
    if (current_status != previous_status) {
      syslog(LOG_INFO, "status changed: was \"%s\", now \"%s\"", status_str(previous_status), status_str(current_status));
      previous_status = current_status;
    }
    teleinfo_data_count++;

    teleinfuse_update (teleinfo_dataset, teleinfo_data_count);
    pthread_testcancel();
    sleep (teleinfuse_thread_args.interval);
    pthread_testcancel();
  }
}
static void *teleinfuse_init(struct fuse_conn_info *conn)
{
  pthread_create( &teleinfuse_thread, NULL, teleinfuse_process, NULL);
  return NULL;
}

static int teleinfuse_getattr(const char *path, struct stat *stbuf)
{
  int res = -ENOENT;

  memset(stbuf, 0, sizeof(struct stat));
  if(strcmp(path, "/") == 0) {
    stbuf->st_mode = S_IFDIR | 0755;
    stbuf->st_nlink = 2;
    res = 0;
  } else {
    pthread_mutex_lock( &teleinfuse_files_mutex );
    for (size_t n=0; n<teleinfuse_files_count; n++) {
      if(strcmp(path+1, teleinfuse_files[n].filename) == 0) {
        stbuf->st_mode = S_IFREG | 0444;
        stbuf->st_nlink = 1;
        stbuf->st_size = strlen(teleinfuse_files[n].content);
        stbuf->st_mtime = teleinfuse_files[n].time;
        res = 0;
        break;
      }
    }
    pthread_mutex_unlock( &teleinfuse_files_mutex );
  }
  return res;
}

static int teleinfuse_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                              off_t offset, struct fuse_file_info *fi)
{
  (void) offset;
  (void) fi;

  if(strcmp(path, "/") != 0)
    return -ENOENT;

  filler(buf, ".", NULL, 0);
  filler(buf, "..", NULL, 0);

  pthread_mutex_lock( &teleinfuse_files_mutex );
  for (size_t n=0; n<teleinfuse_files_count; n++) {
    filler(buf, teleinfuse_files[n].filename, NULL, 0);
  }
  pthread_mutex_unlock( &teleinfuse_files_mutex );

  return 0;
}

static int teleinfuse_open(const char *path, struct fuse_file_info *fi)
{
  int res = -ENOENT;
  pthread_mutex_lock( &teleinfuse_files_mutex );
  for (size_t n=0; n<teleinfuse_files_count; n++) {
    if(strcmp(path+1, teleinfuse_files[n].filename) == 0) {
      res = 0;
      break;
    }
  }
  pthread_mutex_unlock( &teleinfuse_files_mutex );

  if (res== -ENOENT) return res;

  if((fi->flags & 3) != O_RDONLY)
    res = -EACCES;

  return res;
}

static int teleinfuse_read(const char *path, char *buf, size_t size, off_t offset,
                           struct fuse_file_info *fi)
{
  size_t len;
  (void) fi;
  int res = -ENOENT;
  size_t n;

  pthread_mutex_lock( &teleinfuse_files_mutex );
  for (n=0; n<teleinfuse_files_count; n++) {
    if(strcmp(path+1, teleinfuse_files[n].filename) == 0) {
      res = 0;
      break;
    }
  }
  pthread_mutex_unlock( &teleinfuse_files_mutex );
  if (res== -ENOENT) return res;

  pthread_mutex_lock( &teleinfuse_files_mutex );
  len = strlen(teleinfuse_files[n].content);

  if (offset < len) {
    if (offset + size > len)
      size = len - offset;
    memcpy(buf, teleinfuse_files[n].content + offset, size);
  } else
    size = 0;

  pthread_mutex_unlock( &teleinfuse_files_mutex );

  return size;
}

static void teleinfuse_destroy(void * p)
{
  pthread_cancel (teleinfuse_thread);
  pthread_join (teleinfuse_thread, NULL);
}

static struct fuse_operations teleinfuse_oper = {
  .init       = teleinfuse_init,
  .getattr    = teleinfuse_getattr,
  .readdir    = teleinfuse_readdir,
  .open       = teleinfuse_open,
  .read       = teleinfuse_read,
  .destroy    = teleinfuse_destroy,
};
/** options for fuse_opt.h */
struct options {
   int interval;
   int with_datetime;
}options;

/** macro to define options */
#define TELEINFUSE_OPT_KEY(t, p, v) { t, offsetof(struct options, p), v }

/** keys for FUSE_OPT_ options */
enum
{
   KEY_VERSION,
   KEY_HELP,
};

static struct fuse_opt teleinfuse_opts[] =
{
  TELEINFUSE_OPT_KEY("interval=%d", interval, 10),
  TELEINFUSE_OPT_KEY("with_datetime", with_datetime, 1),
  FUSE_OPT_END
};

int main(int argc, char *argv[])
{
  // Args parssing
  if (argc<3) {
    printf ("Usage: %s DEV MOUNTPOINT\nExample: %s /dev/ttyUSB0 /house/electric_meter\n", argv[0], argv[0]);
    exit(EXIT_FAILURE);
  }

  struct fuse_args args = FUSE_ARGS_INIT(0, NULL);
  for(int i = 0; i < argc; i++) {
    if (i == 1) {
      // We skip the first arg: it a device link to serial port
    } else {
      fuse_opt_add_arg(&args, argv[i]);
    }
  }
  if (fuse_opt_parse(&args, &options, teleinfuse_opts, NULL) == -1)
    /** error parsing options */
    return -1;

  openlog("teleinfuse", LOG_PID, LOG_USER) ;
  syslog(LOG_INFO, "starting teleinfuse with %ds intervals", options.interval);
  teleinfuse_thread_args.port = argv[1];
  teleinfuse_thread_args.interval = options.interval;

  int fd;
  if ( (fd = teleinfo_open(teleinfuse_thread_args.port)) ) { // Be sure the port is reacheable
    teleinfo_close(fd);
    fuse_main (args.argc, args.argv, &teleinfuse_oper, NULL);
  } else {
    fprintf(stderr, "Unable to reach \"%s\" as serial port.\n", teleinfuse_thread_args.port);
  }

  closelog() ;
  exit(EXIT_SUCCESS) ;
}
