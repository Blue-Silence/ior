/* -*- mode: c; c-basic-offset: 8; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */
/******************************************************************************\
*                                                                              *
* (C) 2015 The University of Chicago                                           *
* (C) 2020 Red Hat, Inc.                                                       *
*                                                                              *
* See COPYRIGHT in top-level directory.                                        *
*                                                                              *
********************************************************************************
*
* Implement abstract I/O interface for FOO.
*
\******************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "ior.h"
#include "iordef.h"
#include "aiori.h"
#include "utilities.h"

#include "infinifs_client_header.h"

#define CEPH_O_RDONLY          00000000
#define CEPH_O_WRONLY          00000001
#define CEPH_O_RDWR            00000002
#define CEPH_O_CREAT           00000100
#define CEPH_O_EXCL            00000200
#define CEPH_O_TRUNC           00001000
#define CEPH_O_LAZY            00020000
#define CEPH_O_DIRECTORY       00200000
#define CEPH_O_NOFOLLOW        00400000

/************************** O P T I O N S *****************************/
struct INFINIFS_options{
  char * config_path;
  char * prefix;
};

static struct INFINIFS_options o = {
  .config_path = NULL,
  .prefix = NULL,
};

static option_help options [] = {
      {0, "INFINIFS.config_path", "Config path for the client", OPTION_OPTIONAL_ARGUMENT, 's', & o.config_path},
      {0, "INFINIFS.prefix", "Mount prefix", OPTION_OPTIONAL_ARGUMENT, 's', & o.prefix},
      LAST_OPTION
};

struct {
    char * prefix;
    void * client;
} mount;

static struct ceph_mount_info *cmount;

/**************************** P R O T O T Y P E S *****************************/
static void INFINIFS_Init();
static void INFINIFS_Final();
void INFINIFS_xfer_hints(aiori_xfer_hint_t * params);
static aiori_fd_t *INFINIFS_Create(char *path, int flags, aiori_mod_opt_t *options);
static aiori_fd_t *INFINIFS_Open(char *path, int flags, aiori_mod_opt_t *options);
static IOR_offset_t INFINIFS_Xfer(int access, aiori_fd_t *file, IOR_size_t *buffer,
                           IOR_offset_t length, IOR_offset_t offset, aiori_mod_opt_t *options);
static void INFINIFS_Close(aiori_fd_t *, aiori_mod_opt_t *);
static void INFINIFS_Delete(char *path, aiori_mod_opt_t *);
static void INFINIFS_Fsync(aiori_fd_t *, aiori_mod_opt_t *);
static IOR_offset_t INFINIFS_GetFileSize(aiori_mod_opt_t *, char *);
static int INFINIFS_StatFS(const char *path, ior_aiori_statfs_t *stat, aiori_mod_opt_t *options);
static int INFINIFS_MkDir(const char *path, mode_t mode, aiori_mod_opt_t *options);
static int INFINIFS_RmDir(const char *path, aiori_mod_opt_t *options);
static int INFINIFS_Access(const char *path, int mode, aiori_mod_opt_t *options);
static int FOO_Stat(const char *path, struct stat *buf, aiori_mod_opt_t *options);
static void INFINIFS_Sync(aiori_mod_opt_t *);
static option_help * INFINIFS_options();

static aiori_xfer_hint_t * hints = NULL;

/************************** D E C L A R A T I O N S ***************************/
ior_aiori_t infinifs_aiori = {
        .name = "INFINIFS",
        .name_legacy = NULL,
        .initialize = INFINIFS_Init,
        .finalize = INFINIFS_Final,
        .create = INFINIFS_Create,
        .open = INFINIFS_Open,
        .xfer = INFINIFS_Xfer,
        .close = INFINIFS_Close,
        .remove = INFINIFS_Delete,
        .get_options = INFINIFS_options,
        .get_version = aiori_get_version,
        .xfer_hints = INFINIFS_xfer_hints,
        .fsync = INFINIFS_Fsync,
        .get_file_size = INFINIFS_GetFileSize,
        .statfs = INFINIFS_StatFS,
        .mkdir = INFINIFS_MkDir,
        .rmdir = INFINIFS_RmDir,
        .access = INFINIFS_Access,
        .stat = FOO_Stat,
        .sync = INFINIFS_Sync,
        .enable_mdtest = true,
};

#define INFINIFS_ERR(__err_str, __ret) do { \
        errno = -__ret; \
        ERR(__err_str); \
} while(0)

/***************************** F U N C T I O N S ******************************/

void INFINIFS_xfer_hints(aiori_xfer_hint_t * params)
{
  hints = params;
}

static const char* pfix(const char* path) {
        const char* npath = path;
        const char* prefix = o.prefix;
        while (*prefix) {
                if(*prefix++ != *npath++) {
                        return path;
                }
        }
        return npath;
}

static option_help * INFINIFS_options(){
  return options;
}

static void INFINIFS_Init()
{

        /* Short circuit if the options haven't been filled yet. */
        if (!o.config_path || !o.prefix ) {
                WARN("INFINIFS_Init() called before options have been populated!");
                return;
        }

        mount.prefix = o.prefix;

        void *client = infinifs_new_client(o.config_path);
        mount.client = client;

        return;
}

static void INFINIFS_Final()
{
        /* shutdown */
        infinifs_destroy_client(mount.client);
        mount.client = NULL;
}


static aiori_fd_t *INFINIFS_Create(char *path, int flags, aiori_mod_opt_t *options)
{
        const char *file = pfix(path);
        int* fd;
        fd = (int *)malloc(sizeof(int));

        *fd = infinifs_create(mount.client, file);
        if (*fd < 0) {
                INFINIFS_ERR("infinifs_create failed", *fd);
        }
        return (void *) fd;
}

static aiori_fd_t *INFINIFS_Open(char *path, int flags, aiori_mod_opt_t *options)
{
        const char *file = pfix(path);
        int* fd;
        fd = (int *)malloc(sizeof(int));

        *fd = infinifs_open(mount.client, file);
        if (*fd < 0) {
                INFINIFS_ERR("infinifs_open failed", *fd);
        }
        return (void *) fd;
}

static IOR_offset_t INFINIFS_Xfer(int access, aiori_fd_t *file, IOR_size_t *buffer,
                           IOR_offset_t length, IOR_offset_t offset, aiori_mod_opt_t *options)
{
        //Empty func for now.
        return 0;
}

static void INFINIFS_Fsync(aiori_fd_t *file, aiori_mod_opt_t *options)
{
        //Empty function for now.
}

static void INFINIFS_Close(aiori_fd_t *file, aiori_mod_opt_t *options)
{
        int fd = *(int *) file;
        int ret = infinifs_close(mount.client, fd);
        if (ret < 0) {
                INFINIFS_ERR("infinifs_close failed", ret);
        }
        free(file);
        return;
}

static void INFINIFS_Delete(char *path, aiori_mod_opt_t *options)
{
        int ret = infinifs_delete(mount.client, pfix(path));
        if (ret < 0) {
                INFINIFS_ERR("infinifs_delete failed", ret);
        }
        return;
}

static IOR_offset_t INFINIFS_GetFileSize(aiori_mod_opt_t *options, char *path)
{
        //Empty function for now.
}

static int INFINIFS_StatFS(const char *path, ior_aiori_statfs_t *stat_buf, aiori_mod_opt_t *options)
{       
        //No support for now.
        WARN("StatFS NOT SUPPORTED!");
        return -1;
}

static int INFINIFS_MkDir(const char *path, mode_t mode, aiori_mod_opt_t *options)
{
        return infinifs_mkdir(cmount, pfix(path), mode);
}

static int INFINIFS_RmDir(const char *path, aiori_mod_opt_t *options)
{
        return infinifs_rmdir(cmount, pfix(path));
}

static int INFINIFS_Access(const char *path, int mode, aiori_mod_opt_t *options)
{
        //For now WE DO NOT CHECK FOR USER PERMISSION. 
        return 0;
}

static int FOO_Stat(const char *path, struct stat *buf, aiori_mod_opt_t *options)
{
        return ceph_stat(cmount, pfix(path), buf);
}

static void INFINIFS_Sync(aiori_mod_opt_t *options)
{
        //Empty function for now.
}
