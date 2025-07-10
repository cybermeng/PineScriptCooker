# Makefile for compiling the PineVM project with Emscripten

# Compiler
EMCC = emcc

# Source files
CPP_SOURCES = main.cpp ../../PineVM.cpp

# Output files
OUTPUT_JS = public/pine_vm.js
OUTPUT_HTML = public/index.html

# Compiler flags
EMCC_FLAGS = -std=c++17 \
             -O3 \
             -lembind \
             -s ALLOW_MEMORY_GROWTH=1 \
             -s MODULARIZE=1 \
             -s EXPORT_ES6=1 \
             -s EXPORT_NAME="createPineVmModule" \
             -s ENVIRONMENT=web \
             -s WASM=1

all: build

build:
	@echo "--- Compiling C++ to WASM ---"
	@mkdir -p public
	$(EMCC) $(EMCC_FLAGS) $(CPP_SOURCES) -o $(OUTPUT_JS)
	@cp index.html $(OUTPUT_HTML)
	@echo "--- Build complete! ---"
	@echo "To run the server, use a simple http server, e.g.:"
	@echo "python3 -m http.server --directory public"
	@echo "Then open http://localhost:8000 in your browser."

clean:
	@echo "--- Cleaning up build files ---"
	@rm -rf public

.PHONY: all build clean