/*

    File: dir.c

    Copyright (C) 1998-2007 Christophe GRENIER <grenier@cgsecurity.org>
  
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
 
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_TIME_H
#include <time.h>
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#include "types.h"
#ifdef HAVE_UTIME_H
#include <utime.h>
#endif
#include "common.h"
#include "fat.h"
#include "lang.h"
#include "fnctdsk.h"
#include "testdisk.h"
#include "intrf.h"
#ifdef HAVE_NCURSES
#include "intrfn.h"
#else
#include <stdio.h>
#endif
#include "dir.h"
#include "ext2_dir.h"
#include "fat_dir.h"
#include "ntfs_dir.h"
#include "rfs_dir.h"
#include "log.h"

const char *monstr[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
				"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

static int dir_partition_aux(disk_t *disk_car, const partition_t *partition, dir_data_t *dir_data, const unsigned long int inode, const int first_time, char **current_cmd);
static int copy_dir(disk_t *disk_car, const partition_t *partition, dir_data_t *dir_data, const file_data_t *dir);
static char ftypelet (unsigned int bits);
#ifdef HAVE_NCURSES
static long int dir_aff_ncurses(disk_t *disk_car, const partition_t *partition, dir_data_t *dir_data, const file_data_t*dir_list, const unsigned long int inode, const int first_time);
#endif

static char ftypelet (unsigned int bits)
{
#ifdef LINUX_S_ISBLK
  if (LINUX_S_ISBLK (bits))
    return 'b';
#endif
  if (LINUX_S_ISCHR (bits))
    return 'c';
  if (LINUX_S_ISDIR (bits))
    return 'd';
  if (LINUX_S_ISREG (bits))
    return '-';
#ifdef LINUX_S_ISFIFO
  if (LINUX_S_ISFIFO (bits))
    return 'p';
#endif
#ifdef LINUX_S_ISLNK
  if (LINUX_S_ISLNK (bits))
    return 'l';
#endif
#ifdef LINUX_S_ISSOCK
  if (LINUX_S_ISSOCK (bits))
    return 's';
#endif
#ifdef LINUX_S_ISMPC
  if (LINUX_S_ISMPC (bits))
    return 'm';
#endif
#ifdef LINUX_S_ISNWK
  if (LINUX_S_ISNWK (bits))
    return 'n';
#endif
#ifdef LINUX_S_ISDOOR
  if (LINUX_S_ISDOOR (bits))
    return 'D';
#endif
#ifdef LINUX_S_ISCTG
  if (LINUX_S_ISCTG (bits))
    return 'C';
#endif
#ifdef LINUX_S_ISOFD
  if (LINUX_S_ISOFD (bits))
    /* off line, with data  */
    return 'M';
#endif
#ifdef LINUX_S_ISOFL
  /* off line, with no data  */
  if (LINUX_S_ISOFL (bits))
    return 'M';
#endif
  return '?';
}

void mode_string (const unsigned int mode, char *str)
{
  str[0] = ftypelet(mode);
  str[1] = mode & LINUX_S_IRUSR ? 'r' : '-';
  str[2] = mode & LINUX_S_IWUSR ? 'w' : '-';
  str[3] = mode & LINUX_S_IXUSR ? 'x' : '-';
  str[4] = mode & LINUX_S_IRGRP ? 'r' : '-';
  str[5] = mode & LINUX_S_IWGRP ? 'w' : '-';
  str[6] = mode & LINUX_S_IXGRP ? 'x' : '-';
  str[7] = mode & LINUX_S_IROTH ? 'r' : '-';
  str[8] = mode & LINUX_S_IWOTH ? 'w' : '-';
  str[9] = mode & LINUX_S_IXOTH ? 'x' : '-';
  str[10]='\0';
#ifdef LINUX_S_ISUID
  if (mode & LINUX_S_ISUID)
  {
    if (str[3] != 'x')
      /* Set-uid, but not executable by owner.  */
      str[3] = 'S';
    else
      str[3] = 's';
  }
#endif
#ifdef LINUX_S_ISGID
  if (mode & LINUX_S_ISGID)
  {
    if (str[6] != 'x')
      /* Set-gid, but not executable by group.  */
      str[6] = 'S';
    else
      str[6] = 's';
  }
#endif
#ifdef LINUX_S_ISVTX
  if (mode & LINUX_S_ISVTX)
  {
    if (str[9] != 'x')
      /* Sticky, but not executable by others.  */
      str[9] = 'T';
    else
      str[9] = 't';
  }
#endif
}

int dir_aff_log(const disk_t *disk_car, const partition_t *partition, const dir_data_t *dir_data, const file_data_t*dir_list)
{
  int test_date=0;
  const file_data_t *current_file;
  log_partition(disk_car,partition);
  if(dir_data!=NULL)
  {
    log_info("Directory %s\n",dir_data->current_directory);
  }
  for(current_file=dir_list;current_file!=NULL;current_file=current_file->next)
  {
    struct tm		*tm_p;
    char		datestr[80];
    char str[11];
    if(current_file->filestat.st_mtime)
    {
      tm_p = localtime(&current_file->filestat.st_mtime);

      snprintf(datestr, sizeof(datestr),"%2d-%s-%4d %02d:%02d",
	  tm_p->tm_mday, monstr[tm_p->tm_mon],
	  1900 + tm_p->tm_year, tm_p->tm_hour,
	  tm_p->tm_min);
      /* FIXME: a check using current_file->name will be better */
      if(1900+tm_p->tm_year>=2000 && 1900+tm_p->tm_year<=2010)
      {
	test_date=1;
      }
    } else {
      strncpy(datestr, "                 ",sizeof(datestr));
    }
    mode_string(current_file->filestat.st_mode,str);
    log_info("%7lu ",(unsigned long int)current_file->filestat.st_ino);
    log_info("%s %5u  %5u   ", 
	str, (unsigned int)current_file->filestat.st_uid, (unsigned int)current_file->filestat.st_gid);
    log_info("%7llu", (long long unsigned int)current_file->filestat.st_size);
    log_info(" %s %s\n", datestr, current_file->name);
  }
  return test_date;
}

#ifdef HAVE_NCURSES
static long int dir_aff_ncurses(disk_t *disk_car, const partition_t *partition, dir_data_t *dir_data, const file_data_t*dir_list, const unsigned long int inode, const int first_time)
{
  /* Return value
   * -1: quit
   *  1: back
   *  other: new inode
   * */
  int quit=0;
  int offset=0;
  int pos_num=0;
  const file_data_t *current_file;
  const file_data_t *pos=dir_list;
  WINDOW *window=(WINDOW*)dir_data->display;
  aff_copy(window);
  wmove(window,4,0);
  aff_part(window,AFF_PART_ORDER,disk_car,partition);
  mvwaddstr(window,5,0,"Use ");
  if(first_time==0)
  {
    if(has_colors())
      wbkgdset(window,' ' | A_BOLD | COLOR_PAIR(0));
    waddstr(window, "Left");
    if(has_colors())
      wbkgdset(window,' ' | COLOR_PAIR(0));
    waddstr(window," arrow to go back, ");
  }
  if(has_colors())
    wbkgdset(window,' ' | A_BOLD | COLOR_PAIR(0));
  waddstr(window,"Right");
  if(has_colors())
    wbkgdset(window,' ' | COLOR_PAIR(0));
  waddstr(window," arrow to change directory, ");
  if(dir_data->copy_file!=NULL)
  {
    if(has_colors())
      wbkgdset(window,' ' | A_BOLD | COLOR_PAIR(0));
    waddstr(window,"c");
    if(has_colors())
      wbkgdset(window,' ' | COLOR_PAIR(0));
    waddstr(window," to copy, ");
  }
  if(has_colors())
    wbkgdset(window,' ' | A_BOLD | COLOR_PAIR(0));
  waddstr(window,"q");
  if(has_colors())
    wbkgdset(window,' ' | COLOR_PAIR(0));
  waddstr(window," to quit");
  wmove(window,6,0);
  wprintw(window,"Directory %s\n",dir_data->current_directory);
  do
  {
    int i;
    int car;
    for(i=0,current_file=dir_list;(current_file!=NULL) && (i<offset);current_file=current_file->next,i++);
    for(i=offset;(current_file!=NULL) &&((i-offset)<INTER_DIR);i++,current_file=current_file->next)
    {
      struct tm		*tm_p;
      char str[11];
      char		datestr[80];
      wmove(window, 8+i-offset, 0);
      wclrtoeol(window);	/* before addstr for BSD compatibility */
      if(current_file==pos)
	wattrset(window, A_REVERSE);
      if(current_file->filestat.st_mtime!=0)
      {
	tm_p = localtime(&current_file->filestat.st_mtime);
	snprintf(datestr, sizeof(datestr),"%2d-%s-%4d %02d:%02d",
	    tm_p->tm_mday, monstr[tm_p->tm_mon],
	    1900 + tm_p->tm_year, tm_p->tm_hour,
	    tm_p->tm_min);
      /* May have to use %d instead of %e */
      } else {
	strncpy(datestr, "                 ",sizeof(datestr));
      }
      mode_string(current_file->filestat.st_mode,str);
      wprintw(window, "%s %5u %5u   ", 
	  str, (unsigned int)current_file->filestat.st_uid, (unsigned int)current_file->filestat.st_gid);
      wprintw(window, "%7llu", (long long unsigned int)current_file->filestat.st_size);
      /* screen may overlap due to long filename */
      wprintw(window, " %s %s", datestr, current_file->name);
      if(current_file==pos)
	wattroff(window, A_REVERSE);
    }
    wmove(window, 8-1, 51);
    wclrtoeol(window);
    if(offset>0)
      wprintw(window, "Previous");
    /* Clear the last line, useful if overlapping */
    wmove(window,8+i-offset,0);
    wclrtoeol(window);
    wmove(window, 8+INTER_DIR, 51);
    wclrtoeol(window);
    if(current_file!=NULL)
      wprintw(window, "Next");
    if(dir_list==NULL)
    {
      wmove(window,8,0);
      wprintw(window,"No file found, filesystem seems damaged.");
    }
    wrefresh(window);
    /* Using gnome terminal under FC3, TERM=xterm, the screen is not always correct */
    wredrawln(window,0,getmaxy(window));	/* redrawwin def is boggus in pdcur24 */
    car=wgetch(window);
    wmove(window,7,0);
    wclrtoeol(window);
    switch(car)
    {
      case key_ESC:
      case 'q':
      case 'M':
	quit=1;
	break;
      case '-':
      case KEY_LEFT:
	if(first_time==0)
	  return 1;
	break;
    }
    if(dir_list!=NULL)
    {
      switch(car)
      {
	case KEY_UP:
	  if(pos->prev!=NULL)
	  {
	    pos=pos->prev;
	    pos_num--;
	  }
	  if(pos_num<offset)
	    offset--;
	  break;
	case KEY_DOWN:
	  if(pos->next!=NULL)
	  {
	    pos=pos->next;
	    pos_num++;
	  }
	  if(pos_num>=offset+INTER_DIR)
	    offset++;
	  break;
	case 'p':
	case 'P':
	case '+':
	case ' ':
	case KEY_RIGHT:
	case '\r':
	case '\n':

	case KEY_ENTER:
#ifdef PADENTER
	case PADENTER:
#endif
	  if((pos!=NULL) && (LINUX_S_ISDIR(pos->filestat.st_mode)!=0))
	  {
	    unsigned long int new_inode=pos->filestat.st_ino;
	    if((new_inode!=inode) &&(strcmp(pos->name,".")!=0))
	    {
	      if(strcmp(pos->name,"..")==0)
		return 1;
	      if(strlen(dir_data->current_directory)+1+strlen(pos->name)+1<=sizeof(dir_data->current_directory))
	      {
		if(strcmp(dir_data->current_directory,"/"))
		  strcat(dir_data->current_directory,"/");
		strcat(dir_data->current_directory,pos->name);
		return (long int)new_inode;
	      }
	    }
	  }
	  break;
	case KEY_PPAGE:
	  for(i=0;(i<INTER_DIR-1)&&(pos->prev!=NULL);i++)
	  {
	    pos=pos->prev;
	    pos_num--;
	    if(pos_num<offset)
	      offset--;
	  }
	  break;
	case KEY_NPAGE:
	  for(i=0;(i<INTER_DIR-1)&&(pos->next!=NULL);i++)
	  {
	    pos=pos->next;
	    pos_num++;
	    if(pos_num>=offset+INTER_DIR)
	      offset++;
	  }
	  break;
	case 'c':
	  if(dir_data->copy_file!=NULL)
	  {
	    unsigned int current_directory_namelength=strlen(dir_data->current_directory);
	    if(strcmp(pos->name,"..")!=0 &&
		current_directory_namelength+1+strlen(pos->name)<sizeof(dir_data->current_directory)-1)
	    {
	      if(strcmp(dir_data->current_directory,"/"))
		strcat(dir_data->current_directory,"/");
	      if(strcmp(pos->name,".")!=0)
		strcat(dir_data->current_directory,pos->name);
	      if(dir_data->local_dir==NULL)
	      {
		char *res;
		if(LINUX_S_ISDIR(pos->filestat.st_mode)!=0)
		  res=ask_location("Are you sure you want to copy %s and any files below to the directory %s ? [Y/N]",dir_data->current_directory);
		else
		  res=ask_location("Are you sure you want to copy %s to the directory %s ? [Y/N]",dir_data->current_directory);
		// free(dir_data->local_dir);
		dir_data->local_dir=res;
	      }
	      if(dir_data->local_dir!=NULL)
	      {
		int res=-1;
		wmove(window,7,0);
		wclrtoeol(window);
		if(has_colors())
		  wbkgdset(window,' ' | A_BOLD | COLOR_PAIR(1));
		wprintw(window,"Copying, please wait...");
		if(has_colors())
		  wbkgdset(window,' ' | COLOR_PAIR(0));
		wrefresh(window);
		if(LINUX_S_ISDIR(pos->filestat.st_mode)!=0)
		{
		  res=copy_dir(disk_car, partition, dir_data, pos);
		}
		else if(LINUX_S_ISREG(pos->filestat.st_mode)!=0)
		{
		  res=dir_data->copy_file(disk_car, partition, dir_data, pos);
		}
		wmove(window,7,0);
		wclrtoeol(window);
		if(res < -1)
		{
		  if(has_colors())
		    wbkgdset(window,' ' | A_BOLD | COLOR_PAIR(1));
		  wprintw(window,"Copy failed!");
		}
		else
		{
		  if(has_colors())
		    wbkgdset(window,' ' | A_BOLD | COLOR_PAIR(2));
		  if(res < 0)
		    wprintw(window,"Copy done! (Failed to copy some files)");
		  else
		    wprintw(window,"Copy done!");
		}
		if(has_colors())
		  wbkgdset(window,' ' | COLOR_PAIR(0));
	      }
	      dir_data->current_directory[current_directory_namelength]='\0';
	    }
	  }
	  break;
      }
    }
  } while(quit==0);
  return -1;
}
#endif

void delete_list_file(file_data_t *file_list)
{
  file_data_t *current_file=file_list;
  while(current_file!=NULL)
  {
    file_data_t *next=current_file->next;
    free(current_file);
    current_file=next;
  }
}

int dir_partition_aff(disk_t *disk_car, const partition_t *partition, dir_data_t *dir_data, const unsigned long int inode, char **current_cmd)
{
  if(dir_data==NULL)
    return -1;
  return dir_partition_aux(disk_car,partition,dir_data,inode,1,current_cmd);
}

static int dir_partition_aux(disk_t *disk_car, const partition_t *partition, dir_data_t *dir_data, const unsigned long int inode, const int first_time, char**current_cmd)
{
  file_data_t *dir_list;
  long int new_inode=-1;
  if(dir_data->verbose>0)
    log_info("\ndir_partition inode=%ld\n",inode);
  dir_list=dir_data->get_dir(disk_car,partition,dir_data,inode);
  dir_aff_log(disk_car, partition, dir_data, dir_list);
  do
  {
    unsigned int current_directory_namelength=strlen(dir_data->current_directory);
    if(*current_cmd==NULL)
    {
#ifdef HAVE_NCURSES
      new_inode=dir_aff_ncurses(disk_car,partition,dir_data,dir_list,inode,first_time);
#endif
    }
    if(new_inode==0 || new_inode>1)
    {
      if(dir_partition_aux(disk_car, partition, dir_data, (unsigned long int)new_inode,0,current_cmd)<0)
      { /* quit */
	delete_list_file(dir_list);
	return -1;
      }
      /* back */
      dir_data->current_directory[current_directory_namelength]='\0';
    }
  } while(new_inode==0 || new_inode>1);
  delete_list_file(dir_list);
  return new_inode;
}

/*
Returns
-2: no file copied
-1: failed to copy some files
0: all files has been copied
*/
static int copy_dir(disk_t *disk_car, const partition_t *partition, dir_data_t *dir_data, const file_data_t *dir)
{
  file_data_t *dir_list;
  unsigned int current_directory_namelength=strlen(dir_data->current_directory);
  file_data_t *current_file;
  char *dir_name;
  int copy_bad=0;
  int copy_ok=0;
  if(dir_data->get_dir==NULL || dir_data->copy_file==NULL)
    return -2;
  {
    int l1=strlen(dir_data->local_dir);
    int l2=strlen(dir_data->current_directory);
    dir_name=MALLOC(l1+l2+1);
    memcpy(dir_name,dir_data->local_dir,l1);
    filename_convert(dir_name+l1,dir_data->current_directory,l2+1);
  }
  create_dir(dir_name,1);
  dir_list=dir_data->get_dir(disk_car, partition,dir_data, (const unsigned long int)dir->filestat.st_ino);
  for(current_file=dir_list;current_file!=NULL;current_file=current_file->next)
  {
    if(strlen(dir_data->current_directory)+1+strlen(current_file->name)<sizeof(dir_data->current_directory)-1)
    {
      if(strcmp(dir_data->current_directory,"/"))
	strcat(dir_data->current_directory,"/");
      strcat(dir_data->current_directory,current_file->name);
      if(LINUX_S_ISDIR(current_file->filestat.st_mode)!=0)
      {
	int tmp=0;
	if(current_file->filestat.st_ino != dir->filestat.st_ino &&
	    strcmp(current_file->name,"..")!=0 && strcmp(current_file->name,".")!=0)
	  tmp=copy_dir(disk_car, partition, dir_data, current_file);
	if(tmp>=-1)
	  copy_ok=1;
	if(tmp<0)
	  copy_bad=1;
      }
      else if(LINUX_S_ISREG(current_file->filestat.st_mode)!=0)
      {
//	log_trace("copy_file %s\n",dir_data->current_directory);
	int tmp;
	tmp=dir_data->copy_file(disk_car, partition, dir_data, current_file);
	if(tmp==0)
	  copy_ok=1;
	else
	  copy_bad=1;
      }
      dir_data->current_directory[current_directory_namelength]='\0';
    }
  }
  delete_list_file(dir_list);
  set_date(dir_name, dir->filestat.st_atime, dir->filestat.st_mtime);
  free(dir_name);
  return (copy_bad>0?(copy_ok>0?-1:-2):0);
}

FILE *create_file(const char *filename)
{
  FILE *f_out;
  f_out=fopen(filename,"wb");
  if(!f_out)
  {
    create_dir(filename,0);
    f_out=fopen(filename,"wb");
  }
  return f_out;
}

/**
 * set_date - Set the file's date and time
 * @pathname:  Path and name of the file to alter
 * @actime:    Date and time to set
 * @modtime:   Date and time to set
 *
 * Give a file a particular date and time.
 *
 * Return:  1  Success, set the file's date and time
 *	    0  Error, failed to change the file's date and time
 */
int set_date(const char *pathname, time_t actime, time_t modtime)
{
#ifdef HAVE_UTIME
  struct utimbuf ut;
  if (!pathname)
    return 0;
  ut.actime  = actime;
  ut.modtime = modtime;
  if (utime(pathname, &ut)) {
    log_error("ERROR: Couldn't set the file's date and time for %s\n", pathname);
    return 0;
  }
  return 1;
#else
  return 0;
#endif
}
