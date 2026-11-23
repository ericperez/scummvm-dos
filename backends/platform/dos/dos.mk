# DOS port make rules, used in place of ports.mk for plug-in builds (see PLUGINS.md)

ifdef DYNAMIC_MODULES

# The program's export table (the symbols the plug-ins use), made from the finished plug-ins, so the program
# is linked after them. dxe-exports.sh keeps an unchanged table as it is; the stamp records that it ran.
backends/platform/dos/dos-exports.stamp: $(PLUGINS) $(srcdir)/backends/platform/dos/dxe-exports.sh
	$(QUIET)$(MKDIR) backends/platform/dos
	+$(QUIET_PLUGIN)sh $(srcdir)/backends/platform/dos/dxe-exports.sh backends/platform/dos/dos-exports.c $(PLUGINS)
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
