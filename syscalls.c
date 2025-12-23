/**
 * Stub implementations for newlib syscalls not provided by Pico SDK
 */

#include <errno.h>
#include <reent.h>

int _link(const char *oldpath, const char *newpath) {
    (void)oldpath;
    (void)newpath;
    errno = ENOSYS;
    return -1;
}

int _unlink_r(struct _reent *r, const char *path) {
    (void)r;
    (void)path;
    errno = ENOSYS;
    return -1;
}
