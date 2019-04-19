# Milkrack

Milkrack brings Winamp's Old Skool Cool visualizations from Milkdrop
into your Rack. Powered by
[ProjectM](https://github.com/projectM-visualizer/projectm).

## Building

* Install the GLM lib and OpenGL ES development headers, as well as autotools.
  * `apt install autoconf libtool libglm-dev libgles2-mesa-dev libgl1-mesa-dev` on Debian systems
  * `brew install glm automate libtool pkg-config` on OSX
  * If you figure out how to build this on Windows, please let me know! I don't have access to a Windows machine myself.
* Git clone Milkrack under your `plugins` directory
* `git submodule init` and `git submodule update`
* `(cd src/deps/projectm && git apply ../projectm*.diff)`
* `(cd src/deps/projectm && ./autogen.sh && ./configure --with-pic --enable-static --enable-gles && make)`
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

Try applying the patch under `src/deps` to projectM. Just `cd src/deps/projectm` and `git apply ../projectm*.diff`.

### The visualization window is black/shows a scaled down version of my Rack

Your shaders didn't compile. Check the standard output of Rack for errors related to invalid shaders.

Check that you configured the projectM build with `--enable-gles`.

### The visualization only shows some floating W letters with headphones

This happens if Milkrack didn't find any Milkdrop presets to load. If you built the plugin yourself make sure that the `presets` folder got included in the zip file.

## License

This plugin is released under LGPL.
