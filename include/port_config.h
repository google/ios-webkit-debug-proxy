// Google BSD license https://developers.google.com/google-bsd-license
// Copyright 2012 Google Inc. wrightt@google.com

//
// device_id-to-port(s) config file reader.
//

#ifndef PORT_CONFIG_H
#define	PORT_CONFIG_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdlib.h>


struct pc_struct;
typedef struct pc_struct *pc_t;

pc_t pc_new();
void pc_free(pc_t self);

void pc_clear(pc_t self);

// Add a rule.
void pc_add(pc_t self, const char *device_id, int min_port, int max_port);

// Parse a line and add all rules.
//
// Lines should match:
//    [ITEM ("," ITEM)*] ["#" comment]
// where each ITEM is:
//    [40-char-hex or "*" or "null"] [" " or ":""] min_port[-max_port]
//
// Examples:
//   # comments and blank lines are ignored
//   4ea8dd11e8c4fbc1a2deadbeefa0fd3bbbb268c7:9227
//   4ea8dd11e8c4fbc1a2deadbeefa0fd3bbbb268c7 9227   # same as above
//   ddc86a51827948e13bdeadbeef5bc588ea35fcf2:9225-9340  # scan range
//   007007deadbeefe724327890fda98434dabcdeff:9229-9229  # same as 9229
//   4223489deadbeef123478432098342039abcdabc:9322  # iphoneX
//   123478934adcee0000000000000000000000000c:-1  # explicit ignore
//   null:9221   # sets the "9221" device list
//   *:9222-9299 # default to scan
//   * 9222-9299 # same as above
//   :9222-9299  # same as above
// Example with comma:
//   4ea8dd11e8c4fbc1a2deadbeefa0fd3bbbb268c7:9227,:9222-9299
//
// @result NULL if success, else pointer in line of first invalid item
const char *pc_add_line(pc_t self, const char *line, size_t len);

// Calls pc_add_line for every line in a file.
// @param filename path
// @result 0 if success
int pc_add_file(pc_t self, const char *filename);

// Looks up the device_id and sets the to_*ports.
int pc_select_port(pc_t self, const char *device_id,
                   int *to_port, int *to_min_port, int *to_max_port);


#ifdef	__cplusplus
}
#endif

#endif	/* PORT_CONFIG_H */

