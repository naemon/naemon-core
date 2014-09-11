dist_check_SCRIPTS = 705nagiostats.t 900-configparsing.t 910-noservice.t 920-nocontactgroup.t 930-emptygroups.t
TESTS = $(dist_check_SCRIPTS)
EXTRA_DIST = $(TESTS) etc/* var/*
TESTS_ENVIRONMENT = \
					   if test "${abs_srcdir}" != "${abs_builddir}"; then \
					   cp -R ${abs_srcdir}/etc "${abs_builddir}"; \
					   cp -R ${abs_srcdir}/var "${abs_builddir}"; \
					   chmod -R u+w ${abs_builddir}/etc ${abs_builddir}/var; \
					   fi; \
					   builddir=$(abs_builddir); export builddir;

TEST_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
	$(top_srcdir)/build-aux/tap-driver.sh

distclean-local:
	if test "${abs_srcdir}" != "${abs_builddir}"; then \
		rm -r ${abs_builddir}/etc ${abs_builddir}/var; \
	fi;
