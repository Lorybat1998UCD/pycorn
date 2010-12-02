$(phony all): $(TARGET).flash

include common.mk

ifndef USE_BINARIES
$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $(input) $(output)

$(TARGET).elf: $(OBJECTS) $(LDDEPS)
	$(CC) $(OBJECTS) $(LDFLAGS) $(SYSLIBS) -o $(output)
endif