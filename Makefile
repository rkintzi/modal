# See LICENSE file for copyright and license details.

include config.mk

SRC = modal.c
OBJ = ${SRC:.c=.o}
BIN = ${OBJ:.o=}

all: options ${BIN}

options:
	@echo modal build options:
	@echo "CFLAGS   = ${CFLAGS}"
	@echo "LDFLAGS  = ${LDFLAGS}"
	@echo "CC       = ${CC}"

.c.o:
	@echo CC $<
	${CC} -c ${CFLAGS} $<

${OBJ}: config.h config.mk

config.h:
	@echo creating $@ from config.def.h
	@cp config.def.h $@

.o:
	@echo CC -o $@
	@${CC} -o $@ $< ${LDFLAGS}

clean:
	@echo cleaning
	@rm -f ${BIN} ${OBJ} modal-${VERSION}.tar.gz

install: all
	@echo installing executable files to ${DESTDIR}${PREFIX}/bin
	@mkdir -p "${DESTDIR}${PREFIX}/bin"
	@cp -f ${BIN} "${DESTDIR}${PREFIX}/bin"
	@chmod 755 "${DESTDIR}${PREFIX}/bin/modal"

uninstall:
	@echo removing executable files from ${DESTDIR}${PREFIX}/bin
	@rm -f "${DESTDIR}${PREFIX}/bin/modal"

.PHONY: all options clean install uninstall
