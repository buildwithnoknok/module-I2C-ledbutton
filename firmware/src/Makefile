PROJECT = keyboard_firmware
SOURCES = keyboard_firmware.c
TARGET  = keyboard_firmware

TARGET_MCU ?= CH32V003

include ../ch32fun/ch32fun.mk

all   : flash
flash : cv_flash
clean : cv_clean
