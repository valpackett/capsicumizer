#include <ucl++.h>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

extern "C" {
#include <assert.h>
#include <capsicum_helpers.h>
#include <err.h>
#include <libpreopen.h>
#include <sys/capsicum.h>
#include <unistd.h>

extern char **environ;
}

using ucl::Ucl;

std::map<std::string, std::string> get_all_env() {
	std::map<std::string, std::string> result;
	for (char **env = environ; *env != nullptr; env++) {
		char *sep = strchrnul(*env, '=');
		result.emplace(std::string(*env, sep - *env), std::string(sep + 1));
	}
	return result;
}

void append_program_args(std::map<std::string, std::string> &vars, int argc, char **argv) {
	for (int i = 0; i < argc; ++i) {
		vars.emplace(std::to_string(i), std::string(argv[i]));
	}
}

std::string open_library_dirs(Ucl ucl_arr) {
	std::string result;
	for (const auto &val : ucl_arr) {
		// TODO check existence
		int fd = openat(AT_FDCWD, val.string_value().c_str(), O_DIRECTORY | O_RDONLY);
		result += std::to_string(fd);
		result += ":";
	}
	result.pop_back();
	return result;
}

struct po_map *open_access_dirs(Ucl ucl_arr) {
	std::vector<std::string> access_path;
	for (const auto &val : ucl_arr) {
		access_path.push_back(val.string_value());
	}
	struct po_map *pmap = po_map_create(access_path.size());
	for (const auto &dir : access_path) {
		po_preopen(pmap, dir.c_str(), O_DIRECTORY);
	}
	return pmap;
}

int pmap_to_shm_fd(struct po_map *pmap) {
	int shm_fd = po_pack(pmap);
	assert(shm_fd != -1);
	fcntl(shm_fd, F_SETFD, 0);  // un-CLOEXEC
	return shm_fd;
}

std::string join_ld_preload(Ucl ucl_arr) {
	std::string result = "libpreopen.so";
	for (const auto &val : ucl_arr) {
		result += ":";
		result += val.string_value();
	}
	return result;
}

int main(int argc, char *argv[]) {
	if (argc < 2) {
		std::cerr << "usage: " << argv[0] << " script args.." << std::endl;
		return -1;
	}

	auto ucl_vars = get_all_env();
	append_program_args(ucl_vars, argc-1, argv+1);

	std::string uclerr;
	Ucl script = Ucl::parse_from_file(argv[1], ucl_vars, uclerr);
	if (!uclerr.empty()) {
		std::cerr << uclerr << std::endl;
		return -1;
	}

	std::string bin_path = script[std::string("run")].string_value();
	int bin_fd = openat(AT_FDCWD, bin_path.c_str(), O_RDONLY);

	std::string lib_dir_fds = open_library_dirs(script[std::string("library_path")]);

	struct po_map *pmap = open_access_dirs(script[std::string("access_path")]);

	int rtld_fd = openat(AT_FDCWD, "/libexec/ld-elf.so.1", O_RDONLY);

	caph_cache_catpages();
	caph_cache_tzdata();

	if (cap_enter() != 0) {
		err(-1, "cap_enter: %d %s", errno, strerror(errno));
	}

	int shm_fd = pmap_to_shm_fd(pmap);

	std::string ld_preload = join_ld_preload(script[std::string("ld_preload")]);

	if (setenv("SHARED_MEMORYFD", std::to_string(shm_fd).c_str(), 1) != 0) {
		err(-1, "SHARED_MEMORYFD not set");
	}
	if (setenv("LD_LIBRARY_PATH_FDS", lib_dir_fds.c_str(), 1) != 0) {
		err(-1, "LD_LIBRARY_PATH_FDS not set");
	}
	if (setenv("LD_PRELOAD", ld_preload.c_str(), 1) != 0) {
		err(-1, "LD_PRELOAD not set");
	};

	std::vector<std::string> rtld_argv{
	    bin_path + " [capsicumized]",  // rtld doesn't care, we can show the program name
	    "-f",
	    std::to_string(bin_fd),
	    "--",
	    bin_path,
	};

	for (int i = 2; i < argc; i++) {
		rtld_argv.emplace_back(argv[i]);
	}

	std::vector<char *> rtld_argv_c;
	rtld_argv_c.resize(rtld_argv.size() + 1);
	std::transform(rtld_argv.cbegin(), rtld_argv.cend(), rtld_argv_c.begin(),
	               [](auto x) { return const_cast<char *>(strdup(x.c_str())); });
	rtld_argv_c[rtld_argv.size()] = nullptr;

	fexecve(rtld_fd, rtld_argv_c.data(), environ);
}
