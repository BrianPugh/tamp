.PHONY: clean test collect-data venv


venv:
	@. .venv/bin/activate

clean:
	rm -rf build

build/enwik8:
	if [ ! -f build/enwik8 ]; then \
		mkdir -p build; \
		cd build; \
		curl -O https://mattmahoney.net/dc/enwik8.zip; \
		unzip -q enwik8.zip; \
		rm enwik8.zip; \
		cd ..; \
	fi

download-enwik8: build/enwik8

build/silesia:
	if [ ! -f build/silesia ]; then \
		mkdir -p build; \
		cd build; \
		curl -O http://mattmahoney.net/dc/silesia.zip; \
		mkdir silesia; \
		unzip -q silesia.zip -d silesia; \
		rm silesia.zip; \
		cd ..; \
	fi

download-silesia: build/silesia

build/enwik8-100kb: download-enwik8
	@head -c 100000 build/enwik8 > build/enwik8-100kb

build/enwik8-100kb.tamp: build/enwik8-100kb
	@poetry run tamp compress build/enwik8-100kb -o build/enwik8-100kb.tamp

test: venv
	@python -m pytest
	@belay run micropython -m unittest tests/*.py

collect-data: venv download-enwik8
	@python tools/collect-data.py 8
	@python tools/collect-data.py 9
	@python tools/collect-data.py 10

on-device-compression-benchmark: venv build/enwik8-100kb build/enwik8-100kb.tamp
	@port=$$(python -c "import os, belay; print(belay.UsbSpecifier.parse_raw(os.environ['BELAY_DEVICE']).to_port())"); \
	echo "Using port: $$port"; \
	mpremote connect port:$$port rm :enwik8-100kb.tamp || true; \
	belay sync '$(BELAY_DEVICE)' build/enwik8-100kb; \
	belay install '$(BELAY_DEVICE)' --with=dev --run tools/on-device-compression-benchmark.py; \
	mpremote connect port:$$port cp :enwik8-100kb.tamp build/on-device-enwik8-100kb.tamp; \
	cmp build/enwik8-100kb.tamp build/on-device-enwik8-100kb.tamp; \
	echo "Success!"

on-device-decompression-benchmark: venv build/enwik8-100kb.tamp
	@port=$$(python -c "import os, belay; print(belay.UsbSpecifier.parse_raw(os.environ['BELAY_DEVICE']).to_port())"); \
	echo "Using port: $$port"; \
	mpremote connect port:$$port rm :enwik8-100kb-decompressed || true; \
	belay sync '$(BELAY_DEVICE)' build/enwik8-100kb.tamp; \
	belay install '$(BELAY_DEVICE)' --with=dev --run tools/on-device-decompression-benchmark.py; \
	mpremote connect port:$$port cp :enwik8-100kb-decompressed build/on-device-enwik8-100kb-decompressed; \
	cmp build/enwik8-100kb build/on-device-enwik8-100kb-decompressed; \
	echo "Success!"
