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

static struct ceph_mount_info *cmount;

/**************************** P R O T O T Y P E S *****************************/
static void FOO_Init();
static void FOO_Final();
void INFINIFS_xfer_hints(aiori_xfer_hint_t * params);
static aiori_fd_t *FOO_Create(char *path, int flags, aiori_mod_opt_t *options);
static aiori_fd_t *FOO_Open(char *path, int flags, aiori_mod_opt_t *options);
static IOR_offset_t INFINIFS_Xfer(int access, aiori_fd_t *file, IOR_size_t *buffer,
                           IOR_offset_t length, IOR_offset_t offset, aiori_mod_opt_t *options);
static void FOO_Close(aiori_fd_t *, aiori_mod_opt_t *);
static void FOO_Delete(char *path, aiori_mod_opt_t *);
static void INFINIFS_Fsync(aiori_fd_t *, aiori_mod_opt_t *);
static IOR_offset_t INFINIFS_GetFileSize(aiori_mod_opt_t *, char *);
static int INFINIFS_StatFS(const char *path, ior_aiori_statfs_t *stat, aiori_mod_opt_t *options);
static int FOO_MkDir(const char *path, mode_t mode, aiori_mod_opt_t *options);
static int FOO_RmDir(const char *path, aiori_mod_opt_t *options);
static int INFINIFS_Access(const char *path, int mode, aiori_mod_opt_t *options);
static int FOO_Stat(const char *path, struct stat *buf, aiori_mod_opt_t *options);
static void INFINIFS_Sync(aiori_mod_opt_t *);
static option_help * FOO_options();

static aiori_xfer_hint_t * hints = NULL;

/************************** D E C L A R A T I O N S ***************************/
ior_aiori_t infinifs_aiori = {
        .name = "INFINIFS",
        .name_legacy = NULL,
        .initialize = FOO_Init,
        .finalize = FOO_Final,
        .create = FOO_Create,
        .open = FOO_Open,
        .xfer = INFINIFS_Xfer,
        .close = FOO_Close,
        .remove = FOO_Delete,
        .get_options = FOO_options,
        .get_version = aiori_get_version,
        .xfer_hints = INFINIFS_xfer_hints,
        .fsync = INFINIFS_Fsync,
        .get_file_size = INFINIFS_GetFileSize,
        .statfs = INFINIFS_StatFS,
        .mkdir = FOO_MkDir,
        .rmdir = FOO_RmDir,
        .access = INFINIFS_Access,
        .stat = FOO_Stat,
        .sync = INFINIFS_Sync,
        .enable_mdtest = true,
};

#define FOO_ERR(__err_str, __ret) do { \
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

static option_help * FOO_options(){
  return options;
}

static void FOO_Init()
{
        char *remote_prefix = "/";

        /* Short circuit if the options haven't been filled yet. */
        if (!o.user || !o.conf || !o.prefix) {
                WARN("FOO_Init() called before options have been populated!");
                return;
        }
        if (o.remote_prefix != NULL) {
                remote_prefix = o.remote_prefix;
        }

        /* Short circuit if the mount handle already exists */ 
        if (cmount) {
                return;
        }

        int ret;
        /* create FOO mount handle */
        ret = ceph_create(&cmount, o.user);
        if (ret) {
                FOO_ERR("unable to create FOO mount handle", ret);
        }

        /* set the handle using the Ceph config */
        ret = ceph_conf_read_file(cmount, o.conf);
        if (ret) {
                FOO_ERR("unable to read ceph config file", ret);
        }

        /* mount the handle */
        ret = ceph_mount(cmount, remote_prefix);
        if (ret) {
                FOO_ERR("unable to mount FOO", ret);
                ceph_shutdown(cmount);

        }

        Inode *root;

        /* try retrieving the root FOO inode */
        ret = ceph_ll_lookup_root(cmount, &root);
        if (ret) {
                FOO_ERR("uanble to retrieve root FOO inode", ret);
                ceph_shutdown(cmount);

        }

        return;
}

static void FOO_Final()
{
        /* shutdown */
        int ret = ceph_unmount(cmount);
        if (ret < 0) {
		FOO_ERR("ceph_umount failed", ret);
	}
        ret = ceph_release(cmount);
        if (ret < 0) {
                FOO_ERR("ceph_release failed", ret);
        }
	cmount = NULL;
}

static aiori_fd_t *FOO_Create(char *path, int flags, aiori_mod_opt_t *options)
{
        return FOO_Open(path, flags | IOR_CREAT, options);
}

static aiori_fd_t *FOO_Open(char *path, int flags, aiori_mod_opt_t *options)
{
        const char *file = pfix(path);
        int* fd;
        fd = (int *)malloc(sizeof(int));

        mode_t mode = 0664;
        int ceph_flags = (int) 0;

        /* set IOR file flags to FOO flags */
        /* -- file open flags -- */
        if (flags & IOR_RDONLY) {
                ceph_flags |= CEPH_O_RDONLY;
        }
        if (flags & IOR_WRONLY) {
                ceph_flags |= CEPH_O_WRONLY;
        }
        if (flags & IOR_RDWR) {
                ceph_flags |= CEPH_O_RDWR;
        }
        if (flags & IOR_APPEND) {
                FOO_ERR("File append not implemented in FOO", EINVAL);
        }
        if (flags & IOR_CREAT) {
                ceph_flags |= CEPH_O_CREAT;
        }
        if (flags & IOR_EXCL) {
                ceph_flags |= CEPH_O_EXCL;
        }
        if (flags & IOR_TRUNC) {
                ceph_flags |= CEPH_O_TRUNC;
        }
        if (flags & IOR_DIRECT) {
                FOO_ERR("O_DIRECT not implemented in FOO", EINVAL);
        }
        *fd = ceph_open(cmount, file, ceph_flags, mode);
        if (*fd < 0) {
                FOO_ERR("ceph_open failed", *fd);
        }
        if (o.olazy == TRUE) {
                int ret = ceph_lazyio(cmount, *fd, 1);
                if (ret != 0) {
                        WARN("Error enabling lazy mode");
                }
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

static void FOO_Close(aiori_fd_t *file, aiori_mod_opt_t *options)
{
        int fd = *(int *) file;
        int ret = ceph_close(cmount, fd);
        if (ret < 0) {
                FOO_ERR("ceph_close failed", ret);
        }
        free(file);
        return;
}

static void FOO_Delete(char *path, aiori_mod_opt_t *options)
{
        int ret = ceph_unlink(cmount, pfix(path));
        if (ret < 0) {
                FOO_ERR("ceph_unlink failed", ret);
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

static int FOO_MkDir(const char *path, mode_t mode, aiori_mod_opt_t *options)
{
        return ceph_mkdir(cmount, pfix(path), mode);
}

static int FOO_RmDir(const char *path, aiori_mod_opt_t *options)
{
        return ceph_rmdir(cmount, pfix(path));
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
