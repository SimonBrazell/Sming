DISABLE_NETWORK := 1
HWCONFIG := spiffs-2m

CUSTOM_TARGETS	+= files/README.md

# Large text file for demo purposes
files/README.md: $(SMING_HOME)/../README.md
	$(Q) mkdir -p $(@D)
	$(Q) cp $< $@

	
# Emulate both serial ports
ENABLE_HOST_UARTID := 0 1
HOST_NETWORK_OPTIONS := --pause
