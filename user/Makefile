CFLAGS := -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -O2
ifdef CAP
CFLAGS += -DCAPACITY=$(CAP)
endif
ifdef NO_PADDING
CFLAGS += -DNO_PADDING
endif

BINS := producer consumer bench bench_sockets ktest producer_dev consumer_dev bench_dev

all: $(BINS)

%: %.c ../common/ring.h
	gcc $(CFLAGS) $< -o $@

clean:
	rm -f $(BINS)

.PHONY: all clean