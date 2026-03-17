SRCARCH = arm
KERNEL_VERSION := $(shell uname -r)
SRCTREE = /usr/src/linux-headers-$(KERNEL_VERSION)

DTC = dtc
DTCFLAGS = -b 0

dtc_cpp_flags = -x assembler-with-cpp -nostdinc \
	-I $(SRCTREE)/include \
	-I $(SRCTREE)/arch/$(SRCARCH)/boot/dts \
	-I $(SRCTREE)/drivers/of/testcase-data \
	-I $(SRCTREE)/include/dt-bindings/interrupt-controller \
	-undef -D__DTS__

DTCINCLUDES = -i $(SRCTREE)/include \
	-i $(SRCTREE)/arch/$(SRCARCH)/boot/dts \
	-i $(SRCTREE)/drivers/of/testcase-data \
	-i $(SRCTREE)/include/dt-bindings/interrupt-controller

%.dtb: %.dts
	cpp $(dtc_cpp_flags) < $< | $(DTC) $(DTCINCLUDES) -I dts -O dtb $(DTCFLAGS) -o $@ -

all: jetson-mcp2515.dtb