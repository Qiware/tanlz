#if !defined(__REDO_H__)
#define __REDO_H__

#include "comm.h"

#define OPEN_MODE           (0666)  /* 文件权值 */
#define OPEN_FLAGS          (O_CREAT|O_RDWR) /* 文件标志 */
#define DIR_MODE            (0775)  /* 目录权值 */

ssize_t Readn(int fd, void *buff, size_t n);
ssize_t Writen(int fd, const void *buff, size_t n);
int Open(const char *fpath, int flags, mode_t mode);
#define CLOSE(fd)  { if(fd > 0) { close(fd), fd = -1; }}
#define FCLOSE(fp) {fclose(fp), fp = NULL;}
#define FREE(p) { if (p) { free(p), p = NULL; }}

void Sleep(int sec);
int Mkdir(const char *dir, mode_t mode);
int Mkdir2(const char *fname, mode_t mode);

int Random(void);
int System(const char *cmd);

bool proc_is_exist(pid_t pid);

#if defined(HAVE_POSIX_MEMALIGN)
void *memalign_alloc(size_t alignment, size_t size);
#elif defined(HAVE_MEMALIGN)
#define memalign_alloc(alignment, size) memalign(alignment, size)
#else
#define memalign_alloc(alignment, size) malloc(size)
#endif

int bind_cpu(uint16_t id);
int set_fd_limit(int max);

/* 取数值上限 */
#define div_ceiling(num, mod) ((num)%(mod)? (num)/(mod)+1 : (num)/(mod))

struct tm *local_time(const time_t *timep, struct tm *result);

#endif /*__REDO_H__*/
