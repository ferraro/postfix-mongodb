#ifndef _DICT_MONGODB_INCLUDED_
#define _DICT_MONGODB_INCLUDED_

/*++
/* NAME
/*	dict_mongodb 3h
/* SUMMARY
/*	dictionary interface to mongodb databases
/* SYNOPSIS
/*	#include <dict_mongodb.h>
/* DESCRIPTION
/* .nf

 /*
  * Utility library.
  */
#include <dict.h>

 /*
  * External interface.
  */
#define DICT_TYPE_MONGODB "mongodb"

extern DICT *dict_mongodb_open(const char *name, int unused_flags,
				        int dict_flags);

/* LICENSE
/* .ad
/* .fi
/*	The Secure Mailer license must be distributed with this software.
/* AUTHOR(S)
/*	Wietse Venema
/*	IBM T.J. Watson Research
/*	P.O. Box 704
/*	Yorktown Heights, NY 10598, USA
/*--*/

#endif
