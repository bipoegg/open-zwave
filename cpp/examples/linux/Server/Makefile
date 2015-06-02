#
# Makefile for OpenzWave Mac OS X applications
# Greg Satz

# GNU make only

# requires libudev-dev

.SUFFIXES:      .cpp .o .a .s

CC     := $(CROSS_COMPILE)gcc
CXX    := $(CROSS_COMPILE)g++
LD     := $(CROSS_COMPILE)g++
AR     := $(CROSS_COMPILE)ar rc
RANLIB := $(CROSS_COMPILE)ranlib

DEBUG_CFLAGS    := -Wall -Wno-format -g -DDEBUG
RELEASE_CFLAGS  := -Wall -Wno-unknown-pragmas -Wno-format -O3
DEBUG_LDFLAGS   := -g



# Change for DEBUG or RELEASE
CFLAGS  := -c $(DEBUG_CFLAGS)
LDFLAGS := $(DEBUG_LDFLAGS)

INCLUDES	:= -I ../../../src -I ../../../src/command_classes/ -I ../../../src/value_classes/ \
        	-I ../../../src/platform/ -I ../../../h/platform/unix -I ../../../tinyxml/ \
		-I ../../../hidapi/hidapi/

LIBS = $(wildcard ../../../../*.a) 

top_srcdir := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))../../../../)

%.o : %.cpp
	$(CXX) $(CFLAGS) $(INCLUDES) -o $@ $<

all: $(top_srcdir)/Server

lib:
	$(MAKE) -C ../../../..

Socket: Socket.o
	$(LD) -o Socket.o Socket.cpp

$(top_srcdir)/Server: Main.o Socket.o zwavelib.o
	$(LD) -o $@ $(LDFLAGS) $< $(LIBS) Socket.o zwavelib.o -pthread -ludev

clean:
	rm -f test Main.o Socket.o zwavelib.o

XMLLINT := $(shell whereis -b xmllint | cut -c10-)

ifeq ($(XMLLINT),)
xmltest:        $(XMLLINT)
	$(error xmllint command not found.)
else
xmltest:        $(XMLLINT)
	@$(XMLLINT) --noout --schema ../../../../config/zwcfg.xsd zwcfg_*.xml
	@$(XMLLINT) --noout --schema ../../../../config/zwscene.xsd zwscene.xml
endif
