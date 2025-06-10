all: wp5d wp5 debpkg

debpkg: wp5lib wp5d
	cp wp5d debpkg/usr/bin/wp5d
	cp wp5 debpkg/usr/bin/wp5
	
	cp wp5d.service debpkg/lib/systemd/system/wp5d.service
	cp wp5d_poweroff.service debpkg/lib/systemd/system/wp5d_poweroff.service
	cp wp5d_reboot.service debpkg/lib/systemd/system/wp5d_reboot.service
	
	chmod 755 debpkg/DEBIAN/postinst
	dpkg --build debpkg "wp5_arm64.deb"

wp5: wp5lib
	gcc -o wp5 wp5.c wp5lib.o

wp5d: wp5lib
	gcc -o wp5d wp5d.c wp5lib.o

wp5lib: wp5lib.c
	gcc -c wp5lib.c

clean:
	rm -f *.deb
	rm -f debpkg/usr/bin/wp5d
	rm -f debpkg/usr/bin/wp5
	rm -f debpkg/lib/systemd/system/wp5d.service
	rm -f debpkg/lib/systemd/system/wp5d_poweroff.service
	rm -f debpkg/lib/systemd/system/wp5d_reboot.service
	rm -f wp5
	rm -f wp5d
	rm -f wp5lib.o
