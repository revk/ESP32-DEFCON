#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#

PROJECT_NAME := DEFCON
SUFFIX := $(shell components/ESP32-RevK/buildsuffix)

all:    settings.h
	@echo Make: $(PROJECT_NAME)$(SUFFIX).bin
	@idf.py build
	@cp build/$(PROJECT_NAME).bin $(PROJECT_NAME)$(SUFFIX).bin
	@echo Done: $(PROJECT_NAME)$(SUFFIX).bin

beta:
	-git pull
	-git submodule update --recursive
	-git commit -a -m checkpoint
	@make set
	cp DEFCON*.bin betarelease
	git commit -a -m Beta
	git push

issue:
	-git pull
	-git submodule update --recursive
	-git commit -a -m checkpoint
	@make set
	cp DEFCON*.bin betarelease
	cp DEFCON*.bin release
	git commit -a -m Release
	git push

settings.h:     components/ESP32-RevK/revk_settings settings.def components/ESP32-RevK/settings.def
	components/ESP32-RevK/revk_settings $^

components/ESP32-RevK/revk_settings: components/ESP32-RevK/revk_settings.c
	make -C components/ESP32-RevK

tools:	DEFCON

set:    wroom solo pico s3

s3:
	components/ESP32-RevK/setbuildsuffix -S3-MINI-N4-R2
	@make

pico:
	components/ESP32-RevK/setbuildsuffix -S1-PICO
	@make

wroom:
	components/ESP32-RevK/setbuildsuffix -S1-V1
	@make

solo:  
	components/ESP32-RevK/setbuildsuffix -S1-SOLO
	@make

flash:
	idf.py flash

monitor:
	idf.py monitor

clean:
	idf.py clean

menuconfig:
	idf.py menuconfig

pull:
	git pull
	git submodule update --recursive

update:
	git submodule update --init --recursive --remote
	git commit -a -m "Library update"
