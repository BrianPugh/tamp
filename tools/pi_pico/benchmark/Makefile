.DEFAULT_GOAL := build

configure:
	@cmake -B build -S .

build: configure
	@make -C build tamp_benchmark

flash: build
	cp build/tamp_benchmark.uf2 /Volumes/RPI-RP2/

monitor:
	poetry run python -m serial.tools.miniterm ${PORT}

clean:
	@rm -rf build
