[![unlicense](https://img.shields.io/badge/un-license-green.svg?style=flat)](https://unlicense.org)

# The Super Capsicumizer 9000

`capsicumizer` is a sandbox launcher that imposes [Capsicum] capability mode onto an unsuspecting program, allowing "sysadmin style sandboxing" (no source code modifications, all restrictions added externally).

`capsicumizer` is capable of launching some GUI applications (like gedit) on both Wayland and X11.

[Capsicum]: https://www.freebsd.org/cgi/man.cgi?query=capsicum&sektion=4

## Requirements

- a recent version of FreeBSD
- [Meson] build system
- [libucl]
- [libpreopen] (that linked fork, at least for now)

[libucl]: https://github.com/vstakhov/libucl
[libpreopen]: https://github.com/myfreeweb/libpreopen
[Meson]: https://mesonbuild.com

## Usage

Capsicumizer profiles are written in UCL syntax (which is pretty common on FreeBSD), and can be used as directly runnable scripts (`#!`):

```ucl
#!/usr/bin/env capsicumizer

run = "/usr/local/bin/gedit";

access_path = [
	"/home/greg",
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
```

## Contributing

By participating in this project you agree to follow the [Contributor Code of Conduct](https://contributor-covenant.org/version/1/4/).

[The list of contributors is available on GitHub](https://github.com/myfreeweb/capsicumizer/graphs/contributors).


## License

This is free and unencumbered software released into the public domain.  
For more information, please refer to the `UNLICENSE` file or [unlicense.org](https://unlicense.org).
