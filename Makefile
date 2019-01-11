CC = gcc

CFLAGS = -g -ggdb
LDFLAGS =

TARGET1 = mydhcpc
SRCS1 = dhcpcli.c dhcp.c my_socket.c
OBJS1 =  dhcpcli.o dhcp.o my_socket.o

TARGET2 = mydhcps
SRCS2 = dhcpsrv.c server.c my_socket.c dhcp.c
OBJS2 = dhcpsrv.o server.o my_socket.o dhcp.o

RM = rm -f

all: $(TARGET1) $(TARGET2)

$(TARGET1): $(OBJS1)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

$(TARGET2): $(OBJS2)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

.c.o:
	$(CC) $(CFLAGS) -c $<

dhcpcli.o: utility.h my_socket.h dhcp.h
dhcpsrv.o: server.h utility.h my_socket.h dhcp.h
dhcp.o: dhcp.h
my_socket.o: my_socket.h
server.o: utility.h my_socket.h dhcp.h

clean:
	$(RM) $(OBJS1) $(OBJS2)

clean_target:
	$(RM) $(TARGET1) $(TARGET2)

clean_all:
	$(RM) $(TARGET1) $(OBJS1) $(TARGET2) $(OBJS2)
