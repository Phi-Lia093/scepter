/* ============================================================================
 * Global errno variable
 * ============================================================================ */

int errno = 0;

/* ============================================================================
 * strerror - error message strings.
 * ============================================================================ */

#include <errno.h>

const char *strerror(int errnum)
{
    switch (errnum) {
        case 0:             return "Success";
        case EPERM:         return "Operation not permitted";
        case ENOENT:        return "No such file or directory";
        case EINTR:         return "Interrupted system call";
        case EIO:           return "I/O error";
        case ENXIO:         return "No such device or address";
        case E2BIG:         return "Argument list too long";
        case EBADF:         return "Bad file descriptor";
        case EAGAIN:        return "Resource temporarily unavailable";
        case ENOMEM:        return "Out of memory";
        case EACCES:        return "Permission denied";
        case EFAULT:        return "Bad address";
        case EBUSY:         return "Device or resource busy";
        case EEXIST:        return "File exists";
        case EXDEV:         return "Cross-device link";
        case ENODEV:        return "No such device";
        case ENOTDIR:       return "Not a directory";
        case EISDIR:        return "Is a directory";
        case EINVAL:        return "Invalid argument";
        case ENFILE:        return "Too many open files in system";
        case EMFILE:        return "Too many open files";
        case ENOTTY:        return "Not a tty";
        case EFBIG:         return "File too large";
        case ENOSPC:        return "No space left on device";
        case ESPIPE:        return "Illegal seek";
        case EROFS:         return "Read-only filesystem";
        case EMLINK:        return "Too many links";
        case EPIPE:         return "Broken pipe";
        case ERANGE:        return "Result out of range";
        case ENAMETOOLONG:  return "Name too long";
        case ENOSYS:        return "Function not implemented";
        case ENOTEMPTY:     return "Directory not empty";
        case ELOOP:         return "Too many levels of symbolic links";
        case EOVERFLOW:     return "Value too large for data type";
        default:            return "Unknown error";
    }
}
