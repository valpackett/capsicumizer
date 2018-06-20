[![unlicense](https://img.shields.io/badge/un-license-green.svg?style=flat)](http://unlicense.org)

# The Super Capsicumizer 9000

`libcapsicumizer` is a shared library that lets you use [capsicum(4)](https://www.freebsd.org/cgi/man.cgi?query=capsicum&sektion=4) as a path-whitelist style sandbox.

It doesn't support all the syscalls, but enough to let `gtk3-demo` run, with `cap_enter` *before* initializing GTK!

## Usage

### As a developer

Add `capsicumize_*` calls before `cap_enter`:

```c
#include "capsicumizer.h"

int main(int argc, char **argv) {
	capsicumize_dir("/usr/local/share");
	capsicumize_dir("/usr/local/lib");
	capsicumize_dir("/usr/lib");
	capsicumize_dir("/var/db/fontconfig");
	capsicumize_dir("/var/lib/dbus");
	capsicumize_dir("/tmp");
	capsicumize_shm("gdk-wayland");
	if (cap_enter() != 0) return errno;
  // e.g.
	GtkApplication *app = gtk_application_new("org.gtk.example", G_APPLICATION_FLAGS_NONE);
  // ...
}
```

And link the library **before** anything that would open files (e.g. GTK):

```bash
cc -o demo -L`pwd` -lcapsicumizer -Wl,-rpath,`pwd` `pkg-config --cflags --libs gtk+-3.0` demo.c
```

### As a user (`LD_PRELOAD` into an existing application)

In this mode, you provide environment variables and the library will `cap_enter`.

```bash
LD_PRELOAD=./path/to/libcapsicumizer.so \
CAPSICUMIZE_DIRS=/usr/local/share:/usr/local/lib:/usr/lib:/var/db/fontconfig:/var/lib/dbus:/tmp \
CAPSICUMIZE_SHM_SUBSTRS=gdk-wayland \
gtk3-demo
```

## Contributing

By participating in this project you agree to follow the [Contributor Code of Conduct](https://contributor-covenant.org/version/1/4/).

[The list of contributors is available on GitHub](https://github.com/myfreeweb/capsicumizer/graphs/contributors).


## License

This is free and unencumbered software released into the public domain.  
For more information, please refer to the `UNLICENSE` file or [unlicense.org](http://unlicense.org).
