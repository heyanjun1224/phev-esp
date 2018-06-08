PROJECT_NAME := phev-esp
BUILD_NUMBER ?= $(BUILD)
SHELL := /bin/bash
BUILD_DIR ?= ./build
COMP_DIR := ./components/**
SRC_DIR := $(COMP_DIR)/src
TEST_DIR ?= $(COMP_DIR)/test
CJSON_DIR ?= ${CJSON_DIR}
INC_DIRS := $(shell find $(COMP_DIR) -type d)
INC_FLAGS := $(addprefix -I,$(INC_DIRS))
CPPFLAGS ?= $(INC_FLAGS) -DBUILD_NUMBER=$(BUILD_NUMBER) -MMD -Wall -Wextra -g
TEST_BUILD_DIR ?= $(BUILD_DIR)/test
TEST_MAKEFILE = $(TEST_BUILD_DIR)/MakefileTestSupport
#INCLUDE_PATH += -I$(SRC_DIR)/include 
INCLUDE_PATH += -lcjson -I$(INC_FLAGS) -std=c99 -lcjson
CMOCK_DIR := ${CMOCK_DIR}
RM := rm
TEST_CFLAGS += -lcjson
LDFLAGS +=-lcjson
export

ifdef IDF_PATH
undefine LDFLAGS
unexport
include $(IDF_PATH)/make/project.mk
endif

MKDIR_P ?= mkdir -p

setup:
	$(MKDIR_P) $(dir $@)
	ruby $(CMOCK_DIR)/scripts/create_makefile.rb

test: setup
.PHONY: clean

test_clean:
	$(RM) -r $(BUILD_DIR)/test

-include $(TEST_MAKEFILE)