# ============================================================
# Makefile - UAFX Discovery Client
#
# Prerequisito: open62541.c e open62541.h nella stessa directory
#
# Uso:
#   make            → compila discovery_client
#   make clean      → rimuove i file generati
# ============================================================

CC      = gcc
CFLAGS  = -Wall -std=gnu99
LDFLAGS = -pthread

TARGET  = backend

SRCS    = main.c browse.c discovery.c helpers.c model.c tde.c \
          link_builder.c serialize.c http_server.c \
          cJSON.c mongoose.c open62541.c
OBJS    = $(SRCS:.c=.o)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Dipendenze esplicite sugli header
main.o:         main.c common.h model.h http_server.h
browse.o:       browse.c browse.h helpers.h common.h model.h
discovery.o:    discovery.c discovery.h common.h
helpers.o:      helpers.c helpers.h common.h
model.o:        model.c model.h common.h
tde.o:          tde.c tde.h model.h common.h cJSON.h
link_builder.o: link_builder.c link_builder.h model.h
serialize.o:    serialize.c serialize.h model.h cJSON.h
http_server.o:  http_server.c http_server.h serialize.h mongoose.h \
                discovery.h browse.h tde.h link_builder.h model.h
cJSON.o:        cJSON.c cJSON.h
	$(CC) -std=c99 -Wno-unused-parameter -c -o $@ $<
mongoose.o:     mongoose.c mongoose.h
	$(CC) -std=gnu99 -Wno-unused-parameter -Wno-unused-function -c -o $@ $<
open62541.o:    open62541.c open62541.h

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)
