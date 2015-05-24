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

struct Frame buffer[WINDOW_SIZE];
int length[WINDOW_SIZE];
unsigned int frame_tobe_sent = 0, ack_tobe_expected = 0;
unsigned int expected_frame = 0;
unsigned int physical_ready = 0;

unsigned int bufferHead = 0;

static void put_frame(unsigned char *frame, int len) {
	*(unsigned int *)(frame + len) = crc32(frame, len);
	send_frame(frame, len + 4);
	physical_ready = 0;
}


int send_data_frame() {
	/*if (ack_tobe_expected < frame_tobe_sent && bufferHead > frame_tobe_sent) return 0;
	if (ack_tobe_expected > frame_tobe_sent && bufferHead < ack_tobe_expecackted) return 0;
	if (buffer[bufferHead].kind != FRAME_DATA) return 0;*/

	if (((ack_tobe_expected <= bufferHead) && (bufferHead < frame_tobe_sent)) || ((ack_tobe_expected <= bufferHead) && (frame_tobe_sent < ack_tobe_expected)) || ((ack_tobe_expected > frame_tobe_sent) && (bufferHead < frame_tobe_sent))){
		put_frame((unsigned char *)&buffer[bufferHead], 3 + length[bufferHead]);
		start_timer(bufferHead, DATA_TIMER);

		log_printf("SEND DATA %d %d\n", bufferHead, *(short *)buffer[bufferHead].data);


		buffer[bufferHead].kind = -1;
		bufferHead = (bufferHead + 1) % WINDOW_SIZE;
		return 1;

	}
	else
		return 0;
	
}

void send_ack_frame(int seq) {
	struct Frame s;

	s.kind = FRAME_ACK;
	s.ack = seq;
	
	log_printf("SEND ACK %d\n", seq);
	put_frame((unsigned char *)&s, 2);
}

void send_nak_frame() {
	struct Frame s;

	s.kind = FRAME_NAK;

	log_printf("SEND NAK\n");
	put_frame((unsigned char *)&s, 2);
}

int main(int argc, char **argv) {

	int event = 0, arg, i, len, seq;
	unsigned char packet[PKT_LEN];
	struct Frame *p;
	struct Frame f;

	protocol_init(argc, argv);
	lprintf("Written by lcj,R build: " __DATE__"  "__TIME__"\n");

	enable_network_layer();

	while (1) {
		event = wait_for_event(&arg);

		switch (event) {
		case PHYSICAL_LAYER_READY:
			physical_ready = 1;
			send_data_frame();
			break;

		case NETWORK_LAYER_READY:
			len = get_packet(packet);

			//½«°üÑ¹Èë»º´æ
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
			//log_printf("%d", phl_sq_len());

			break;

		case FRAME_RECEIVED:
			len = recv_frame((unsigned char *)&f, sizeof f);

			if (len < 5 || crc32((unsigned char *)&f, len) != 0) {
				dbg_event("**** Receiver Error, Bad CRC Checksum\n");

				send_nak_frame(-1);

				break;
			}

			if (f.kind == FRAME_ACK) {
				log_printf("RECV ACK %d\n", f.ack);
				if (f.ack == ack_tobe_expected) {
					stop_timer(ack_tobe_expected);
					ack_tobe_expected = (ack_tobe_expected + 1) % WINDOW_SIZE;
				}
				else {
					for (i = ack_tobe_expected; i != bufferHead; i = (i + 1) % WINDOW_SIZE) {
						buffer[i].kind = FRAME_DATA;
						stop_timer(i);
					}
					bufferHead = ack_tobe_expected;
				}
			}

			if (f.kind == FRAME_DATA) {
				
				log_printf("RECV DATA %d\n", f.seq);
				if (f.seq == expected_frame) {
					send_ack_frame(f.seq);
					put_packet(f.data, len - 7);
					expected_frame = (expected_frame + 1) % WINDOW_SIZE;
				}
				else{
					if ((f.seq == expected_frame - 1) || ((f.seq == WINDOW_SIZE - 1) && (expected_frame == 0))){
						send_ack_frame(f.seq);
					}
				}
			}

			if (f.kind == FRAME_NAK) {
				dbg_event("---- DATA %d received a nak\n", arg);

				log_printf("RECV NAK\n");
				for (i = ack_tobe_expected; i != bufferHead; i = (i + 1) % WINDOW_SIZE) {
					buffer[i].kind = FRAME_DATA;
					stop_timer(i);
				}
				bufferHead = ack_tobe_expected;
			}
			break;

		case DATA_TIMEOUT:
			dbg_event("---- DATA %d timeout\n", arg);

			for (i = ack_tobe_expected; i != bufferHead; i = (i + 1) % WINDOW_SIZE) {
				buffer[i].kind = FRAME_DATA;
				stop_timer(i);
			}
			bufferHead = ack_tobe_expected;
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
