CFLAGS := -std=c11 -Wall -Wextra -O2
ifdef CAP
CFLAGS += -DCAPACITY=$(CAP)
endif

BINS := smoketest producer consumer

all: $(BINS)

%: %.c ../common/ring.h
	gcc $(CFLAGS) $< -o $@

clean:
	rm -f $(BINS)

.PHONY: all clean