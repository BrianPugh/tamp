.DEFAULT_GOAL := build

configure:
	@cmake -B build -S .

build: configure
	@make -C build tamp_benchmark

monitor:
	poetry run python -m serial.tools.miniterm ${PORT}
clean:
	@rm -rf build
