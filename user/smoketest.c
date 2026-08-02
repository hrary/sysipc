#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../common/ring.h"

// takes in char buffer and integer n which is the message
// clears the buffer bits for safety??
// copies value of n into the buffer
static void make_msg(unsigned char *buf, int n) {
    memset(buf, 0, SLOT_SIZE); // sets SLOT_SIZE bytes of buf to 0
    memcpy(buf, &n, sizeof n); // copies sizeof n bytes from &n to buf
}

// takes in char buffer and returns value of the integer in the buffer
// reads the otuput basically
static int msg_num(const unsigned char *buf) {
    int n;
    // copies sizeof n bytes from buf to &n
    memcpy(&n, buf, sizeof n);
    return n;
}

int main(void) {
    // static so that it is allocated in the data segment and not on the stack
    // because the ring buffer could be large and we don't want to risk stack overflow
    static struct ring r; 
    unsigned char in[SLOT_SIZE], out[SLOT_SIZE]; // input and output buffers for messages like producers and consumers

    ring_init(&r);

    assert(ring_pop(&r, out) == -1);            // empty check

    for (int i = 0; i < CAPACITY; i++) {        // fills ring buffer
        make_msg(in, i); // puts the value of i into the input buffer in
        assert(ring_push(&r, in) == 0); // pushes value of in into the ring buffer
    }
    make_msg(in, 999);
    assert(ring_push(&r, in) == -1);            // tries to pass a value into a full buffer

    for (int i = 0; i < CAPACITY; i++) {        // drain in order
        assert(ring_pop(&r, out) == 0); // ring_pop returns 0 on success
        assert(msg_num(out) == i); // verifies the bits match
    }
    assert(ring_pop(&r, out) == -1);            // tries to pop from an empty buf

    for (int round = 0; round < 5; round++) {   // wraparound test
        for (int i = 0; i < 3; i++) {
            make_msg(in, round * 3 + i);
            assert(ring_push(&r, in) == 0);
        }
        for (int i = 0; i < 3; i++) {
            assert(ring_pop(&r, out) == 0);
            assert(msg_num(out) == round * 3 + i);
        }
    }

    printf("all tests passed\n");
    return 0;
}