/*

    File: intrf.c

    Copyright (C) 1998-2009 Christophe GRENIER <grenier@cgsecurity.org>

    This software is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write the Free Software Foundation, Inc., 51
    Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
 
#include <stdio.h>
#include <stdarg.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#include <ctype.h>
#ifdef HAVE_TIME_H
#include <time.h>
#endif
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_SYS_CYGWIN_H
#include <sys/cygwin.h>
#endif
#ifdef HAVE_LIBGEN_H
#include <libgen.h>
#endif
#include <errno.h>
#include "types.h"
#include "common.h"
#include "lang.h"
#include "intrf.h"
#include "fnctdsk.h"
#include "list.h"
#include "dir.h"
#include "log.h"

char intr_buffer_screen[MAX_LINES][BUFFER_LINE_LENGTH+1];
int intr_nbr_line=0;

int screen_buffer_add(const char *_format, ...)
{
  char tmp_line[BUFFER_LINE_LENGTH+1];
  char *pos_in_tmp_line=tmp_line;
  va_list ap;
  memset(tmp_line, '\0', sizeof(tmp_line));
  va_start(ap,_format);
  vsnprintf(tmp_line, sizeof(tmp_line), _format, ap);
  va_end(ap);
  while(pos_in_tmp_line!=NULL && (intr_nbr_line<MAX_LINES))
  {
    const unsigned int len=strlen(intr_buffer_screen[intr_nbr_line]);
    unsigned int nbr=BUFFER_LINE_LENGTH-len;
    char *ret_ligne= strchr(pos_in_tmp_line,'\n');
    if(ret_ligne!=NULL && ret_ligne-pos_in_tmp_line < nbr)
      nbr=ret_ligne-pos_in_tmp_line;
    memcpy(&intr_buffer_screen[intr_nbr_line][len], pos_in_tmp_line, nbr);
    intr_buffer_screen[intr_nbr_line][len+nbr]='\0';
    if(ret_ligne!=NULL)
    {
      if(++intr_nbr_line<MAX_LINES)
	intr_buffer_screen[intr_nbr_line][0]='\0';
      ret_ligne++;
    }
    pos_in_tmp_line=ret_ligne;
  }
  /*	log_trace("aff_intr_buffer_screen %d =>%s<=\n",intr_nbr_line,tmp_line); */
  if(intr_nbr_line==MAX_LINES)
  {
    log_warning("Buffer can't store more than %d lines.\n", MAX_LINES);
    intr_nbr_line++;
  }
  return 0;
}

void screen_buffer_reset(void)
{
  intr_nbr_line=0;
  memset(intr_buffer_screen, 0, sizeof(intr_buffer_screen));
}

void screen_buffer_to_log(void)
{
  int i;
  if(intr_buffer_screen[intr_nbr_line][0]!='\0')
    intr_nbr_line++;
  /* to log file */
  for(i=0;i<intr_nbr_line;i++)
    log_info("%s\n",intr_buffer_screen[i]);
}

int get_partition_status(const partition_t *partition)
{
  switch(partition->status)
  {
    case STATUS_PRIM:           return 'P';
    case STATUS_PRIM_BOOT:      return '*';
    case STATUS_EXT:            return 'E';
    case STATUS_EXT_IN_EXT:     return 'X';
    case STATUS_LOG:            return 'L';
    case STATUS_DELETED:        return 'D';
    default:			return ' ';
  }
}

const char *aff_part_aux(const unsigned int newline, const disk_t *disk_car, const partition_t *partition)
{
  char status=' ';
  static char msg[200];
  unsigned int pos=0;
  const arch_fnct_t *arch=partition->arch;
  if(arch==NULL)
  {
    log_error("BUG: No arch for a partition\n");
    msg[0]='\0';
    return msg;
  }
  msg[sizeof(msg)-1]=0;
  if((newline&AFF_PART_ORDER)==AFF_PART_ORDER)
  {
    if(partition->status!=STATUS_EXT_IN_EXT && partition->order!=NO_ORDER)
      pos+=snprintf(&msg[pos],sizeof(msg)-pos-1,"%2u ", partition->order);
    else
      pos+=snprintf(&msg[pos],sizeof(msg)-pos-1,"   ");
  }
  if((newline&AFF_PART_STATUS)==AFF_PART_STATUS)
  {
    status=get_partition_status(partition);
    /* Don't marked as D(eleted) an entry that is not a partition */
    if((newline&AFF_PART_ORDER)==AFF_PART_ORDER &&
	partition->order==NO_ORDER && partition->status==STATUS_DELETED)
      status=' ';
  }
  pos+=snprintf(&msg[pos],sizeof(msg)-pos-1,"%c", status);
  if(arch->get_partition_typename(partition)!=NULL)
    pos+=snprintf(&msg[pos],sizeof(msg)-pos-1, " %-20s ",
        arch->get_partition_typename(partition));
  else if(arch->get_part_type)
    pos+=snprintf(&msg[pos],sizeof(msg)-pos-1, " Sys=%02X               ", arch->get_part_type(partition));
  else
    pos+=snprintf(&msg[pos],sizeof(msg)-pos-1, " Unknown              ");
  if(disk_car->unit==UNIT_SECTOR)
  {
    pos+=snprintf(&msg[pos],sizeof(msg)-pos-1, " %10llu %10llu ",
        (long long unsigned)(partition->part_offset/disk_car->sector_size),
        (long long unsigned)((partition->part_offset+partition->part_size-1)/disk_car->sector_size));
  }
  else
  {
    pos+=snprintf(&msg[pos],sizeof(msg)-pos-1,"%5u %3u %2u %5u %3u %2u ",
        offset2cylinder(disk_car,partition->part_offset),
        offset2head(    disk_car,partition->part_offset),
        offset2sector(  disk_car,partition->part_offset),
        offset2cylinder(disk_car,partition->part_offset+partition->part_size-1),
        offset2head(    disk_car,partition->part_offset+partition->part_size-1),
        offset2sector(  disk_car,partition->part_offset+partition->part_size-1));
  }
  pos+=snprintf(&msg[pos],sizeof(msg)-pos-1,"%10llu", (long long unsigned)(partition->part_size/disk_car->sector_size));
  if(partition->partname[0]!='\0')
    pos+=snprintf(&msg[pos],sizeof(msg)-pos-1, " [%s]",partition->partname);
  if(partition->fsname[0]!='\0')
    snprintf(&msg[pos],sizeof(msg)-pos-1, " [%s]",partition->fsname);
  return msg;
}

#define PATH_SEP '/'
#if defined(__CYGWIN__)
/* /cygdrive/c/ => */
#define PATH_DRIVE_LENGTH 9
#endif

unsigned long long int ask_number_cli(char **current_cmd, const unsigned long long int val_cur, const unsigned long long int val_min, const unsigned long long int val_max, const char * _format, ...)
{
  if(*current_cmd!=NULL)
  {
    unsigned long int tmp_val;
    while(*current_cmd[0]==',')
      (*current_cmd)++;
#ifdef HAVE_ATOLL
      tmp_val = atoll(*current_cmd);
#else
      tmp_val = atol(*current_cmd);
#endif
    while(*current_cmd[0]!=',' && *current_cmd[0]!='\0')
      (*current_cmd)++;
    if (val_min==val_max || (tmp_val >= val_min && tmp_val <= val_max))
      return tmp_val;
    else
    {
      char res[200];
      va_list ap;
      va_start(ap,_format);
      vsnprintf(res,sizeof(res),_format,ap);
      log_error("%s", res);
      if(val_min!=val_max)
	log_error("(%llu-%llu) :",val_min,val_max);
      log_error("Invalid value\n");
      va_end(ap);
    }
  }
  return val_cur;
}

void aff_part_buffer(const unsigned int newline,const disk_t *disk_car,const partition_t *partition)
{
  const char *msg;
  msg=aff_part_aux(newline, disk_car, partition);
  screen_buffer_add("%s\n", msg);
}

void log_CHS_from_LBA(const disk_t *disk_car, const unsigned long int pos_LBA)
{
  unsigned long int tmp;
  unsigned long int cylinder, head, sector;
  tmp=disk_car->geom.sectors_per_head;
  sector=(pos_LBA%tmp)+1;
  tmp=pos_LBA/tmp;
  cylinder=tmp / disk_car->geom.heads_per_cylinder;
  head=tmp % disk_car->geom.heads_per_cylinder;
  log_info("%lu/%lu/%lu", cylinder, head, sector);
}
