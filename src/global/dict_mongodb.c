/*++
/* NAME
/*	dict_mongodb 3
/* SUMMARY
/*	dictionary interface to mongodb
/* SYNOPSIS
/*	#include <dict_mongodb.h>
/*
/*	DICT	*dict_mongodb_open(name, open_flags, dict_flags)
/*	const char *name;
/*	int	open_flags;
/*	int	dict_flags;
/* DESCRIPTION
/*	dict_mongodb_open() opens a mongodb, providing
/*	a dictionary interface for Postfix key->value mappings.
/*	The result is a pointer to the installed dictionary.
/*
/*	Configuration parameters are described in mongodb_table(5).
/*
/*	Arguments:
/* .IP name
/*	The path to the Postfix mongodb configuration file.
/* .IP open_flags
/*	O_RDONLY or O_RDWR. This function ignores flags that don't
/*	specify a read, write or append mode.
/* .IP dict_flags
/*	See dict_open(3).
/* SEE ALSO
/*	dict(3) generic dictionary manager
/* HISTORY
/* .ad
/* .fi
/*	This version has been written by Stephan Ferraro, Ferraro Ltd.
/*  E-Mail: stephan@ferraro.net
/* AUTHOR(S)
/*	Wietse Venema
/*	IBM T.J. Watson Research
/*	P.O. Box 704
/*	Yorktown Heights, NY 10598, USA
/*--*/

/* System library. */

#include <sys_defs.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>			/* XXX sscanf() */
#include <stdlib.h>

/* Utility library. */

#include <msg.h>
#include <mymalloc.h>
#include <dict.h>
#include <vstring.h>
#include <stringops.h>
#include <auto_clnt.h>
#include <vstream.h>

/* Global library. */

#include "cfg_parser.h"

#define MONGO_HAVE_STDINT	1
#define MONGO_HAVE_UNISTD	1
#define MONGO_USE__INT64	1
#include <mongo.h>

/* Application-specific. */

#include <mongo.h>
#include "dict_mongodb.h"

 /*
  * Structure of one mongodb dictionary handle.
  */
typedef struct {
    DICT			dict;           /* parent class */
    CFG_PARSER		*parser;		/* common parameter parser */
    char			*host;			/* hostname */
    unsigned int	port;			/* port */
    unsigned int	auth;			/* 1 if authentification should be used, otherwise 0 */
	char			*username;		/* username if auth == 1 */
	char			*password;		/* password if auth == 1 */
	char			*dbname;		/* database name */
	char			*collection;	/* collection */
	char			*key;			/* key */
	mongo			conn[1];		/* MongoDB connection structure */
} DICT_MONGODB;

 /*
  * MongoDB option defaults and names.
  */
#define DICT_MONGODB_DEF_HOST	"localhost"
#define DICT_MONGODB_DEF_PORT	"27017"

/* Private prototypes */
int		dict_mongodb_my_connect(DICT_MONGODB *dict_mongodb);

/* mysql_parse_config - parse mysql configuration file */

static void mongodb_parse_config(DICT_MONGODB *dict_mongodb, const char *mongodbcf)
{
    CFG_PARSER	*p				= dict_mongodb->parser;
	char		*tmp;
	
	dict_mongodb->host			= cfg_get_str(p, "host", "", 0, 0);

	tmp							= cfg_get_str(p, "port", "", 0, 0);
	dict_mongodb->port			= atoi(tmp);
	myfree(tmp);

	tmp							= cfg_get_str(p, "auth", "", 0, 0);
	dict_mongodb->auth			= atoi(tmp);
	myfree(tmp);

    dict_mongodb->username		= cfg_get_str(p, "user", "", 0, 0);
    dict_mongodb->password		= cfg_get_str(p, "password", "", 0, 0);
    dict_mongodb->dbname		= cfg_get_str(p, "dbname", "", 1, 0);
	dict_mongodb->collection	= cfg_get_str(p, "collection", "", 1, 0);
	dict_mongodb->key			= cfg_get_str(p, "key", "", 1, 0);
}

/* dict_mysql_lookup - find database entry, for the moment, it supports only key/value strings */
/* TODO: Check if connection is still alive, if not, auto-reconnect */

static const char *dict_mongodb_lookup(DICT *dict, const char *name)
{
    DICT_MONGODB	*dict_mongodb = (DICT_MONGODB *) dict;
	bson			query[1];
	mongo_cursor	cursor[1];
	char			*db_coll_str;
	char			*found			= NULL;
	
	bson_init(query);
	bson_append_string(query, dict_mongodb->key, name);
	bson_finish(query);
	
	/* Create string like tutorial.persons */
	db_coll_str	= mymalloc(strlen(dict_mongodb->dbname) + strlen(dict_mongodb->collection) + 2);
	memcpy(db_coll_str, dict_mongodb->dbname, strlen(dict_mongodb->dbname));
	db_coll_str[strlen(dict_mongodb->dbname)] = '.';
	memcpy(db_coll_str + strlen(dict_mongodb->dbname) + 1, dict_mongodb->collection, strlen(dict_mongodb->collection));
	db_coll_str[strlen(dict_mongodb->dbname) + strlen(dict_mongodb->collection) + 1] = 0;

	mongo_cursor_init(cursor, dict_mongodb->conn, db_coll_str);
	mongo_cursor_set_query(cursor, query);
	
	while (mongo_cursor_next(cursor) == MONGO_OK) {
		bson_iterator iterator[1];
		
		if (bson_find(iterator, mongo_cursor_bson(cursor), dict_mongodb->key)) {
			found = bson_iterator_string(iterator);
			break;
		}
	}
	bson_destroy( query );
	mongo_cursor_destroy( cursor );
	myfree(db_coll_str);

	return found;
}

/* dict_mysql_close - close MYSQL database */

static void dict_mongodb_close(DICT *dict)
{
    DICT_MONGODB *dict_mongodb = (DICT_MONGODB *) dict;
	
    cfg_parser_free(dict_mongodb->parser);
    myfree(dict_mongodb->host);
	if (dict_mongodb->auth) {
		myfree(dict_mongodb->username);
		myfree(dict_mongodb->password);
	}
    myfree(dict_mongodb->collection);
    myfree(dict_mongodb->key);
    dict_free(dict);
	// mongo_destroy( conn );
}

/* dict_mongodb_my_connect - connect to mongodb database */
int		dict_mongodb_my_connect(DICT_MONGODB *dict_mongodb)
{
	/*
     * Connect to mongodb database
     */
	if (mongo_client(dict_mongodb->conn , dict_mongodb->host, dict_mongodb->port)) {
		msg_fatal("connect to mongodb database failed: %s at port %d", dict_mongodb->host, dict_mongodb->port);
		dict_mongodb_close((DICT *)dict_mongodb);
		return -1;
	}
	if (dict_mongodb->auth) {
		/* Authentificate to MongoDB server */
		if (mongo_cmd_authenticate(dict_mongodb->conn, dict_mongodb->dbname, dict_mongodb->username, dict_mongodb->password) == MONGO_ERROR) {
			msg_fatal("mongodb autentification failed: %s at host %s", dict_mongodb->username, dict_mongodb->host);
			dict_mongodb_close((DICT *)dict_mongodb);
			return -1;
		}
	}
	return 0;
}

/* dict_mongodb_open - open memcache */

DICT   *dict_mongodb_open(const char *name, int open_flags, int dict_flags)
{
    DICT_MONGODB	*dict_mongodb;
    CFG_PARSER		*parser;

    /*
     * Open the configuration file.
     */
    if ((parser = cfg_parser_alloc(name)) == 0)
		return (dict_surrogate(DICT_TYPE_MONGODB, name, open_flags, dict_flags,
			       "open %s: %m", name));

    /*
     * Create the dictionary object.
     */
    dict_mongodb = (DICT_MONGODB *)dict_alloc(DICT_TYPE_MONGODB, name, sizeof(*dict_mongodb));
	/* Pass pointer functions */
    dict_mongodb->dict.lookup	= dict_mongodb_lookup;
	dict_mongodb->dict.close	= dict_mongodb_close;
	dict_mongodb->dict.flags	= dict_flags;
	dict_mongodb->parser		= parser;
	mongodb_parse_config(dict_mongodb, name);
	dict_mongodb->dict.owner	= cfg_get_owner(dict_mongodb->parser);

	if (dict_mongodb_my_connect(dict_mongodb) == -1) {
		return NULL;
	}
	
	return (DICT_DEBUG(&dict_mongodb->dict));
}
