# OpenClaw — Native C Implementation
# Top-level Makefile

CC       ?= gcc
CFLAGS    = -O2 -Wall -Wextra -Wpedantic -std=c11 -D_GNU_SOURCE \
            -Iinclude -Ivendor/cJSON
LDFLAGS   = -lcurl -lssl -lcrypto

# SQLite for gateway
GW_LDFLAGS = $(LDFLAGS) -lsqlite3 -lpthread

PREFIX   ?= /usr
BINDIR    = $(PREFIX)/bin
ETCDIR    = /etc/openclaw
LIBDIR    = /var/lib/openclaw
UNITDIR   = /lib/systemd/system
VERSION   = 0.1.0
ARCH     ?= $(shell dpkg --print-architecture 2>/dev/null || echo amd64)

# ============================================================
# Source files
# ============================================================

# Shared core objects (used by both CLI and gateway)
CORE_SRC = core/config.o core/json_helpers.o core/auth.o \
           core/http_client.o core/stream_anthropic.o \
           core/stream_openai.o core/session.o \
           vendor/cJSON/cJSON.o

# CLI objects
CLI_SRC  = cli/main.o cli/cmd_agent.o cli/cmd_version.o cli/cmd_config.o

# Gateway objects
GW_SRC   = gateway/main.o gateway/epoll_server.o gateway/http_parser.o \
           gateway/http_response.o gateway/router.o \
           gateway/handler_health.o gateway/handler_webhook.o \
           gateway/handler_chat.o gateway/ws_server.o \
           gateway/session_store.o gateway/channel_webchat.o \
           gateway/auth_gateway.o gateway/tls.o gateway/systemd.o

# ============================================================
# Targets
# ============================================================

.PHONY: all clean cli gateway kernel install deb help

all: cli gateway
	@echo "Build complete."
	@echo "  ./openclaw     — CLI binary"
	@echo "  ./openclawgw   — Gateway daemon"

help:
	@echo "OpenClaw Build System"
	@echo "====================="
	@echo "  make            — Build CLI + gateway"
	@echo "  make cli        — Build CLI only"
	@echo "  make gateway    — Build gateway only"
	@echo "  make kernel     — Build kernel module (requires kernel headers)"
	@echo "  make install    — Install to system"
	@echo "  make deb        — Build Debian package"
	@echo "  make clean      — Remove build artifacts"

# ============================================================
# CLI Binary
# ============================================================

cli: openclaw

openclaw: $(CLI_SRC) $(CORE_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Built: openclaw"

# ============================================================
# Gateway Daemon
# ============================================================

gateway: openclawgw

openclawgw: $(GW_SRC) $(CORE_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(GW_LDFLAGS)
	@echo "Built: openclawgw"

# ============================================================
# Kernel Module
# ============================================================

kernel:
	$(MAKE) -C kernel

# ============================================================
# Object file rules
# ============================================================

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# ============================================================
# Install
# ============================================================

install: openclaw openclawgw
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 openclaw $(DESTDIR)$(BINDIR)/openclaw
	install -m 755 openclawgw $(DESTDIR)$(BINDIR)/openclawgw
	install -d $(DESTDIR)$(ETCDIR)
	install -m 644 ../debian/openclaw.json.example $(DESTDIR)$(ETCDIR)/openclaw.json.example
	install -d $(DESTDIR)$(UNITDIR)
	install -m 644 ../debian/openclawgw.service $(DESTDIR)$(UNITDIR)/openclawgw.service
	install -d $(DESTDIR)$(LIBDIR)

# ============================================================
# Debian Package
# ============================================================

DEB_DIR = openclaw_$(VERSION)-1_$(ARCH)

deb: openclaw openclawgw
	@echo "Building Debian package..."
	rm -rf $(DEB_DIR)
	# DEBIAN control
	mkdir -p $(DEB_DIR)/DEBIAN
	cp ../debian/control   $(DEB_DIR)/DEBIAN/control
	cp ../debian/postinst  $(DEB_DIR)/DEBIAN/postinst
	cp ../debian/prerm     $(DEB_DIR)/DEBIAN/prerm
	cp ../debian/conffiles $(DEB_DIR)/DEBIAN/conffiles
	chmod 755 $(DEB_DIR)/DEBIAN/postinst $(DEB_DIR)/DEBIAN/prerm
	# Binaries
	mkdir -p $(DEB_DIR)/usr/bin
	cp openclaw    $(DEB_DIR)/usr/bin/
	cp openclawgw  $(DEB_DIR)/usr/bin/
	# Config
	mkdir -p $(DEB_DIR)/etc/openclaw
	cp ../debian/openclaw.json.example $(DEB_DIR)/etc/openclaw/
	# systemd
	mkdir -p $(DEB_DIR)/lib/systemd/system
	cp ../debian/openclawgw.service $(DEB_DIR)/lib/systemd/system/
	# modules-load
	mkdir -p $(DEB_DIR)/etc/modules-load.d
	echo "openclaw" > $(DEB_DIR)/etc/modules-load.d/openclaw.conf
	# State dir
	mkdir -p $(DEB_DIR)/var/lib/openclaw
	# Build
	dpkg-deb --build $(DEB_DIR)
	@echo "Package: $(DEB_DIR).deb"

# ============================================================
# Clean
# ============================================================

clean:
	rm -f openclaw openclawgw
	rm -f cli/*.o core/*.o gateway/*.o vendor/cJSON/*.o
	rm -rf openclaw_*_*.deb openclaw_*_*/
	$(MAKE) -C kernel clean 2>/dev/null || true
