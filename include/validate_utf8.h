/*
 * Copyright 2011 self.disconnect
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * This is the state transition table for validating UTF-8 input.
 *
 * We start in the initial state (s0) and expect to be in that state at the end
 * of the input sequence. If we transition to the error state (e) or end in a
 * state other than end state (s0), we consider that input sequence to be
 * invalid. In an attempt to make the table a little easier to understand,
 * states t1, t2, and t3 are used to signify the various tail states. The
 * number following the t is the number of remaining tail inputs remaining. A
 * tail accepts input in the range of %x80 - %xBF. With valid input, t3
 * transitions to t2, t2 transitions to t1, and t1 transition back to s0.
 *
 * States s1 through s4 are used to handle the more complex intermediary
 * transitions.
 *
 * Here is the ABNF from which the table was derived (see RFC 3629):
 *
 *   UTF8-octets = *( UTF8-char )
 *   UTF8-char   = UTF8-1 / UTF8-2 / UTF8-3 / UTF8-4
 *   UTF8-1      = %x00-7F
 *   UTF8-2      = %xC2-DF UTF8-tail
 *   UTF8-3      = %xE0 %xA0-BF UTF8-tail / %xE1-EC 2( UTF8 - tail ) /
 *                 %xED %x80-9F UTF8-tail / %xEE-EF 2( UTF8 - tail )
 *   UTF8-4      = %xF0 %x90-BF 2( UTF8-tail ) / %xF1-F3 3( UTF8 - tail ) /
 *                 %xF4 %x80-8F 2( UTF8-tail )
 *   UTF8-tail   = %x80-BF
 *
 * start           end
 * state  input   state
 * --------------------
 *  s0 : %x00-7F => s0
 *  s0 : %x80-C1 => e
 *  s0 : %xC2-DF => t1
 *  s0 : %xE0    => s1
 *  s0 : %xE1-EC => t2
 *  s0 : %xED    => s2
 *  s0 : %xEE-EF => t2
 *  s0 : %xF0    => s3
 *  s0 : %xF1-F3 => t3
 *  s0 : %xF4    => s4
 *  s0 : %xF5-FF => e
 *  t1 : %x00-7F => e
 *  t1 : %x80-BF => s0
 *  t1 : %xC0-FF => e
 *  t2 : %x00-7F => e
 *  t2 : %x80-BF => t1
 *  t2 : %xC0-FF => e
 *  s1 : %x00-9F => e
 *  s1 : %xA0-BF => t1
 *  s1 : %xC0-FF => e
 *  s2 : %x00-7F => e
 *  s2 : %x80-9F => t1
 *  s2 : %xA0-FF => e
 *  t3 : %x00-7F => e
 *  t3 : %x80-BF => t2
 *  t3 : %xC0-FF => e
 *  s3 : %x00-8F => e
 *  s3 : %x90-BF => t2
 *  s3 : %xC0-FF => e
 *  s4 : %x00-7F => e
 *  s4 : %x80-8F => t2
 *  s4 : %x90-FF => e
 *
 *  Here is an example of one way to use the UTF-8 validation table to validate
 *  input. The input bytes must be unsigned in order to make sure that the
 *  table is accessed properly. While processing the input sequence, only the
 *  INVALID state corresponds to an error. After proccessing of the input has
 *  been completed, any state other than the VALID state is an error.
 *
 *    unsigned char *input = // pointer to the input sequence
 *    unsigned int state = UTF8_VALID;
 *
 *    while (*input != 0) {
 *      state = validate_utf8[state + *input++];
 *      if (state == UTF8_INVALID) {
 *        break;
 *      }
 *    }
 *    if (state == UTF8_VALID) {
 *      // Valid
 *    } else {
 *      // Invalid
 *    }
 *
 */

#if !defined(_VALIDATE_UTF8_H_)
#define _VALIDATE_UTF8_H_

#define S0 0x000
#define T1 0x100
#define T2 0x200
#define S1 0x300
#define S2 0x400
#define T3 0x500
#define S3 0x600
#define S4 0x700
#define ER 0x800

#define UTF8_VALID   0x000
#define UTF8_INVALID 0x800

static const unsigned short validate_utf8[2048] = {
/* S0 (0x000) */
S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0, /* %x00-0F */
S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0, /* %x10-1F */
S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0, /* %x20-2F */
S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0, /* %x30-3F */
S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0, /* %x40-4F */
S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0, /* %x50-5F */
S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0, /* %x60-6F */
S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0, /* %x70-7F */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x80-8F */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x90-9F */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %xA0-AF */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %xB0-BF */
ER,ER,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1, /* %xC0-CF */
T1,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1, /* %xD0-DF */
S1,T2,T2,T2,T2,T2,T2,T2,T2,T2,T2,T2,T2,S2,T2,T2, /* %xE0-EF */
S3,T3,T3,T3,S4,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %xF0-FF */
/* T1 (0x100) */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x00-0F */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x10-1F */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x20-2F */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x30-3F */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x40-4F */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x50-5F */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x60-6F */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x70-7F */
S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0, /* %x80-8F */
S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0, /* %x90-9F */
S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0, /* %xA0-AF */
S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0,S0, /* %xB0-BF */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %xC0-CF */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %xD0-DF */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %xE0-EF */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %xF0-FF */
/* T2 (0x200) */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x00-0F */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x10-1F */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x20-2F */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x30-3F */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x40-4F */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x50-5F */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x60-6F */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x70-7F */
T1,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1, /* %x80-8F */
T1,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1, /* %x90-9F */
T1,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1, /* %xA0-AF */
T1,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1, /* %xB0-BF */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %xC0-CF */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %xD0-DF */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %xE0-EF */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %xF0-FF */
/* S1 (0x300) */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x00-0F */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x10-1F */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x20-2F */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x30-3F */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x40-4F */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x50-5F */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x60-6F */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x70-7F */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x80-8F */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x90-9F */
T1,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1, /* %xA0-AF */
T1,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1, /* %xB0-BF */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %xC0-CF */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %xD0-DF */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %xE0-EF */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %xF0-FF */
/* S2 (0x400) */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x00-0F */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x10-1F */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x20-2F */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x30-3F */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x40-4F */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x50-5F */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x60-6F */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x70-7F */
T1,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1, /* %x80-8F */
T1,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1,T1, /* %x90-9F */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %xA0-AF */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %xB0-BF */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %xC0-CF */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %xD0-DF */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %xE0-EF */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %xF0-FF */
/* T3 (0x500) */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x00-0F */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x10-1F */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x20-2F */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x30-3F */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x40-4F */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x50-5F */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x60-6F */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x70-7F */
T2,T2,T2,T2,T2,T2,T2,T2,T2,T2,T2,T2,T2,T2,T2,T2, /* %x80-8F */
T2,T2,T2,T2,T2,T2,T2,T2,T2,T2,T2,T2,T2,T2,T2,T2, /* %x90-9F */
T2,T2,T2,T2,T2,T2,T2,T2,T2,T2,T2,T2,T2,T2,T2,T2, /* %xA0-AF */
T2,T2,T2,T2,T2,T2,T2,T2,T2,T2,T2,T2,T2,T2,T2,T2, /* %xB0-BF */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %xC0-CF */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %xD0-DF */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %xE0-EF */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %xF0-FF */
/* S3 (0x600) */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x00-0F */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x10-1F */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x20-2F */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x30-3F */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x40-4F */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x50-5F */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x60-6F */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x70-7F */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x80-8F */
T2,T2,T2,T2,T2,T2,T2,T2,T2,T2,T2,T2,T2,T2,T2,T2, /* %x90-9F */
T2,T2,T2,T2,T2,T2,T2,T2,T2,T2,T2,T2,T2,T2,T2,T2, /* %xA0-AF */
T2,T2,T2,T2,T2,T2,T2,T2,T2,T2,T2,T2,T2,T2,T2,T2, /* %xB0-BF */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %xC0-CF */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %xD0-DF */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %xE0-EF */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %xF0-FF */
/* S4 (0x700) */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x00-0F */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x10-1F */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x20-2F */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x30-3F */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x40-4F */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x50-5F */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x60-6F */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x70-7F */
T2,T2,T2,T2,T2,T2,T2,T2,T2,T2,T2,T2,T2,T2,T2,T2, /* %x80-8F */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %x90-9F */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %xA0-AF */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %xB0-BF */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %xC0-CF */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %xD0-DF */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %xE0-EF */
ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER,ER, /* %xF0-FF */
};

#undef S0
#undef T1
#undef T2
#undef S1
#undef S2
#undef T3
#undef S3
#undef S4
#undef ER

#endif /* _VALIDATE_UTF8_H_ */
