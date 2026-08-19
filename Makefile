CC = clang
CFLAGS = -O2 -Wall -Wextra -I. -Idev -mmacosx-version-min=11.0
LDFLAGS = -framework Hypervisor -pthread -mmacosx-version-min=11.0

TARGET = watercheese
ENTITLEMENTS = watercheese.entitlements

OBJS = main.o io.o vcpu.o dev/uart.o

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)
	codesign --entitlements $(ENTITLEMENTS) --force -s - $(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)

entitlements: $(TARGET)
	codesign -d --entitlements :- ./$(TARGET)

.PHONY: clean entitlements