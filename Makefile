BINDIR=out

main:
	cd $(BINDIR) && cmake -DCMAKE_BUILD_TYPE=Debug ..
	make -C $(BINDIR)

test:	main
	cd $(BINDIR) && ctest -DCMAKE_BUILD_TYPE=Debug -V -R capnprof_test

clean:
	make -C $(BINDIR) clean

all:	main
