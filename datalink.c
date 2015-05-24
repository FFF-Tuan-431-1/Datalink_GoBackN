#include <stdio.h>
#include <string.h>

#include "protocol.h"
#include "datalink.h"

struct Frame {
    unsigned char kind;
    unsigned char ack;
    unsigned char seq;
    unsigned char data[PKT_LEN];
    unsigned int padding;
};

#define WINDOW_SIZE 10
#define DATA_TIMER 2000

static struct Frame buffer[WINDOW_SIZE];
static int length[WINDOW_SIZE];
static unsigned int frame_tobe_sent = 0, ack_tobe_expected = 0, bufferHead = 0;
static unsigned int expected_frame = 0;

static void put_frame(unsigned char *frame, int len) {
    *(unsigned int *) (frame + len) = crc32(frame, len);
    send_frame(frame, len + 4);
}


static void send_data_frame() {
    int i;
    if (((ack_tobe_expected <= bufferHead) && (bufferHead < frame_tobe_sent)) ||
        ((ack_tobe_expected <= bufferHead) && (frame_tobe_sent < ack_tobe_expected)) ||
        ((ack_tobe_expected > frame_tobe_sent) && (bufferHead < frame_tobe_sent))) {
        put_frame((unsigned char *) &buffer[bufferHead], 3 + length[bufferHead]);
        start_timer(bufferHead, DATA_TIMER);

        dbg_event("SEND DATA %d %d\n", bufferHead, *(short *) buffer[bufferHead].data);


        buffer[bufferHead].kind = -1;
        bufferHead = (bufferHead + 1) % WINDOW_SIZE;
    }
    else {
        for (i = ack_tobe_expected; i != bufferHead; i = (i + 1) % WINDOW_SIZE) {
            buffer[i].kind = FRAME_DATA;
            stop_timer(i);
        }
        bufferHead = ack_tobe_expected;
    }
}

static void send_ack_frame(int seq) {
    struct Frame s;

    s.kind = FRAME_ACK;
    s.ack = seq;

    dbg_event("SEND ACK %d\n", seq);
    put_frame((unsigned char *) &s, 2);
}

static void send_nak_frame() {
    struct Frame s;

    s.kind = FRAME_NAK;

    dbg_event("SEND NAK\n");
    put_frame((unsigned char *) &s, 2);
}

int main(int argc, char **argv) {

    int event = 0, arg, i, len;
    unsigned char packet[PKT_LEN];
    struct Frame *p;
    struct Frame f;

    protocol_init(argc, argv);
    lprintf("Written by lcj,R build: " __DATE__"  "__TIME__"\n");

    enable_network_layer();

    while (1) {
        event = wait_for_event(&arg);

        dbg_event("A: %d, F: %d, B: %d\n", ack_tobe_expected, frame_tobe_sent, bufferHead);
        switch (event) {
            case PHYSICAL_LAYER_READY:
                send_data_frame();
                break;

            case NETWORK_LAYER_READY:
                len = get_packet(packet);

                p = &buffer[frame_tobe_sent];
                p->ack = 0;
                p->kind = FRAME_DATA;
                p->seq = frame_tobe_sent;
                memcpy(p->data, packet, len);

                length[frame_tobe_sent] = len;
                frame_tobe_sent = (frame_tobe_sent + 1) % WINDOW_SIZE;

                if (phl_sq_len() < 50) {
                    send_data_frame();
                }

                break;

            case FRAME_RECEIVED:
                len = recv_frame((unsigned char *) &f, sizeof f);

                if (len < 5 || crc32((unsigned char *) &f, len) != 0) {
                    dbg_event("**** Receiver Error, Bad CRC Checksum\n");

                    send_nak_frame();

                    break;
                }

                if (f.kind == FRAME_ACK) {
                    dbg_event("RECV ACK %d\n", f.ack);
                    if (f.ack == ack_tobe_expected) {
                        stop_timer(ack_tobe_expected);
                        ack_tobe_expected = (ack_tobe_expected + 1) % WINDOW_SIZE;
                    }
                    else if (f.ack == ack_tobe_expected - 1) {
                        break;
                    } else {
                        for (i = ack_tobe_expected; i != bufferHead; i = (i + 1) % WINDOW_SIZE) {
                            buffer[i].kind = FRAME_DATA;
                            stop_timer(i);
                        }
                        bufferHead = ack_tobe_expected;
                    }
                }

                if (f.kind == FRAME_DATA) {

                    dbg_event("RECV DATA %d %d\n", f.seq, *(short *) f.data);
                    if (f.seq == expected_frame) {
                        send_ack_frame(f.seq);
                        send_ack_frame(f.seq);
                        send_ack_frame(f.seq);
                        put_packet(f.data, len - 7);
                        expected_frame = (expected_frame + 1) % WINDOW_SIZE;
                    }
                }

                if (f.kind == FRAME_NAK) {
                    dbg_event("---- DATA %d received a nak\n", arg);

                    dbg_event("RECV NAK\n");
                    for (i = ack_tobe_expected; i != bufferHead; i = (i + 1) % WINDOW_SIZE) {
                        buffer[i].kind = FRAME_DATA;
                        stop_timer(i);
                    }
                    bufferHead = ack_tobe_expected;
                }
                break;

            case DATA_TIMEOUT:
                dbg_event("---- DATA %d timeout\n", arg);

                if (arg > WINDOW_SIZE) {
                    send_ack_frame(arg - WINDOW_SIZE);
                }
                else {
                    for (i = ack_tobe_expected; i != bufferHead; i = (i + 1) % WINDOW_SIZE) {
                        buffer[i].kind = FRAME_DATA;
                        stop_timer(i);
                    }
                    bufferHead = ack_tobe_expected;
                }
                break;

            default:
                break;
        }

        if ((frame_tobe_sent + 1) % WINDOW_SIZE == ack_tobe_expected) {
            disable_network_layer();
        }
        else {
            enable_network_layer();
        }

    }
}
