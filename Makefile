.SUFFIXES:
#---------------------------------------------------------------------------------
ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM)
endif
ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>devkitPro)
endif

include $(DEVKITARM)/base_tools

TARGET      = bios_linker.gba bios_dumper.gba
SRC_LINKER  = src/bios-link.cpp \
              src/Sha256.cpp \
              $(BUILD)/bios_dumper.gba.s
SRC_DUMPER  = src/bios-dump.c

.PHONY: all
all: $(TARGET)

BUILD       = build
GAME_CODE   = 0000
MAKER_CODE  = 00

ARCH        = -marm
LIBS        = -lgba
LIBDIRS     = $(DEVKITPRO)/libgba

DEFINES     = -DBIOS_WRITE_SRAM -DBIOS_CALC_SHA256
INCLUDES    = -iquote $(BUILD) \
              -iquote include \
              -iquote gba-link-connection/lib \
              $(LIBDIRS:%=-isystem %/include)
CFLAGS      = -g -Wall -O3 \
              $(ARCH) -mcpu=arm7tdmi -mtune=arm7tdmi \
              -fomit-frame-pointer \
              -ffast-math \
              $(DEFINES) \
              $(INCLUDES)

LD          = $(CXX)
LDFLAGS     = -g $(ARCH) -Wl,-Map,$@.map \
              $(LIBDIRS:%=-L%/lib) $(LIBS)

OBJ_LINKER  = $(SRC_LINKER:%=$(BUILD)/%.o)
OBJ_DUMPER  = $(SRC_DUMPER:%=$(BUILD)/%.o)
OBJ         = $(OBJ_LINKER) $(OBJ_DUMPER)

build/src/bios-link.cpp.o: build/bios_dumper.gba.h

.PHONY: clean
clean:
	rm -fr $(BUILD) $(TARGET)

%.gba: $(BUILD)/%.elf
	$(OBJCOPY) -O binary $< $(BUILD)/$*.gba
	gbafix $(BUILD)/$*.gba -t$* -c$(GAME_CODE) -m$(MAKER_CODE)
	mv $(BUILD)/$*.gba .

$(BUILD)/bios_linker.elf: $(OBJ_LINKER)
	$(LD) -specs=gba.specs $+ $(LDFLAGS) -o $@

$(BUILD)/bios_dumper.elf: $(OBJ_DUMPER)
	$(LD) -specs=gba_mb.specs $+ $(LDFLAGS) -o $@

$(BUILD)/%.c.o: %.c
	@mkdir -p $(dir $@)
	$(CC) -MMD -MP -MF $(BUILD)/$*.d $(CFLAGS) -c $< -o $@ $(ERROR_FILTER)

$(BUILD)/%.s.o: %.s
	@mkdir -p $(dir $@)
	$(CC) -MMD -MP -MF $(BUILD)/$*.d -c $< -o $@ $(ERROR_FILTER)

$(BUILD)/%.cpp.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) -MMD -MP -MF $(BUILD)/$*.d $(CFLAGS) -c $< -o $@ $(ERROR_FILTER)

$(BUILD)/%.s $(BUILD)/%.h: %
	bin2s -a 4 -H $(BUILD)/$*.h $< > $(BUILD)/$*.s

%.s %.h: %.grit
	grit -ff $< -fts -o$*

.SECONDARY:

-include $(OBJ:.o=.d)
