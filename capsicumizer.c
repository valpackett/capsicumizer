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
#include <libgen.h>
#include <err.h>
#include <errno.h>
#include <link.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/capsicum.h>
#include <libelf.h>
#include <gelf.h>
#include "capsicumizer.h"

static int (*real_openat)(int fd, const char *path, int flags, ...);
static int (*real_shm_open)(const char *path, int flags, mode_t mode);
static void *(*real_dlopen)(const char *path, int mode);

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
	real_dlopen = dlsym(RTLD_NEXT, "dlopen");
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

static inline void dl_probe(const char *prefix, const char *libname) {
	char *fullpath = NULL;
	asprintf(&fullpath, "%s/%s", prefix, libname);
	if (dlopen(fullpath, RTLD_LAZY | RTLD_NOLOAD) == NULL) {
		dlclose(dlopen(fullpath, RTLD_LAZY | RTLD_NODELETE));
	}
}

static inline bool dl_is_opened(const char *libname) {
	struct link_map *lm;
	// TODO: dlopen once? dlinfo once??
	if (dlinfo(real_dlopen(NULL, 0), RTLD_DI_LINKMAP, &lm) < 0) {
		errx(1, "Capsicumize: self-dlinfo %s", dlerror());
	}
	while (lm) {
		if (strcmp(libname, basename((char *)lm->l_name)) == 0) {
			return true;
		}
		lm = lm->l_next;
	}
	return false;
}

void *dlopen(const char * restrict path, int mode) {
	printf("DLOPEN %s\n", path);
	FOR_DIRS(fd) {
		IF_DIR_MATCH(dirlen) {
			int elffd = real_openat(fds[fd], slice_chop(path, dirlen), O_RDONLY | O_CLOEXEC | O_VERIFY);
			if (elffd < 1) {
				return NULL;
			}
			// Parsing needed libraries and loading them.
			// This is probably a reasonable way to handle dependencies?
			//
			// Ideally, rtld would just take our fds for lookup...
			// But it can only do that statically, via LD_LIBRARY_PATH_FDS,
			// which is read once at initialization into a static (private) variable :(
			elf_version(EV_CURRENT);
			Elf *elf = elf_begin(elffd, ELF_C_READ, NULL);
			if (elf == NULL) {
				errx(1, "Capsicumize: Failed to read elf");
			}
			GElf_Ehdr ehdr;
			if (gelf_getehdr(elf, &ehdr) == NULL) {
				errx(1, "Capsicumize: Failed to get elf hdr");
			}
			Elf_Scn *scn = NULL;
			GElf_Shdr shdr;
			while ((scn = elf_nextscn(elf, scn)) != NULL) {
				if (gelf_getshdr(scn, &shdr) == NULL) {
					errx(1, "Capsicumize: Failed to get elf section hdr");
				}
				char *secname = elf_strptr(elf, ehdr.e_shstrndx, shdr.sh_name);
				if (strncmp(secname, ".dynamic", 8) != 0) {
					continue;
				}
				Elf_Data *dat = NULL;
				while ((dat = elf_getdata(scn, dat)) != NULL) {
					GElf_Xword neededs[128] = {0};
					size_t nextneeded = 0;
					GElf_Xword rpaths[32] = {0};
					size_t nextrpath = 0;
					GElf_Dyn dyn;
					size_t didx = 0;
					while (gelf_getdyn(dat, didx, &dyn) == &dyn) {
						if (dyn.d_tag == DT_NEEDED && nextneeded < 128) {
							neededs[nextneeded] = dyn.d_un.d_val;
							nextneeded++;
						} else if ((dyn.d_tag == DT_RPATH || dyn.d_tag == DT_RUNPATH) && nextrpath < 32) {
							rpaths[nextrpath] = dyn.d_un.d_val;
							nextrpath++;
						}
						didx++;
					}
					for (size_t needed = 0; needed < nextneeded; needed++) {
						const char *neededstr = elf_strptr(elf, shdr.sh_link, neededs[needed]);
						if (dl_is_opened(neededstr)) {
							continue;
						}
						for (size_t rpath = 0; rpath < nextrpath; rpath++) {
							const char *rpathstr = elf_strptr(elf, shdr.sh_link, rpaths[rpath]);
							dl_probe(rpathstr, neededstr);
						}
						dl_probe("/usr/lib", neededstr);
						dl_probe("/lib", neededstr);
					}
				}
			}
			elf_end(elf);
			// lseek(elffd, 0, SEEK_SET);
			void *p = fdlopen(elffd, mode);
			printf("FDLOP %s %p\n", path, p);
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

int mkdir(const char *path, mode_t mode) {
	printf("MKD %s\n", path);
	FOR_DIRS(fd) {
		IF_DIR_MATCH(dirlen) {
			return mkdirat(fds[fd], slice_chop(path, dirlen), mode);
		} else if (strlen(path) == dirlen && memcmp(dirs[fd], path, dirlen) == 0) {
			return 0;
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
