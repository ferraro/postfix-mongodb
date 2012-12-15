Postfix for MongoDB
===================

Postfix for MongoDB is a fork of the famous Postfix SMTP daemon.
It allows to store virtual email addresses in a MongoDB database.

So the difference between the official and this version is that
the MongoDB dict lookup driver has been included.
As long the C Driver of MongoDB (libmongoc) is not yet realized as
official packages for main-stream Linux and BSD distributions,
this dictionnary driver can not be included in the official version
of Postfix daemon.

This is the first version, with very basic functions, so please
consider it as alpha version and only be used by advanced and
experienced users of Postfix with C programming knowledges.

In future, it would be good that this driver would be included in
the official postfix release.

The postfix version used here is 2.5.4.

If you need additional extra-features, please contact me at
contact@ferraro.net.

Example of a /etc/postfix/mmongodb-aliases.cf file:

	#
	# MongoDB Postfix Alias File
	# (C) Stephan Ferraro <stephan@ferraro.net>, Ferraro Ltd.
	#

	# Hostname
	host = 127.0.0.1

	# Port
	port = 27017

	# Use authentification to log into mongodb
	mongo_auth = 1
	mongo_user = root
	mongo_password = root

	# The database name on the servers.
	dbname = test

	# Search query by key and value
	# Example:
	# collection = address
	# key = name
	#
	# means in MongoDB search query: db.address.find({name: full_email_address});
	# The value with the full_email_address is filled up by Postfix
	collection = address
	key = name

The file can then be included so in the main.cf file:

	virtual_mailbox_maps = mongodb:/etc/postfix/mongodb-aliases.cf

Installation
========

1. Download libmongoc version v0.7 at https://github.com/mongodb/mongo-c-driver/zipball/v0.7, compile it and then run make install to install it at /usr/local/lib
2. Install Ubuntu Linux Server 12.10 (maybe it works too on other Linux distributions, but I have not tested it)
3. Compile this postfix source code, on Ubuntu Linux you need to run:

		make tidy
		make makefiles CCARGS="-DUSE_TLS"
		make SYSLIBS="-L/usr/lib/x86_64-linux-gnu -lssl -lcrypto -lpcre -ldb -lnsl -lresolv -L/usr/local/lib -lmongoc"
		make install

4. Start MongoDB server
5. Configure Postfix configuration files, specially add in main.cf the entry "virtual_mailbox_maps = mongodb:/etc/postfix/mongodb-aliases.cf" and add the file /etc/postfix/mongodb-aliases.cf
6. Start Postfix server and check log file at /var/log/mail.log for debugging purposes