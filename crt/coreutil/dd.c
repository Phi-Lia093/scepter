/* dd - convert and copy a file/device */
#include "stdio.h"
#include "string.h"
#include "unistd.h"
#include "fcntl.h"
#include "stdlib.h"

#define BLOCK 512

static long parse_size(const char *s)
{
    long mult = 1;
    size_t len = strlen(s);
    if (len && (s[len-1] == 'K' || s[len-1] == 'k')) { mult = 1024; }
    else if (len && s[len-1] == 'M') { mult = 1024 * 1024; }
    else if (len && s[len-1] == 'G') { mult = 1024 * 1024 * 1024; }
    return strtol(s, NULL, 10) * mult;
}

int main(int argc, char *argv[])
{
    const char *ifile = NULL, *ofile = NULL;
    long bs = BLOCK, count = -1, skip = 0, seek = 0, conv = 0;

    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "if=", 3) == 0) ifile = argv[i] + 3;
        else if (strncmp(argv[i], "of=", 3) == 0) ofile = argv[i] + 3;
        else if (strncmp(argv[i], "bs=", 3) == 0) bs = parse_size(argv[i] + 3);
        else if (strncmp(argv[i], "count=", 6) == 0) count = strtol(argv[i] + 6, NULL, 10);
        else if (strncmp(argv[i], "skip=", 5) == 0) skip = strtol(argv[i] + 5, NULL, 10);
        else if (strncmp(argv[i], "seek=", 5) == 0) seek = strtol(argv[i] + 5, NULL, 10);
        else if (strncmp(argv[i], "conv=", 5) == 0) {
            if (strstr(argv[i] + 5, "notrunc")) conv |= 1;
        } else {
            fprintf(stderr, "dd: unrecognized operand: %s\n", argv[i]);
            return 1;
        }
    }

    if (!ifile || !ofile) {
        fprintf(stderr, "usage: dd if=FILE of=FILE [bs=N] [count=N] [skip=N] [seek=N]\n");
        return 1;
    }

    int in = open(ifile, O_RDONLY);
    if (in < 0) {
        fprintf(stderr, "dd: %s: cannot open\n", ifile);
        return 1;
    }
    int out = open(ofile, O_WRONLY | O_CREAT | (conv & 1 ? 0 : O_TRUNC), 0666);
    if (out < 0) {
        fprintf(stderr, "dd: %s: cannot create\n", ofile);
        close(in);
        return 1;
    }

    if (skip > 0)
        lseek(in, skip * bs, SEEK_SET);
    if (seek > 0)
        lseek(out, seek * bs, SEEK_SET);

    char buf[8192];
    long done = 0;
    int err = 0;

    long n = count;
    while (n != 0) {
        long want = (bs > (long)sizeof(buf)) ? (long)sizeof(buf) : bs;
        long r = read(in, buf, (size_t)want);
        if (r <= 0)
            break;
        long off = 0;
        while (off < r) {
            long w = write(out, buf + off, (size_t)(r - off));
            if (w <= 0) { err = 1; break; }
            off += w;
        }
        done += r;
        if (err) break;
        if (count > 0) n--;
    }

    fprintf(stderr, "%ld bytes copied\n", done);

    close(in);
    close(out);
    return err ? 1 : 0;
}
