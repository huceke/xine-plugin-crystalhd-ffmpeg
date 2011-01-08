#ifndef BITSREADEER_H
#define BITSREADEER_H

#include <stdint.h>

typedef struct {
  uint8_t *buffer, *start;
  int      offbits, length, oflow;
} bits_reader_t;

void bits_reader_set( bits_reader_t *br, uint8_t *buf, int len );
uint32_t read_bits( bits_reader_t *br, int nbits );
void skip_bits( bits_reader_t *br, int nbits );
uint32_t get_bits( bits_reader_t *br, int nbits );

#endif
