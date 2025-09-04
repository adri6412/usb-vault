CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c99
LDFLAGS = -lssl -lcrypto -lsqlite3 -ljansson

TARGET = vaultusb
SOURCES = vaultusb.c

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCES) $(LDFLAGS)

clean:
	rm -f $(TARGET)

install: $(TARGET)
	mkdir -p /usr/local/bin
	cp $(TARGET) /usr/local/bin/
	chmod +x /usr/local/bin/$(TARGET)
	mkdir -p /opt/vaultusb
	touch /opt/vaultusb/vault.db
	chmod 666 /opt/vaultusb/vault.db
