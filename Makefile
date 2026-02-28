CC = gcc

CFLAGS = -g -Wall -I/opt/homebrew/include -I/usr/local/include
LIBS = -L/opt/homebrew/lib -lcjson -lssl -lcrypto

TARGET = client
SRCS = client.c

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS) $(LIBS)

clean:
	rm -f $(TARGET)
