.PHONY: all build debug simple-test hard-test test clean

all: build

build:
	cmake -S . -B build
	cmake --build build

debug:
	cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
	cmake --build build-debug

simple-test:
	./scripts/benchmark_simpleWorkers.sh

hard-test:


test: simple-test hard-test


clean:
	rm -rf build/ build-debug/
