# ============================================================
# Makefile - UAFX Framework Backend
#
# Prerequisiti:
#   - open62541.c e open62541.h nella stessa directory
#   - tipi UAFX generati in ../../lib/uafx_types/
#
# Uso:
#   make          → compila backend
#   make clean    → rimuove i file generati
# ============================================================

CC      = gcc

# Directory dei tipi UAFX generati dal nodeset compiler
UAFX_TYPES_DIR = ../../../lib/uafx_types
OPEN62541_DIR = ../../../lib/

CFLAGS  = -Wall -std=gnu99 -DUA_ENABLE_AMALGAMATION -I$(UAFX_TYPES_DIR) -I$(OPEN62541_DIR)
LDFLAGS = -pthread

TARGET  = backend

# Sorgenti del backend
SRCS    = main.c browse.c discovery.c helpers.c model.c tde.c \
          link_builder.c serialize.c http_server.c \
          connection_manager.c \
          cJSON.c mongoose.c open62541.c

# Sorgenti generati — compilati separatamente e linkati
GENERATED_SRCS = $(UAFX_TYPES_DIR)/my_uafx_model.c \
                 $(UAFX_TYPES_DIR)/types_di_generated.c \
                 $(UAFX_TYPES_DIR)/types_uafx_data_generated.c \
                 $(UAFX_TYPES_DIR)/types_uafx_ac_generated.c

OBJS    = $(SRCS:.c=.o)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(GENERATED_SRCS) $(LDFLAGS)

# ── Dipendenze esplicite ──────────────────────────────────────

main.o:         main.c common.h model.h http_server.h
browse.o:       browse.c browse.h helpers.h common.h model.h
discovery.o:    discovery.c discovery.h common.h
helpers.o:      helpers.c helpers.h common.h
model.o:        model.c model.h common.h
tde.o:          tde.c tde.h model.h common.h cJSON.h
link_builder.o: link_builder.c link_builder.h model.h
serialize.o:    serialize.c serialize.h model.h cJSON.h
http_server.o:  http_server.c http_server.h serialize.h mongoose.h \
                discovery.h browse.h tde.h link_builder.h \
                connection_manager.h model.h

connection_manager.o: connection_manager.c connection_manager.h \
                      model.h common.h \
                      $(UAFX_TYPES_DIR)/types_uafx_data_generated.h \
                      $(UAFX_TYPES_DIR)/types_uafx_ac_generated.h \
                      $(UAFX_TYPES_DIR)/my_uafx_model.h

cJSON.o: cJSON.c cJSON.h
	$(CC) -std=c99 -Wno-unused-parameter -c -o $@ $<

mongoose.o: mongoose.c mongoose.h
	$(CC) -std=gnu99 -Wno-unused-parameter -Wno-unused-function -c -o $@ $<

open62541.o: open62541.c open62541.h

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<
# ── cm_test ───────────────────────────────────────────────────
CM_TEST_SRCS = cm_test.c browse.c discovery.c helpers.c model.c tde.c \
               link_builder.c serialize.c connection_manager.c \
               cJSON.c mongoose.c open62541.c

CM_TEST_OBJS = $(CM_TEST_SRCS:.c=.o)

cm_test: $(CM_TEST_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(GENERATED_SRCS) $(LDFLAGS)

clean:
	rm -f $(OBJS) $(CM_TEST_OBJS) $(TARGET) cm_test
clean:
	rm -f $(OBJS) $(TARGET)
