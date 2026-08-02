CFLAGS := -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -O2
ifdef CAP
CFLAGS += -DCAPACITY=$(CAP)
endif

BINS := producer consumer

all: $(BINS)

%: %.c ../common/ring.h
	gcc $(CFLAGS) $< -o $@

clean:
	rm -f $(BINS)

.PHONY: all clean