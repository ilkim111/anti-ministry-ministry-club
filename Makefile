.PHONY: all backend electron-deps electron-dev pack dist dist-mac dist-win dist-linux clean

# ── Backend (C++ via CMake) ───────────────────────────────────────────
backend:
	@echo "Building C++ backend..."
	./scripts/build.sh Release

backend-debug:
	@echo "Building C++ backend (debug)..."
	./scripts/build.sh Debug

# ── Electron ──────────────────────────────────────────────────────────
electron-deps:
	cd electron && npm install

electron-dev: electron-deps
	cd electron && npm start

# ── Package ───────────────────────────────────────────────────────────
pack: backend electron-deps
	@echo "Packaging (unpacked dir)..."
	cd electron && npm run pack

dist: backend electron-deps
	@echo "Building distributable..."
	cd electron && npm run dist

dist-mac: backend electron-deps
	cd electron && npm run dist:mac

dist-win: backend electron-deps
	cd electron && npm run dist:win

dist-linux: backend electron-deps
	cd electron && npm run dist:linux

# ── All ───────────────────────────────────────────────────────────────
all: dist

# ── Clean ─────────────────────────────────────────────────────────────
clean:
	rm -rf build/
	rm -rf electron/dist/
	rm -rf electron/node_modules/
