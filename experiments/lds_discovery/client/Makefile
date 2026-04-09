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
CFLAGS  = -Wall -Wextra -std=c99
LDFLAGS = -pthread

TARGET  = discovery_client

SRCS    = main.c browse.c discovery.c helpers.c model.c  open62541.c
OBJS    = $(SRCS:.c=.o)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Dipendenze esplicite sugli header
main.o:      main.c common.h helpers.h discovery.h browse.h model.h
browse.o:    browse.c browse.h helpers.h common.h
discovery.o: discovery.c discovery.h common.h
helpers.o:   helpers.c helpers.h common.h
model.o:     model.c model.h common.h
open62541.o: open62541.c open62541.h
 
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<
 
clean:
	rm -f $(OBJS) $(TARGET)
 
