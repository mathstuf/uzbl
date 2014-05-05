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

PYTHON   = python3
COVERAGE = $(shell which coverage)

# --- configuration ends here ---

ifeq ($(ENABLE_GTK3),auto)
ENABLE_GTK3 := $(shell pkg-config --exists gtk+-3.0 && echo yes)
endif

REQ_PKGS += 'webkit2gtk-3.0 >= 1.2.4' javascriptcoregtk-3.0 gtk+-3.0
REQ_PKGS += 'libsoup-2.4 >= 2.33.4' gthread-2.0 glib-2.0

CPPFLAGS += -DG_DISABLE_DEPRECATED
# WebKitGTK uses deprecated features, so uzbl can't blanket this out.
#CPPFLAGS += -DGTK_DISABLE_DEPRECATED

ARCH := $(shell uname -m)

COMMIT_HASH := $(shell ./misc/hash.sh)

CPPFLAGS += -D_XOPEN_SOURCE=500 -DARCH=\"$(ARCH)\" -DCOMMIT=\"$(COMMIT_HASH)\"

HAVE_LIBSOUP_VERSION := $(shell pkg-config --exists 'libsoup-2.4 >= 2.41.1' && echo yes)
ifeq ($(HAVE_LIBSOUP_VERSION),yes)
CPPFLAGS += -DHAVE_LIBSOUP_CHECK_VERSION
endif

HAVE_TLS_API := $(shell pkg-config --exists 'webkit2gtk-3.0 >= 2.3.1' && echo yes)
ifeq ($(HAVE_TLS_API),yes)
REQ_PKGS += gnutls
endif

PKG_CFLAGS := $(shell pkg-config --cflags $(REQ_PKGS))

LDLIBS := $(shell pkg-config --libs $(REQ_PKGS) x11)

CFLAGS += -std=c99 $(PKG_CFLAGS) -ggdb -W -Wall -Wextra -pthread -Wunused-function

SOURCES := \
    comm.c \
    commands.c \
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
    events.h \
    gui.h \
    inspector.h \
    io.h \
    js.h \
    requests.h \
    menu.h \
    scheme.h \
    setup.h \
    status-bar.h \
    util.h \
    uzbl-core.h \
    variables.h \
    webkit.h \
    3p/async-queue-source/rb-async-queue-watch.h

SRC  = $(addprefix src/,$(SOURCES))
HEAD = $(addprefix src/,$(HEADERS))
OBJ  = $(foreach obj, $(SRC:.c=.o),  $(obj))
LOBJ = $(foreach obj, $(SRC:.c=.lo), $(obj))
PY   = $(wildcard uzbl/*.py uzbl/plugins/*.py)

all: uzbl-browser

VPATH := src

${OBJ}: ${HEAD}

uzbl-core: ${OBJ}

uzbl-browser: uzbl-core uzbl-event-manager uzbl-browser.1 uzbl.desktop bin/uzbl-browser

uzbl-browser.1: uzbl-browser.1.in
	sed 's#@PREFIX@#$(PREFIX)#' < uzbl-browser.1.in > uzbl-browser.1

uzbl.desktop: uzbl.desktop.in
	sed 's#@PREFIX@#$(PREFIX)#' < uzbl.desktop.in > uzbl.desktop

bin/uzbl-browser: bin/uzbl-browser.in
	sed 's#@PREFIX@#$(PREFIX)#' < bin/uzbl-browser.in > bin/uzbl-browser
	chmod +x bin/uzbl-browser

build: ${PY}
	$(PYTHON) setup.py build

.PHONY: uzbl-event-manager
uzbl-event-manager: build

# this is here because the .so needs to be compiled with -fPIC on x86_64
${LOBJ}: ${SRC} ${HEAD}
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -c src/$(@:.lo=.c) -o $@

test-event-manager: force
	${PYTHON} -m unittest discover tests/event-manager -v

coverage-event-manager: force
	${PYTHON} ${COVERAGE} erase
	${PYTHON} ${COVERAGE} run -m unittest discover tests/event-manager
	${PYTHON} ${COVERAGE} html ${PY}
	echo Open \'htmlcov/index.html\' in your browser to see the results

test-uzbl-core: uzbl-core
	./uzbl-core --uri http://www.uzbl.org --verbose

test-uzbl-browser: uzbl-browser
	./bin/uzbl-browser --uri http://www.uzbl.org --verbose

test-uzbl-core-sandbox: sandbox uzbl-core sandbox-install-uzbl-core sandbox-install-example-data
	./sandbox/env.sh uzbl-core --uri http://www.uzbl.org --verbose
	make DESTDIR=./sandbox uninstall
	rm -rf ./sandbox/usr

test-uzbl-browser-sandbox: sandbox uzbl-browser sandbox-install-uzbl-browser sandbox-install-example-data
	./sandbox/env.sh ${PYTHON} -S sandbox/usr/bin/uzbl-event-manager restart -navv &
	./sandbox/env.sh uzbl-browser --uri http://www.uzbl.org --verbose
	./sandbox/env.sh ${PYTHON} -S sandbox/usr/bin/uzbl-event-manager stop -vv -o /dev/null
	make DESTDIR=./sandbox uninstall
	rm -rf ./sandbox/usr

test-uzbl-tabbed-sandbox: sandbox uzbl-browser sandbox-install-uzbl-browser sandbox-install-uzbl-tabbed sandbox-install-example-data
	./sandbox/env.sh ${PYTHON} -S sandbox/usr/bin/uzbl-event-manager restart -avv
	./sandbox/env.sh uzbl-tabbed
	./sandbox/env.sh ${PYTHON} -S sandbox/usr/bin/uzbl-event-manager stop -avv
	make DESTDIR=./sandbox uninstall
	rm -rf ./sandbox/usr

test-uzbl-event-manager-sandbox: sandbox uzbl-browser sandbox-install-uzbl-browser sandbox-install-example-data
	./sandbox/env.sh ${PYTHON} -S sandbox/usr/bin/uzbl-event-manager restart -navv
	make DESTDIR=./sandbox uninstall
	rm -rf ./sandbox/usr

clean:
	rm -f uzbl-core
	rm -f $(OBJ) ${LOBJ}
	rm -f uzbl.desktop
	rm -f bin/uzbl-browser
	find ./examples/ -name "*.pyc" -delete || :
	find -name __pycache__ -type d -delete || :
	rm -rf ./sandbox/
	$(PYTHON) setup.py clean

strip:
	@echo Stripping binary
	@strip uzbl-core
	@echo ... done.

SANDBOXOPTS=\
	DESTDIR=./sandbox \
	PREFIX=/usr \
	RUN_PREFIX=`pwd`/sandbox/usr \
	PYINSTALL_EXTRA='--prefix=./usr --install-scripts=./usr/bin'

sandbox: misc/env.sh
	mkdir -p sandbox/usr/lib64
	cp -p misc/env.sh sandbox/env.sh
	test -e sandbox/usr/lib || ln -s lib64 sandbox/usr/lib

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
	$(INSTALL) -m644 docs/*.md $(DOCDIR)/
	$(INSTALL) -m644 src/config.h $(DOCDIR)/
	$(INSTALL) -m644 README.md $(DOCDIR)/
	$(INSTALL) -m644 AUTHORS $(DOCDIR)/
	$(INSTALL) -m755 uzbl-core $(INSTALLDIR)/bin/uzbl-core
	$(INSTALL) -m644 uzbl-core.1 $(MANDIR)/man1/uzbl-core.1
	$(INSTALL) -m644 uzbl-event-manager.1 $(MANDIR)/man1/uzbl-event-manager.1

install-event-manager: install-dirs
ifeq ($(DESTDIR),)
	$(PYTHON) setup.py install --prefix=$(PREFIX) --install-scripts=$(INSTALLDIR)/bin $(PYINSTALL_EXTRA)
else
	$(PYTHON) setup.py install --prefix=$(PREFIX) --root=$(DESTDIR) --install-scripts=$(INSTALLDIR)/bin $(PYINSTALL_EXTRA)
endif

install-uzbl-browser: install-dirs install-uzbl-core install-event-manager
	$(INSTALL) -d $(INSTALLDIR)/share/applications
	$(INSTALL) -m755 bin/uzbl-browser $(INSTALLDIR)/bin/uzbl-browser
	#sed 's#@PREFIX@#$(PREFIX)#g' < README.browser.md > README.browser.md
	#$(INSTALL) -m644 README.browser.md $(DOCDIR)/README.browser.md
	#sed 's#@PREFIX@#$(PREFIX)#g' < README.event-manager.md > README.event-manager.md
	#$(INSTALL) -m644 README.event-manager.md $(DOCDIR)/README.event-manager.md
	cp -rv examples $(INSTALLDIR)/share/uzbl/examples
	chmod 755 $(INSTALLDIR)/share/uzbl/examples/data/scripts/*.sh $(INSTALLDIR)/share/uzbl/examples/data/scripts/*.py
	$(INSTALL) -m644 uzbl.desktop $(INSTALLDIR)/share/applications/uzbl.desktop
	$(INSTALL) -m644 uzbl-browser.1 $(MANDIR)/man1/uzbl-browser.1

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
