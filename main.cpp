/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
  This program can be distributed under the terms of the GNU GPLv2.
  See the file COPYING.
*/
#define FUSE_USE_VERSION 31
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <assert.h>
#include <getopt.h>
#include <stdlib.h>
#include <dirent.h>
#include <string>
#include <filesystem>
#include <nos/print.h>
#include <nos/fprint.h>
#include <nos/io/file.h>
#include <nos/trace.h>

struct xmp_dirp
{
        DIR *dp;
        struct dirent *entry;
        off_t offset;
};

namespace fs = std::filesystem;

/*
 * Command line options
 *
 * We can't set default values for the char* fields here because
 * fuse_opt_parse would attempt to free() them when the user specifies
 * different values on the command line.
 */
static struct options
{
        const char *filename;
        const char *contents;
        const char *target;
        const char *pwd;
        int show_help;
} options;
#define OPTION(t, p)                           \
    { t, offsetof(struct options, p), 1 }
static const struct fuse_opt option_spec[] =
{
        OPTION("--name=%s", filename),
        OPTION("--contents=%s", contents),
        OPTION("--target=%s", target),
        OPTION("-h", show_help),
        OPTION("--help", show_help),
        FUSE_OPT_END
};
static void *hello_init(struct fuse_conn_info *conn,
                        struct fuse_config *cfg)
{
        TRACE();
        (void) conn;
        cfg->kernel_cache = 1;
        return NULL;
}
static int hello_getattr(const char *path, struct stat *stbuf,
                         struct fuse_file_info *fi)
{
        TRACE();
        auto realpath = fs::absolute(fs::path(options.pwd) / fs::path(options.target))
                        / fs::relative(path, "/");
        PRINT(realpath.c_str());

        (void) fi;
        int res = 0;
        memset(stbuf, 0, sizeof(struct stat));
        if (strcmp(path, "/") == 0)
        {
                stbuf->st_mode = S_IFDIR | 0755;
                stbuf->st_nlink = 2;
        }
        else if (fs::exists(realpath))
        {
                stat(realpath.c_str(), stbuf);
        }
        else
                res = -ENOENT;
        return res;
}

static inline struct xmp_dirp *get_dirp(struct fuse_file_info *fi)
{
        return (struct xmp_dirp *) (uintptr_t) fi->fh;
}

static int hello_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                         off_t offset, struct fuse_file_info *fi,
                         enum fuse_readdir_flags flags)
{
        TRACE();
        struct xmp_dirp *d = get_dirp(fi);

        (void) path;
        if (offset != d->offset)
        {
                seekdir(d->dp, offset);
                d->entry = NULL;
                d->offset = offset;
        }
        while (1)
        {
                struct stat st;
                off_t nextoff;
                enum fuse_fill_dir_flags fill_flags = (fuse_fill_dir_flags)0;

                if (!d->entry)
                {
                        d->entry = readdir(d->dp);
                        if (!d->entry)
                                break;
                }
                if (!(fill_flags & FUSE_FILL_DIR_PLUS))
                {
                        memset(&st, 0, sizeof(st));
                        st.st_ino = d->entry->d_ino;
                        st.st_mode = d->entry->d_type << 12;
                }
                nextoff = telldir(d->dp);
                if (filler(buf, d->entry->d_name, &st, nextoff, fill_flags))
                        break;

                d->entry = NULL;
                d->offset = nextoff;
        }

        return 0;
}

static int hello_opendir(const char *path, struct fuse_file_info *fi)
{
        TRACE();
        auto rootpath = fs::absolute(fs::path(options.pwd) / fs::path(options.target));
        auto realpath = rootpath / fs::relative(path, "/");

        if (!fs::exists(realpath))
                return -ENOENT;
        if ((fi->flags & O_ACCMODE) != O_RDONLY)
                return -EACCES;

        struct xmp_dirp *d = (struct xmp_dirp*)malloc(sizeof(struct xmp_dirp));
        d->dp = opendir(realpath.c_str());
        if (d->dp == NULL)
        {
                free(d);
                return -errno;
        }
        d->offset = 0;
        d->entry = NULL;

        fi->fh = (unsigned long) d;
        return 0;
}

static int hello_open(const char *path, struct fuse_file_info *fi)
{
        TRACE();
        auto rootpath = fs::absolute(fs::path(options.pwd) / fs::path(options.target));
        auto realpath = rootpath / fs::relative(path, "/");

        if (!fs::exists(realpath))
                return -ENOENT;
        if ((fi->flags & O_ACCMODE) != O_RDONLY)
                return -EACCES;

        fi->fh = open(realpath.c_str(), O_RDONLY);

        return 0;
}

static int hello_read(const char *path, char *buf, size_t size, off_t offset,
                      struct fuse_file_info *fi)
{
        TRACE();
        int retstat = 0;

        retstat = pread(fi->fh, buf, size, offset);
        if (retstat < 0)
                retstat = -1;

        return retstat;
}

static struct fuse_operations hello_oper =
{};

static void show_help(const char *progname)
{
        printf("usage: %s [options] <mountpoint>\n\n", progname);
        printf("File-system specific options:\n"
               "    --name=<s>          Name of the \"hello\" file\n"
               "                        (default: \"hello\")\n"
               "    --contents=<s>      Contents \"hello\" file\n"
               "                        (default \"Hello, World!\\n\")\n"
               "\n");
}
int main(int argc, char *argv[])
{
        hello_oper.init           = hello_init;
        hello_oper.getattr        = hello_getattr;
        hello_oper.readdir        = hello_readdir;
        hello_oper.opendir        = hello_opendir;
        hello_oper.open           = hello_open;
        hello_oper.read           = hello_read;

        int ret;
        struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
        /* Set defaults -- we have to use strdup so that
           fuse_opt_parse can free the defaults if other
           values are specified */
        options.filename = strdup("hello");
        options.contents = strdup("Hello World!\n");
        options.pwd = strdup(fs::current_path().c_str());
        /* Parse options */
        if (fuse_opt_parse(&args, &options, option_spec, NULL) == -1)
                return 1;
        /* When --help is specified, first print our own file-system
           specific help text, then signal fuse_main to show
           additional help (by adding `--help` to the options again)
           without usage: line (by setting argv[0] to the empty
           string) */
        if (options.show_help)
        {
                show_help(argv[0]);
                assert(fuse_opt_add_arg(&args, "--help") == 0);
                args.argv[0][0] = '\0';
        }
        ret = fuse_main(args.argc, args.argv, &hello_oper, NULL);
        fuse_opt_free_args(&args);
        return ret;
}