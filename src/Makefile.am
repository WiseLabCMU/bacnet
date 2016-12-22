MIO_ROOT=../
miodir = $(includedir)
include_HEADERS = mio.h 
mio_HEADERS = mio_error.h mio_collection.h mio_geolocation.h mio_meta.h \
	      mio_node.h mio_packet.h mio_pubsub.h mio_reference.h \
	      mio_schedule.h mio_transducer.h mio_user.h \
	      mio_affiliations.h mio_connection.h mio_handlers.h
noinst_HEADERS = ../libs/libstrophe/src/common.h
lib_LIBRARIES = libmio.a
libmio_a_SOURCES = mio_connection.c mio_handlers.c mio_meta.c  \
		   mio_transducer.c mio_affiliations.c mio_node.c \
		   mio_reference.c mio_geolocation.c mio_schedule.c \
		   mio_collection.c mio_pubsub.c mio_transducer.c \
		   mio_user.c mio_error.c mio_packet.c 
libmio_a_CPPFLAGS = -Wall -g3 -I ../libs/libstrophe/ -I ../libs/libstrophe/src
lbimio_a_AR = ar
lbimio_a_ARFLAGS = rcs 
