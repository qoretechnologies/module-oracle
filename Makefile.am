moddir = $(userlibdir)

AUTOMAKE_OPTIONS = foreign subdir-objects

SUBDIRS = src

noinst_HEADERS = src/oracle-config.h \
	src/oracle.h \
	src/oracleobject.h \
	src/oracle-module.h \
	src/QoreOracleStatement.h \
	src/QorePreparedStatement.h \
	src/QoreOracleConnection.h \
	src/OraColumnValue.h \
	src/OraResultSet.h \
	src/AQMessagePriv.h \
	src/AQQueuePriv.h \
	src/ocilib/oci_api.h \
	src/ocilib/ocilib_checks.h \
	src/ocilib/ocilib_internal.h \
	src/ocilib/oci_types.h \
	src/ocilib/oci_defs.h \
	src/ocilib/ocilib_defs.h \
	src/ocilib/ocilib_types.h \
	src/ocilib/oci_import.h \
	src/ocilib/ocilib.h \
	src/ocilib/oci_loader.h

USER_MODULES = qlib/OracleExtensions.qm

EXTRA_DIST = COPYING.MIT COPYING.LGPL AUTHORS README \
	RELEASE-NOTES \
	src/ql_oracle.qpp \
	src/QC_AQMessage.qpp \
	src/QC_AQQueue.qpp \
	test/db.qtest \
	test/sql-stmt.qtest \
	test/aq-test.q \
	test/sql-stmt-describe.q \
    test/oracle.qtest \
	test/oracle-native-bulk-load.qtest \
	$(USER_MODULES) \
	qore-oracle-module.spec

ACLOCAL_AMFLAGS=-I m4

dist_mod_DATA = $(USER_MODULES)

if COND_DOXYGEN
DOX = oracle OracleExtensions
DOXYGEN_OUTPUT = docs/oracle/html docs/OracleExtensions/html
DOXYFILES = docs/doxyfile docs/mainpage.doxygen
DOXYFILES_SRC = docs/doxyfile.tmpl docs/footer_template.html docs/qlib/Doxyfile.tmpl
QLIB_TMP_DOXYFILES = docs/qlib/OracleExtensions.qm.dox.h docs/qlib/Doxyfile.OracleExtensions
QDX = qdx
DX_CLEANFILES = ${DOXYFILES} $(QLIB_TMP_DOXYFILES)

docs/qlib/Doxyfile.tmp: docs/qlib/Doxyfile.tmpl
	sed s:__USERMODDIR__:${usermoddir}: $< > $@

docs/qlib/Doxyfile.OracleExtensions: docs/qlib/Doxyfile.tmp
	$(QDX) -M=qlib/OracleExtensions.qm:qlib/OracleExtensions.qm.dox.h $< $@

docs/qlib/OracleExtensions.qm.dox.h: qlib/OracleExtensions.qm
	$(QDX) $< $@

docs/OracleExtensions/html: docs/qlib/OracleExtensions.qm.dox.h docs/qlib/Doxyfile.OracleExtensions
	cd docs; $(DOXYGEN_CMD) qlib/Doxyfile.OracleExtensions
	$(QDX) --post docs/OracleExtensions/html docs/OracleExtensions/html/search

docs/oracle/html: $(DOXYFILES)
	cd docs; $(DOXYGEN_CMD) doxyfile;
	$(QDX) --post docs/oracle/html docs/oracle/html/search

docs/mainpage.doxygen: docs/mainpage.doxygen.tmpl
	$(QDX) -d $< $@

docs/doxyfile: docs/doxyfile.tmpl
	sed -e s/__VERSION__/${VERSION}/ -e s:__USERMODDIR__:${usermoddir}: $< > $@

doxygen-doc: $(DOXYGEN_OUTPUT)

EXTRA_DIST += $(DOXYGEN_OUTPUT)

html-local: $(DOXYGEN_OUTPUT)

install-html-local:
	for dox in $(DOX); do $(MKDIR_P) $(DESTDIR)$(datadir)/$(PACKAGE)/docs/$$dox/html/search; done
	for dir in $(DOXYGEN_OUTPUT); do \
	   for f in $$dir/*; do if [ -f $$f ]; then $(INSTALL_DATA) $$f $(DESTDIR)$(datadir)/$(PACKAGE)/$$dir; fi; done; \
	   for f in $$dir/search/*; do $(INSTALL_DATA) $$f $(DESTDIR)$(datadir)/$(PACKAGE)/$$dir/search; done; \
	done

clean-local:
	$(RM) -rf $(DOXYGEN_OUTPUT)

uninstall-local:
	$(RM) -rf $(DESTDIR)$(datadir)/$(PACKAGE)/docs

mostlyclean-local:
	rm -rf ${DOXYGEN_OUTPUT} ${DX_CLEANFILES}

else
doxygen-doc:
	echo no doxygen support
endif

libtool: $(LIBTOOL_DEPS)
	$(SHELL) ./config.status --recheck
