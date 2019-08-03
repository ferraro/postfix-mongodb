/*++
/* NAME
/*	dict_mongodb 3
/* SUMMARY
/*	dictionary interface to mongodb, compatible with libmongoc-1.0
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

#include <bson/bson.h>
#include <mongoc/mongoc.h>
#include "dict_mongodb.h"

 /*
  * Structure of one mongodb dictionary handle.
  */
typedef struct {
    DICT			    dict;               /* parent class */
    CFG_PARSER		    *parser;		    /* common parameter parser */
    char			    *uri;			    /* URI like mongodb://localhost:27017 */
	char			    *dbname;		    /* database name */
	char			    *collection;	    /* collection name */
	char			    *key;			    /* key */
	char			    *value;			    /* value */
    mongoc_client_t     *mongo_client;      /* Mongo client handle */
    mongoc_uri_t        *mongo_uri;         /* Mongo URI */
    mongoc_database_t   *mongo_database;    /* Mongo database */
    mongoc_collection_t *mongo_collection;  /* Mongo collection */
	int				    connected;		    /* 1 = connected, 0 = disconnected */
} DICT_MONGODB;

 /*
  * MongoDB option defaults and names.
  */
#define DICT_MONGODB_DEF_URI		"mongodb://localhost:27017"
#define DICT_MONGODB_DEF_TIMEOUT	1000

/* Private prototypes */
int		dict_mongodb_my_connect(DICT_MONGODB *dict_mongodb);

/* mysql_parse_config - parse mysql configuration file */

static void mongodb_parse_config(DICT_MONGODB *dict_mongodb, const char *mongodbcf)
{
    CFG_PARSER	*p				= dict_mongodb->parser;
	char		*tmp;
	
	dict_mongodb->uri			= cfg_get_str(p, "uri", "", 1, 0);

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
    bson_t			*query;
    mongoc_cursor_t	*cursor;
    const           bson_t *doc;
	char			*db_coll_str;
	char			*found			= NULL;
	int				ret;
	char			*plus_name		= NULL;
	
	/* Check if there is a connection to MongoDB server */
	if (!dict_mongodb->connected) {
		// Never successfully connected, so connect now
		msg_info("connect to mongodb server: %s", dict_mongodb->uri);
		if (dict_mongodb_my_connect(dict_mongodb) != DICT_ERR_NONE) {
			msg_warn("lookup failed: no connection to mongodb server: %s", dict_mongodb->uri);
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

	query = bson_new();
    BSON_APPEND_UTF8 (query, "hello", "world");
	if (plus_name == NULL) {
        BSON_APPEND_UTF8(query, dict_mongodb->key, name);
	} else {
        BSON_APPEND_UTF8(query, dict_mongodb->key, plus_name);
	}

    cursor = mongoc_collection_find_with_opts(dict_mongodb->mongo_collection, query, NULL, NULL);
    if (mongoc_cursor_next(cursor, &doc)) {
        char *str;
        // OK
        str = bson_as_canonical_extended_json (doc, NULL);
        found = strdup(str);
        bson_free(str);
    }

    bson_destroy(query);
    mongoc_cursor_destroy(cursor);

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
    myfree(dict_mongodb->collection);
    myfree(dict_mongodb->key);

    /*
    * Release our handles and clean up libmongoc
    */
    mongoc_collection_destroy(dict_mongodb->mongo_collection);
    mongoc_database_destroy(dict_mongodb->mongo_database);
    mongoc_uri_destroy(dict_mongodb->mongo_uri);
    mongoc_client_destroy(dict_mongodb->mongo_client);
    mongoc_cleanup();

    dict_free(dict);
}

/* dict_mongodb_my_connect - connect to mongodb database */
int		dict_mongodb_my_connect(DICT_MONGODB *dict_mongodb)
{
	/*
     * Connect to mongodb database
     */
	int status;
    bson_error_t error;
	
	dict_mongodb->connected = 0;

    /*
    * Required to initialize libmongoc's internals
    */
    mongoc_init();

    /*
    * Safely create a MongoDB URI object from the given string
    */
    dict_mongodb->mongo_uri = mongoc_uri_new_with_error(dict_mongodb->uri, &error);
    if (!dict_mongodb->uri) {
        msg_warn("failed to parse URI: %s error message: %s", dict_mongodb->uri, error.message);
        DICT_ERR_VAL_RETURN(&dict_mongodb->dict, DICT_ERR_RETRY, DICT_ERR_RETRY);
    }

    /*
    * Create a new client instance
    */
    dict_mongodb->mongo_client = mongoc_client_new_from_uri(dict_mongodb->mongo_uri);
    if (!dict_mongodb->mongo_client) {
        msg_warn("connect to mongodb database failed");
        DICT_ERR_VAL_RETURN(&dict_mongodb->dict, DICT_ERR_RETRY, DICT_ERR_RETRY);
    }

    dict_mongodb->mongo_database = mongoc_client_get_database(dict_mongodb->mongo_client, dict_mongodb->dbname);
    dict_mongodb->mongo_collection = mongoc_client_get_collection(dict_mongodb->mongo_client, dict_mongodb->dbname, dict_mongodb->collection);

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
