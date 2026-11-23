MODULE := backends/platform/dos

MODULE_OBJS := \
	dos.o \
	dos-cdaudio.o \
	dos-files.o \
	dos-graphics.o \
	dos-input.o \
	dos-keyboard.o \
	dos-mixer.o \
	dos-opl.o \
	dos-sb.o \
	dos-vesa.o

# The local web server's sockets, over the Watt-32 TCP/IP library
ifdef USE_SDL_NET
MODULE_OBJS += dos-net.o

# Watt-32's headers (socket.h, netdb.h, ...), ahead of the compiler's own, for this file only
backends/platform/dos/dos-net.o: CXXFLAGS += $(WATT32_CFLAGS)

# The address comes from the stack (SDLNet_ResolveIP() in dos-net.cpp), not POSIX ioctls DJGPP lacks
backends/networking/sdl_net/localwebserver.o: CPPFLAGS += -UPOSIX
endif

# We don't use rules.mk but rather manually update OBJS and MODULE_DIRS.
MODULE_OBJS := $(addprefix $(MODULE)/, $(MODULE_OBJS))
OBJS := $(MODULE_OBJS) $(OBJS)
MODULE_DIRS += $(sort $(dir $(MODULE_OBJS)))
