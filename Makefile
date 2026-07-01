# If RACK_DIR is not defined when calling the Makefile, default to two directories above
RACK_DIR ?= ../..

# corrupter DSP submodule
FLAGS += -Ivendor/corrupter-dsp/include -Ivendor/corrupter-dsp/src

# Corrupter DSP sources
SOURCES += vendor/corrupter-dsp/src/engine.cpp
SOURCES += vendor/corrupter-dsp/src/c_api.cpp
SOURCES += vendor/corrupter-dsp/src/pitch_quantizer.cpp
SOURCES += vendor/corrupter-dsp/src/internal/clock_engine.cpp
SOURCES += vendor/corrupter-dsp/src/internal/corrupt_engine.cpp

# Plugin sources
SOURCES += $(wildcard src/*.cpp)

# Add files to the ZIP package when running `make dist`
DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)
DISTRIBUTABLES += $(wildcard presets)

# Include the Rack plugin Makefile framework
include $(RACK_DIR)/plugin.mk
