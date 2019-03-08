[![unlicense](https://img.shields.io/badge/un-license-green.svg?style=flat)](https://unlicense.org)

# The Super Capsicumizer 9000

`capsicumizer` is a sandbox launcher that imposes [Capsicum] capability mode onto an unsuspecting program, allowing "sysadmin style" or "[oblivious]" sandboxing (i.e. no source code modifications, all restrictions added externally).

You just write AppArmor-esque "profiles" and `capsicumizer` takes care of sandboxing the applications.

`capsicumizer` is capable of launching some GUI applications (like gedit) on both Wayland and X11.

(GTK 3.24 required because of [this patch](https://gitlab.gnome.org/GNOME/gtk/merge_requests/203))

Note: applications that use syscalls directly (instead of going through `libc`) — namely, anything compiled with Golang's official compiler — won't be able to actually access files under the allowed paths.
This is because we rely on `LD_PRELOAD`ing a library ([libpreopen]) that overrides libc functions like `open` to make them use `openat` style functions with pre-opened directory descriptors.

[Capsicum]: https://www.freebsd.org/cgi/man.cgi?query=capsicum&sektion=4
[oblivious]: https://www.youtube.com/watch?v=ErXtGMmRzJs

[![gedit demo: refusing to open /etc](https://unrelentingtech.s3.dualstack.eu-west-1.amazonaws.com/gedit-sandbox-etc-fs8.png)](https://unrelenting.technology/notes/2018-11-24-21-43-46)

## Requirements

- a recent version of FreeBSD
- [Meson] build system
- [libucl] >=0.8.1 (pkg: #[233383](https://bugs.freebsd.org/bugzilla/show_bug.cgi?id=233383))
- [libpreopen] (that linked fork, at least for now) (pkg: someday)

[libucl]: https://github.com/vstakhov/libucl
[libpreopen]: https://github.com/myfreeweb/libpreopen
[Meson]: https://mesonbuild.com

## Usage

Capsicumizer profiles are written in UCL syntax (which is pretty common on FreeBSD), and can be used as directly runnable scripts (`#!`):

```conf
#!/usr/bin/env capsicumizer

run = "/usr/local/bin/gedit";

access_path = [
	"$HOME",
	"/usr/local",
	"/var/db/fontconfig",
	"/tmp",
];

library_path = [
	"/lib",
	"/usr/lib",
	"/usr/local/lib",
	"/usr/local/lib/gvfs",
	"/usr/local/lib/gio/modules",
	"/usr/local/lib/gedit",
];

# gedit does not need any extra preloads
# this is just an example
ld_preload = [
	"libgobject-2.0.so"
];
```

Environment variables and program arguments ($0 etc.) are exposed as UCL variables.

## Contributing

By participating in this project you agree to follow the [Contributor Code of Conduct](https://contributor-covenant.org/version/1/4/).

[The list of contributors is available on GitHub](https://github.com/myfreeweb/capsicumizer/graphs/contributors).


## License

This is free and unencumbered software released into the public domain.  
For more information, please refer to the `UNLICENSE` file or [unlicense.org](https://unlicense.org).
