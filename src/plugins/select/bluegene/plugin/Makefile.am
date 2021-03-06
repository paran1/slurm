# Makefile for select/bluegene plugin

CPPFLAGS = -DBLUEGENE_CONFIG_FILE=\"$(sysconfdir)/bluegene.conf\"
AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic -lm

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common $(BG_INCLUDES)

if BLUEGENE_LOADED

if BGL_LOADED
pkglib_LTLIBRARIES = select_bluegene.la libsched_if64.la
else
pkglib_LTLIBRARIES = select_bluegene.la libsched_if.la
endif

# Blue Gene node selection plugin.
select_bluegene_la_SOURCES = select_bluegene.c \
				bg_boot_time.h \
				bg_job_place.c bg_job_place.h \
				bg_job_run.c bg_job_run.h \
				bg_block_info.c bg_block_info.h \
				bg_record_functions.c bg_record_functions.h \
				bluegene.c bluegene.h \
				state_test.c state_test.h \
				bg_switch_connections.c \
				block_sys.c \
				dynamic_block.c dynamic_block.h \
				defined_block.c defined_block.h \
				jobinfo.c jobinfo.h\
				nodeinfo.c nodeinfo.h\
				../wrap_rm_api.h

select_bluegene_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

select_bluegene_la_LIBADD  = ../block_allocator/libbluegene_block_allocator.la


# MPIRUN dynamic lib.
if BGL_LOADED
libsched_if64_la_SOURCES = libsched_if64.c
libsched_if64_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
else
libsched_if_la_SOURCES = libsched_if64.c
libsched_if_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
endif

sbin_PROGRAMS = slurm_prolog slurm_epilog sfree

convenience_libs = \
	$(top_builddir)/src/api/libslurm.o -ldl

sfree_LDADD = $(convenience_libs)
slurm_prolog_LDADD = $(convenience_libs)
slurm_epilog_LDADD = $(convenience_libs)
sfree_SOURCES = sfree.c sfree.h opts.c \
		../block_allocator/bridge_linker.c \
		../block_allocator/bridge_linker.h
slurm_prolog_SOURCES = slurm_prolog.c
slurm_epilog_SOURCES = slurm_epilog.c
sfree_LDFLAGS = -export-dynamic -lm $(CMD_LDFLAGS)
slurm_prolog_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)
slurm_epilog_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

force:
$(select_bluegene_la_LIBADD) $(sfree_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

else
# These are needed for pack/unpack of structures for cross-cluster stuff

pkglib_LTLIBRARIES = select_bluegene.la

select_bluegene_la_SOURCES = select_bluegene.c \
			jobinfo.c jobinfo.h\
			nodeinfo.c nodeinfo.h\
			../wrap_rm_api.h
select_bluegene_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)


endif
