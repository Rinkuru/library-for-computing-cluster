.PHONY: all build debug analyze check-lcov simple-test hard-test speedup-test test clean coverage-test

CFLAGS = -Wall -Wextra -Wformat=2 -Wformat-security -Werror=format-security -D_FORTIFY_SOURCE=2 -fstack-protector-strong -fPIE
LDFLAGS = -pie -Wl,-z,relro -Wl,-z,now
COVERAGE_CFLAGS = -O0 -g --coverage

all: build

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
	BUILD_DIR=build ./scripts/benchmark_simpleWorkers.sh

hard-test: build
	BUILD_DIR=build ./scripts/benchmark_HardFunc7Workers.sh

speedup-test: build
	BUILD_DIR=build ./scripts/benchmark_HardWorkers.sh

test: build
	BUILD_DIR=build ./scripts/benchmark_simpleWorkers.sh
	BUILD_DIR=build ./scripts/benchmark_HardFunc7Workers.sh
	BUILD_DIR=build ./scripts/benchmark_HardWorkers.sh

coverage-build:
	cmake -S . -B build-coverage -DCMAKE_C_COMPILER=gcc -DCMAKE_C_FLAGS="$(COVERAGE_CFLAGS)" -DCMAKE_EXE_LINKER_FLAGS="--coverage"
	cmake --build build-coverage

coverage-test: coverage-build
	lcov --zerocounters --directory build-coverage
	BUILD_DIR=build-coverage ./scripts/benchmark_simpleWorkers.sh
	./scripts/coverage_report.sh "coverage after simple test"
	BUILD_DIR=build-coverage ./scripts/benchmark_HardFunc7Workers.sh
	./scripts/coverage_report.sh "coverage after hard func test"
	BUILD_DIR=build-coverage ./scripts/benchmark_HardWorkers.sh
	./scripts/coverage_report.sh "coverage after speedup test"

clean:
	rm -rf build/ build-clang/ build-debug/ build-coverage/
	rm scripts/*.log
	rm scripts/*.info
