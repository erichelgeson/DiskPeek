/*
 * libhfs - library for reading and writing Macintosh HFS volumes
 * Windows implementation of OS abstraction layer.
 *
 * Based on os/unix.c by Robert Leslie.
 */

#ifdef _WIN32

#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <share.h>

#include "libhfs.h"
#include "os.h"

/*
 * NAME:	os->open()
 * DESCRIPTION:	open and lock a new descriptor from the given path and mode
 */
int os_open(void **priv, const char *path, int mode)
{
  HANDLE h;
  DWORD access;
  DWORD share;

  switch (mode)
    {
    case HFS_MODE_RDONLY:
      access = GENERIC_READ;
      share  = FILE_SHARE_READ;
      break;

    case HFS_MODE_RDWR:
    default:
      access = GENERIC_READ | GENERIC_WRITE;
      share  = 0;  /* exclusive access for write */
      break;
    }

  h = CreateFileA(path, access, share, NULL, OPEN_EXISTING,
                  FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE)
    ERROR(ENOENT, "error opening medium");

  *priv = (void *) h;

  return 0;

fail:
  return -1;
}

/*
 * NAME:	os->close()
 * DESCRIPTION:	close an open descriptor
 */
int os_close(void **priv)
{
  HANDLE h = (HANDLE) *priv;

  *priv = INVALID_HANDLE_VALUE;

  if (!CloseHandle(h))
    ERROR(EIO, "error closing medium");

  return 0;

fail:
  return -1;
}

/*
 * NAME:	os->same()
 * DESCRIPTION:	return 1 iff path is same as the open descriptor
 */
int os_same(void **priv, const char *path)
{
  HANDLE h = (HANDLE) *priv;
  BY_HANDLE_FILE_INFORMATION info1;
  HANDLE h2;
  BY_HANDLE_FILE_INFORMATION info2;

  if (!GetFileInformationByHandle(h, &info1))
    ERROR(EIO, "can't get path information");

  h2 = CreateFileA(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                   NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h2 == INVALID_HANDLE_VALUE)
    ERROR(EIO, "can't get path information");

  if (!GetFileInformationByHandle(h2, &info2))
    {
      CloseHandle(h2);
      ERROR(EIO, "can't get path information");
    }

  CloseHandle(h2);

  return info1.dwVolumeSerialNumber == info2.dwVolumeSerialNumber &&
         info1.nFileIndexHigh == info2.nFileIndexHigh &&
         info1.nFileIndexLow  == info2.nFileIndexLow;

fail:
  return -1;
}

/*
 * NAME:	os->seek()
 * DESCRIPTION:	set a descriptor's seek pointer (offset in blocks)
 */
unsigned long os_seek(void **priv, unsigned long offset)
{
  HANDLE h = (HANDLE) *priv;
  LARGE_INTEGER li;
  LARGE_INTEGER result;

  if (offset == (unsigned long) -1)
    {
      li.QuadPart = 0;
      if (!SetFilePointerEx(h, li, &result, FILE_END))
        ERROR(EIO, "error seeking medium");
    }
  else
    {
      li.QuadPart = (LONGLONG)offset << HFS_BLOCKSZ_BITS;
      if (!SetFilePointerEx(h, li, &result, FILE_BEGIN))
        ERROR(EIO, "error seeking medium");
    }

  return (unsigned long)(result.QuadPart >> HFS_BLOCKSZ_BITS);

fail:
  return -1;
}

/*
 * NAME:	os->read()
 * DESCRIPTION:	read blocks from an open descriptor
 */
unsigned long os_read(void **priv, void *buf, unsigned long len)
{
  HANDLE h = (HANDLE) *priv;
  DWORD bytes_to_read = (DWORD)(len << HFS_BLOCKSZ_BITS);
  DWORD bytes_read;

  if (!ReadFile(h, buf, bytes_to_read, &bytes_read, NULL))
    ERROR(EIO, "error reading from medium");

  return (unsigned long)bytes_read >> HFS_BLOCKSZ_BITS;

fail:
  return -1;
}

/*
 * NAME:	os->write()
 * DESCRIPTION:	write blocks to an open descriptor
 */
unsigned long os_write(void **priv, const void *buf, unsigned long len)
{
  HANDLE h = (HANDLE) *priv;
  DWORD bytes_to_write = (DWORD)(len << HFS_BLOCKSZ_BITS);
  DWORD bytes_written;

  if (!WriteFile(h, buf, bytes_to_write, &bytes_written, NULL))
    ERROR(EIO, "error writing to medium");

  return (unsigned long)bytes_written >> HFS_BLOCKSZ_BITS;

fail:
  return -1;
}

#endif /* _WIN32 */
