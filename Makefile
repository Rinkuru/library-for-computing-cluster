.PHONY: all build debug analyze simple-test hard-test speedup-test test clean

all: build

CFLAGS = -Wall -Wextra -Wformat=2 -Wformat-security -Werror=format-security -D_FORTIFY_SOURCE=2 -fstack-protector-strong -fPIE
LDFLAGS = -pie -Wl,-z,relro -Wl,-z,now

build:
	cmake -S . -B build -DCMAKE_C_COMPILER=gcc -DCMAKE_C_FLAGS="$(CFLAGS)" -DCMAKE_EXE_LINKER_FLAGS="$(LDFLAGS)"
	cmake --build build
	cmake -S . -B build-clang -DCMAKE_C_COMPILER=clang -DCMAKE_C_FLAGS="$(CFLAGS)" -DCMAKE_EXE_LINKER_FLAGS="$(LDFLAGS)"
	cmake --build build-clang

debug:
	cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_C_FLAGS="$(CFLAGS)" -DCMAKE_EXE_LINKER_FLAGS="$(LDFLAGS)"
	cmake --build build-debug

analyze:
	clang --analyze -Iinclude -Iexamples src/master.c
	clang --analyze -Iinclude -Iexamples src/worker.c
	clang --analyze -Iinclude -Iexamples src/common_multiplexing.c
	clang --analyze -Iinclude -Iexamples src/deadline_timer.c
	clang --analyze -Iinclude -Iexamples examples/integral.c
	clang --analyze -Iinclude -Iexamples examples/integral_master.c
	clang --analyze -Iinclude -Iexamples examples/integral_worker.c
	rm *.plist


simple-test: build
	./scripts/benchmark_simpleWorkers.sh

hard-test: build
	./scripts/benchmark_HardFunc7Workers.sh

speedup-test: build
	./scripts/benchmark_HardWorkers.sh

test: build
	./scripts/benchmark_simpleWorkers.sh
	./scripts/benchmark_HardFunc7Workers.sh
	./scripts/benchmark_HardWorkers.sh


clean:
	rm -rf build/ build-clang/ build-debug/
	rm scripts/*.log
