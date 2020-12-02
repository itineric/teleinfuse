/*
 * teleinfuse is a FUSE module to access to the Télé information of linky electric meter running in standard mode
 * Télé info data are transmitted by french electric meters (EDF/ERDF)
 * [FR] Permet de lire la téléinformation cliente (TIC) d'un compteur linky en mode standard.
 * [FR] Pour le mode TIC historique, voir le projet original
 *
 * Based on https://github.com/neomilium/teleinfuse project by Romuald Conty
 *
 * Copyright (C) 2020 itineric
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _TELEINFO_H_
#define _TELEINFO_H_

#include <sys/types.h>
typedef struct {
  char label[9];
  char datetime[14];
  char value[99];
} teleinfo_data;

#define TI_MESSAGE_COUNT_MAX 71
// Start + end message
// Total max etiquette size: 395
// Total max datetime size: 24 messages * 13 (datetime size) = 312
// Total max data size: 639
// one LF + one CR per line (TI_MESSAGE_COUNT_MAX * 2)
// Two tabs per non datetimed message (47 messages * 2)
// Three tabs per non datetimed message (24 messages * 3)
// 1 checksum char per message
#define TI_FRAME_LENGTH_MAX (2 + 395 + 312 + 639 + (TI_MESSAGE_COUNT_MAX * 2) + (47 * 2) + (24 * 3) + TI_MESSAGE_COUNT_MAX)

#define DATETIME_FILENAME_SUFFIX ".datetime"

// returns file descriptor if succeed otherwise 0
int teleinfo_open (const char * port);

// returns 0 if succeed otherwise negative
#define teleinfo_read_frame(X, Y, Z) teleinfo_read_frame_ext(X, Y, Z, NULL)
int teleinfo_read_frame_ext (const int fd, char *const buffer, const size_t buflen, int *error_counter);

// returns 0 if succeed otherwise negative
int teleinfo_decode (const char * frame, teleinfo_data dataset[], size_t * datasetlen);

void teleinfo_close (int fd);

#endif
