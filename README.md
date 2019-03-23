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

## Troubleshooting

### no matching function for call to `min(float, error)'

If you're getting this kind of errors:

```
Failed to link program: error: linking with uncompiled shader
Failed to compile shader 'Fragment: blur2'. Error: 0:30(62): error: could not implicitly convert operands to arithmetic operator
0:30(12): error: no matching function for call to `min(float, error)'; candidates are:
0:30(12): error:    float min(float, float)
(...)
Failed to link program: error: linking with uncompiled shader
```

Try applying [this patch](https://gist.github.com/deltaoscarmike/5f53db9d6bbfeafad95104a78327fca1) to projectM.

## License

This plugin is released under LGPL.
