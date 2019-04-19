# If RACK_DIR is not defined when calling the Makefile, default to two directories above
RACK_DIR ?= ../..

# Must follow the format in the Naming section of
# https://vcvrack.com/manual/PluginDevelopmentTutorial.html
SLUG = Milkrack

# Must follow the format in the Versioning section of
# https://vcvrack.com/manual/PluginDevelopmentTutorial.html
VERSION = 0.6.0

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
	OBJECTS += libs/win/libprojectM/libprojectM.a
else
	OBJECTS += src/deps/projectm/src/libprojectM/.libs/libprojectM.a
endif

# Add .cpp and .c files to the build
SOURCES += $(wildcard src/*.cpp)

# Add files to the ZIP package when running `make dist`
# The compiled plugin is automatically added.
DISTRIBUTABLES += $(wildcard LICENSE*) res src/deps/projectm/presets

# Include the VCV Rack plugin Makefile framework
include $(RACK_DIR)/plugin.mk
