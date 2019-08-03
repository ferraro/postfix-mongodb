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
/*	This version has been written by Stephan Ferraro, Aionda GmbH
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

/* Application-specific. */

#include <mongo-client/mongo.h>
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
	char			*value;			/* value */
	int				connected;		/* 1 = connected, 0 = disconnected */
} DICT_MONGODB;

 /*
  * MongoDB option defaults and names.
  */
#define DICT_MONGODB_DEF_HOST		"localhost"
#define DICT_MONGODB_DEF_PORT		"27017"
#define DICT_MONGODB_DEF_TIMEOUT	1000

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
	dict_mongodb->value			= cfg_get_str(p, "value", "", 1, 0);
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
	int				ret;
	int				repeat;
	char			*plus_name		= NULL;
	
	/* Check if there is a connection to MongoDB server */
	if (!dict_mongodb->connected) {
		// Never successfully connected, so connect now
		msg_info("connect to mongodb server: %s:%d", dict_mongodb->host, dict_mongodb->port);
		if (dict_mongodb_my_connect(dict_mongodb) != DICT_ERR_NONE) {
			msg_warn("lookup failed: no connection to mongodb server: %s:%d", dict_mongodb->host, dict_mongodb->port);
			DICT_ERR_VAL_RETURN(dict, DICT_STAT_ERROR, NULL);
		}
	}

	// Support Plus Addressing formats
	// Example: name+test@domain.tld should be converted to name@domain.tld
	if (strchr(name, '+') && strchr(name, '@')) {
		// Allocate a tiny amount more memory space than necessary to reduce code complexity
		plus_name			= mymalloc(strlen(name) + 1);
		// Copy complete name to new string
		strcpy(plus_name, name);
		
		// Overwrite the string from +test@domain.tld to @domain.tld
		strcpy(strchr(plus_name, '+'), strchr(plus_name, '@'));
	}

	bson_init(query);
	if (plus_name == NULL) {
		bson_append_string(query, dict_mongodb->key, name);
	} else {
		bson_append_string(query, dict_mongodb->key, plus_name);
	}
	bson_finish(query);

	/* Create string like tutorial.persons */
	db_coll_str	= mymalloc(strlen(dict_mongodb->dbname) + strlen(dict_mongodb->collection) + 2);
	memcpy(db_coll_str, dict_mongodb->dbname, strlen(dict_mongodb->dbname));
	db_coll_str[strlen(dict_mongodb->dbname)] = '.';
	memcpy(db_coll_str + strlen(dict_mongodb->dbname) + 1, dict_mongodb->collection, strlen(dict_mongodb->collection));
	db_coll_str[strlen(dict_mongodb->dbname) + strlen(dict_mongodb->collection) + 1] = 0;

	do {
		repeat	= 0;

		mongo_cursor_init(cursor, dict_mongodb->conn, db_coll_str);
		mongo_cursor_set_query(cursor, query);

		ret		= mongo_cursor_next(cursor);
		if (ret == MONGO_OK) {
			bson_iterator iterator[1];
			
			if (bson_find(iterator, mongo_cursor_bson(cursor), dict_mongodb->value)) {
				found = bson_iterator_string(iterator);
				break;
			}
		}
		// ret != MONGO_OK
		// Did we had a MongoDB connection problem (maybe wrong authentification)
		if (dict_mongodb->conn->err == MONGO_IO_ERROR) {
			msg_info("no connection to mongodb server, reconnect to: %s:%d", dict_mongodb->host, dict_mongodb->port);

			// Try to reconnect
			mongo_reconnect(dict_mongodb->conn);

			if (mongo_check_connection(dict_mongodb->conn) == MONGO_OK && dict_mongodb->conn->err == MONGO_OK) {
				// Reconnection was successfull
				dict_mongodb_auth(dict_mongodb);
				repeat = 1;
			} else {
				// Reconnect to MongoDB server failed, reject by soft error
				msg_warn("reconnect to mongodb server failed: %s:%d", dict_mongodb->host, dict_mongodb->port);
				myfree(db_coll_str);
				if (plus_name) {
					myfree(plus_name);
				}
				DICT_ERR_VAL_RETURN(dict, DICT_STAT_FAIL, NULL);
			}
		}
	} while (repeat);
	
	bson_destroy( query );
	mongo_cursor_destroy( cursor );
	myfree(db_coll_str);

	if (plus_name) {
		myfree(plus_name);
	}

	if (found) {
		// Value found in database
		dict->error = DICT_STAT_SUCCESS;
		return found;
	}
	// Value not found in database
	DICT_ERR_VAL_RETURN(dict, DICT_STAT_SUCCESS, NULL);
}

/* dict_mongodb_close - close MongoDB database */

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
	mongo_destroy(dict_mongodb->conn);
    dict_free(dict);
}

/* dict_mongodb_my_connect - connect to mongodb database */
int		dict_mongodb_auth(DICT_MONGODB *dict_mongodb)
{
	if (dict_mongodb->auth) {
		/* Authentificate to MongoDB server */
		if (mongo_cmd_authenticate(dict_mongodb->conn, dict_mongodb->dbname, dict_mongodb->username, dict_mongodb->password) == MONGO_ERROR) {
			msg_fatal("mongodb autentification failed: %s at host %s", dict_mongodb->username, dict_mongodb->host);
			DICT_ERR_VAL_RETURN(&dict_mongodb->dict, DICT_ERR_RETRY, DICT_ERR_RETRY);
		}
	}
}

/* dict_mongodb_my_connect - connect to mongodb database */
int		dict_mongodb_my_connect(DICT_MONGODB *dict_mongodb)
{
	/*
     * Connect to mongodb database
     */
	int status;
	
	dict_mongodb->connected = 0;
	status = mongo_client(dict_mongodb->conn , dict_mongodb->host, dict_mongodb->port);
	if (status != MONGO_OK) {
		switch ( dict_mongodb->conn->err ) {
			case MONGO_CONN_NO_SOCKET:
				msg_warn("connect to mongodb database failed: %s at port %d: no socket", dict_mongodb->host, dict_mongodb->port);
				break;
			case MONGO_CONN_FAIL:
				msg_warn("connect to mongodb database failed: %s at port %d", dict_mongodb->host, dict_mongodb->port);
				break;
			case MONGO_CONN_NOT_MASTER:
				msg_warn("connect to mongodb database failed: %s at port %d: not master", dict_mongodb->host, dict_mongodb->port);
				break;
		}
		DICT_ERR_VAL_RETURN(&dict_mongodb->dict, DICT_ERR_RETRY, DICT_ERR_RETRY);
	}
	/* Set timeout of 1000 ms */
	mongo_set_op_timeout(dict_mongodb->conn, DICT_MONGODB_DEF_TIMEOUT);

	/* authentificate */
	dict_mongodb_auth(dict_mongodb);
	
	dict_mongodb->connected = 1;
	DICT_ERR_VAL_RETURN(&dict_mongodb->dict, DICT_ERR_NONE, DICT_ERR_NONE);
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
	dict_mongodb_my_connect(dict_mongodb);
	
	return (DICT_DEBUG(&dict_mongodb->dict));
}
