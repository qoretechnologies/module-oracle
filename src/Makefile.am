MNAME=oracle

AUTOMAKE_OPTIONS = foreign subdir-objects

if COND_MINGW
QPP=qpp_native
else
QPP=qpp
endif

.qpp.cpp:
	$(QPP) -V $<

GENERATED_SRC = ql_oracle.cpp QC_AQQueue.cpp QC_AQMessage.cpp
CLEANFILES = $(GENERATED_SRC)

if COND_SINGLE_COMPILATION_UNIT
single-compilation-unit.cpp: $(GENERATED_SRC)
ORACLE_SOURCES = single-compilation-unit.cpp
else
ORACLE_SOURCES = oracle.cpp \
	oracleobject.cpp \
	QoreOracleConnection.cpp \
	QoreOracleBulkLoad.cpp \
	QoreOracleStatement.cpp \
	QorePreparedStatement.cpp \
	OraColumnValue.cpp \
	OraResultSet.cpp \
	ocilib/exception.cpp \
	ocilib/string.cpp \
	ocilib/timestamp.cpp \
	ocilib/collection.cpp \
	ocilib/element.cpp \
	ocilib/library.cpp \
	ocilib/number.cpp \
	ocilib/typeinfo.cpp \
	ocilib/column.cpp \
	ocilib/error.cpp \
	ocilib/list.cpp \
	ocilib/memory.cpp \
	ocilib/lob.cpp \
	ocilib/object.cpp \
	ocilib/interval.cpp \
	ocilib/date.cpp \
	ocilib/connection.cpp \
	ocilib/msg.cpp \
	ocilib/enqueue.cpp \
	ocilib/dequeue.cpp \
	ocilib/agent.cpp \
	ocilib/callback.cpp

nodist_oracle_la_SOURCES = $(GENERATED_SRC)
endif

lib_LTLIBRARIES = oracle.la
oracle_la_SOURCES = $(ORACLE_SOURCES)
oracle_la_LDFLAGS = -module -avoid-version ${ORACLE_LIBS}

INCLUDES = -I$(top_srcdir)/include $(ORACLE_INCLUDES) -I$(top_srcdir)/src/ocilib

AM_CPPFLAGS = ${QORE_CPPFLAGS}

if COND_PROFILE
AM_CXXFLAGS=-g -pg
else
if COND_DEBUG
AM_CXXFLAGS=-g
AM_CFLAGS=-g
endif
endif

if COND_MINGW
RM_OTHER=$(DESTDIR)$(libdir)/$(MNAME).dll.a
endif

# adding the oracle dir to the rpath on Darwin does not seem to work, so we use install_name_tool below

install-exec-hook:
	mv $(DESTDIR)$(libdir)/$(MNAME).$(MODULE_SUFFIX) $(DESTDIR)$(libdir)/$(MNAME)-api-$(MODULE_API).qmod
	$(RM) $(DESTDIR)$(libdir)/$(MNAME).la $(RM_OTHER)
if COND_DARWIN
	install_name_tool -add_rpath ${ORACLE_LIB_DIR} $(DESTDIR)$(libdir)/$(MNAME)-api-$(MODULE_API).qmod
endif
