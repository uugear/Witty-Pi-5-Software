CC := gcc
DPKG := dpkg

WP5LIB_HEADER := wp5lib.h
CONTROL_FILE := debpkg/DEBIAN/control

VERSION_MAJOR := $(shell awk '/^[[:space:]]*#define[[:space:]]+SOFTWARE_VERSION_MAJOR[[:space:]]+/ {print $$NF; exit}' $(WP5LIB_HEADER))
VERSION_MINOR := $(shell awk '/^[[:space:]]*#define[[:space:]]+SOFTWARE_VERSION_MINOR[[:space:]]+/ {print $$NF; exit}' $(WP5LIB_HEADER))
VERSION_PATCH := $(shell awk '/^[[:space:]]*#define[[:space:]]+SOFTWARE_VERSION_PATCH[[:space:]]+/ {print $$NF; exit}' $(WP5LIB_HEADER))

SOFTWARE_VERSION := $(VERSION_MAJOR).$(VERSION_MINOR).$(VERSION_PATCH)
DEB_FILE := wp5_$(SOFTWARE_VERSION)-1_arm64.deb

.PHONY: all clean debpkg test

all: wp5d wp5 debpkg

debpkg: wp5lib wp5d wp5
	cp wp5d debpkg/usr/bin/wp5d
	cp wp5 debpkg/usr/bin/wp5

	cp wp5d.service debpkg/lib/systemd/system/wp5d.service
	cp wp5d_poweroff.service debpkg/lib/systemd/system/wp5d_poweroff.service
	cp wp5d_reboot.service debpkg/lib/systemd/system/wp5d_reboot.service

	chmod 755 debpkg/DEBIAN/postinst
	sed -i 's/^Version:.*/Version: $(SOFTWARE_VERSION)/' $(CONTROL_FILE)
	@size_kb=$$(find debpkg -mindepth 1 -maxdepth 1 ! -name DEBIAN -exec du -sk {} + 2>/dev/null | awk '{sum += $$1} END {print sum + 0}'); \
	if grep -q '^Installed-Size:' $(CONTROL_FILE); then \
		sed -i "s/^Installed-Size:.*/Installed-Size: $$size_kb/" $(CONTROL_FILE); \
	else \
		printf 'Installed-Size: %s\n' "$$size_kb" >> $(CONTROL_FILE); \
	fi
	$(DPKG) --build debpkg "$(DEB_FILE)"

wp5: wp5lib
	$(CC) -o wp5 wp5.c wp5lib.o wp5dst.o

wp5d: wp5lib
	$(CC) -o wp5d wp5d.c wp5lib.o wp5dst.o

wp5lib: wp5lib.c wp5lib.h wp5dst.o
	$(CC) -c wp5lib.c

wp5dst.o: wp5dst.c wp5dst.h
	$(CC) -c wp5dst.c

clean:
	rm -f *.deb
	rm -f debpkg/usr/bin/wp5d
	rm -f debpkg/usr/bin/wp5
	rm -f debpkg/lib/systemd/system/wp5d.service
	rm -f debpkg/lib/systemd/system/wp5d_poweroff.service
	rm -f debpkg/lib/systemd/system/wp5d_reboot.service
	rm -f wp5
	rm -f wp5d
	rm -f wp5lib.o wp5dst.o

# Host-side tests; do not require Raspberry Pi hardware or an I2C adapter.
test:
	$(CC) -std=gnu11 -Wall -Wextra -Werror -I. -o tests/test_dst tests/test_dst.c wp5dst.c
	./tests/test_dst
	rm -f tests/test_dst
