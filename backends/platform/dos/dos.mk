# DOS port make rules, used in place of ports.mk for plug-in builds (see PLUGINS.md)

ifdef DYNAMIC_MODULES

# Tracker music: libopenmpt is not linked into the program but into OPENMPT.DXE, which the program loads while a
# game of an engine with tracker music runs (dos-openmpt.cpp). Its other needs (libstdc++, zlib) come from the
# program, through the export table.
ifneq ($(filter -lopenmpt,$(LIBS)),)
DOS_OPENMPT_DXE := backends/platform/dos/openmpt.dxe
DOS_OPENMPT_LIBS := $(sort $(filter -L%,$(LIBS))) -lopenmpt
LIBS := $(filter-out -lopenmpt,$(LIBS))

$(DOS_OPENMPT_DXE): backends/platform/dos/dos-openmpt-dxe.o $(srcdir)/backends/platform/dos/dxe-link.sh
	$(QUIET_PLUGIN)sh $(srcdir)/backends/platform/dos/dxe-link.sh -E _dosOpenMPT $< $(DOS_OPENMPT_LIBS) -o $@

clean-dos-openmpt:
	-$(RM) $(DOS_OPENMPT_DXE) backends/platform/dos/dos-openmpt-dxe.o
clean: clean-dos-openmpt
endif

# The program's export table (the symbols the plug-ins use), made from the finished plug-ins, so the program
# is linked after them. dxe-exports.sh keeps an unchanged table as it is; the stamp records that it ran.
backends/platform/dos/dos-exports.stamp: $(PLUGINS) $(DOS_OPENMPT_DXE) $(srcdir)/backends/platform/dos/dxe-exports.sh
	$(QUIET)$(MKDIR) backends/platform/dos
	+$(QUIET_PLUGIN)sh $(srcdir)/backends/platform/dos/dxe-exports.sh backends/platform/dos/dos-exports.c $(PLUGINS) $(DOS_OPENMPT_DXE)
	$(QUIET)touch $@

backends/platform/dos/dos-exports.c: backends/platform/dos/dos-exports.stamp
	@if ! test -f $@; then \
		rm -f backends/platform/dos/dos-exports.stamp; \
		$(MAKE) backends/platform/dos/dos-exports.stamp; \
	fi

# C, not C++ (sys/dxe.h's macros), with the cross C compiler named after the C++ one (there is no CC)
backends/platform/dos/dos-exports.o: backends/platform/dos/dos-exports.c
	+$(QUIET_CC)$(CXX:g++=gcc) -O1 -c $< -o $@

clean-dos-exports:
	-$(RM) backends/platform/dos/dos-exports.c backends/platform/dos/dos-exports.o backends/platform/dos/dos-exports.stamp
clean: clean-dos-exports

endif
