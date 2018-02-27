SRCDIR := src
INCDIR := include
OBJDIR := obj
BINDIR := bin

CC := gcc
CCFLAGS  := -Wall -c
INCLUDES := -I$(INCDIR)
LIBS := -lrt -lpthread

# Executables and libraries
CLIENT := $(BINDIR)/gtipc-client
SERVER := $(BINDIR)/gtipc-server
API := $(BINDIR)/libgtipc.a

.PHONY: all api client server clean

all: api client server

client: $(CLIENT)

server: $(SERVER)

api: $(API)

# gtipc API library target
$(API): $(OBJDIR)/gtipc_api.o | $(BINDIR)
	ar rcs $(API) $(OBJDIR)/gtipc_api.o

# gtipc client target
$(CLIENT): $(OBJDIR)/client.o | $(BINDIR) # '|' means ignore the BINDIR when using $^
	$(CC) -L$(BINDIR) $^ -o $@ -lgtipc $(LIBS)

# gtipc server target
$(SERVER): $(OBJDIR)/gtipc_server.o | $(BINDIR)
	$(CC) $^ -o $@ $(LIBS)

$(OBJDIR)/client.o: $(API) sample/client.c | $(OBJDIR)
	$(CC) $(CCFLAGS) $(INCLUDES) sample/client.c -o $@

$(OBJDIR)/gtipc_api.o: $(SRCDIR)/api/gtipc_api.c $(INCDIR)/gtipc_api.h | $(OBJDIR)
	$(CC) $(CCFLAGS) $(INCLUDES) $(SRCDIR)/api/gtipc_api.c -o $@ $(LIBS)

$(OBJDIR)/gtipc_server.o: $(SRCDIR)/server/gtipc_server.c $(SRCDIR)/server/gtipc_server.h | $(OBJDIR)
	$(CC) $(CCFLAGS) $(SRCDIR)/server/gtipc_server.c -o $@

# If BINDIR or OBJDIR do not exist, create them
$(BINDIR):
	mkdir -p $@

$(OBJDIR):
	mkdir -p $@

clean:
	rm $(OBJDIR)/*.o
	rm $(BINDIR)/*
