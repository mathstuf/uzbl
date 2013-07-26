# Create a local.mk file to store default local settings to override the
# defaults below.
include $(wildcard local.mk)

# packagers, set DESTDIR to your "package directory" and PREFIX to the prefix you want to have on the end-user system
# end-users who build from source: don't care about DESTDIR, update PREFIX if you want to
# RUN_PREFIX : what the prefix is when the software is run. usually the same as PREFIX
PREFIX     ?= /usr/local
INSTALLDIR ?= $(DESTDIR)$(PREFIX)
MANDIR     ?= $(INSTALLDIR)/share/man
DOCDIR     ?= $(INSTALLDIR)/share/uzbl/docs
RUN_PREFIX ?= $(PREFIX)
INSTALL    ?= install -p

ENABLE_WEBKIT2 ?= auto
ENABLE_GTK3    ?= auto

ENABLE_CUSTOM_SCHEME ?= yes

PYTHON   = python3
PYTHONV  = $(shell $(PYTHON) --version | sed -n /[0-9].[0-9]/p)
COVERAGE = $(shell which coverage)

# --- configuration ends here ---

ifeq ($(ENABLE_WEBKIT2),auto)
ENABLE_WEBKIT2 := $(shell pkg-config --exists webkit2gtk-3.0 && echo yes)
endif

ifeq ($(ENABLE_GTK3),auto)
ENABLE_GTK3 := $(shell pkg-config --exists gtk+-3.0 && echo yes)
endif

ifeq ($(ENABLE_WEBKIT2),yes)
REQ_PKGS += 'webkit2gtk-3.0 >= 1.2.4' javascriptcoregtk-3.0
CPPFLAGS += -DUSE_WEBKIT2
# WebKit2 requires GTK3
ENABLE_GTK3 := yes
else
ifeq ($(ENABLE_GTK3),yes)
REQ_PKGS += 'webkitgtk-3.0 >= 1.2.4' javascriptcoregtk-3.0
else
REQ_PKGS += 'webkit-1.0 >= 1.2.4' javascriptcoregtk-1.0
endif
endif

ifeq ($(ENABLE_GTK3),yes)
REQ_PKGS += gtk+-3.0
CPPFLAGS += -DG_DISABLE_DEPRECATED -DGTK_DISABLE_DEPRECATED
else
REQ_PKGS += gtk+-2.0
endif

REQ_PKGS += 'libsoup-2.4 >= 2.33.4' gthread-2.0 glib-2.0

ARCH := $(shell uname -m)

COMMIT_HASH := $(shell ./misc/hash.sh)

CPPFLAGS += -D_BSD_SOURCE -D_POSIX_SOURCE -DARCH=\"$(ARCH)\" -DCOMMIT=\"$(COMMIT_HASH)\"

HAVE_LIBSOUP_VERSION := $(shell pkg-config --exists 'libsoup-2.4 >= 2.41.1' && echo yes)
ifeq ($(HAVE_LIBSOUP_VERSION),yes)
CPPFLAGS += -DHAVE_LIBSOUP_CHECK_VERSION
endif

PKG_CFLAGS := $(shell pkg-config --cflags $(REQ_PKGS))

LDLIBS := $(shell pkg-config --libs $(REQ_PKGS) x11)

CFLAGS += -std=c99 $(PKG_CFLAGS) -ggdb -W -Wall -Wextra -pthread -Wunused-function

SOURCES := \
    comm.c \
    commands.c \
    cookie-jar.c \
    events.c \
    gui.c \
    inspector.c \
    io.c \
    js.c \
    requests.c \
    scheme.c \
    status-bar.c \
    util.c \
    uzbl-core.c \
    variables.c \
    3p/async-queue-source/rb-async-queue-watch.c

HEADERS := \
    comm.h \
    commands.h \
    config.h \
    cookie-jar.h \
    events.h \
    gui.h \
    inspector.h \
    io.h \
    js.h \
    requests.h \
    menu.h \
    scheme.h \
    status-bar.h \
    util.h \
    uzbl-core.h \
    variables.h \
    webkit.h \
    3p/async-queue-source/rb-async-queue-watch.h

ifneq ($(ENABLE_WEBKIT2),yes)
SOURCES += \
    scheme-request.c \
    soup.c
HEADERS += \
    scheme-request.h \
    soup.h
endif

SRC  = $(addprefix src/,$(SOURCES))
HEAD = $(addprefix src/,$(HEADERS))
OBJ  = $(foreach obj, $(SRC:.c=.o),  $(obj))
LOBJ = $(foreach obj, $(SRC:.c=.lo), $(obj))
PY = $(wildcard uzbl/*.py uzbl/plugins/*.py)

all: uzbl-browser

VPATH:=src

${OBJ}: ${HEAD}

uzbl-core: ${OBJ}

uzbl-browser: uzbl-core uzbl-event-manager uzbl.desktop bin/uzbl-browser

uzbl.desktop: uzbl.desktop.in
	sed 's#@PREFIX@#$(PREFIX)#' < uzbl.desktop.in > uzbl.desktop

bin/uzbl-browser: bin/uzbl-browser.in
	sed 's#@PREFIX@#$(PREFIX)#' < bin/uzbl-browser.in > bin/uzbl-browser

build: ${PY}
	$(PYTHON) setup.py build

.PHONY: uzbl-event-manager
uzbl-event-manager: build

# the 'tests' target can never be up to date
.PHONY: tests
force:

# this is here because the .so needs to be compiled with -fPIC on x86_64
${LOBJ}: ${SRC} ${HEAD}
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -c src/$(@:.lo=.c) -o $@

# When compiling unit tests, compile uzbl as a library first
tests: ${LOBJ} force
	$(CC) -shared -Wl ${LOBJ} -o ./tests/libuzbl-core.so
	cd ./tests/; $(MAKE)

test-event-manager: force
	${PYTHON} -m unittest discover tests/event-manager -v

coverage-event-manager: force
	${PYTHON} ${COVERAGE} erase
	${PYTHON} ${COVERAGE} run -m unittest discover tests/event-manager
	${PYTHON} ${COVERAGE} html ${PY}
	# Hmm, I wonder what a good default browser would be
	uzbl-browser htmlcov/index.html

test-uzbl-core: uzbl-core
	./uzbl-core --uri http://www.uzbl.org --verbose

test-uzbl-browser: uzbl-browser
	./bin/uzbl-browser --uri http://www.uzbl.org --verbose

test-uzbl-core-sandbox: sandbox uzbl-core sandbox-install-uzbl-core sandbox-install-example-data
	. ./sandbox/env.sh && uzbl-core --uri http://www.uzbl.org --verbose
	make DESTDIR=./sandbox uninstall
	rm -rf ./sandbox/usr

test-uzbl-browser-sandbox: sandbox uzbl-browser sandbox-install-uzbl-browser sandbox-install-example-data
	. ./sandbox/env.sh && ${PYTHON} -S `which uzbl-event-manager` restart -navv &
	. ./sandbox/env.sh && uzbl-browser --uri http://www.uzbl.org --verbose
	. ./sandbox/env.sh && ${PYTHON} -S `which uzbl-event-manager` stop -vv -o /dev/null
	make DESTDIR=./sandbox uninstall
	rm -rf ./sandbox/usr

test-uzbl-tabbed-sandbox: sandbox uzbl-browser sandbox-install-uzbl-browser sandbox-install-uzbl-tabbed sandbox-install-example-data
	. ./sandbox/env.sh && ${PYTHON} -S `which uzbl-event-manager` restart -avv
	. ./sandbox/env.sh && uzbl-tabbed
	. ./sandbox/env.sh && ${PYTHON} -S `which uzbl-event-manager` stop -avv
	make DESTDIR=./sandbox uninstall
	rm -rf ./sandbox/usr

test-uzbl-event-manager-sandbox: sandbox uzbl-browser sandbox-install-uzbl-browser sandbox-install-example-data
	. ./sandbox/env.sh && ${PYTHON} -S `which uzbl-event-manager` restart -navv
	make DESTDIR=./sandbox uninstall
	rm -rf ./sandbox/usr

clean:
	rm -f uzbl-core
	rm -f $(OBJ) ${LOBJ}
	rm -f uzbl.desktop
	rm -f bin/uzbl-browser
	find ./examples/ -name "*.pyc" -delete
	find -name __pycache__ -type d -delete
	cd ./tests/; $(MAKE) clean
	rm -rf ./sandbox/
	$(PYTHON) setup.py clean

strip:
	@echo Stripping binary
	@strip uzbl-core
	@echo ... done.

SANDBOXOPTS=\
	DESTDIR=./sandbox\
	RUN_PREFIX=`pwd`/sandbox/usr/local\
	PYINSTALL_EXTRA='--prefix=./sandbox/usr/local --install-scripts=./sandbox/usr/local/bin'

sandbox: misc/env.sh
	mkdir -p sandbox/${PREFIX}/lib64
	cp -p misc/env.sh sandbox/env.sh
	test -e sandbox/${PREFIX}/lib || ln -s lib64 sandbox/${PREFIX}/lib

sandbox-install-uzbl-browser:
	make ${SANDBOXOPTS} install-uzbl-browser

sandbox-install-uzbl-tabbed:
	make ${SANDBOXOPTS} install-uzbl-tabbed

sandbox-install-uzbl-core:
	make ${SANDBOXOPTS} install-uzbl-core

sandbox-install-event-manager:
	make ${SANDBOXOPTS} install-event-manager

sandbox-install-example-data:
	make ${SANDBOXOPTS} install-example-data

install: install-uzbl-core install-uzbl-browser install-uzbl-tabbed

install-dirs:
	[ -d "$(INSTALLDIR)/bin" ] || install -d -m755 $(INSTALLDIR)/bin

install-uzbl-core: uzbl-core install-dirs
	$(INSTALL) -d $(INSTALLDIR)/share/uzbl/
	$(INSTALL) -d $(DOCDIR)
	$(INSTALL) -d $(MANDIR)/man1
	$(INSTALL) -m644 docs/* $(DOCDIR)/
	$(INSTALL) -m644 src/config.h $(DOCDIR)/
	$(INSTALL) -m644 README $(DOCDIR)/
	$(INSTALL) -m644 AUTHORS $(DOCDIR)/
	$(INSTALL) -m755 uzbl-core $(INSTALLDIR)/bin/uzbl-core
	$(INSTALL) -m644 uzbl.1 $(MANDIR)/man1/uzbl.1
	$(INSTALL) -m644 uzbl-event-manager.1 $(MANDIR)/man1/uzbl-event-manager.1

install-event-manager: install-dirs
	$(PYTHON) setup.py install --prefix=$(PREFIX) --root=$(DESTDIR) --install-scripts=$(INSTALLDIR)/bin $(PYINSTALL_EXTRA)

install-uzbl-browser: install-dirs install-uzbl-core install-event-manager
	$(INSTALL) -d $(INSTALLDIR)/share/applications
	sed 's#^PREFIX=.*#PREFIX=$(RUN_PREFIX)#' < bin/uzbl-browser > $(INSTALLDIR)/bin/uzbl-browser
	chmod 755 $(INSTALLDIR)/bin/uzbl-browser
	cp -r examples $(INSTALLDIR)/share/uzbl/
	chmod 755 $(INSTALLDIR)/share/uzbl/examples/data/scripts/*
	$(INSTALL) -m644 uzbl.desktop $(INSTALLDIR)/share/applications/uzbl.desktop

install-uzbl-tabbed: install-dirs
	$(INSTALL) -m755 bin/uzbl-tabbed $(INSTALLDIR)/bin/uzbl-tabbed

# you probably only want to do this manually when testing and/or to the sandbox. not meant for distributors
install-example-data:
	$(INSTALL) -d $(DESTDIR)/home/.config/uzbl
	$(INSTALL) -d $(DESTDIR)/home/.cache/uzbl
	$(INSTALL) -d $(DESTDIR)/home/.local/share/uzbl
	cp -rp examples/config/* $(DESTDIR)/home/.config/uzbl/
	cp -rp examples/data/*   $(DESTDIR)/home/.local/share/uzbl/

uninstall:
	rm -rf $(INSTALLDIR)/bin/uzbl-*
	rm -rf $(INSTALLDIR)/share/uzbl
