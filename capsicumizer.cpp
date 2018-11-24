#include <ucl++.h>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

extern "C" {
#include <capsicum_helpers.h>
#include <err.h>
#include <libpreopen.h>
#include <sys/capsicum.h>
#include <unistd.h>

extern char **environ;
}

using ucl::Ucl;

int main(int argc, char *argv[]) {
	if (argc < 2) {
		std::cerr << "usage: " << argv[0] << " script args.." << std::endl;
		return -1;
	}

	std::string uclerr;
	std::ifstream script_stream(argv[1]);
	Ucl script = Ucl::parse(script_stream, uclerr);
	script_stream.close();
	if (!uclerr.empty()) {
		std::cerr << uclerr << std::endl;
		return -1;
	}

	auto bin_path = script[std::string("run")].string_value();
	int bin_fd = openat(AT_FDCWD, bin_path.c_str(), O_RDONLY);

	std::string lib_dir_fds;
	for (const auto &val : script[std::string("library_path")]) {
		// TODO check existence
		int fd = openat(AT_FDCWD, val.string_value().c_str(), O_DIRECTORY | O_RDONLY);
		lib_dir_fds += std::to_string(fd);
		lib_dir_fds += ":";
	}
	lib_dir_fds.pop_back();

	std::vector<std::string> access_path;
	for (const auto &val : script[std::string("access_path")]) {
		access_path.push_back(val.string_value());
	}
	struct po_map *pmap = po_map_create(access_path.size());
	for (const auto &dir : access_path) {
		po_preopen(pmap, dir.c_str(), O_DIRECTORY);
	}

	caph_cache_catpages();
	caph_cache_tzdata();

	int rtld_fd = openat(AT_FDCWD, "/libexec/ld-elf.so.1", O_RDONLY);

	if (cap_enter() != 0) {
		err(-1, "cap_enter: %d %s", errno, strerror(errno));
	}

	int shm_fd = po_pack(pmap);
	assert(shm_fd != -1);
	fcntl(shm_fd, F_SETFD, 0);  // un-CLOEXEC
	if (setenv("SHARED_MEMORYFD", std::to_string(shm_fd).c_str(), 1) != 0) {
		err(-1, "SHARED_MEMORYFD not set");
	}

	if (setenv("LD_LIBRARY_PATH_FDS", lib_dir_fds.c_str(), 1) != 0) {
		err(-1, "LD_LIBRARY_PATH_FDS not set");
	}

	std::string ld_preload = "libpreopen.so";
	for (const auto &val : script[std::string("ld_preload")]) {
		ld_preload += ":";
		ld_preload += val.string_value();
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
