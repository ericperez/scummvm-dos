MODULE := backends/platform/dos

ifdef DOS_SDL
# --backend=sdl: SDL3 does video, input and PCM output; the rest comes from here (see dos-sdl.h)
MODULE_OBJS := \
	dos-sdl.o \
	dos-sdl-main.o \
	dos-cdaudio.o \
	dos-files.o \
	dos-midi.o \
	dos-opl.o \
	dos-vesa.o

# A last -lm, so the float functions all come from libm, not libstdc++'s stubs (multiple definition of tanf)
LIBS += -lm
else
MODULE_OBJS := \
	dos.o \
	dos-cdaudio.o \
	dos-files.o \
	dos-graphics.o \
	dos-input.o \
	dos-keyboard.o \
	dos-midi.o \
	dos-mixer.o \
	dos-opl.o \
	dos-sb.o \
	dos-vesa.o
endif

# Plug-ins: the export table made by dos.mk, linked ahead of the libraries so what it names is taken from them
ifdef DYNAMIC_MODULES
MODULE_OBJS += dos-exports.o
endif

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
