#!/usr/bin/make -f
UNAME := $(shell uname)

############ MORTAR IO LIBRARIES ###############
MIO_ROOT=../
MIO_LIBRARY_PATH=../libs/c/libMIO/
include $(MIO_LIBRARY_PATH)/vars.mk

ifneq ($(UNAME),Darwin)
	LIBS+=-luuid
	DFLAGS+=-DUNIX
	ALLIB+= $(BACNETLIB)
endif

UTHASH_INCLUDE=./libs/uthash/
############### Drivers ###############

#ENFUSE
ENFUSEDIR=./drivers/libfuse
ENFUSESRC=$(ENFUSEDIR)/src
ENFUSEOBJ=$(ENFUSEDIR)/obj/libfuse.o
ENFUSELIB=$(ENFUSEOBJ) -lcurl $(ENFUSEDIR)/libs/jsmn/jsmn.o
ENFUSEFLAG=ENFUSEADAPTER

ADAPTERI+= -I$(ENFUSESRC) 
ALLLIB= $(ENFUSELIB)

#MODBUS
MODBUSDIR=./drivers/libmodbus-3.0.4/
MODBUSSRC=$(MODBUSDIR)/src/
MODBUSLIB=-L /usr/local/lib/
MODBUSPKGCONFIG=`pkg-config --cflags --libs libmodbus`
ADAPTERI+=-I$(MODBUSSRC)
ALLLIB+=obj/modbus_adapter.o

#BACNET
BACNETSTACK_DIR=./drivers/bacnet-stack/

#PUP
PUPLIB=./drivers/libpup/obj/libpup.o
PUPSRC=./drivers/libpup/src/
ALLLIB+= $(PUPLIB) obj/pup_adapter.o

#BACNET
BACNETLIB=-L./drivers/bacnet-stack/demo/handler -L./drivers/bacnet-stack/demo/object/ -L./drivers/bacnet-stack/src/ -L./drivers/bacnet-stack/lib/ -L./drivers/bacnet-stack/ports/linux
BACNETI=-I./drivers/bacnet-stack/include/ -I./drivers/bacnet-stack/ports/linux/ -I./drivers/bacnet-stack/demo/object/

#HUE
HUEDIR=./drivers/libhue
HUEI=$(HUEDIR)/src/
HUEOBJ=$(HUEDIR)/obj/libhue.o
HUELIB=$(HUEOBJ) -lcurl $(HUEDIR)/libs/jsmn/jsmn.o

#B3



ADAPTERI+= -I$(HUEI) 
ALLLIB= $(HUELIB)
#######################################

############### Adapters ###############
ADAPTERSRC=./src
ADAPTERI+= -I./src/
ADAPTEREXEC= adapter

#ENFUSE
ENFUSEADAPTER=obj/libfuse_adapter.o
ADAPTEREXEC+= enfuse
ALLLIB+= obj/libfuse_adapter.o

#MODBUS
ADAPTEREXEC+= modbus
ALLPKGCONFIG+=$(MODBUSPKGCONFIG)

#Public Unitary Protocol
PUPADAPTER=./obj/pup_adapter.o
ADAPTEREXEC+= pup

# BACNET
ADAPTEREXEC+= bacnet

# Phillips Hue
ADAPTEREXEC+= hue

# B3 GPIO 
ADAPTEREXEC+= b3_gpio 

###############


################ UTILITIES ########################

CC=gcc -O3 -g
AR=ar

LIB = -l$(MIO_LIB_NAME) -lstrophe -lexpat -lssl -lresolv -lpthread -lm

INCLUDE = -I$(UTHASH_INCLUDE) -I$(STROPHE_INCLUDE) -I$(STROPHE_INCLUDE_SRC) -I$(MIO_LIBRARY_PATH) -I./src/ $(ADAPTERI)
CFLAGS =-g -Wall

LDFLAGS = -L ./obj/ -L$(STROPHE_LIB) -L $(MIO_LIBRARY_PATH) $(LIB) $(LIBS) 


################### Make Directives #####################
.PHONY : all
all : $(ADAPTEREXEC)

.PHONY : clean
clean :
	  rm -r -f obj/* bin/*

.PHONY :DEBUG
debug : 
	DFLAGS+= -DDEBUG


############### Command Line ###############
adapter: libfuse_adapter.o modbus_adapter.o pup_adapter.o libhue_adapter.o
	$(CC) $(CFLAGS) $(ALLPKGCONFIG) $(INCLUDE) $(DFLAGS) -DALL -o bin/adapter src/adapter.c $(LDFLAGS) $(ADAPTERLIB) $(ALLLIB)
enfuse:  libfuse_adapter.o  src/adapter.c src/libfuse_adapter.c src/libfuse_adapter.h xml_tools.o
	$(CC) $(CFLAGS) $(INCLUDE) $(DFLAGS) -D$(ENFUSEFLAG) -o bin/enfuse src/adapter.c  obj/xml_tools.o $(ADAPTERLIB) $(ENFUSELIB) $(ENFUSEADAPTER) $(LDFLAGS)
modbus:  modbus_adapter.o src/adapter.c src/modbus_adapter.c src/modbus_adapter.h xml_tools.o
	$(CC) $(CFLAGS) $(INCLUDE) -I$(MODBUSSRC) -DMODBUSADAPTER -o bin/modbus src/adapter.c obj/xml_tools.o obj/modbus_adapter.o $(ADAPTERLIB)  $(MODBUSPKGCONFIG) $(LDFLAGS) 
#$(MODBUSLIB) 
pup: pup_adapter.o src/adapter.c
	$(CC) $(CFLAGS) $(INCLUDE) $(PUPI) $(LDFLAGS) -DPUPADAPTER -o bin/pup src/adapter.c $(LDFLAGS) $(PUPLIB) $(ADAPTERLIB) $(PUPADAPTER)
bacnet: bacnet_adapter.o src/adapter.c
	$(CC) $(CFLAGS) $(INCLUDE) $(BACNETI) -DBACNETADAPTER -o bin/bacnet src/adapter.c  ./obj/bacnet_adapter.o ./drivers/bacnet-stack/demo/object/device-client.o ./drivers/bacnet-stack/src/version.o ../libs/c/libstrophe/libstrophe.a ../libs/c/libMIO/libMIO.a ./drivers/bacnet-stack/src/bvlc.c ./drivers/bacnet-stack/lib/libbacnet.a ./obj/ $(LDFLAGS)
	#$(CC) $(CFLAGS) $(INCLUDE)  $(BACNETI) -DBACNETADAPTER -o bin/bacnet src/adapter.c $(LDFLAGS) $(BACNETLIB) $(ADAPTERLIB) $(STROPHE_LIB)/libstrophe.a obj/bacnet_adapter.o

hue: libhue_adapter.o src/adapter.c
	$(CC) $(CFLAGS) $(INCLUDE) -I$(HUEI) $(DFLAGS) -DHUE -o bin/hue src/adapter.c $(LDFLAGS) $(HUELIB) $(ADAPTERLIB) $(STROPHE_LIB)/libstrophe.a obj/libhue_adapter.o

b3_gpio: b3_gpio_adapter.o src/adapter.c
	$(CC) $(CFLAGS) $(INCLUDE) $(DFLAGS) -DB3GPIOADAPTER -o bin/b3_gpio src/adapter.c $(LDFLAGS) $(ADAPTERLIB) $(STROPHE_LIB)/libstrophe.a obj/b3_gpio_adapter.o


############### Adapter binaries ###############
libfuse_adapter.o :  src/libfuse_adapter.c src/libfuse_adapter.h
	$(CC) $(CFLAGS) $(INCLUDE) $(LIBFUSEI) $(DFLAGS) -I./src/ -c src/libfuse_adapter.c; mv libfuse_adapter.o obj/
modbus_adapter.o :  src/modbus_adapter.c src/modbus_adapter.h
	$(CC) $(CFLAGS) $(INCLUDE) -I $(UTHASH_INCLUDE) -I$(MODBUSSRC) $(DFLAGS) -c src/modbus_adapter.c;mv modbus_adapter.o obj/
bacnet_adapter.o:  src/bacnet_adapter.c src/bacnet_adapter.h
	$(CC) $(CFLAGS) $(INCLUDE) $(BACNETI) -c src/bacnet_adapter.c;mv bacnet_adapter.o obj/
pup_adapter.o :  src/pup_adapter.h src/pup_adapter.c
	$(CC) $(CFLAGS) $(INCLUDE) -I$(PUPSRC) -c src/pup_adapter.c;mv pup_adapter.o obj/

libhue_adapter.o :  src/libhue_adapter.h src/libhue_adapter.c 
	$(CC) $(CFLAGS) $(INCLUDE) -I$(HUEI) -c src/libhue_adapter.c;mv libhue_adapter.o obj/

b3_gpio_adapter.o :  src/b3_gpio_adapter.h src/b3_gpio_adapter.c 
	$(CC) $(CFLAGS) $(INCLUDE) -I$(HUEI) -c src/b3_gpio_adapter.c;mv b3_gpio_adapter.o obj/

xml_tools.o: src/xml_tools.c src/xml_tools.h
	$(CC) $(CFLAGS) $(INCLUDE) -c src/xml_tools.c;mv xml_tools.o obj/

############### Driver binaries ###############
# Build the driver libraries
#
# libfuse.o:
# 	cd $(LIBFUSE_DIR);make $(LIBFUSE);cd ../..
# modbus.o : drivers/libmodbus
# 	cd $(LIBMODBUS_DIR);./autogen.sh;
# bacnet.o : drivers/
# modbus.o : drivers/libmodbus
# 	cd $(LIBMODBUS_DIR);./autogen.sh;




