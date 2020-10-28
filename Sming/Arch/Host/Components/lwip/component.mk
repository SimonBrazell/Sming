# Uses cmake to build
CUSTOM_BUILD := 1

# => LWIP
ifndef MAKE_CLEAN
ifndef ENABLE_CUSTOM_LWIP
ENABLE_CUSTOM_LWIP		:= 2
else ifneq ($(ENABLE_CUSTOM_LWIP), 2)
$(error Host only supports LWIP version 2)
endif
endif

COMPONENT_SUBMODULES	:= lwip
COMPONENT_INCDIRS		:= . lwip/src/include
COMPONENT_VARS			+= ENABLE_LWIPDEBUG
ENABLE_LWIPDEBUG		?= 0
ifeq ($(ENABLE_LWIPDEBUG), 1)
	CMAKE_OPTIONS		:= -DCMAKE_BUILD_TYPE=Debug
else
	CMAKE_OPTIONS		:= -DCMAKE_BUILD_TYPE=Release
endif

ifeq ($(SMING_RELEASE),1)
	CMAKE_OPTIONS		+= -DLWIP_NOASSERT=1
endif

ifeq ($(UNAME),Windows)
	COMPONENT_INCDIRS	+= lwip/contrib/ports/win32/include
	CMAKE_OPTIONS		+= -G "MSYS Makefiles"
else
	COMPONENT_INCDIRS	+= lwip/contrib/ports/unix/port/include
endif

CMAKE_OPTIONS			+= -DLWIP_LIBNAME=$(COMPONENT_VARIANT)

$(COMPONENT_RULE)$(COMPONENT_LIBPATH):
	$(Q) $(CMAKE) -DUSER_LIBDIR=$(COMPONENT_LIBDIR) $(CMAKE_OPTIONS) $(COMPONENT_PATH)/$(UNAME)
	$(Q) $(MAKE)
