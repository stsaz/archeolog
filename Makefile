# archeolog makefile

ROOT := ..
ARLG_DIR := $(ROOT)/archeolog
FFBASE_DIR := $(ROOT)/ffbase
FFOS_DIR := $(ROOT)/ffos

include $(FFBASE_DIR)/test/makeconf

BIN := archeolog
ifeq "$(OS)" "windows"
	BIN := archeolog.exe
endif

CFLAGS := -I$(ARLG_DIR)/src -I$(FFOS_DIR) -I$(FFBASE_DIR) \
	-DFFBASE_HAVE_FFERR_STR
ifeq "$(OPT)" "0"
	CFLAGS += -g -O0 -DFF_DEBUG
else
	CFLAGS += -O3 -s
endif
ifneq "$(OLD_CPU)" "1"
	CFLAGS += -march=nehalem
endif

# build, install
default: $(BIN)
	$(MAKE) -f $(firstword $(MAKEFILE_LIST)) install

# build, install, package
build-package: default
	$(MAKE) -f $(firstword $(MAKEFILE_LIST)) package

%.o: $(ARLG_DIR)/src/%.c \
		$(wildcard $(ARLG_DIR)/src/*.h) \
		$(wildcard $(ARLG_DIR)/src/util/*.h) \
		$(ARLG_DIR)/Makefile
	$(C) $(CFLAGS) $< -o $@
$(BIN): main.o conf.o
	$(LINK) $+ $(LINKFLAGS) -o $@

test: test.o
	$(LINK) $+ $(LINKFLAGS) -o $@


# copy files to install directory
INST_DIR := archeolog-0
install:
	$(MKDIR) $(INST_DIR)
	$(CP) \
		$(BIN) \
		$(ARLG_DIR)/README.md \
		$(INST_DIR)
	chmod 0644 $(INST_DIR)/*
	chmod 0755 $(INST_DIR)/$(BIN)


# package
PKG_VER := 0.1
PKG_ARCH := amd64
PKG_PACKER := tar -c --owner=0 --group=0 --numeric-owner -v --zstd -f
PKG_EXT := tar.zst
ifeq "$(OS)" "windows"
	PKG_ARCH := x64
	PKG_PACKER := zip -r -v
	PKG_EXT := zip
endif
package:
	$(PKG_PACKER) archeolog-$(PKG_VER)-$(OS)-$(PKG_ARCH).$(PKG_EXT) $(INST_DIR)
