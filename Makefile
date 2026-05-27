.PHONY: all build debug simple-test hard-test speedup-test test clean

all: build

build:
	cmake -S . -B build
	cmake --build build

debug:
	cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
	cmake --build build-debug

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
	rm -rf build/ build-debug/
	rm scripts/*.log
