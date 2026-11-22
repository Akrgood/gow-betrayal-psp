TARGET = gow_betrayal
CORE = ../src
THIRD = ../third_party
PYTHON ?= python3
VPATH = $(CORE):$(CORE)/gen:$(THIRD)/stb
OBJS = main_psp.o intro_psp.o audio_psp.o locale_psp.o engine.o scene.o gfx.o jrt.o platform.o \
       GamePsp.o $(filter-out Game.o,$(notdir $(patsubst %.cpp,%.o,$(wildcard $(CORE)/gen/*.cpp)))) \
       stb_image_impl.o
INCDIR = $(CORE) $(THIRD)/stb
CXXFLAGS = -std=gnu++17 -O2 -g -G0 -DNDEBUG -fwrapv -fno-strict-aliasing -MMD -MP
CFLAGS = -O2 -G0
LIBS = -lpspaudio -lpsppower -lpspdmac -lz -lstdc++ -lm
BUILD_PRX = 1
PSP_LARGE_MEMORY = 0
EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = God of War: Betrayal

# Optional XMB menu artwork. Place icon0.png, ICON1.PMF, pic1.png and
# SND0.at3 in ../artwork/ to embed them; absent files are simply skipped.
ART_DIR ?= ../artwork
ART_FILES := $(wildcard $(ART_DIR)/icon0.png $(ART_DIR)/ICON1.PMF $(ART_DIR)/pic1.png $(ART_DIR)/SND0.at3)
ifneq ($(filter %/icon0.png,$(ART_FILES)),)
PSP_EBOOT_ICON = "$(ART_DIR)/icon0.png"
endif
ifneq ($(filter %/ICON1.PMF,$(ART_FILES)),)
PSP_EBOOT_ICON1 = "$(ART_DIR)/ICON1.PMF"
endif
ifneq ($(filter %/pic1.png,$(ART_FILES)),)
PSP_EBOOT_PIC1 = "$(ART_DIR)/pic1.png"
endif
ifneq ($(filter %/SND0.at3,$(ART_FILES)),)
PSP_EBOOT_SND0 = "$(ART_DIR)/SND0.at3"
endif

PSPSDK := $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak

$(PSP_EBOOT): $(ART_FILES) Makefile

GamePsp.cpp: $(CORE)/gen/Game.cpp ../tools/gow_psp_patches.py
	$(PYTHON) ../tools/gow_psp_patches.py $< $@

locale_psp.o: locale_strings.inc
locale_strings.inc: locales.tsv controls_psp.tsv gen_locales.py
	$(PYTHON) gen_locales.py locales.tsv controls_psp.tsv $@

-include $(OBJS:.o=.d)
