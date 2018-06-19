#ifndef __FreeBSD__
#error "What are you even doing"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <limits.h>
#include <string.h>
#include <err.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/capsicum.h>
#include "capsicumizer.h"

static int (*real_openat)(int fd, const char *path, int flags, ...);
static int (*real_shm_open)(const char *path, int flags, mode_t mode);

static const size_t NUM_DIRS = 128;
static char dirs[NUM_DIRS][PATH_MAX + 1] = {0};
static int fds[NUM_DIRS] = {0};
static int nextfd = 0;

static const size_t NUM_SHM_SUBSTRS = 16;
static char shm_substrs[NUM_SHM_SUBSTRS][PATH_MAX + 1] = {0};
static int nextshm = 0;

int capsicumize_dir(const char *path) {
	if (nextfd >= NUM_DIRS) {
		return ETOOMANY;
	}
	unsigned long len = strlen(path);
	if (len >= PATH_MAX) {
		return ETOOLONG;
	}
	strncpy(dirs[nextfd], path, PATH_MAX);
	dirs[nextfd][PATH_MAX] = '\0';
	fds[nextfd] = real_openat(AT_FDCWD, dirs[nextfd], O_DIRECTORY | O_CLOEXEC);
	return nextfd++;
}

int capsicumize_shm(const char *substr) {
	if (nextshm >= NUM_SHM_SUBSTRS) {
		return ETOOMANY;
	}
	unsigned long len = strlen(substr);
	if (len >= PATH_MAX) {
		return ETOOLONG;
	}
	strncpy(shm_substrs[nextshm], substr, PATH_MAX);
	return nextshm++;
}

__attribute__((constructor)) void init_capsicumizer() {
	real_openat = dlsym(RTLD_NEXT, "openat");
	real_shm_open = dlsym(RTLD_NEXT, "shm_open");
	char *env_dirs = getenv("CAPSICUMIZE_DIRS");
	char *env_shms = getenv("CAPSICUMIZE_SHM_SUBSTRS");
	if (env_dirs == NULL && env_shms == NULL) {
		return;
	}
	// NOTE: LD_PRELOAD (env) mode: we capsicumize here
	char *token;
	while ((token = strsep(&env_dirs, ":")) != NULL) {
		int ret = capsicumize_dir(token);
		if (ret == ETOOMANY) {
			errx(1, "Capsicumize: Too many directories");
		} else if (ret == ETOOLONG) {
			errx(1, "Capsicumize: Directory path too long");
		} else if (ret < 0) {
			errx(1, "Capsicumize: Unknown error");
		}
	}
	while ((token = strsep(&env_shms, ":")) != NULL) {
		int ret = capsicumize_shm(token);
		if (ret == ETOOMANY) {
			errx(1, "Capsicumize: Too many shm substrings");
		} else if (ret == ETOOLONG) {
			errx(1, "Capsicumize: Shm substring too long");
		} else if (ret < 0) {
			errx(1, "Capsicumize: Unknown error");
		}
	}
	if (cap_enter() != 0) {
		errx(2, "Capsicumize: Could not initialize Capsicum");
	}
}

#define FOR_DIRS(fd) for (size_t fd = 0; fd < nextfd; fd++)

#define IF_DIR_MATCH(dirlen) \
		unsigned long dirlen = strlen(dirs[fd]); \
		if (strlen(path) > dirlen && memcmp(dirs[fd], path, dirlen) == 0)

static size_t sliced_chars = 0;

static inline const char *slice_chop(const char *s, size_t offset) {
	const char *result = &s[offset];
	sliced_chars = 0;
	while (*result == '/') {
		result++;
		sliced_chars++;
	}
	return result;
}

int open(const char *path, int flags, ...) {
	va_list ap;
	int mode;
	if ((flags & O_CREAT) != 0) {
		va_start(ap, flags);
		mode = va_arg(ap, int);
		va_end(ap);
	} else {
		mode = 0;
	}

	printf("OPEN %s\n", path);
	FOR_DIRS(fd) {
		IF_DIR_MATCH(dirlen) {
			return real_openat(fds[fd], slice_chop(path, dirlen), flags, mode);
		}
	}
	return -1;
}

// libc's private open, used in e.g. fopen
int _open(const char *path, int flags, ...) {
	va_list ap;
	int mode;
	if ((flags & O_CREAT) != 0) {
		va_start(ap, flags);
		mode = va_arg(ap, int);
		va_end(ap);
	} else {
		mode = 0;
	}

	printf("_OPEN %s\n", path);
	FOR_DIRS(fd) {
		IF_DIR_MATCH(dirlen) {
			return real_openat(fds[fd], slice_chop(path, dirlen), flags, mode);
		}
	}
	return -1;
}

int openat(int fd, const char *path, int flags, ...) {
	va_list ap;
	int mode;
	if ((flags & O_CREAT) != 0) {
		va_start(ap, flags);
		mode = va_arg(ap, int);
		va_end(ap);
	} else {
		mode = 0;
	}

	if (fd != AT_FDCWD) {
		return real_openat(fd, path, flags, mode);
	}

	FOR_DIRS(fd) {
		IF_DIR_MATCH(dirlen) {
			return real_openat(fds[fd], slice_chop(path, dirlen), flags, mode);
		}
	}
	return -1;
}

void *dlopen(const char * restrict path, int mode) {
	printf("DLOPEN %s\n", path);
	FOR_DIRS(fd) {
		IF_DIR_MATCH(dirlen) {
			void *p = fdlopen(real_openat(fds[fd], slice_chop(path, dirlen), O_RDONLY | O_CLOEXEC | O_VERIFY), mode);
			printf("FDLOP %p\n", p);
			return p;
		}
	}
	return NULL;
}

int connect(int s, const struct sockaddr *name, socklen_t namelen) {
	struct sockaddr_un *sun = (struct sockaddr_un *)name;
	const char *path = sun->sun_path;

	FOR_DIRS(fd) {
		IF_DIR_MATCH(dirlen) {
			const char *newpath = slice_chop(path, dirlen);
			size_t newlen = namelen - dirlen - sliced_chars;
			memmove(sun->sun_path, newpath, newlen);
			sun->sun_path[newlen] = '\0';
			int x = connectat(fds[fd], s, name, newlen);
			return x;
		}
	}
	return -1;
}

int stat(const char * restrict path, struct stat * restrict sb) {
	printf("STAT %s\n", path);
	FOR_DIRS(fd) {
		IF_DIR_MATCH(dirlen) {
			return fstatat(fds[fd], slice_chop(path, dirlen), sb, 0);
		}
	}
	return -1;
}

int access(const char * restrict path, int mode) {
	printf("ACC %s\n", path);
	FOR_DIRS(fd) {
		IF_DIR_MATCH(dirlen) {
			return faccessat(fds[fd], slice_chop(path, dirlen), mode, 0);
		}
	}
	return -1;
}

int eaccess(const char * restrict path, int mode) {
	printf("EACC %s\n", path);
	FOR_DIRS(fd) {
		IF_DIR_MATCH(dirlen) {
			return faccessat(fds[fd], slice_chop(path, dirlen), mode, AT_EACCESS);
		}
	}
	return -1;
}

int shm_open(const char *path, int flags, mode_t mode) {
	for (size_t shm = 0; shm < nextshm; shm++) {
		if (strstr(path, shm_substrs[shm]) != NULL) {
			return real_shm_open(SHM_ANON, flags, mode);
		}
	}
	return real_shm_open(path, flags, mode);
}
