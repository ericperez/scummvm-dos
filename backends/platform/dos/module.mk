MODULE := backends/platform/dos

MODULE_OBJS := \
	dos.o \
	dos-files.o \
	dos-graphics.o \
	dos-input.o \
	dos-keyboard.o \
	dos-mixer.o \
	dos-sb.o \
	dos-vesa.o

# We don't use rules.mk but rather manually update OBJS and MODULE_DIRS.
MODULE_OBJS := $(addprefix $(MODULE)/, $(MODULE_OBJS))
OBJS := $(MODULE_OBJS) $(OBJS)
MODULE_DIRS += $(sort $(dir $(MODULE_OBJS)))
