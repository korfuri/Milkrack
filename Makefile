# If RACK_DIR is not defined when calling the Makefile, default to two directories above
RACK_DIR ?= ../..

# Must follow the format in the Naming section of
# https://vcvrack.com/manual/PluginDevelopmentTutorial.html
SLUG = Milkrack

# Must follow the format in the Versioning section of
# https://vcvrack.com/manual/PluginDevelopmentTutorial.html
VERSION = 0.6.1

# Platform detection
include $(RACK_DIR)/arch.mk

# FLAGS will be passed to both the C and C++ compiler
FLAGS +=
CFLAGS +=
CXXFLAGS +=

# Careful about linking to shared libraries, since you can't assume much about the user's environment and library search path.
# Static libraries are fine.
LDFLAGS += -fPIC
ifdef ARCH_WIN
	LDFLAGS += -shared -Wl,--export-all-symbols -lopengl32
endif

ifdef ARCH_WIN
	LIBPROJECTM = libs/win/libprojectM/libprojectM.a
	LDFLAGS += -L$(CURDIR)/libs/win/libprojectM -lprojectM
else
	LIBPROJECTM = src/deps/projectm/src/libprojectM/.libs/libprojectM.a
	OBJECTS += $(LIBPROJECTM)
endif

# Add .cpp and .c files to the build
SOURCES += $(wildcard src/*.cpp)

# Add files to the ZIP package when running `make dist`
# The compiled plugin is automatically added.
DISTRIBUTABLES += $(wildcard LICENSE*) res src/deps/projectm/presets/presets_projectM

# Include the VCV Rack plugin Makefile framework
include $(RACK_DIR)/plugin.mk

dep: $(LIBPROJECTM)

libs/win/libprojectM/libprojectM.a: libs/win/libprojectM/libprojectM.lib
	choco upgrade mingw
	(cd libs/win/libprojectM; gendef libprojectM.lib)
	(cd libs/win/libprojectM; dlltool -D libprojectM.lib -d libprojectM.def -l libprojectM.a)

src/deps/projectm/src/libprojectM/.libs/libprojectM.a:
	(cd src/deps/projectm; git apply ../projectm_*.diff || true)
	(cd src/deps/projectm; ./autogen.sh)
	(cd src/deps/projectm; export CFLAGS=-I$(shell pwd)/src/deps/glm CXXFLAGS=-I$(shell pwd)/src/deps/glm ; ./configure --with-pic --enable-static --disable-threading)
	(cd src/deps/projectm; export CFLAGS=-I$(shell pwd)/src/deps/glm CXXFLAGS=-I$(shell pwd)/src/deps/glm ; make)

depclean:
	(cd src/deps/projectm; make clean)
	(cd src/deps/projectm; git checkout -- .)
