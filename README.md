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
* `(cd src/deps/projectm && ./autogen.sh && ./configure --with-pic --enable-static --enable-gles --disable-threading && make)`
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

### version '300' is not supported

If you encounter errors such as:

```
Failed to compile shader 'Vertex: v2f_c4f'. Error: ERROR: 0:1: '' :  version '300' is not supported
ERROR: 0:1: '' : syntax error: #version
ERROR: 0:2: 'layout' : syntax error: syntax error
```

or others like those found in issue #2, this is due to a version
incompatibility between OpenGL, OSX, projectM and Rack. It's not clear
whether there's a solution, and we don't have the knowledge or the
resources to fix it (no access to an OSX dev box). If you have a Mac
and you know something about OpenGL, please contribute to issue #2.

### The visualization window is black/shows a scaled down version of my Rack

If your window is just black, make sure you're sending sound into the
module. No signal or non-audio signals may not trigger the preset to
render anything.

If you are actually sending audio into the module, this is a sign that
your shaders didn't compile. Check the standard output of Rack for
errors related to invalid shaders and refer to the other
troubleshooting options in this README to find matching errors.

Check that you configured the projectM build with `--enable-gles`.

### The visualization only shows some floating W letters with headphones

This happens if Milkrack didn't find any Milkdrop presets to load. If you built the plugin yourself make sure that the `presets` folder got included in the zip file.

## License

This plugin is released under LGPL.
