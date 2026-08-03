CC       := clang
TARGET   := watercheese
SRCS     := main.c io.c
OBJS     := $(SRCS:.c=.o)
HDRS     := $(wildcard *.h)

ENTITLEMENTS := watercheese.entitlements

CFLAGS   := -O2 -mmacosx-version-min=11.0
LDFLAGS  := -mmacosx-version-min=11.0
LDLIBS   := -framework Hypervisor

.PHONY: all clean sign

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)
	codesign --entitlements $(ENTITLEMENTS) --force -s - $@
	codesign -d --entitlements :- ./$@

# Every TU here includes most of the headers; just rebuild all objects
# if any header changes, rather than generating per-file .d files.
$(OBJS): $(HDRS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(TARGET) $(OBJS)
