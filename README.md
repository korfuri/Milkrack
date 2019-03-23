# Milkrack

Milkrack brings Winamp's Old Skool Cool visualizations from Milkdrop
into your Rack. Powered by
[ProjectM](https://github.com/projectM-visualizer/projectm).

This is very much a work in progress, and I'm not sure it will ever
reach the state where it can easily be distributed across platforms
etc.

## Building

* Git clone Milkrack under your `plugins` directory
* `git submodule init` and `git submodule update`
* `(cd src/deps/projectm && ./configure --with-pics --enable-gles && make)`
  to build projectM
* `make` Milkrack itself

This plugin is released under LGPL.
