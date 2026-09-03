# Copyright (c) 2013-2020 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Pattern rule to print variables, e.g. make print-top_srcdir
print-%: FORCE
	@echo '$*'='$($*)'

ACLOCAL_AMFLAGS = -I build-aux/m4
SUBDIRS = src
if ENABLE_MAN
SUBDIRS += doc/man
endif
.PHONY: deploy FORCE
.INTERMEDIATE: $(COVERAGE_INFO)

export PYTHONPATH

if BUILD_BITCOIN_LIBS
pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libbitcoinconsensus.pc
endif

BITCOIND_BIN=$(top_builddir)/src/$(BITCOIN_DAEMON_NAME)$(EXEEXT)
BITCOIN_QT_BIN=$(top_builddir)/src/qt/$(BITCOIN_GUI_NAME)$(EXEEXT)
BITCOIN_TEST_BIN=$(top_builddir)/src/test/$(BITCOIN_TEST_NAME)$(EXEEXT)
BITCOIN_CLI_BIN=$(top_builddir)/src/$(BITCOIN_CLI_NAME)$(EXEEXT)
BITCOIN_TX_BIN=$(top_builddir)/src/$(BITCOIN_TX_NAME)$(EXEEXT)
BITCOIN_UTIL_BIN=$(top_builddir)/src/$(BITCOIN_UTIL_NAME)$(EXEEXT)
BITCOIN_WALLET_BIN=$(top_builddir)/src/$(BITCOIN_WALLET_TOOL_NAME)$(EXEEXT)
BITCOIN_NODE_BIN=$(top_builddir)/src/$(BITCOIN_MP_NODE_NAME)$(EXEEXT)
BITCOIN_GUI_BIN=$(top_builddir)/src/$(BITCOIN_MP_GUI_NAME)$(EXEEXT)
BITCOIN_WIN_INSTALLER=$(PACKAGE)-$(PACKAGE_VERSION)-win64-setup$(EXEEXT)

empty :=
space := $(empty) $(empty)

OSX_APP=TrollCoin-Qt.app
OSX_VOLNAME = $(subst $(space),-,$(PACKAGE_NAME))
OSX_ZIP = $(OSX_VOLNAME).zip
OSX_DEPLOY_SCRIPT=$(top_srcdir)/contrib/macdeploy/macdeployqtplus
OSX_INSTALLER_ICONS=$(top_srcdir)/src/qt/res/icons/bitcoin.icns
OSX_PLIST=$(top_builddir)/share/qt/Info.plist #not installed

# The sanitizer suppressions live in that same untracked tree, so they are not distributed
# either -- for the same reason, and not merely when they happen to be absent.
DIST_CONTRIB = \
	       $(top_srcdir)/contrib/linearize/linearize-data.py \
	       $(top_srcdir)/contrib/linearize/linearize-hashes.py \
	       $(top_srcdir)/contrib/signet/miner

DIST_SHARE = \
  $(top_srcdir)/share/genbuild.sh \
  $(top_srcdir)/share/rpcauth/README.md \
  $(top_srcdir)/share/rpcauth/rpcauth.py

BIN_CHECKS=$(top_srcdir)/contrib/devtools/symbol-check.py \
           $(top_srcdir)/contrib/devtools/security-check.py \
           $(top_srcdir)/contrib/devtools/utils.py

WINDOWS_PACKAGING = $(top_srcdir)/share/pixmaps/bitcoin.ico \
  $(top_srcdir)/share/pixmaps/nsis-header.bmp \
  $(top_srcdir)/share/pixmaps/nsis-wizard.bmp \
  $(top_srcdir)/doc/README_windows.txt

OSX_PACKAGING = $(OSX_DEPLOY_SCRIPT) $(OSX_INSTALLER_ICONS) \
  $(top_srcdir)/contrib/macdeploy/detached-sig-create.sh

COVERAGE_INFO = $(COV_TOOL_WRAPPER) baseline.info \
  test_trollcoin_filtered.info total_coverage.info \
  baseline_filtered.info functional_test.info functional_test_filtered.info \
  test_trollcoin_coverage.info test_trollcoin.info fuzz.info fuzz_filtered.info fuzz_coverage.info

dist-hook:
	-$(GIT) archive --format=tar HEAD -- src/clientversion.cpp | $(AMTAR) -C $(top_distdir) -xf -
# Belt and braces on the same problem. share/rpcauth used to be listed as a DIRECTORY, and
# automake copies those recursively with no ignore filtering -- so `make dist` after
# `make check` packaged the __pycache__ that running share/rpcauth/rpcauth.py leaves behind,
# and any other untracked file dropped in there would travel too. The two files are named
# individually above, which fixes the general case; this hook stays because a Python cache can
# still appear under a directory some other EXTRA_DIST entry names.
	find "$(distdir)" -name __pycache__ -type d -prune -exec rm -rf {} + ; \
	find "$(distdir)" \( -name '*.pyc' -o -name '*.pyo' \) -delete

if TARGET_WINDOWS
$(BITCOIN_WIN_INSTALLER): all-recursive
	$(MKDIR_P) $(top_builddir)/release
	STRIPPROG="$(STRIP)" $(INSTALL_STRIP_PROGRAM) $(BITCOIND_BIN) $(top_builddir)/release
	STRIPPROG="$(STRIP)" $(INSTALL_STRIP_PROGRAM) $(BITCOIN_QT_BIN) $(top_builddir)/release
	STRIPPROG="$(STRIP)" $(INSTALL_STRIP_PROGRAM) $(BITCOIN_TEST_BIN) $(top_builddir)/release
	STRIPPROG="$(STRIP)" $(INSTALL_STRIP_PROGRAM) $(BITCOIN_CLI_BIN) $(top_builddir)/release
	STRIPPROG="$(STRIP)" $(INSTALL_STRIP_PROGRAM) $(BITCOIN_TX_BIN) $(top_builddir)/release
	STRIPPROG="$(STRIP)" $(INSTALL_STRIP_PROGRAM) $(BITCOIN_WALLET_BIN) $(top_builddir)/release
	STRIPPROG="$(STRIP)" $(INSTALL_STRIP_PROGRAM) $(BITCOIN_UTIL_BIN) $(top_builddir)/release
	@test -f $(MAKENSIS) && echo 'OutFile "$@"' | cat $(top_builddir)/share/setup.nsi - | $(MAKENSIS) -V2 - || \
	  echo error: could not build $@
	@echo built $@

deploy: $(BITCOIN_WIN_INSTALLER)
endif

if TARGET_DARWIN
$(OSX_APP)/Contents/PkgInfo:
	$(MKDIR_P) $(@D)
	@echo "APPL????" > $@

$(OSX_APP)/Contents/Resources/empty.lproj:
	$(MKDIR_P) $(@D)
	@touch $@

$(OSX_APP)/Contents/Info.plist: $(OSX_PLIST)
	$(MKDIR_P) $(@D)
	$(INSTALL_DATA) $< $@

$(OSX_APP)/Contents/Resources/bitcoin.icns: $(OSX_INSTALLER_ICONS)
	$(MKDIR_P) $(@D)
	$(INSTALL_DATA) $< $@

$(OSX_APP)/Contents/MacOS/TrollCoin-Qt: all-recursive
	$(MKDIR_P) $(@D)
	STRIPPROG="$(STRIP)" $(INSTALL_STRIP_PROGRAM)  $(BITCOIN_QT_BIN) $@

$(OSX_APP)/Contents/Resources/Base.lproj/InfoPlist.strings:
	$(MKDIR_P) $(@D)
	echo '{	CFBundleDisplayName = "$(PACKAGE_NAME)"; CFBundleName = "$(PACKAGE_NAME)"; }' > $@

OSX_APP_BUILT=$(OSX_APP)/Contents/PkgInfo $(OSX_APP)/Contents/Resources/empty.lproj \
  $(OSX_APP)/Contents/Resources/bitcoin.icns $(OSX_APP)/Contents/Info.plist \
  $(OSX_APP)/Contents/MacOS/TrollCoin-Qt $(OSX_APP)/Contents/Resources/Base.lproj/InfoPlist.strings

osx_volname:
	echo $(OSX_VOLNAME) >$@

if BUILD_DARWIN
$(OSX_ZIP): $(OSX_APP_BUILT) $(OSX_PACKAGING)
	$(PYTHON) $(OSX_DEPLOY_SCRIPT) $(OSX_APP) $(OSX_VOLNAME) -translations-dir=$(QT_TRANSLATION_DIR) -zip

deploydir: $(OSX_ZIP)
else !BUILD_DARWIN
APP_DIST_DIR=$(top_builddir)/dist

$(OSX_ZIP): deploydir
	if [ -n "$(SOURCE_DATE_EPOCH)" ]; then find $(APP_DIST_DIR) -exec touch -d @$(SOURCE_DATE_EPOCH) {} +; fi
	cd $(APP_DIST_DIR) && find . | sort | $(ZIP) -X@ $@

$(APP_DIST_DIR)/$(OSX_APP)/Contents/MacOS/TrollCoin-Qt: $(OSX_APP_BUILT) $(OSX_PACKAGING)
	INSTALL_NAME_TOOL=$(INSTALL_NAME_TOOL) OTOOL=$(OTOOL) STRIP=$(STRIP) $(PYTHON) $(OSX_DEPLOY_SCRIPT) $(OSX_APP) $(OSX_VOLNAME) -translations-dir=$(QT_TRANSLATION_DIR)

deploydir: $(APP_DIST_DIR)/$(OSX_APP)/Contents/MacOS/TrollCoin-Qt
endif !BUILD_DARWIN

deploy: $(OSX_ZIP)
endif

$(BITCOIN_QT_BIN): FORCE
	$(MAKE) -C src qt/$(@F)

$(BITCOIND_BIN): FORCE
	$(MAKE) -C src $(@F)

$(BITCOIN_CLI_BIN): FORCE
	$(MAKE) -C src $(@F)

$(BITCOIN_TX_BIN): FORCE
	$(MAKE) -C src $(@F)

$(BITCOIN_UTIL_BIN): FORCE
	$(MAKE) -C src $(@F)

$(BITCOIN_WALLET_BIN): FORCE
	$(MAKE) -C src $(@F)

$(BITCOIN_NODE_BIN): FORCE
	$(MAKE) -C src $(@F)

$(BITCOIN_GUI_BIN): FORCE
	$(MAKE) -C src $(@F)

if USE_LCOV
LCOV_FILTER_PATTERN = \
	-p "/usr/local/" \
	-p "/usr/include/" \
	-p "/usr/lib/" \
	-p "/usr/lib64/" \
	-p "src/leveldb/" \
	-p "src/crc32c/" \
	-p "src/bench/" \
	-p "src/crypto/ctaes" \
	-p "src/minisketch" \
	-p "src/secp256k1" \
	-p "depends"

DIR_FUZZ_SEED_CORPUS ?= qa-assets/fuzz_seed_corpus

$(COV_TOOL_WRAPPER):
	@echo 'exec $(COV_TOOL) "$$@"' > $(COV_TOOL_WRAPPER)
	@chmod +x $(COV_TOOL_WRAPPER)

baseline.info: $(COV_TOOL_WRAPPER)
	$(LCOV) -c -i -d $(abs_builddir)/src -o $@

baseline_filtered.info: baseline.info
	$(abs_builddir)/contrib/filter-lcov.py $(LCOV_FILTER_PATTERN) $< $@
	$(LCOV) -a $@ $(LCOV_OPTS) -o $@

fuzz.info: baseline_filtered.info
if !HAVE_ROOT_TEST_TREE
	@echo "test/fuzz/test_runner.py is not in this checkout; coverage needs the root test tree." >&2; exit 1
endif
# Same two problems `make check` had: the QA tree was never authenticated before being run, and
# the runner resolves ../config.ini relative to itself, which under an out-of-tree configure is
# the SOURCE tree while configure wrote config.ini into the BUILD tree. Coverage could
# therefore run drifted QA against stale binaries at the same Core SHA.
	@$(MAKE) $(AM_MAKEFLAGS) check-qa-tree
	@$(MKDIR_P) $(top_builddir)/test/fuzz
	@test "$(abs_top_srcdir)" = "$(abs_top_builddir)" || \
	  cp -f $(top_srcdir)/test/fuzz/test_runner.py $(top_builddir)/test/fuzz/
	@TIMEOUT=15 $(top_builddir)/test/fuzz/test_runner.py $(DIR_FUZZ_SEED_CORPUS) -l DEBUG
	$(LCOV) -c $(LCOV_OPTS) -d $(abs_builddir)/src --t fuzz-tests -o $@
	$(LCOV) -z $(LCOV_OPTS) -d $(abs_builddir)/src

fuzz_filtered.info: fuzz.info
	$(abs_builddir)/contrib/filter-lcov.py $(LCOV_FILTER_PATTERN) $< $@
	$(LCOV) -a $@ $(LCOV_OPTS) -o $@

test_trollcoin.info: baseline_filtered.info
	$(MAKE) -C src/ check
	$(LCOV) -c $(LCOV_OPTS) -d $(abs_builddir)/src -t test_trollcoin -o $@
	$(LCOV) -z $(LCOV_OPTS) -d $(abs_builddir)/src

test_trollcoin_filtered.info: test_trollcoin.info
	$(abs_builddir)/contrib/filter-lcov.py $(LCOV_FILTER_PATTERN) $< $@
	$(LCOV) -a $@ $(LCOV_OPTS) -o $@

functional_test.info: test_trollcoin_filtered.info
if !HAVE_ROOT_TEST_TREE
	@echo "test/functional/test_runner.py is not in this checkout; coverage needs the root test tree." >&2; exit 1
endif
	@$(MAKE) $(AM_MAKEFLAGS) check-qa-tree
	@$(MKDIR_P) $(top_builddir)/test/functional
	@test "$(abs_top_srcdir)" = "$(abs_top_builddir)" || \
	  cp -f $(top_srcdir)/test/functional/test_runner.py $(top_builddir)/test/functional/
	@TIMEOUT=15 $(top_builddir)/test/functional/test_runner.py $(EXTENDED_FUNCTIONAL_TESTS)
	$(LCOV) -c $(LCOV_OPTS) -d $(abs_builddir)/src --t functional-tests -o $@
	$(LCOV) -z $(LCOV_OPTS) -d $(abs_builddir)/src

functional_test_filtered.info: functional_test.info
	$(abs_builddir)/contrib/filter-lcov.py $(LCOV_FILTER_PATTERN) $< $@
	$(LCOV) -a $@ $(LCOV_OPTS) -o $@

fuzz_coverage.info: fuzz_filtered.info
	$(LCOV) -a $(LCOV_OPTS) baseline_filtered.info -a fuzz_filtered.info -o $@ | $(GREP) "\%" | $(AWK) '{ print substr($$3,2,50) "/" $$5 }' > coverage_percent.txt

test_trollcoin_coverage.info: baseline_filtered.info test_trollcoin_filtered.info
	$(LCOV) -a $(LCOV_OPTS) baseline_filtered.info -a test_trollcoin_filtered.info -o $@

total_coverage.info: test_trollcoin_filtered.info functional_test_filtered.info
	$(LCOV) -a $(LCOV_OPTS) baseline_filtered.info -a test_trollcoin_filtered.info -a functional_test_filtered.info -o $@ | $(GREP) "\%" | $(AWK) '{ print substr($$3,2,50) "/" $$5 }' > coverage_percent.txt

fuzz.coverage/.dirstamp: fuzz_coverage.info
	$(GENHTML) -s $(LCOV_OPTS) $< -o $(@D)
	@touch $@

test_trollcoin.coverage/.dirstamp:  test_trollcoin_coverage.info
	$(GENHTML) -s $(LCOV_OPTS) $< -o $(@D)
	@touch $@

total.coverage/.dirstamp: total_coverage.info
	$(GENHTML) -s $(LCOV_OPTS) $< -o $(@D)
	@touch $@

cov_fuzz: fuzz.coverage/.dirstamp

cov: test_trollcoin.coverage/.dirstamp total.coverage/.dirstamp

endif

dist_noinst_SCRIPTS = autogen.sh

EXTRA_DIST = $(DIST_SHARE) $(DIST_CONTRIB) $(WINDOWS_PACKAGING) $(OSX_PACKAGING) $(BIN_CHECKS)

# QA-TREE.sha256 IS distributed: it is the commit's statement about which QA tree its
# results refer to, and a tarball that omits it cannot be checked against one.
EXTRA_DIST += QA-TREE.sha256

# Verify the working tree against that pin. Not wired into `check` -- a QA tree can be
# legitimately absent, and this must be a deliberate question, not a surprise failure.
check-qa-tree:
	@if test ! -f "$(top_srcdir)/QA-TREE.sha256"; then \
	  echo "no QA-TREE.sha256 in this checkout"; exit 1; \
	fi; \
	cd "$(top_srcdir)" && \
	if command -v sha256sum >/dev/null 2>&1; then \
	  grep -v '^#' QA-TREE.sha256 | sha256sum -c --quiet - || exit 1; \
	elif command -v shasum >/dev/null 2>&1; then \
	  set -o pipefail 2>/dev/null || true; \
	  grep -v '^#' QA-TREE.sha256 | shasum -a 256 -c - > .qa-out.$$$$ 2>&1; \
	  qa_rc=$$?; \
	  grep -v ': OK$$' .qa-out.$$$$ > .qa-bad.$$$$ || true; \
	  if test "$$qa_rc" != "0" || test -s .qa-bad.$$$$; then \
	    cat .qa-bad.$$$$; rm -f .qa-bad.$$$$ .qa-out.$$$$; \
	    echo "shasum reported a failure (exit $$qa_rc)"; exit 1; \
	  fi; \
	  rm -f .qa-bad.$$$$ .qa-out.$$$$; \
	else \
	  echo "no sha256sum or shasum on this host; cannot verify the QA pin"; exit 1; \
	fi; \
	if find test -name cache -prune -o \
	        \( -name '*.pyc' -o -name '*.pyo' -o -name __pycache__ \) -print | grep -q .; then \
	  echo "QA tree contains compiled Python caches; Python may execute those instead of the"; \
	  echo "pinned sources, so the pin would not describe what runs. Remove them:"; \
	  echo "  find test -name __pycache__ -prune -exec rm -rf {} +"; \
	  exit 1; \
	fi; \
	grep -v '^#' QA-TREE.sha256 | sed 's/^[0-9a-f]*  //' | sort > .qa-pinned.$$$$; \
	while read -r qa_f; do \
	  if test -L "$$qa_f"; then \
	    echo "$$qa_f is a symlink; a manifest-listed path must be a regular file --"; \
	    echo "a symlink hashes as its target while being something else entirely."; \
	    rm -f .qa-pinned.$$$$; exit 1; \
	  fi; \
	done < .qa-pinned.$$$$; \
	find test -name __pycache__ -prune -o -name cache -prune -o \
	     ! -type d ! -name '*.pyc' ! -name '*.pyo' \
	     ! -name config.ini ! -name config.ini.in -print \
	  | sort > .qa-present.$$$$; \
	extra=`comm -13 .qa-pinned.$$$$ .qa-present.$$$$`; \
	rm -f .qa-pinned.$$$$ .qa-present.$$$$; \
	if test -n "$$extra"; then \
	  echo "QA tree has files the pin does not list:"; echo "$$extra" | sed 's/^/  /'; \
	  echo "a QA tree with unpinned files is not the pinned tree"; exit 1; \
	fi; \
	echo "QA tree matches QA-TREE.sha256 exactly (no missing, no extra)"
.PHONY: check-qa-tree

# The root test tree itself is NOT distributed, and its entries have been removed rather than
# made conditional.
#
# It is maintained outside this repository, so the only copy any build could package is
# whatever happens to be lying in the working directory -- 408 untracked files here, 30 of them
# generated .pyc, and a blocktools.py that has drifted from every tracked branch. Distributing
# those recursively meant one commit could produce two different tarballs depending on what was
# on disk, which is exactly what a source distribution must not do.
#
# Guarding them with HAVE_ROOT_TEST_TREE, as an earlier attempt did, fixed the clean-checkout
# failure and kept the ambient-file problem: `make dist` in a development tree still swept in
# untracked, unversioned, partly-generated files. The presence of an ignored path is not a fact
# about the commit and must not decide what a tarball contains.
#
# The QA tree is delivered separately and pinned by QA-TREE.sha256.

CLEANFILES = $(OSX_ZIP) $(BITCOIN_WIN_INSTALLER)

DISTCHECK_CONFIGURE_FLAGS = --enable-man

doc/doxygen/.stamp: doc/Doxyfile FORCE
	$(MKDIR_P) $(@D)
	$(DOXYGEN) $^
	$(AM_V_at) touch $@

if HAVE_DOXYGEN
docs: doc/doxygen/.stamp
else
docs:
	@echo "error: doxygen not found"
endif

clean-docs:
	rm -rf doc/doxygen

clean-local: clean-docs
	rm -rf coverage_percent.txt test_trollcoin.coverage/ total.coverage/ fuzz.coverage/ test/tmp/ cache/ $(OSX_APP)
	rm -rf test/functional/__pycache__ test/functional/test_framework/__pycache__ test/cache share/rpcauth/__pycache__
	rm -rf osx_volname dist/

test-security-check:
if TARGET_DARWIN
	$(AM_V_at) CC='$(CC)' CFLAGS='$(CFLAGS)' CPPFLAGS='$(CPPFLAGS)' LDFLAGS='$(LDFLAGS)' $(PYTHON) $(top_srcdir)/contrib/devtools/test-security-check.py TestSecurityChecks.test_MACHO
	$(AM_V_at) CC='$(CC)' CFLAGS='$(CFLAGS)' CPPFLAGS='$(CPPFLAGS)' LDFLAGS='$(LDFLAGS)' $(PYTHON) $(top_srcdir)/contrib/devtools/test-symbol-check.py TestSymbolChecks.test_MACHO
endif
if TARGET_WINDOWS
	$(AM_V_at) CC='$(CC)' CFLAGS='$(CFLAGS)' CPPFLAGS='$(CPPFLAGS)' LDFLAGS='$(LDFLAGS)' $(PYTHON) $(top_srcdir)/contrib/devtools/test-security-check.py TestSecurityChecks.test_PE
	$(AM_V_at) CC='$(CC)' CFLAGS='$(CFLAGS)' CPPFLAGS='$(CPPFLAGS)' LDFLAGS='$(LDFLAGS)' $(PYTHON) $(top_srcdir)/contrib/devtools/test-symbol-check.py TestSymbolChecks.test_PE
endif
if TARGET_LINUX
	$(AM_V_at) CC='$(CC)' CFLAGS='$(CFLAGS)' CPPFLAGS='$(CPPFLAGS)' LDFLAGS='$(LDFLAGS)' $(PYTHON) $(top_srcdir)/contrib/devtools/test-security-check.py TestSecurityChecks.test_ELF
	$(AM_V_at) CC='$(CC)' CFLAGS='$(CFLAGS)' CPPFLAGS='$(CPPFLAGS)' LDFLAGS='$(LDFLAGS)' $(PYTHON) $(top_srcdir)/contrib/devtools/test-symbol-check.py TestSymbolChecks.test_ELF
endif
